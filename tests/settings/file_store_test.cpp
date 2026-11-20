#include <catch2/catch_test_macros.hpp>
#include "build_defs.h"

#if !ENGINE_PLATFORM_WINDOWS
#include <unistd.h>  // geteuid: the read-only directory case below is POSIX-only
#endif

#include "catch_engine_types.h"
#include "config_store_conformance.h"
#include "io/file.h"
#include "io/directory.h"
#include "io/file_system.h"
#include "settings/file_store.h"
#include "temp_dir.h"
#include "test_text.h"

using namespace vse;
using namespace vse::settings;
using namespace settings_test;

namespace
{

engine_test::Text Slurp(const io::Path& path)
{
    Vector<u8> bytes;
    if (io::ReadFile(path, &bytes).Failed())
    {
        return engine_test::MakeText("<missing>");
    }
    return engine_test::MakeText(StringView(reinterpret_cast<const char*>(bytes.Data()), bytes.Size()));
}

void Write(const io::Path& path, StringView text)
{
    REQUIRE(io::WriteFile(path, text.Data(), text.Length()).Ok());
}

StoredValue Text(const char* text)
{
    StoredValue sv;
    sv.value = Value::FromText(StringView(text));
    return sv;
}

}  // namespace

TEST_CASE("ConfigStore conformance: FileConfigStore", "[settings][file][conformance]")
{
    io_test::TempDir dir;
    const io::Path path = dir / "settings.cfg";
    auto make = [&] { return ConfigStorePtr(MakeUnique<FileConfigStore>(path)); };
    RunWritableConformance(make);
    RunDurabilityConformance(make);
    RunCapabilityConformance(make);
}

TEST_CASE("FileConfigStore: a missing file is an empty store, and the first commit creates it with the chosen mode",
          "[settings][file]")
{
    io_test::TempDir dir;
    const io::Path path = dir / "new.cfg";
    FileStoreOptions options;
    options.permissions = 0640;
    FileConfigStore store(path, options);
    REQUIRE(store.Load().Ok());
    CHECK_FALSE(store.Damaged());
    CHECK(CountKeys(store) == 0);
    REQUIRE(store.Set("a.b", Text("1")).Ok());
    const Status s = store.Commit(CommitOptions());
    REQUIRE(s.Ok());
    CHECK(s.done == 1);
    CHECK(Slurp(path).View() == "[a]\nb = 1\n");
#if !ENGINE_PLATFORM_WINDOWS  // Windows has no file modes: the store's permissions option does not apply there
                              // (io/BACKEND.md, SetPermissions)
    io::FileInfo info;
    REQUIRE(io::Stat(path, &info).Ok());
    CHECK((info.permissions & 0777) == 0640);
#endif
}

TEST_CASE("FileConfigStore: what a person wrote survives a save", "[settings][file]")
{
    io_test::TempDir dir;
    const io::Path path = dir / "settings.cfg";
    const char* original =
        "# my settings\nversion = 1\n\n[renderer]\n# fast\npresent_mode = mailbox   # fifo | mailbox\nwidth = 1280\nlegacy = 3 ; unknown key\n";
    Write(path, original);
    FileConfigStore store(path);
    REQUIRE(store.Load().Ok());
    REQUIRE(store.Set("renderer.width", Text("1920")).Ok());
    REQUIRE(store.Set("renderer.hdr", Text("off")).Ok());
    REQUIRE(store.Commit(CommitOptions()).Ok());
    CHECK(
        Slurp(path).View() ==
        "# my settings\nversion = 1\n\n[renderer]\n# fast\npresent_mode = mailbox   # fifo | mailbox\nwidth = 1920\nlegacy = 3 ; unknown key\nhdr = off\n");
    // The previous file is the backup.
    CHECK(Slurp(dir / "settings.cfg.bak").View() == StringView(original));
}

TEST_CASE("FileConfigStore: BOM and CRLF are kept, a large file loads, and invalid UTF-8 damages the store",
          "[settings][file]")
{
    io_test::TempDir dir;
    const io::Path path = dir / "a.cfg";
    Write(path, "\xEF\xBB\xBF"
                "a = 1\r\nb = 2\r\n");
    FileConfigStore store(path);
    REQUIRE(store.Load().Ok());
    REQUIRE(store.Set("b", Text("3")).Ok());
    REQUIRE(store.Set("c", Text("4")).Ok());
    REQUIRE(store.Commit(CommitOptions()).Ok());
    CHECK(Slurp(path).View() == StringView("\xEF\xBB\xBF"
                                           "a = 1\r\nb = 3\r\nc = 4\r\n"));

    engine_test::Text big = engine_test::MakeText();
    for (int i = 0; i < 30000; ++i)
    {
        big.Append(engine_test::Format("section%d.key_%d = value_%d   # a trailing comment\n", i % 50, i, i).View());
    }
    REQUIRE(big.Length() > (1u << 20));
    const io::Path big_path = dir / "big.cfg";
    Write(big_path, big.View());
    FileConfigStore bigstore(big_path);
    REQUIRE(bigstore.Load().Ok());
    CHECK(CountKeys(bigstore) == 30000);
    REQUIRE(bigstore.Set("section0.key_0", Text("changed")).Ok());
    REQUIRE(bigstore.Commit(CommitOptions()).Ok());
    CHECK(Slurp(big_path).Length() == big.Length() - StringView("value_0").Length() + StringView("changed").Length());

    const io::Path bad_path = dir / "bad.cfg";
    Write(bad_path, "a = 1\nb = \xff\xfe\n");
    FileConfigStore bad(bad_path);
    CHECK(bad.Load().Is(StoreError::Damaged));
    CHECK(bad.Damaged());
    CHECK(bad.Report().error_count == 1);
    CHECK(CountKeys(bad) == 1);  // the readable line is available
}

