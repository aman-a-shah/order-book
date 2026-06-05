#pragma once

#include <array>
#include <cstddef>
#include <cstdint>

#include "lob/types.hpp"

namespace lob {

template <std::size_t Capacity>
class OrderIndex {
public:
    bool insert(std::uint64_t order_id, Order* order) {
        std::size_t slot = hash(order_id);
        for (std::size_t probes = 0; probes < Capacity; ++probes) {
            Entry& entry = entries_[slot];
            if (!entry.occupied || entry.deleted) {
                entry.order_id = order_id;
                entry.order = order;
                entry.occupied = true;
                entry.deleted = false;
                ++size_;
                return true;
            }
            if (entry.order_id == order_id) {
                return false;
            }
            slot = (slot + 1) & (Capacity - 1);
        }
        return false;
    }

    Order* find(std::uint64_t order_id) const {
        std::size_t slot = hash(order_id);
        for (std::size_t probes = 0; probes < Capacity; ++probes) {
            const Entry& entry = entries_[slot];
            if (!entry.occupied) {
                return nullptr;
            }
            if (!entry.deleted && entry.order_id == order_id) {
                return entry.order;
            }
            slot = (slot + 1) & (Capacity - 1);
        }
        return nullptr;
    }

    bool erase(std::uint64_t order_id) {
        std::size_t slot = hash(order_id);
        for (std::size_t probes = 0; probes < Capacity; ++probes) {
            Entry& entry = entries_[slot];
            if (!entry.occupied) {
                return false;
            }
            if (!entry.deleted && entry.order_id == order_id) {
                entry.deleted = true;
                entry.order = nullptr;
                --size_;
                return true;
            }
            slot = (slot + 1) & (Capacity - 1);
        }
        return false;
    }

    std::size_t size() const { return size_; }

private:
    static_assert((Capacity & (Capacity - 1)) == 0, "OrderIndex capacity must be a power of two");

    struct Entry {
        std::uint64_t order_id{0};
        Order* order{nullptr};
        bool occupied{false};
        bool deleted{false};
    };

    static std::size_t hash(std::uint64_t value) {
        value ^= value >> 33;
        value *= 0xff51afd7ed558ccdULL;
        value ^= value >> 33;
        value *= 0xc4ceb9fe1a85ec53ULL;
        value ^= value >> 33;
        return static_cast<std::size_t>(value) & (Capacity - 1);
    }

    alignas(64) std::array<Entry, Capacity> entries_{};
    std::size_t size_{0};
};

}  // namespace lob
