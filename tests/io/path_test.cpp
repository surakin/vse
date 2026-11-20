#include <catch2/catch_test_macros.hpp>

#include "io/path.h"
#include "test_text.h"
#include "temp_dir.h"

using io_test::Str;
using vse::StringView;
using vse::usize;
using vse::io::kMaxPathBytes;
using vse::io::Path;
using vse::io::PathBuilder;
using vse::io::PathStyle;

namespace
{

struct PathRow
{
    const char* input;
    bool absolute;
    const char* normalize;
    const char* parent;
    const char* file_name;
    const char* stem;
    const char* extension;
};
struct JoinRow
{
    const char* a;
    const char* b;
    const char* result;
};
struct RelRow
{
    const char* path;
    const char* base;
    bool ok;
    const char* result;
};

#include "path_table.inc"

template <usize N> void CheckRows(const PathRow (&rows)[N], PathStyle style)
{
    for (const PathRow& r : rows)
    {
        INFO("input: " << r.input);
        Path p(r.input, style);
        CHECK(p.IsAbsolute() == r.absolute);
        CHECK(Str(p.Normalize().View()) == r.normalize);
        CHECK(Str(p.Parent().View()) == r.parent);
        CHECK(Str(p.FileName()) == r.file_name);
        CHECK(Str(p.Stem()) == r.stem);
        CHECK(Str(p.Extension()) == r.extension);
    }
}

template <usize N> void CheckJoins(const JoinRow (&rows)[N], PathStyle style)
{
    for (const JoinRow& r : rows)
    {
        INFO("join: " << r.a << " , " << r.b);
        CHECK(Str(Path(r.a, style).Join(Path(r.b, style)).View()) == r.result);
    }
}

template <usize N> void CheckRels(const RelRow (&rows)[N], PathStyle style)
{
    for (const RelRow& r : rows)
    {
        INFO("relative: " << r.path << " to " << r.base);
        Path out(style);
        const bool ok = Path(r.path, style).RelativeTo(Path(r.base, style), &out);
        CHECK(ok == r.ok);
        if (ok)
        {
            CHECK(Str(out.View()) == r.result);
        }
    }
}

struct Lcg
{
    unsigned long long s;
    unsigned Next()
    {
        s = s * 6364136223846793005ull + 1442695040888963407ull;
        return static_cast<unsigned>(s >> 33);
    }
};

engine_test::Text RandomRelative(Lcg& rng, bool allow_dots)
{
    static const char* names[] = {"a", "b", "cc", "d.txt", "e.tar.gz", ".hid", "..x"};
    engine_test::Text s = engine_test::MakeText();
    const unsigned count = 1 + rng.Next() % 5;
    for (unsigned i = 0; i < count; ++i)
    {
        if (i > 0)
        {
            s.Append('/');
        }
        const unsigned pick = rng.Next() % (allow_dots ? 10u : 7u);
        s.Append(StringView(pick < 7 ? names[pick] : (pick == 7 ? "." : "..")));
    }
    return s;
}

}  // namespace

TEST_CASE("Path: the Spike C table, POSIX style", "[io][path]")
{
    CheckRows(kPosixRows, PathStyle::Posix);
}
TEST_CASE("Path: the Spike C table, Windows style (parsed on any host)", "[io][path]")
{
    CheckRows(kWindowsRows, PathStyle::Windows);
}
TEST_CASE("Path: Join table", "[io][path]")
{
    CheckJoins(kPosixJoins, PathStyle::Posix);
    CheckJoins(kWindowsJoins, PathStyle::Windows);
}
TEST_CASE("Path: RelativeTo table", "[io][path]")
{
    CheckRels(kPosixRels, PathStyle::Posix);
    CheckRels(kWindowsRels, PathStyle::Windows);
}

