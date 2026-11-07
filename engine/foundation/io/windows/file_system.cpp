// The Windows backend of io/file_system.h (Phase 13). One of the places allowed to call the Win32 file API directly.
//
// Day 151 implements Stat, Lstat and the existence questions. Every other function of the header is Day 152's
// (plans/phase-13-windows-dx12/day-152-file-layer-directories-async.md) and returns `Unsupported` until then, so the library links and
// nothing pretends to work; KNOWN-ISSUES.md lists them.
#include "io/file_system.h"

#include "io/windows/win_path.h"
#include "os/windows/win32.h"

#include "unity/windows_enable.h"
#include <aclapi.h>
#include "unity/windows_disable.h"

namespace engine_placeholder::io {

namespace {

using windows::kShareAll;
constexpr i64 kUnixEpochIn100Ns = 116444736000000000LL; // 1970-01-01 as a FILETIME (100 ns units since 1601-01-01)

class HandleGuard {
public:
    explicit HandleGuard(HANDLE h) : h_(h) {}
    ~HandleGuard() {
        if (h_ != INVALID_HANDLE_VALUE) CloseHandle(h_);
    }
    HandleGuard(const HandleGuard&) = delete;
    HandleGuard& operator=(const HandleGuard&) = delete;

private:
    HANDLE h_;
};

// An impersonation copy of the process token, made once: AccessCheck needs one. Null when it cannot be made (then write access is not
// checked against the ACL). Kept for the life of the process.
HANDLE ImpersonationToken() {
    static const HANDLE token = [] {
        HANDLE process = nullptr;
        if (!OpenProcessToken(GetCurrentProcess(), TOKEN_DUPLICATE | TOKEN_QUERY, &process)) return HANDLE{nullptr};
        HANDLE copy = nullptr;
        const BOOL ok = DuplicateToken(process, SecurityImpersonation, &copy);
        CloseHandle(process);
        return ok ? copy : HANDLE{nullptr};
    }();
    return token;
}

// Whether the current user may write the object behind `h` by its ACL (write data to a file, add a file to a directory). True when it
// cannot be told (no token, no security descriptor): the read-only attribute still counts.
bool AclAllowsWrite(HANDLE h, bool directory) {
    const HANDLE token = ImpersonationToken();
    if (token == nullptr) return true;
    PSECURITY_DESCRIPTOR descriptor = nullptr;
    if (GetSecurityInfo(h, SE_FILE_OBJECT, OWNER_SECURITY_INFORMATION | GROUP_SECURITY_INFORMATION | DACL_SECURITY_INFORMATION, nullptr,
                        nullptr, nullptr, nullptr, &descriptor) != ERROR_SUCCESS) {
        return true;
    }
    GENERIC_MAPPING mapping = {FILE_GENERIC_READ, FILE_GENERIC_WRITE, FILE_GENERIC_EXECUTE, FILE_ALL_ACCESS};
    PRIVILEGE_SET privileges{};
    DWORD privileges_length = sizeof(privileges);
    DWORD granted = 0;
    BOOL allowed = FALSE;
    const BOOL checked = AccessCheck(descriptor, token, directory ? FILE_ADD_FILE : FILE_WRITE_DATA, &mapping, &privileges,
                                     &privileges_length, &granted, &allowed);
    LocalFree(descriptor);
    return !checked || allowed;
}

u32 PermissionsFor(FileType type, bool read_only_attribute) {
    if (type == FileType::Symlink || type == FileType::Other) return 0777;
    if (type == FileType::Directory) return 0755;
    return read_only_attribute ? 0444 : 0644;
}

// A file another program holds open without sharing even its attributes (`pagefile.sys`) cannot be opened at all, not even for
// FILE_READ_ATTRIBUTES, and GetFileAttributesExW is refused too: describe it from its directory entry instead (the entry also carries
// the reparse tag). `sharing_error` is returned when the entry cannot be read, or when the name has a wildcard the search would expand.
Status StatFromDirectoryEntry(const windows::WidePath& wide, FileInfo* out, bool follow, DWORD sharing_error) {
    for (usize i = wide.length; i > 0 && wide.text[i - 1] != L'\\' && wide.text[i - 1] != L'/'; --i) {
        if (wide.text[i - 1] == L'*' || wide.text[i - 1] == L'?') return Status::FromOs(static_cast<i32>(sharing_error));
    }
    WIN32_FIND_DATAW data;
    const HANDLE find = FindFirstFileExW(wide.text, FindExInfoBasic, &data, FindExSearchNameMatch, nullptr, 0);
    if (find == INVALID_HANDLE_VALUE) return Status::FromOs(static_cast<i32>(sharing_error));
    FindClose(find);
    const bool directory = (data.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) != 0;
    const bool reparse = (data.dwFileAttributes & FILE_ATTRIBUTE_REPARSE_POINT) != 0;
    FileInfo info;
    if (!follow && reparse && data.dwReserved0 == IO_REPARSE_TAG_SYMLINK) { // dwReserved0 is the reparse tag
        info.type = FileType::Symlink;
    } else if (!follow && reparse && IsReparseTagNameSurrogate(data.dwReserved0)) {
        info.type = FileType::Other;
    } else {
        info.type = directory ? FileType::Directory : FileType::Regular;
    }
    info.size = info.type == FileType::Regular ? (static_cast<u64>(data.nFileSizeHigh) << 32 | data.nFileSizeLow) : 0;
    const i64 written = static_cast<i64>(static_cast<u64>(data.ftLastWriteTime.dwHighDateTime) << 32 | data.ftLastWriteTime.dwLowDateTime);
    info.modified_unix_ns = (written - kUnixEpochIn100Ns) * 100;
    const bool read_only_attribute = !directory && (data.dwFileAttributes & FILE_ATTRIBUTE_READONLY) != 0;
    info.permissions = PermissionsFor(info.type, read_only_attribute);
    info.read_only = (info.type == FileType::Regular || info.type == FileType::Directory) && read_only_attribute;
    *out = info;
    return Status::Success();
}

// `want_read_only`: compute FileInfo::read_only from the ACL as well (Stat and Lstat). Exists, IsFile and IsDirectory do not read it, so
// they skip opening the security descriptor and the access check.
Status DoStat(const Path& path, FileInfo* out, bool follow, bool want_read_only = true) {
    windows::WidePath wide;
    if (Status s = windows::ToWin32(path, &wide); s.Failed()) return s;
    // FILE_FLAG_BACKUP_SEMANTICS opens directories too; without FILE_FLAG_OPEN_REPARSE_POINT the open follows links and junctions.
    const DWORD flags = FILE_FLAG_BACKUP_SEMANTICS | (follow ? 0u : static_cast<DWORD>(FILE_FLAG_OPEN_REPARSE_POINT));
    bool have_acl = want_read_only;
    HANDLE h = CreateFileW(wide.text, FILE_READ_ATTRIBUTES | (have_acl ? READ_CONTROL : 0u), kShareAll, nullptr, OPEN_EXISTING, flags, nullptr);
    if (h == INVALID_HANDLE_VALUE && have_acl && GetLastError() == ERROR_ACCESS_DENIED) {
        have_acl = false; // the ACL itself may not be readable: the attributes still are
        h = CreateFileW(wide.text, FILE_READ_ATTRIBUTES, kShareAll, nullptr, OPEN_EXISTING, flags, nullptr);
    }
    if (h == INVALID_HANDLE_VALUE) {
        if (GetLastError() == ERROR_SHARING_VIOLATION) return StatFromDirectoryEntry(wide, out, follow, ERROR_SHARING_VIOLATION);
        return windows::LastError(wide);
    }
    HandleGuard guard(h);

    FileInfo info;
    if (GetFileType(h) != FILE_TYPE_DISK) { // a device (`\\.\NUL`, a console) or a pipe
        info.type = FileType::Other;
        info.permissions = 0666;
        *out = info;
        return Status::Success();
    }
    FILE_ATTRIBUTE_TAG_INFO tag{};
    FILE_STANDARD_INFO standard{};
    FILE_BASIC_INFO basic{};
    if (!GetFileInformationByHandleEx(h, FileAttributeTagInfo, &tag, sizeof(tag)) ||
        !GetFileInformationByHandleEx(h, FileStandardInfo, &standard, sizeof(standard)) ||
        !GetFileInformationByHandleEx(h, FileBasicInfo, &basic, sizeof(basic))) {
        return Status::FromOs(static_cast<i32>(GetLastError()));
    }
    const bool directory = (tag.FileAttributes & FILE_ATTRIBUTE_DIRECTORY) != 0;
    const bool reparse = (tag.FileAttributes & FILE_ATTRIBUTE_REPARSE_POINT) != 0;
    if (!follow && reparse && tag.ReparseTag == IO_REPARSE_TAG_SYMLINK) {
        info.type = FileType::Symlink;
    } else if (!follow && reparse && IsReparseTagNameSurrogate(tag.ReparseTag)) {
        info.type = FileType::Other; // a junction (mount point) or another name surrogate: a link, but not a symbolic one (BACKEND.md)
    } else {
        // Other reparse points (cloud placeholders, deduplicated files) are ordinary files and directories to their users.
        info.type = directory ? FileType::Directory : FileType::Regular;
    }
    info.size = info.type == FileType::Regular ? static_cast<u64>(standard.EndOfFile.QuadPart) : 0;
    info.modified_unix_ns = (static_cast<i64>(basic.LastWriteTime.QuadPart) - kUnixEpochIn100Ns) * 100;
    // POSIX bits from the read-only attribute, which Windows honors on files only (on a directory it marks a customized folder).
    const bool read_only_attribute = !directory && (tag.FileAttributes & FILE_ATTRIBUTE_READONLY) != 0;
    info.permissions = PermissionsFor(info.type, read_only_attribute);
    info.read_only = info.type != FileType::Symlink && info.type != FileType::Other &&
                     (read_only_attribute || (have_acl && !AclAllowsWrite(h, directory)));
    *out = info;
    return Status::Success();
}

Status NotYet() { return Status::Of(Error::Unsupported); } // Day 152

} // namespace

Status Stat(const Path& path, FileInfo* out) { return DoStat(path, out, true); }
Status Lstat(const Path& path, FileInfo* out) { return DoStat(path, out, false); }

bool Exists(const Path& path) {
    FileInfo info;
    return DoStat(path, &info, true, false).Ok();
}

bool IsFile(const Path& path) {
    FileInfo info;
    return DoStat(path, &info, true, false).Ok() && info.type == FileType::Regular;
}

bool IsDirectory(const Path& path) {
    FileInfo info;
    return DoStat(path, &info, true, false).Ok() && info.type == FileType::Directory;
}

// --- Day 152 -------------------------------------------------------------------------------------------------------------
Status Remove(const Path&) { return NotYet(); }
Status RemoveAll(const Path&) { return NotYet(); }
Status Rename(const Path&, const Path&) { return NotYet(); }
Status Copy(const Path&, const Path&, bool) { return NotYet(); }
Status SetPermissions(const Path&, u32) { return NotYet(); }
Status SyncDirectory(const Path&) { return NotYet(); }
Status CreateDirectory(const Path&) { return NotYet(); }
Status CreateDirectories(const Path&) { return NotYet(); }
Status CreatePrivateDirectory(const Path&) { return NotYet(); }
Status CreateTempFile(const Path&, StringView, Path*) { return NotYet(); }
Status CreateTempDirectory(const Path&, StringView, Path*) { return NotYet(); }
Status CurrentDirectory(Path*) { return NotYet(); }
Status SetCurrentDirectory(const Path&) { return NotYet(); }
Status Absolute(const Path&, Path*) { return NotYet(); }
Status Canonical(const Path&, Path*) { return NotYet(); }
Status ReadLink(const Path&, Path*) { return NotYet(); }
Status TempDirectory(Path*) { return NotYet(); }
Status ExecutablePath(Path*) { return NotYet(); }
Status ExecutableDirectory(Path*) { return NotYet(); }
Status UserConfigDirectory(StringView, Path*) { return NotYet(); }
Status UserCacheDirectory(StringView, Path*) { return NotYet(); }
Status UserDataDirectory(StringView, Path*) { return NotYet(); }
Status UserStateDirectory(StringView, Path*) { return NotYet(); }
Status CrashDumpDirectory(Path*) { return NotYet(); }

} // namespace engine_placeholder::io
