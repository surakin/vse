#include <catch2/catch_test_macros.hpp>

#include <cerrno>
#include <cstdlib>
#include <cstring>
#include <fcntl.h>
#include <string>
#include <sys/stat.h>
#include <unistd.h>

#include "io/file_system.h"
#include "temp_dir.h"

using namespace engine_placeholder;
using io::Error;
using io::Path;
using io_test::Str;
using io_test::TempDir;

namespace {

struct EnvGuard { // restores an environment variable at scope exit
    std::string name;
    std::string old;
    bool had;
    explicit EnvGuard(const char* n) : name(n) {
        const char* v = std::getenv(n);
        had = v != nullptr;
        if (had) old = v;
    }
    ~EnvGuard() {
        if (had) ::setenv(name.c_str(), old.c_str(), 1);
        else ::unsetenv(name.c_str());
    }
};

} // namespace

TEST_CASE("io::Error: the errno table and names", "[io][error]") {
    struct Row { int code; Error error; };
    const Row rows[] = {
        {0, Error::None}, {ENOENT, Error::NotFound}, {EACCES, Error::PermissionDenied}, {EPERM, Error::PermissionDenied},
        {EEXIST, Error::AlreadyExists}, {ENOTDIR, Error::NotADirectory}, {EISDIR, Error::IsADirectory},
        {ENOTEMPTY, Error::NotEmpty}, {ENOSPC, Error::NoSpace}, {EDQUOT, Error::NoSpace}, {EFBIG, Error::NoSpace},
        {ENAMETOOLONG, Error::TooLong}, {EINTR, Error::Interrupted}, {ENOSYS, Error::Unsupported},
        {EOPNOTSUPP, Error::Unsupported}, {EINVAL, Error::Invalid}, {EBADF, Error::Invalid}, {EXDEV, Error::CrossDevice},
        {ELOOP, Error::SymlinkLoop}, {ERANGE, Error::TooLong}, {EROFS, Error::ReadOnly}, {EBUSY, Error::Busy}, {ETXTBSY, Error::Busy},
        {EMFILE, Error::TooManyOpenFiles}, {ENFILE, Error::TooManyOpenFiles}, {ENOMEM, Error::OutOfMemory},
        {EIO, Error::Io}, {ENXIO, Error::Io}, {EFAULT, Error::Io}, {99999, Error::Io},
    };
    for (const Row& r : rows) {
        INFO("errno " << r.code);
        CHECK(io::ErrorFromOsCode(r.code) == r.error);
    }
    CHECK(std::string(io::ErrorName(Error::NotFound)) == "not_found");
    CHECK(std::string(io::ErrorName(Error::Io)) == "io");
    const io::Status s = io::Status::FromOs(ENOENT);
    CHECK(s.Failed());
    CHECK(s.Is(Error::NotFound));
    CHECK(s.os_code == ENOENT);
    CHECK(io::Status::Success().Ok());
}

TEST_CASE("io: Stat, Lstat and the existence questions", "[io][fs]") {
    TempDir dir;
    const Path file = dir / "f.txt";
    REQUIRE(io_test::WriteText(file, "hello"));
    io::FileInfo info;
    REQUIRE(io::Stat(file, &info).Ok());
    CHECK(info.type == io::FileType::Regular);
    CHECK(info.size == 5);
    CHECK(info.modified_unix_ns > 0);
    CHECK((info.permissions & 0600) == 0600);
    CHECK_FALSE(info.read_only);
    CHECK(io::IsFile(file));
    CHECK_FALSE(io::IsDirectory(file));
    CHECK(io::IsDirectory(dir.Path()));
    CHECK(io::Exists(file));

    const Path missing = dir / "nope";
    CHECK(io::Stat(missing, &info).Is(Error::NotFound));
    CHECK_FALSE(io::Exists(missing));
    // A file used as a directory.
    CHECK(io::Stat(file / "child", &info).Is(Error::NotADirectory));

    // Symbolic links: Stat follows, Lstat describes the link; a dangling link does not exist.
    const Path link = dir / "link";
    REQUIRE(::symlink("f.txt", io_test::Str(link.View()).c_str()) == 0);
    REQUIRE(io::Lstat(link, &info).Ok());
    CHECK(info.type == io::FileType::Symlink);
    REQUIRE(io::Stat(link, &info).Ok());
    CHECK(info.type == io::FileType::Regular);
    const Path dangling = dir / "dangling";
    REQUIRE(::symlink("gone", io_test::Str(dangling.View()).c_str()) == 0);
    CHECK_FALSE(io::Exists(dangling));
    CHECK(io::Lstat(dangling, &info).Ok());

    // A symlink loop.
    const Path loop = dir / "loop";
    REQUIRE(::symlink("loop", io_test::Str(loop.View()).c_str()) == 0);
    CHECK(io::Stat(loop, &info).Is(Error::SymlinkLoop));
}

