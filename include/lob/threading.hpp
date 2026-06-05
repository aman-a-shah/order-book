#pragma once

#include <cstddef>

#if defined(__linux__)
#include <pthread.h>
#include <sched.h>
#endif

namespace lob {

inline bool pin_current_thread_to_core(std::size_t core_id) {
#if defined(__linux__)
    cpu_set_t cpuset;
    CPU_ZERO(&cpuset);
    CPU_SET(core_id, &cpuset);
    return pthread_setaffinity_np(pthread_self(), sizeof(cpu_set_t), &cpuset) == 0;
#else
    (void)core_id;
    return false;
#endif
}

}  // namespace lob
