#pragma once

#include <array>
#include <cstddef>
#include <cstdint>

#include "lob/types.hpp"

namespace lob {

constexpr std::size_t BinaryMessageSize = 40;

inline void write_u32(std::array<std::uint8_t, BinaryMessageSize>& out, std::size_t offset, std::uint32_t value) {
    for (std::size_t i = 0; i < 4; ++i) out[offset + i] = static_cast<std::uint8_t>(value >> (i * 8U));
}

inline void write_u64(std::array<std::uint8_t, BinaryMessageSize>& out, std::size_t offset, std::uint64_t value) {
    for (std::size_t i = 0; i < 8; ++i) out[offset + i] = static_cast<std::uint8_t>(value >> (i * 8U));
}

inline std::uint32_t read_u32(const std::array<std::uint8_t, BinaryMessageSize>& in, std::size_t offset) {
    std::uint32_t value = 0;
    for (std::size_t i = 0; i < 4; ++i) value |= static_cast<std::uint32_t>(in[offset + i]) << (i * 8U);
    return value;
}

inline std::uint64_t read_u64(const std::array<std::uint8_t, BinaryMessageSize>& in, std::size_t offset) {
    std::uint64_t value = 0;
    for (std::size_t i = 0; i < 8; ++i) value |= static_cast<std::uint64_t>(in[offset + i]) << (i * 8U);
    return value;
}

inline std::array<std::uint8_t, BinaryMessageSize> encode_binary_order(const OrderCommand& command) {
    std::array<std::uint8_t, BinaryMessageSize> out{};
    out[0] = static_cast<std::uint8_t>(command.type);
    out[1] = static_cast<std::uint8_t>(command.side);
    write_u64(out, 2, command.order_id);
    write_u32(out, 10, command.price);
    write_u32(out, 14, command.quantity);
    write_u64(out, 18, command.new_order_id);
    write_u32(out, 26, command.symbol_id);
    write_u32(out, 30, 0x4f424c31U);
    return out;
}

inline bool decode_binary_order(const std::array<std::uint8_t, BinaryMessageSize>& in, OrderCommand& command) {
    if (read_u32(in, 30) != 0x4f424c31U) {
        return false;
    }
    if (in[0] > static_cast<std::uint8_t>(OrderType::Replace) || in[1] > static_cast<std::uint8_t>(Side::Sell)) {
        return false;
    }
    command = OrderCommand{
        static_cast<OrderType>(in[0]),
        static_cast<Side>(in[1]),
        read_u64(in, 2),
        read_u32(in, 10),
        read_u32(in, 14),
        read_u64(in, 18),
        read_u32(in, 26)
    };
    return true;
}

}  // namespace lob
