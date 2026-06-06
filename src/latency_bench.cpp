#include <algorithm>
#include <cstdint>
#include <iostream>
#include <limits>
#include <memory>

#include "lob/histogram.hpp"
#include "lob/order_book.hpp"
#include "lob/timing.hpp"

int main(int argc, char** argv) {
    const std::size_t events = argc > 1 ? static_cast<std::size_t>(std::stoull(argv[1])) : 2'000'000;
    using Book = lob::OrderBook<1 << 16, 1024, 1 << 17>;

    auto book = std::make_unique<Book>();
    lob::PowerOfTwoHistogram<> histogram;

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
        histogram.observe(end - start);
    }

    const double seconds = wall.elapsed_seconds();

    std::cout << "events=" << events << '\n';
    std::cout << "throughput_events_per_second=" << static_cast<std::uint64_t>(events / seconds) << '\n';
    std::cout << "mean_cycles_or_ticks=" << histogram.mean() << '\n';
    std::cout << "p50_cycles_or_ticks_upper_bound=" << histogram.percentile(50.0) << '\n';
    std::cout << "p90_cycles_or_ticks_upper_bound=" << histogram.percentile(90.0) << '\n';
    std::cout << "p99_cycles_or_ticks_upper_bound=" << histogram.percentile(99.0) << '\n';
    std::cout << "p999_cycles_or_ticks_upper_bound=" << histogram.percentile(99.9) << '\n';
    std::cout << "max_cycles_or_ticks=" << histogram.max() << '\n';
    std::cout << "live_orders=" << book->live_order_count() << '\n';
    return 0;
}
