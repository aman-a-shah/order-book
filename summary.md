# Low-Latency Limit Order Book — Project Summary

A complete, self-contained briefing on this repository: the problem, the architecture, every
component's implementation, the governing math, the benchmark methodology and results, and the
editorial research-note PDF that ships with it. Reading this should leave you able to explain or
extend any part of the system without opening the source.

---

## 1. Problem & goals

Build an **ultra-low-latency, single-threaded limit order book (LOB) and matching engine in C++17**,
decoupled from a mock market feed by a lock-free single-producer/single-consumer (SPSC) queue. The
design exists to demonstrate the systems ideas that matter in production market infrastructure and
to be defensible in a quant-systems interview (Citadel / Jane Street style).

**Performance targets (from `Plan.md`):**
- **Throughput** ≥ 2,000,000 order events/sec on a single thread.
- **Latency** mean end-to-end processing < 200 ns (hardware-counter timed).
- **Cache efficiency**: contiguous memory layout, minimal L1/L2 misses.
- **No-allocation rule**: zero `new`/`malloc` on the hot path; all memory pre-allocated at startup.

**Core design bets (the interview talking points):**
- **Flat contiguous arrays over trees/maps.** `std::map`/`std::unordered_map` scatter nodes across
  the heap and stall the CPU on pointer chases. A short, sorted, contiguous price-level array fits in
  L1 and is scanned with SIMD faster than a tree returns its first dereference. O(N) on a tiny cache-
  resident N beats O(1) with a cache miss.
- **SPSC, not MPMC.** One feed → one matching thread means no multi-core CAS contention / cache-line
  bouncing. Synchronization is two atomic cursors with acquire/release ordering only.
- **Bounded, deterministic memory.** Fixed-capacity object pool + open-addressed index + fixed price-
  level arrays, all sized at construction. Working set is known ahead of time.
- **Core isolation.** Feed handler and matching engine pinned to dedicated cores (Linux) to remove
  scheduler jitter.

The implementation is intentionally small (header-only C++17, plain Makefile, no CMake/bundler) so it
can be read end to end.

---

## 2. Architecture

```
[ Feed handler thread ]  (core 0)
        │  push raw 40-byte messages
        ▼
┌─────────────────────────────────────┐
│  Lock-free SPSC ring buffer          │   power-of-two capacity, std::atomic
│  (alignas(64) head/tail cursors)     │   cursors, acquire/release ordering
└─────────────────────────────────────┘
        │  pop bytes
        ▼
[ Matching engine thread ]  (core 1)
        ├─► decode binary message
        ├─► route by symbol_id (MultiBookRouter)
        └─► OrderBook.process():
              ├─ flat sorted PriceLevel arrays (SIMD price scan)
              ├─ ObjectPool arena (intrusive doubly-linked orders)
              └─ open-addressed OrderIndex (cancel/modify lookup)
```

Two execution domains, isolated on separate cores, joined only by the ring buffer.

---

## 3. Components (implementation detail)

All headers live in `include/lob/`. Everything is header-only and templated on capacities.

### 3.1 Types (`types.hpp`)
- `Side` (Buy=0, Sell=1), `OrderType` (Limit=0, Market=1, Cancel=2, Modify=3, Replace=4) — `uint8_t`.
- `OrderCommand` — the input: type, side, order_id (u64), price (u32), quantity (u32), new_order_id
  (u64, for replace), symbol_id (u32).
- `Order` — resting order: order_id, price, quantity, side, intrusive `next`/`prev` pointers.
- `PriceLevel` — `alignas(32)`, **exactly 32 bytes** (`static_assert(sizeof==32)`): price,
  total_volume, head_order, tail_order, order_count. 32 B = one 256-bit SIMD load / half a cache line.
- `ExecutionReport` — result: filled_quantity, trade_count, notional, and flags accepted / resting /
  canceled / modified / replaced / rejected / trade_log_full.
