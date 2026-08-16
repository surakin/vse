#include "../os.h"

#include <cerrno>
#include <cstdlib>
#include <ctime>
#include <dlfcn.h>
#include <fcntl.h>
#include <sys/mman.h>
#include <unistd.h>

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

int Close(int fd) {
    return ::close(fd);
}

long Lseek(int fd, long offset, int whence) {
    return ::lseek(fd, offset, whence);
}

void* ReserveMemory(usize size) {
    void* mem = ::mmap(nullptr, size, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    return mem == MAP_FAILED ? nullptr : mem;
}

void ReleaseMemory(void* addr, usize size) {
    ::munmap(addr, size);
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

} // namespace engine_placeholder::os
