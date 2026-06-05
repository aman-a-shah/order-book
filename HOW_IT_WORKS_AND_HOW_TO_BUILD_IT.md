# How This Project Works And How To Build It From Scratch

## What This Is

This is a low-latency limit order book. It accepts order commands, matches aggressive orders against resting liquidity, stores passive orders by price-time priority, supports cancels by order id, and can be driven from a separate mock market-feed thread through a lock-free queue.

The important engineering constraint is determinism: memory is bounded, the critical path does not allocate, the hot data structures are contiguous, and the producer-consumer path avoids locks.

## Runtime Flow

1. The feed thread creates `OrderCommand` messages.
2. It pushes each command into `SpscQueue<OrderCommand, N>`.
3. The matching thread pops commands from the queue.
4. The matching thread calls `OrderBook::process`.
5. `process` dispatches to limit, market, or cancel handling.
6. Limit and market orders match against the opposite side while prices cross.
7. Unfilled limit quantity becomes a resting order.
8. Cancels remove the order from both its price-level FIFO list and the order-id index.

## Core Data Structures

### OrderCommand

`OrderCommand` is the wire-style message:

- `type`: limit, market, or cancel.
- `side`: buy or sell.
- `order_id`: globally unique id for accepted resting orders.
- `price`: integer tick price for limit orders.
- `quantity`: integer share/contract quantity.

Prices are integers instead of floating point values because real matching engines work in ticks. This avoids rounding errors and makes comparisons deterministic.

### Order

`Order` is the live resting order record. It contains order id, price, remaining quantity, side, and intrusive `next` / `prev` pointers. The pointers link orders together inside one price level in FIFO order.

Intrusive lists avoid separate list-node allocation. The order object is the list node.

### PriceLevel

`PriceLevel` stores one price:

- `price`
- `total_volume`
- `head_order`
- `tail_order`
- `order_count`

The struct is `alignas(32)` and is kept at 32 bytes. That makes the object compact and predictable in memory.

### ObjectPool

`ObjectPool<Capacity>` owns a fixed `std::array<Order, Capacity>`. At startup it fills a free-list of array indexes. Allocating an order pops an index. Releasing an order pushes the index back.

This gives constant-time order allocation without calling the general-purpose heap allocator during matching. That matters because heap allocation creates unpredictable latency spikes and can take locks inside the allocator.

### OrderIndex

`OrderIndex<Capacity>` is a fixed-capacity open-addressed hash table from `order_id` to `Order*`.

Cancels need fast order-id lookup. A tree or heap-allocating hash map would work functionally, but it would introduce pointer chasing and allocator behavior. This table is a bounded array with linear probing, so memory access is predictable.

### Flat Price-Level Arrays

The book stores:

- bids sorted descending by price.
- asks sorted ascending by price.

Best bid is `bids_[0]`. Best ask is `asks_[0]`.

This makes top-of-book checks extremely cheap. Insertion may shift price levels, which is `O(number_of_levels)`, but for a bounded, cache-resident book that is often faster than chasing nodes through a tree. The project is deliberately optimized for hardware locality rather than textbook asymptotic complexity.

## Matching Logic

### Buy Limit Order

A buy limit order can trade with asks priced at or below the buy price.

The book repeatedly checks the best ask:

```text
while remaining_quantity > 0 and best_ask.price <= buy_limit:
    fill against the oldest order at best ask
```

If the incoming order still has quantity after all crossing asks are consumed, the remainder rests on the bid side.

### Sell Limit Order

A sell limit order can trade with bids priced at or above the sell price.

```text
while remaining_quantity > 0 and best_bid.price >= sell_limit:
    fill against the oldest order at best bid
```

If quantity remains, it rests on the ask side.

### Market Order

A market buy consumes asks until it is filled or the ask side is empty. A market sell consumes bids until it is filled or the bid side is empty. Any unfilled market quantity expires; it does not rest.

### Price-Time Priority

Price priority comes from sorted price-level arrays. Time priority comes from appending each new resting order to the tail of its price-level linked list and always matching from the head.

### Cancel

Cancel flow:

1. Look up `order_id` in `OrderIndex`.
2. Find the owning price level from `order->side` and `order->price`.
3. Unlink the order from the level's intrusive list.
4. Subtract its remaining quantity from `total_volume`.
5. Erase the price level if it became empty.
6. Remove the id from `OrderIndex`.
7. Return the order object to `ObjectPool`.

## Lock-Free Feed Queue

`SpscQueue<T, Capacity>` is a ring buffer with exactly one producer and exactly one consumer.

It has:

- a fixed array buffer.
- an atomic `head` cursor owned mostly by the consumer.
- an atomic `tail` cursor owned mostly by the producer.
- power-of-two capacity so indexes use `cursor & (capacity - 1)`.

Push:

1. Producer reads `tail` relaxed.
2. Producer reads `head` acquire to see available space.
3. Producer writes into the buffer.
4. Producer publishes the new `tail` with release ordering.

Pop:

1. Consumer reads `head` relaxed.
2. Consumer reads `tail` acquire to see available data.
3. Consumer copies from the buffer.
4. Consumer publishes the new `head` with release ordering.

There is no `std::mutex`, no condition variable, and no compare-exchange loop. SPSC does not need CAS because only one thread writes each cursor.

## SIMD Price Scanning

