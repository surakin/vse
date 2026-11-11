#include <catch2/catch_test_macros.hpp>

#include "sync/atomic.h"
#include "sync/thread.h"
#include "build_defs.h"

#if !ENGINE_PLATFORM_WINDOWS
#include <dirent.h>
#include <sys/stat.h>
#include <unistd.h>
#endif

#include "file_conformance.h"
#include "io/file.h"
#include "io/file_system.h"
#include "temp_dir.h"
#include "test_text.h"

using namespace engine_placeholder;
using io::Error;
using io::OpenMode;
using io::Path;
using io_test::Str;
using io_test::TempDir;

namespace
{

io::FilePtr MakeLocal(TempDir& dir, StringView content, bool writable)
{
    static int counter = 0;
    const engine_test::Text name = engine_test::Format("conf%d", counter++);
    const Path path = dir / name.View();
    REQUIRE(io::WriteFile(path, content.Data(), content.Length()).Ok());
    io::FilePtr f;
    REQUIRE(io::File::Open(path, writable ? OpenMode::ReadWrite : OpenMode::Read, io::kOpenNone, &f).Ok());
    return f;
}

usize CountEntries(const Path& dir)
{
#if ENGINE_PLATFORM_WINDOWS
    usize n = 0;
    io_test::ForEachEntry(io_test::WinPath(dir).text, [&](const wchar_t*, DWORD) { ++n; });
    return n;
#else
    char p[io::kMaxPathBytes + 1];
    dir.ToNative(p, sizeof(p));
    usize n = 0;
    if (DIR* d = ::opendir(p))
    {
        while (const dirent* e = ::readdir(d))
        {
            if (StringView(e->d_name) != StringView(".") && StringView(e->d_name) != StringView(".."))
            {
                ++n;
            }
        }
        ::closedir(d);
    }
    return n;
#endif
}

}  // namespace

TEST_CASE("io::File: LocalFile passes the read conformance suite", "[io][file]")
{
    TempDir dir;
    io_test::RunReadConformance([&](StringView c, bool w) { return MakeLocal(dir, c, w); });
}

TEST_CASE("io::File: LocalFile passes the write conformance suite", "[io][file]")
{
    TempDir dir;
    io_test::RunWriteConformance([&](StringView c, bool w) { return MakeLocal(dir, c, w); });
}