TEST_CASE("FileConfigStore: a damaged file is never overwritten by accident", "[settings][file][damaged]")
{
    io_test::TempDir dir;
    const io::Path path = dir / "settings.cfg";
    const char* damaged = "version = 1\nthis line is garbage\n[renderer]\nwidth = 1280\nbroken = \"oops\n";
    Write(path, damaged);
    FileConfigStore store(path);
    CHECK(store.Load().Is(StoreError::Damaged));
    CHECK(store.Report().error_count == 2);
    StoredValue sv;
    REQUIRE(store.Get("renderer.width", &sv).Ok());
    CHECK(sv.value.AsText() == "1280");
    REQUIRE(store.Set("renderer.width", Text("1920")).Ok());
    const Status refused = store.Commit(CommitOptions());
    CHECK(refused.Is(StoreError::Damaged));
    CHECK(refused.done == 0);
    CHECK(Slurp(path).View() == StringView(damaged));  // byte for byte
    CHECK_FALSE(io::Exists(dir / "settings.cfg.bak"));
    // The explicit replace: the damaged text goes to the backup, the lines that could not be read are dropped.
    CommitOptions replace;
    replace.replace_damaged = true;
    REQUIRE(store.Commit(replace).Ok());
    CHECK(Slurp(dir / "settings.cfg.bak").View() == StringView(damaged));
    CHECK(Slurp(path).View() == "version = 1\n[renderer]\nwidth = 1920\n");
    CHECK_FALSE(store.Damaged());
}

TEST_CASE("FileConfigStore: failures leave the old file and the staged changes", "[settings][file][failure]")
{
    io_test::TempDir dir;
    const io::Path path = dir / "settings.cfg";
    Write(path, "a = 1\n");
    FileStoreOptions options;
    options.keep_backup = false;  // so the injected failure hits the write of the settings file itself
    for (int step = 1; step <= 4; ++step)
    {
        INFO("failure step " << step);
        FileConfigStore store(path, options);
        REQUIRE(store.Load().Ok());
        REQUIRE(store.Set("a", Text("2")).Ok());
        REQUIRE(store.Set("b", Text("3")).Ok());
        io::detail::SetAtomicWriteFailureStep(step);
        const Status failed = store.Commit(CommitOptions());
        io::detail::SetAtomicWriteFailureStep(0);
        CHECK(failed.Is(StoreError::Io));
        CHECK(failed.done == 0);
        CHECK(failed.total == 2);
        CHECK(Slurp(path).View() == "a = 1\n");
        // No temporary file is left behind.
        Vector<io::DirectoryItem> items;
        REQUIRE(io::ListDirectory(dir.Path(), true, &items).Ok());
        CHECK(items.Size() == 1);
        // The changes are still staged: the retry lands both.
        const Status retry = store.Commit(CommitOptions());
        REQUIRE(retry.Ok());
        CHECK(retry.done == 2);
        CHECK(Slurp(path).View() == "a = 2\nb = 3\n");
        Write(path, "a = 1\n");
    }
    // A failure while writing the backup stops before the settings file is touched.
    FileConfigStore with_backup(path);
    REQUIRE(with_backup.Load().Ok());
    REQUIRE(with_backup.Set("a", Text("9")).Ok());
    io::detail::SetAtomicWriteFailureStep(4);
    CHECK(with_backup.Commit(CommitOptions()).Is(StoreError::Io));
    io::detail::SetAtomicWriteFailureStep(0);
    CHECK(Slurp(path).View() == "a = 1\n");
}

