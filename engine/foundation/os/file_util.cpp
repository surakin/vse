#include "os/file_util.h"

#include <cstdio> // SEEK_SET/SEEK_END: ISO C, portable — not POSIX-specific
#include <fcntl.h> // O_RDONLY: POSIX-shaped, but Open() treats it as portable (see os.h)

#include "os/os.h"

namespace engine_placeholder::os {

u8* ReadFileToArena(const char* path, ArenaAllocator& arena, usize& out_size) {
    int fd = Open(path, O_RDONLY);
    if (fd < 0) {
        return nullptr;
    }

    long end = Lseek(fd, 0, SEEK_END);
    if (end < 0 || Lseek(fd, 0, SEEK_SET) < 0) {
        Close(fd);
        return nullptr;
    }
    usize size = static_cast<usize>(end);

    u8* buffer = static_cast<u8*>(arena.Alloc(size, alignof(u8)));
    if (buffer == nullptr && size > 0) {
        Close(fd);
        return nullptr;
    }

    usize total_read = 0;
    while (total_read < size) {
        long n = Read(fd, buffer + total_read, size - total_read);
        if (n <= 0) {
            break; // error, or EOF before the size Lseek reported
        }
        total_read += static_cast<usize>(n);
    }
    Close(fd);

    if (total_read != size) {
        return nullptr;
    }
    out_size = size;
    return buffer;
}

} // namespace engine_placeholder::os
