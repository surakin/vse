// The Windows file backend (Phase 13 Day 151): the Win32 error table, native path forms, the names the backend refuses, sharing with other
// openers, Stat and Lstat over junctions and links, long paths, and that nothing leaks a handle. Windows only.
#include <catch2/catch_test_macros.hpp>

#include <cstdlib>
#include <cwchar>

#include "file_conformance.h"
#include "io/file.h"
#include "io/file_system.h"
#include "temp_dir.h"
#include "test_env.h"
#include "test_text.h"

#include "os/windows/win32.h"

#include "unity/windows_enable.h"
#include <winioctl.h>
#include "unity/windows_disable.h"


using namespace engine_placeholder;
using io::Error;
using io::OpenMode;
using io::Path;
using io_test::TempDir;
using io_test::WinPath;

namespace {

io::FilePtr MakeWindowsLocal(TempDir& dir, StringView content, bool writable) {
    static int counter = 0;
    const engine_test::Text name = engine_test::Format("conf%d", counter++);
    const Path path = dir / name.View();
    REQUIRE(io::WriteFile(path, content.Data(), content.Length()).Ok());
    io::FilePtr f;
    REQUIRE(io::File::Open(path, writable ? OpenMode::ReadWrite : OpenMode::Read, io::kOpenNone, &f).Ok());
    return f;
}

// Creates a symbolic link without elevation, which Windows allows only in Developer Mode. Returns false (and the reason) otherwise.
bool MakeSymlink(const Path& link, const wchar_t* target, bool directory, DWORD* error) {
    const WinPath l(link);
    const DWORD flags = SYMBOLIC_LINK_FLAG_ALLOW_UNPRIVILEGED_CREATE | (directory ? SYMBOLIC_LINK_FLAG_DIRECTORY : 0);
    if (CreateSymbolicLinkW(l.text, target, flags)) return true;
    *error = GetLastError();
    return false;
}

DWORD HandleCount() {
    DWORD n = 0;
    GetProcessHandleCount(GetCurrentProcess(), &n);
    return n;
}

} // namespace

TEST_CASE("io::Error: the Win32 table", "[io][error][windows]") {
    struct Row {
        DWORD code;
        Error error;
    };
    const Row rows[] = {
        {ERROR_SUCCESS, Error::None},
        {ERROR_FILE_NOT_FOUND, Error::NotFound},
        {ERROR_PATH_NOT_FOUND, Error::NotFound},
        {ERROR_INVALID_DRIVE, Error::NotFound},
        {ERROR_BAD_NETPATH, Error::NotFound},
        {ERROR_ACCESS_DENIED, Error::PermissionDenied},
        {ERROR_PRIVILEGE_NOT_HELD, Error::PermissionDenied},
        {ERROR_SHARING_VIOLATION, Error::Busy},
        {ERROR_LOCK_VIOLATION, Error::Busy},
        {ERROR_DELETE_PENDING, Error::Busy},
        {ERROR_ALREADY_EXISTS, Error::AlreadyExists},
        {ERROR_FILE_EXISTS, Error::AlreadyExists},
        {ERROR_DIRECTORY, Error::NotADirectory},
        {ERROR_DIR_NOT_EMPTY, Error::NotEmpty},
        {ERROR_DISK_FULL, Error::NoSpace},
        {ERROR_HANDLE_DISK_FULL, Error::NoSpace},
        {ERROR_DISK_QUOTA_EXCEEDED, Error::NoSpace},
        {ERROR_FILENAME_EXCED_RANGE, Error::TooLong},
        {ERROR_INSUFFICIENT_BUFFER, Error::TooLong},
        {ERROR_INVALID_NAME, Error::Invalid},
        {ERROR_INVALID_PARAMETER, Error::Invalid},
        {ERROR_INVALID_HANDLE, Error::Invalid},
        {ERROR_NOT_SAME_DEVICE, Error::CrossDevice},
        {ERROR_CANT_RESOLVE_FILENAME, Error::SymlinkLoop},
        {ERROR_WRITE_PROTECT, Error::ReadOnly},
        {ERROR_OPERATION_ABORTED, Error::Interrupted},
        {ERROR_NOT_SUPPORTED, Error::Unsupported},
        {ERROR_TOO_MANY_OPEN_FILES, Error::TooManyOpenFiles},
        {ERROR_NOT_ENOUGH_MEMORY, Error::OutOfMemory},
        {ERROR_HANDLE_EOF, Error::EndOfFile},
        {ERROR_NOT_READY, Error::Io},
        {ERROR_IO_PENDING, Error::Io},
        {ERROR_BROKEN_PIPE, Error::Io},
        {ERROR_CRC, Error::Io},
        {99999, Error::Io},
    };
    for (const Row& r : rows) {
        INFO("Win32 code " << r.code);
        CHECK(io::ErrorFromOsCode(static_cast<i32>(r.code)) == r.error);
    }
    const io::Status s = io::Status::FromOs(ERROR_SHARING_VIOLATION);
    CHECK(s.Is(Error::Busy));
    CHECK(s.os_code == ERROR_SHARING_VIOLATION); // the message keeps the Win32 code
}

