Product Requirement Document (PRD) & Engineering Spec
1. Executive Summary & Core Objectives
The goal is to build an ultra-low-latency, single-threaded Limit Order Book (LOB) matching engine in C++17 decoupled from a mock live market feed via a lock-free Single Producer Single Consumer (SPSC) queue.
Core Performance Targets
Throughput: ≥2,000,000 order events processed per second on a single thread.
Latency: Mean end-to-end processing latency of sub-200 nanoseconds (<200 ns).
Cache Efficiency: Minimize L1/L2 data cache misses through a contiguous memory layout, avoiding heap allocation jitters on the critical path.
2. System Architecture & Component Design
The system is split into two distinct execution domains isolated on dedicated CPU cores to eliminate context-switching overhead.
[ Mock Feed Handler Thread ] (Core 1)
           │
           ▼ (Pushes raw bytes)
┌──────────────────────────────────────┐
│  Lock-Free SPSC Ring Buffer Queue    │ ◄── Contiguous array, std::atomic, CAS
└──────────────────────────────────────┘
           │
           ▼ (Pops raw bytes)
[ Matching Engine Thread ]   (Core 2)
           │
           ├──► 1. Flat Sorted Vector (Price Scanner via AVX2 SIMD)
           └──► 2. Object Pool / Intrusive Doubly Linked List (Order Allocator)