TEST_CASE("Path: canonical form, native form and the empty path", "[io][path]")
{
    Path w("C:\\a\\\\b\\", PathStyle::Windows);
    CHECK(Str(w.View()) == "C:/a/b");
    char native[32];
    REQUIRE(w.ToNative(native, sizeof(native)));
    CHECK(StringView(native) == StringView("C:\\a\\b"));
    char tiny[4];
    CHECK_FALSE(w.ToNative(tiny, sizeof(tiny)));

    Path posix("a\\b", PathStyle::Posix);  // a backslash is a name character on POSIX
    CHECK(Str(posix.FileName()) == "a\\b");

    Path empty;
    CHECK(empty.IsEmpty());
    CHECK(empty.IsRelative());
    CHECK(Str(empty.Normalize().View()) == ".");
    CHECK(empty != Path("."));
}

TEST_CASE("Path: root name, components and comparison", "[io][path]")
{
    Path unc("\\\\server\\share\\dir\\f.txt", PathStyle::Windows);
    CHECK(Str(unc.RootName()) == "//server/share");
    Path::Components c = unc.GetComponents();
    StringView part;
    REQUIRE(c.Next(&part));
    CHECK(Str(part) == "dir");
    REQUIRE(c.Next(&part));
    CHECK(Str(part) == "f.txt");
    CHECK_FALSE(c.Next(&part));

    CHECK(Path("C:/Dir/File", PathStyle::Windows) == Path("c:\\dir\\FILE", PathStyle::Windows));
    CHECK(Path("/Dir", PathStyle::Posix) != Path("/dir", PathStyle::Posix));
    CHECK(Path("C:/Dir", PathStyle::Windows).Hash() == Path("c:/dIR", PathStyle::Windows).Hash());
    CHECK(Path("a/b", PathStyle::Posix) <
          Path("a b", PathStyle::Posix));  // ordered by component: "a" < "a b" (bytewise, "a b" < "a/b")
    CHECK(Path("ÄB", PathStyle::Windows) != Path("äb", PathStyle::Windows));  // non-ASCII is not case-folded

    CHECK(Path("/a/b/c").StartsWith(Path("/a/b")));
    CHECK_FALSE(Path("/a/bc").StartsWith(Path("/a/b")));
    CHECK(Path("a/b").IsInside(Path("a")));
    CHECK_FALSE(Path("a/../b").IsInside(Path("a")));
    CHECK_FALSE(Path("/x").IsInside(Path("x")));
}

TEST_CASE("Path: builders", "[io][path]")
{
    CHECK(Str(Path("a/b.txt").WithFileName("c.md").View()) == "a/c.md");
    CHECK(Str(Path("/").WithFileName("c").View()) == "/c");
    CHECK(Str(Path("a/b.txt").WithExtension("md").View()) == "a/b.md");
    CHECK(Str(Path("a/b.txt").WithExtension(".md").View()) == "a/b.md");
    CHECK(Str(Path("a/b").WithExtension("md").View()) == "a/b.md");
    CHECK(Str(Path("a/b.txt").WithExtension("").View()) == "a/b");
    CHECK(Str(Path("a/.gitignore").WithExtension("x").View()) == "a/.gitignore.x");
    CHECK(Str(Path("a/b.tar.gz").WithoutExtension().View()) == "a/b.tar");
    CHECK(Str(Path("a/b").WithoutExtension().View()) == "a/b");
    Path p("a");
    p.Append("b/c");
    CHECK(Str(p.View()) == "a/b/c");
    CHECK(Str((Path("x") / Path("y") / "z").View()) == "x/y/z");
}

