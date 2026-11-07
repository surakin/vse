#pragma once

#include <atomic>
#include <cstdlib>
#include <cstring>

#if defined(_WIN32)
#include <cwchar>

#include "os/windows/utf16.h"
#include "os/windows/win32.h"

#include "unity/windows_enable.h"
#include <aclapi.h>
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

#if defined(_WIN32)

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
    PACL acl = nullptr;
    if (SetEntriesInAclW(2, entries, nullptr, &acl) != ERROR_SUCCESS) return false;
    WinPath p(path);
    const DWORD r = SetNamedSecurityInfoW(p.text, SE_FILE_OBJECT, DACL_SECURITY_INFORMATION | PROTECTED_DACL_SECURITY_INFORMATION, nullptr,
                                          nullptr, acl, nullptr);
    LocalFree(acl);
    return r == ERROR_SUCCESS;
}

/// Calls `visit(wide path, attributes)` for every entry of the directory `dir` (not `.` and `..`).
template <typename Visit>
inline void ForEachEntry(const wchar_t* dir, Visit&& visit) {
    wchar_t pattern[io::kMaxPathBytes + 32];
    const usize n = std::wcslen(dir);
    if (n + 3 >= sizeof(pattern) / sizeof(pattern[0])) return;
    std::wmemcpy(pattern, dir, n);
    std::wmemcpy(pattern + n, L"\\*", 3);
    WIN32_FIND_DATAW data;
    const HANDLE find = FindFirstFileExW(pattern, FindExInfoBasic, &data, FindExSearchNameMatch, nullptr, 0);
    if (find == INVALID_HANDLE_VALUE) return;
    do {
        if (std::wcscmp(data.cFileName, L".") == 0 || std::wcscmp(data.cFileName, L"..") == 0) continue;
        wchar_t child[io::kMaxPathBytes + 32];
        const usize m = std::wcslen(data.cFileName);
        if (n + 1 + m + 1 > sizeof(child) / sizeof(child[0])) continue;
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

/// Removes a tree; a link or junction is removed as itself, never followed.
inline void RemoveTree(const wchar_t* path, int depth = 0) {
    const DWORD attributes = GetFileAttributesW(path);
    if (attributes == INVALID_FILE_ATTRIBUTES) return;
    const bool directory = (attributes & FILE_ATTRIBUTE_DIRECTORY) != 0;
    if (directory && (attributes & FILE_ATTRIBUTE_REPARSE_POINT) == 0 && depth < 128) {
        ForEachEntry(path, [&](const wchar_t* child, DWORD) { RemoveTree(child, depth + 1); });
    }
    if (directory) {
        RemoveDirectoryW(path);
    } else {
        SetFileAttributesW(path, FILE_ATTRIBUTE_NORMAL);
        DeleteFileW(path);
    }
}

/// A directory created for one test and removed (with everything in it) afterwards. On Windows it is made with Win32 until the file
/// layer's own CreateTempDirectory and RemoveAll exist there (Phase 13 Day 152).
class TempDir {
public:
    TempDir() {
        wchar_t base[MAX_PATH + 1];
        const DWORD n = GetTempPathW(MAX_PATH + 1, base);
        if (n == 0 || n > MAX_PATH) std::abort();
        static std::atomic<u32> counter{0};
        for (int attempt = 0; attempt < 100; ++attempt) {
            wchar_t name[MAX_PATH + 64];
            swprintf(name, sizeof(name) / sizeof(name[0]), L"%lsre_io_test.%lu.%lu.%llu", base, GetCurrentProcessId(),
                     static_cast<unsigned long>(counter.fetch_add(1)), static_cast<unsigned long long>(GetTickCount64()));
            if (CreateDirectoryW(name, nullptr)) {
                char utf8[io::kMaxPathBytes + 1];
                const usize length = os::windows::Utf16ToUtf8(name, std::wcslen(name), utf8, sizeof(utf8));
                if (length >= sizeof(utf8) || !io::Path::Parse(StringView(utf8, length), io::PathStyle::Windows, &path_)) std::abort();
                return;
            }
            if (GetLastError() != ERROR_ALREADY_EXISTS) break;
        }
        std::abort();
    }
    ~TempDir() {
        const WinPath p = WinPath::Extended(path_); // the tree can be deeper than MAX_PATH (the long-path test)
        MakeWritable(p.text);
        RemoveTree(p.text);
    }
    TempDir(const TempDir&) = delete;
    TempDir& operator=(const TempDir&) = delete;

    const io::Path& Path() const { return path_; }
    io::Path operator/(StringView name) const { return path_.Join(name); }
    engine_test::Text Native() const { return engine_test::MakeText(path_.View()); }

private:
    io::Path path_;
};

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

/// A directory created for one test and removed (with everything in it) afterwards.
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

#endif

} // namespace io_test
