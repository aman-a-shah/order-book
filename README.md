# Low-Latency Limit Order Book

A single-threaded C++17 limit order book and matching engine with a lock-free SPSC feed pipeline, fixed-capacity object pools, contiguous price-level storage, SIMD-assisted price scans, binary message parsing, deterministic replay, multi-symbol routing, market-depth snapshots, and local performance/TSAN verification targets.

The implementation is intentionally small enough to defend in an interview, but it uses the same systems ideas that matter in production market infrastructure: bounded memory, cache locality, acquire/release atomics, price-time priority, and benchmarkable latency claims.

## Build And Run

This project uses a plain Makefile because the local machine does not have CMake installed.

```sh
make test
make bench
make pipeline
make replay
make replay-bench
make visualize
make tsan
```

`make all` runs the test suite, the latency benchmark, the threaded pipeline demo, and the deterministic replay.

## Current Local Results

Measured on the current Apple Silicon laptop:

```text
make all
All order book and queue tests passed
latency_bench throughput_events_per_second ~= 5M-6M
pipeline_demo throughput_events_per_second ~= 7M-11M
replay_bench throughput_events_per_second ~= 9M-10M
make tsan processed 10,000 events with no reported data races
```

The benchmark reports `mean_cycles_or_ticks` because the counter source is platform-specific: x86 uses `__rdtsc()`, while Apple Silicon uses the ARM virtual counter. For nanosecond claims on a production Linux x86 workstation, pin threads, isolate cores, disable frequency scaling, and convert TSC cycles using the measured invariant TSC frequency.

## Project Layout

```text
include/lob/types.hpp        Core command, order, price-level, and report types
include/lob/binary_protocol.hpp  40-byte little-endian feed message codec
include/lob/histogram.hpp    Fixed-size latency histogram
include/lob/multi_book.hpp   One order book per symbol with symbol-id routing
include/lob/object_pool.hpp  Fixed-size startup arena for order records
include/lob/order_index.hpp  Fixed-capacity open-addressed order-id lookup table
include/lob/order_book.hpp   Matching engine, price-time priority, cancels
include/lob/replay.hpp       CSV replay parser
include/lob/spsc_queue.hpp   Lock-free single-producer single-consumer ring buffer
include/lob/simd_scan.hpp    AVX2 / NEON / scalar price scan helpers
include/lob/timing.hpp       Hardware counter and wall-clock helpers
include/lob/threading.hpp    Linux CPU-affinity helper, no-op elsewhere
data/sample_replay.csv       Deterministic audit/replay stream
tests/order_book_tests.cpp   Deterministic correctness tests
tests/protocol_tests.cpp     Binary protocol round-trip test
tests/replay_tests.cpp       Golden-file replay test
src/latency_bench.cpp        Hot-path add/cancel latency benchmark
src/pipeline_demo.cpp        Producer thread -> SPSC queue -> matching thread demo
src/replay_runner.cpp        Human-readable replay summary
src/replay_bench.cpp         Binary decode -> symbol route -> match benchmark
src/visualizer.cpp           Terminal market-depth and trade replay demo
```

## Design Highlights

- Price levels are fixed-size contiguous arrays, not `std::map` or `std::unordered_map`.
- Orders are allocated from a startup `std::array` arena and linked intrusively at each price level.
- Cancels use a fixed-capacity open-addressed hash table.
- Modify orders can reduce quantity without losing time priority.
- Cancel-replace orders lose time priority and can optionally receive a new order id.
- Trades are recorded in a fixed-capacity trade log.
- Top-N market-depth snapshots are available without exposing internal storage.
- Multi-symbol routing stores one independent book per instrument.
- Binary messages are 40-byte fixed-width little-endian records.
- The SPSC queue uses only `std::atomic<std::uint64_t>` cursors with acquire/release ordering.
- Producer and consumer cursors are separated with `alignas(64)` to reduce false sharing.
- SIMD helpers use AVX2 on x86, NEON on ARM, and scalar fallback elsewhere.
- ThreadSanitizer target validates the producer-consumer path.

## Demo Commands

```sh
make visualize
make replay
make replay-bench
```

`make visualize` prints each replayed event, final top-of-book depth, and the resulting trade tape.

For deeper measurement notes, see [PERFORMANCE.md](PERFORMANCE.md).

## Research Note (PDF)

`site/` holds an editorial research-note PDF that walks the engine end to end — feed
protocol, lock-free transport, matching/memory layout, correctness, and benchmarked
latency/throughput — with typeset math and hand-rolled SVG charts. **Every figure is
generated from a real engine run; no number is hand-written.**

```sh
make site                       # build/site_export -> JSON (runs the real engine)
PYTHONPATH=src python3 site/export_data.py   # assemble site/data.json (+ tests, TSAN)
bash site/build_pdf.sh          # regenerate data, then print site/low-latency-order-book.pdf
```

`build/site_export` runs the add/cancel hot path, the binary decode→route→match replay,
the threaded SPSC pipeline (swept over size), and a snapshotted depth-ladder scenario,
emitting the histogram, throughput, and depth data the page binds to. The page is a single
HTML/CSS/JS document printed to PDF through headless Chrome.
