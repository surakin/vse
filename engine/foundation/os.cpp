#include "os.h"

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

int Open(const char* path, int flags) {
    return ::open(path, flags);
}

int Close(int fd) {
    return ::close(fd);
}

void* MMap(void* addr, usize len, int prot, int flags, int fd, long offset) {
    return ::mmap(addr, len, prot, flags, fd, offset);
}

int MUnmap(void* addr, usize len) {
    return ::munmap(addr, len);
}

int MProtect(void* addr, usize len, int prot) {
    return ::mprotect(addr, len, prot);
}

} // namespace engine_placeholder::os
