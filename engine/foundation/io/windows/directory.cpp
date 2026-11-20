// The Windows backend of io/directory.h (Phase 13 Day 152): Directory::Open, Next, Close, DirectoryEntry::Info and
// detail::GetIdentity; the rest is the neutral io/directory.cpp. One of the places allowed to call the Win32 directory
// API directly.
#include "io/directory.h"

#include "containers/unique_ptr.h"
#include "io/detail/directory_identity.h"
#include "io/windows/win_path.h"
#include "os/windows/utf16.h"
#include "os/windows/win32.h"

namespace vse::io
{

namespace
{

// An open listing: the find handle, the entry FindFirstFileExW returned before the first Next, and the current name in
// UTF-8.
struct FindState
{
    HANDLE find = INVALID_HANDLE_VALUE;
    bool pending = false;  // `data` holds an entry Next has not returned yet
    WIN32_FIND_DATAW data{};
    char name[4 * MAX_PATH];  // cFileName is at most MAX_PATH UTF-16 units: 3 bytes each in UTF-8 at most
    Path dir;
};

FindState* AsState(void* handle)
{
    return static_cast<FindState*>(handle);
}

// What a directory entry is, without following it: a symbolic link is Symlink, a junction or another name surrogate
// Other (as Lstat says), any other reparse point (a cloud placeholder) the file or directory it stands for.
FileType TypeFromFindData(const WIN32_FIND_DATAW& data)
{
    const bool directory = (data.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) != 0;
    if ((data.dwFileAttributes & FILE_ATTRIBUTE_REPARSE_POINT) != 0)
    {
        if (data.dwReserved0 == IO_REPARSE_TAG_SYMLINK)
        {
            return FileType::Symlink;  // dwReserved0 is the reparse tag
        }
        if (IsReparseTagNameSurrogate(data.dwReserved0))
        {
            return FileType::Other;
        }
    }
    if ((data.dwFileAttributes & FILE_ATTRIBUTE_DEVICE) != 0)
    {
        return FileType::Other;
    }
    return directory ? FileType::Directory : FileType::Regular;
}

bool IsDotEntry(const wchar_t* name)
{
    return name[0] == L'.' && (name[1] == 0 || (name[1] == L'.' && name[2] == 0));
}

}  // namespace

Status DirectoryEntry::Info(FileInfo* out) const
{
    if (owner == nullptr)
    {
        return Status::Of(Error::Invalid);
    }
    Path path;
    if (!AsState(owner)->dir.TryJoin(Path(name, AsState(owner)->dir.Style()), &path))
    {
        return Status::Of(Error::TooLong);
    }
    return Lstat(path, out);
}

void Directory::Close()
{
    if (handle_ != nullptr)
    {
        FindState* state = AsState(handle_);
        if (state->find != INVALID_HANDLE_VALUE)
        {
            FindClose(state->find);
        }
        UniquePtr<FindState> owned(state);  // made by MakeUnique in Open
        handle_ = nullptr;
    }
}

Status Directory::Open(const Path& path, Directory* out)
{
    windows::WidePath pattern;
    Path all;
    if (!path.TryJoin(Path("*", path.Style()), &all))
    {
        return Status::Of(Error::TooLong);
    }
    if (Status s = windows::ToWin32(all, &pattern); s.Failed())
    {
        return s;
    }
    UniquePtr<FindState> state = MakeUnique<FindState>();
    if (!state)
    {
        return Status::Of(Error::OutOfMemory);
    }
    state->find = FindFirstFileExW(pattern.text, FindExInfoBasic, &state->data, FindExSearchNameMatch, nullptr,
                                   FIND_FIRST_EX_LARGE_FETCH);
    if (state->find == INVALID_HANDLE_VALUE)
    {
        const DWORD e = GetLastError();
        // An empty root directory (`C:\` of an empty volume) has no `.` and `..` either: no match is an empty listing,
        // not an error.
        if (e != ERROR_FILE_NOT_FOUND)
        {
            return windows::RefineNotFound(pattern, e);
        }
        if (!IsDirectory(path))
        {
            return windows::RefineNotFound(pattern, ERROR_PATH_NOT_FOUND);
        }
    }
    else
    {
        state->pending = true;
    }
    state->dir = path;
    out->Close();
    out->handle_ = state.Release();
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
    FindState* state = AsState(handle_);
    for (;;)
    {
        if (!state->pending)
        {
            if (state->find == INVALID_HANDLE_VALUE)
            {
                return Status::Success();
            }
            if (!FindNextFileW(state->find, &state->data))
            {
                const DWORD e = GetLastError();
                if (e == ERROR_NO_MORE_FILES)
                {
                    return Status::Success();
                }
                return Status::FromOs(static_cast<i32>(e));
            }
        }
        state->pending = false;
        if (IsDotEntry(state->data.cFileName))
        {
            continue;
        }
        usize units = 0;
        while (units < MAX_PATH && state->data.cFileName[units] != 0)
        {
            ++units;
        }
        const usize bytes = os::windows::Utf16ToUtf8(state->data.cFileName, units, state->name, sizeof(state->name));
        if (bytes >= sizeof(state->name))
        {
            return Status::Of(Error::TooLong);
        }
        entry->name = StringView(state->name, bytes);
        entry->type = TypeFromFindData(state->data);
        entry->owner = handle_;
        *found = true;
        return Status::Success();
    }
}

namespace detail
{

Status GetIdentity(const Path& path, Identity* out)
{
    windows::WidePath wide;
    if (Status s = windows::ToWin32(path, &wide); s.Failed())
    {
        return s;
    }
    const HANDLE h = CreateFileW(wide.text, FILE_READ_ATTRIBUTES, windows::kShareAll, nullptr, OPEN_EXISTING,
                                 FILE_FLAG_BACKUP_SEMANTICS, nullptr);
    if (h == INVALID_HANDLE_VALUE)
    {
        return windows::LastError(wide);
    }
    // The 128-bit id of FileIdInfo covers ReFS, whose ids do not fit the 64-bit file index; it is folded into the 64
    // bits a walk compares.
    FILE_ID_INFO id{};
    BY_HANDLE_FILE_INFORMATION info{};
    Status result;
    if (GetFileInformationByHandleEx(h, FileIdInfo, &id, sizeof(id)))
    {
        u64 low = 0;
        u64 high = 0;
        for (int i = 0; i < 8; ++i)
        {
            low |= static_cast<u64>(id.FileId.Identifier[i]) << (8 * i);
        }
        for (int i = 0; i < 8; ++i)
        {
            high |= static_cast<u64>(id.FileId.Identifier[8 + i]) << (8 * i);
        }
        out->device = id.VolumeSerialNumber;
        out->inode = low ^ (high * 0x9E3779B97F4A7C15ull);
    }
    else if (GetFileInformationByHandle(h, &info))
    {
        out->device = info.dwVolumeSerialNumber;
        out->inode = static_cast<u64>(info.nFileIndexHigh) << 32 | info.nFileIndexLow;
    }
    else
    {
        result = Status::FromOs(static_cast<i32>(GetLastError()));
    }
    CloseHandle(h);
    return result;
}

}  // namespace detail

}  // namespace vse::io
