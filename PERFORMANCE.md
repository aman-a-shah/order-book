# Performance And Verification Report

This file records the local verification workflow and the commands to reproduce deeper profiling on a Linux x86 workstation.

## Local Machine

Current measurements were taken on Apple Silicon with Apple Clang. The code uses the NEON SIMD path locally. On x86 Linux with `-mavx2` / `-march=native`, the AVX2 path is selected automatically by the compiler.

## Correctness Verification

```sh
make test
```

Covers:

- limit, market, and cancel behavior.
- partial fills and full fills.
- price-time priority.
- modify quantity reduction while preserving priority.
- cancel-replace priority loss.
- top-N depth snapshots.
- multi-symbol routing isolation.
- SPSC FIFO behavior.
- binary protocol encode/decode.
- deterministic replay against a golden expected output.

## ThreadSanitizer

```sh
make tsan
```

The sanitizer target runs the producer thread, SPSC queue, and matching thread over 10,000 events. On this machine it completed with no reported data races.

## Local Benchmark Snapshot

Representative output from the current machine:

```text
make all
latency_bench:
  events=2,000,000
  throughput_events_per_second ~= 5.7M
  mean_cycles_or_ticks ~= 167
  p99_cycles_or_ticks_upper_bound ~= 4095

pipeline_demo:
  events=1,000,000
  throughput_events_per_second ~= 7.2M

make replay-bench
  events=1,100,000
  throughput_events_per_second ~= 9.8M
  mean_decode_route_match_ticks ~= 92
  p99_decode_route_match_ticks_upper_bound ~= 511
```

The benchmark counter is named `cycles_or_ticks` because the low-level hardware counter differs by architecture. Apple Silicon reads the ARM virtual counter. x86 reads `__rdtsc()`.

## Linux x86 Profiling Commands

For stronger resume claims, run these on an isolated Linux workstation:

```sh
make clean
make CXXFLAGS="-std=c++17 -O3 -DNDEBUG -Wall -Wextra -Wpedantic -Iinclude -march=native -mavx2" all replay-bench

perf stat -d ./build/latency_bench 5000000
perf stat -d ./build/replay_bench data/sample_replay.csv 500000
perf record -g ./build/replay_bench data/sample_replay.csv 500000
perf report

valgrind --tool=cachegrind ./build/latency_bench 1000000
valgrind --tool=cachegrind ./build/replay_bench data/sample_replay.csv 100000
```

Recommended machine setup:

- pin feed and matching threads to physical cores.
- disable frequency scaling or use performance governor.
- avoid running benchmarks on battery power.
- run several warm-up iterations.
- report p50, p90, p99, p99.9, max, throughput, and rejected-event count.

## Honest Interpretation

The direct latency benchmark measures a bounded add/cancel hot path. The replay benchmark measures binary decode, symbol routing, and matching against a deterministic event stream. These are complementary workloads; neither should be presented as a universal exchange latency number.

The strongest claim is:

> The engine uses bounded memory, lock-free SPSC ingestion, fixed-capacity indexing, contiguous price-level storage, SIMD-aware scans, deterministic replay tests, and sanitizer verification. Local synthetic benchmarks exceed the 2M events/sec target, and the project includes the profiling hooks needed to validate cache and branch behavior on Linux.
