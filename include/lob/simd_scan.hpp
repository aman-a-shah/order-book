#pragma once

#include <cstddef>
#include <cstdint>

#if defined(__AVX2__)
#include <immintrin.h>
#elif defined(__ARM_NEON) || defined(__ARM_NEON__)
#include <arm_neon.h>
#endif

namespace lob::simd {

inline std::size_t find_first_ask_at_or_below(const std::uint32_t* prices,
                                              std::size_t count,
                                              std::uint32_t limit_price) {
#if defined(__AVX2__)
    const __m256i limit = _mm256_set1_epi32(static_cast<int>(limit_price));
    const __m256i one = _mm256_set1_epi32(1);
    std::size_t i = 0;
    for (; i + 8 <= count; i += 8) {
        const __m256i p = _mm256_loadu_si256(reinterpret_cast<const __m256i*>(prices + i));
        const __m256i p_minus_one = _mm256_sub_epi32(p, one);
        const __m256i cmp = _mm256_cmpgt_epi32(limit, p_minus_one);
        const int mask = _mm256_movemask_ps(_mm256_castsi256_ps(cmp));
        if (mask != 0) {
            return i + static_cast<std::size_t>(__builtin_ctz(mask));
        }
    }
    for (; i < count; ++i) {
        if (prices[i] <= limit_price) return i;
    }
    return count;
#elif defined(__ARM_NEON) || defined(__ARM_NEON__)
    const uint32x4_t limit = vdupq_n_u32(limit_price);
    std::size_t i = 0;
    for (; i + 4 <= count; i += 4) {
        const uint32x4_t p = vld1q_u32(prices + i);
        const uint32x4_t cmp = vcleq_u32(p, limit);
        const std::uint64_t bits =
            (static_cast<std::uint64_t>(vgetq_lane_u32(cmp, 0) != 0) << 0) |
            (static_cast<std::uint64_t>(vgetq_lane_u32(cmp, 1) != 0) << 1) |
            (static_cast<std::uint64_t>(vgetq_lane_u32(cmp, 2) != 0) << 2) |
            (static_cast<std::uint64_t>(vgetq_lane_u32(cmp, 3) != 0) << 3);
        if (bits != 0) {
            return i + static_cast<std::size_t>(__builtin_ctzll(bits));
        }
    }
    for (; i < count; ++i) {
        if (prices[i] <= limit_price) return i;
    }
    return count;
#else
    for (std::size_t i = 0; i < count; ++i) {
        if (prices[i] <= limit_price) return i;
    }
    return count;
#endif
}

inline std::size_t find_first_bid_at_or_above(const std::uint32_t* prices,
                                              std::size_t count,
                                              std::uint32_t limit_price) {
#if defined(__AVX2__)
    const __m256i limit_minus_one = _mm256_set1_epi32(static_cast<int>(limit_price - 1));
    std::size_t i = 0;
    for (; i + 8 <= count; i += 8) {
        const __m256i p = _mm256_loadu_si256(reinterpret_cast<const __m256i*>(prices + i));
        const __m256i cmp = _mm256_cmpgt_epi32(p, limit_minus_one);
        const int mask = _mm256_movemask_ps(_mm256_castsi256_ps(cmp));
        if (mask != 0) {
            return i + static_cast<std::size_t>(__builtin_ctz(mask));
        }
    }
    for (; i < count; ++i) {
        if (prices[i] >= limit_price) return i;
    }
    return count;
#elif defined(__ARM_NEON) || defined(__ARM_NEON__)
    const uint32x4_t limit = vdupq_n_u32(limit_price);
    std::size_t i = 0;
    for (; i + 4 <= count; i += 4) {
        const uint32x4_t p = vld1q_u32(prices + i);
        const uint32x4_t cmp = vcgeq_u32(p, limit);
        const std::uint64_t bits =
            (static_cast<std::uint64_t>(vgetq_lane_u32(cmp, 0) != 0) << 0) |
            (static_cast<std::uint64_t>(vgetq_lane_u32(cmp, 1) != 0) << 1) |
            (static_cast<std::uint64_t>(vgetq_lane_u32(cmp, 2) != 0) << 2) |
            (static_cast<std::uint64_t>(vgetq_lane_u32(cmp, 3) != 0) << 3);
        if (bits != 0) {
            return i + static_cast<std::size_t>(__builtin_ctzll(bits));
        }
    }
    for (; i < count; ++i) {
        if (prices[i] >= limit_price) return i;
    }
    return count;
#else
    for (std::size_t i = 0; i < count; ++i) {
        if (prices[i] >= limit_price) return i;
    }
    return count;
#endif
}

}  // namespace lob::simd
