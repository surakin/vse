// macOS's answers to the shared POSIX file layer's hooks (io/posix/posix_io.h; Phase 26 Day 287).
#include "io/posix/posix_io.h"

#include "os/last_error.h"

#include <fcntl.h>
#include <unistd.h>

namespace engine_placeholder::io::posix {

i64 ModifiedUnixNs(const struct stat& st) {
    return static_cast<i64>(st.st_mtimespec.tv_sec) * 1'000'000'000LL + st.st_mtimespec.tv_nsec;
}

int SyncDescriptor(int fd) {
    // fsync on macOS hands the data to the drive, whose cache may still lose it on power loss; F_FULLFSYNC asks the drive to write it to the
    // medium. A file system that does not support it (some network, FAT and FUSE volumes) refuses with ENOTSUP, EOPNOTSUPP (a number of its own on
    // Darwin), EINVAL or ENOTTY: fsync is the most it can do.
    for (;;) {
        if (::fcntl(fd, F_FULLFSYNC) == 0) return 0;
        const int error = os::LastError();
        if (error == EINTR) continue;
        if (error != ENOTSUP && error != EOPNOTSUPP && error != EINVAL && error != ENOTTY) return -1;
        break;
    }
    for (;;) {
        if (::fsync(fd) == 0) return 0;
        if (os::LastError() != EINTR) return -1;
    }
}

} // namespace engine_placeholder::io::posix
