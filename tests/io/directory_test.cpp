#include <catch2/catch_test_macros.hpp>

#include <set>
#include <string>
#include <unistd.h>
#include <vector>

#include "io/directory.h"
#include "temp_dir.h"

using namespace engine_placeholder;
using io::Error;
using io::Path;
using io_test::Str;
using io_test::TempDir;

namespace {

std::set<std::string> Names(const Path& dir) {
    std::set<std::string> names;
    io::Directory d;
    REQUIRE(io::Directory::Open(dir, &d).Ok());
    io::DirectoryEntry e;
    bool found = false;
    for (;;) {
        REQUIRE(d.Next(&e, &found).Ok());
        if (!found) break;
        CHECK(names.insert(Str(e.name)).second); // never twice
    }
    return names;
}

struct WalkLog {
    std::vector<std::string> seen; // "relative/path" (+ "/" after children, + "!err" for errors)
    Path root;
    io::WalkAction on_dir = io::WalkAction::Continue;
    std::string stop_at;
    std::string skip_at;
};

io::WalkAction Collect(void* user, const io::WalkEntry& e) {
    WalkLog* log = static_cast<WalkLog*>(user);
    Path rel(e.path.Style());
    REQUIRE(e.path.RelativeTo(log->root, &rel));
    std::string text = Str(rel.View());
    if (e.after_children) text += "/";
    if (e.error.Failed()) text += std::string("!") + io::ErrorName(e.error.error);
    log->seen.push_back(text);
    if (!log->stop_at.empty() && text == log->stop_at) return io::WalkAction::Stop;
    if (!log->skip_at.empty() && text == log->skip_at) return io::WalkAction::SkipSubtree;
    return io::WalkAction::Continue;
}

void MakeTree(TempDir& dir) {
    REQUIRE(io::CreateDirectories(dir / "a/b/c").Ok());
    REQUIRE(io::CreateDirectories(dir / "z").Ok());
    REQUIRE(io_test::WriteText(dir / "a/one.txt", "1"));
    REQUIRE(io_test::WriteText(dir / "a/b/two.cpp", "2"));
    REQUIRE(io_test::WriteText(dir / "a/b/c/three.txt", "3"));
    REQUIRE(io_test::WriteText(dir / "top.txt", "t"));
}

} // namespace

