#pragma once

#include <chrono>
#include <cstdint>

#if defined(__x86_64__) || defined(_M_X64) || defined(__i386__)
#include <x86intrin.h>
#endif

namespace lob {

inline std::uint64_t read_cycle_counter() {
#if defined(__x86_64__) || defined(_M_X64) || defined(__i386__)
    return __rdtsc();
#elif defined(__aarch64__)
    std::uint64_t value = 0;
    asm volatile("mrs %0, cntvct_el0" : "=r"(value));
    return value;
#else
    return static_cast<std::uint64_t>(
        std::chrono::high_resolution_clock::now().time_since_epoch().count());
#endif
}

class Stopwatch {
public:
    Stopwatch() : start_(clock::now()) {}

    double elapsed_seconds() const {
        const auto end = clock::now();
        return std::chrono::duration<double>(end - start_).count();
    }

private:
    using clock = std::chrono::steady_clock;
    clock::time_point start_;
};

}  // namespace lob