TEST_CASE("Path: native Windows forms", "[io][path][windows]") {
    CHECK(io::NativePathStyle() == io::PathStyle::Windows);
    // Both separators on input; one canonical form; ASCII case folded in comparisons, kept in the text.
    CHECK(Path("C:\\a/b\\c") == Path("C:/a/b/c"));
    CHECK(Path("C:/Users/A") == Path("c:/users/a"));
    CHECK(Path("C:/Users/A").View() == StringView("C:/Users/A"));
    // Drive-relative: a drive but no root directory, so not absolute.
    const Path drive_relative("C:foo");
    CHECK(drive_relative.RootName() == StringView("C:"));
    CHECK_FALSE(drive_relative.IsAbsolute());
    CHECK_FALSE(Path("/rooted/no/drive").IsAbsolute());
    // UNC: the server and share are the root name.
    const Path unc("\\\\server\\share\\x\\y.txt");
    CHECK(unc.RootName() == StringView("//server/share"));
    CHECK(unc.IsAbsolute());
    char native[64];
    REQUIRE(unc.ToNative(native, sizeof(native)));
    CHECK(StringView(native) == StringView("\\\\server\\share\\x\\y.txt"));
    // The `\\?\` and `\\.\` prefixes survive as written.
    const Path verbatim("\\\\?\\C:\\very\\long");
    REQUIRE(verbatim.ToNative(native, sizeof(native)));
    CHECK(StringView(native) == StringView("\\\\?\\C:\\very\\long"));
    // A device path is a UNC root, so it reads with its root's separator (`\\.\NUL\`, which Win32 opens as the device too).
    const Path device("\\\\.\\NUL");
    CHECK(device.RootName() == StringView("//./NUL"));
    REQUIRE(device.ToNative(native, sizeof(native)));
    CHECK(StringView(native) == StringView("\\\\.\\NUL\\"));
    // Trailing dots and spaces and reserved names are text to Path; the backend is what refuses them (below).
    CHECK(Path("C:/a/b.").FileName() == StringView("b."));
    CHECK(Path("C:/a/CON").FileName() == StringView("CON"));
}

TEST_CASE("io::File on Windows: reserved names, trailing dots and spaces and streams are refused", "[io][file][windows]") {
    TempDir dir;
    io::FilePtr f;
    const char* const refused[] = {"CON",  "con",     "NUL",     "nul.txt",     "nul .txt",   "PRN",    "AUX.log", "COM1",  "COM0",
                                   "com9.bin", "LPT1", "lpt9.x", "CONIN$", "CONOUT$", "COM\xC2\xB9", "LPT\xC2\xB3", "name.",
                                   "name ",  "name. .", "file:stream"};
    for (const char* name : refused) {
        INFO("name '" << name << "'");
        CHECK(io::File::Open(dir / name, OpenMode::Write, io::kOpenNone, &f).Is(Error::Invalid));
        io::FileInfo info;
        CHECK(io::Stat(dir / name, &info).Is(Error::Invalid));
        // Anywhere in the path, not only the last component.
        CHECK(io::File::Open((dir / name) / "child", OpenMode::Write, io::kOpenNone, &f).Is(Error::Invalid));
    }
    const char* const accepted[] = {"CONSOLE", "COM10", "LPT10", "LPT", "nul_x", "a.b.c", ".hidden", "con-figure"};
    for (const char* name : accepted) {
        INFO("name '" << name << "'");
        REQUIRE(io::File::Open(dir / name, OpenMode::Write, io::kOpenNone, &f).Ok());
        CHECK(f->Close().Ok());
    }
    // Invalid UTF-8 is refused instead of becoming U+FFFD in a name.
    CHECK(io::File::Open(dir / "bad\xFF", OpenMode::Write, io::kOpenNone, &f).Is(Error::Invalid));
    // A device path written with its prefix is the caller's explicit choice and passes through.
    REQUIRE(io::File::Open(Path("\\\\.\\NUL"), OpenMode::Write, io::kOpenNone, &f).Ok());
    CHECK(f->Write("discarded", 9).Ok());
    CHECK((f->Capabilities() & io::kCapSeekable) == 0);
    io::FileInfo info;
    REQUIRE(io::Stat(Path("\\\\.\\NUL"), &info).Ok());
    CHECK(info.type == io::FileType::Other);
}