TEST_CASE("io::Directory: empty, entries, types and Info", "[io][dir]") {
    TempDir dir;
    CHECK(Names(dir.Path()).empty());
    bool empty = false;
    REQUIRE(io::Directory::IsEmpty(dir.Path(), &empty).Ok());
    CHECK(empty);
    REQUIRE(io_test::WriteText(dir / "f.txt", "hello"));
    REQUIRE(io::CreateDirectory(dir / "sub").Ok());
    REQUIRE(::symlink("f.txt", Str((dir / "ln").View()).c_str()) == 0);
    REQUIRE(io::Directory::IsEmpty(dir.Path(), &empty).Ok());
    CHECK_FALSE(empty);

    io::Directory d;
    REQUIRE(io::Directory::Open(dir.Path(), &d).Ok());
    CHECK(d.IsOpen());
    CHECK(d.GetPath() == dir.Path());
    std::set<std::string> seen;
    io::DirectoryEntry e;
    bool found = false;
    for (;;) {
        REQUIRE(d.Next(&e, &found).Ok());
        if (!found) break;
        seen.insert(Str(e.name));
        CHECK(e.name != StringView("."));
        CHECK(e.name != StringView(".."));
        io::FileInfo info;
        REQUIRE(e.Info(&info).Ok());
        if (e.name == StringView("f.txt")) {
            CHECK(e.type == io::FileType::Regular);
            CHECK(info.size == 5);
        } else if (e.name == StringView("sub")) {
            CHECK(e.type == io::FileType::Directory);
        } else {
            CHECK(e.type == io::FileType::Symlink);
            CHECK(info.type == io::FileType::Symlink);
        }
    }
    CHECK(seen == std::set<std::string>{"f.txt", "sub", "ln"});
    // Closing, moving and use after close.
    io::FileInfo before_move;
    io::Directory moved = static_cast<io::Directory&&>(d);
    CHECK(e.Info(&before_move).Ok()); // the entry follows the directory it came from
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

TEST_CASE("io::Directory: names with spaces, newlines and Unicode; many entries", "[io][dir]") {
    TempDir dir;
    const std::vector<std::string> odd = {"with space", "new\nline", "日本語.txt", "ünï", ".hidden", "tab\there", "quote\"s"};
    for (const std::string& n : odd) REQUIRE(io_test::WriteText(dir / StringView(n.data(), n.size()), "x"));
    CHECK(Names(dir.Path()) == std::set<std::string>(odd.begin(), odd.end()));

    TempDir big;
    constexpr int kN = 10000;
    for (int i = 0; i < kN; ++i) {
        const std::string n = "file" + std::to_string(i);
        REQUIRE(io_test::WriteText(big / StringView(n.data(), n.size()), ""));
    }
    CHECK(Names(big.Path()).size() == static_cast<size_t>(kN));
    Vector<io::DirectoryItem> items;
    REQUIRE(io::ListDirectory(big.Path(), true, &items).Ok());
    REQUIRE(items.Size() == static_cast<usize>(kN));
    for (usize i = 1; i < items.Size(); ++i) CHECK(items[i - 1].name.View().Compare(items[i].name.View()) < 0);
}

TEST_CASE("io::ListDirectory: sorted order is bytewise and stable across runs", "[io][dir]") {
    TempDir dir;
    for (const char* n : {"b", "a", "B", "10", "9", "_x", "é", "Z"}) REQUIRE(io_test::WriteText(dir / n, ""));
    Vector<io::DirectoryItem> items;
    REQUIRE(io::ListDirectory(dir.Path(), true, &items).Ok());
    std::vector<std::string> got;
    for (usize i = 0; i < items.Size(); ++i) got.push_back(Str(items[i].name.View()));
    CHECK(got == std::vector<std::string>{"10", "9", "B", "Z", "_x", "a", "b", "é"});
    CHECK(items[0].type == io::FileType::Regular);
    Vector<io::DirectoryItem> unsorted;
    REQUIRE(io::ListDirectory(dir.Path(), false, &unsorted).Ok());
    CHECK(unsorted.Size() == 8);
    CHECK(io::ListDirectory(dir / "nope", true, &items).Is(Error::NotFound));
    CHECK(items.Size() == 8); // unchanged on failure
}

TEST_CASE("io::Directory: entries added or removed during iteration", "[io][dir]") {
    TempDir dir;
    for (int i = 0; i < 200; ++i) {
        const std::string n = "e" + std::to_string(i);
        REQUIRE(io_test::WriteText(dir / StringView(n.data(), n.size()), ""));
    }
    io::Directory d;
    REQUIRE(io::Directory::Open(dir.Path(), &d).Ok());
    io::DirectoryEntry e;
    bool found = false;
    std::set<std::string> seen;
    int step = 0;
    for (;;) {
        REQUIRE(d.Next(&e, &found).Ok());
        if (!found) break;
        CHECK(seen.insert(Str(e.name)).second); // an entry is never returned twice
        if (++step == 10) {
            REQUIRE(io_test::WriteText(dir / "added1", ""));
            REQUIRE(io::Remove(dir / "e199").Ok());
            REQUIRE(io::Remove(dir / "e198").Ok());
        }
        io::FileInfo info;
        const io::Status s = e.Info(&info); // an entry removed since it was listed
        CHECK((s.Ok() || s.Is(Error::NotFound)));
    }
    CHECK(seen.size() >= 198);
}

TEST_CASE("io::MatchesWildcard", "[io][dir]") {
    struct Row { const char* pattern; const char* name; bool match; };
    const Row rows[] = {
        {"", "", true}, {"", "a", false}, {"*", "", true}, {"*", "anything.txt", true}, {"*", ".hidden", true},
        {"a*", "a", true}, {"a*", "abc", true}, {"a*", "ba", false}, {"*a", "ba", true}, {"*a", "ab", false},
        {"*.txt", "notes.txt", true}, {"*.txt", "notes.txt.bak", false}, {"*.txt", ".txt", true}, {"a*b*c", "aXXbYYc", true},
        {"a*b*c", "aXXbYY", false}, {"a**b", "ab", true}, {"*a*", "bab", true}, {"*ab", "aab", true}, {"*ab*ab", "abab", true},
        {"?", "a", true}, {"?", "", false}, {"?", "ab", false}, {"??", "ab", true}, {"?.txt", "a.txt", true},
        {"?", "é", true}, {"?", "日", true}, {"??", "日本", true}, {"?", "日本", false}, {"日*", "日本語", true},
        {"\\*", "*", true}, {"\\*", "a", false}, {"a\\?b", "a?b", true}, {"a\\?b", "axb", false}, {"\\\\", "\\", true},
        {"abc", "abc", true}, {"abc", "abd", false}, {"abc", "ABC", false}, {"a?c*", "abcdef", true}, {"*?", "", false},
        {"*?", "x", true}, {"x*?", "x", false}, {"a\\", "a\\", true},
    };
    for (const Row& r : rows) {
        INFO("pattern '" << r.pattern << "' name '" << r.name << "'");
        CHECK(io::MatchesWildcard(r.pattern, r.name) == r.match);
    }
    // A pathological pattern finishes quickly (no exponential backtracking).
    std::string name(2000, 'a');
    CHECK_FALSE(io::MatchesWildcard("*a*a*a*a*a*a*a*a*b", StringView(name.data(), name.size())));
}

TEST_CASE("io::WalkDirectory: order, depth, filter, skipping and stopping", "[io][dir][walk]") {
    TempDir dir;
    MakeTree(dir);
    io::WalkOptions o;
    o.sorted = true;
    WalkLog log;
    log.root = dir.Path();
    REQUIRE(io::WalkDirectory(dir.Path(), o, &Collect, &log).Ok());
    CHECK(log.seen == std::vector<std::string>{"a", "a/b", "a/b/c", "a/b/c/three.txt", "a/b/two.cpp", "a/one.txt", "top.txt", "z"});

    // Post-order: a directory is reported after its children.
    log.seen.clear();
    o.post_order = true;
    REQUIRE(io::WalkDirectory(dir.Path(), o, &Collect, &log).Ok());
    CHECK(log.seen == std::vector<std::string>{"a/b/c/three.txt", "a/b/c/", "a/b/two.cpp", "a/b/", "a/one.txt", "a/", "top.txt", "z/"});

    // Depth limit.
    o.post_order = false;
    o.max_depth = 1;
    log.seen.clear();
    REQUIRE(io::WalkDirectory(dir.Path(), o, &Collect, &log).Ok());
    CHECK(log.seen == std::vector<std::string>{"a", "top.txt", "z"});
    o.max_depth = 2;
    log.seen.clear();
    REQUIRE(io::WalkDirectory(dir.Path(), o, &Collect, &log).Ok());
    CHECK(log.seen == std::vector<std::string>{"a", "a/b", "a/one.txt", "top.txt", "z"});

    // Name filter: matching entries are reported, directories are still entered.
    o.max_depth = 32;
    o.name_pattern = "*.txt";
    log.seen.clear();
    REQUIRE(io::WalkDirectory(dir.Path(), o, &Collect, &log).Ok());
    CHECK(log.seen == std::vector<std::string>{"a/b/c/three.txt", "a/one.txt", "top.txt"});
    o.name_pattern = StringView();

    // Skipping a subtree and stopping.
    log.seen.clear();
    log.skip_at = "a/b";
    REQUIRE(io::WalkDirectory(dir.Path(), o, &Collect, &log).Ok());
    CHECK(log.seen == std::vector<std::string>{"a", "a/b", "a/one.txt", "top.txt", "z"});
    log.skip_at.clear();
    log.seen.clear();
    log.stop_at = "a/b/two.cpp";
    REQUIRE(io::WalkDirectory(dir.Path(), o, &Collect, &log).Ok());
    CHECK(log.seen.back() == "a/b/two.cpp");
    CHECK(log.seen.size() == 5);

    // The same walk without sorting sees the same set.
    o.sorted = false;
    log.stop_at.clear();
    log.seen.clear();
    REQUIRE(io::WalkDirectory(dir.Path(), o, &Collect, &log).Ok());
    CHECK(std::set<std::string>(log.seen.begin(), log.seen.end()).size() == 8);

    // Errors.
    CHECK(io::WalkDirectory(dir / "nope", o, &Collect, &log).Is(Error::NotFound));
    CHECK(io::WalkDirectory(dir / "top.txt", o, &Collect, &log).Is(Error::NotADirectory));
    CHECK(io::WalkDirectory(dir.Path(), o, nullptr, &log).Is(Error::Invalid));
    o.max_depth = 500;
    CHECK(io::WalkDirectory(dir.Path(), o, &Collect, &log).Is(Error::Invalid));
}

TEST_CASE("io::WalkDirectory: symbolic links and loops", "[io][dir][walk]") {
    TempDir dir;
    REQUIRE(io::CreateDirectories(dir / "real/inner").Ok());
    REQUIRE(io_test::WriteText(dir / "real/inner/f.txt", "x"));
    REQUIRE(::symlink("real", Str((dir / "lnk").View()).c_str()) == 0);            // link to a directory
    REQUIRE(::symlink("real/inner/f.txt", Str((dir / "flink").View()).c_str()) == 0); // link to a file
    REQUIRE(::symlink("..", Str((dir / "real/inner/up").View()).c_str()) == 0);        // link back to an ancestor
    io::WalkOptions o;
    o.sorted = true;
    WalkLog log;
    log.root = dir.Path();
    // Links are reported but not followed by default.
    REQUIRE(io::WalkDirectory(dir.Path(), o, &Collect, &log).Ok());
    CHECK(log.seen == std::vector<std::string>{"flink", "lnk", "real", "real/inner", "real/inner/f.txt", "real/inner/up"});
    // Following: the directory link is walked, and the link to an ancestor is reported as a loop, once, without descending.
    o.follow_symlinks = true;
    log.seen.clear();
    REQUIRE(io::WalkDirectory(dir.Path(), o, &Collect, &log).Ok());
    int loops = 0;
    for (const std::string& s : log.seen) loops += s.find("!symlink_loop") != std::string::npos;
    CHECK(loops >= 1);
    for (const std::string& s : log.seen) { // every entry once
        int count = 0;
        for (const std::string& t : log.seen) count += t == s;
        CHECK(count == 1);
    }
    bool through_link = false;
    for (const std::string& s : log.seen) through_link = through_link || s == "lnk/inner/f.txt";
    CHECK(through_link);
    CHECK(log.seen.size() < 40); // finite
}

TEST_CASE("io::WalkDirectory: an unreadable subdirectory is reported and the walk goes on", "[io][dir][walk]") {
    if (::geteuid() == 0) {
        SUCCEED("root reads everything");
        return;
    }
    TempDir dir;
    MakeTree(dir);
    REQUIRE(::chmod(Str((dir / "a/b").View()).c_str(), 0000) == 0);
    io::WalkOptions o;
    o.sorted = true;
    WalkLog log;
    log.root = dir.Path();
    REQUIRE(io::WalkDirectory(dir.Path(), o, &Collect, &log).Ok());
    int reported = 0;
    for (const std::string& s : log.seen) reported += s.rfind("a/b", 0) == 0 && s.find("!permission_denied") != std::string::npos;
    CHECK(reported == 1); // reported once, with the error
    int plain = 0;
    for (const std::string& s : log.seen) plain += s == "a/b";
    CHECK(plain == 0); // not also reported without it
    // Post-order reports it once as well.
    o.post_order = true;
    log.seen.clear();
    REQUIRE(io::WalkDirectory(dir.Path(), o, &Collect, &log).Ok());
    int post = 0;
    for (const std::string& s : log.seen) post += s.rfind("a/b", 0) == 0 && s.find("!") != std::string::npos;
    CHECK(post == 1);
    o.post_order = false;
    REQUIRE(io::WalkDirectory(dir.Path(), o, &Collect, &log).Ok());
    CHECK(log.seen.back() == "z"); // later entries were still visited
    REQUIRE(::chmod(Str((dir / "a/b").View()).c_str(), 0755) == 0);
}

TEST_CASE("io::Directory: the creation and removal statics", "[io][dir]") {
    TempDir dir;
    REQUIRE(io::Directory::CreateAll(dir / "x/y/z").Ok());
    CHECK(io::Directory::Create(dir / "x/y/z").Is(Error::AlreadyExists));
    CHECK(io::Directory::Remove(dir / "x").Is(Error::NotEmpty));
    REQUIRE(io::Directory::RemoveAll(dir / "x").Ok());
    CHECK_FALSE(io::Exists(dir / "x"));
    bool empty = true;
    CHECK(io::Directory::IsEmpty(dir / "nope", &empty).Is(Error::NotFound));
}