TEST_CASE("io::File: open modes, flags and errors", "[io][file]")
{
    TempDir dir;
    io::FilePtr f;
    CHECK(io::File::Open(dir / "missing", OpenMode::Read, io::kOpenNone, &f).Is(Error::NotFound));
    CHECK(io::File::Open(dir / "missing", OpenMode::ReadWrite, io::kOpenNone, &f).Is(Error::NotFound));
    CHECK(io::File::Open(dir.Path(), OpenMode::Read, io::kOpenNone, &f).Is(Error::IsADirectory));
    CHECK(io::File::Open(dir.Path(), OpenMode::Write, io::kOpenNone, &f).Is(Error::IsADirectory));
    CHECK_FALSE(f);

    // Write creates and truncates.
    REQUIRE(io::File::Open(dir / "w", OpenMode::Write, io::kOpenNone, &f).Ok());
    REQUIRE(f->Write("abcdef", 6, 0).Ok());
    REQUIRE(f->Close().Ok());
    REQUIRE(io::File::Open(dir / "w", OpenMode::Write, io::kOpenNone, &f).Ok());
    u64 size = 1;
    REQUIRE(f->Size(&size).Ok());
    CHECK(size == 0);
    // A write-only file cannot be read; a read-only file cannot be written.
    usize n = 0;
    char b[4];
    CHECK(f->Read(b, 4, 0, &n).Is(Error::Invalid));
    REQUIRE(io::File::Open(dir / "w", OpenMode::Read, io::kOpenNone, &f).Ok());
    CHECK(f->Write("x", 1, 0).Is(Error::Invalid));
    CHECK((f->Capabilities() & io::kCapWritable) == 0);
    CHECK(f->Truncate(0).Is(Error::Invalid));

    // CreateNew.
    CHECK(io::File::Open(dir / "w", OpenMode::ReadWrite, io::kOpenCreateNew, &f).Is(Error::AlreadyExists));
    REQUIRE(io::File::Open(dir / "n", OpenMode::ReadWrite, io::kOpenCreateNew, &f).Ok());
    // Create on ReadWrite; Truncate flag.
    REQUIRE(io::File::Open(dir / "c", OpenMode::ReadWrite, io::kOpenCreate, &f).Ok());
    REQUIRE(f->Write("12345", 5, 0).Ok());
    REQUIRE(io::File::Open(dir / "c", OpenMode::ReadWrite, io::kOpenTruncate, &f).Ok());
    REQUIRE(f->Size(&size).Ok());
    CHECK(size == 0);
    // A read-only open cannot create or truncate.
    CHECK(io::File::Open(dir / "ghost", OpenMode::Read, io::kOpenCreate, &f).Is(Error::Invalid));
    CHECK(io::File::Open(dir / "w", OpenMode::Read, io::kOpenTruncate, &f).Is(Error::Invalid));
    CHECK_FALSE(io::Exists(dir / "ghost"));
    // Seek after Close fails like everything else.
    REQUIRE(io::File::Open(dir / "w", OpenMode::Read, io::kOpenNone, &f).Ok());
    CHECK(f->IsOpen());
    REQUIRE(f->Close().Ok());
    CHECK_FALSE(f->IsOpen());
    CHECK(f->Seek(5).Is(Error::Invalid));
    // A missing parent.
    CHECK(io::File::Open(dir / "no/such/dir/f", OpenMode::Write, io::kOpenNone, &f).Is(Error::NotFound));
    // An unreadable file.
#if ENGINE_PLATFORM_WINDOWS
    REQUIRE(io_test::WriteText(dir / "secret", "x"));
    REQUIRE(io_test::DenyCurrentUser(dir / "secret", FILE_READ_DATA));
    CHECK(io::File::Open(dir / "secret", OpenMode::Read, io::kOpenNone, &f).Is(Error::PermissionDenied));
#else
    if (::geteuid() != 0)
    {
        REQUIRE(io_test::WriteText(dir / "secret", "x"));
        REQUIRE(::chmod(Str((dir / "secret").View()).CStr(), 0200) == 0);
        CHECK(io::File::Open(dir / "secret", OpenMode::Read, io::kOpenNone, &f).Is(Error::PermissionDenied));
    }
#endif
}

TEST_CASE("io::File: append mode", "[io][file]")
{
    TempDir dir;
    io::FilePtr f;
    REQUIRE(io::File::Open(dir / "log", OpenMode::Append, io::kOpenNone, &f).Ok());
    CHECK((f->Capabilities() & io::kCapAppendOnly) != 0);
    REQUIRE(f->Append("one\n", 4).Ok());
    REQUIRE(f->Write("two\n", 4).Ok());  // sequential write on an append file appends
    CHECK(f->Write("zzz", 3, 0).Is(Error::Unsupported));
    REQUIRE(f->Close().Ok());
    REQUIRE(
        io::File::Open(dir / "log", OpenMode::Append, io::kOpenNone, &f).Ok());  // reopening appends, does not truncate
    REQUIRE(f->Append("three\n", 6).Ok());
    f.Reset();
    CHECK(io_test::ReadText(dir / "log") == "one\ntwo\nthree\n");
    // Append on a file not opened for it.
    REQUIRE(io::File::Open(dir / "log", OpenMode::ReadWrite, io::kOpenNone, &f).Ok());
    CHECK(f->Append("x", 1).Is(Error::Unsupported));
}