TEST_CASE("FileConfigStore: read-only files and directories, and missing directories", "[settings][file][permissions]")
{
    io_test::TempDir dir;
#if !ENGINE_PLATFORM_WINDOWS  // a directory without write permission refuses the commit; on Windows a directory's
                              // read-only attribute does not stop writes
    if (::geteuid() != 0)
    {  // (root writes anywhere)
        REQUIRE(io::CreateDirectory(dir / "ro").Ok());
        Write(dir / "ro/settings.cfg", "a = 1\n");
        REQUIRE(io::SetPermissions(dir / "ro", 0500).Ok());
        FileConfigStore store(dir / "ro/settings.cfg");
        REQUIRE(store.Load().Ok());
        REQUIRE(store.Set("a", Text("2")).Ok());
        const Status refused = store.Commit(CommitOptions());
        CHECK((refused.Is(StoreError::PermissionDenied) || refused.Is(StoreError::ReadOnly)));
        CHECK(Slurp(dir / "ro/settings.cfg").View() == "a = 1\n");
        REQUIRE(io::SetPermissions(dir / "ro", 0700).Ok());
    }
#endif
    // A missing directory is created only when the options say so.
    FileConfigStore no_dirs(dir / "deep/er/settings.cfg");
    REQUIRE(no_dirs.Load().Ok());
    REQUIRE(no_dirs.Set("a", Text("1")).Ok());
    CHECK(no_dirs.Commit(CommitOptions()).Failed());
    CHECK_FALSE(io::Exists(dir / "deep"));
    FileStoreOptions make_dirs;
    make_dirs.create_directories = true;
    FileConfigStore with_dirs(dir / "deep/er/settings.cfg", make_dirs);
    REQUIRE(with_dirs.Load().Ok());
    REQUIRE(with_dirs.Set("a", Text("1")).Ok());
    CHECK(with_dirs.Commit(CommitOptions()).Ok());
    CHECK(Slurp(dir / "deep/er/settings.cfg").View() == "a = 1\n");
}

TEST_CASE("FileConfigStore: two stores on one file: the last commit wins and the file is never torn",
          "[settings][file]")
{
    io_test::TempDir dir;
    const io::Path path = dir / "settings.cfg";
    Write(path, "a = 1\nb = 2\n");
    FileConfigStore first(path), second(path);
    REQUIRE(first.Load().Ok());
    REQUIRE(second.Load().Ok());
    REQUIRE(first.Set("a", Text("10")).Ok());
    REQUIRE(second.Set("b", Text("20")).Ok());
    REQUIRE(first.Commit(CommitOptions()).Ok());
    CHECK(Slurp(path).View() == "a = 10\nb = 2\n");
    REQUIRE(second.Commit(CommitOptions()).Ok());
    CHECK(Slurp(path).View() == "a = 1\nb = 20\n");  // the whole file is the second store's view
    // A text store takes text only.
    StoredValue typed;
    typed.typed = true;
    typed.value = Value::FromInt(1);
    CHECK(first.Set("c", typed).Is(StoreError::Invalid));
    CHECK(first.Set("c", Text("two\nlines")).Is(StoreError::Invalid));
}

TEST_CASE("FileConfigStore: a Load that cannot read the file keeps staged changes and blocks Commit",
          "[settings][file][regression]")
{
    io_test::TempDir dir;
    const io::Path path = dir / "settings.cfg";
    Write(path, engine_test::Repeated(200, 'x').View());
    FileStoreOptions options;
    options.max_bytes = 100;  // the file is over the cap: it cannot be read
    FileConfigStore store(path, options);
    REQUIRE(store.Set("a", Text("1")).Ok());
    CHECK(store.Load().Is(StoreError::TooLong));
    StoredValue sv;
    CHECK(store.Get("a", &sv).Ok());                               // the staged change is still there
    CHECK(store.Commit(CommitOptions()).Is(StoreError::TooLong));  // and the unreadable file is not overwritten
    CHECK(Slurp(path).Length() == 200);
}

TEST_CASE("FileConfigStore: a failed replace of a damaged file leaves the store as it was",
          "[settings][file][regression]")
{
    io_test::TempDir dir;
    const io::Path path = dir / "settings.cfg";
    const char* damaged = "a = 1\ngarbage line\n";
    Write(path, damaged);
    FileStoreOptions options;
    options.keep_backup = false;
    FileConfigStore store(path, options);
    CHECK(store.Load().Is(StoreError::Damaged));
    REQUIRE(store.Set("a", Text("2")).Ok());
    CommitOptions replace;
    replace.replace_damaged = true;
    io::detail::SetAtomicWriteFailureStep(1);
    CHECK(store.Commit(replace).Is(StoreError::Io));
    io::detail::SetAtomicWriteFailureStep(0);
    CHECK(store.Damaged());                                        // still damaged
    CHECK(store.Commit(CommitOptions()).Is(StoreError::Damaged));  // still refused without the flag
    CHECK(Slurp(path).View() == StringView(damaged));
    CHECK(store.Commit(replace).Ok());  // the retry writes what was prepared
    CHECK(Slurp(path).View() == "a = 2\n");
}