TEST_CASE("io: read-only files report it", "[io][fs]") {
    TempDir dir;
    const Path file = dir / "ro.txt";
    REQUIRE(io_test::WriteText(file, "x"));
    REQUIRE(::chmod(io_test::Str(file.View()).c_str(), 0444) == 0);
    io::FileInfo info;
    REQUIRE(io::Stat(file, &info).Ok());
    if (::geteuid() != 0) { // root can write anything
        CHECK(info.read_only);
    }
    CHECK(info.permissions == 0444);
}

TEST_CASE("io: CreateDirectory, CreateDirectories and Remove", "[io][fs]") {
    TempDir dir;
    const Path d = dir / "a";
    CHECK(io::CreateDirectory(d).Ok());
    CHECK(io::CreateDirectory(d).Is(Error::AlreadyExists));
    CHECK(io::CreateDirectory(dir / "x/y").Is(Error::NotFound));
    CHECK(io::CreateDirectories(dir / "x/y/z").Ok());
    CHECK(io::IsDirectory(dir / "x/y/z"));
    CHECK(io::CreateDirectories(dir / "x/y/z").Ok()); // existing is success
    REQUIRE(io_test::WriteText(dir / "file", "x"));
    CHECK(io::CreateDirectories(dir / "file").Is(Error::NotADirectory));
    CHECK(io::CreateDirectories(dir / "file/sub").Is(Error::NotADirectory));

    CHECK(io::Remove(dir / "x/y/z").Ok());
    CHECK(io::Remove(dir / "x").Is(Error::NotEmpty));
    CHECK(io::Remove(dir / "file").Ok());
    CHECK(io::Remove(dir / "file").Is(Error::NotFound));

    // Names with spaces and Unicode.
    const Path odd = dir / "dir with space/日本語 ünï";
    CHECK(io::CreateDirectories(odd).Ok());
    CHECK(io::IsDirectory(odd));

    // Over-long names.
    const std::string long_name(300, 'n');
    CHECK(io::CreateDirectory(dir / StringView(long_name.data(), long_name.size())).Is(Error::TooLong));
}

TEST_CASE("io: read-only directories refuse changes", "[io][fs]") {
    if (::geteuid() == 0) {
        SUCCEED("root ignores directory permissions");
        return;
    }
    TempDir dir;
    const Path d = dir / "locked";
    REQUIRE(io::CreateDirectory(d).Ok());
    REQUIRE(::chmod(io_test::Str(d.View()).c_str(), 0555) == 0);
    CHECK(io::CreateDirectory(d / "child").Is(Error::PermissionDenied));
    Path unused;
    CHECK(io::CreateTempFile(d, "t", &unused).Is(Error::PermissionDenied));
}

