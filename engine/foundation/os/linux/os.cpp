#include "../os.h"

#include <cerrno>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <dirent.h>
#include <dlfcn.h>
#include <fcntl.h>
#include <poll.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <unistd.h>

extern "C" char** environ;

namespace engine_placeholder::os {

namespace {

// Copies `path` into `buffer` (size kMaxPathBytes) and null-terminates
// it, for POSIX calls that require a C string. Returns nullptr if `path`
// doesn't fit (including the terminator) rather than silently
// truncating.
const char* ToCPath(StringView path, char (&buffer)[kMaxPathBytes]) {
    if (path.Length() >= kMaxPathBytes) {
        return nullptr;
    }
    for (usize i = 0; i < path.Length(); ++i) {
        buffer[i] = path[i];
    }
    buffer[path.Length()] = '\0';
    return buffer;
}

} // namespace

void Exit(int code) {
    ::exit(code);
}

long Write(int fd, const void* buf, usize len) {
    return ::write(fd, buf, len);
}

long WriteAllWithTimeout(int fd, const void* buf, usize len, u64 timeout_nanos) {
    const char* bytes = static_cast<const char*>(buf);
    usize done = 0;
    const u64 deadline = MonotonicTimeNanos() + timeout_nanos;
    while (done < len) {
        // Wait until the descriptor can take a small chunk (a pipe whose reader stopped never can), then write only that much, so
        // the write itself cannot block past the deadline.
        const u64 now = MonotonicTimeNanos();
        const int wait_ms = deadline > now ? static_cast<int>((deadline - now + 999999ull) / 1000000ull) : 0;
        struct pollfd waiting = {fd, POLLOUT, 0};
        const int ready = ::poll(&waiting, 1, wait_ms);
        if (ready < 0 && errno == EINTR) {
            continue;
        }
        if (ready <= 0 || (waiting.revents & (POLLERR | POLLHUP | POLLNVAL)) != 0) {
            break;
        }
        const usize chunk = len - done < 512 ? len - done : 512;
        const long n = ::write(fd, bytes + done, chunk);
        if (n > 0) {
            done += static_cast<usize>(n);
        } else if (!(n < 0 && (errno == EINTR || errno == EAGAIN))) {
            break;
        }
    }
    return static_cast<long>(done);
}

long Read(int fd, void* buf, usize len) {
    return ::read(fd, buf, len);
}

int Open(StringView path, int flags, int mode) {
    char buf[kMaxPathBytes];
    const char* c_path = ToCPath(path, buf);
    if (c_path == nullptr) {
        return -1;
    }
    return ::open(c_path, flags, mode);
}

int CreateFileForWriting(StringView path, int mode) {
    return Open(path, O_WRONLY | O_CREAT | O_TRUNC, mode);
}

int Close(int fd) {
    return ::close(fd);
}

void* ReserveMemory(usize size) {
    void* mem = ::mmap(nullptr, size, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    return mem == MAP_FAILED ? nullptr : mem;
}

void ReleaseMemory(void* addr, usize size) {
    ::munmap(addr, size);
}

i64 ResidentMemoryKiB() {
    const int fd = ::open("/proc/self/statm", O_RDONLY | O_CLOEXEC);
    if (fd < 0) return 0;
    char text[128];
    const ssize_t n = ::read(fd, text, sizeof(text) - 1);
    ::close(fd);
    if (n <= 0) return 0;
    text[n] = '\0';
    long pages = 0;
    long resident = 0;
    if (std::sscanf(text, "%ld %ld", &pages, &resident) != 2) return 0;
    const long page_bytes = ::sysconf(_SC_PAGESIZE);
    return page_bytes > 0 ? static_cast<i64>(resident) * (page_bytes / 1024) : 0;
}

i64 OpenHandleCount() {
    DIR* dir = ::opendir("/proc/self/fd");
    if (dir == nullptr) return 0;
    i64 count = 0;
    while (const dirent* entry = ::readdir(dir)) {
        if (entry->d_name[0] != '.') ++count; // not . and ..
    }
    ::closedir(dir);
    return count > 0 ? count - 1 : 0; // without the descriptor of the directory itself
}

u64 MonotonicTimeNanos() {
    struct timespec ts;
    ::clock_gettime(CLOCK_MONOTONIC, &ts);
    return static_cast<u64>(ts.tv_sec) * 1'000'000'000ULL + static_cast<u64>(ts.tv_nsec);
}

void SleepNanos(u64 nanos) {
    struct timespec req;
    req.tv_sec = static_cast<time_t>(nanos / 1'000'000'000ULL);
    req.tv_nsec = static_cast<long>(nanos % 1'000'000'000ULL);
    struct timespec rem;
    while (::nanosleep(&req, &rem) == -1 && errno == EINTR) {
        req = rem;
    }
}

DynamicLibraryHandle LoadLibrary(StringView path) {
    char buf[kMaxPathBytes];
    const char* c_path = ToCPath(path, buf);
    if (c_path == nullptr) {
        return nullptr;
    }
    return ::dlopen(c_path, RTLD_NOW | RTLD_LOCAL);
}

void* GetSymbol(DynamicLibraryHandle handle, const char* symbol_name) {
    return ::dlsym(handle, symbol_name);
}

void UnloadLibrary(DynamicLibraryHandle handle) {
    if (handle != nullptr) {
        ::dlclose(handle);
    }
}

bool EnvironmentNamesIgnoreCase() { return false; }

usize EnvironmentCount() {
    usize n = 0;
    if (environ != nullptr) {
        while (environ[n] != nullptr) ++n;
    }
    return n;
}

bool EnvironmentEntry(usize index, char* buffer, usize capacity, usize* length) {
    *length = 0;
    if (capacity > 0) buffer[0] = '\0';
    if (environ == nullptr) return false;
    for (usize i = 0; i < index; ++i) { // walk to the entry; stops at the end of the array
        if (environ[i] == nullptr) return false;
    }
    const char* entry = environ[index];
    if (entry == nullptr) return false;
    const usize full = std::strlen(entry);
    *length = full;
    if (capacity > 0) {
        const usize copy = full < capacity - 1 ? full : capacity - 1;
        std::memcpy(buffer, entry, copy);
        buffer[copy] = '\0';
    }
    return true;
}

} // namespace engine_placeholder::os
