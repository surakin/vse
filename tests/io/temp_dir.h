#pragma once

#include <atomic>
#include <cstdlib>
#include <cstring>
#include "build_defs.h"

#if ENGINE_PLATFORM_WINDOWS
#include <cwchar>

#include "os/windows/utf16.h"
#include "os/windows/win32.h"

#include "unity/windows_enable.h"
#include <aclapi.h>
#include <winioctl.h>
#include "unity/windows_disable.h"
#else
#include <dirent.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>
#endif

#include "io/directory.h"
#include "io/file_system.h"
#include "test_text.h"

// Helpers of the file layer's tests. They plant and read files with the operating system directly (POSIX, or Win32 on Windows), so they
// do not depend on the code under test.
namespace io_test {

using namespace engine_placeholder;

inline engine_test::Text Str(StringView v) { return engine_test::MakeText(v.Data() == nullptr ? StringView() : v); }

#if ENGINE_PLATFORM_WINDOWS

/// A path in the form Win32 file functions take (UTF-16, `\` separators, the `\\?\` prefix when long); through os/, not the file layer.
struct WinPath {
    wchar_t text[io::kMaxPathBytes + 16];
    explicit WinPath(StringView utf8) {
        if (os::windows::PathToWin32(utf8, text, sizeof(text) / sizeof(text[0])) >= sizeof(text) / sizeof(text[0])) text[0] = 0;
    }
    explicit WinPath(const io::Path& path) : WinPath(path.View()) {}