TEST_CASE("io: RemoveAll removes trees, never follows links and refuses dangerous arguments", "[io][fs]") {
    TempDir dir;
    TempDir other; // a tree that must survive
    REQUIRE(io_test::WriteText(other / "keep.txt", "keep"));
    REQUIRE(io::CreateDirectories(dir / "t/a/b").Ok());
    REQUIRE(io_test::WriteText(dir / "t/a/b/f", "1"));
    REQUIRE(io_test::WriteText(dir / "t/a/g", "2"));
    REQUIRE(::symlink(other.Native().c_str(), io_test::Str((dir / "t/a/link").View()).c_str()) == 0);
    CHECK(io::RemoveAll(dir / "t").Ok());
    CHECK_FALSE(io::Exists(dir / "t"));
    CHECK(io::Exists(other / "keep.txt")); // the link was removed, not followed
    CHECK(io::RemoveAll(dir / "t").Is(Error::NotFound));

    // A file and a link are removed too.
    REQUIRE(io_test::WriteText(dir / "one", "1"));
    CHECK(io::RemoveAll(dir / "one").Ok());

    // Dangerous arguments.
    CHECK(io::RemoveAll(Path()).Is(Error::Invalid));
    CHECK(io::RemoveAll(Path("/")).Is(Error::Invalid));
    CHECK(io::RemoveAll(Path("/home")).Is(Error::Invalid));
    CHECK(io::RemoveAll(Path("/tmp/..")).Is(Error::Invalid));
    // `..` after a link means something else to the kernel than to a lexical Normalize: refused, nothing deleted.
    REQUIRE(io::CreateDirectories(dir / "real/victim").Ok());
    REQUIRE(::symlink(io_test::Str((dir / "real/victim").View()).c_str(), io_test::Str((dir / "viaLink").View()).c_str()) == 0);
    CHECK(io::RemoveAll(dir / "viaLink/../real").Is(Error::Invalid));
    CHECK(io::IsDirectory(dir / "real/victim"));
    CHECK(io::RemoveAll(Path("/usr/../etc/..")).Is(Error::Invalid));

    // A tree deeper than the limit is refused rather than recursing without bound.
    Path deep = dir / "deep";
    for (int i = 0; i < 140; ++i) deep = deep.Join("d");
    CHECK(io::CreateDirectories(deep).Ok());
    CHECK(io::RemoveAll(dir / "deep").Is(Error::TooLong));
    // Make it shallow enough for the fixture's own cleanup: the refused RemoveAll deleted nothing, so remove the deepest 20 levels from the
    // bottom up.
    Path leaf = dir / "deep";
    for (int i = 0; i < 140; ++i) leaf = leaf.Join("d");
    for (int level = 0; level < 20; ++level) {
        REQUIRE(io::Remove(leaf).Ok());
        leaf = leaf.Parent();
    }
}

TEST_CASE("io: Rename and Copy", "[io][fs]") {
    TempDir dir;
    REQUIRE(io_test::WriteText(dir / "a", "AAA"));
    REQUIRE(io_test::WriteText(dir / "b", "BBB"));
    CHECK(io::Rename(dir / "a", dir / "b").Ok()); // replaces
    CHECK(io_test::ReadText(dir / "b") == "AAA");
    CHECK_FALSE(io::Exists(dir / "a"));
    CHECK(io::Rename(dir / "a", dir / "c").Is(Error::NotFound));
    REQUIRE(io::CreateDirectory(dir / "d").Ok());
    CHECK(io::Rename(dir / "b", dir / "d").Is(Error::IsADirectory));
    REQUIRE(io_test::WriteText(dir / "d/x", "1"));
    REQUIRE(io::CreateDirectory(dir / "e").Ok());
    CHECK(io::Rename(dir / "e", dir / "d").Is(Error::NotEmpty));

    CHECK(io::Copy(dir / "b", dir / "copy", false).Ok());
    CHECK(io_test::ReadText(dir / "copy") == "AAA");
    CHECK(io::Copy(dir / "b", dir / "copy", false).Is(Error::AlreadyExists));
    REQUIRE(io_test::WriteText(dir / "b", "new longer content"));
    CHECK(io::Copy(dir / "b", dir / "copy", true).Ok());
    CHECK(io_test::ReadText(dir / "copy") == "new longer content");
    CHECK(io::Copy(dir / "d", dir / "dc", false).Is(Error::IsADirectory));
    // Copying a file onto itself (also through a hard link) is refused and leaves it intact.
    CHECK(io::Copy(dir / "b", dir / "b", true).Is(Error::Invalid));
    REQUIRE(::link(io_test::Str((dir / "b").View()).c_str(), io_test::Str((dir / "hard").View()).c_str()) == 0);
    CHECK(io::Copy(dir / "b", dir / "hard", true).Is(Error::Invalid));
    CHECK(io_test::ReadText(dir / "b") == "new longer content");
    CHECK(io::Copy(dir / "missing", dir / "mc", false).Is(Error::NotFound));
    // The permission bits travel with the copy.
    REQUIRE(::chmod(io_test::Str((dir / "b").View()).c_str(), 0640) == 0);
    REQUIRE(io::Copy(dir / "b", dir / "perm", false).Ok());
    io::FileInfo info;
    REQUIRE(io::Stat(dir / "perm", &info).Ok());
    CHECK(info.permissions == 0640);
}

