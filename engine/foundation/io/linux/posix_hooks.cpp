// Linux's answers to the shared POSIX file layer's hooks (io/posix/posix_io.h; Phase 26 Day 303).
#include "io/posix/posix_io.h"

#include "os/last_error.h"

#include <unistd.h>

namespace engine_placeholder::io::posix {

i64 ModifiedUnixNs(const struct stat& st) { return static_cast<i64>(st.st_mtim.tv_sec) * 1'000'000'000LL + st.st_mtim.tv_nsec; }

int SyncDescriptor(int fd) {
    for (;;) {
        if (::fsync(fd) == 0) return 0;
        if (os::LastError() != EINTR) return -1;
    }
}

int CopyData(int in, int out) { return CopyLoop(in, out); }

} // namespace engine_placeholder::io::posix
