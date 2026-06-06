#include <array>
#include <iomanip>
#include <iostream>
#include <memory>
#include <string>
#include <vector>

#include "lob/order_book.hpp"
#include "lob/replay.hpp"

namespace {

const char* type_name(lob::OrderType type) {
    switch (type) {
        case lob::OrderType::Limit: return "LIMIT ";
        case lob::OrderType::Market: return "MARKET";
        case lob::OrderType::Cancel: return "CANCEL";
        case lob::OrderType::Modify: return "MODIFY";
        case lob::OrderType::Replace: return "REPLCE";
    }
    return "UNKNOWN";
}

const char* side_name(lob::Side side) {
    return side == lob::Side::Buy ? "BUY " : "SELL";
}

template <std::size_t N>
void print_side_by_side(const std::array<lob::DepthLevel, N>& bids,
                        std::size_t bid_count,
                        const std::array<lob::DepthLevel, N>& asks,
                        std::size_t ask_count) {
    std::cout << "       BID DEPTH               ASK DEPTH\n";
    std::cout << "  price     qty  orders    price     qty  orders\n";
    for (std::size_t i = 0; i < N; ++i) {
        if (i < bid_count) {
            std::cout << std::setw(7) << bids[i].price
                      << std::setw(8) << bids[i].total_volume
                      << std::setw(8) << bids[i].order_count << "  ";
        } else {
            std::cout << "                         ";
        }
        if (i < ask_count) {
            std::cout << std::setw(7) << asks[i].price
                      << std::setw(8) << asks[i].total_volume
                      << std::setw(8) << asks[i].order_count;
        }
        std::cout << '\n';
    }
}

}  // namespace

int main(int argc, char** argv) {
    const std::string path = argc > 1 ? argv[1] : "data/sample_replay.csv";
    std::vector<lob::ReplayEvent> events;
    std::string error;
    if (!lob::load_replay_file(path, events, error)) {
        std::cerr << error << '\n';
        return 1;
    }

    using Book = lob::OrderBook<1 << 16, 4096, 1 << 17>;
    auto book = std::make_unique<Book>();

    std::cout << "Low-Latency Order Book Replay\n";
    std::cout << "source=" << path << "\n\n";

    for (const auto& event : events) {
        const auto& command = event.command;
        const auto report = book->process(command);
        std::cout << "t=" << std::setw(5) << event.timestamp_ns
                  << " ns  " << type_name(command.type)
                  << "  " << side_name(command.side)
                  << "  id=" << std::setw(4) << command.order_id
                  << "  px=" << std::setw(5) << command.price
                  << "  qty=" << std::setw(3) << command.quantity
                  << "  accepted=" << (report.accepted ? "Y" : "N")
                  << "  filled=" << std::setw(3) << report.filled_quantity
                  << "  trades=" << report.trade_count << '\n';
    }

    std::array<lob::DepthLevel, 5> bids{};
    std::array<lob::DepthLevel, 5> asks{};
    const auto bid_count = book->bid_depth(bids);
    const auto ask_count = book->ask_depth(asks);

    std::cout << "\nFinal book\n";
    print_side_by_side(bids, bid_count, asks, ask_count);

    std::cout << "\nTrades\n";
    for (std::size_t i = 0; i < book->trade_count(); ++i) {
        const auto& trade = book->trades()[i];
        std::cout << '#' << trade.sequence
                  << " taker=" << trade.taker_order_id
                  << " maker=" << trade.maker_order_id
                  << " side=" << side_name(trade.aggressor_side)
                  << " px=" << trade.price
                  << " qty=" << trade.quantity << '\n';
    }

    return 0;
}