TEST_CASE("io::File: locations, schemes and the registry", "[io][file]")
{
    TempDir dir;
    REQUIRE(io_test::WriteText(dir / "f.txt", "local"));
    const engine_test::Text native = engine_test::Concat(dir.Native().View(), "/f.txt");
    io::FilePtr f;
    char b[8];
    usize n = 0;
    REQUIRE(io::File::OpenLocation(native.View(), OpenMode::Read, io::kOpenNone, &f).Ok());
    REQUIRE(f->Read(b, 8, 0, &n).Ok());
    CHECK(StringView(b, n) == StringView("local"));
    // A file URL is `file://` plus an absolute path, whose `/` starts it on POSIX; a drive path gets one more
    // (`file:///C:/x`).
    const StringView file_scheme = io::NativePathStyle() == io::PathStyle::Windows ? StringView("file:///")
                                                                                   : StringView("file://");
    const engine_test::Text url = engine_test::Concat(file_scheme, native.View());
    REQUIRE(io::File::OpenLocation(url.View(), OpenMode::Read, io::kOpenNone, &f).Ok());
    CHECK(io::File::OpenLocation("file://host/x", OpenMode::Read, io::kOpenNone, &f).Is(Error::Unsupported));
    // Percent escapes in a file URL.
    REQUIRE(io_test::WriteText(dir / "a b.txt", "spaced"));
    const engine_test::Text escaped = engine_test::Concat(file_scheme, dir.Native().View(), "/a%20b.txt");
    REQUIRE(io::File::OpenLocation(escaped.View(), OpenMode::Read, io::kOpenNone, &f).Ok());
    CHECK(io::File::OpenLocation("file:///tmp/%zz", OpenMode::Read, io::kOpenNone, &f).Is(Error::Invalid));
    CHECK(io::File::OpenLocation("file:///tmp/%2", OpenMode::Read, io::kOpenNone, &f).Is(Error::Invalid));
    CHECK(io::File::OpenLocation("file:///tmp/a%00b", OpenMode::Read, io::kOpenNone, &f)
              .Is(Error::Invalid));  // an encoded NUL
    CHECK(io::File::OpenLocation("nope://x/y", OpenMode::Read, io::kOpenNone, &f).Is(Error::Unsupported));

    // A registered scheme gets the whole location; a directory path with a colon is still a path.
    static engine_test::Text seen = engine_test::MakeText();
    io::FileFactory factory = [](StringView location, OpenMode, u32, io::FilePtr* out) -> io::Status
    {
        seen = engine_test::MakeText(location);
        out->Reset();
        return io::Status::Of(Error::NotFound);
    };
    REQUIRE(io::RegisterFileScheme("fake", factory).Ok());
    CHECK(io::RegisterFileScheme("fake", factory).Is(Error::AlreadyExists));
    CHECK(io::RegisterFileScheme("file", factory).Is(Error::Invalid));
    CHECK(io::RegisterFileScheme("Bad", factory).Is(Error::Invalid));
    CHECK(io::RegisterFileScheme("x", nullptr).Is(Error::Invalid));
    CHECK(io::File::OpenLocation("fake://server/a b", OpenMode::Read, io::kOpenNone, &f).Is(Error::NotFound));
    CHECK(seen == "fake://server/a b");
    io::UnregisterFileScheme("fake");
    CHECK(io::File::OpenLocation("fake://server/a", OpenMode::Read, io::kOpenNone, &f).Is(Error::Unsupported));
    io::UnregisterFileScheme("fake");  // unknown: ignored
}

TEST_CASE("io::File: files over 4 GiB and generated files", "[io][file]")
{
    TempDir dir;
    io::FilePtr f;
    REQUIRE(io::File::Open(dir / "sparse", OpenMode::ReadWrite, io::kOpenCreate, &f).Ok());
    const u64 five_gib = u64{5} << 30;
    const io::Status t = f->Truncate(five_gib);
    if (t.Failed())
    {
        WARN("skipped the 5 GiB offset test: the file system refused a sparse file (" << io::ErrorName(t.error) << ")");
    }
    else
    {
        REQUIRE(f->Write("end!", 4, five_gib - 4).Ok());
        char b[4];
        REQUIRE(io::ReadExact(*f, b, 4, five_gib - 4).Ok());
        CHECK(StringView(b, 4) == StringView("end!"));
        u64 size = 0;
        REQUIRE(f->Size(&size).Ok());
        CHECK(size == five_gib);
    }
#if ENGINE_PLATFORM_LINUX  // Windows has no file that reports a wrong size; macOS has no /proc (io_macos_test.cpp reads
                           // a FIFO instead)
    // /proc files report size 0 but have content: ReadAll must not trust the size.
    Vector<u8> status;
    REQUIRE(io::ReadFile(Path("/proc/self/status"), &status).Ok());
    CHECK(status.Size() > 100);
#endif
}

TEST_CASE("io::File: a full device fails with NoSpace", "[io][file]")
{
    io::FilePtr f;
    if (!io::Exists(Path("/dev/full")))
    {
        WARN("skipped: /dev/full does not exist here");
        return;
    }
    REQUIRE(io::File::Open(Path("/dev/full"), OpenMode::Write, io::kOpenNone, &f).Ok());
    CHECK(f->Write("data", 4, 0).Is(Error::NoSpace));
}