- `Trade` — sequence, taker_order_id, maker_order_id, price, quantity, aggressor_side.
- `DepthLevel` — price, total_volume, order_count (the snapshot view; internal storage never exposed).

### 3.2 Order book (`order_book.hpp`)
`OrderBook<MaxOrders, MaxPriceLevels, IndexCapacity>`. Single-threaded; one instance per symbol.

- **Storage**: two `std::array<PriceLevel, MaxPriceLevels>` (`bids_`, `asks_`) kept sorted —
  **bids descending, asks ascending — so the best price is always index 0**. Parallel
  `std::array<uint32_t, MaxPriceLevels>` arrays (`bid_prices_`, `ask_prices_`) hold just the prices,
  packed contiguously for the SIMD scan. All arrays `alignas(64)`.
- **Insertion** (`append_bid`/`append_ask`): linear scan to the sorted insertion index; if the price
  level exists, link the order at the tail (FIFO) and add volume; otherwise shift the array up by one
  and create the level. Empty levels are erased by shifting down.
- **Orders within a level**: intrusive doubly-linked list (`head_order`/`tail_order`). Appending at
  the tail and matching from the head gives **time priority** for free.
- **Matching** (`match_against_asks`/`match_against_bids` → `match_level`): while quantity remains and
  the best opposing level crosses the limit price, fill FIFO from the head order; partial fills reduce
  quantity in place, full fills unlink + release the order and record a `Trade`.
- **Order operations**:
  - *Limit*: match marketable portion, rest the remainder (allocate from pool, insert into index +
    level). Self-rejects duplicate order ids.
  - *Market*: match against the opposite side with no price bound (limit = MAX for buy, 0 for sell).
  - *Cancel*: index lookup → unlink from level → release to pool → erase from index.
  - *Modify*: **reduce quantity only, keeping time priority** (no unlink). Rejects increases.
  - *Replace* (cancel-replace): cancel then re-insert as a new limit — **forfeits time priority**,
    optionally takes a new order id.
- **Depth**: `bid_depth`/`ask_depth` copy the top-N levels into a caller-supplied `DepthLevel` array.
- **SIMD entry points**: `first_crossing_ask`/`first_crossing_bid` call into `simd::`.

### 3.3 Object pool (`object_pool.hpp`)
`ObjectPool<Capacity>` — a `std::array<Order, Capacity>` arena with a LIFO free-list of indices.
`allocate` pops a free index and initializes the order in place; `release` pushes it back. **No heap
allocation ever occurs on the hot path.** O(1) allocate/release.

### 3.4 Order index (`order_index.hpp`)
`OrderIndex<Capacity>` — open-addressed hash table, **power-of-two capacity** (mask instead of modulo),
**linear probing**, tombstone deletion (`deleted` flag), 64-bit MurmurHash3 finalizer for the hash.
Maps order_id → `Order*` for O(1)-ish cancel/modify/replace lookup. Lives cache-local, not on the heap.

### 3.5 SPSC queue (`spsc_queue.hpp`)
`SpscQueue<T, Capacity>` — lock-free ring buffer. `static_assert` enforces power-of-two capacity.
- Two cursors, each a `struct alignas(64) Cursor { std::atomic<uint64_t> value; }` — **separate cache
  lines to avoid false sharing** between producer and consumer.
- `push`: relaxed-load own `tail`, **acquire**-load `head`; full iff `tail - head == Capacity`; write
  `buffer_[tail & mask_]`; **release**-store `tail + 1`.
- `pop`: relaxed-load own `head`, **acquire**-load `tail`; empty iff `head == tail`; read
  `buffer_[head & mask_]`; **release**-store `head + 1`.
- No mutex, no CAS — single producer and single consumer make plain load/store with acquire/release
  sufficient.

