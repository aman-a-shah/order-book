#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <limits>

#include "lob/object_pool.hpp"
#include "lob/order_index.hpp"
#include "lob/simd_scan.hpp"
#include "lob/types.hpp"

namespace lob {

template <std::size_t MaxOrders, std::size_t MaxPriceLevels, std::size_t IndexCapacity = MaxOrders * 2>
class OrderBook {
public:
    ExecutionReport process(const OrderCommand& command) {
        if (command.type == OrderType::Cancel) {
            return cancel(command.order_id);
        }
        if (command.quantity == 0 || command.order_id == 0) {
            ExecutionReport report;
            report.incoming_order_id = command.order_id;
            report.requested_quantity = command.quantity;
            report.rejected = true;
            return report;
        }
        if (index_.find(command.order_id) != nullptr) {
            ExecutionReport report;
            report.incoming_order_id = command.order_id;
            report.requested_quantity = command.quantity;
            report.rejected = true;
            return report;
        }
        if (command.type == OrderType::Market) {
            return execute_market(command);
        }
        return execute_limit(command);
    }

    std::size_t bid_level_count() const { return bid_count_; }
    std::size_t ask_level_count() const { return ask_count_; }
    std::size_t live_order_count() const { return index_.size(); }
    std::size_t pool_available() const { return pool_.available(); }

    const PriceLevel* best_bid() const { return bid_count_ == 0 ? nullptr : &bids_[0]; }
    const PriceLevel* best_ask() const { return ask_count_ == 0 ? nullptr : &asks_[0]; }

    std::size_t first_crossing_ask(std::uint32_t limit_price) const {
        return simd::find_first_ask_at_or_below(ask_prices_.data(), ask_count_, limit_price);
    }

    std::size_t first_crossing_bid(std::uint32_t limit_price) const {
        return simd::find_first_bid_at_or_above(bid_prices_.data(), bid_count_, limit_price);
    }

private:
    ExecutionReport execute_market(const OrderCommand& command) {
        ExecutionReport report;
        report.incoming_order_id = command.order_id;
        report.requested_quantity = command.quantity;
        report.accepted = true;
        auto remaining = command.quantity;
        if (command.side == Side::Buy) {
            match_against_asks(remaining, std::numeric_limits<std::uint32_t>::max(), report);
        } else {
            match_against_bids(remaining, 0, report);
        }
        return report;
    }

    ExecutionReport execute_limit(const OrderCommand& command) {
        ExecutionReport report;
        report.incoming_order_id = command.order_id;
        report.requested_quantity = command.quantity;
        report.accepted = true;

        auto remaining = command.quantity;
        if (command.side == Side::Buy) {
            match_against_asks(remaining, command.price, report);
        } else {
            match_against_bids(remaining, command.price, report);
        }

        if (remaining == 0) {
            return report;
        }

        Order* resting = pool_.allocate(command.order_id, command.price, remaining, command.side);
        if (resting == nullptr || !index_.insert(command.order_id, resting)) {
            if (resting != nullptr) pool_.release(resting);
            report.rejected = true;
            report.accepted = false;
            return report;
        }

        const bool inserted = command.side == Side::Buy ? append_bid(resting) : append_ask(resting);
        if (!inserted) {
            index_.erase(command.order_id);
            pool_.release(resting);
            report.rejected = true;
            report.accepted = false;
            return report;
        }

        report.resting = true;
        return report;
    }

    ExecutionReport cancel(std::uint64_t order_id) {
        ExecutionReport report;
        report.incoming_order_id = order_id;
        Order* order = index_.find(order_id);
        if (order == nullptr) {
            report.rejected = true;
            return report;
        }

        auto& levels = order->side == Side::Buy ? bids_ : asks_;
        auto& prices = order->side == Side::Buy ? bid_prices_ : ask_prices_;
        auto& count = order->side == Side::Buy ? bid_count_ : ask_count_;
        const std::size_t level_idx = find_level(levels, count, order->price);
        if (level_idx == count) {
            report.rejected = true;
            return report;
        }

        unlink_order(levels[level_idx], order);
        levels[level_idx].total_volume -= order->quantity;
        if (levels[level_idx].head_order == nullptr) {
            erase_level(levels, prices, count, level_idx);
        }

        index_.erase(order_id);
        pool_.release(order);
        report.accepted = true;
        report.canceled = true;
        return report;
    }

    void match_against_asks(std::uint32_t& remaining, std::uint32_t limit_price, ExecutionReport& report) {
        while (remaining > 0 && ask_count_ > 0 && asks_[0].price <= limit_price) {
            match_level(asks_[0], remaining, report);
            if (asks_[0].head_order == nullptr) {
                erase_level(asks_, ask_prices_, ask_count_, 0);
            }
        }
    }

