// The macOS debugger check (debug/debugger.h; Phase 26 Day 307): the kernel's process flags through sysctl(KERN_PROC_PID), P_TRACED while a
// debugger (lldb) is attached. Cached for a second, as on Linux. Allocates nothing (the assert path asks).
#include "debug/debugger.h"

#include "os/os.h"
#include "sync/atomic.h"

#include <sys/sysctl.h>
#include <unistd.h>

namespace engine_placeholder::debug {

namespace {

constexpr u64 kCacheNanos = 1000ull * 1000 * 1000;
// One word holds both the time of the last check and the answer (time << 1 | attached), so a reader never pairs a fresh time with a stale answer.
Atomic<u64> g_debugger_state{0};

bool ReadTracerState() {
    int mib[4] = {CTL_KERN, KERN_PROC, KERN_PROC_PID, ::getpid()};
    struct kinfo_proc info;
    size_t size = sizeof(info);
    info.kp_proc.p_flag = 0;
    if (::sysctl(mib, 4, &info, &size, nullptr, 0) != 0 || size != sizeof(info)) return false;
    return (info.kp_proc.p_flag & P_TRACED) != 0;
}

} // namespace

bool IsDebuggerAttached() {
    const u64 now = os::MonotonicTimeNanos() | 1; // never 0, so a stored state is never mistaken for "never checked"
    const u64 state = g_debugger_state.Load(MemoryOrder::Relaxed);
    if (state != 0 && now - (state >> 1) < kCacheNanos) {
        return (state & 1) != 0;
    }
    const bool attached = ReadTracerState();
    g_debugger_state.Store((now << 1) | (attached ? 1ull : 0ull), MemoryOrder::Relaxed);
    return attached;
}

} // namespace engine_placeholder::debug
