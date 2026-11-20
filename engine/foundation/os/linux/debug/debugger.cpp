#include "debug/debugger.h"
#include "os/linux/debug/debugger_linux.h"

#include "os/os.h"

#include "sync/atomic.h"
#include <fcntl.h>

namespace vse::debug
{

namespace detail
{

bool StatusShowsTracer(const char* text, usize length)
{
    static constexpr char kKey[] = "TracerPid:";
    const usize key_length = sizeof(kKey) - 1;
    for (usize i = 0; i + key_length <= length; ++i)
    {
        // The key must start a line.
        if (i != 0 && text[i - 1] != '\n')
        {
            continue;
        }
        usize k = 0;
        while (k < key_length && text[i + k] == kKey[k])
        {
            ++k;
        }
        if (k != key_length)
        {
            continue;
        }
        usize p = i + key_length;
        while (p < length && (text[p] == ' ' || text[p] == '\t'))
        {
            ++p;
        }
        // Any digit other than a lone zero means a tracer.
        bool any_digit = false;
        bool nonzero = false;
        while (p < length && text[p] >= '0' && text[p] <= '9')
        {
            any_digit = true;
            nonzero = nonzero || text[p] != '0';
            ++p;
        }
        return any_digit && nonzero;
    }
    return false;
}

}  // namespace detail

namespace
{

constexpr u64 kCacheNanos = 1000ull * 1000 * 1000;
// One word holds both the time of the last check and the answer (time << 1 | attached), so a reader never pairs a fresh
// time with a stale answer. 0 means never checked.
Atomic<u64> g_debugger_state{0};

bool ReadTracerState()
{
    const int fd = os::Open("/proc/self/status", O_RDONLY);
    if (fd < 0)
    {
        return false;
    }
    char text[4096];
    usize used = 0;
    for (;;)
    {
        const long n = os::Read(fd, text + used, sizeof(text) - used);
        if (n <= 0)
        {
            break;
        }
        used += static_cast<usize>(n);
        if (used == sizeof(text))
        {
            break;
        }
    }
    os::Close(fd);
    return detail::StatusShowsTracer(text, used);
}

}  // namespace

bool IsDebuggerAttached()
{
    const u64 now = os::MonotonicTimeNanos() | 1;  // never 0, so a stored state is never mistaken for "never checked"
    const u64 state = g_debugger_state.Load(MemoryOrder::Relaxed);
    if (state != 0 && now - (state >> 1) < kCacheNanos)
    {
        return (state & 1) != 0;
    }
    const bool attached = ReadTracerState();
    g_debugger_state.Store((now << 1) | (attached ? 1ull : 0ull), MemoryOrder::Relaxed);
    return attached;
}

}  // namespace vse::debug