TEST_CASE("io: CreatePrivateDirectory", "[io][fs]") {
    TempDir dir;
    const Path p = dir / "parent/private";
    REQUIRE(io::CreatePrivateDirectory(p).Ok());
    io::FileInfo info;
    REQUIRE(io::Stat(p, &info).Ok());
    CHECK(info.permissions == 0700);
    // An existing directory with group/other access is tightened.
    const Path open_dir = dir / "open";
    REQUIRE(io::CreateDirectory(open_dir).Ok());
    REQUIRE(::chmod(io_test::Str(open_dir.View()).c_str(), 0755) == 0);
    REQUIRE(io::CreatePrivateDirectory(open_dir).Ok());
    REQUIRE(io::Stat(open_dir, &info).Ok());
    CHECK(info.permissions == 0700);
    // A symbolic link, even to a directory, is refused; a file is not a directory.
    const Path link = dir / "link";
    REQUIRE(::symlink(io_test::Str(open_dir.View()).c_str(), io_test::Str(link.View()).c_str()) == 0);
    CHECK(io::CreatePrivateDirectory(link).Is(Error::SymlinkLoop));
    REQUIRE(io_test::WriteText(dir / "file", "x"));
    CHECK(io::CreatePrivateDirectory(dir / "file").Failed());
}

TEST_CASE("io: temporary files and directories", "[io][fs]") {
    TempDir dir;
    Path a;
    Path b;
    REQUIRE(io::CreateTempFile(dir.Path(), "cache.tmp", &a).Ok());
    REQUIRE(io::CreateTempFile(dir.Path(), "cache.tmp", &b).Ok());
    CHECK(a != b);
    CHECK(Str(a.FileName()).rfind("cache.tmp.", 0) == 0);
    io::FileInfo info;
    REQUIRE(io::Stat(a, &info).Ok());
    CHECK(info.type == io::FileType::Regular);
    CHECK(info.size == 0);
    CHECK(info.permissions == 0600);
    Path d;
    REQUIRE(io::CreateTempDirectory(dir.Path(), "work", &d).Ok());
    CHECK(io::IsDirectory(d));
    CHECK(io::CreateTempFile(dir.Path(), "a/b", &a).Is(Error::Invalid));
    CHECK(io::CreateTempFile(dir.Path(), "", &a).Is(Error::Invalid));
    CHECK(io::CreateTempFile(dir.Path(), StringView("a\0b", 3), &a).Is(Error::Invalid));
    CHECK(io::CreateTempFile(dir / "missing", "x", &a).Is(Error::NotFound));
}

TEST_CASE("io: current directory, Absolute, Canonical and ReadLink", "[io][fs]") {
    TempDir dir;
    Path cwd;
    REQUIRE(io::CurrentDirectory(&cwd).Ok());
    CHECK(cwd.IsAbsolute());
    Path abs;
    REQUIRE(io::Absolute(Path("a/b"), &abs).Ok());
    CHECK(abs == cwd.Join("a/b"));
    REQUIRE(io::Absolute(Path("/x/y"), &abs).Ok());
    CHECK(abs == Path("/x/y"));

    REQUIRE(io::CreateDirectories(dir / "real/sub").Ok());
    REQUIRE(::symlink("real", io_test::Str((dir / "alias").View()).c_str()) == 0);
    Path canon;
    REQUIRE(io::Canonical(dir / "alias/sub/../sub", &canon).Ok());
    Path expect;
    REQUIRE(io::Canonical(dir / "real/sub", &expect).Ok());
    CHECK(canon == expect);
    CHECK(io::Canonical(dir / "nope", &canon).Is(Error::NotFound));

    Path target;
    REQUIRE(io::ReadLink(dir / "alias", &target).Ok());
    CHECK(Str(target.View()) == "real");
    CHECK(io::ReadLink(dir / "real", &target).Is(Error::Invalid));
    CHECK(io::ReadLink(dir / "nope", &target).Is(Error::NotFound));

    // SetCurrentDirectory (process-global: restored at the end).
    REQUIRE(io::SetCurrentDirectory(dir / "real").Ok());
    Path now;
    REQUIRE(io::CurrentDirectory(&now).Ok());
    CHECK(now == expect.Parent());
    CHECK(io::SetCurrentDirectory(dir / "nope").Is(Error::NotFound));
    REQUIRE(io::SetCurrentDirectory(cwd).Ok());
}