### 3.6 SIMD price scan (`simd_scan.hpp`)
`find_first_ask_at_or_below(prices, count, limit)` and `find_first_bid_at_or_above(...)`. Three paths
selected at compile time:
- **AVX2** (x86): `_mm256_loadu_si256` loads 8 packed `int32` prices, `_mm256_cmpgt_epi32` compares,
  `_mm256_movemask_ps` + `__builtin_ctz` reads the first matching index. 8 levels per instruction.
- **NEON** (ARM/Apple Silicon): `vld1q_u32` + `vcleq_u32`/`vcgeq_u32`, 4 lanes per instruction.
- **Scalar** fallback elsewhere.
Each path has a scalar tail for the remainder. The contiguous `bid_prices_`/`ask_prices_` arrays are
what make this vectorizable.

### 3.7 Binary protocol (`binary_protocol.hpp`)
Fixed-width **40-byte little-endian** record (`BinaryMessageSize = 40`):

| offset | bytes | field |
|---|---|---|
| 0 | 1 | type |
| 1 | 1 | side |
| 2 | 8 | order_id |
| 10 | 4 | price |
| 14 | 4 | quantity |
| 18 | 8 | new_order_id |
| 26 | 4 | symbol_id |
| 30 | 4 | magic = `0x4F424C31` (ASCII `"OBL1"`) |
| 34 | 6 | reserved |

`encode_binary_order`/`decode_binary_order` use manual byte shifts (endian-independent). Decode
**validates the magic word and the type/side ranges** before admitting a message — a corrupt frame is
rejected, not matched. Fixed width means the decoder never branches on length and the producer never
allocates.

### 3.8 Multi-symbol routing (`multi_book.hpp`)
`MultiBookRouter<SymbolCount, MaxOrdersPerSymbol, MaxPriceLevelsPerSymbol, IndexCapacityPerSymbol>` —
a `std::array<OrderBook, SymbolCount>`. `process` routes by `symbol_id` (rejecting out-of-range) to an
independent, isolated book per instrument.

### 3.9 Timing & threading (`timing.hpp`, `threading.hpp`)
- `read_cycle_counter()`: `__rdtsc()` on x86, `mrs cntvct_el0` (virtual counter) on AArch64, `chrono`
  fallback otherwise. **The unit is architecture-specific** — ARM ticks ≠ x86 TSC cycles ≠ ns.
- `Stopwatch`: wall time via `std::chrono::steady_clock`.
- `pin_current_thread_to_core(core)`: `pthread_setaffinity_np` on Linux; no-op elsewhere.

### 3.10 Histogram (`histogram.hpp`)
`PowerOfTwoHistogram<BucketCount=64>` — latency goes into bucket `floor(log2(value))`; bucket `b`'s
upper bound is `2^(b+1) - 1`. Provides `mean`, `max`, `percentile(p)` (rank-based over buckets),
per-bucket accessors, and `merge` (used to combine repeated runs). Cheap, allocation-free, good for
spiky tail distributions.

### 3.11 Replay (`replay.hpp`)
CSV parser → `vector<ReplayEvent>` where `ReplayEvent = {timestamp_ns, OrderCommand}`. Lines are
`timestamp,type,side,order_id,price,quantity,new_order_id[,symbol_id]` (7 or 8 fields); `#` comments
and blank lines are skipped; type/side accept short or long tokens (`L`/`LIMIT`, `B`/`BUY`, …).

---

## 4. Executables (`src/`) & how to run (`Makefile`)

| Target | Source | What it does |
|---|---|---|
| `make test` | `tests/*.cpp` | Order-book correctness, binary round-trip, golden-file replay |
| `make bench` | `latency_bench.cpp` | 2M alternating add/cancel, per-event cycle-counter latency histogram |
| `make pipeline` | `pipeline_demo.cpp` | Producer(core 0) → SPSC → consumer(core 1), throughput |
| `make replay` | `replay_runner.cpp` | Human-readable replay summary (used by the golden test) |
| `make replay-bench` | `replay_bench.cpp` | Binary decode → route → match over a repeated stream |
| `make visualize` | `visualizer.cpp` | Per-event trace + final depth ladder + trade tape |
| `make tsan` | `pipeline_demo.cpp` | Same pipeline built with `-fsanitize=thread` |
| `make site` | `site_export.cpp` | Runs **all** workloads + a depth scenario, emits JSON (feeds the PDF) |

