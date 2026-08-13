#include "../os.h"

#include <cstdlib>
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

void* ReserveMemory(usize size) {
    void* mem = ::mmap(nullptr, size, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    return mem == MAP_FAILED ? nullptr : mem;
}

void ReleaseMemory(void* addr, usize size) {
    ::munmap(addr, size);
}

} // namespace engine_placeholder::os
