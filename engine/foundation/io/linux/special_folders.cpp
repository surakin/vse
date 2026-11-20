// The Linux half of io/file_system.h (Phase 21 Day 287: split from the shared io/posix/file_system.cpp): the executable
// path (/proc/self/exe), the temporary directory and the XDG user directories. It calls no file API directly (the link
// is read through io::ReadLink).
#include "io/file_system.h"
#include "io/posix/posix_io.h"

#include "cstr/cstr.h"
#include "cstr/mem_ops.h"
#include "os/console.h"

namespace vse::io
{

namespace
{

using posix::AbsoluteEnv;
using posix::FromPath;

Status UserDirectory(const char* xdg_variable, const char* home_relative, StringView app_name, Path* out)
{
    if (!posix::ValidAppName(app_name))
    {
        return Status::Of(Error::Invalid);
    }
    Path base(PathStyle::Posix);
    StringView xdg = AbsoluteEnv(xdg_variable);
    if (!xdg.Empty())
    {
        if (Status s = FromPath(xdg, &base); s.Failed())
        {
            return s;
        }
    }
    else
    {
        Path home(PathStyle::Posix);
        if (Status s = posix::HomeDirectory(&home); s.Failed())
        {
            return s;
        }
        if (!home.TryJoin(Path(home_relative, PathStyle::Posix), &base))
        {
            return Status::Of(Error::TooLong);
        }
    }
    Path result(PathStyle::Posix);
    Path app;
    if (!Path::Parse(app_name, PathStyle::Posix, &app) || !base.TryJoin(app, &result))
    {
        return Status::Of(Error::TooLong);
    }
    *out = static_cast<Path&&>(result);
    return Status::Success();
}

}  // namespace

Status TempDirectory(Path* out)
{
    StringView dir = AbsoluteEnv("TMPDIR");
    return FromPath(dir.Empty() ? StringView("/tmp") : dir, out);
}

Status ExecutablePath(Path* out)
{
    return ReadLink(Path("/proc/self/exe", PathStyle::Posix), out);
}

Status UserConfigDirectory(StringView app_name, Path* out)
{
    return UserDirectory("XDG_CONFIG_HOME", ".config", app_name, out);
}
Status UserCacheDirectory(StringView app_name, Path* out)
{
    return UserDirectory("XDG_CACHE_HOME", ".cache", app_name, out);
}
Status UserDataDirectory(StringView app_name, Path* out)
{
    return UserDirectory("XDG_DATA_HOME", ".local/share", app_name, out);
}
Status UserStateDirectory(StringView app_name, Path* out)
{
    return UserDirectory("XDG_STATE_HOME", ".local/state", app_name, out);
}

Status CrashDumpDirectory(Path* out)
{
    // A crash handler must not guess a location: without $XDG_STATE_HOME or $HOME there is no directory (no
    // password-database fallback).
    if (AbsoluteEnv("XDG_STATE_HOME").Empty() && AbsoluteEnv("HOME").Empty())
    {
        return Status::Of(Error::NotFound);
    }
    char app[256];
    if (Status s = posix::CrashAppName(app); s.Failed())
    {
        return s;
    }
    Path state(PathStyle::Posix);
    if (Status s = UserStateDirectory(StringView(app), &state); s.Failed())
    {
        return s;  // validates the name too
    }
    Path dir(PathStyle::Posix);
    if (!state.TryJoin(Path("crashes", PathStyle::Posix), &dir))
    {
        return Status::Of(Error::TooLong);
    }
    if (Status s = CreatePrivateDirectory(dir); s.Failed())
    {
        return s;
    }
    *out = static_cast<Path&&>(dir);
    return Status::Success();
}

}  // namespace vse::io
