// The Windows table of io::ErrorFromOsCode: GetLastError() codes (Phase 13 Day 151). The message of a failure is the
// engine's own text (io::ErrorName) plus this number; FormatMessageW is never called on these paths.
#include "io/error.h"

#include "os/windows/win32.h"

namespace vse::io
{

Error ErrorFromOsCode(i32 os_code)
{
    switch (static_cast<DWORD>(os_code))
    {
    case ERROR_SUCCESS: return Error::None;
    case ERROR_FILE_NOT_FOUND:
    case ERROR_PATH_NOT_FOUND:
    case ERROR_INVALID_DRIVE:
    case ERROR_BAD_NETPATH:
    case ERROR_BAD_NET_NAME:
    case ERROR_BAD_UNIT: return Error::NotFound;
    case ERROR_ACCESS_DENIED:
    case ERROR_PRIVILEGE_NOT_HELD:
    case ERROR_NETWORK_ACCESS_DENIED: return Error::PermissionDenied;
    case ERROR_ALREADY_EXISTS:
    case ERROR_FILE_EXISTS: return Error::AlreadyExists;
    case ERROR_DIRECTORY:
        return Error::NotADirectory;  // "the directory name is invalid": a file where a directory was needed
    case ERROR_DIR_NOT_EMPTY: return Error::NotEmpty;
    case ERROR_DISK_FULL:
    case ERROR_HANDLE_DISK_FULL:
    case ERROR_DISK_QUOTA_EXCEEDED:
    case ERROR_FILE_TOO_LARGE: return Error::NoSpace;
    case ERROR_FILENAME_EXCED_RANGE:
    case ERROR_BUFFER_OVERFLOW:
    case ERROR_INSUFFICIENT_BUFFER: return Error::TooLong;
    case ERROR_OPERATION_ABORTED:
    case ERROR_CANCELLED: return Error::Interrupted;
    case ERROR_INVALID_FUNCTION:
    case ERROR_NOT_SUPPORTED:
    case ERROR_CALL_NOT_IMPLEMENTED: return Error::Unsupported;
    case ERROR_INVALID_NAME:
    case ERROR_BAD_PATHNAME:
    case ERROR_INVALID_PARAMETER:
    case ERROR_INVALID_HANDLE:
    case ERROR_NEGATIVE_SEEK:
    case ERROR_NOT_A_REPARSE_POINT:
        return Error::Invalid;  // ReadLink of something that is not a link, as readlink's EINVAL
    case ERROR_NOT_SAME_DEVICE: return Error::CrossDevice;
    case ERROR_CANT_RESOLVE_FILENAME:  // too many reparse points on the way (a link loop)
    case ERROR_STOPPED_ON_SYMLINK: return Error::SymlinkLoop;
    case ERROR_WRITE_PROTECT: return Error::ReadOnly;
    case ERROR_SHARING_VIOLATION:  // another opener did not allow this access (the engine itself shares everything:
                                   // BACKEND.md)
    case ERROR_LOCK_VIOLATION:
    case ERROR_BUSY:
    case ERROR_USER_MAPPED_FILE:
    case ERROR_DELETE_PENDING:
    case ERROR_CURRENT_DIRECTORY: return Error::Busy;
    case ERROR_TOO_MANY_OPEN_FILES: return Error::TooManyOpenFiles;
    case ERROR_NOT_ENOUGH_MEMORY:
    case ERROR_OUTOFMEMORY:
    case ERROR_NO_SYSTEM_RESOURCES: return Error::OutOfMemory;
    case ERROR_HANDLE_EOF: return Error::EndOfFile;
    default:
        // ERROR_NOT_READY, ERROR_IO_PENDING (never a failure the layer reports), ERROR_BROKEN_PIPE, ERROR_CRC,
        // ERROR_GEN_FAILURE, ... have no portable meaning.
        return Error::Io;
    }
}

}  // namespace vse::io
