// The Linux half of the job system's platform interface (jobs/detail/thread_platform.h); moved out of topology.cpp and worker_pool.cpp on
// Phase 13 Day 153, unchanged.
#include "jobs/detail/thread_platform.h"

#include <pthread.h>
#include <sched.h>
#include <sys/syscall.h>
#include <unistd.h>

namespace engine_placeholder::jobs::detail {

usize QueryAllowedCpus(u32* out, usize capacity) {
    cpu_set_t set;
    CPU_ZERO(&set);
    if (sched_getaffinity(0, sizeof(set), &set) != 0) return 0;
    usize n = 0;
    for (u32 cpu = 0; cpu < CPU_SETSIZE && n < capacity; ++cpu) {
        if (CPU_ISSET(cpu, &set)) out[n++] = cpu;
    }
    return n;
}

i32 CurrentThreadId() { return static_cast<i32>(::syscall(SYS_gettid)); }

bool CanPinThreads() { return true; }

bool PinCurrentThread(u32 cpu) {
    cpu_set_t set;
    CPU_ZERO(&set);
    CPU_SET(cpu, &set);
    return pthread_setaffinity_np(pthread_self(), sizeof(set), &set) == 0;
}

void NameCurrentThread(const char*) {}

} // namespace engine_placeholder::jobs::detail
