#include <cerrno>

#include "io/error.h"

namespace engine_placeholder::io {

const char* ErrorName(Error error) {
    switch (error) {
    case Error::None: return "none";
    case Error::NotFound: return "not_found";
    case Error::PermissionDenied: return "permission_denied";
    case Error::AlreadyExists: return "already_exists";
    case Error::NotADirectory: return "not_a_directory";
    case Error::IsADirectory: return "is_a_directory";
    case Error::NotEmpty: return "not_empty";
    case Error::NoSpace: return "no_space";
    case Error::TooLong: return "too_long";
    case Error::Interrupted: return "interrupted";
    case Error::Unsupported: return "unsupported";
    case Error::Invalid: return "invalid";
    case Error::CrossDevice: return "cross_device";
    case Error::SymlinkLoop: return "symlink_loop";
    case Error::ReadOnly: return "read_only";
    case Error::Busy: return "busy";
    case Error::TooManyOpenFiles: return "too_many_open_files";
    case Error::OutOfMemory: return "out_of_memory";
    case Error::Io: return "io";
    }
    return "io";
}

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
