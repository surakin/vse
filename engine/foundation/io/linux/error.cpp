// The Linux table of io::ErrorFromOsCode (errno). io::ErrorName is neutral: io/error.cpp.
#include <cerrno>

#include "io/error.h"

namespace engine_placeholder::io {

Error ErrorFromOsCode(i32 os_code) {
    switch (os_code) {
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
    case ERANGE: return Error::TooLong; // ERANGE: getcwd/readlink buffers
    case EINTR: return Error::Interrupted;
    case ENOSYS:
    case ENOTSUP: // equal to EOPNOTSUPP on Linux
        return Error::Unsupported;
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
    default: return Error::Io; // EIO, ENXIO, EFAULT, ESTALE, ... have no portable meaning
    }
}

} // namespace engine_placeholder::io