`simd_scan.hpp` provides three compile-time paths:

- AVX2 on x86.
- NEON on ARM.
- scalar fallback.

The helpers scan packed integer prices looking for the first ask at or below a limit, or the first bid at or above a limit. On x86 AVX2, eight 32-bit prices are compared in one vector. On ARM NEON, four 32-bit prices are compared in one vector.

The matching loop itself can use top-of-book because the arrays are sorted, but the SIMD helpers demonstrate the vectorized scan pattern needed for threshold searches, validation, and future batched strategies.

## Performance Measurement

`src/latency_bench.cpp` measures a bounded hot path: add one order, cancel it, repeat. That keeps memory usage stable and isolates the object pool, hash index, level insert, level erase, and dispatch path.

`src/pipeline_demo.cpp` measures end-to-end producer thread to queue to consumer thread throughput.

`include/lob/timing.hpp` uses:

- `__rdtsc()` on x86.
- `cntvct_el0` on ARM64.
- `std::chrono` fallback elsewhere.

Counter values are reported as `cycles_or_ticks` because ARM virtual counter ticks are not the same thing as x86 CPU cycles.

## Languages And Tools You Need To Know

### C++17

You need:

- templates for fixed-capacity generic containers.
- `std::array` for startup-owned contiguous storage.
- `std::atomic` and explicit memory ordering.
- move/copy basics for fixed-size command messages.
- RAII and object lifetime rules.
- integer types from `<cstdint>`.
- compile-time assertions with `static_assert`.

### Build Tooling

You need:

- Makefiles.
- compiler flags such as `-O3`, `-DNDEBUG`, `-g`, and `-fsanitize=thread`.
- basic command-line profiling workflow.
- ability to build with Clang or GCC.

No external framework is required. This is deliberate: the point is to show control over memory, CPU behavior, and concurrency primitives.

### CPU And Operating-System Concepts

You need:

- cache lines and false sharing.
- L1/L2/L3 cache locality.
- branch prediction.
- heap allocation latency.
- CPU affinity and thread pinning.
- producer-consumer pipelines.
- data races and sanitizer validation.
- the difference between throughput, mean latency, tail latency, and jitter.

### Concurrency Theory

You need:

- single-producer single-consumer queue design.
- acquire/release memory ordering.
- why SPSC can avoid compare-and-swap.
- why MPMC queues are more expensive.
- how cache-line ownership moves between cores.

### Market Microstructure

You need:

- limit orders.
- market orders.
- bid/ask sides.
- spread.
- top of book.
- price-time priority.
- partial fills and full fills.
- cancels.
- integer tick prices.
- notional value.

### Algorithms And Data Structures

You need:

- sorted arrays.
- intrusive doubly linked lists.
- open-addressed hash tables.
- ring buffers.
- fixed-size object pools.
- amortized versus worst-case reasoning.
- why Big-O is not enough for latency-sensitive systems.

### Math

You need:

- integer arithmetic and overflow awareness.
- latency percentiles such as p50 and p99.
- throughput calculations: `events / seconds`.
- weighted notional: `fill_quantity * execution_price`.
- basic probability intuition for hash-table probing.
- benchmark statistics and outlier interpretation.

You do not need stochastic calculus, options pricing, or advanced quant research math for this project. This is a quant systems project, so the relevant math is measurement, discrete market mechanics, and performance reasoning.

## How To Rebuild It Yourself

1. Define the command and order structs with integer prices.
2. Implement a fixed object pool backed by `std::array`.
3. Implement price levels with FIFO intrusive linked lists.
4. Store bid and ask price levels in sorted contiguous arrays.
5. Implement matching against best ask for buys and best bid for sells.
6. Add market order behavior.
7. Add a fixed open-addressed order-id index.
8. Implement cancel by unlinking from the FIFO list and returning the order to the pool.
9. Add deterministic unit tests for full fills, partial fills, cancels, and FIFO priority.
10. Implement the SPSC ring buffer with acquire/release atomics.
11. Add a threaded producer-consumer demo.
12. Add SIMD scan helpers for packed price arrays.
13. Add a hot-path latency benchmark.
14. Add a sanitizer target.
15. Profile, inspect cache behavior, and refine only after measuring.

## What To Say In An Interview

The strongest explanation is:

> I built this around hardware locality. The matching thread owns the book, so the book itself does not need locks. Feed ingestion is decoupled with an SPSC queue, which avoids mutexes and avoids the CAS contention of an MPMC queue. Orders are stored in a fixed object pool, price levels are contiguous arrays, and cancels use a bounded open-addressed index. The design trades some theoretical insertion complexity for predictable cache-resident memory access, which is the right tradeoff for a small, hot order book.

Be ready to defend:

- why integer ticks beat floating point prices.
- why one matching thread simplifies correctness.
- why `std::unordered_map` is not automatically faster.
- why acquire/release is enough for the SPSC queue.
- why p99 latency matters more than only mean latency.
- why benchmarks must describe their workload honestly.

## Useful Next Enhancements

- Add modify/replace orders.
- Add multiple symbols with one book per instrument.
- Add binary market-data message parsing.
- Add persistent trade/event logs.
- Add Linux `perf` and Cachegrind reports on an x86 workstation.
- Add a replay benchmark from a real historical order stream.
- Add a better latency histogram without storing every sample.
