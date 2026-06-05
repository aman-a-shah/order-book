#pragma once

#include <cstdint>

namespace lob {

enum class Side : std::uint8_t { Buy = 0, Sell = 1 };
enum class OrderType : std::uint8_t { Limit = 0, Market = 1, Cancel = 2 };

struct OrderCommand {
    OrderType type{OrderType::Limit};
    Side side{Side::Buy};
    std::uint64_t order_id{0};
    std::uint32_t price{0};
    std::uint32_t quantity{0};
};

struct Order {
    std::uint64_t order_id{0};
    std::uint32_t price{0};
    std::uint32_t quantity{0};
    Side side{Side::Buy};
    Order* next{nullptr};
    Order* prev{nullptr};
};

struct alignas(32) PriceLevel {
    std::uint32_t price{0};
    std::uint32_t total_volume{0};
    Order* head_order{nullptr};
    Order* tail_order{nullptr};
    std::uint64_t order_count{0};
};

static_assert(sizeof(PriceLevel) == 32, "PriceLevel must stay one 256-bit cache/SIMD-sized record");

struct ExecutionReport {
    std::uint64_t incoming_order_id{0};
    std::uint32_t requested_quantity{0};
    std::uint32_t filled_quantity{0};
    std::uint64_t trade_count{0};
    std::uint64_t notional{0};
    bool accepted{false};
    bool resting{false};
    bool canceled{false};
    bool rejected{false};

    std::uint32_t remaining() const {
        return requested_quantity - filled_quantity;
    }
};

}  // namespace lob
