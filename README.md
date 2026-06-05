# Low-Latency Limit Order Book

A single-threaded C++17 limit order book and matching engine with a lock-free SPSC feed pipeline, fixed-capacity object pools, contiguous price-level storage, SIMD-assisted price scans, and local performance/TSAN verification targets.

The implementation is intentionally small enough to defend in an interview, but it uses the same systems ideas that matter in production market infrastructure: bounded memory, cache locality, acquire/release atomics, price-time priority, and benchmarkable latency claims.

## Build And Run

This project uses a plain Makefile because the local machine does not have CMake installed.

```sh
make test
make bench
make pipeline
make tsan
```

`make all` runs the test suite, the latency benchmark, and the threaded pipeline demo.

## Current Local Results

Measured on the current Apple Silicon laptop:

```text
make all
All order book and queue tests passed
latency_bench throughput_events_per_second ~= 5.8M-6.3M
pipeline_demo throughput_events_per_second ~= 10M-11M
make tsan processed 10,000 events with no reported data races
```

The benchmark reports `mean_cycles_or_ticks` because the counter source is platform-specific: x86 uses `__rdtsc()`, while Apple Silicon uses the ARM virtual counter. For nanosecond claims on a production Linux x86 workstation, pin threads, isolate cores, disable frequency scaling, and convert TSC cycles using the measured invariant TSC frequency.

## Project Layout

```text
include/lob/types.hpp        Core command, order, price-level, and report types
include/lob/object_pool.hpp  Fixed-size startup arena for order records
include/lob/order_index.hpp  Fixed-capacity open-addressed order-id lookup table
include/lob/order_book.hpp   Matching engine, price-time priority, cancels
include/lob/spsc_queue.hpp   Lock-free single-producer single-consumer ring buffer
include/lob/simd_scan.hpp    AVX2 / NEON / scalar price scan helpers
include/lob/timing.hpp       Hardware counter and wall-clock helpers
include/lob/threading.hpp    Linux CPU-affinity helper, no-op elsewhere
tests/order_book_tests.cpp   Deterministic correctness tests
src/latency_bench.cpp        Hot-path add/cancel latency benchmark
src/pipeline_demo.cpp        Producer thread -> SPSC queue -> matching thread demo
```

## Design Highlights

- Price levels are fixed-size contiguous arrays, not `std::map` or `std::unordered_map`.
- Orders are allocated from a startup `std::array` arena and linked intrusively at each price level.
- Cancels use a fixed-capacity open-addressed hash table.
- The SPSC queue uses only `std::atomic<std::uint64_t>` cursors with acquire/release ordering.
- Producer and consumer cursors are separated with `alignas(64)` to reduce false sharing.
- SIMD helpers use AVX2 on x86, NEON on ARM, and scalar fallback elsewhere.
- ThreadSanitizer target validates the producer-consumer path.
