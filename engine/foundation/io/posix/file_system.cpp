// The POSIX backend of io/file_system.h, Linux and macOS (Phase 21 Day 287: moved from io/linux/). The executable path,
// the temporary and the user directories are each system's own (io/linux/special_folders.cpp,
// io/macos/special_folders.cpp). This is one of the places allowed to call the POSIX file API directly.
#include "io/file_system.h"
#include "io/posix/posix_io.h"

#include "os/last_error.h"
#include "os/console.h"
#include "cstr/cstr.h"

#include <dirent.h>
#include <fcntl.h>
#include <pwd.h>
#include <sys/stat.h>
#include <unistd.h>
#include "cstr/mem_ops.h"

namespace engine_placeholder::io
{

namespace
{

using NativePath = char[kMaxPathBytes + 1];

Status Errno()
{
    return Status::FromOs(os::LastError());
}

Status ToNative(const Path& path, NativePath& buffer)
{
    return path.ToNative(buffer, sizeof(NativePath)) ? Status::Success() : Status::Of(Error::TooLong);
}

FileType TypeOf(mode_t mode)
{
    if (S_ISREG(mode))
    {
        return FileType::Regular;
    }
    if (S_ISDIR(mode))
    {
        return FileType::Directory;
    }
    if (S_ISLNK(mode))
    {
        return FileType::Symlink;
    }
    return FileType::Other;
}

Status DoStat(const Path& path, FileInfo* out, bool follow)
{
    NativePath p;
    if (Status s = ToNative(path, p); s.Failed())
    {
        return s;
    }
    struct stat st;
    if ((follow ? ::stat(p, &st) : ::lstat(p, &st)) != 0)
    {
        return Errno();
    }
    FileInfo info;
    info.type = TypeOf(st.st_mode);
    info.size = static_cast<u64>(st.st_size);
    info.modified_unix_ns = posix::ModifiedUnixNs(st);
    info.permissions = static_cast<u32>(st.st_mode & 07777);
    info.read_only = !S_ISLNK(st.st_mode) && ::faccessat(AT_FDCWD, p, W_OK, AT_EACCESS) != 0;
    *out = info;
    return Status::Success();
}

using posix::FromPath;

constexpr int kMaxTreeDepth = 128;

// Removes `name` below the directory `parent_fd`, without following links; holds one descriptor per level. Some file
// systems skip entries when others are removed during a read of the directory, so an `ENOTEMPTY` after the sweep means
// "sweep again" (a few times).
Status RemoveTree(int parent_fd, const char* name, int depth)
{
    struct stat st;
    if (::fstatat(parent_fd, name, &st, AT_SYMLINK_NOFOLLOW) != 0)
    {
        return Errno();
    }
    if (!S_ISDIR(st.st_mode))
    {
        return ::unlinkat(parent_fd, name, 0) == 0 ? Status::Success() : Errno();
    }
    if (depth >= kMaxTreeDepth)
    {
        return Status::Of(Error::TooLong);
    }
    for (int attempt = 0; attempt < 3; ++attempt)
    {
        const int fd = ::openat(parent_fd, name, O_RDONLY | O_DIRECTORY | O_NOFOLLOW | O_CLOEXEC);
        if (fd < 0)
        {
            return Errno();
        }
        DIR* dir = ::fdopendir(fd);
        if (dir == nullptr)
        {
            const Status s = Errno();
            ::close(fd);
            return s;
        }
        Status first;
        for (;;)
        {
            os::SetLastError(0);
            const struct dirent* entry = ::readdir(dir);
            if (entry == nullptr)
            {
                if (os::LastError() != 0 && first.Ok())
                {
                    first = Errno();
                }
                break;
            }
            if (CStrCompare(entry->d_name, ".") == 0 || CStrCompare(entry->d_name, "..") == 0)
            {
                continue;
            }
            const Status s = RemoveTree(fd, entry->d_name, depth + 1);
            if (s.Failed() && first.Ok())
            {
                first = s;
            }
        }
        ::closedir(dir);  // closes fd
        if (first.Failed())
        {
            return first;
        }
        if (::unlinkat(parent_fd, name, AT_REMOVEDIR) == 0)
        {
            return Status::Success();
        }
        if (os::LastError() != ENOTEMPTY)
        {
            return Errno();
        }
    }
    return Status::Of(Error::NotEmpty);
}

Status MakeTemp(const Path& directory, StringView prefix, bool as_directory, Path* out)
{
    if (prefix.Empty() || prefix.IndexOf('/') != prefix.Length() || prefix.IndexOf('\0') != prefix.Length())
    {
        return Status::Of(Error::Invalid);
    }
    Path tmpl;
    // `<prefix>.XXXXXX`
    char leaf[kMaxPathBytes];
    if (prefix.Length() + 8 > sizeof(leaf))
    {
        return Status::Of(Error::TooLong);
    }
    MemCopy(leaf, prefix.Data(), prefix.Length());
    MemCopy(leaf + prefix.Length(), ".XXXXXX", 8);
    if (!directory.TryJoin(Path(StringView(leaf, prefix.Length() + 7), PathStyle::Posix), &tmpl))
    {
        return Status::Of(Error::TooLong);
    }
    NativePath p;
    if (Status s = ToNative(tmpl, p); s.Failed())
    {
        return s;
    }
    if (as_directory)
    {
        if (::mkdtemp(p) == nullptr)
        {
            return Errno();
        }
    }
    else
    {
        const int fd = ::mkstemp(p);
        if (fd < 0)
        {
            return Errno();
        }
        ::close(fd);
    }
    return FromPath(StringView(p), out);
}

}  // namespace

namespace posix
{

bool ValidAppName(StringView name)
{
    return !name.Empty() && name != StringView(".") && name != StringView("..") && name.IndexOf('/') == name.Length() &&
           name.IndexOf('\0') == name.Length() && name.Length() <= 200;
}

StringView AbsoluteEnv(const char* name)
{
    const char* v = os::GetEnvironment(name);
    return (v != nullptr && v[0] == '/') ? StringView(v) : StringView();
}

Status HomeDirectory(Path* out)
{
    StringView home = AbsoluteEnv("HOME");
    if (!home.Empty())
    {
        return FromPath(home, out);
    }
    struct passwd pw;
    struct passwd* result = nullptr;
    char buf[16384];
    if (::getpwuid_r(::geteuid(), &pw, buf, sizeof(buf), &result) == 0 && result != nullptr && pw.pw_dir != nullptr &&
        pw.pw_dir[0] == '/')
    {
        return FromPath(StringView(pw.pw_dir), out);
    }
    return Status::Of(Error::NotFound);
}

Status FromPath(StringView text, Path* out)
{
    if (!Path::Parse(text, PathStyle::Posix, out))
    {
        return Status::Of(Error::TooLong);
    }
    return Status::Success();
}

int CopyLoop(int in, int out)
{
    char buffer[64 * 1024];
    for (;;)
    {
        const ssize_t n = ::read(in, buffer, sizeof(buffer));
        if (n < 0)
        {
            if (os::LastError() == EINTR)
            {
                continue;
            }
            return -1;
        }
        if (n == 0)
        {
            return 0;
        }
        for (ssize_t done = 0; done < n;)
        {
            const ssize_t w = ::write(out, buffer + done, static_cast<usize>(n - done));
            if (w < 0)
            {
                if (os::LastError() == EINTR)
                {
                    continue;
                }
                return -1;
            }
            done += w;
        }
    }
}

Status CrashAppName(char (&app)[256])
{
    const char* configured = os::GetEnvironment("ENGINE_APP_NAME");
    if (configured != nullptr && configured[0] != '\0')
    {
        const usize length = CStrLength(configured);
        if (length > 200)
        {
            return Status::Of(Error::Invalid);
        }
        MemCopy(app, configured, length + 1);
        return Status::Success();
    }
    Path exe(PathStyle::Posix);
    if (Status s = ExecutablePath(&exe); s.Failed())
    {
        return s;
    }
    const StringView name = exe.FileName();
    if (name.Empty() || name.Length() > 200)
    {
        return Status::Of(Error::NotFound);
    }
    MemCopy(app, name.Data(), name.Length());
    app[name.Length()] = '\0';
    return Status::Success();
}

}  // namespace posix

Status Stat(const Path& path, FileInfo* out)
{
    return DoStat(path, out, true);
}
Status Lstat(const Path& path, FileInfo* out)
{
    return DoStat(path, out, false);
}

bool Exists(const Path& path)
{
    FileInfo info;
    return Stat(path, &info).Ok();
}
bool IsFile(const Path& path)
{
    FileInfo info;
    return Stat(path, &info).Ok() && info.type == FileType::Regular;
}
bool IsDirectory(const Path& path)
{
    FileInfo info;
    return Stat(path, &info).Ok() && info.type == FileType::Directory;
}

Status Remove(const Path& path)
{
    NativePath p;
    if (Status s = ToNative(path, p); s.Failed())
    {
        return s;
    }
    return ::remove(p) == 0 ? Status::Success() : Errno();
}

Status RemoveAll(const Path& path)
{
    if (path.IsEmpty())
    {
        return Status::Of(Error::Invalid);
    }
    Path absolute;
    if (Status s = Absolute(path, &absolute); s.Failed())
    {
        return s;
    }
    // The kernel resolves `..` through links, a lexical Normalize does not: refuse anything that would make them
    // disagree.
    usize components = 0;
    StringView c;
    Path::Components it = absolute.GetComponents();
    while (it.Next(&c))
    {
        if (c == StringView(".") || c == StringView(".."))
        {
            return Status::Of(Error::Invalid);
        }
        ++components;
    }
    if (components < kRemoveAllMinComponents)
    {
        return Status::Of(Error::Invalid);
    }
    NativePath p;
    if (Status s = ToNative(absolute, p); s.Failed())
    {
        return s;
    }
    return RemoveTree(AT_FDCWD, p, 0);
}

Status Rename(const Path& from, const Path& to)
{
    NativePath a;
    NativePath b;
    if (Status s = ToNative(from, a); s.Failed())
    {
        return s;
    }
    if (Status s = ToNative(to, b); s.Failed())
    {
        return s;
    }
    return ::rename(a, b) == 0 ? Status::Success() : Errno();
}

Status Copy(const Path& from, const Path& to, bool overwrite)
{
    NativePath a;
    NativePath b;
    if (Status s = ToNative(from, a); s.Failed())
    {
        return s;
    }
    if (Status s = ToNative(to, b); s.Failed())
    {
        return s;
    }
    const int in = ::open(a, O_RDONLY | O_CLOEXEC);
    if (in < 0)
    {
        return Errno();
    }
    struct stat st;
    if (::fstat(in, &st) != 0)
    {
        const Status s = Errno();
        ::close(in);
        return s;
    }
    if (S_ISDIR(st.st_mode))
    {
        ::close(in);
        return Status::Of(Error::IsADirectory);
    }
    // No O_TRUNC yet: `to` may be the same file as `from`, which truncating would destroy.
    const int out = ::open(b, O_WRONLY | O_CREAT | O_CLOEXEC | (overwrite ? 0 : O_EXCL), st.st_mode & 0777);
    if (out < 0)
    {
        const Status s = Errno();
        ::close(in);
        return s;
    }
    struct stat out_st;
    Status setup;
    if (::fstat(out, &out_st) != 0)
    {
        setup = Errno();
    }
    else if (out_st.st_dev == st.st_dev && out_st.st_ino == st.st_ino)
    {
        setup = Status::Of(Error::Invalid);  // the same file: truncating it would destroy the source
    }
    else if (overwrite && S_ISREG(out_st.st_mode) && ::ftruncate(out, 0) != 0)
    {
        setup = Errno();
    }
    if (setup.Failed())
    {
        ::close(in);
        ::close(out);
        return setup;
    }
    Status result = posix::CopyData(in, out) == 0 ? Status::Success() : Errno();
    if (result.Ok() && ::fchmod(out, st.st_mode & 0777) != 0)
    {
        result = Errno();
    }
    ::close(in);
    if (::close(out) != 0 && result.Ok())
    {
        result = Errno();
    }
    return result;
}

Status SetPermissions(const Path& path, u32 permissions)
{
    NativePath p;
    if (Status s = ToNative(path, p); s.Failed())
    {
        return s;
    }
    return ::chmod(p, static_cast<mode_t>(permissions & 07777)) == 0 ? Status::Success() : Errno();
}

Status SyncDirectory(const Path& path)
{
    NativePath p;
    if (Status s = ToNative(path, p); s.Failed())
    {
        return s;
    }
    const int fd = ::open(p, O_RDONLY | O_DIRECTORY | O_CLOEXEC);
    if (fd < 0)
    {
        return Errno();
    }
    const Status result = posix::SyncDescriptor(fd) == 0 ? Status::Success() : Errno();
    ::close(fd);
    return result;
}

Status CreateDirectory(const Path& path)
{
    NativePath p;
    if (Status s = ToNative(path, p); s.Failed())
    {
        return s;
    }
    return ::mkdir(p, 0777) == 0 ? Status::Success() : Errno();
}

namespace
{

Status CreateDirectoriesWithMode(const Path& path, mode_t mode)
{
    NativePath p;
    if (Status s = ToNative(path, p); s.Failed())
    {
        return s;
    }
    Status s = ::mkdir(p, mode) == 0 ? Status::Success() : Errno();
    if (s.Ok())
    {
        return s;
    }
    if (s.Is(Error::AlreadyExists))
    {
        return IsDirectory(path) ? Status::Success() : Status::Of(Error::NotADirectory);
    }
    if (s.Is(Error::NotFound))
    {
        const Path parent = path.Parent();
        if (parent.IsEmpty() || parent == path)
        {
            return s;
        }
        if (Status p2 = CreateDirectoriesWithMode(parent, mode); p2.Failed())
        {
            return p2;
        }
        s = ::mkdir(p, mode) == 0 ? Status::Success() : Errno();
        if (s.Is(Error::AlreadyExists) && IsDirectory(path))
        {
            return Status::Success();  // created by someone else meanwhile
        }
    }
    return s;
}

}  // namespace

Status CreateDirectories(const Path& path)
{
    return CreateDirectoriesWithMode(path, 0777);
}

Status CreatePrivateDirectory(const Path& path)
{
    NativePath p;
    if (Status s = ToNative(path, p); s.Failed())
    {
        return s;
    }
    // An existing ancestor must belong to this user or to root: a directory another user owns could have the private
    // directory renamed or replaced.
    for (Path ancestor = path.Parent(); !ancestor.IsEmpty(); ancestor = ancestor.Parent())
    {
        NativePath a;
        if (Status s = ToNative(ancestor, a); s.Failed())
        {
            return s;
        }
        struct stat ast;
        // Another user's directory is fine if that user alone can change it; one others can write to (and that is not
        // sticky) lets the private directory be renamed away or swapped.
        if (::stat(a, &ast) == 0 && ast.st_uid != ::geteuid() && ast.st_uid != 0 && (ast.st_mode & 022) != 0 &&
            (ast.st_mode & S_ISVTX) == 0)
        {
            return Status::Of(Error::PermissionDenied);
        }
        if (ancestor.Parent() == ancestor)
        {
            break;
        }
    }
    // Missing parents are created private too.
    const Path parent = path.Parent();
    if (!parent.IsEmpty() && parent != path)
    {
        if (Status s = CreateDirectoriesWithMode(parent, 0700); s.Failed())
        {
            return s;
        }
    }
    if (::mkdir(p, 0700) != 0 && os::LastError() != EEXIST)
    {
        return Errno();
    }
    // Open the final component without following a link and check the descriptor, so nothing can be swapped in between.
    const int fd = ::open(p, O_RDONLY | O_DIRECTORY | O_NOFOLLOW | O_CLOEXEC);
    if (fd < 0)
    {
        const Status open_error = Errno();
        struct stat link;
        // Linux reports a link under O_DIRECTORY|O_NOFOLLOW as ENOTDIR, so ask what the name is.
        if (::lstat(p, &link) == 0 && S_ISLNK(link.st_mode))
        {
            return Status::Of(Error::SymlinkLoop);
        }
        return open_error;
    }
    struct stat st;
    Status result;
    if (::fstat(fd, &st) != 0)
    {
        result = Errno();
    }
    else if (st.st_uid != ::geteuid())
    {
        result = Status::Of(Error::PermissionDenied);
    }
    else if ((st.st_mode & 077) != 0 && ::fchmod(fd, 0700) != 0)
    {
        result = Errno();
    }
    else if (::faccessat(fd, ".", W_OK | X_OK, AT_EACCESS) != 0)
    {
        result = Status::Of(Error::PermissionDenied);
    }
    ::close(fd);
    return result;
}

Status CreateTempFile(const Path& directory, StringView prefix, Path* out)
{
    return MakeTemp(directory, prefix, false, out);
}
Status CreateTempDirectory(const Path& directory, StringView prefix, Path* out)
{
    return MakeTemp(directory, prefix, true, out);
}

Status CurrentDirectory(Path* out)
{
    NativePath p;
    if (::getcwd(p, sizeof(p)) == nullptr)
    {
        return Errno();
    }
    return FromPath(StringView(p), out);
}

Status SetCurrentDirectory(const Path& path)
{
    NativePath p;
    if (Status s = ToNative(path, p); s.Failed())
    {
        return s;
    }
    return ::chdir(p) == 0 ? Status::Success() : Errno();
}

Status Absolute(const Path& path, Path* out)
{
    if (path.IsAbsolute())
    {
        *out = path;
        return Status::Success();
    }
    Path cwd(PathStyle::Posix);
    if (Status s = CurrentDirectory(&cwd); s.Failed())
    {
        return s;
    }
    Path result(PathStyle::Posix);
    if (!cwd.TryJoin(path, &result))
    {
        return Status::Of(Error::TooLong);
    }
    *out = static_cast<Path&&>(result);
    return Status::Success();
}

Status Canonical(const Path& path, Path* out)
{
    NativePath p;
    NativePath resolved;
    if (Status s = ToNative(path, p); s.Failed())
    {
        return s;
    }
    if (::realpath(p, resolved) == nullptr)
    {
        return Errno();
    }
    return FromPath(StringView(resolved), out);
}

Status ReadLink(const Path& path, Path* out)
{
    NativePath p;
    NativePath target;
    if (Status s = ToNative(path, p); s.Failed())
    {
        return s;
    }
    const ssize_t n = ::readlink(p, target, sizeof(NativePath));
    if (n < 0)
    {
        return Errno();
    }
    if (static_cast<usize>(n) > kMaxPathBytes)
    {
        return Status::Of(Error::TooLong);  // readlink does not report truncation
    }
    return FromPath(StringView(target, static_cast<usize>(n)), out);
}

Status ExecutableDirectory(Path* out)
{
    Path exe(PathStyle::Posix);
    if (Status s = ExecutablePath(&exe); s.Failed())
    {
        return s;
    }
    *out = exe.Parent();
    return Status::Success();
}

}  // namespace engine_placeholder::io
