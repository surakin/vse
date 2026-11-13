#include "../os.h"

#include "os/last_error.h"
#include "os/console.h"
#include "os/posix/posix_os.h"
#include "cstr/format.h"
#include <time.h>
#include <dirent.h>
#include <dlfcn.h>
#include <fcntl.h>
#include <sys/random.h>
#include <unistd.h>

// Linux's half of os.h; the calls Linux shares with macOS are in os/posix/os_posix.cpp (Phase 21 Day 285).
namespace engine_placeholder::os
{

namespace posix
{

char** ProcessEnvironment()
{
    return environ;
}

}  // namespace posix

i64 ResidentMemoryKiB()
{
    const int fd = ::open("/proc/self/statm", O_RDONLY | O_CLOEXEC);
    if (fd < 0)
    {
        return 0;
    }
    char text[128];
    const ssize_t n = ::read(fd, text, sizeof(text) - 1);
    ::close(fd);
    if (n <= 0)
    {
        return 0;
    }
    text[n] = '\0';
    long pages = 0;
    long resident = 0;
    if (ScanFrom(text, "%ld %ld", &pages, &resident) != 2)
    {
        return 0;
    }
    const long page_bytes = ::sysconf(_SC_PAGESIZE);
    return page_bytes > 0 ? static_cast<i64>(resident) * (page_bytes / 1024) : 0;
}

i64 OpenHandleCount()
{
    DIR* dir = ::opendir("/proc/self/fd");
    if (dir == nullptr)
    {
        return 0;
    }
    i64 count = 0;
    while (const dirent* entry = ::readdir(dir))
    {
        if (entry->d_name[0] != '.')
        {
            ++count;  // not . and ..
        }
    }
    ::closedir(dir);
    return count > 0 ? count - 1 : 0;  // without the descriptor of the directory itself
}

u64 MonotonicTimeNanos()
{
    struct timespec ts;
    ::clock_gettime(CLOCK_MONOTONIC, &ts);
    return static_cast<u64>(ts.tv_sec) * 1'000'000'000ULL + static_cast<u64>(ts.tv_nsec);
}

void FillRandomBytes(void* buffer, usize count)
{
    char* out = static_cast<char*>(buffer);
    usize done = 0;
    while (done < count)
    {
        const isize got = ::getrandom(out + done, count - done, 0);
        if (got < 0)
        {
            if (os::LastError() == EINTR)
            {
                continue;
            }
            os::Abort();
        }
        done += static_cast<usize>(got);
    }
}

DynamicLibraryHandle LoadLibrary(StringView path)
{
    char buf[kMaxPathBytes];
    const char* c_path = posix::ToCPath(path, buf);
    if (c_path == nullptr)
    {
        return nullptr;
    }
    return ::dlopen(c_path, RTLD_NOW | RTLD_LOCAL);
}

}  // namespace engine_placeholder::os
