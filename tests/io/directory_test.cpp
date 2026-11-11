#include <catch2/catch_test_macros.hpp>
#include "build_defs.h"

#if !ENGINE_PLATFORM_WINDOWS
#include <unistd.h>
#endif

#include "io/directory.h"
#include "temp_dir.h"
#include "test_text.h"

using namespace engine_placeholder;
using io::Error;
using io::Path;
using io_test::Str;
using io_test::TempDir;

namespace
{

engine_test::TextSet Names(const Path& dir)
{
    engine_test::TextSet names;
    io::Directory d;
    REQUIRE(io::Directory::Open(dir, &d).Ok());
    io::DirectoryEntry e;
    bool found = false;
    for (;;)
    {
        REQUIRE(d.Next(&e, &found).Ok());
        if (!found)
        {
            break;
        }
        CHECK(names.Insert(e.name));  // never twice
    }
    return names;
}

struct WalkLog
{
    engine_test::TextList seen;  // "relative/path" (+ "/" after children, + "!err" for errors)
    Path root;
    io::WalkAction on_dir = io::WalkAction::Continue;
    engine_test::Text stop_at = engine_test::MakeText();
    engine_test::Text skip_at = engine_test::MakeText();
};

io::WalkAction Collect(void* user, const io::WalkEntry& e)
{
    WalkLog* log = static_cast<WalkLog*>(user);
    Path rel(e.path.Style());
    REQUIRE(e.path.RelativeTo(log->root, &rel));
    engine_test::Text text = Str(rel.View());
    if (e.after_children)
    {
        text.Append('/');
    }
    if (e.error.Failed())
    {
        text.Append('!');
        text.Append(StringView(io::ErrorName(e.error.error)));
    }
    log->seen.Push(text.View());
    if (log->stop_at.Length() != 0 && text == log->stop_at)
    {
        return io::WalkAction::Stop;
    }
    if (log->skip_at.Length() != 0 && text == log->skip_at)
    {
        return io::WalkAction::SkipSubtree;
    }
    return io::WalkAction::Continue;
}

void MakeTree(TempDir& dir)
{
    REQUIRE(io::CreateDirectories(dir / "a/b/c").Ok());
    REQUIRE(io::CreateDirectories(dir / "z").Ok());
    REQUIRE(io_test::WriteText(dir / "a/one.txt", "1"));
    REQUIRE(io_test::WriteText(dir / "a/b/two.cpp", "2"));
    REQUIRE(io_test::WriteText(dir / "a/b/c/three.txt", "3"));
    REQUIRE(io_test::WriteText(dir / "top.txt", "t"));
}

}  // namespace

