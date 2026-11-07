// The Windows backend of io/file_system.h, second half (Phase 13 Day 152): removing, renaming, copying, permissions, creating directories
// and temporary names, the current directory, resolving paths, links, and the special folders. Stat and Lstat are io/windows/file_system.cpp.
// One of the places allowed to call the Win32 file API directly.
//
// Renames and removals use the POSIX semantics NTFS offers since Windows 10 1809 (FileRenameInfoEx, FileDispositionInfoEx): a rename
// replaces a file that is open, read-only or an empty directory, and a removed file's name is gone at once even while it is open, as on
// Linux. A file system without them (FAT) gets the classic MoveFileExW / DeleteFileW path.
#include <cstdlib>
#include <cstring>
#include <cwchar>

#include "containers/vector.h"
#include "io/file_system.h"
#include "io/windows/win_path.h"
#include "os/windows/utf16.h"
#include "os/windows/win32.h"

#include "unity/windows_enable.h"
#include <aclapi.h>
#include <bcrypt.h>
#include <knownfolders.h>
#include <shlobj.h>
#include <winioctl.h>
#include "unity/windows_disable.h"

namespace engine_placeholder::io {

namespace {

using windows::HandleGuard;
using windows::kShareAll;
using windows::kWidePathCapacity;
using windows::WidePath;

// FileRenameInfoEx and FileDispositionInfoEx flags (winbase.h names some of them only for newer targets).
constexpr DWORD kRenameReplaceIfExists = 0x1;
constexpr DWORD kRenamePosixSemantics = 0x2;
constexpr DWORD kRenameIgnoreReadOnly = 0x40;
constexpr DWORD kDispositionDelete = 0x1;
constexpr DWORD kDispositionPosixSemantics = 0x2;
constexpr DWORD kDispositionIgnoreReadOnly = 0x10;

Status Win32Error() { return Status::FromOs(static_cast<i32>(GetLastError())); }

// True for the errors a file system without the POSIX information classes gives (FAT, some network shares).
bool NoPosixSemantics(DWORD e) { return e == ERROR_INVALID_PARAMETER || e == ERROR_NOT_SUPPORTED || e == ERROR_INVALID_FUNCTION; }

Status FromWide(const wchar_t* text, usize length, Path* out) {
    char utf8[kMaxPathBytes + 1];
    // Long results come back with the `\\?\` prefix; the layer adds it itself when it needs it, so a Path never carries it.
    if (length >= 8 && std::wcsncmp(text, L"\\\\?\\UNC\\", 8) == 0) {
        wchar_t unc[kMaxPathBytes + 8];
        if (length - 6 >= sizeof(unc) / sizeof(unc[0])) return Status::Of(Error::TooLong);
        unc[0] = L'\\';
        unc[1] = L'\\';
        std::wmemcpy(unc + 2, text + 8, length - 8);
        const usize n = os::windows::Utf16ToUtf8(unc, length - 6, utf8, sizeof(utf8));
        if (n >= sizeof(utf8) || !Path::Parse(StringView(utf8, n), PathStyle::Windows, out)) return Status::Of(Error::TooLong);
        return Status::Success();
    }
    if (length >= 4 && std::wcsncmp(text, L"\\\\?\\", 4) == 0 && length >= 6 && text[5] == L':') {
        text += 4;
        length -= 4;
    }
    const usize n = os::windows::Utf16ToUtf8(text, length, utf8, sizeof(utf8));
    if (n >= sizeof(utf8) || !Path::Parse(StringView(utf8, n), PathStyle::Windows, out)) return Status::Of(Error::TooLong);
    return Status::Success();
}

// Removes one name (a file, an empty directory, a link or junction as itself), POSIX style: gone at once even while open, read-only or not.
Status RemoveOne(const WidePath& wide) {
    const HANDLE h = CreateFileW(wide.text, DELETE | FILE_READ_ATTRIBUTES, kShareAll, nullptr, OPEN_EXISTING,
                                 FILE_FLAG_BACKUP_SEMANTICS | FILE_FLAG_OPEN_REPARSE_POINT, nullptr);
    if (h == INVALID_HANDLE_VALUE) return windows::LastError(wide);
    FILE_DISPOSITION_INFO_EX info{};
    info.Flags = kDispositionDelete | kDispositionPosixSemantics | kDispositionIgnoreReadOnly;
    const BOOL ok = SetFileInformationByHandle(h, FileDispositionInfoEx, &info, sizeof(info));
    const DWORD e = ok ? 0 : GetLastError();
    CloseHandle(h);
    if (ok) return Status::Success();
    if (!NoPosixSemantics(e)) return Status::FromOs(static_cast<i32>(e));
    // The classic calls: the read-only attribute must go first, and comes back if the removal fails.
    const DWORD attributes = GetFileAttributesW(wide.text);
    if (attributes == INVALID_FILE_ATTRIBUTES) return windows::LastError(wide);
    if ((attributes & FILE_ATTRIBUTE_DIRECTORY) != 0) return RemoveDirectoryW(wide.text) ? Status::Success() : Win32Error();
    if ((attributes & FILE_ATTRIBUTE_READONLY) != 0) SetFileAttributesW(wide.text, attributes & ~static_cast<DWORD>(FILE_ATTRIBUTE_READONLY));
    if (DeleteFileW(wide.text)) return Status::Success();
    const Status s = Win32Error();
    if ((attributes & FILE_ATTRIBUTE_READONLY) != 0) SetFileAttributesW(wide.text, attributes);
    return s;
}

constexpr int kMaxTreeDepth = 128;

// Removes the tree at `path` (an extended-form buffer of kWidePathCapacity whose first `length` characters are the path), never following a
// link or junction: those are removed as themselves. Appends child names to the buffer in place and restores it.
Status RemoveTree(WidePath& path, int depth) {
    const DWORD attributes = GetFileAttributesW(path.text);
    if (attributes == INVALID_FILE_ATTRIBUTES) return windows::LastError(path);
    const bool directory = (attributes & FILE_ATTRIBUTE_DIRECTORY) != 0 && (attributes & FILE_ATTRIBUTE_REPARSE_POINT) == 0;
    if (!directory) return RemoveOne(path);
    if (depth >= kMaxTreeDepth) return Status::Of(Error::TooLong);
    const usize base = path.length;
    for (int attempt = 0; attempt < 3; ++attempt) {
        if (base + 3 >= kWidePathCapacity) return Status::Of(Error::TooLong);
        path.text[base] = L'\\';
        path.text[base + 1] = L'*';
        path.text[base + 2] = 0;
        WIN32_FIND_DATAW data;
        const HANDLE find = FindFirstFileExW(path.text, FindExInfoBasic, &data, FindExSearchNameMatch, nullptr, FIND_FIRST_EX_LARGE_FETCH);
        const DWORD find_error = find == INVALID_HANDLE_VALUE ? GetLastError() : 0;
        path.text[base] = 0;
        // A directory that cannot be listed is reported as such (PermissionDenied), not as the NotEmpty its removal would say.
        if (find == INVALID_HANDLE_VALUE && find_error != ERROR_FILE_NOT_FOUND) return Status::FromOs(static_cast<i32>(find_error));
        Status first;
        if (find != INVALID_HANDLE_VALUE) {
            do {
                const wchar_t* name = data.cFileName;
                if (name[0] == L'.' && (name[1] == 0 || (name[1] == L'.' && name[2] == 0))) continue;
                const usize n = std::wcslen(name);
                if (base + 1 + n >= kWidePathCapacity) {
                    if (first.Ok()) first = Status::Of(Error::TooLong);
                    continue;
                }
                path.text[base] = L'\\';
                std::wmemcpy(path.text + base + 1, name, n + 1);
                path.length = base + 1 + n;
                const Status s = RemoveTree(path, depth + 1);
                path.text[base] = 0;
                path.length = base;
                if (s.Failed() && !s.Is(Error::NotFound) && first.Ok()) first = s; // removed meanwhile: fine
            } while (FindNextFileW(find, &data));
            FindClose(find);
        }
        if (first.Failed()) return first;
        const Status removed = RemoveOne(path);
        if (!removed.Is(Error::NotEmpty)) return removed; // something appeared meanwhile: sweep again (a few times)
    }
    return Status::Of(Error::NotEmpty);
}

// Renames the name behind `source` (an open handle with DELETE access) to `to`, POSIX style.
BOOL RenameHandle(HANDLE source, const WidePath& to) {
    alignas(FILE_RENAME_INFO) byte buffer[sizeof(FILE_RENAME_INFO) + kWidePathCapacity * sizeof(wchar_t)];
    FILE_RENAME_INFO* info = reinterpret_cast<FILE_RENAME_INFO*>(buffer);
    std::memset(info, 0, sizeof(FILE_RENAME_INFO));
    info->Flags = kRenameReplaceIfExists | kRenamePosixSemantics | kRenameIgnoreReadOnly;
    info->RootDirectory = nullptr;
    info->FileNameLength = static_cast<DWORD>(to.length * sizeof(wchar_t));
    std::memcpy(info->FileName, to.text, (to.length + 1) * sizeof(wchar_t));
    return SetFileInformationByHandle(source, FileRenameInfoEx, info, static_cast<DWORD>(sizeof(FILE_RENAME_INFO) + to.length * sizeof(wchar_t)));
}

// The current user's SID, copied into `out` (at least SECURITY_MAX_SID_SIZE bytes). Returns false when the token cannot be read.
bool CurrentUserSid(byte* out, usize capacity) {
    HANDLE token = nullptr;
    if (!OpenProcessToken(GetCurrentProcess(), TOKEN_QUERY, &token)) return false;
    alignas(TOKEN_USER) byte user[sizeof(TOKEN_USER) + SECURITY_MAX_SID_SIZE];
    DWORD length = 0;
    const BOOL got = GetTokenInformation(token, TokenUser, user, sizeof(user), &length);
    CloseHandle(token);
    if (!got) return false;
    PSID sid = reinterpret_cast<TOKEN_USER*>(user)->User.Sid;
    return CopySid(static_cast<DWORD>(capacity), out, sid) != FALSE;
}

// The ACL of a private directory: the current user may do everything, nobody else anything (inherited by what is created inside). Free
// with LocalFree.
PACL PrivateAcl(byte* sid) {
    EXPLICIT_ACCESSW entry{};
    entry.grfAccessPermissions = GENERIC_ALL;
    entry.grfAccessMode = SET_ACCESS;
    entry.grfInheritance = SUB_CONTAINERS_AND_OBJECTS_INHERIT;
    entry.Trustee.TrusteeForm = TRUSTEE_IS_SID;
    entry.Trustee.TrusteeType = TRUSTEE_IS_USER;
    entry.Trustee.ptstrName = reinterpret_cast<LPWSTR>(sid);
    PACL acl = nullptr;
    return SetEntriesInAclW(1, &entry, nullptr, &acl) == ERROR_SUCCESS ? acl : nullptr;
}

bool ValidLeaf(StringView name) {
    return !name.Empty() && name != StringView(".") && name != StringView("..") && name.IndexOf('/') == name.Length() &&
           name.IndexOf('\\') == name.Length() && name.IndexOf(':') == name.Length() && name.IndexOf('\0') == name.Length() &&
           name[name.Length() - 1] != '.' && name[name.Length() - 1] != ' ' && !windows::IsReservedDeviceName(name);
}

Status MakeTemp(const Path& directory, StringView prefix, bool as_directory, Path* out) {
    if (!ValidLeaf(prefix) || prefix.Length() > 200) return Status::Of(Error::Invalid);
    static const char kAlphabet[] = "abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789";
    for (int attempt = 0; attempt < 100; ++attempt) {
        byte random[8];
        if (BCryptGenRandom(nullptr, random, sizeof(random), BCRYPT_USE_SYSTEM_PREFERRED_RNG) != 0) return Status::Of(Error::Io);
        char leaf[256];
        std::memcpy(leaf, prefix.Data(), prefix.Length());
        leaf[prefix.Length()] = '.';
        for (usize i = 0; i < sizeof(random); ++i) leaf[prefix.Length() + 1 + i] = kAlphabet[random[i] % (sizeof(kAlphabet) - 1)];
        Path candidate;
        if (!directory.TryJoin(Path(StringView(leaf, prefix.Length() + 1 + sizeof(random)), directory.Style()), &candidate)) {
            return Status::Of(Error::TooLong);
        }
        WidePath wide;
        if (Status s = windows::ToWin32(candidate, &wide); s.Failed()) return s;
        // Exclusive creation. The new name inherits the directory's ACL (in the user's own folders that is already private); an explicit
        // owner-only ACL would follow the file when WriteFileAtomic renames it over its target (BACKEND.md, CreateTempFile).
        bool created = false;
        if (as_directory) {
            created = CreateDirectoryW(wide.text, nullptr) != FALSE;
        } else {
            const HANDLE h = CreateFileW(wide.text, GENERIC_WRITE, kShareAll, nullptr, CREATE_NEW, FILE_ATTRIBUTE_NORMAL, nullptr);
            created = h != INVALID_HANDLE_VALUE;
            if (created) CloseHandle(h);
        }
        if (created) {
            *out = static_cast<Path&&>(candidate);
            return Status::Success();
        }
        const DWORD e = GetLastError();
        if (e != ERROR_FILE_EXISTS && e != ERROR_ALREADY_EXISTS) return windows::RefineNotFound(wide, e);
    }
    return Status::Of(Error::AlreadyExists);
}

Status KnownFolder(REFKNOWNFOLDERID id, Path* out) {
    PWSTR folder = nullptr;
    const HRESULT hr = SHGetKnownFolderPath(id, KF_FLAG_DEFAULT, nullptr, &folder);
    if (FAILED(hr) || folder == nullptr) {
        if (folder != nullptr) CoTaskMemFree(folder);
        return Status{Error::NotFound, static_cast<i32>(hr)};
    }
    const Status s = FromWide(folder, std::wcslen(folder), out);
    CoTaskMemFree(folder);
    return s;
}

// `<known folder>/<app>[/<sub>]`, not created.
Status AppFolder(REFKNOWNFOLDERID id, StringView app_name, const char* sub, Path* out) {
    if (!ValidLeaf(app_name) || app_name.Length() > 200) return Status::Of(Error::Invalid);
    Path base;
    if (Status s = KnownFolder(id, &base); s.Failed()) return s;
    Path result;
    if (!base.TryJoin(Path(app_name, PathStyle::Windows), &result)) return Status::Of(Error::TooLong);
    if (sub != nullptr && !result.TryJoin(Path(StringView(sub), PathStyle::Windows), &result)) return Status::Of(Error::TooLong);
    *out = static_cast<Path&&>(result);
    return Status::Success();
}

// Creates one directory; an existing directory counts as success, an existing file is NotADirectory.
Status CreateOne(const Path& path, SECURITY_ATTRIBUTES* security) {
    WidePath wide;
    if (Status s = windows::ToWin32(path, &wide); s.Failed()) return s;
    if (CreateDirectoryW(wide.text, security)) return Status::Success();
    const Status s = windows::LastError(wide);
    if (s.Is(Error::AlreadyExists)) return IsDirectory(path) ? Status::Success() : Status::Of(Error::NotADirectory);
    return s;
}

// Iterative, not recursive: a path buffer is about 9 KiB and a deep tree (the 140 levels of the RemoveAll test) would overflow the 1 MiB
// default stack of a Windows thread (Day 152). Walks up to the first ancestor that exists or can be made, then creates downwards.
Status CreateDirectoriesWith(const Path& path, SECURITY_ATTRIBUTES* security) {
    Status s = CreateOne(path, security);
    if (!s.Is(Error::NotFound)) return s;
    Vector<Path> missing; // nearest first
    missing.Push(path);
    for (;;) {
        const Path parent = missing[missing.Size() - 1].Parent();
        if (parent.IsEmpty() || parent == missing[missing.Size() - 1]) return s; // no root to stand on
        const Status p = CreateOne(parent, security);
        if (p.Ok()) break;
        if (!p.Is(Error::NotFound)) return p;
        missing.Push(parent);
    }
    for (usize i = missing.Size(); i-- > 0;) {
        if (Status c = CreateOne(missing[i], security); c.Failed()) return c;
    }
    return Status::Success();
}

} // namespace

Status Remove(const Path& path) {
    WidePath wide;
    if (Status s = windows::ToWin32(path, &wide); s.Failed()) return s;
    return RemoveOne(wide);
}

Status RemoveAll(const Path& path) {
    if (path.IsEmpty()) return Status::Of(Error::Invalid);
    Path absolute;
    if (Status s = Absolute(path, &absolute); s.Failed()) return s;
    // Win32 resolves `..` lexically, but refuse it anyway: the same rule as on Linux, where the kernel resolves it through links.
    usize components = 0;
    StringView c;
    Path::Components it = absolute.GetComponents();
    while (it.Next(&c)) {
        if (c == StringView(".") || c == StringView("..")) return Status::Of(Error::Invalid);
        ++components;
    }
    if (components < kRemoveAllMinComponents) return Status::Of(Error::Invalid);
    WidePath wide;
    if (Status s = windows::ToWin32(absolute, &wide, true); s.Failed()) return s;
    return RemoveTree(wide, 0);
}

Status Rename(const Path& from, const Path& to) {
    WidePath a;
    WidePath b;
    if (Status s = windows::ToWin32(from, &a); s.Failed()) return s;
    if (Status s = windows::ToWin32(to, &b, true); s.Failed()) return s; // the rename call takes a full path
    const HANDLE h = CreateFileW(a.text, DELETE | SYNCHRONIZE | FILE_READ_ATTRIBUTES, kShareAll, nullptr, OPEN_EXISTING,
                                 FILE_FLAG_BACKUP_SEMANTICS | FILE_FLAG_OPEN_REPARSE_POINT, nullptr);
    if (h == INVALID_HANDLE_VALUE) return windows::LastError(a);
    BOOL ok = RenameHandle(h, b);
    DWORD e = ok ? 0 : GetLastError();
    CloseHandle(h);
    if (!ok && NoPosixSemantics(e)) {
        ok = MoveFileExW(a.text, b.text, MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH);
        e = ok ? 0 : GetLastError();
    }
    if (ok) return Status::Success();
    if (e == ERROR_ACCESS_DENIED) {
        // A directory in the way: a file cannot replace it, a directory only an empty one (as rename(2) answers).
        const DWORD target = GetFileAttributesW(b.text);
        if (target != INVALID_FILE_ATTRIBUTES && (target & FILE_ATTRIBUTE_DIRECTORY) != 0) {
            const DWORD source = GetFileAttributesW(a.text);
            const bool source_is_directory = source != INVALID_FILE_ATTRIBUTES && (source & FILE_ATTRIBUTE_DIRECTORY) != 0;
            return Status{source_is_directory ? Error::NotEmpty : Error::IsADirectory, static_cast<i32>(e)};
        }
    }
    return windows::RefineNotFound(b, e);
}

Status Copy(const Path& from, const Path& to, bool overwrite) {
    WidePath a;
    WidePath b;
    if (Status s = windows::ToWin32(from, &a); s.Failed()) return s;
    if (Status s = windows::ToWin32(to, &b); s.Failed()) return s;
    FileInfo source;
    if (Status s = Stat(from, &source); s.Failed()) return s;
    if (source.type == FileType::Directory) return Status::Of(Error::IsADirectory);
    // The same file under two names: copying would truncate the source (CopyFile2 would also refuse, less clearly).
    FileInfo target;
    if (Stat(to, &target).Ok()) {
        const HANDLE ha = CreateFileW(a.text, FILE_READ_ATTRIBUTES, kShareAll, nullptr, OPEN_EXISTING, FILE_FLAG_BACKUP_SEMANTICS, nullptr);
        const HANDLE hb = CreateFileW(b.text, FILE_READ_ATTRIBUTES, kShareAll, nullptr, OPEN_EXISTING, FILE_FLAG_BACKUP_SEMANTICS, nullptr);
        HandleGuard ga(ha);
        HandleGuard gb(hb);
        FILE_ID_INFO ia{};
        FILE_ID_INFO ib{};
        if (ha != INVALID_HANDLE_VALUE && hb != INVALID_HANDLE_VALUE && GetFileInformationByHandleEx(ha, FileIdInfo, &ia, sizeof(ia)) &&
            GetFileInformationByHandleEx(hb, FileIdInfo, &ib, sizeof(ib)) && ia.VolumeSerialNumber == ib.VolumeSerialNumber &&
            std::memcmp(&ia.FileId, &ib.FileId, sizeof(ia.FileId)) == 0) {
            return Status::Of(Error::Invalid);
        }
        if (target.type == FileType::Directory) return Status::Of(Error::IsADirectory);
    }
    COPYFILE2_EXTENDED_PARAMETERS parameters{};
    parameters.dwSize = sizeof(parameters);
    parameters.dwCopyFlags = overwrite ? 0 : COPY_FILE_FAIL_IF_EXISTS;
    const HRESULT hr = CopyFile2(a.text, b.text, &parameters);
    if (SUCCEEDED(hr)) return Status::Success();
    const DWORD e = HRESULT_FACILITY(hr) == FACILITY_WIN32 ? static_cast<DWORD>(HRESULT_CODE(hr)) : ERROR_GEN_FAILURE;
    return windows::RefineNotFound(b, e);
}

Status SetPermissions(const Path& path, u32 permissions) {
    WidePath wide;
    if (Status s = windows::ToWin32(path, &wide); s.Failed()) return s;
    const DWORD attributes = GetFileAttributesW(wide.text);
    if (attributes == INVALID_FILE_ATTRIBUTES) return windows::LastError(wide);
    // Only the owner write bit has a counterpart, the read-only attribute, and only on files (BACKEND.md).
    if ((attributes & FILE_ATTRIBUTE_DIRECTORY) != 0) return Status::Success();
    const DWORD wanted = (permissions & 0200) != 0 ? (attributes & ~static_cast<DWORD>(FILE_ATTRIBUTE_READONLY)) : (attributes | FILE_ATTRIBUTE_READONLY);
    if (wanted == attributes) return Status::Success();
    return SetFileAttributesW(wide.text, wanted == 0 ? FILE_ATTRIBUTE_NORMAL : wanted) ? Status::Success() : Win32Error();
}

Status SyncDirectory(const Path& path) {
    WidePath wide;
    if (Status s = windows::ToWin32(path, &wide); s.Failed()) return s;
    const DWORD attributes = GetFileAttributesW(wide.text);
    if (attributes == INVALID_FILE_ATTRIBUTES) return windows::LastError(wide);
    if ((attributes & FILE_ATTRIBUTE_DIRECTORY) == 0) return Status::Of(Error::NotADirectory);
    // NTFS flushes a directory's metadata through a directory handle opened for writing (measured on Day 152: read access is refused).
    const HANDLE h = CreateFileW(wide.text, GENERIC_READ | GENERIC_WRITE, kShareAll, nullptr, OPEN_EXISTING, FILE_FLAG_BACKUP_SEMANTICS, nullptr);
    if (h == INVALID_HANDLE_VALUE) return windows::LastError(wide);
    const Status result = FlushFileBuffers(h) ? Status::Success() : Win32Error();
    CloseHandle(h);
    return result;
}

Status CreateDirectory(const Path& path) {
    WidePath wide;
    if (Status s = windows::ToWin32(path, &wide); s.Failed()) return s;
    return CreateDirectoryW(wide.text, nullptr) ? Status::Success() : windows::LastError(wide);
}

Status CreateDirectories(const Path& path) { return CreateDirectoriesWith(path, nullptr); }

Status CreatePrivateDirectory(const Path& path) {
    alignas(8) byte sid[SECURITY_MAX_SID_SIZE];
    if (!CurrentUserSid(sid, sizeof(sid))) return Win32Error();
    PACL acl = PrivateAcl(sid);
    if (acl == nullptr) return Status::Of(Error::OutOfMemory);
    SECURITY_DESCRIPTOR descriptor;
    InitializeSecurityDescriptor(&descriptor, SECURITY_DESCRIPTOR_REVISION);
    SetSecurityDescriptorDacl(&descriptor, TRUE, acl, FALSE);
    SetSecurityDescriptorControl(&descriptor, SE_DACL_PROTECTED, SE_DACL_PROTECTED); // nothing inherited from the parent
    SECURITY_ATTRIBUTES security{sizeof(SECURITY_ATTRIBUTES), &descriptor, FALSE};
    Status result;
    // Missing parents are created private too.
    const Path parent = path.Parent();
    if (!parent.IsEmpty() && parent != path) result = CreateDirectoriesWith(parent, &security);
    WidePath wide;
    if (result.Ok()) result = windows::ToWin32(path, &wide);
    if (result.Ok() && !CreateDirectoryW(wide.text, &security) && GetLastError() != ERROR_ALREADY_EXISTS) result = windows::LastError(wide);
    if (result.Ok()) {
        // It may have existed: open the name itself (not what a link or junction points to), check it, and make it private.
        const HANDLE h = CreateFileW(wide.text, READ_CONTROL | WRITE_DAC | FILE_READ_ATTRIBUTES, kShareAll, nullptr, OPEN_EXISTING,
                                     FILE_FLAG_BACKUP_SEMANTICS | FILE_FLAG_OPEN_REPARSE_POINT, nullptr);
        if (h == INVALID_HANDLE_VALUE) {
            result = windows::LastError(wide);
        } else {
            FILE_ATTRIBUTE_TAG_INFO tag{};
            PSID owner = nullptr;
            PSECURITY_DESCRIPTOR owner_descriptor = nullptr;
            if (!GetFileInformationByHandleEx(h, FileAttributeTagInfo, &tag, sizeof(tag))) {
                result = Win32Error();
            } else if ((tag.FileAttributes & FILE_ATTRIBUTE_REPARSE_POINT) != 0) {
                result = Status::Of(Error::SymlinkLoop);
            } else if ((tag.FileAttributes & FILE_ATTRIBUTE_DIRECTORY) == 0) {
                result = Status::Of(Error::NotADirectory);
            } else if (GetSecurityInfo(h, SE_FILE_OBJECT, OWNER_SECURITY_INFORMATION, &owner, nullptr, nullptr, nullptr, &owner_descriptor) !=
                       ERROR_SUCCESS) {
                result = Win32Error();
            } else {
                // Owned by this user, or by the Administrators group when this process runs elevated (its default owner).
                bool mine = EqualSid(owner, sid) != FALSE;
                if (!mine) {
                    HANDLE token = nullptr;
                    alignas(TOKEN_OWNER) byte default_owner[sizeof(TOKEN_OWNER) + SECURITY_MAX_SID_SIZE];
                    DWORD length = 0;
                    if (OpenProcessToken(GetCurrentProcess(), TOKEN_QUERY, &token)) {
                        if (GetTokenInformation(token, TokenOwner, default_owner, sizeof(default_owner), &length)) {
                            mine = EqualSid(owner, reinterpret_cast<TOKEN_OWNER*>(default_owner)->Owner) != FALSE;
                        }
                        CloseHandle(token);
                    }
                }
                if (!mine) {
                    result = Status::Of(Error::PermissionDenied);
                } else if (SetSecurityInfo(h, SE_FILE_OBJECT, DACL_SECURITY_INFORMATION | PROTECTED_DACL_SECURITY_INFORMATION, nullptr, nullptr,
                                           acl, nullptr) != ERROR_SUCCESS) {
                    result = Win32Error();
                }
            }
            if (owner_descriptor != nullptr) LocalFree(owner_descriptor);
            CloseHandle(h);
        }
    }
    LocalFree(acl);
    return result;
}

Status CreateTempFile(const Path& directory, StringView prefix, Path* out) { return MakeTemp(directory, prefix, false, out); }
Status CreateTempDirectory(const Path& directory, StringView prefix, Path* out) { return MakeTemp(directory, prefix, true, out); }

Status CurrentDirectory(Path* out) {
    wchar_t buffer[kWidePathCapacity];
    const DWORD n = GetCurrentDirectoryW(static_cast<DWORD>(kWidePathCapacity), buffer);
    if (n == 0) return Win32Error();
    if (n >= kWidePathCapacity) return Status::Of(Error::TooLong);
    return FromWide(buffer, n, out);
}

Status SetCurrentDirectory(const Path& path) {
    WidePath wide;
    if (Status s = windows::ToWin32(path, &wide); s.Failed()) return s;
    return SetCurrentDirectoryW(wide.text) ? Status::Success() : windows::LastError(wide);
}

Status Absolute(const Path& path, Path* out) {
    if (path.IsAbsolute()) {
        *out = path;
        return Status::Success();
    }
    // Lexical, `..` kept: only the missing root is filled in.
    Path base;
    const StringView root_name = path.RootName();
    if (!root_name.Empty()) {
        // `C:foo`: relative to drive C's own current directory, which Win32 keeps per drive (GetFullPathNameW of `C:` returns it).
        wchar_t drive[3] = {static_cast<wchar_t>(root_name[0]), L':', 0};
        wchar_t buffer[kWidePathCapacity];
        const DWORD n = GetFullPathNameW(drive, static_cast<DWORD>(kWidePathCapacity), buffer, nullptr);
        if (n == 0) return Win32Error();
        if (n >= kWidePathCapacity) return Status::Of(Error::TooLong);
        if (Status s = FromWide(buffer, n, &base); s.Failed()) return s;
        // What follows the drive, read as a relative path.
        Path rest;
        if (!Path::Parse(path.View().Substring(root_name.Length()), PathStyle::Windows, &rest)) return Status::Of(Error::TooLong);
        Path result;
        if (!base.TryJoin(rest, &result)) return Status::Of(Error::TooLong);
        *out = static_cast<Path&&>(result);
        return Status::Success();
    }
    if (Status s = CurrentDirectory(&base); s.Failed()) return s;
    Path result;
    // A rooted path without a drive (`/x`) takes the current directory's root name; a relative one joins it.
    if (!base.TryJoin(path, &result)) return Status::Of(Error::TooLong);
    *out = static_cast<Path&&>(result);
    return Status::Success();
}

Status Canonical(const Path& path, Path* out) {
    WidePath wide;
    if (Status s = windows::ToWin32(path, &wide); s.Failed()) return s;
    const HANDLE h = CreateFileW(wide.text, FILE_READ_ATTRIBUTES, kShareAll, nullptr, OPEN_EXISTING, FILE_FLAG_BACKUP_SEMANTICS, nullptr);
    if (h == INVALID_HANDLE_VALUE) return windows::LastError(wide);
    wchar_t buffer[kWidePathCapacity];
    const DWORD n = GetFinalPathNameByHandleW(h, buffer, static_cast<DWORD>(kWidePathCapacity), FILE_NAME_NORMALIZED | VOLUME_NAME_DOS);
    const DWORD e = n == 0 ? GetLastError() : 0;
    CloseHandle(h);
    if (n == 0) return Status::FromOs(static_cast<i32>(e));
    if (n >= kWidePathCapacity) return Status::Of(Error::TooLong);
    return FromWide(buffer, n, out);
}

Status ReadLink(const Path& path, Path* out) {
    WidePath wide;
    if (Status s = windows::ToWin32(path, &wide); s.Failed()) return s;
    const HANDLE h = CreateFileW(wide.text, FILE_READ_ATTRIBUTES, kShareAll, nullptr, OPEN_EXISTING,
                                 FILE_FLAG_BACKUP_SEMANTICS | FILE_FLAG_OPEN_REPARSE_POINT, nullptr);
    if (h == INVALID_HANDLE_VALUE) return windows::LastError(wide);
    // REPARSE_DATA_BUFFER, which only the driver headers declare: the symbolic link form has a Flags word the mount point form lacks.
    struct ReparseHeader {
        DWORD tag;
        WORD data_length;
        WORD reserved;
        WORD substitute_offset;
        WORD substitute_length;
        WORD print_offset;
        WORD print_length;
    };
    alignas(8) byte buffer[MAXIMUM_REPARSE_DATA_BUFFER_SIZE];
    DWORD got = 0;
    const BOOL ok = DeviceIoControl(h, FSCTL_GET_REPARSE_POINT, nullptr, 0, buffer, sizeof(buffer), &got, nullptr);
    const DWORD e = ok ? 0 : GetLastError();
    CloseHandle(h);
    if (!ok) return Status::FromOs(static_cast<i32>(e));
    if (got < sizeof(ReparseHeader)) return Status::Of(Error::Io);
    ReparseHeader header;
    std::memcpy(&header, buffer, sizeof(header));
    usize names_at = sizeof(ReparseHeader);
    if (header.tag == IO_REPARSE_TAG_SYMLINK) {
        names_at += sizeof(ULONG); // Flags
    } else if (header.tag != IO_REPARSE_TAG_MOUNT_POINT) {
        return Status::Of(Error::Invalid); // another kind of reparse point is not a link
    }
    const bool use_print = header.print_length != 0;
    const usize offset = names_at + (use_print ? header.print_offset : header.substitute_offset);
    usize length = (use_print ? header.print_length : header.substitute_length) / sizeof(wchar_t);
    if (offset + length * sizeof(wchar_t) > got) return Status::Of(Error::Io);
    wchar_t target[MAXIMUM_REPARSE_DATA_BUFFER_SIZE / sizeof(wchar_t)];
    std::memcpy(target, buffer + offset, length * sizeof(wchar_t));
    usize start = 0;
    if (!use_print && length >= 4 && std::wcsncmp(target, L"\\??\\", 4) == 0) start = 4; // the NT form of an absolute target
    return FromWide(target + start, length - start, out);
}

Status TempDirectory(Path* out) {
    wchar_t buffer[MAX_PATH + 2];
    const DWORD n = GetTempPath2W(static_cast<DWORD>(MAX_PATH + 2), buffer);
    if (n == 0) return Win32Error();
    if (n > MAX_PATH + 1) return Status::Of(Error::TooLong);
    return FromWide(buffer, n, out);
}

Status ExecutablePath(Path* out) {
    wchar_t buffer[kWidePathCapacity];
    const DWORD n = GetModuleFileNameW(nullptr, buffer, static_cast<DWORD>(kWidePathCapacity));
    if (n == 0) return Win32Error();
    if (n >= kWidePathCapacity) return Status::Of(Error::TooLong); // truncated
    return FromWide(buffer, n, out);
}

Status ExecutableDirectory(Path* out) {
    Path exe;
    if (Status s = ExecutablePath(&exe); s.Failed()) return s;
    *out = exe.Parent();
    return Status::Success();
}

Status UserConfigDirectory(StringView app_name, Path* out) { return AppFolder(FOLDERID_RoamingAppData, app_name, nullptr, out); }
Status UserCacheDirectory(StringView app_name, Path* out) { return AppFolder(FOLDERID_LocalAppData, app_name, "cache", out); }
Status UserDataDirectory(StringView app_name, Path* out) { return AppFolder(FOLDERID_LocalAppData, app_name, nullptr, out); }
Status UserStateDirectory(StringView app_name, Path* out) { return AppFolder(FOLDERID_LocalAppData, app_name, "state", out); }

Status CrashDumpDirectory(Path* out) {
    char app[256];
    const char* configured = std::getenv("ENGINE_APP_NAME"); // read before any settings exist (the crash handler's directory)
    if (configured != nullptr && configured[0] != '\0') {
        const usize length = std::strlen(configured);
        if (length > 200) return Status::Of(Error::Invalid);
        std::memcpy(app, configured, length + 1);
    } else {
        Path exe;
        if (Status s = ExecutablePath(&exe); s.Failed()) return s;
        const StringView name = exe.Stem(); // `game.exe` -> `game`
        if (name.Empty() || name.Length() > 200) return Status::Of(Error::NotFound);
        std::memcpy(app, name.Data(), name.Length());
        app[name.Length()] = '\0';
    }
    Path dir;
    if (Status s = AppFolder(FOLDERID_LocalAppData, StringView(app), "crashes", &dir); s.Failed()) return s; // validates the name
    if (Status s = CreatePrivateDirectory(dir); s.Failed()) return s;
    *out = static_cast<Path&&>(dir);
    return Status::Success();
}

} // namespace engine_placeholder::io