TEST_CASE("io::File: concurrent offset reads of one file", "[io][file]")
{
    TempDir dir;
    const engine_test::Text content = io_test::PatternBytes(256 * 1024);
    io::FilePtr f = MakeLocal(dir, content.View(), false);
    Atomic<int> bad{0};
    Vector<Thread> threads;
    threads.Resize(4);
    for (int t = 0; t < 4; ++t)
    {
        threads[t] = Thread(
            [&, t]
            {
                Vector<char> got = engine_test::Chars(8192);
                for (int i = 0; i < 200; ++i)
                {
                    const u64 off = static_cast<u64>((t * 7919 + i * 1237) % (content.Length() - 8192));
                    if (io::ReadExact(*f, got.Data(), got.Size(), off).Failed() ||
                        StringView(got.Data(), got.Size()) != content.View().Substring(off, 8192))
                    {
                        bad++;
                    }
                }
            });
    }
    for (usize i = 0; i < threads.Size(); ++i)
    {
        threads[i].Join();
    }
    CHECK(bad.Load() == 0);
}

TEST_CASE("io: the read helpers", "[io][file]")
{
    TempDir dir;
    const StringView text = "line one\nline two\n";
    REQUIRE(io::WriteFile(dir / "t.txt", text.Data(), text.Length()).Ok());
    String<HeapAllocator> out(HeapAllocator::Default());
    REQUIRE(io::ReadTextFile(dir / "t.txt", &out).Ok());
    CHECK(out.View() == text);
    ArenaAllocator arena(1 << 20);
    u8* data = nullptr;
    usize size = 0;
    REQUIRE(io::ReadFile(dir / "t.txt", arena, &data, &size).Ok());
    CHECK(size == text.Length());
    CHECK(StringView(reinterpret_cast<char*>(data), size) == text);
    REQUIRE(io::WriteFile(dir / "empty", "", 0).Ok());
    REQUIRE(io::ReadFile(dir / "empty", arena, &data, &size).Ok());
    CHECK(size == 0);
    CHECK(data != nullptr);
#if ENGINE_PLATFORM_LINUX
    // A file whose reported size is wrong (a /proc file reports 0) still reads whole through the arena path.
    REQUIRE(io::ReadFile(Path("/proc/self/status"), arena, &data, &size).Ok());
    CHECK(size > 100);
#endif
    ArenaAllocator tiny(8);
    CHECK(io::ReadFile(dir / "t.txt", tiny, &data, &size).Is(Error::OutOfMemory));
    Vector<u8> bytes;
    CHECK(io::ReadFile(dir / "t.txt", &bytes, 4).Is(Error::TooLong));
    CHECK(io::ReadFile(dir / "missing", &bytes).Is(Error::NotFound));
}