TEST_CASE("io::Directory: empty, entries, types and Info", "[io][dir]")
{
    TempDir dir;
    CHECK(Names(dir.Path()).Empty());
    bool empty = false;
    REQUIRE(io::Directory::IsEmpty(dir.Path(), &empty).Ok());
    CHECK(empty);
    REQUIRE(io_test::WriteText(dir / "f.txt", "hello"));
    REQUIRE(io::CreateDirectory(dir / "sub").Ok());
    const bool symlink = io_test::SymlinksAvailable();  // Windows: Developer Mode
    if (symlink)
    {
        REQUIRE(io_test::MakeSymlink("f.txt", dir / "ln"));
    }
    REQUIRE(io::Directory::IsEmpty(dir.Path(), &empty).Ok());
    CHECK_FALSE(empty);

    io::Directory d;
    REQUIRE(io::Directory::Open(dir.Path(), &d).Ok());
    CHECK(d.IsOpen());
    CHECK(d.GetPath() == dir.Path());
    engine_test::TextSet seen;
    io::DirectoryEntry e;
    bool found = false;
    for (;;)
    {
        REQUIRE(d.Next(&e, &found).Ok());
        if (!found)
        {
            break;
        }
        seen.Insert(e.name);
        CHECK(e.name != StringView("."));
        CHECK(e.name != StringView(".."));
        io::FileInfo info;
        REQUIRE(e.Info(&info).Ok());
        if (e.name == StringView("f.txt"))
        {
            CHECK(e.type == io::FileType::Regular);
            CHECK(info.size == 5);
        }
        else if (e.name == StringView("sub"))
        {
            CHECK(e.type == io::FileType::Directory);
        }
        else
        {
            CHECK(e.type == io::FileType::Symlink);
            CHECK(info.type == io::FileType::Symlink);
        }
    }
    if (symlink)
    {
        CHECK(seen.SameAs({"f.txt", "sub", "ln"}));
    }
    else
    {
        CHECK(seen.SameAs({"f.txt", "sub"}));
    }
    // Closing, moving and use after close.
    io::FileInfo before_move;
    io::Directory moved = static_cast<io::Directory&&>(d);
    CHECK(e.Info(&before_move).Ok());  // the entry follows the directory it came from
    CHECK_FALSE(d.IsOpen());
    CHECK(moved.IsOpen());
    moved.Close();
    moved.Close();
    CHECK(moved.Next(&e, &found).Is(Error::Invalid));
    // Errors.
    io::Directory x;
    CHECK(io::Directory::Open(dir / "missing", &x).Is(Error::NotFound));
    CHECK(io::Directory::Open(dir / "f.txt", &x).Is(Error::NotADirectory));
    CHECK_FALSE(x.IsOpen());
}

TEST_CASE("io::Directory: names with spaces, newlines and Unicode; many entries", "[io][dir]")
{
    TempDir dir;
#if ENGINE_PLATFORM_WINDOWS  // control characters and `"` are not allowed in Windows names
    const char* const odd[] = {"with space", "semi;colon", "日本語.txt", "ünï", ".hidden", "brace{}[]", "percent%20#"};
    for (const char* n : odd)
    {
        REQUIRE(io_test::WriteText(dir / StringView(n), "x"));
    }
    CHECK(Names(dir.Path())
              .SameAs({"with space", "semi;colon", "日本語.txt", "ünï", ".hidden", "brace{}[]", "percent%20#"}));
#else
    const char* const odd[] = {"with space", "new\nline", "日本語.txt", "ünï", ".hidden", "tab\there", "quote\"s"};
    for (const char* n : odd)
    {
        REQUIRE(io_test::WriteText(dir / StringView(n), "x"));
    }
    CHECK(
        Names(dir.Path()).SameAs({"with space", "new\nline", "日本語.txt", "ünï", ".hidden", "tab\there", "quote\"s"}));
#endif

    TempDir big;
    constexpr int kN = 10000;
    for (int i = 0; i < kN; ++i)
    {
        const engine_test::Text n = engine_test::Format("file%d", i);
        REQUIRE(io_test::WriteText(big / n.View(), ""));
    }
    CHECK(Names(big.Path()).Size() == static_cast<usize>(kN));
    Vector<io::DirectoryItem> items;
    REQUIRE(io::ListDirectory(big.Path(), true, &items).Ok());
    REQUIRE(items.Size() == static_cast<usize>(kN));
    for (usize i = 1; i < items.Size(); ++i)
    {
        CHECK(items[i - 1].name.View().Compare(items[i].name.View()) < 0);
    }
}

TEST_CASE("io::ListDirectory: sorted order is bytewise and stable across runs", "[io][dir]")
{
    TempDir dir;
    // No two names differ only in case: NTFS and APFS would make them one file (Phase 13 Day 152).
    for (const char* n : {"b", "a", "C", "10", "9", "_x", "é", "Z"})
    {
        REQUIRE(io_test::WriteText(dir / n, ""));
    }
    Vector<io::DirectoryItem> items;
    REQUIRE(io::ListDirectory(dir.Path(), true, &items).Ok());
    engine_test::TextList got;
    for (usize i = 0; i < items.Size(); ++i)
    {
        got.Push(items[i].name.View());
    }
    CHECK(got.SameAs({"10", "9", "C", "Z", "_x", "a", "b", "é"}));
    CHECK(items[0].type == io::FileType::Regular);
    Vector<io::DirectoryItem> unsorted;
    REQUIRE(io::ListDirectory(dir.Path(), false, &unsorted).Ok());
    CHECK(unsorted.Size() == 8);
    CHECK(io::ListDirectory(dir / "nope", true, &items).Is(Error::NotFound));
    CHECK(items.Size() == 8);  // unchanged on failure
}

