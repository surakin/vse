// The neutral half of io/error.h; the per-platform ErrorFromOsCode tables are io/<platform>/error.cpp.
#include "io/error.h"

namespace vse::io
{

const char* ErrorName(Error error)
{
    switch (error)
    {
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
    case Error::EndOfFile: return "end_of_file";
    case Error::Io: return "io";
    }
    return "io";
}

}  // namespace vse::io