    void match_against_bids(std::uint32_t& remaining, std::uint32_t limit_price, ExecutionReport& report) {
        while (remaining > 0 && bid_count_ > 0 && bids_[0].price >= limit_price) {
            match_level(bids_[0], remaining, report);
            if (bids_[0].head_order == nullptr) {
                erase_level(bids_, bid_prices_, bid_count_, 0);
            }
        }
    }

    void match_level(PriceLevel& level, std::uint32_t& remaining, ExecutionReport& report) {
        while (remaining > 0 && level.head_order != nullptr) {
            Order* resting = level.head_order;
            const std::uint32_t fill = resting->quantity < remaining ? resting->quantity : remaining;
            resting->quantity -= fill;
            remaining -= fill;
            level.total_volume -= fill;
            report.filled_quantity += fill;
            report.notional += static_cast<std::uint64_t>(fill) * resting->price;
            ++report.trade_count;

            if (resting->quantity == 0) {
                unlink_order(level, resting);
                index_.erase(resting->order_id);
                pool_.release(resting);
            }
        }
    }

    bool append_bid(Order* order) {
        std::size_t idx = 0;
        while (idx < bid_count_ && bids_[idx].price > order->price) ++idx;
        return append_order_at_level(bids_, bid_prices_, bid_count_, idx, order);
    }

    bool append_ask(Order* order) {
        std::size_t idx = 0;
        while (idx < ask_count_ && asks_[idx].price < order->price) ++idx;
        return append_order_at_level(asks_, ask_prices_, ask_count_, idx, order);
    }

    bool append_order_at_level(std::array<PriceLevel, MaxPriceLevels>& levels,
                               std::array<std::uint32_t, MaxPriceLevels>& prices,
                               std::size_t& count,
                               std::size_t idx,
                               Order* order) {
        if (idx < count && levels[idx].price == order->price) {
            link_tail(levels[idx], order);
            levels[idx].total_volume += order->quantity;
            return true;
        }
        if (count == MaxPriceLevels) {
            return false;
        }
        for (std::size_t i = count; i > idx; --i) {
            levels[i] = levels[i - 1];
            prices[i] = prices[i - 1];
        }
        ++count;
        levels[idx] = PriceLevel{};
        levels[idx].price = order->price;
        prices[idx] = order->price;
        link_tail(levels[idx], order);
        levels[idx].total_volume = order->quantity;
        return true;
    }

    static void link_tail(PriceLevel& level, Order* order) {
        order->prev = level.tail_order;
        order->next = nullptr;
        if (level.tail_order != nullptr) {
            level.tail_order->next = order;
        } else {
            level.head_order = order;
        }
        level.tail_order = order;
        ++level.order_count;
    }

    static void unlink_order(PriceLevel& level, Order* order) {
        if (order->prev != nullptr) {
            order->prev->next = order->next;
        } else {
            level.head_order = order->next;
        }
        if (order->next != nullptr) {
            order->next->prev = order->prev;
        } else {
            level.tail_order = order->prev;
        }
        order->next = nullptr;
        order->prev = nullptr;
        --level.order_count;
    }

    static std::size_t find_level(const std::array<PriceLevel, MaxPriceLevels>& levels,
                                  std::size_t count,
                                  std::uint32_t price) {
        for (std::size_t i = 0; i < count; ++i) {
            if (levels[i].price == price) return i;
        }
        return count;
    }

    static void erase_level(std::array<PriceLevel, MaxPriceLevels>& levels,
                            std::array<std::uint32_t, MaxPriceLevels>& prices,
                            std::size_t& count,
                            std::size_t idx) {
        for (std::size_t i = idx + 1; i < count; ++i) {
            levels[i - 1] = levels[i];
            prices[i - 1] = prices[i];
        }
        --count;
        levels[count] = PriceLevel{};
        prices[count] = 0;
    }

    ObjectPool<MaxOrders> pool_{};
    OrderIndex<IndexCapacity> index_{};
    alignas(64) std::array<PriceLevel, MaxPriceLevels> bids_{};
    alignas(64) std::array<PriceLevel, MaxPriceLevels> asks_{};
    alignas(64) std::array<std::uint32_t, MaxPriceLevels> bid_prices_{};
    alignas(64) std::array<std::uint32_t, MaxPriceLevels> ask_prices_{};
    std::size_t bid_count_{0};
    std::size_t ask_count_{0};
};

}  // namespace lob