TEST_CASE("io::Directory: entries added or removed during iteration", "[io][dir]")
{
    TempDir dir;
    for (int i = 0; i < 200; ++i)
    {
        const engine_test::Text n = engine_test::Format("e%d", i);
        REQUIRE(io_test::WriteText(dir / n.View(), ""));
    }
    io::Directory d;
    REQUIRE(io::Directory::Open(dir.Path(), &d).Ok());
    io::DirectoryEntry e;
    bool found = false;
    engine_test::TextSet seen;
    int step = 0;
    for (;;)
    {
        REQUIRE(d.Next(&e, &found).Ok());
        if (!found)
        {
            break;
        }
        CHECK(seen.Insert(e.name));  // an entry is never returned twice
        if (++step == 10)
        {
            REQUIRE(io_test::WriteText(dir / "added1", ""));
            REQUIRE(io::Remove(dir / "e199").Ok());
            REQUIRE(io::Remove(dir / "e198").Ok());
        }
        io::FileInfo info;
        const io::Status s = e.Info(&info);  // an entry removed since it was listed
        CHECK((s.Ok() || s.Is(Error::NotFound)));
    }
    CHECK(seen.Size() >= 198);
}

TEST_CASE("io::MatchesWildcard", "[io][dir]")
{
    struct Row
    {
        const char* pattern;
        const char* name;
        bool match;
    };
    const Row rows[] = {
        {"", "", true},
        {"", "a", false},
        {"*", "", true},
        {"*", "anything.txt", true},
        {"*", ".hidden", true},
        {"a*", "a", true},
        {"a*", "abc", true},
        {"a*", "ba", false},
        {"*a", "ba", true},
        {"*a", "ab", false},
        {"*.txt", "notes.txt", true},
        {"*.txt", "notes.txt.bak", false},
        {"*.txt", ".txt", true},
        {"a*b*c", "aXXbYYc", true},
        {"a*b*c", "aXXbYY", false},
        {"a**b", "ab", true},
        {"*a*", "bab", true},
        {"*ab", "aab", true},
        {"*ab*ab", "abab", true},
        {"?", "a", true},
        {"?", "", false},
        {"?", "ab", false},
        {"??", "ab", true},
        {"?.txt", "a.txt", true},
        {"?", "é", true},
        {"?", "日", true},
        {"??", "日本", true},
        {"?", "日本", false},
        {"日*", "日本語", true},
        {"\\*", "*", true},
        {"\\*", "a", false},
        {"a\\?b", "a?b", true},
        {"a\\?b", "axb", false},
        {"\\\\", "\\", true},
        {"abc", "abc", true},
        {"abc", "abd", false},
        {"abc", "ABC", false},
        {"a?c*", "abcdef", true},
        {"*?", "", false},
        {"*?", "x", true},
        {"x*?", "x", false},
        {"a\\", "a\\", true},
    };
    for (const Row& r : rows)
    {
        INFO("pattern '" << r.pattern << "' name '" << r.name << "'");
        CHECK(io::MatchesWildcard(r.pattern, r.name) == r.match);
    }
    // A pathological pattern finishes quickly (no exponential backtracking).
    const engine_test::Text name = engine_test::Repeated(2000, 'a');
    CHECK_FALSE(io::MatchesWildcard("*a*a*a*a*a*a*a*a*b", name.View()));
}

