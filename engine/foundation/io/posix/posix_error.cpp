// The POSIX table of io::ErrorFromOsCode (errno), Linux and macOS (Phase 21 Day 287: moved from io/linux/).
// io::ErrorName is neutral: io/error.cpp.
#include "build_defs.h"
#include "os/last_error.h"

#include "io/error.h"

namespace engine_placeholder::io
{

Error ErrorFromOsCode(i32 os_code)
{
    switch (os_code)
    {
    case 0: return Error::None;
    case ENOENT: return Error::NotFound;
    case EACCES:
    case EPERM: return Error::PermissionDenied;
    case EEXIST: return Error::AlreadyExists;
    case ENOTDIR: return Error::NotADirectory;
    case EISDIR: return Error::IsADirectory;
    case ENOTEMPTY: return Error::NotEmpty;
    case ENOSPC:
    case EDQUOT:
    case EFBIG: return Error::NoSpace;
    case ENAMETOOLONG:
    case ERANGE: return Error::TooLong;  // ERANGE: getcwd/readlink buffers
    case EINTR: return Error::Interrupted;
    case ENOSYS:
    case ENOTSUP:  // equal to EOPNOTSUPP on Linux
        return Error::Unsupported;
#if ENGINE_PLATFORM_MACOS || ENGINE_PLATFORM_IOS
    case EOPNOTSUPP: return Error::Unsupported;  // a number of its own on Darwin (102; ENOTSUP is 45)
    case EAUTH:                                  // authentication error (NFS)
    case ENEEDAUTH: return Error::PermissionDenied;
    case EFTYPE: return Error::Invalid;    // wrong type of file for the operation
    case ENOATTR: return Error::NotFound;  // an extended attribute that does not exist
#endif
    case EINVAL:
    case EBADF: return Error::Invalid;
    case EXDEV: return Error::CrossDevice;
    case ELOOP: return Error::SymlinkLoop;
    case EROFS: return Error::ReadOnly;
    case EBUSY:
    case ETXTBSY: return Error::Busy;
    case EMFILE:
    case ENFILE: return Error::TooManyOpenFiles;
    case ENOMEM: return Error::OutOfMemory;
    default: return Error::Io;  // EIO, ENXIO, EFAULT, ESTALE, ... have no portable meaning
    }
}

}  // namespace engine_placeholder::io
