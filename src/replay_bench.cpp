#include <cstdint>
#include <iostream>
#include <memory>
#include <string>
#include <vector>

#include "lob/binary_protocol.hpp"
#include "lob/histogram.hpp"
#include "lob/multi_book.hpp"
#include "lob/replay.hpp"
#include "lob/timing.hpp"

int main(int argc, char** argv) {
    const std::string path = argc > 1 ? argv[1] : "data/sample_replay.csv";
    const std::size_t repeats = argc > 2 ? static_cast<std::size_t>(std::stoull(argv[2])) : 100'000;

    std::vector<lob::ReplayEvent> events;
    std::string error;
    if (!lob::load_replay_file(path, events, error)) {
        std::cerr << error << '\n';
        return 1;
    }
    if (events.empty()) {
        std::cerr << "replay file has no events\n";
        return 1;
    }

    std::vector<std::array<std::uint8_t, lob::BinaryMessageSize>> messages;
    messages.reserve(events.size());
    for (const auto& event : events) {
        messages.push_back(lob::encode_binary_order(event.command));
    }

    using Router = lob::MultiBookRouter<2, 1 << 19, 4096, 1 << 20>;
    auto router = std::make_unique<Router>();
    lob::PowerOfTwoHistogram<> histogram;
    std::uint64_t processed = 0;
    std::uint64_t rejected = 0;

    lob::Stopwatch wall;
    for (std::size_t r = 0; r < repeats; ++r) {
        for (const auto& message : messages) {
            lob::OrderCommand command;
            const auto start = lob::read_cycle_counter();
            if (!lob::decode_binary_order(message, command)) {
                std::cerr << "binary decode failed\n";
                return 1;
            }
            const std::uint64_t id_offset = static_cast<std::uint64_t>(r) * 1'000'000ULL;
            command.order_id += id_offset;
            if (command.new_order_id != 0) {
                command.new_order_id += id_offset;
            }
            const auto report = router->process(command);
            const auto end = lob::read_cycle_counter();
            histogram.observe(end - start);
            ++processed;
            if (!report.accepted) {
                ++rejected;
            }
        }
    }

    const double seconds = wall.elapsed_seconds();
    std::cout << "source=" << path << '\n';
    std::cout << "events=" << processed << '\n';
    std::cout << "rejected=" << rejected << '\n';
    std::cout << "throughput_events_per_second=" << static_cast<std::uint64_t>(processed / seconds) << '\n';
    std::cout << "mean_decode_route_match_ticks=" << histogram.mean() << '\n';
    std::cout << "p99_decode_route_match_ticks_upper_bound=" << histogram.percentile(99.0) << '\n';
    std::cout << "max_decode_route_match_ticks=" << histogram.max() << '\n';
    return 0;
}