TEST_CASE("io::WalkDirectory: order, depth, filter, skipping and stopping", "[io][dir][walk]")
{
    TempDir dir;
    MakeTree(dir);
    io::WalkOptions o;
    o.sorted = true;
    WalkLog log;
    log.root = dir.Path();
    REQUIRE(io::WalkDirectory(dir.Path(), o, &Collect, &log).Ok());
    CHECK(log.seen.SameAs({"a", "a/b", "a/b/c", "a/b/c/three.txt", "a/b/two.cpp", "a/one.txt", "top.txt", "z"}));

    // Post-order: a directory is reported after its children.
    log.seen.Clear();
    o.post_order = true;
    REQUIRE(io::WalkDirectory(dir.Path(), o, &Collect, &log).Ok());
    CHECK(log.seen.SameAs({"a/b/c/three.txt", "a/b/c/", "a/b/two.cpp", "a/b/", "a/one.txt", "a/", "top.txt", "z/"}));

    // Depth limit.
    o.post_order = false;
    o.max_depth = 1;
    log.seen.Clear();
    REQUIRE(io::WalkDirectory(dir.Path(), o, &Collect, &log).Ok());
    CHECK(log.seen.SameAs({"a", "top.txt", "z"}));
    o.max_depth = 2;
    log.seen.Clear();
    REQUIRE(io::WalkDirectory(dir.Path(), o, &Collect, &log).Ok());
    CHECK(log.seen.SameAs({"a", "a/b", "a/one.txt", "top.txt", "z"}));

    // Name filter: matching entries are reported, directories are still entered.
    o.max_depth = 32;
    o.name_pattern = "*.txt";
    log.seen.Clear();
    REQUIRE(io::WalkDirectory(dir.Path(), o, &Collect, &log).Ok());
    CHECK(log.seen.SameAs({"a/b/c/three.txt", "a/one.txt", "top.txt"}));
    o.name_pattern = StringView();

    // Skipping a subtree and stopping.
    log.seen.Clear();
    log.skip_at = engine_test::MakeText("a/b");
    REQUIRE(io::WalkDirectory(dir.Path(), o, &Collect, &log).Ok());
    CHECK(log.seen.SameAs({"a", "a/b", "a/one.txt", "top.txt", "z"}));
    log.skip_at = engine_test::MakeText();
    log.seen.Clear();
    log.stop_at = engine_test::MakeText("a/b/two.cpp");
    REQUIRE(io::WalkDirectory(dir.Path(), o, &Collect, &log).Ok());
    CHECK(log.seen.Back() == "a/b/two.cpp");
    CHECK(log.seen.Size() == 5);

    // The same walk without sorting sees the same set.
    o.sorted = false;
    log.stop_at = engine_test::MakeText();
    log.seen.Clear();
    REQUIRE(io::WalkDirectory(dir.Path(), o, &Collect, &log).Ok());
    {
        engine_test::TextSet unique;
        for (usize i = 0; i < log.seen.Size(); ++i)
        {
            unique.Insert(log.seen[i].View());
        }
        CHECK(unique.Size() == 8);
    }

    // Errors.
    CHECK(io::WalkDirectory(dir / "nope", o, &Collect, &log).Is(Error::NotFound));
    CHECK(io::WalkDirectory(dir / "top.txt", o, &Collect, &log).Is(Error::NotADirectory));
    CHECK(io::WalkDirectory(dir.Path(), o, nullptr, &log).Is(Error::Invalid));
    o.max_depth = 500;
    CHECK(io::WalkDirectory(dir.Path(), o, &Collect, &log).Is(Error::Invalid));
}

