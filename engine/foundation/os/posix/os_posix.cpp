#include "posix_os.h"

#include "cstr/cstr.h"
#include "cstr/mem_ops.h"
#include "os/console.h"  // ::exit (the C library header os::Abort comes from)
#include "os/last_error.h"
#include "os/terminal.h"
#include <dlfcn.h>
#include <fcntl.h>
#include <poll.h>
#include <sys/mman.h>
#include <time.h>
#include <unistd.h>

// The functions of os.h that are the same POSIX calls on Linux and macOS (one difference handled: poll on a device,
// WriteAllWithTimeout) (Phase 21 Day 285, moved from os/linux/os.cpp). The clock, random bytes, process figures,
// library loading and the environment pointer are per platform (os/linux/os.cpp, os/macos/os.cpp).
namespace vse::os
{

namespace posix
{

const char* ToCPath(StringView path, char (&buffer)[kMaxPathBytes])
{
    if (path.Length() >= kMaxPathBytes)
    {
        return nullptr;
    }
    for (usize i = 0; i < path.Length(); ++i)
    {
        buffer[i] = path[i];
    }
    buffer[path.Length()] = '\0';
    return buffer;
}

}  // namespace posix

void Exit(int code)
{
    ::exit(code);
}

long Write(int fd, const void* buf, usize len)
{
    return ::write(fd, buf, len);
}

bool IsTerminal(int fd)
{
    return ::isatty(fd) != 0;
}

bool EnableTerminalSequences(int)
{
    return true;
}

long WriteAllWithTimeout(int fd, const void* buf, usize len, u64 timeout_nanos)
{
    const char* bytes = static_cast<const char*>(buf);
    usize done = 0;
    const u64 deadline = MonotonicTimeNanos() + timeout_nanos;
    while (done < len)
    {
        // Wait until the descriptor can take a small chunk (a pipe whose reader stopped never can), then write only
        // that much, so the write itself cannot block past the deadline.
        const u64 now = MonotonicTimeNanos();
        const int wait_ms = deadline > now ? static_cast<int>((deadline - now + 999999ull) / 1000000ull) : 0;
        struct pollfd waiting = {fd, POLLOUT, 0};
        const int ready = ::poll(&waiting, 1, wait_ms);
        if (ready < 0 && os::LastError() == EINTR)
        {
            continue;
        }
        // macOS's poll does not support some devices (/dev/null answers POLLNVAL; Linux answers POLLOUT): a descriptor
        // that is open is then written without the wait, as a device of that kind does not block. A closed one stays a
        // failure.
        const bool unpollable_device = ready > 0 && (waiting.revents & POLLNVAL) != 0 && ::fcntl(fd, F_GETFD) != -1;
        if (!unpollable_device && (ready <= 0 || (waiting.revents & (POLLERR | POLLHUP | POLLNVAL)) != 0))
        {
            break;
        }
        const usize chunk = len - done < 512 ? len - done : 512;
        const long n = ::write(fd, bytes + done, chunk);
        if (n > 0)
        {
            done += static_cast<usize>(n);
        }
        else if (!(n < 0 && (os::LastError() == EINTR || os::LastError() == EAGAIN)))
        {
            break;
        }
    }
    return static_cast<long>(done);
}

long Read(int fd, void* buf, usize len)
{
    return ::read(fd, buf, len);
}

int Open(StringView path, int flags, int mode)
{
    char buf[kMaxPathBytes];
    const char* c_path = posix::ToCPath(path, buf);
    if (c_path == nullptr)
    {
        return -1;
    }
    return ::open(c_path, flags, mode);
}

int CreateFileForWriting(StringView path, int mode)
{
    return Open(path, O_WRONLY | O_CREAT | O_TRUNC, mode);
}

int Close(int fd)
{
    return ::close(fd);
}

void* ReserveMemory(usize size)
{
    // Anonymous private pages are zero-filled on both systems; their size is the system's (4 KiB on x86-64, 16 KiB on
    // Apple silicon).
    void* mem = ::mmap(nullptr, size, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    return mem == MAP_FAILED ? nullptr : mem;
}

void ReleaseMemory(void* addr, usize size)
{
    ::munmap(addr, size);
}

u64 WallClockUnixMillis()
{
    struct timespec ts;
    ::clock_gettime(CLOCK_REALTIME, &ts);
    return static_cast<u64>(ts.tv_sec) * 1000ULL + static_cast<u64>(ts.tv_nsec) / 1'000'000ULL;
}

void SleepNanos(u64 nanos)
{
    struct timespec req;
    req.tv_sec = static_cast<time_t>(nanos / 1'000'000'000ULL);
    req.tv_nsec = static_cast<long>(nanos % 1'000'000'000ULL);
    struct timespec rem;
    while (::nanosleep(&req, &rem) == -1 && os::LastError() == EINTR)
    {
        req = rem;
    }
}

void* GetSymbol(DynamicLibraryHandle handle, const char* symbol_name)
{
    return ::dlsym(handle, symbol_name);
}

void UnloadLibrary(DynamicLibraryHandle handle)
{
    if (handle != nullptr)
    {
        ::dlclose(handle);
    }
}

bool EnvironmentNamesIgnoreCase()
{
    return false;
}

usize EnvironmentCount()
{
    char** const environment = posix::ProcessEnvironment();
    usize n = 0;
    if (environment != nullptr)
    {
        while (environment[n] != nullptr)
        {
            ++n;
        }
    }
    return n;
}

bool EnvironmentEntry(usize index, char* buffer, usize capacity, usize* length)
{
    *length = 0;
    if (capacity > 0)
    {
        buffer[0] = '\0';
    }
    char** const environment = posix::ProcessEnvironment();
    if (environment == nullptr)
    {
        return false;
    }
    for (usize i = 0; i < index; ++i)
    {  // walk to the entry; stops at the end of the array
        if (environment[i] == nullptr)
        {
            return false;
        }
    }
    const char* entry = environment[index];
    if (entry == nullptr)
    {
        return false;
    }
    const usize full = CStrLength(entry);
    *length = full;
    if (capacity > 0)
    {
        const usize copy = full < capacity - 1 ? full : capacity - 1;
        MemCopy(buffer, entry, copy);
        buffer[copy] = '\0';
    }
    return true;
}

}  // namespace vse::os
