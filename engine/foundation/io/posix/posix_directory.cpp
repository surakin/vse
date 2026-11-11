// The POSIX backend of io/directory.h, Linux and macOS (Phase 26 Day 303: moved from io/linux/) (Open, Next, Close,
// DirectoryEntry::Info, detail::GetIdentity; the rest is io/directory.cpp). One of the places allowed to call the POSIX
// directory API directly.
#include "io/directory.h"
#include "io/detail/directory_identity.h"
#include "io/posix/posix_io.h"

#include "os/last_error.h"
#include "cstr/cstr.h"

#include <dirent.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>
#include "cstr/mem_ops.h"

namespace engine_placeholder::io
{

namespace
{

FileType TypeFromDirent(byte d_type)
{
    switch (d_type)
    {
    case DT_REG: return FileType::Regular;
    case DT_DIR: return FileType::Directory;
    case DT_LNK: return FileType::Symlink;
    case DT_UNKNOWN: return FileType::None;  // the caller asks the file system
    default: return FileType::Other;
    }
}

FileType TypeFromMode(mode_t mode)
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

DIR* AsDir(void* handle)
{
    return static_cast<DIR*>(handle);
}

}  // namespace

Status DirectoryEntry::Info(FileInfo* out) const
{
    if (owner == nullptr)
    {
        return Status::Of(Error::Invalid);
    }
    char name_z[kMaxPathBytes + 1];
    if (name.Length() > kMaxPathBytes)
    {
        return Status::Of(Error::TooLong);
    }
    MemCopy(name_z, name.Data(), name.Length());
    name_z[name.Length()] = '\0';
    struct stat st;
    if (::fstatat(::dirfd(AsDir(owner)), name_z, &st, AT_SYMLINK_NOFOLLOW) != 0)
    {
        return Status::FromOs(os::LastError());
    }
    FileInfo info;
    info.type = TypeFromMode(st.st_mode);
    info.size = static_cast<u64>(st.st_size);
    info.modified_unix_ns = posix::ModifiedUnixNs(st);
    info.permissions = static_cast<u32>(st.st_mode & 07777);
    info.read_only = !S_ISLNK(st.st_mode) && ::faccessat(::dirfd(AsDir(owner)), name_z, W_OK, AT_EACCESS) != 0;
    *out = info;
    return Status::Success();
}

void Directory::Close()
{
    if (handle_ != nullptr)
    {
        ::closedir(AsDir(handle_));
        handle_ = nullptr;
    }
}

Status Directory::Open(const Path& path, Directory* out)
{
    char native[kMaxPathBytes + 1];
    if (!path.ToNative(native, sizeof(native)))
    {
        return Status::Of(Error::TooLong);
    }
    const int fd = ::open(native, O_RDONLY | O_DIRECTORY | O_CLOEXEC);
    if (fd < 0)
    {
        return Status::FromOs(os::LastError());
    }
    DIR* dir = ::fdopendir(fd);
    if (dir == nullptr)
    {
        const Status s = Status::FromOs(os::LastError());
        ::close(fd);
        return s;
    }
    out->Close();
    out->handle_ = dir;
    out->path_ = path;
    return Status::Success();
}

Status Directory::Next(DirectoryEntry* entry, bool* found)
{
    *found = false;
    if (handle_ == nullptr)
    {
        return Status::Of(Error::Invalid);
    }
    for (;;)
    {
        os::SetLastError(0);
        const struct dirent* e = ::readdir(AsDir(handle_));
        if (e == nullptr)
        {
            return os::LastError() == 0 ? Status::Success() : Status::FromOs(os::LastError());
        }
        if (CStrCompare(e->d_name, ".") == 0 || CStrCompare(e->d_name, "..") == 0)
        {
            continue;
        }
        entry->name = StringView(e->d_name);
        entry->owner = handle_;
        entry->type = TypeFromDirent(e->d_type);
        if (entry->type == FileType::None)
        {  // DT_UNKNOWN: ask
            struct stat st;
            if (::fstatat(::dirfd(AsDir(handle_)), e->d_name, &st, AT_SYMLINK_NOFOLLOW) != 0)
            {
                if (os::LastError() == ENOENT)
                {
                    continue;  // removed meanwhile
                }
                return Status::FromOs(os::LastError());
            }
            entry->type = TypeFromMode(st.st_mode);
        }
        *found = true;
        return Status::Success();
    }
}

namespace detail
{

Status GetIdentity(const Path& path, Identity* out)
{
    char native[kMaxPathBytes + 1];
    if (!path.ToNative(native, sizeof(native)))
    {
        return Status::Of(Error::TooLong);
    }
    struct stat st;
    if (::stat(native, &st) != 0)
    {
        return Status::FromOs(os::LastError());
    }
    out->device = static_cast<u64>(st.st_dev);
    out->inode = static_cast<u64>(st.st_ino);
    return Status::Success();
}

}  // namespace detail

}  // namespace engine_placeholder::io