TEST_CASE("io::File on Windows: directories, missing parents and a file used as a directory", "[io][file][windows]") {
    TempDir dir;
    io::FilePtr f;
    CHECK(io::File::Open(dir.Path(), OpenMode::Read, io::kOpenNone, &f).Is(Error::IsADirectory));
    CHECK(io::File::Open(dir.Path(), OpenMode::Write, io::kOpenNone, &f).Is(Error::IsADirectory));
    CHECK(io::File::Open(dir.Path(), OpenMode::ReadWrite, io::kOpenCreateNew, &f).Is(Error::AlreadyExists)); // as O_EXCL on Linux
    CHECK(io::File::Open(dir / "no/such/dir/f", OpenMode::Write, io::kOpenNone, &f).Is(Error::NotFound));
    REQUIRE(io_test::WriteText(dir / "file", "x"));
    CHECK(io::File::Open(dir / "file/child", OpenMode::Read, io::kOpenNone, &f).Is(Error::NotADirectory));
    CHECK(io::File::Open(dir / "file/a/b", OpenMode::Write, io::kOpenNone, &f).Is(Error::NotADirectory));
    io::FileInfo info;
    CHECK(io::Stat(dir / "file/child", &info).Is(Error::NotADirectory));
    CHECK(io::Stat(dir / "missing/child", &info).Is(Error::NotFound));
}