TEST_CASE("io::WalkDirectory: symbolic links and loops", "[io][dir][walk]")
{
    if (!io_test::SymlinksAvailable())
    {
        WARN("skipped: this process cannot create symbolic links (Windows: Developer Mode)");
        return;
    }
    TempDir dir;
    REQUIRE(io::CreateDirectories(dir / "real/inner").Ok());
    REQUIRE(io_test::WriteText(dir / "real/inner/f.txt", "x"));
    REQUIRE(io_test::MakeSymlink("real", dir / "lnk", true));          // link to a directory
    REQUIRE(io_test::MakeSymlink("real/inner/f.txt", dir / "flink"));  // link to a file
    REQUIRE(io_test::MakeSymlink("..", dir / "real/inner/up", true));  // link back to an ancestor
    io::WalkOptions o;
    o.sorted = true;
    WalkLog log;
    log.root = dir.Path();
    // Links are reported but not followed by default.
    REQUIRE(io::WalkDirectory(dir.Path(), o, &Collect, &log).Ok());
    CHECK(log.seen.SameAs({"flink", "lnk", "real", "real/inner", "real/inner/f.txt", "real/inner/up"}));
    // Following: the directory link is walked, and the link to an ancestor is reported as a loop, once, without
    // descending.
    o.follow_symlinks = true;
    log.seen.Clear();
    REQUIRE(io::WalkDirectory(dir.Path(), o, &Collect, &log).Ok());
    int loops = 0;
    for (usize i = 0; i < log.seen.Size(); ++i)
    {
        loops += log.seen[i].View().Contains("!symlink_loop") ? 1 : 0;
    }
    CHECK(loops >= 1);
    for (usize i = 0; i < log.seen.Size(); ++i)
    {
        CHECK(log.seen.Count(log.seen[i].View()) == 1);  // every entry once
    }
    CHECK(log.seen.Count("lnk/inner/f.txt") >= 1);
    CHECK(log.seen.Size() < 40);  // finite
}

TEST_CASE("io::WalkDirectory: an unreadable subdirectory is reported and the walk goes on", "[io][dir][walk]")
{
    if (io_test::RunningAsRoot())
    {
        SUCCEED("root reads everything");
        return;
    }
    TempDir dir;
    MakeTree(dir);
    REQUIRE(io_test::DenyListing(dir / "a/b"));
    io::WalkOptions o;
    o.sorted = true;
    WalkLog log;
    log.root = dir.Path();
    REQUIRE(io::WalkDirectory(dir.Path(), o, &Collect, &log).Ok());
    int reported = 0;
    for (usize i = 0; i < log.seen.Size(); ++i)
    {
        reported += log.seen[i].View().StartsWith("a/b") && log.seen[i].View().Contains("!permission_denied") ? 1 : 0;
    }
    CHECK(reported == 1);  // reported once, with the error
    int plain = 0;
    plain = static_cast<int>(log.seen.Count("a/b"));
    CHECK(plain == 0);  // not also reported without it
    // Post-order reports it once as well.
    o.post_order = true;
    log.seen.Clear();
    REQUIRE(io::WalkDirectory(dir.Path(), o, &Collect, &log).Ok());
    int post = 0;
    for (usize i = 0; i < log.seen.Size(); ++i)
    {
        post += log.seen[i].View().StartsWith("a/b") && log.seen[i].View().Contains("!") ? 1 : 0;
    }
    CHECK(post == 1);
    o.post_order = false;
    REQUIRE(io::WalkDirectory(dir.Path(), o, &Collect, &log).Ok());
    CHECK(log.seen.Back() == "z");  // later entries were still visited
    REQUIRE(io_test::AllowAll(dir / "a/b"));
}

TEST_CASE("io::Directory: the creation and removal statics", "[io][dir]")
{
    TempDir dir;
    REQUIRE(io::Directory::CreateAll(dir / "x/y/z").Ok());
    CHECK(io::Directory::Create(dir / "x/y/z").Is(Error::AlreadyExists));
    CHECK(io::Directory::Remove(dir / "x").Is(Error::NotEmpty));
    REQUIRE(io::Directory::RemoveAll(dir / "x").Ok());
    CHECK_FALSE(io::Exists(dir / "x"));
    bool empty = true;
    CHECK(io::Directory::IsEmpty(dir / "nope", &empty).Is(Error::NotFound));
}