Component A: The Communication Layer (Lock-Free SPSC Queue)
To decouple network ingestion from processing, implement a ring-buffer-based Single Producer Single Consumer queue.
Lock-Free Enforcement: No std::mutex or condition variables. Synchronization must rely entirely on std::atomic<uint64_t> sequence numbers acting as read and write pointers.
Memory Ordering: Use explicit memory orderings (std::memory_order_release when updating pointers; std::memory_order_acquire when reading pointers) instead of sequential consistency (std::memory_order_seq_cst) to eliminate unnecessary CPU memory barriers.
Cache Line Padding: Force cache line alignment (alignas(64)) between the producer variables and consumer variables to prevent false sharing (where threads invalidate each other's CPU cache lines).
Component B: The Storage Layer (The Cache-Aware Order Book)
Traditional textbooks suggest using a binary search tree (std::map) or hash map (std::unordered_map) for an order book. For HFT, this is a trap. Node-based structures scatter pointers across the heap, causing rampant L1 cache misses.
Instead, implement a Flat Sorted Vector layout:
Price Levels: Maintain buy (bids) and sell (asks) sides in fixed-size, contiguous arrays/vectors sorted by price-time priority.
The No-Allocation Rule: All memory for orders and price levels must be pre-allocated at startup inside a custom Fixed-Size Object Pool (std::array acting as an arena allocator). No calls to new or malloc are allowed during live matching execution.
3. Data Structures & SIMD Implementation Details
Data Struct Definitions
To keep memory usage minimal and deterministic, align memory fields to prevent compiler padding bytes:
C++
#include <cstdint>

enum class Side : uint8_t { Buy = 0, Sell = 1 };
enum class OrderType : uint8_t { Limit = 0, Market = 1, Cancel = 2 };

struct Order {
    uint64_t order_id;
    uint32_t price;
    uint32_t quantity;
    Side side;
    // Intrusive pointers for the price level linked list
    Order* next;
    Order* prev;
};

// Represents a price level in the flat book
struct alignas(32) PriceLevel {
    uint32_t price;
    uint32_t total_volume;
    Order* head_order; // Pointer to first order at this price (FIFO)
    Order* tail_order; // Pointer to last order at this price
};
AVX2 SIMD Vectorization
When a market order arrives, the matching engine needs to scan the book instantly to find the best bid or ask. Instead of iterating over the PriceLevel vector line by line (scalar traversal), load multiple price levels simultaneously into 256-bit AVX2 registers.
Since each PriceLevel struct uses 32 bytes of data with proper padding, an AVX2 register (256 bits=32 bytes) can process exactly one structural element or multiple packed prices simultaneously.
High-Level SIMD Algorithm for Best Price Scan:
Load 8 packed 32-bit integer prices from your contiguous array into an AVX2 register using _mm256_load_si256.
Broadcast the target execution or threshold price to another register using _mm256_set1_epi32.
Use a vector comparison intrinsic like _mm256_cmpgt_epi32 (compare greater than) to check all 8 elements in a single CPU clock cycle.
Generate a bitmask via _mm256_movemask_ps to locate the exact vector index of the matching price level instantly.
4. Implementation & Execution Timeline (4-Week Plan)
┌────────────────────────────────────────────────────────────────────────┐
│                          PROJECT TIMELINE                              │
├───────────────────┬───────────────────┬────────────────┬───────────────┤
│ Weeks 1: Core     │ Week 2: Lock-Free │ Week 3: SIMD   │ Week 4:       │
│ Data Structures   │ Messaging         │ Optimization   │ Profiling &   │
│ & Logic           │ Pipeline          │ & Vectorization│ Verification  │
└───────────────────┴───────────────────┴────────────────┴───────────────┘
Week 1: Foundational Engine & Matching Logic
Deliverables: Custom object allocator pool, memory-aligned data structures, and the structural OrderBook logic.
Tasks: * Implement basic matching algorithms: matching incoming Limit/Market orders, updating total_volume, and evicting empty price levels.
Write a deterministic suite of Unit Tests handling matching edge-cases (e.g., partial fills, full fills, order cancellations).
Week 2: Decoupled Architecture (The SPSC Ring Buffer)
Deliverables: Thread-isolated, lock-free message queue.
Tasks:
Implement the SPSC ring buffer structure.
Enforce cache alignment using alignas(64) between memory barriers.
Set up a multi-threaded test runner: Thread 1 acts as a market generator pushing orders into the ring buffer; Thread 2 pops and processes them.
Week 3: Hardware Sympathy & SIMD Vectorization
Deliverables: Replacement of standard library lookup structures with flat contiguous memory maps accelerated by AVX2.
Tasks:
Migrate all lookup logic away from pointer-heavy configurations.
Write the explicit AVX2 assembly intrinsics layout for scanning top-of-book prices.
Ensure compilation configurations use optimal hardware targeting (-mavx2 -O3 -march=native).
Week 4: Hardcore Profiling, Optimization & Verification
Deliverables: Performance verification reports demonstrating zero data-races and sub-200ns latencies.
Tasks:
TSAN Validation: Compile the codebase using ThreadSanitizer (-fsanitize=thread) and pass 10,000+ rapid-fire concurrent mock orders to mathematically guarantee zero data races.
Low-Level Profiling: Run the binary inside perf to evaluate CPU branch prediction misses. Use valgrind --tool=cachegrind to prove that the L1/L2 cache miss rate dropped effectively by 40% or more following the flat vector optimization.
Latency Microbenchmarking: Utilize High-Resolution CPU TSC (Time Stamp Counter) timers via __rdtsc() to capture precise processing times per order.
5. Interview Strategy: How to Sell This to Citadel/Jane Street
When a Quant Systems interviewer reviews your resume, they will skip past your high-level descriptions and zero in on the technical claims. Prepare to defend the following discussion points:
Why did you choose an SPSC Queue instead of a Lock-Free MPMC Queue?
The Answer: "Multi-Producer Multi-Consumer (MPMC) queues introduce severe cache-line bouncing due to multiple cores contending to compare-and-swap the same pointers. Since our architecture relies on a single input data feed channel feeding a single dedicated matching engine thread, an SPSC structure avoids this atomic contention completely, letting us process elements near theoretical hardware limits."
Why choose a Flat Vector over an Unordered Map if maps have O(1) lookup complexity?
The Answer: "While an std::unordered_map has an average algorithmic time complexity of O(1), its spatial locality is poor. Buckets point to linked nodes scattered randomly across heap memory, forcing the CPU to stall while waiting for RAM fetches. A flat vector stores elements sequentially. Even though searching a short vector takes O(N), it fits entirely inside the CPU's lightning-fast L1 cache, allowing us to scan it instantly using vector hardware intrinsics before a standard map fetch could even complete a single pointer indirection."
How did you prevent the OS from interrupting your execution threads?
The Answer: "To achieve sub-200ns latency determinism, I eliminated OS scheduler interference by setting thread affinities using pthread_setaffinity_np, effectively pinning the feed handler and matching engine to isolated, dedicated physical CPU cores."