TEST_CASE("io::File on Windows: open files can be renamed and deleted, and a foreign exclusive open is Busy", "[io][file][share][windows]") {
    TempDir dir;
    REQUIRE(io_test::WriteText(dir / "open.txt", "content"));
    io::FilePtr f;
    REQUIRE(io::File::Open(dir / "open.txt", OpenMode::Read, io::kOpenNone, &f).Ok());
    // Another opener (here the test itself through Win32) renames the file the engine holds open, then deletes it: FILE_SHARE_DELETE.
    const WinPath from(dir / "open.txt");
    const WinPath to(dir / "renamed.txt");
    CHECK(MoveFileExW(from.text, to.text, MOVEFILE_REPLACE_EXISTING) != FALSE);
    char buf[7];
    REQUIRE(io::ReadExact(*f, buf, 7, 0).Ok()); // the open file still reads
    CHECK(StringView(buf, 7) == StringView("content"));
    CHECK(DeleteFileW(to.text) != FALSE);
    CHECK(f->Close().Ok());
    CHECK_FALSE(io::Exists(dir / "renamed.txt"));

    // A program that opens without sharing keeps the engine out, and the engine says Busy (ERROR_SHARING_VIOLATION).
    REQUIRE(io_test::WriteText(dir / "locked.txt", "x"));
    const WinPath locked(dir / "locked.txt");
    const HANDLE exclusive = CreateFileW(locked.text, GENERIC_READ, 0, nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    REQUIRE(exclusive != INVALID_HANDLE_VALUE);
    const io::Status s = io::File::Open(dir / "locked.txt", OpenMode::Read, io::kOpenNone, &f);
    CHECK(s.Is(Error::Busy));
    CHECK(s.os_code == ERROR_SHARING_VIOLATION);
    CloseHandle(exclusive);
    CHECK(io::File::Open(dir / "locked.txt", OpenMode::Read, io::kOpenNone, &f).Ok());
}

TEST_CASE("io: Stat and Lstat on Windows", "[io][fs][windows]") {
    TempDir dir;
    const Path file = dir / "f.txt";
    REQUIRE(io_test::WriteText(file, "hello"));
    io::FileInfo info;
    REQUIRE(io::Stat(file, &info).Ok());
    CHECK(info.type == io::FileType::Regular);
    CHECK(info.size == 5);
    CHECK(info.permissions == 0644);
    CHECK_FALSE(info.read_only);
    // The modification time is in the engine's epoch (Unix nanoseconds): within a minute of now.
    FILETIME now_ft;
    GetSystemTimeAsFileTime(&now_ft);
    const i64 now_ns = ((static_cast<i64>(now_ft.dwHighDateTime) << 32 | now_ft.dwLowDateTime) - 116444736000000000LL) * 100;
    CHECK(info.modified_unix_ns > now_ns - i64{60} * 1000000000);
    CHECK(info.modified_unix_ns <= now_ns + i64{1000000000});
    CHECK(io::IsFile(file));
    CHECK(io::IsDirectory(dir.Path()));

    // The read-only attribute is the permission bits; an ACL that denies writing makes it read-only too.
    const WinPath f(file);
    REQUIRE(SetFileAttributesW(f.text, FILE_ATTRIBUTE_READONLY));
    REQUIRE(io::Stat(file, &info).Ok());
    CHECK(info.permissions == 0444);
    CHECK(info.read_only);
    REQUIRE(SetFileAttributesW(f.text, FILE_ATTRIBUTE_NORMAL));
    REQUIRE(io_test::WriteText(dir / "acl.txt", "x"));
    REQUIRE(io_test::DenyCurrentUser(dir / "acl.txt", FILE_WRITE_DATA));
    REQUIRE(io::Stat(dir / "acl.txt", &info).Ok());
    CHECK(info.permissions == 0644);
    CHECK(info.read_only);

    // Directories.
    REQUIRE(io::Stat(dir.Path(), &info).Ok());
    CHECK(info.type == io::FileType::Directory);
    CHECK(info.permissions == 0755);
    CHECK_FALSE(info.read_only);

    // A junction: Lstat describes the link itself (Other: a junction is not a symbolic link), Stat follows it.
    const WinPath sub(dir / "target");
    REQUIRE(CreateDirectoryW(sub.text, nullptr));
    REQUIRE(io_test::WriteText(dir / "target/inside.txt", "in"));
    REQUIRE(io_test::MakeJunction(dir / "target", dir / "junction"));
    REQUIRE(io::Lstat(dir / "junction", &info).Ok());
    CHECK(info.type == io::FileType::Other);
    REQUIRE(io::Stat(dir / "junction", &info).Ok());
    CHECK(info.type == io::FileType::Directory);
    REQUIRE(io::Stat(dir / "junction/inside.txt", &info).Ok());
    CHECK(info.size == 2);

    // Symbolic links need Developer Mode (or elevation) to create.
    DWORD error = 0;
    if (!MakeSymlink(dir / "link", L"f.txt", false, &error)) {
        WARN("skipped the symbolic link checks: CreateSymbolicLinkW failed with " << error
                                                                                  << " (1314 = no privilege: turn on Developer Mode to run them)");
        return;
    }
    REQUIRE(io::Lstat(dir / "link", &info).Ok());
    CHECK(info.type == io::FileType::Symlink);
    REQUIRE(io::Stat(dir / "link", &info).Ok());
    CHECK(info.type == io::FileType::Regular);
    REQUIRE(MakeSymlink(dir / "dangling", L"gone", false, &error));
    CHECK_FALSE(io::Exists(dir / "dangling"));
    CHECK(io::Lstat(dir / "dangling", &info).Ok());
    REQUIRE(MakeSymlink(dir / "loop", L"loop", false, &error));
    CHECK(io::Stat(dir / "loop", &info).Is(Error::SymlinkLoop));
}

TEST_CASE("io::File on Windows: paths longer than MAX_PATH work with LongPathsEnabled off", "[io][file][windows]") {
    DWORD enabled = 0;
    DWORD size = sizeof(enabled);
    RegGetValueW(HKEY_LOCAL_MACHINE, L"SYSTEM\\CurrentControlSet\\Control\\FileSystem", L"LongPathsEnabled", RRF_RT_REG_DWORD, nullptr,
                 &enabled, &size);
    INFO("LongPathsEnabled = " << enabled); // recorded; the paths below must work either way
    TempDir dir;
    // Twelve levels of 30 characters: more than 360 characters below the temporary directory.
    Path deep = dir.Path();
    engine_test::Text relative = engine_test::MakeText();
    for (int level = 0; level < 12; ++level) {
        const engine_test::Text name = engine_test::Format("level%02d_abcdefghijklmnopqrstu", level);
        deep = deep / name.View();
        if (level > 0) relative.Append("/");
        relative.Append(name.View());
        const WinPath p(deep);
        REQUIRE(CreateDirectoryW(p.text, nullptr));
    }
    const Path file = deep / "deep_file.txt";
    REQUIRE(file.View().Length() > 300);
    io::FilePtr f;
    REQUIRE(io::File::Open(file, OpenMode::Write, io::kOpenNone, &f).Ok());
    REQUIRE(f->Write("deep", 4, 0).Ok());
    REQUIRE(f->Close().Ok());
    Vector<u8> back;
    REQUIRE(io::ReadFile(file, &back).Ok());
    CHECK(StringView(reinterpret_cast<const char*>(back.Data()), back.Size()) == StringView("deep"));
    io::FileInfo info;
    REQUIRE(io::Stat(file, &info).Ok());
    CHECK(info.size == 4);
    // With `..` in it: resolved lexically, as Win32 does for short paths (the prefix alone would not resolve it).
    const Path with_dots = (deep / "..") / (deep.FileName()) / "deep_file.txt";
    REQUIRE(io::Stat(with_dots, &info).Ok());
    CHECK(info.size == 4);

    // A long relative path, resolved against the current directory.
    wchar_t saved[MAX_PATH + 1];
    REQUIRE(GetCurrentDirectoryW(MAX_PATH + 1, saved) > 0);
    const WinPath base(dir.Path());
    REQUIRE(SetCurrentDirectoryW(base.text));
    const Path relative_file = Path(relative.View()) / "deep_file.txt";
    REQUIRE(relative_file.View().Length() > 300);
    const io::Status relative_stat = io::Stat(relative_file, &info);
    SetCurrentDirectoryW(saved);
    CHECK(relative_stat.Ok());

    // Renamed, copied and removed through the layer, and the whole tree listed and removed.
    REQUIRE(io::Rename(file, deep / "renamed_file.txt").Ok());
    CHECK(io::Stat(file, &info).Is(Error::NotFound));
    REQUIRE(io::Copy(deep / "renamed_file.txt", deep / "copied_file.txt", false).Ok());
    REQUIRE(io::Remove(deep / "renamed_file.txt").Ok());
    CHECK_FALSE(io::Exists(deep / "renamed_file.txt"));
    Vector<io::DirectoryItem> items;
    REQUIRE(io::ListDirectory(deep, true, &items).Ok());
    REQUIRE(items.Size() == 1);
    CHECK(items[0].name.View() == StringView("copied_file.txt"));
    REQUIRE(io::CreateDirectories(deep / "more/levels/below").Ok());
    const Path top = dir / "level00_abcdefghijklmnopqrstu";
    REQUIRE(io::RemoveAll(top).Ok());
    CHECK_FALSE(io::Exists(top));
}

TEST_CASE("io: special folders on Windows", "[io][fs][windows]") {
    // The expected values come from the environment Windows sets for every session, independent of the known-folder calls under test.
    auto env = [](const wchar_t* name) {
        wchar_t value[MAX_PATH + 1];
        const DWORD n = GetEnvironmentVariableW(name, value, MAX_PATH + 1);
        char utf8[4 * MAX_PATH];
        const usize m = os::windows::Utf16ToUtf8(value, n, utf8, sizeof(utf8));
        return Path(StringView(utf8, m));
    };
    const Path roaming = env(L"APPDATA");
    const Path local = env(L"LOCALAPPDATA");
    Path p;
    REQUIRE(io::UserConfigDirectory("app", &p).Ok());
    CHECK(p == roaming / "app");
    REQUIRE(io::UserDataDirectory("app", &p).Ok());
    CHECK(p == local / "app");
    REQUIRE(io::UserCacheDirectory("app", &p).Ok());
    CHECK(p == local / "app/cache");
    REQUIRE(io::UserStateDirectory("app", &p).Ok());
    CHECK(p == local / "app/state");
    // Bad application names: the POSIX ones, and what Windows cannot keep.
    const char* const bad[] = {"", ".", "..", "a/b", "a\\b", "a:b", "CON", "nul.txt", "name.", "name "};
    for (const char* name : bad) {
        INFO("app name '" << name << "'");
        CHECK(io::UserConfigDirectory(name, &p).Is(Error::Invalid));
    }
    CHECK(io::UserConfigDirectory(engine_test::Repeated(201, 'a').View(), &p).Is(Error::Invalid));
    // The temporary directory is the user's, as GetTempPathW says.
    wchar_t temp[MAX_PATH + 1];
    const DWORD n = GetTempPathW(MAX_PATH + 1, temp);
    char utf8[4 * MAX_PATH];
    const usize m = os::windows::Utf16ToUtf8(temp, n, utf8, sizeof(utf8));
    REQUIRE(io::TempDirectory(&p).Ok());
    CHECK(p == Path(StringView(utf8, m)));
    // The executable.
    Path exe;
    Path exe_dir;
    REQUIRE(io::ExecutablePath(&exe).Ok());
    REQUIRE(io::ExecutableDirectory(&exe_dir).Ok());
    CHECK(exe.IsAbsolute());
    CHECK(exe.Parent() == exe_dir);
    CHECK(io::IsFile(exe));
    CHECK(exe.Extension() == StringView(".exe"));
    // The crash dump directory: under the local application data, named after ENGINE_APP_NAME (else the executable), private. A name of
    // the test's own, removed afterwards, so the run leaves nothing in the user's profile.
    const engine_test::Text app = engine_test::Format("re_io_test_crashes.%lu", GetCurrentProcessId());
    test_support::SetEnv("ENGINE_APP_NAME", app.CStr());
    Path crashes;
    const io::Status made = io::CrashDumpDirectory(&crashes);
    test_support::UnsetEnv("ENGINE_APP_NAME");
    REQUIRE(made.Ok());
    CHECK(crashes == local / app.View() / "crashes");
    CHECK(io::IsDirectory(crashes));
    CHECK(io_test::AclIsPrivate(crashes));
    REQUIRE(io::RemoveAll(local / app.View()).Ok());
    CHECK_FALSE(io::Exists(local / app.View()));
}

// Day 161: %APPDATA% and %LOCALAPPDATA% come first when they hold an absolute path, as $XDG_* on Linux, so a test points the user folders (and
// the crash dumps) into a directory of its own; a relative value is ignored. The variables are put back afterwards.
TEST_CASE("io: the user folders follow APPDATA and LOCALAPPDATA when they are absolute", "[io][fs][windows]") {
    auto get = [](const char* name) {
        const char* v = std::getenv(name);
        return engine_test::MakeText(StringView(v != nullptr ? v : ""));
    };
    struct Restore {
        engine_test::Text roaming, local;
        ~Restore() {
            test_support::SetEnv("APPDATA", roaming.CStr());
            test_support::SetEnv("LOCALAPPDATA", local.CStr());
        }
    } restore{get("APPDATA"), get("LOCALAPPDATA")};
    Path known_config;
    REQUIRE(io::UserConfigDirectory("app", &known_config).Ok()); // the session's own value, which is the known folder
    TempDir dir;
    test_support::SetEnv("APPDATA", engine_test::MakeText((dir / "roaming").View()).CStr());
    test_support::SetEnv("LOCALAPPDATA", engine_test::MakeText((dir / "local").View()).CStr());
    Path p;
    REQUIRE(io::UserConfigDirectory("app", &p).Ok());
    CHECK(p == dir / "roaming/app");
    REQUIRE(io::UserDataDirectory("app", &p).Ok());
    CHECK(p == dir / "local/app");
    REQUIRE(io::UserCacheDirectory("app", &p).Ok());
    CHECK(p == dir / "local/app/cache");
    REQUIRE(io::UserStateDirectory("app", &p).Ok());
    CHECK(p == dir / "local/app/state");
    test_support::SetEnv("ENGINE_APP_NAME", "crashing_app");
    Path crashes;
    const io::Status made = io::CrashDumpDirectory(&crashes);
    test_support::UnsetEnv("ENGINE_APP_NAME");
    REQUIRE(made.Ok());
    CHECK(crashes == dir / "local/crashing_app/crashes");
    CHECK(io::IsDirectory(crashes));
    // A relative value is not a place: the known folder is used.
    test_support::SetEnv("APPDATA", "relative\\roaming");
    REQUIRE(io::UserConfigDirectory("app", &p).Ok());
    CHECK(p == known_config);
}

TEST_CASE("io: RemoveAll does not follow a junction out of the tree", "[io][fs][windows]") {
    TempDir dir;
    TempDir outside;
    REQUIRE(io_test::WriteText(outside / "keep.txt", "keep"));
    REQUIRE(io::CreateDirectories(dir / "tree/sub").Ok());
    REQUIRE(io_test::MakeJunction(outside.Path(), dir / "tree/sub/junction"));
    REQUIRE(io_test::WriteText(dir / "tree/sub/junction/through.txt", "x")); // written through the junction, lands outside
    REQUIRE(io::Exists(outside / "through.txt"));
    // Read-only files and a file held open inside the tree go too (POSIX semantics).
    REQUIRE(io_test::WriteText(dir / "tree/ro.txt", "r"));
    REQUIRE(io_test::SetReadOnlyFile(dir / "tree/ro.txt"));
    io::FilePtr held;
    REQUIRE(io_test::WriteText(dir / "tree/held.txt", "h"));
    REQUIRE(io::File::Open(dir / "tree/held.txt", OpenMode::Read, io::kOpenNone, &held).Ok());
    REQUIRE(io::RemoveAll(dir / "tree").Ok());
    CHECK_FALSE(io::Exists(dir / "tree"));
    CHECK(io::Exists(outside / "keep.txt"));
    CHECK(io::Exists(outside / "through.txt"));
    // Directory entries report a junction as Other and a walk does not enter it.
    REQUIRE(io::CreateDirectory(dir / "walk").Ok());
    REQUIRE(io_test::MakeJunction(outside.Path(), dir / "walk/j"));
    Vector<io::DirectoryItem> items;
    REQUIRE(io::ListDirectory(dir / "walk", true, &items).Ok());
    REQUIRE(items.Size() == 1);
    CHECK(items[0].type == io::FileType::Other);
}

TEST_CASE("io: Rename replaces open, read-only and empty-directory targets", "[io][fs][windows]") {
    TempDir dir;
    REQUIRE(io_test::WriteText(dir / "a", "new"));
    REQUIRE(io_test::WriteText(dir / "b", "old"));
    io::FilePtr open_b;
    REQUIRE(io::File::Open(dir / "b", OpenMode::Read, io::kOpenNone, &open_b).Ok());
    REQUIRE(io::Rename(dir / "a", dir / "b").Ok()); // MoveFileExW would refuse: the target is open
    char buf[3];
    REQUIRE(io::ReadExact(*open_b, buf, 3, 0).Ok());
    CHECK(StringView(buf, 3) == StringView("old")); // the open file is the old one
    CHECK(io_test::ReadText(dir / "b") == "new");
    REQUIRE(io_test::WriteText(dir / "c", "c"));
    REQUIRE(io_test::SetReadOnlyFile(dir / "b"));
    REQUIRE(io::Rename(dir / "c", dir / "b").Ok()); // over a read-only file, as rename(2)
    CHECK(io_test::ReadText(dir / "b") == "c");
    REQUIRE(io::CreateDirectory(dir / "d1").Ok());
    REQUIRE(io::CreateDirectory(dir / "d2").Ok());
    REQUIRE(io::Rename(dir / "d1", dir / "d2").Ok()); // a directory over an empty one
    CHECK_FALSE(io::Exists(dir / "d1"));
    REQUIRE(io_test::WriteText(dir / "f", "f"));
    CHECK(io::Rename(dir / "d2", dir / "f").Is(Error::NotADirectory));
    // SyncDirectory flushes a directory handle (opened for writing).
    CHECK(io::SyncDirectory(dir.Path()).Ok());
    CHECK(io::SyncDirectory(dir / "f").Is(Error::NotADirectory));
    CHECK(io::SyncDirectory(dir / "missing").Is(Error::NotFound));
}

TEST_CASE("io::File on Windows: the conformance suites and the error paths leak no handle", "[io][file][windows]") {
    TempDir dir;
    // Warm up anything created once per process (the ACL check's token, the log).
    io::FileInfo info;
    REQUIRE(io::Stat(dir.Path(), &info).Ok());
    const DWORD before = HandleCount();
    io_test::RunReadConformance([&](StringView c, bool w) { return MakeWindowsLocal(dir, c, w); });
    io_test::RunWriteConformance([&](StringView c, bool w) { return MakeWindowsLocal(dir, c, w); });
    io::FilePtr f;
    for (int i = 0; i < 20; ++i) {
        CHECK(io::File::Open(dir / "missing", OpenMode::Read, io::kOpenNone, &f).Is(Error::NotFound));
        CHECK(io::File::Open(dir.Path(), OpenMode::Read, io::kOpenNone, &f).Is(Error::IsADirectory));
        CHECK(io::File::Open(dir / "CON", OpenMode::Read, io::kOpenNone, &f).Is(Error::Invalid));
        CHECK(io::Stat(dir / "missing/x", &info).Is(Error::NotFound));
        CHECK(io::Stat(dir.Path(), &info).Ok());
        CHECK(io::Lstat(dir.Path(), &info).Ok());
    }
    f.Reset();
    CHECK(HandleCount() == before);
}

TEST_CASE("io::File on Windows: Write and kOpenTruncate replace hidden and system files and keep their attributes", "[io][file][windows]") {
    TempDir dir;
    const Path path = dir / "hidden.txt";
    REQUIRE(io_test::WriteText(path, "old content"));
    const WinPath p(path);
    REQUIRE(SetFileAttributesW(p.text, FILE_ATTRIBUTE_HIDDEN | FILE_ATTRIBUTE_SYSTEM));
    // O_TRUNC on Linux keeps the file and its mode; CREATE_ALWAYS would refuse a hidden file whose attributes the call does not repeat.
    REQUIRE(io::WriteFile(path, "new", 3).Ok());
    CHECK(io_test::ReadText(path) == "new");
    io::FilePtr f;
    REQUIRE(io::File::Open(path, OpenMode::ReadWrite, io::kOpenCreate | io::kOpenTruncate, &f).Ok());
    u64 size = 1;
    REQUIRE(f->Size(&size).Ok());
    CHECK(size == 0);
    f.Reset();
    REQUIRE(io::File::Open(path, OpenMode::Append, io::kOpenTruncate, &f).Ok());
    f.Reset();
    const DWORD attributes = GetFileAttributesW(p.text);
    CHECK((attributes & FILE_ATTRIBUTE_HIDDEN) != 0);
    CHECK((attributes & FILE_ATTRIBUTE_SYSTEM) != 0);
}

TEST_CASE("io_test::TempDir removes a tree deeper than MAX_PATH", "[io][windows]") {
    Path root;
    {
        TempDir dir;
        root = dir.Path();
        Path deep = dir.Path();
        for (int level = 0; level < 12; ++level) {
            deep = deep / engine_test::Format("level%02d_abcdefghijklmnopqrstu", level).View();
            REQUIRE(CreateDirectoryW(WinPath(deep).text, nullptr));
        }
        REQUIRE(io_test::WriteText(deep / "f.txt", "x"));
    }
    CHECK(GetFileAttributesW(WinPath(root).text) == INVALID_FILE_ATTRIBUTES);
}

TEST_CASE("io: Stat describes a file another program holds without sharing its attributes", "[io][fs][windows]") {
    // The paging file is opened by the system with no sharing at all: even FILE_READ_ATTRIBUTES is refused (ERROR_SHARING_VIOLATION).
    wchar_t windows_dir[MAX_PATH + 1];
    REQUIRE(GetWindowsDirectoryW(windows_dir, MAX_PATH + 1) > 0);
    const Path pagefile(engine_test::Format("%c:/pagefile.sys", static_cast<char>(windows_dir[0])).View());
    const HANDLE probe = CreateFileW(WinPath(pagefile).text, FILE_READ_ATTRIBUTES, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
                                     nullptr, OPEN_EXISTING, FILE_FLAG_BACKUP_SEMANTICS, nullptr);
    const DWORD error = probe == INVALID_HANDLE_VALUE ? GetLastError() : 0;
    if (probe != INVALID_HANDLE_VALUE) CloseHandle(probe);
    if (error != ERROR_SHARING_VIOLATION) {
        WARN("skipped: " << pagefile.View().Data() << " is not held exclusively here (open result " << error << ")");
        return;
    }
    io::FileInfo info;
    REQUIRE(io::Stat(pagefile, &info).Ok());
    CHECK(info.type == io::FileType::Regular);
    CHECK(info.size > 0);
    CHECK(io::Exists(pagefile));
    CHECK(io::IsFile(pagefile));
}

TEST_CASE("io: RemoveAll reports a subdirectory it cannot list instead of NotEmpty", "[io][fs][windows]") {
    TempDir dir;
    REQUIRE(io::CreateDirectories(dir / "tree/locked").Ok());
    REQUIRE(io_test::WriteText(dir / "tree/locked/f.txt", "x"));
    REQUIRE(io_test::DenyListing(dir / "tree/locked"));
    CHECK(io::RemoveAll(dir / "tree").Is(Error::PermissionDenied)); // as opening a 0000 directory on Linux
    REQUIRE(io_test::AllowAll(dir / "tree/locked"));
}
