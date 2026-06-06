#include <cstdlib>
#include <iostream>

#include "lob/order_book.hpp"
#include "lob/multi_book.hpp"
#include "lob/spsc_queue.hpp"

namespace {

using Book = lob::OrderBook<1024, 128, 2048>;

void require(bool condition, const char* message) {
    if (!condition) {
        std::cerr << "FAIL: " << message << '\n';
        std::exit(1);
    }
}

void test_limit_order_rests() {
    Book book;
    const auto report = book.process({lob::OrderType::Limit, lob::Side::Buy, 1, 100, 25});
    require(report.accepted, "limit order accepted");
    require(report.resting, "unfilled limit rests");
    require(book.best_bid() != nullptr, "best bid present");
    require(book.best_bid()->price == 100, "best bid price");
    require(book.best_bid()->total_volume == 25, "best bid volume");
}

void test_full_fill_removes_level() {
    Book book;
    book.process({lob::OrderType::Limit, lob::Side::Sell, 10, 101, 50});
    const auto report = book.process({lob::OrderType::Limit, lob::Side::Buy, 11, 101, 50});
    require(report.accepted, "crossing buy accepted");
    require(report.filled_quantity == 50, "full fill quantity");
    require(report.trade_count == 1, "one trade generated");
    require(book.best_ask() == nullptr, "empty ask removed");
    require(book.live_order_count() == 0, "filled order removed from index");
}

void test_partial_fill_reduces_fifo_head() {
    Book book;
    book.process({lob::OrderType::Limit, lob::Side::Sell, 20, 102, 80});
    const auto report = book.process({lob::OrderType::Limit, lob::Side::Buy, 21, 102, 30});
    require(report.filled_quantity == 30, "partial fill quantity");
    require(book.best_ask() != nullptr, "partial ask remains");
    require(book.best_ask()->total_volume == 50, "partial volume remains");
    require(book.live_order_count() == 1, "remaining order stays indexed");
}

void test_price_time_priority() {
    Book book;
    book.process({lob::OrderType::Limit, lob::Side::Sell, 30, 100, 10});
    book.process({lob::OrderType::Limit, lob::Side::Sell, 31, 100, 20});
    const auto report = book.process({lob::OrderType::Market, lob::Side::Buy, 32, 0, 15});
    require(report.filled_quantity == 15, "market fills across FIFO level");
    require(report.trade_count == 2, "market consumes first order then second");
    require(book.best_ask() != nullptr, "second order remains");
    require(book.best_ask()->total_volume == 15, "FIFO remainder volume");
}

void test_cancel() {
    Book book;
    book.process({lob::OrderType::Limit, lob::Side::Buy, 40, 99, 10});
    const auto report = book.process({lob::OrderType::Cancel, lob::Side::Buy, 40, 0, 0});
    require(report.accepted, "cancel accepted");
    require(report.canceled, "cancel flag set");
    require(book.best_bid() == nullptr, "canceled level removed");
    require(book.live_order_count() == 0, "canceled order removed from index");
}

void test_sorted_levels_and_simd_scan() {
    Book book;
    book.process({lob::OrderType::Limit, lob::Side::Sell, 50, 105, 1});
    book.process({lob::OrderType::Limit, lob::Side::Sell, 51, 101, 1});
    book.process({lob::OrderType::Limit, lob::Side::Sell, 52, 103, 1});
    require(book.best_ask()->price == 101, "asks sorted ascending");
    require(book.first_crossing_ask(102) == 0, "SIMD ask crossing scan");

    book.process({lob::OrderType::Limit, lob::Side::Buy, 53, 95, 1});
    book.process({lob::OrderType::Limit, lob::Side::Buy, 54, 99, 1});
    book.process({lob::OrderType::Limit, lob::Side::Buy, 55, 97, 1});
    require(book.best_bid()->price == 99, "bids sorted descending");
    require(book.first_crossing_bid(98) == 0, "SIMD bid crossing scan");
}

void test_spsc_fifo_ordering() {
    lob::SpscQueue<lob::OrderCommand, 8> queue;
    require(queue.push({lob::OrderType::Limit, lob::Side::Buy, 1, 100, 1}), "push 1");
    require(queue.push({lob::OrderType::Limit, lob::Side::Sell, 2, 101, 2}), "push 2");
    lob::OrderCommand out;
    require(queue.pop(out), "pop 1");
    require(out.order_id == 1, "fifo first id");
    require(queue.pop(out), "pop 2");
    require(out.order_id == 2, "fifo second id");
    require(!queue.pop(out), "empty pop fails");
}

void test_trade_log_records_maker_and_taker() {
    Book book;
    book.process({lob::OrderType::Limit, lob::Side::Sell, 60, 101, 25});
    const auto report = book.process({lob::OrderType::Market, lob::Side::Buy, 61, 0, 10});
    require(report.accepted, "market accepted for trade log");
    require(book.trade_count() == 1, "trade log count");
    require(book.trades()[0].taker_order_id == 61, "trade taker id");
    require(book.trades()[0].maker_order_id == 60, "trade maker id");
    require(book.trades()[0].price == 101, "trade price");
    require(book.trades()[0].quantity == 10, "trade quantity");
    require(book.trades()[0].aggressor_side == lob::Side::Buy, "trade aggressor side");
}

void test_modify_reduces_quantity_without_losing_priority() {
    Book book;
    book.process({lob::OrderType::Limit, lob::Side::Sell, 70, 100, 20});
    book.process({lob::OrderType::Limit, lob::Side::Sell, 71, 100, 20});
    const auto modified = book.process({lob::OrderType::Modify, lob::Side::Sell, 70, 0, 5});
    require(modified.accepted && modified.modified, "modify accepted");
    require(book.best_ask()->total_volume == 25, "modify reduces level volume");
    const auto fill = book.process({lob::OrderType::Market, lob::Side::Buy, 72, 0, 6});
    require(fill.trade_count == 2, "modified head keeps priority then next order fills");
    require(book.trades()[0].maker_order_id == 70, "modified order remains FIFO head");
    require(book.trades()[1].maker_order_id == 71, "second FIFO order follows");
}

void test_replace_loses_time_priority() {
    Book book;
    book.process({lob::OrderType::Limit, lob::Side::Sell, 80, 100, 10});
    book.process({lob::OrderType::Limit, lob::Side::Sell, 81, 100, 10});
    const auto replaced = book.process({lob::OrderType::Replace, lob::Side::Sell, 80, 100, 10, 82});
    require(replaced.accepted && replaced.replaced, "replace accepted");
    const auto fill = book.process({lob::OrderType::Market, lob::Side::Buy, 83, 0, 11});
    require(fill.trade_count == 2, "replace fill spans two makers");
    require(book.trades()[0].maker_order_id == 81, "unchanged order has priority over replacement");
    require(book.trades()[1].maker_order_id == 82, "replacement rests at tail");
}

void test_depth_snapshot() {
    Book book;
    book.process({lob::OrderType::Limit, lob::Side::Buy, 90, 100, 3});
    book.process({lob::OrderType::Limit, lob::Side::Buy, 91, 99, 4});
    book.process({lob::OrderType::Limit, lob::Side::Sell, 92, 101, 5});
    std::array<lob::DepthLevel, 4> bids{};
    std::array<lob::DepthLevel, 4> asks{};
    require(book.bid_depth(bids) == 2, "bid depth count");
    require(book.ask_depth(asks) == 1, "ask depth count");
    require(bids[0].price == 100 && bids[0].total_volume == 3, "top bid depth");
    require(asks[0].price == 101 && asks[0].total_volume == 5, "top ask depth");
}

void test_multi_symbol_routing_isolates_books() {
    lob::MultiBookRouter<2, 128, 32, 256> router;
    router.process({lob::OrderType::Limit, lob::Side::Buy, 100, 10000, 10, 0, 0});
    router.process({lob::OrderType::Limit, lob::Side::Buy, 200, 20000, 20, 0, 1});
    require(router.book(0).best_bid()->price == 10000, "symbol 0 best bid");
    require(router.book(1).best_bid()->price == 20000, "symbol 1 best bid");
    const auto rejected = router.process({lob::OrderType::Limit, lob::Side::Buy, 300, 1, 1, 0, 2});
    require(rejected.rejected, "invalid symbol rejected");
}

}  // namespace

int main() {
    test_limit_order_rests();
    test_full_fill_removes_level();
    test_partial_fill_reduces_fifo_head();
    test_price_time_priority();
    test_cancel();
    test_sorted_levels_and_simd_scan();
    test_spsc_fifo_ordering();
    test_trade_log_records_maker_and_taker();
    test_modify_reduces_quantity_without_losing_priority();
    test_replace_loses_time_priority();
    test_depth_snapshot();
    test_multi_symbol_routing_isolates_books();
    std::cout << "All order book and queue tests passed\n";
    return 0;
}