TEST_CASE("io: special folders", "[io][fs]") {
    EnvGuard home("HOME"), cfg("XDG_CONFIG_HOME"), cache("XDG_CACHE_HOME"), data("XDG_DATA_HOME"), state("XDG_STATE_HOME"), tmp("TMPDIR");
    ::setenv("HOME", "/home/test", 1);
    ::unsetenv("XDG_CONFIG_HOME");
    ::unsetenv("XDG_CACHE_HOME");
    ::unsetenv("XDG_DATA_HOME");
    ::unsetenv("XDG_STATE_HOME");
    Path p;
    REQUIRE(io::UserConfigDirectory("app", &p).Ok());
    CHECK(Str(p.View()) == "/home/test/.config/app");
    REQUIRE(io::UserCacheDirectory("app", &p).Ok());
    CHECK(Str(p.View()) == "/home/test/.cache/app");
    REQUIRE(io::UserDataDirectory("app", &p).Ok());
    CHECK(Str(p.View()) == "/home/test/.local/share/app");
    REQUIRE(io::UserStateDirectory("app", &p).Ok());
    CHECK(Str(p.View()) == "/home/test/.local/state/app");

    ::setenv("XDG_CONFIG_HOME", "/xdg/config", 1);
    REQUIRE(io::UserConfigDirectory("app", &p).Ok());
    CHECK(Str(p.View()) == "/xdg/config/app");
    ::setenv("XDG_CONFIG_HOME", "relative/config", 1); // relative values are ignored
    REQUIRE(io::UserConfigDirectory("app", &p).Ok());
    CHECK(Str(p.View()) == "/home/test/.config/app");
    ::setenv("HOME", "relative/home", 1); // HOME must be absolute too: falls back to the password database
    ::unsetenv("XDG_CONFIG_HOME");
    const io::Status s = io::UserConfigDirectory("app", &p);
    if (s.Ok()) {
        CHECK(p.IsAbsolute());
    } else {
        CHECK(s.Is(Error::NotFound));
    }
    ::unsetenv("HOME");
    const io::Status s2 = io::UserCacheDirectory("app", &p);
    CHECK((s2.Ok() || s2.Is(Error::NotFound)));

    // Bad application names.
    CHECK(io::UserConfigDirectory("", &p).Is(Error::Invalid));
    CHECK(io::UserConfigDirectory("..", &p).Is(Error::Invalid));
    CHECK(io::UserConfigDirectory("a/b", &p).Is(Error::Invalid));
    const std::string long_name(201, 'a');
    CHECK(io::UserConfigDirectory(StringView(long_name.data(), long_name.size()), &p).Is(Error::Invalid));

    ::setenv("TMPDIR", "/var/tmp", 1);
    REQUIRE(io::TempDirectory(&p).Ok());
    CHECK(Str(p.View()) == "/var/tmp");
    ::setenv("TMPDIR", "relative", 1);
    REQUIRE(io::TempDirectory(&p).Ok());
    CHECK(Str(p.View()) == "/tmp");

    // Nothing is created.
    ::setenv("HOME", "/nonexistent-home-for-test", 1);
    REQUIRE(io::UserDataDirectory("app", &p).Ok());
    CHECK_FALSE(io::Exists(p));

    Path exe;
    Path exe_dir;
    REQUIRE(io::ExecutablePath(&exe).Ok());
    REQUIRE(io::ExecutableDirectory(&exe_dir).Ok());
    CHECK(exe.IsAbsolute());
    CHECK(exe.Parent() == exe_dir);
    CHECK(io::IsFile(exe));
}