TEST_CASE("Path: length limits", "[io][path]")
{
    Path out;
    const engine_test::Text max = engine_test::Repeated(kMaxPathBytes, 'x');
    REQUIRE(Path::Parse(max.View(), PathStyle::Posix, &out));
    CHECK(out.View().Length() == kMaxPathBytes);
    const engine_test::Text over = engine_test::Repeated(kMaxPathBytes + 1, 'x');
    Path keep("keep");
    CHECK_FALSE(Path::Parse(over.View(), PathStyle::Posix, &keep));
    CHECK(Str(keep.View()) == "keep");  // unchanged on failure
    CHECK_FALSE(Path::Parse(StringView("a\0b", 3), PathStyle::Posix, &keep));
    // Redundant separators do not count against the limit: the canonical form is what is measured.
    engine_test::Text slashes = engine_test::Repeated(kMaxPathBytes + 100, '/');
    slashes.Append('a');
    REQUIRE(Path::Parse(slashes.View(), PathStyle::Posix, &out));
    CHECK(Str(out.View()) == "/a");
    // TryJoin reports a too-long result and leaves out untouched.
    const engine_test::Text half = engine_test::Repeated(kMaxPathBytes - 2, 'x');
    Path left(half.View());
    Path right("yyyy");
    Path result("untouched");
    CHECK_FALSE(left.TryJoin(right, &result));
    CHECK(Str(result.View()) == "untouched");
    // A single name of 255 bytes round-trips through every lexical operation.
    const engine_test::Text name = engine_test::Repeated(255, 'n');
    Path longname(name.View());
    CHECK(Str(longname.FileName()) == name.View());
    CHECK(Str(longname.Normalize().View()) == name.View());
    CHECK(longname.Parent().IsEmpty());
}

TEST_CASE("Path: properties over random paths", "[io][path]")
{
    Lcg rng{12345};
    for (int i = 0; i < 1000; ++i)
    {
        for (PathStyle style : {PathStyle::Posix, PathStyle::Windows})
        {
            const engine_test::Text text = RandomRelative(rng, true);
            Path p(text.View(), style);
            INFO("path: " << text.CStr());
            // Normalize is idempotent.
            const Path n = p.Normalize();
            CHECK(n.Normalize() == n);
            // Parent(Join(a, b)) == a for a relative single component b that is not "." or "..".
            const engine_test::Text name = RandomRelative(rng, false);
            const StringView single = name.View().Substring(0, name.View().IndexOf('/'));
            Path joined = p.Join(single);
            CHECK(joined.Parent() == p);
            CHECK(Str(joined.FileName()) == single);
            // Under a base, RelativeTo then Join returns the (normalized) original.
            const engine_test::Text tail = RandomRelative(rng, false);
            Path base = Path(text.View(), style).Normalize();
            Path below = base.Join(tail.View());
            Path rel(style);
            REQUIRE(below.RelativeTo(base, &rel));
            CHECK(base.Join(rel).Normalize() == below.Normalize());
            CHECK(below.IsInside(base));
            // The ordering is consistent with ==.
            CHECK(((p < n) || (n < p) || p == n));
            CHECK_FALSE(((p < n) && (n < p)));
        }
    }
}

TEST_CASE("Path: PathBuilder works without the heap", "[io][path]")
{
    PathBuilder<32> b(PathStyle::Windows);
    REQUIRE(b.Set("C:\\users\\"));
    REQUIRE(b.Append("me\\cache"));
    CHECK(StringView(b.CStr()) == StringView("C:/users/me/cache"));
    CHECK_FALSE(b.Append("0123456789012345678901234567890"));        // would not fit
    CHECK(StringView(b.CStr()) == StringView("C:/users/me/cache"));  // unchanged on failure
    CHECK_FALSE(b.Set(StringView("a\0b", 3)));
    PathBuilder<16> posix;
    REQUIRE(posix.Set("/a//b/"));
    CHECK(StringView(posix.CStr()) == StringView("/a/b"));
    REQUIRE(posix.Append("/abs"));
    CHECK(StringView(posix.CStr()) == StringView("/abs"));
}

TEST_CASE("Path: copies are independent and moves leave a usable source", "[io][path]")
{
    Path a("one/two");
    Path b = a;
    b.Append("three");
    CHECK(Str(a.View()) == "one/two");
    CHECK(Str(b.View()) == "one/two/three");
    Path c = static_cast<Path&&>(b);
    CHECK(Str(c.View()) == "one/two/three");
    b = Path("again");
    CHECK(Str(b.View()) == "again");
}
