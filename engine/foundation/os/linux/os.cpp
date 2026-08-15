#include "../os.h"

#include <cerrno>
#include <cstdlib>
#include <ctime>
#include <fcntl.h>
#include <sys/mman.h>
#include <unistd.h>

namespace engine_placeholder::os {

void Exit(int code) {
    ::exit(code);
}

long Write(int fd, const void* buf, usize len) {
    return ::write(fd, buf, len);
}

long Read(int fd, void* buf, usize len) {
    return ::read(fd, buf, len);
}

int Open(const char* path, int flags, int mode) {
    return ::open(path, flags, mode);
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

} // namespace engine_placeholder::os
