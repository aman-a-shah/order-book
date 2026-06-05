#pragma once

#include <array>
#include <atomic>
#include <cstddef>
#include <cstdint>

namespace lob {

template <typename T, std::size_t Capacity>
class SpscQueue {
public:
    static_assert((Capacity & (Capacity - 1)) == 0, "SPSC capacity must be a power of two");

    bool push(const T& value) {
        const std::uint64_t tail = tail_.value.load(std::memory_order_relaxed);
        const std::uint64_t head = head_.value.load(std::memory_order_acquire);
        if (tail - head == Capacity) {
            return false;
        }
        buffer_[tail & mask_] = value;
        tail_.value.store(tail + 1, std::memory_order_release);
        return true;
    }

    bool pop(T& out) {
        const std::uint64_t head = head_.value.load(std::memory_order_relaxed);
        const std::uint64_t tail = tail_.value.load(std::memory_order_acquire);
        if (head == tail) {
            return false;
        }
        out = buffer_[head & mask_];
        head_.value.store(head + 1, std::memory_order_release);
        return true;
    }

    std::size_t size() const {
        const std::uint64_t tail = tail_.value.load(std::memory_order_acquire);
        const std::uint64_t head = head_.value.load(std::memory_order_acquire);
        return static_cast<std::size_t>(tail - head);
    }

    bool empty() const { return size() == 0; }

private:
    struct alignas(64) Cursor {
        std::atomic<std::uint64_t> value{0};
    };

    static constexpr std::size_t mask_ = Capacity - 1;

    alignas(64) std::array<T, Capacity> buffer_{};
    Cursor head_{};
    Cursor tail_{};
};

}  // namespace lob
