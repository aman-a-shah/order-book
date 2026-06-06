#pragma once

#include <array>
#include <cstddef>
#include <cstdint>

namespace lob {

template <std::size_t BucketCount = 64>
class PowerOfTwoHistogram {
public:
    void observe(std::uint64_t value) {
        ++count_;
        total_ += value;
        if (value > max_) max_ = value;
        ++buckets_[bucket_for(value)];
    }

    std::uint64_t count() const { return count_; }
    std::uint64_t max() const { return max_; }

    double mean() const {
        return count_ == 0 ? 0.0 : static_cast<double>(total_) / static_cast<double>(count_);
    }

    std::uint64_t percentile(double pct) const {
        if (count_ == 0) return 0;
        std::uint64_t rank = static_cast<std::uint64_t>((pct / 100.0) * static_cast<double>(count_ - 1)) + 1;
        std::uint64_t seen = 0;
        for (std::size_t i = 0; i < buckets_.size(); ++i) {
            seen += buckets_[i];
            if (seen >= rank) {
                return upper_bound_for_bucket(i);
            }
        }
        return max_;
    }

private:
    static std::size_t bucket_for(std::uint64_t value) {
        std::size_t bucket = 0;
        while (value > 1 && bucket + 1 < BucketCount) {
            value >>= 1U;
            ++bucket;
        }
        return bucket;
    }

    static std::uint64_t upper_bound_for_bucket(std::size_t bucket) {
        if (bucket == 0) return 1;
        if (bucket >= 63) return UINT64_MAX;
        return (1ULL << (bucket + 1U)) - 1ULL;
    }

    std::array<std::uint64_t, BucketCount> buckets_{};
    std::uint64_t count_{0};
    long double total_{0};
    std::uint64_t max_{0};
};

}  // namespace lob
