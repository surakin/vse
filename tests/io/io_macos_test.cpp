// The file layer on macOS (Phase 26 Day 287): what differs from Linux (the errno table's Darwin rows are in file_system_test.cpp's shared
// table): normalization and case on this volume (the layer reports what the file system does and never folds),
// F_FULLFSYNC's cost for a settings-sized file, the executable path and the standard directories under ~/Library.
#include <catch2/catch_test_macros.hpp>

#include <fcntl.h>
#include <pwd.h>
#include <sys/stat.h>
#include <sys/time.h>
#include <unistd.h>

#include "io/error.h"
#include "io/file.h"
#include "io/file_system.h"
#include "os/console.h"
#include "os/last_error.h"
#include "os/os.h"
#include "mem/arena_allocator.h"
#include "os/signal.h"
#include "sync/thread.h"
#include "temp_dir.h"
#include "test_text.h"

using namespace engine_placeholder;
using io::Error;
using io::Path;
using io_test::Str;
using io_test::EnvGuard;
using io_test::TempDir;

TEST_CASE("io on macOS: a name written in NFC, looked up in NFD, is what the volume says", "[io][fs][macos]") {
    TempDir dir;
    const Path nfc = dir / "caf\xC3\xA9";  // é as one code point (U+00E9)
    const Path nfd = dir / "cafe\xCC\x81"; // e + combining acute (U+0301)
    REQUIRE(io::WriteFile(nfc, "x", 1).Ok());
    // The layer must not normalize: its answer for the NFD name is the file system's own. APFS is normalization-insensitive and keeps the
    // form it was given; HFS+ stores decomposed names. Both are allowed; record which this volume is.
    const bool raw_found = ::access(Str(nfd.View()).CStr(), F_OK) == 0;
    io::FileInfo info;
    CHECK(io::Stat(nfd, &info).Ok() == raw_found);
    os::Print("NFD lookup of an NFC name on this volume: %s\n", raw_found ? "found (normalization-insensitive)" : "not found");
    // The name lists in the form it was written (APFS keeps it), bytes unchanged by the layer.
    io::Directory listing;
    REQUIRE(io::Directory::Open(dir.Path(), &listing).Ok());
    io::DirectoryEntry entry;
    bool found = false;
    REQUIRE(listing.Next(&entry, &found).Ok());
    REQUIRE(found);
    const bool listed_as_nfc = entry.name == StringView("caf\xC3\xA9");
    os::Print("the name lists as %s\n", listed_as_nfc ? "NFC (as written)" : "another form");
    CHECK((listed_as_nfc || entry.name == StringView("cafe\xCC\x81")));
}

TEST_CASE("io on macOS: two names differing only in case follow the volume's rule", "[io][fs][macos]") {
    TempDir dir;
    const long case_sensitive = ::pathconf(Str(dir.Path().View()).CStr(), _PC_CASE_SENSITIVE);
    REQUIRE(case_sensitive >= 0);
    os::Print("the temporary volume is case-%s\n", case_sensitive == 1 ? "sensitive" : "insensitive");
    REQUIRE(io::WriteFile(dir / "Case.txt", "upper", 5).Ok());
    const Path lower = dir / "case.txt";
    if (case_sensitive == 1) {
        CHECK_FALSE(io::Exists(lower));
        REQUIRE(io::WriteFile(lower, "lower", 5).Ok());
        CHECK(io_test::ReadText(dir / "Case.txt").View() == StringView("upper"));
    } else {
        CHECK(io::Exists(lower)); // the same file under the other spelling
        CHECK(io_test::ReadText(lower).View() == StringView("upper"));
        // A rename that changes only the case is a real rename: the listing shows the new spelling.
        REQUIRE(io::Rename(dir / "Case.txt", lower).Ok());
        io::Directory listing;
        REQUIRE(io::Directory::Open(dir.Path(), &listing).Ok());
        io::DirectoryEntry entry;
        bool found = false;
        REQUIRE(listing.Next(&entry, &found).Ok());
        REQUIRE(found);
        CHECK(entry.name == StringView("case.txt"));
    }
}

TEST_CASE("io on macOS: Sync and WriteFileAtomic reach the medium (F_FULLFSYNC); the cost for 4 KiB", "[io][file][macos]") {
    TempDir dir;
    char data[4096];
    for (usize i = 0; i < sizeof data; ++i) data[i] = static_cast<char>('a' + i % 26);
    const Path target = dir / "settings.cfg";
    constexpr int kRuns = 10;
    u64 slowest = 0;
    u64 total = 0;
    for (int i = 0; i < kRuns; ++i) {
        const u64 start = os::MonotonicTimeNanos();
        REQUIRE(io::WriteFileAtomic(target, data, sizeof data).Ok());
        const u64 took = os::MonotonicTimeNanos() - start;
        total += took;
        if (took > slowest) slowest = took;
    }
    os::Print("WriteFileAtomic of 4 KiB with F_FULLFSYNC: mean %.2f ms, slowest %.2f ms over %d runs\n", static_cast<double>(total) / kRuns / 1e6,
              static_cast<double>(slowest) / 1e6, kRuns);
    io::FilePtr file;
    REQUIRE(io::File::Open(target, io::OpenMode::ReadWrite, 0, &file).Ok());
    CHECK(file->Sync().Ok());
    CHECK(io_test::ReadText(target).View().Length() == 4096);
    CHECK(io::SyncDirectory(dir.Path()).Ok());
}