Build flags: `-std=c++17 -O3 -DNDEBUG -Wall -Wextra -Wpedantic`; on x86 Linux add `-march=native
-mavx2` for the AVX2 path. `make all` = test + bench + pipeline + replay.

`tests/`: `order_book_tests.cpp` (matching, partial/full fills, price-time priority, modify-keeps-
priority, replace-loses-priority, depth, multi-symbol isolation, SPSC FIFO), `protocol_tests.cpp`
(40-byte encode/decode round-trip), `replay_tests.cpp` (replay output vs committed
`tests/replay_expected.txt`). `data/sample_replay.csv` is the deterministic stream.

---

## 5. Governing math

**SPSC ring index & state** (capacity `N = 2^k`):
$$\operatorname{slot}(i) = i \bmod N = i \mathbin{\&} (N-1), \qquad \text{full} \iff \text{tail}-\text{head}=N, \qquad \text{empty} \iff \text{tail}=\text{head}.$$
Correctness rests on a release/acquire **happens-before** edge: the producer's `store(tail, release)`
synchronizes-with the consumer's `load(tail, acquire)`, so the buffer write is visible before the slot
is dequeued — the minimum barrier the hardware needs.

**Price-time priority** (for two resting bids `a, b`; price comparison reversed for asks):
$$a \prec b \iff p_a > p_b \;\lor\; (p_a = p_b \,\wedge\, t_a < t_b).$$

**Bounded memory** (all fixed at construction):
$$M = N_o\cdot\operatorname{sizeof}(\text{Order}) + L\cdot\operatorname{sizeof}(\text{PriceLevel}) + C\cdot\operatorname{sizeof}(\text{Entry}).$$

**Latency histogram bucketing:**
$$b(x) = \lfloor \log_2 x \rfloor, \qquad \text{bucket } b \text{ covers } (2^{b},\, 2^{b+1}].$$

**Throughput:** $T = E / \Delta t$ (events processed / wall seconds).

---

## 6. Results (measured, machine-dependent)

Measured on Apple M4 / Apple clang 17 (NEON path). **All figures are emitted by the engine, never
hand-written**; throughput is noisy on a loaded laptop, so the page/report use medians of repeated
runs.

- **Median per-event latency: ~63 counter ticks (p50)**, mean tens of ticks, p99 ~255–511 ticks. The
  distribution is bimodal (a spike of sub-tick fast paths + a mode around the low-tick octaves) with a
  thin tail from scheduler interrupts the single thread cannot mask.
- **Throughput clears the 2M ev/s target on every workload**: single-thread add/cancel hot path
  ~5–15M/s, binary decode→route→match replay ~3–7M/s, two-thread SPSC pipeline ~3–5M/s (median of a
  50k–1M event size sweep; smaller runs read higher because the working set stays warm in cache).
- **Correctness**: all test suites pass; deterministic replay matches the golden file byte-for-byte.
- **Race-freedom**: ThreadSanitizer over 10,000 events through producer→ring→consumer reports
  **0 data races** — empirical backing for the acquire/release reasoning, not a substitute for it.
- **Structural facts**: `PriceLevel` = 32 B, `Order` = 40 B, message = 40 B, ring capacity = 65,536
  slots, latency-config book arena ≈ 8.6 MB resident, allocated once.

**Honesty caveat**: counter ticks are architecture-specific (ARM virtual counter here), so they are
honest *relative* magnitudes, not nanoseconds. A nanosecond claim requires pinned/isolated cores,
disabled frequency scaling, and conversion via the measured invariant counter frequency — see
`PERFORMANCE.md` for the Linux x86 `perf`/`cachegrind` workflow.

