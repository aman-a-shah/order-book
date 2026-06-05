#include <atomic>
#include <cstdint>
#include <iostream>
#include <memory>
#include <thread>

#include "lob/order_book.hpp"
#include "lob/spsc_queue.hpp"
#include "lob/threading.hpp"
#include "lob/timing.hpp"

int main(int argc, char** argv) {
    const std::uint64_t events = argc > 1 ? std::stoull(argv[1]) : 1'000'000ULL;
    using Queue = lob::SpscQueue<lob::OrderCommand, 1 << 16>;
    using Book = lob::OrderBook<1 << 20, 4096, 1 << 21>;

    auto queue = std::make_unique<Queue>();
    auto book = std::make_unique<Book>();
    std::atomic<bool> producer_done{false};
    std::atomic<std::uint64_t> processed{0};
    std::atomic<std::uint64_t> rejected{0};

    lob::Stopwatch wall;

    std::thread producer([&] {
        lob::pin_current_thread_to_core(0);
        for (std::uint64_t i = 1; i <= events; ++i) {
            const lob::OrderCommand command{
                lob::OrderType::Limit,
                (i & 1ULL) == 0 ? lob::Side::Buy : lob::Side::Sell,
                i,
                (i & 1ULL) == 0 ? 9'999U : 10'001U,
                1U
            };
            while (!queue->push(command)) {
            }
        }
        producer_done.store(true, std::memory_order_release);
    });

    std::thread consumer([&] {
        lob::pin_current_thread_to_core(1);
        lob::OrderCommand command;
        while (!producer_done.load(std::memory_order_acquire) || !queue->empty()) {
            if (queue->pop(command)) {
                const auto report = book->process(command);
                if (!report.accepted) {
                    rejected.fetch_add(1, std::memory_order_relaxed);
                }
                processed.fetch_add(1, std::memory_order_relaxed);
            }
        }
    });

    producer.join();
    consumer.join();

    const double seconds = wall.elapsed_seconds();
    std::cout << "events=" << events << '\n';
    std::cout << "processed=" << processed.load() << '\n';
    std::cout << "rejected=" << rejected.load() << '\n';
    std::cout << "throughput_events_per_second=" << static_cast<std::uint64_t>(events / seconds) << '\n';
    std::cout << "resting_orders=" << book->live_order_count() << '\n';
    return rejected.load() == 0 && processed.load() == events ? 0 : 1;
}