TEST_CASE("io on macOS: the executable path is the real file, and its directory", "[io][fs][macos]") {
    Path exe;
    REQUIRE(io::ExecutablePath(&exe).Ok());
    CHECK(exe.IsAbsolute());
    CHECK(exe.FileName().Find(StringView("io_tests")) == 0);
    Path canonical;
    REQUIRE(io::Canonical(exe, &canonical).Ok());
    CHECK(canonical == exe); // realpath'ed: no link or `..` left
    Path dir;
    REQUIRE(io::ExecutableDirectory(&dir).Ok());
    CHECK(dir == exe.Parent());
}

TEST_CASE("io on macOS: the temporary and user directories", "[io][fs][macos]") {
    EnvGuard home("HOME"), tmp("TMPDIR"), cfg("XDG_CONFIG_HOME");
    ::setenv("HOME", "/home/test", 1);
    ::setenv("XDG_CONFIG_HOME", "/xdg/config", 1); // not a macOS variable: ignored
    Path p;
    REQUIRE(io::UserConfigDirectory("app", &p).Ok());
    CHECK(Str(p.View()) == "/home/test/Library/Application Support/app");
    REQUIRE(io::UserDataDirectory("app", &p).Ok());
    CHECK(Str(p.View()) == "/home/test/Library/Application Support/app");
    REQUIRE(io::UserCacheDirectory("app", &p).Ok());
    CHECK(Str(p.View()) == "/home/test/Library/Caches/app");
    REQUIRE(io::UserStateDirectory("app", &p).Ok());
    CHECK(Str(p.View()) == "/home/test/Library/Application Support/app/state");
    // HOME must be absolute: a relative one falls back to the password database.
    ::setenv("HOME", "relative/home", 1);
    const io::Status s = io::UserConfigDirectory("app", &p);
    if (s.Ok()) {
        CHECK(p.IsAbsolute());
    } else {
        CHECK(s.Is(Error::NotFound));
    }
    // Bad application names.
    CHECK(io::UserConfigDirectory("", &p).Is(Error::Invalid));
    CHECK(io::UserConfigDirectory("..", &p).Is(Error::Invalid));
    CHECK(io::UserConfigDirectory("a/b", &p).Is(Error::Invalid));
    const engine_test::Text long_name = engine_test::Repeated(201, 'a');
    CHECK(io::UserConfigDirectory(long_name.View(), &p).Is(Error::Invalid));
    // Nothing is created.
    ::setenv("HOME", "/nonexistent-home-for-test", 1);
    REQUIRE(io::UserDataDirectory("app", &p).Ok());
    CHECK_FALSE(io::Exists(p));

    // TMPDIR when absolute; otherwise the per-user directory confstr names (under /var/folders), not the shared /tmp.
    ::setenv("TMPDIR", "/var/tmp", 1);
    REQUIRE(io::TempDirectory(&p).Ok());
    CHECK(Str(p.View()) == "/var/tmp");
    ::setenv("TMPDIR", "relative", 1);
    REQUIRE(io::TempDirectory(&p).Ok());
    CHECK(p.IsAbsolute());
    CHECK(Str(p.View()) != "/tmp");
    CHECK(io::IsDirectory(p));
    ::unsetenv("TMPDIR");
    Path unset;
    REQUIRE(io::TempDirectory(&unset).Ok());
    CHECK(unset == p);
}

namespace {

// Writes 30,000 bytes into the FIFO at `path` once a reader opens it. The open does not block (O_NONBLOCK, retried while no reader is there,
// for at most five seconds), so a reader that never comes fails the test instead of hanging it; SIGPIPE is ignored by the caller, so a reader
// that stops early is a short write here, not the end of the test process.
void FeedFifo(const Path& path) {
    const u64 deadline = os::MonotonicTimeNanos() + 5'000'000'000ull;
    int fd = -1;
    while (fd < 0 && os::MonotonicTimeNanos() < deadline) {
        fd = ::open(Str(path.View()).CStr(), O_WRONLY | O_NONBLOCK | O_CLOEXEC);
        if (fd < 0 && os::LastError() != ENXIO && os::LastError() != EINTR) return; // ENXIO: no reader yet
        if (fd < 0) os::SleepNanos(1'000'000);
    }
    if (fd < 0) return;
    ::fcntl(fd, F_SETFL, ::fcntl(fd, F_GETFL) & ~O_NONBLOCK); // the writes may wait for the reader
    char block[300];
    for (usize i = 0; i < sizeof block; ++i) block[i] = static_cast<char>('0' + i % 10);
    for (int i = 0; i < 100; ++i) {
        for (usize done = 0; done < sizeof block;) {
            const ssize_t n = ::write(fd, block + done, sizeof block - done);
            if (n < 0 && os::LastError() == EINTR) continue;
            if (n <= 0) { // the reader is gone (EPIPE): the size check fails
                ::close(fd);
                return;
            }
            done += static_cast<usize>(n);
        }
    }
    ::close(fd);
}

} // namespace