TEST_CASE("io: WriteFileAtomic replaces whole files and leaves nothing behind on failure", "[io][file]")
{
    TempDir dir;
    const Path target = dir / "config.ini";
    REQUIRE(io::WriteFileAtomic(target, "old", 3).Ok());  // creates
    CHECK(io_test::ReadText(target) == "old");
    io::FileInfo info;
    REQUIRE(io::Stat(target, &info).Ok());
    CHECK(info.permissions == io_test::ReportedPermissions(0600));
    REQUIRE(io::WriteFileAtomic(target, "new content", 11).Ok());
    CHECK(io_test::ReadText(target) == "new content");
    CHECK(CountEntries(dir.Path()) == 1);  // no temporary left
    // A replaced file keeps its permission bits.
    REQUIRE(io::SetPermissions(target, 0640).Ok());
    REQUIRE(io::WriteFileAtomic(target, "perm", 4).Ok());
    REQUIRE(io::Stat(target, &info).Ok());
    CHECK(info.permissions == io_test::ReportedPermissions(0640));
    REQUIRE(io::WriteFileAtomic(target, "new content", 11).Ok());
    // A symbolic link target: the file behind the link is replaced and the link stays.
    REQUIRE(io::CreateDirectory(dir / "real").Ok());
    REQUIRE(io_test::WriteText(dir / "real/data", "before"));
    if (io_test::SymlinksAvailable())
    {  // Windows: Developer Mode
        REQUIRE(io_test::MakeSymlink("real/data", dir / "lnk"));
        REQUIRE(io::WriteFileAtomic(dir / "lnk", "after", 5).Ok());
        CHECK(io_test::ReadText(dir / "real/data") == "after");
        REQUIRE(io::Lstat(dir / "lnk", &info).Ok());
        CHECK(info.type == io::FileType::Symlink);
        REQUIRE(io::RemoveAll(dir / "real").Ok());
        REQUIRE(io::Remove(dir / "lnk").Ok());
    }
    else
    {
        WARN(
            "skipped the symbolic link target of WriteFileAtomic: this process cannot create symbolic links (Windows: Developer Mode)");
        REQUIRE(io::RemoveAll(dir / "real").Ok());
    }
    CHECK(io::SyncDirectory(dir.Path()).Ok());
    CHECK(io::SyncDirectory(dir / "nope").Is(Error::NotFound));
    CHECK(io::SyncDirectory(target).Is(Error::NotADirectory));

    // A new file can be given other permissions (ordinary generated files); an existing one keeps its own.
    REQUIRE(io::WriteFileAtomic(dir / "ordinary.txt", "x", 1, 0644).Ok());
    REQUIRE(io::Stat(dir / "ordinary.txt", &info).Ok());
    CHECK(info.permissions == io_test::ReportedPermissions(0644));
    REQUIRE(io::SetPermissions(dir / "ordinary.txt", 0400).Ok());  // read-only: the one bit every platform keeps
    REQUIRE(io::WriteFileAtomic(dir / "ordinary.txt", "y", 1, 0644).Ok());
    REQUIRE(io::Stat(dir / "ordinary.txt", &info).Ok());
    CHECK(info.permissions == io_test::ReportedPermissions(0400));
    REQUIRE(io::SetPermissions(dir / "ordinary.txt", 0600).Ok());
    REQUIRE(io::Remove(dir / "ordinary.txt").Ok());
    // A failure at each step keeps the old content and removes the temporary.
    for (int step = 1; step <= 4; ++step)
    {
        INFO("failing step " << step);
        io::detail::SetAtomicWriteFailureStep(step);
        const io::Status s = io::WriteFileAtomic(target, "BROKEN", 6);
        io::detail::SetAtomicWriteFailureStep(0);
        CHECK(s.Is(Error::Io));
        CHECK(io_test::ReadText(target) == "new content");
        CHECK(CountEntries(dir.Path()) == 1);
    }
    // A missing directory, a directory as the target, a root.
    CHECK(io::WriteFileAtomic(dir / "no/such/f", "x", 1).Is(Error::NotFound));
    REQUIRE(io::CreateDirectory(dir / "d").Ok());
    CHECK(io::WriteFileAtomic(dir / "d", "x", 1).Failed());
    CHECK(CountEntries(dir.Path()) == 2);  // config.ini and d: still no temporary
    CHECK(io::WriteFileAtomic(Path("/"), "x", 1).Is(Error::Invalid));
    // A large payload, larger than any internal buffer.
    const engine_test::Text big = io_test::PatternBytes(5 * 1024 * 1024);
    REQUIRE(io::WriteFileAtomic(target, big.CStr(), big.Length()).Ok());
    Vector<u8> back;
    REQUIRE(io::ReadFile(target, &back).Ok());
    CHECK(StringView(reinterpret_cast<const char*>(back.Data()), back.Size()) == big.View());
}

TEST_CASE("The span overloads write and read the same bytes as the pointer forms", "[io][span]")
{
    TempDir dir;
    const byte payload[] = {1, 2, 3, 250, 0, 7};
    const Path a = dir / "span_a";
    const Path b = dir / "span_b";
    REQUIRE(io::WriteFile(a, ByteSpan(payload)).Ok());
    REQUIRE(io::WriteFileAtomic(b, ByteSpan(payload), 0644).Ok());
    for (const Path& p : {a, b})
    {
        Vector<u8> back;
        REQUIRE(io::ReadFile(p, &back).Ok());
        REQUIRE(back.Size() == sizeof(payload));
        CHECK(ByteSpan(back).Size() == sizeof(payload));
        CHECK(back[3] == 250);
    }
    io::FilePtr f;
    REQUIRE(io::File::Open(a, OpenMode::Read, io::kOpenNone, &f).Ok());
    byte buf[4] = {};
    usize n = 0;
    REQUIRE(f->Read(MutableByteSpan(buf), &n).Ok());
    CHECK(n == 4);
    CHECK(buf[2] == 3);
}