---

## 7. The research-note PDF (`site/`)

An editorial, research-paper-style PDF (`site/low-latency-order-book.pdf`, 7 pages) that walks the
engine end to end with typeset math and hand-rolled SVG charts, built per the design spec in
`QUANT_SHOWCASE_STYLE.md`. **Every figure is generated from a real engine run.**

**Pipeline**: `src/site_export.cpp` (`make site`) runs the add/cancel hot path (5 merged runs), the
binary replay, the threaded SPSC pipeline swept over size, and a constructed-but-real depth-ladder
scenario, emitting a single JSON object. `site/export_data.py` runs it 3× (keeping the median-
throughput run), also runs the tests + TSAN, stamps platform/compiler/git/date metadata, and writes
`site/data.json`. `site/build_pdf.sh` regenerates data, serves the folder over HTTP (needed for
`fetch`), and prints `index.html` → PDF via headless Chrome.

**Source files** (the canonical deliverable): `site/index.html` (structure + KaTeX math),
`site/styles.css` (OKLCH design tokens + a `@media print` block), `site/charts.js` (dependency-free
SVG chart engine + 7 renderers), `site/export_data.py`, `site/build_pdf.sh`. Figures: latency CDF
(hero), 40-byte message map, cumulative depth ladder, log-scale latency histogram with p50/p99
markers, two-series tail-percentile curve, throughput-vs-2M-target bars, and a pipeline scaling sweep;
plus a verification matrix and trade-tape table.

**Two non-obvious fixes made during the build (documented in-code):**
1. **KaTeX print hang.** `p { text-wrap: pretty }` is inherited into KaTeX's deeply-nested span tree
   and sends Chrome's line-break optimizer into a hang during the headless print pass (`--print-to-pdf`
   never captures). Fix: `.equation, .katex, .katex * { text-wrap: nowrap; }`.
2. **Throughput honesty.** The pipeline's single 1M-event run is the noisiest and can dip below the 2M
   target; the report uses the **median of the size sweep** (the full sweep is shown transparently in
   the scaling figure) so the "clears 2M on every workload" claim holds.
`build_pdf.sh` was also hardened against a `set -e` early-exit in its poll loop and against this
machine's Chrome spawning a persistent updater that prevents the process from exiting.

---

## 8. Reproduce everything

```sh
make test            # correctness (order book, protocol, golden replay)
make tsan            # 0 data races over the threaded pipeline
make bench           # latency histogram
make replay-bench    # binary decode → route → match throughput
make visualize       # depth ladder + trade tape trace
make site            # engine → JSON (for the PDF)

PYTHONPATH=src python3 site/export_data.py   # assemble site/data.json (+ tests, TSAN, metadata)
bash site/build_pdf.sh                        # regenerate data, then print the PDF
python3 site/verify_pdf.py                    # per-page whitespace/gap check on the PDF
```

For the strongest latency/cache claims, run on isolated Linux x86 with `-march=native -mavx2`, pinned
cores, disabled frequency scaling, and `perf stat` / `valgrind --tool=cachegrind` (commands in
`PERFORMANCE.md`).

---

## 9. File map

```
include/lob/   types · order_book · object_pool · order_index · spsc_queue ·
               simd_scan · binary_protocol · multi_book · timing · threading · histogram · replay
src/           latency_bench · pipeline_demo · replay_runner · replay_bench · visualizer · site_export
tests/         order_book_tests · protocol_tests · replay_tests · replay_expected.txt
data/          sample_replay.csv
site/          index.html · styles.css · charts.js · export_data.py · build_pdf.sh ·
               data.json · low-latency-order-book.pdf · verify_pdf.py
docs           README.md · PERFORMANCE.md · Plan.md (PRD) · QUANT_SHOWCASE_STYLE.md · summary.md
Makefile       plain make (no CMake); -O3 -std=c++17, optional -mavx2 -march=native
```
