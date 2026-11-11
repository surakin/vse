#include "../os.h"

#include "os/console.h"
#include "os/last_error.h"
#include "os/posix/posix_os.h"
#include <crt_externs.h>
#include <dlfcn.h>
#include <libproc.h>
#include <mach/mach.h>
#include <sys/random.h>
#include <time.h>
#include <unistd.h>

// macOS's half of os.h (Phase 26 Day 301); the calls macOS shares with Linux are in os/posix/os_posix.cpp.
namespace engine_placeholder::os
{

namespace posix
{

// A library (the engine's are static, but a test or a plug-in may be a dylib) cannot name `environ` on macOS; the C
// library's accessor gives the same array, current after setenv.
char** ProcessEnvironment()
{
    return *::_NSGetEnviron();
}

}  // namespace posix

i64 ResidentMemoryKiB()
{
    mach_task_basic_info_data_t info;
    mach_msg_type_number_t count = MACH_TASK_BASIC_INFO_COUNT;
    if (::task_info(::mach_task_self(), MACH_TASK_BASIC_INFO, reinterpret_cast<task_info_t>(&info), &count) !=
        KERN_SUCCESS)
    {
        return 0;
    }
    return static_cast<i64>(info.resident_size / 1024);
}

i64 OpenHandleCount()
{
    // The first call answers how many bytes the list needs; the margin covers descriptors opened meanwhile. The list
    // goes in pages of our own (ReserveMemory), so the count does not touch the heap it may be measuring.
    const pid_t self = ::getpid();
    const int needed = ::proc_pidinfo(self, PROC_PIDLISTFDS, 0, nullptr, 0);
    if (needed <= 0)
    {
        return 0;
    }
    const usize capacity = static_cast<usize>(needed) + 64 * sizeof(proc_fdinfo);
    void* const list = ReserveMemory(capacity);
    if (list == nullptr)
    {
        return 0;
    }
    const int used = ::proc_pidinfo(self, PROC_PIDLISTFDS, 0, list, static_cast<int>(capacity));
    ReleaseMemory(list, capacity);
    return used > 0 ? static_cast<i64>(static_cast<usize>(used) / sizeof(proc_fdinfo)) : 0;
}

u64 MonotonicTimeNanos()
{
    // The clock that stops while the machine sleeps, as CLOCK_MONOTONIC does on Linux (macOS's CLOCK_MONOTONIC keeps
    // counting through sleep). Already in nanoseconds: no mach_absolute_time timebase to multiply, so no overflow to
    // guard.
    return ::clock_gettime_nsec_np(CLOCK_UPTIME_RAW);
}

void FillRandomBytes(void* buffer, usize count)
{
    // getentropy gives at most 256 bytes per call and fails only on a bad argument.
    char* out = static_cast<char*>(buffer);
    usize done = 0;
    while (done < count)
    {
        const usize chunk = count - done < 256 ? count - done : 256;
        if (::getentropy(out + done, chunk) != 0)
        {
            os::Abort();
        }
        done += chunk;
    }
}

namespace
{

void* OpenIn(const char* directory, StringView name)
{
    char full[kMaxPathBytes];
    usize n = 0;
    for (const char* p = directory; *p != '\0'; ++p)
    {
        full[n++] = *p;
    }
    if (n + name.Length() >= kMaxPathBytes)
    {
        return nullptr;
    }
    for (usize i = 0; i < name.Length(); ++i)
    {
        full[n++] = name[i];
    }
    full[n] = '\0';
    return ::dlopen(full, RTLD_NOW | RTLD_LOCAL);
}

}  // namespace

DynamicLibraryHandle LoadLibrary(StringView path)
{
    // dlopen searches the current directory for a bare name on macOS (dlopen(3)); the engine never does, as on Windows.
    // An absolute path is loaded as named; a bare name is looked for next to the program, then in /usr/lib; a relative
    // path with a directory is refused.
    if (path.Length() == 0 || path.Length() >= kMaxPathBytes)
    {
        return nullptr;
    }
    if (path[0] == '/')
    {
        char buf[kMaxPathBytes];
        return ::dlopen(posix::ToCPath(path, buf), RTLD_NOW | RTLD_LOCAL);
    }
    if (path.IndexOf('/') != path.Length())
    {
        return nullptr;
    }
    if (void* const next_to_program = OpenIn("@executable_path/", path))
    {
        return next_to_program;
    }
    return OpenIn("/usr/lib/", path);
}

}  // namespace engine_placeholder::os
