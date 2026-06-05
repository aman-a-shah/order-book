#pragma once

#include <array>
#include <cstddef>
#include <cstdint>

#include "lob/types.hpp"

namespace lob {

template <std::size_t Capacity>
class ObjectPool {
public:
    ObjectPool() {
        for (std::size_t i = 0; i < Capacity; ++i) {
            free_list_[i] = Capacity - 1 - i;
        }
    }

    Order* allocate(std::uint64_t order_id, std::uint32_t price, std::uint32_t quantity, Side side) {
        if (free_count_ == 0) {
            return nullptr;
        }
        const auto idx = free_list_[--free_count_];
        Order& order = orders_[idx];
        order.order_id = order_id;
        order.price = price;
        order.quantity = quantity;
        order.side = side;
        order.next = nullptr;
        order.prev = nullptr;
        return &order;
    }

    void release(Order* order) {
        if (order == nullptr) {
            return;
        }
        const auto idx = static_cast<std::size_t>(order - orders_.data());
        order->next = nullptr;
        order->prev = nullptr;
        order->quantity = 0;
        free_list_[free_count_++] = idx;
    }

    std::size_t capacity() const { return Capacity; }
    std::size_t available() const { return free_count_; }
    std::size_t used() const { return Capacity - free_count_; }

private:
    alignas(64) std::array<Order, Capacity> orders_{};
    std::array<std::size_t, Capacity> free_list_{};
    std::size_t free_count_{Capacity};
};

}  // namespace lob
