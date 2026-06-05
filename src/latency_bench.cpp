#include <algorithm>
#include <cstdint>
#include <iostream>
#include <limits>
#include <memory>
#include <vector>

#include "lob/order_book.hpp"
#include "lob/timing.hpp"

int main(int argc, char** argv) {
    const std::size_t events = argc > 1 ? static_cast<std::size_t>(std::stoull(argv[1])) : 2'000'000;
    using Book = lob::OrderBook<1 << 16, 1024, 1 << 17>;

    auto book = std::make_unique<Book>();
    std::vector<std::uint64_t> samples;
    samples.reserve(events);

    lob::Stopwatch wall;
    for (std::uint64_t i = 0; i < events; ++i) {
        const bool add = (i & 1U) == 0;
        const std::uint64_t order_id = (i / 2) + 1;
        const lob::OrderCommand command{
            add ? lob::OrderType::Limit : lob::OrderType::Cancel,
            lob::Side::Buy,
            order_id,
            add ? 10'000U : 0U,
            add ? 1U : 0U
        };
        const auto start = lob::read_cycle_counter();
        const auto report = book->process(command);
        const auto end = lob::read_cycle_counter();
        if (!report.accepted) {
            std::cerr << "benchmark command rejected at event " << i << '\n';
            return 1;
        }
        samples.push_back(end - start);
    }

    const double seconds = wall.elapsed_seconds();
    std::sort(samples.begin(), samples.end());
    const auto p50 = samples[samples.size() / 2];
    const auto p99 = samples[(samples.size() * 99) / 100];
    const auto max = samples.back();

    long double total = 0;
    for (const auto value : samples) total += value;
    const auto mean = static_cast<double>(total / samples.size());

    std::cout << "events=" << events << '\n';
    std::cout << "throughput_events_per_second=" << static_cast<std::uint64_t>(events / seconds) << '\n';
    std::cout << "mean_cycles_or_ticks=" << mean << '\n';
    std::cout << "p50_cycles_or_ticks=" << p50 << '\n';
    std::cout << "p99_cycles_or_ticks=" << p99 << '\n';
    std::cout << "max_cycles_or_ticks=" << max << '\n';
    std::cout << "live_orders=" << book->live_order_count() << '\n';
    return 0;
}
