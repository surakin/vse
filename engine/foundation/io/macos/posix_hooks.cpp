// macOS's answers to the shared POSIX file layer's hooks (io/posix/posix_io.h; Phase 21 Day 287).
#include "io/posix/posix_io.h"

#include "os/last_error.h"

#include <copyfile.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>

namespace engine_placeholder::io::posix
{

i64 ModifiedUnixNs(const struct stat& st)
{
    return static_cast<i64>(st.st_mtimespec.tv_sec) * 1'000'000'000LL + st.st_mtimespec.tv_nsec;
}

int SyncDescriptor(int fd)
{
    // fsync on macOS hands the data to the drive, whose cache may still lose it on power loss; F_FULLFSYNC asks the
    // drive to write it to the medium. A file system that does not support it (some network, FAT and FUSE volumes)
    // refuses with ENOTSUP, EOPNOTSUPP (a number of its own on Darwin), EINVAL or ENOTTY: fsync is the most it can do.
    for (;;)
    {
        if (::fcntl(fd, F_FULLFSYNC) == 0)
        {
            return 0;
        }
        const int error = os::LastError();
        if (error == EINTR)
        {
            continue;
        }
        if (error != ENOTSUP && error != EOPNOTSUPP && error != EINVAL && error != ENOTTY)
        {
            return -1;
        }
        break;
    }
    for (;;)
    {
        if (::fsync(fd) == 0)
        {
            return 0;
        }
        if (os::LastError() != EINTR)
        {
            return -1;
        }
    }
}

int CopyData(int in, int out)
{
    // fcopyfile on the descriptors Copy already holds, so the same-file check it made cannot be raced by a rename
    // between check and copy. COPYFILE_DATA only: an APFS clone (COPYFILE_CLONE) would carry the times and extended
    // attributes, which Copy promises not to, and it needs the path-based copyfile. fcopyfile refuses a source that is
    // not a regular file (ENOTSUP for a FIFO or a device), which the loop copies as on Linux.
    struct stat st;
    if (::fstat(in, &st) != 0)
    {
        return -1;
    }
    if (!S_ISREG(st.st_mode))
    {
        return CopyLoop(in, out);
    }
    if (::fcopyfile(in, out, nullptr, COPYFILE_DATA) == 0)
    {
        return 0;
    }
    const int error = os::LastError();
    if (error != EINTR && error != ENOTSUP && error != EOPNOTSUPP)
    {
        return -1;
    }
    // Interrupted (a signal without SA_RESTART) or refused by the volume: a partial copy has moved both offsets, so
    // start again from the beginning with the loop. A target that cannot be rewound (a FIFO named as the destination)
    // makes the copy fail with that error.
    if (::lseek(in, 0, SEEK_SET) != 0 || ::lseek(out, 0, SEEK_SET) != 0 || ::ftruncate(out, 0) != 0)
    {
        return -1;
    }
    return CopyLoop(in, out);
}

}  // namespace engine_placeholder::io::posix
