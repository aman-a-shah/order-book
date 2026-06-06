#include <array>
#include <cstdint>
#include <iostream>
#include <memory>
#include <string>
#include <vector>

#include "lob/order_book.hpp"
#include "lob/replay.hpp"

namespace {

const char* side_name(lob::Side side) {
    return side == lob::Side::Buy ? "B" : "S";
}

template <typename Book>
void print_summary(const Book& book,
                   std::size_t events,
                   std::size_t accepted,
                   std::size_t rejected,
                   std::ostream& out) {
    out << "events=" << events << '\n';
    out << "accepted=" << accepted << '\n';
    out << "rejected=" << rejected << '\n';
    out << "trades=" << book.trade_count() << '\n';
    out << "dropped_trades=" << book.dropped_trade_count() << '\n';
    out << "live_orders=" << book.live_order_count() << '\n';
    if (const auto* bid = book.best_bid()) {
        out << "best_bid=" << bid->price << 'x' << bid->total_volume << '\n';
    } else {
        out << "best_bid=NONE\n";
    }
    if (const auto* ask = book.best_ask()) {
        out << "best_ask=" << ask->price << 'x' << ask->total_volume << '\n';
    } else {
        out << "best_ask=NONE\n";
    }
    for (std::size_t i = 0; i < book.trade_count(); ++i) {
        const auto& trade = book.trades()[i];
        out << "trade[" << i << "]="
            << trade.sequence << ','
            << trade.taker_order_id << ','
            << trade.maker_order_id << ','
            << trade.price << ','
            << trade.quantity << ','
            << side_name(trade.aggressor_side) << '\n';
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

    std::size_t accepted = 0;
    std::size_t rejected = 0;
    for (const auto& event : events) {
        const auto report = book->process(event.command);
        if (report.accepted) {
            ++accepted;
        } else {
            ++rejected;
        }
    }

    print_summary(*book, events.size(), accepted, rejected, std::cout);
    return rejected == 0 ? 0 : 2;
}