TEST_CASE("io on macOS: a source whose size is not its content (a FIFO) reads whole", "[io][file][macos]") {
    // Linux reads /proc/self/status for this (file_test.cpp); macOS has no /proc. A FIFO reports no size, so ReadFile must read to the end,
    // through both overloads (a Vector, and an arena).
    const os::SignalHandler old_pipe = os::SetSignalHandler(SIGPIPE, SIG_IGN);
    TempDir dir;
    const Path fifo = dir / "fifo";
    REQUIRE(::mkfifo(Str(fifo.View()).CStr(), 0600) == 0);
    {
        Thread writer([&] { FeedFifo(fifo); });
        Vector<u8> content;
        const io::Status s = io::ReadFile(fifo, &content);
        writer.Join();
        REQUIRE(s.Ok());
        CHECK(content.Size() == 30000);
    }
    {
        Thread writer([&] { FeedFifo(fifo); });
        ArenaAllocator arena(1 << 17);
        u8* data = nullptr;
        usize size = 0;
        const io::Status s = io::ReadFile(fifo, arena, &data, &size);
        writer.Join();
        REQUIRE(s.Ok());
        CHECK(size == 30000);
    }
    os::SetSignalHandler(SIGPIPE, old_pipe);
}

TEST_CASE("io on macOS: Copy copies the data and the mode with fcopyfile, not the times (no clone)", "[io][fs][macos]") {
    TempDir dir;
    const Path from = dir / "from.bin";
    const Path to = dir / "to.bin";
    REQUIRE(io::WriteFile(from, "copied bytes", 12).Ok());
    REQUIRE(::chmod(Str(from.View()).CStr(), 0640) == 0);
    struct timeval old_times[2] = {{1'000'000'000, 0}, {1'000'000'000, 0}}; // 2001
    REQUIRE(::utimes(Str(from.View()).CStr(), old_times) == 0);
    REQUIRE(io::Copy(from, to, false).Ok());
    CHECK(io_test::ReadText(to).View() == StringView("copied bytes"));
    io::FileInfo info;
    REQUIRE(io::Stat(to, &info).Ok());
    CHECK(info.permissions == 0640);
    CHECK(info.modified_unix_ns > 1'700'000'000LL * 1'000'000'000LL); // now, not the source's 2001: an APFS clone would have kept it
    CHECK(io::Copy(from, to, false).Is(Error::AlreadyExists));
    REQUIRE(io::WriteFile(from, "new", 3).Ok());
    REQUIRE(io::Copy(from, to, true).Ok());
    CHECK(io_test::ReadText(to).View() == StringView("new")); // overwritten, truncated
}

TEST_CASE("io on macOS: without HOME the user directories come from Foundation (the account's home)", "[io][fs][macos]") {
    EnvGuard home("HOME");
    ::unsetenv("HOME");
    struct passwd* pw = ::getpwuid(::geteuid());
    REQUIRE(pw != nullptr);
    const Path account(pw->pw_dir);
    Path p;
    REQUIRE(io::UserConfigDirectory("app", &p).Ok());
    CHECK(p == account.Join("Library/Application Support/app"));
    REQUIRE(io::UserCacheDirectory("app", &p).Ok());
    CHECK(p == account.Join("Library/Caches/app"));
    CHECK(io::CrashDumpDirectory(&p).Is(Error::NotFound)); // the crash handler does not guess without HOME
}

TEST_CASE("io on macOS: Copy from a FIFO copies what it delivers, as on Linux", "[io][fs][macos]") {
    // fcopyfile refuses a source that is not a regular file (ENOTSUP); Copy must still copy it, through the read/write loop (Day 288 review).
    const os::SignalHandler old_pipe = os::SetSignalHandler(SIGPIPE, SIG_IGN);
    TempDir dir;
    const Path fifo = dir / "fifo";
    REQUIRE(::mkfifo(Str(fifo.View()).CStr(), 0600) == 0);
    Thread writer([&] { FeedFifo(fifo); });
    const io::Status s = io::Copy(fifo, dir / "copy.bin", false);
    writer.Join();
    os::SetSignalHandler(SIGPIPE, old_pipe);
    REQUIRE(s.Ok());
    io::FileInfo info;
    REQUIRE(io::Stat(dir / "copy.bin", &info).Ok());
    CHECK(info.size == 30000);
}