    /// The same path with the `\\?\` prefix even when it is short (an absolute drive path), so paths built below it by appending names stay
    /// usable past MAX_PATH.
    static WinPath Extended(const io::Path& path) {
        WinPath p(path);
        const usize n = std::wcslen(p.text);
        const bool prefixed = n >= 4 && p.text[0] == L'\\' && p.text[1] == L'\\' && (p.text[2] == L'?' || p.text[2] == L'.');
        if (!prefixed && n >= 2 && p.text[1] == L':' && n + 4 < sizeof(p.text) / sizeof(p.text[0])) {
            std::wmemmove(p.text + 4, p.text, n + 1);
            std::wmemcpy(p.text, L"\\\\?\\", 4);
        }
        return p;
    }
};

/// Writes `text` to `path` through Win32.
inline bool WriteText(const io::Path& path, const char* text) {
    const WinPath p(path);
    const HANDLE h = CreateFileW(p.text, GENERIC_WRITE, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, nullptr, CREATE_ALWAYS,
                                 FILE_ATTRIBUTE_NORMAL, nullptr);
    if (h == INVALID_HANDLE_VALUE) return false;
    const DWORD n = static_cast<DWORD>(std::strlen(text));
    DWORD put = 0;
    const bool ok = WriteFile(h, text, n, &put, nullptr) && put == n;
    CloseHandle(h);
    return ok;
}

/// Reads up to 4 KiB of a file the same way.
inline engine_test::Text ReadText(const io::Path& path) {
    const WinPath p(path);
    const HANDLE h = CreateFileW(p.text, GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, nullptr, OPEN_EXISTING,
                                 FILE_ATTRIBUTE_NORMAL, nullptr);
    if (h == INVALID_HANDLE_VALUE) return engine_test::MakeText("<unreadable>");
    char buf[4096];
    DWORD got = 0;
    if (!ReadFile(h, buf, sizeof(buf), &got, nullptr)) got = 0;
    CloseHandle(h);
    return engine_test::MakeText(StringView(buf, got));
}

/// Gives `path` a protected DACL that denies the current user `denied` and allows everything else (Windows' counterpart of a chmod that
/// takes a permission away). Returns false when it could not.
inline bool DenyCurrentUser(const io::Path& path, DWORD denied) {
    HANDLE token = nullptr;
    if (!OpenProcessToken(GetCurrentProcess(), TOKEN_QUERY, &token)) return false;
    alignas(TOKEN_USER) byte user[512];
    DWORD length = 0;
    const BOOL got = GetTokenInformation(token, TokenUser, user, sizeof(user), &length);
    CloseHandle(token);
    if (!got) return false;
    EXPLICIT_ACCESSW entries[2] = {};
    for (EXPLICIT_ACCESSW& e : entries) {
        e.grfInheritance = NO_INHERITANCE;
        e.Trustee.TrusteeForm = TRUSTEE_IS_SID;
        e.Trustee.TrusteeType = TRUSTEE_IS_USER;
        e.Trustee.ptstrName = static_cast<LPWSTR>(reinterpret_cast<TOKEN_USER*>(user)->User.Sid);
    }
    entries[0].grfAccessPermissions = denied;
    entries[0].grfAccessMode = DENY_ACCESS;
    entries[1].grfAccessPermissions = GENERIC_ALL;
    entries[1].grfAccessMode = GRANT_ACCESS;
    // Inheritable: a protected ACL replaces what the children inherit, and without it they would be left with no entry at all.
    entries[1].grfInheritance = SUB_CONTAINERS_AND_OBJECTS_INHERIT;
    PACL acl = nullptr;
    // With nothing to deny, only the allow entry (AllowAll).
    if (denied == 0 ? SetEntriesInAclW(1, entries + 1, nullptr, &acl) != ERROR_SUCCESS : SetEntriesInAclW(2, entries, nullptr, &acl) != ERROR_SUCCESS) {
        return false;
    }
    WinPath p(path);
    const DWORD r = SetNamedSecurityInfoW(p.text, SE_FILE_OBJECT, DACL_SECURITY_INFORMATION | PROTECTED_DACL_SECURITY_INFORMATION, nullptr,
                                          nullptr, acl, nullptr);
    LocalFree(acl);
    return r == ERROR_SUCCESS;
}

/// Calls `visit(wide path, attributes)` for every entry of the directory `dir` (not `.` and `..`).
template <typename Visit>
inline void ForEachEntry(const wchar_t* dir, Visit&& visit) {
    // On the heap: callers recurse down a tree, and two path buffers per level on the 1 MiB stack would not last 64 levels.
    constexpr usize kCapacity = io::kMaxPathBytes + 32;
    Vector<wchar_t> buffers;
    buffers.Resize(2 * kCapacity);
    wchar_t* pattern = buffers.Data();
    wchar_t* child = buffers.Data() + kCapacity;
    const usize n = std::wcslen(dir);
    if (n + 3 >= kCapacity) return;
    std::wmemcpy(pattern, dir, n);
    std::wmemcpy(pattern + n, L"\\*", 3);
    WIN32_FIND_DATAW data;
    const HANDLE find = FindFirstFileExW(pattern, FindExInfoBasic, &data, FindExSearchNameMatch, nullptr, 0);
    if (find == INVALID_HANDLE_VALUE) return;
    do {
        if (std::wcscmp(data.cFileName, L".") == 0 || std::wcscmp(data.cFileName, L"..") == 0) continue;
        const usize m = std::wcslen(data.cFileName);
        if (n + 1 + m + 1 > kCapacity) continue;
        std::wmemcpy(child, dir, n);
        child[n] = L'\\';
        std::wmemcpy(child + n + 1, data.cFileName, m + 1);
        visit(static_cast<const wchar_t*>(child), data.dwFileAttributes);
    } while (FindNextFileW(find, &data));
    FindClose(find);
}

/// Clears the read-only attribute in a tree (not following links or junctions) so it can be removed.
inline void MakeWritable(const wchar_t* path, int depth = 0) {
    const DWORD attributes = GetFileAttributesW(path);
    if (depth > 64 || attributes == INVALID_FILE_ATTRIBUTES || (attributes & FILE_ATTRIBUTE_REPARSE_POINT) != 0) return;
    if ((attributes & FILE_ATTRIBUTE_READONLY) != 0) SetFileAttributesW(path, attributes & ~static_cast<DWORD>(FILE_ATTRIBUTE_READONLY));
    if ((attributes & FILE_ATTRIBUTE_DIRECTORY) == 0) return;
    ForEachEntry(path, [&](const wchar_t* child, DWORD) { MakeWritable(child, depth + 1); });
}
inline void MakeWritable(const engine_test::Text& path) { MakeWritable(WinPath(path.View()).text); }

/// Makes `link` a junction (a mount-point reparse point) to the directory `target`. Junctions need no privilege, unlike symbolic links.
inline bool MakeJunction(const io::Path& target, const io::Path& link) {
    const WinPath l(link);
    if (!CreateDirectoryW(l.text, nullptr)) return false;
    const HANDLE h = CreateFileW(l.text, GENERIC_WRITE, 0, nullptr, OPEN_EXISTING, FILE_FLAG_BACKUP_SEMANTICS | FILE_FLAG_OPEN_REPARSE_POINT,
                                 nullptr);
    if (h == INVALID_HANDLE_VALUE) return false;
    char native[io::kMaxPathBytes + 1];
    target.ToNative(native, sizeof(native));
    wchar_t print[io::kMaxPathBytes + 1];
    const usize print_length = os::windows::Utf8ToUtf16(StringView(native), print, sizeof(print) / sizeof(print[0]));
    // The reparse data of a mount point (REPARSE_DATA_BUFFER's MountPointReparseBuffer, which only the driver headers declare).
    struct MountPoint {
        DWORD tag;
        WORD data_length;
        WORD reserved;
        WORD substitute_offset;
        WORD substitute_length;
        WORD print_offset;
        WORD print_length;
        wchar_t path[2 * (io::kMaxPathBytes + 8)];
    };
    MountPoint data{};
    usize at = 0;
    for (const wchar_t c : {L'\\', L'?', L'?', L'\\'}) data.path[at++] = c;
    for (usize i = 0; i < print_length; ++i) data.path[at++] = print[i];
    const usize substitute_length = at;
    data.path[at++] = 0;
    for (usize i = 0; i < print_length; ++i) data.path[at++] = print[i];
    data.path[at++] = 0;
    data.tag = IO_REPARSE_TAG_MOUNT_POINT;
    data.substitute_length = static_cast<WORD>(substitute_length * sizeof(wchar_t));
    data.print_offset = static_cast<WORD>((substitute_length + 1) * sizeof(wchar_t));
    data.print_length = static_cast<WORD>(print_length * sizeof(wchar_t));
    data.data_length = static_cast<WORD>(8 + at * sizeof(wchar_t));
    DWORD unused = 0;
    const BOOL ok = DeviceIoControl(h, FSCTL_SET_REPARSE_POINT, &data, 8 + data.data_length, nullptr, 0, &unused, nullptr);
    CloseHandle(h);
    return ok != FALSE;
}

/// Makes `link` a symbolic link whose text is `target`. Windows allows it without elevation only in Developer Mode: false otherwise.
inline bool MakeSymlink(StringView target, const io::Path& link, bool to_directory = false) {
    wchar_t wide_target[io::kMaxPathBytes + 1];
    char native[io::kMaxPathBytes + 1];
    // A link's text uses Windows separators.
    const usize n = target.Length() < io::kMaxPathBytes ? target.Length() : io::kMaxPathBytes;
    for (usize i = 0; i < n; ++i) native[i] = target[i] == '/' ? '\\' : target[i];
    native[n] = 0;
    os::windows::Utf8ToUtf16(StringView(native, n), wide_target, sizeof(wide_target) / sizeof(wide_target[0]));
    const DWORD flags = SYMBOLIC_LINK_FLAG_ALLOW_UNPRIVILEGED_CREATE | (to_directory ? SYMBOLIC_LINK_FLAG_DIRECTORY : 0);
    return CreateSymbolicLinkW(WinPath(link).text, wide_target, flags) != FALSE;
}

/// True when this process can create symbolic links (asked once, by making one in the temporary directory).
inline bool SymlinksAvailable() {
    static const bool available = [] {
        wchar_t base[MAX_PATH + 1];
        const DWORD n = GetTempPathW(MAX_PATH + 1, base);
        if (n == 0 || n > MAX_PATH - 40) return false;
        wchar_t probe[MAX_PATH + 1];
        swprintf(probe, MAX_PATH + 1, L"%lsre_io_symlink_probe.%lu", base, GetCurrentProcessId());
        const bool ok = CreateSymbolicLinkW(probe, L"target", SYMBOLIC_LINK_FLAG_ALLOW_UNPRIVILEGED_CREATE) != FALSE;
        if (ok) DeleteFileW(probe);
        return ok;
    }();
    return available;
}

/// A link to the directory `target` (absolute): a symbolic link where one can be made, else a junction.
inline bool MakeDirectoryLink(const io::Path& target, const io::Path& link) {
    if (SymlinksAvailable()) return MakeSymlink(target.View(), link, true);
    return MakeJunction(target, link);
}

/// Makes a file read-only the platform's way (the read-only attribute).
inline bool SetReadOnlyFile(const io::Path& path) { return SetFileAttributesW(WinPath(path).text, FILE_ATTRIBUTE_READONLY) != FALSE; }

/// Takes away the right to add entries to a directory (chmod 0555 on POSIX).
inline bool DenyChanges(const io::Path& dir) { return DenyCurrentUser(dir, FILE_ADD_FILE | FILE_ADD_SUBDIRECTORY); }

/// Takes away the right to list a directory (chmod 0000 on POSIX).
inline bool DenyListing(const io::Path& dir) { return DenyCurrentUser(dir, FILE_LIST_DIRECTORY | FILE_TRAVERSE); }

/// Gives the current user full access to `path` again, with a protected ACL (undoes DenyCurrentUser, DenyChanges, DenyListing).
inline bool AllowAll(const io::Path& path) { return DenyCurrentUser(path, 0); }

/// Makes `link` a second name of the file `existing`.
inline bool HardLink(const io::Path& existing, const io::Path& link) {
    return CreateHardLinkW(WinPath(link).text, WinPath(existing).text, nullptr) != FALSE;
}

/// True when the process ignores permissions (root on POSIX; never on Windows, where an administrator still meets ACLs).
inline bool RunningAsRoot() { return false; }

/// True when `path`'s ACL is protected and every entry allows the current user (what CreatePrivateDirectory promises; Windows stores an
/// inheritable grant as two entries, the effective one and an inherit-only one).
inline bool AclIsPrivate(const io::Path& path) {
    PACL acl = nullptr;
    PSECURITY_DESCRIPTOR descriptor = nullptr;
    if (GetNamedSecurityInfoW(WinPath(path).text, SE_FILE_OBJECT, DACL_SECURITY_INFORMATION, nullptr, nullptr, &acl, nullptr, &descriptor) !=
        ERROR_SUCCESS) {
        return false;
    }
    SECURITY_DESCRIPTOR_CONTROL control = 0;
    DWORD revision = 0;
    GetSecurityDescriptorControl(descriptor, &control, &revision);
    bool ok = acl != nullptr && acl->AceCount >= 1 && (control & SE_DACL_PROTECTED) != 0;
    HANDLE token = nullptr;
    alignas(TOKEN_USER) byte user[sizeof(TOKEN_USER) + SECURITY_MAX_SID_SIZE];
    DWORD length = 0;
    ok = ok && OpenProcessToken(GetCurrentProcess(), TOKEN_QUERY, &token) && GetTokenInformation(token, TokenUser, user, sizeof(user), &length);
    if (token != nullptr) CloseHandle(token);
    for (DWORD i = 0; ok && i < acl->AceCount; ++i) {
        void* ace = nullptr;
        ok = GetAce(acl, i, &ace) && static_cast<ACE_HEADER*>(ace)->AceType == ACCESS_ALLOWED_ACE_TYPE &&
             EqualSid(&static_cast<ACCESS_ALLOWED_ACE*>(ace)->SidStart, reinterpret_cast<TOKEN_USER*>(user)->User.Sid);
    }
    LocalFree(descriptor);
    return ok;
}

#else

/// Writes `text` to `path` through the POSIX API (these helpers are independent of the code under test).
inline bool WriteText(const io::Path& path, const char* text) {
    char p[io::kMaxPathBytes + 1];
    if (!path.ToNative(p, sizeof(p))) return false;
    const int fd = ::open(p, O_WRONLY | O_CREAT | O_TRUNC, 0644);
    if (fd < 0) return false;
    const size_t n = std::strlen(text);
    const bool ok = ::write(fd, text, n) == static_cast<ssize_t>(n);
    ::close(fd);
    return ok;
}

/// Reads up to 4 KiB of a file the same way.
inline engine_test::Text ReadText(const io::Path& path) {
    char p[io::kMaxPathBytes + 1];
    if (!path.ToNative(p, sizeof(p))) return engine_test::MakeText();
    const int fd = ::open(p, O_RDONLY);
    if (fd < 0) return engine_test::MakeText("<unreadable>");
    char buf[4096];
    const ssize_t n = ::read(fd, buf, sizeof(buf));
    ::close(fd);
    return engine_test::MakeText(StringView(buf, n > 0 ? static_cast<size_t>(n) : 0));
}

/// Gives the owner full access to a tree (not following links) so it can be removed after a test made parts read-only.
inline void MakeWritable(const engine_test::Text& path, int depth = 0) {
    struct stat st;
    if (depth > 64 || ::lstat(path.CStr(), &st) != 0 || S_ISLNK(st.st_mode)) return;
    ::chmod(path.CStr(), st.st_mode | S_IRWXU);
    if (!S_ISDIR(st.st_mode)) return;
    if (DIR* d = ::opendir(path.CStr())) {
        while (const dirent* e = ::readdir(d)) {
            if (std::strcmp(e->d_name, ".") != 0 && std::strcmp(e->d_name, "..") != 0) {
                MakeWritable(engine_test::Concat(path.View(), "/", StringView(e->d_name)), depth + 1);
            }
        }
        ::closedir(d);
    }
}

inline bool MakeSymlink(StringView target, const io::Path& link, bool to_directory = false) {
    (void)to_directory;
    return ::symlink(Str(target).CStr(), Str(link.View()).CStr()) == 0;
}
inline bool SymlinksAvailable() { return true; }
inline bool MakeDirectoryLink(const io::Path& target, const io::Path& link) { return MakeSymlink(target.View(), link, true); }
inline bool SetReadOnlyFile(const io::Path& path) { return ::chmod(Str(path.View()).CStr(), 0444) == 0; }
inline bool DenyChanges(const io::Path& dir) { return ::chmod(Str(dir.View()).CStr(), 0555) == 0; }
inline bool DenyListing(const io::Path& dir) { return ::chmod(Str(dir.View()).CStr(), 0000) == 0; }
inline bool AllowAll(const io::Path& path) { return ::chmod(Str(path.View()).CStr(), 0755) == 0; }
inline bool HardLink(const io::Path& existing, const io::Path& link) { return ::link(Str(existing.View()).CStr(), Str(link.View()).CStr()) == 0; }
inline bool RunningAsRoot() { return ::geteuid() == 0; }

#endif

/// The permission bits Stat reports for a file that was given `posix`: the bits themselves on POSIX; on Windows only the owner write bit
/// has a counterpart (the read-only attribute), so 0644 or 0444 (io/BACKEND.md, Stat).
inline u32 ReportedPermissions(u32 posix) {
    if (io::NativePathStyle() == io::PathStyle::Windows) return (posix & 0200) != 0 ? 0644u : 0444u;
    return posix;
}

/// Removes the `re_io_test.*` directories an interrupted run left behind: only those not modified for an hour, so a test program running
/// at the same time keeps its own. Called once per process, with the directory just created as the clock (its modification time is "now").
inline void SweepStaleTempDirs(const io::Path& tmp, const io::Path& fresh) {
    io::FileInfo now_info;
    if (io::Stat(fresh, &now_info).Failed()) return;
    constexpr i64 kStaleNs = i64{3600} * 1000000000;
    Vector<io::DirectoryItem> items;
    if (io::ListDirectory(tmp, false, &items).Failed()) return;
    for (usize i = 0; i < items.Size(); ++i) {
        if (items[i].type != io::FileType::Directory || !items[i].name.View().StartsWith("re_io_test.")) continue;
        const io::Path candidate = tmp.Join(items[i].name.View());
        io::FileInfo info;
        if (candidate == fresh || io::Lstat(candidate, &info).Failed() || now_info.modified_unix_ns - info.modified_unix_ns < kStaleNs) continue;
        MakeWritable(engine_test::MakeText(candidate.View()));
        io::RemoveAll(candidate);
    }
}

/// A directory created for one test and removed (with everything in it) afterwards, through the file layer itself (the helpers above
/// plant shapes; making and removing the test's own directory is what every test exercises anyway).
class TempDir {
public:
    TempDir() {
        io::Path tmp;
        io::TempDirectory(&tmp);
        if (io::CreateTempDirectory(tmp, "re_io_test", &path_).Failed()) {
            std::abort();
        }
        static std::atomic<bool> swept{false}; // the first directory of the process cleans up after earlier, interrupted runs
        if (!swept.exchange(true)) SweepStaleTempDirs(tmp, path_);
    }
    ~TempDir() {
        MakeWritable(Native());
        io::RemoveAll(path_);
    }
    TempDir(const TempDir&) = delete;
    TempDir& operator=(const TempDir&) = delete;

    const io::Path& Path() const { return path_; }
    io::Path operator/(StringView name) const { return path_.Join(name); }
    engine_test::Text Native() const { return engine_test::MakeText(path_.View()); }

private:
    io::Path path_;
};

} // namespace io_test
