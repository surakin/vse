// The macOS half of io/file_system.h (Phase 26 Days 303-304): the executable path (_NSGetExecutablePath, resolved with
// realpath), the per-user temporary directory and the user directories under ~/Library. Objective-C++ for
// NSSearchPathForDirectoriesInDomains. One of the places allowed to call the POSIX file API directly.
#include "io/file_system.h"
#include "io/posix/posix_io.h"

#include "cstr/cstr.h"
#include "cstr/mem_ops.h"
#include "os/console.h"  // ::realpath and ::confstr's C library headers
#include "os/last_error.h"

#import <Foundation/Foundation.h>
#include <mach-o/dyld.h>
#include <unistd.h>

namespace engine_placeholder::io
{

namespace
{

using posix::AbsoluteEnv;
using posix::FromPath;

enum class Folder
{
    ApplicationSupport,
    Caches
};

// The folder itself (~/Library/Application Support or ~/Library/Caches). $HOME first when it is absolute, as Linux
// reads $HOME and $XDG_* and Windows %APPDATA% (a test or a launcher can point it elsewhere; in a sandbox launchd sets
// $HOME to the container, so the answer is the container's). Without it, Foundation's answer (the account's home, or
// the container's: it does not read $HOME); then the password database.
Status FolderBase(Folder folder, Path* out)
{
    const char* relative = folder == Folder::ApplicationSupport ? "Library/Application Support" : "Library/Caches";
    if (AbsoluteEnv("HOME").Empty())
    {
        @autoreleasepool
        {
            NSArray<NSString*>* const found = NSSearchPathForDirectoriesInDomains(
                folder == Folder::ApplicationSupport ? NSApplicationSupportDirectory : NSCachesDirectory,
                NSUserDomainMask, YES);
            const char* const text = found.firstObject.fileSystemRepresentation;
            if (text != nullptr && text[0] == '/')
            {
                return FromPath(StringView(text), out);
            }
        }
    }
    Path home(PathStyle::Posix);
    if (Status s = posix::HomeDirectory(&home); s.Failed())
    {
        return s;  // $HOME when absolute, else the password database
    }
    return home.TryJoin(Path(relative, PathStyle::Posix), out) ? Status::Success() : Status::Of(Error::TooLong);
}

// <folder>/<app>[/<leaf>]
Status LibraryDirectory(Folder folder, StringView app_name, const char* leaf, Path* out)
{
    if (!posix::ValidAppName(app_name))
    {
        return Status::Of(Error::Invalid);
    }
    Path base(PathStyle::Posix);
    if (Status s = FolderBase(folder, &base); s.Failed())
    {
        return s;
    }
    Path app;
    if (!Path::Parse(app_name, PathStyle::Posix, &app))
    {
        return Status::Of(Error::TooLong);
    }
    Path result(PathStyle::Posix);
    if (!base.TryJoin(app, &result))
    {
        return Status::Of(Error::TooLong);
    }
    if (leaf != nullptr)
    {
        Path with_leaf(PathStyle::Posix);
        if (!result.TryJoin(Path(leaf, PathStyle::Posix), &with_leaf))
        {
            return Status::Of(Error::TooLong);
        }
        result = static_cast<Path&&>(with_leaf);
    }
    *out = static_cast<Path&&>(result);
    return Status::Success();
}

}  // namespace

Status TempDirectory(Path* out)
{
    // $TMPDIR is the per-user directory under /var/folders that launchd sets; confstr gives the same when it is unset
    // (a process started without the user's environment). /tmp is shared by every user, so it is only the last resort.
    StringView dir = AbsoluteEnv("TMPDIR");
    if (!dir.Empty())
    {
        return FromPath(dir, out);
    }
    char buffer[kMaxPathBytes + 1];
    const usize n = ::confstr(_CS_DARWIN_USER_TEMP_DIR, buffer, sizeof(buffer));
    if (n > 1 && n <= sizeof(buffer) && buffer[0] == '/')
    {
        return FromPath(StringView(buffer, n - 1), out);
    }
    return FromPath(StringView("/tmp"), out);
}

Status ExecutablePath(Path* out)
{
    // The path the program was started by, which may hold a link or `..`; realpath gives the file itself, as
    // /proc/self/exe does on Linux.
    char raw[kMaxPathBytes + 1];
    u32 size = sizeof(raw);
    if (::_NSGetExecutablePath(raw, &size) != 0)
    {
        return Status::Of(Error::TooLong);
    }
    char resolved[kMaxPathBytes + 1];  // realpath writes at most PATH_MAX (1024 on macOS) bytes
    if (::realpath(raw, resolved) == nullptr)
    {
        return Status::FromOs(os::LastError());
    }
    return FromPath(StringView(resolved), out);
}

Status UserConfigDirectory(StringView app_name, Path* out)
{
    return LibraryDirectory(Folder::ApplicationSupport, app_name, nullptr, out);
}
Status UserDataDirectory(StringView app_name, Path* out)
{
    return LibraryDirectory(Folder::ApplicationSupport, app_name, nullptr, out);
}
Status UserCacheDirectory(StringView app_name, Path* out)
{
    return LibraryDirectory(Folder::Caches, app_name, nullptr, out);
}
Status UserStateDirectory(StringView app_name, Path* out)
{
    return LibraryDirectory(Folder::ApplicationSupport, app_name, "state", out);
}

Status CrashDumpDirectory(Path* out)
{
    // As on Linux, a crash handler does not guess: without $HOME there is no directory (no password-database fallback).
    if (AbsoluteEnv("HOME").Empty())
    {
        return Status::Of(Error::NotFound);
    }
    char app[256];
    if (Status s = posix::CrashAppName(app); s.Failed())
    {
        return s;
    }
    Path dir(PathStyle::Posix);
    if (Status s = LibraryDirectory(Folder::ApplicationSupport, StringView(app), "crashes", &dir); s.Failed())
    {
        return s;
    }
    if (Status s = CreatePrivateDirectory(dir); s.Failed())
    {
        return s;
    }
    *out = static_cast<Path&&>(dir);
    return Status::Success();
}

}  // namespace engine_placeholder::io
