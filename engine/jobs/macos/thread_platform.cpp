// The macOS half of the job system's platform interface (jobs/detail/thread_platform.h; Phase 26 Day 305).
#include "jobs/detail/thread_platform.h"

#include "cstr/mem_ops.h"
#include "cstr/cstr.h"

#include <pthread.h>
#include <sys/sysctl.h>

namespace engine_placeholder::jobs::detail
{

usize QueryAllowedCpus(u32* out, usize capacity)
{
    // macOS has no per-process CPU mask: every active logical CPU may run any thread.
    int active = 0;
    size_t size = sizeof(active);
    if (::sysctlbyname("hw.activecpu", &active, &size, nullptr, 0) != 0 || active <= 0)
    {
        return 0;
    }
    usize n = 0;
    for (u32 cpu = 0; cpu < static_cast<u32>(active) && n < capacity; ++cpu)
    {
        out[n++] = cpu;
    }
    return n;
}

i32 CurrentThreadId()
{
    // A 64-bit id that counts up from boot; the low 31 bits keep it positive in the interface's i32 (it is for logs and
    // tests). It is for logs and tests: never 0 (the low bits of an id that wrapped past 2^31 can be), and 1 if the
    // call fails (it does not for the calling thread).
    u64 id = 0;
    if (::pthread_threadid_np(nullptr, &id) != 0)
    {
        return 1;
    }
    const i32 low = static_cast<i32>(id & 0x7FFFFFFFu);
    return low != 0 ? low : 1;
}

bool CanPinThreads()
{
    return false;
}

bool PinCurrentThread(u32)
{
    // No affinity API on macOS: THREAD_AFFINITY_POLICY is a grouping hint the scheduler may ignore, and Apple silicon
    // does not honor it.
    return false;
}

void NameCurrentThread(const char* name)
{
    // pthread_setname_np names the calling thread only (there is no form that names another), so the worker calls it on
    // itself; the name is cut to 63 bytes (MAXTHREADNAMESIZE 64 with the NUL) on a UTF-8 boundary, so a long name never
    // ends in half a character.
    char cut[64];
    usize n = CStrLength(name);
    if (n > sizeof(cut) - 1)
    {
        n = sizeof(cut) - 1;
        while (n > 0 && (static_cast<u8>(name[n]) & 0xC0) == 0x80)
        {
            --n;  // name[n] is a continuation byte: drop the character it belongs to
        }
    }
    MemCopy(cut, name, n);
    cut[n] = '\0';
    ::pthread_setname_np(cut);
}

}  // namespace engine_placeholder::jobs::detail
