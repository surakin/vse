

#include <catch2/catch_test_macros.hpp>

#include "fs_fixture.h"
#include "links.h"
#include "reference.h"
#include "lang/utility.h"

using namespace docgen;
using docgen_test::Num;
using docgen_test::Str;
using docgen_test::StrList;
using docgen_test::StrPairList;

namespace
{

Str LinksTestStr(StringView v)
{
    return Str(v.Data(), v.Length());
}
StringView Sv(const Str& s)
{
    return StringView(s.Data(), s.Size());
}

// A throwaway repository tree, removed recursively.
struct Tree
{
    Str root;
    explicit Tree(const StrPairList& files)
    {
        root = docgen_test::MakeTempRoot("docgen_links");
        for (auto& f : files)
        {
            Put(f.first, f.second);
        }
    }
    ~Tree() { docgen_test::RemoveTreeAt(root); }
    void Put(const Str& rel, const Str& content) { docgen_test::WriteFileText(root + "/" + rel, content); }
};

struct Checked
{
    ArenaAllocator arena{usize{1} << 28};
    ReferenceBuild build{arena};
    Vector<FileDiag, ArenaAllocator> diags{arena};
    LinkStats stats;
    bool read_ok = false;
    StrList lines;

    explicit Checked(const Tree& t, bool with_index = false, bool external = false)
    {
        const SymbolIndex* index = nullptr;
        if (with_index)
        {
            Str map = t.root + "/docs/reference/modules.txt";
            BuildReference(Sv(t.root), Sv(map), arena, build);
            index = &build.index;
        }
        LinkOptions opt;
        opt.list_external = external;
        read_ok = CheckLinks(Sv(t.root), index, arena, opt, diags, stats);
        for (usize i = 0; i < diags.Size(); ++i)
        {
            const FileDiag& f = diags[i];
            lines.Push(LinksTestStr(f.path) + ":" + Num(f.d.line) + ": " + SeverityName(f.d.severity) + ": " +
                       LinksTestStr(f.d.message));
        }
    }
    bool Has(const Str& needle) const
    {
        for (auto& l : lines)
        {
            if (l.Find(needle) != Str::npos)
            {
                return true;
            }
        }
        return false;
    }
    usize Errors() const
    {
        usize n = 0;
        for (usize i = 0; i < diags.Size(); ++i)
        {
            n += diags[i].d.severity == Severity::Error;
        }
        return n;
    }
};

const char kHome[] = "# Docs\n\n- [guide](guide/a.md)\n";

struct LinksTestScanned
{
    Str text;  // the scanned views point into it
    ArenaAllocator arena{usize{1} << 24};
    MdDoc doc{arena};
    explicit LinksTestScanned(Str t) : text(engine_placeholder::Move(t)) { ScanMarkdown(Sv(text), arena, doc); }
};

}  // namespace

TEST_CASE("Markdown scan: fences carry info, body and lines", "[docgen][links][md]")
{
    LinksTestScanned s("intro\n```cpp compile run\nint x;\n[not](a.md)\n```\n~~~\nplain\n~~~\n");
    REQUIRE(s.doc.fences.Size() == 2);
    CHECK(LinksTestStr(s.doc.fences[0].info) == "cpp compile run");
    CHECK(LinksTestStr(s.doc.fences[0].body) == "int x;\n[not](a.md)");
    CHECK(s.doc.fences[0].line == 2);
    CHECK(s.doc.fences[0].body_line == 3);
    CHECK(LinksTestStr(s.doc.fences[1].body) == "plain");
    CHECK(s.doc.links.Size() == 0);  // link-shaped text in a fence is not a link
    CHECK(s.doc.diagnostics.Size() == 0);
}

TEST_CASE("Markdown scan: empty and unterminated fences", "[docgen][links][md]")
{
    LinksTestScanned a("```\n```\n");
    REQUIRE(a.doc.fences.Size() == 1);
    CHECK(a.doc.fences[0].body.Length() == 0);
    CHECK(a.doc.fences[0].closed);
    LinksTestScanned b("text\n```cpp\nint x;\n");
    REQUIRE(b.doc.fences.Size() == 1);
    CHECK_FALSE(b.doc.fences[0].closed);
    REQUIRE(b.doc.diagnostics.Size() == 1);
    CHECK(b.doc.diagnostics[0].line == 2);
    CHECK(LinksTestStr(b.doc.fences[0].body) == "int x;");
}

TEST_CASE("Markdown scan: a longer fence is not closed by a shorter one", "[docgen][links][md]")
{
    LinksTestScanned s("````md\n```\ninner\n```\n````\nafter [x](y.md)\n");
    REQUIRE(s.doc.fences.Size() == 1);
    CHECK(LinksTestStr(s.doc.fences[0].body) == "```\ninner\n```");
    REQUIRE(s.doc.links.Size() == 1);
    CHECK(s.doc.links[0].line == 6);
}

TEST_CASE("Markdown scan: heading anchors follow GitHub slugs", "[docgen][links][md]")
{
    LinksTestScanned s(
        "# Hello, World!\n## `Counter::Add(u64)`\n## Hello, World!\n## Hello, World!\n### See [the guide](g.md) ###\n## `Foo` {#my-id}\n<a id=\"enum-x\"></a>\n");
    CHECK(s.doc.HasAnchor(StringView("hello-world")));
    CHECK(s.doc.HasAnchor(StringView("hello-world-1")));
    CHECK(s.doc.HasAnchor(StringView("hello-world-2")));
    CHECK(s.doc.HasAnchor(StringView("counteradd")) == false);
    CHECK(s.doc.HasAnchor(StringView("counteraddu64")));
    CHECK(s.doc.HasAnchor(StringView("see-the-guide")));
    CHECK(s.doc.HasAnchor(StringView("my-id")));
    CHECK(s.doc.HasAnchor(StringView("enum-x")));
    CHECK_FALSE(s.doc.HasAnchor(StringView("nope")));
}

TEST_CASE("Markdown scan: links, images, autolinks, refs; code spans are skipped", "[docgen][links][md]")
{
    LinksTestScanned s("See [a](a.md#x), ![img](pics/p.png) and `[no](code.md)` plus [[jobs::Counter]].\n"
                       "<https://example.com/x> and [t](<sp ace.md> \"title\") [p](f(1).md)\n"
                       "[ref]: other.md#top\n"
                       "use [text][ref] here\n");
    REQUIRE(s.doc.links.Size() == 6);
    // the reference definition (line 3) is recorded as soon as it is seen, ahead of the paragraph around it
    CHECK(LinksTestStr(s.doc.links[0].target) == "other.md#top");
    CHECK(s.doc.links[0].line == 3);
    CHECK(LinksTestStr(s.doc.links[1].target) == "a.md#x");
    CHECK_FALSE(s.doc.links[1].image);
    CHECK(LinksTestStr(s.doc.links[2].target) == "pics/p.png");
    CHECK(s.doc.links[2].image);
    CHECK(LinksTestStr(s.doc.links[3].target) == "https://example.com/x");
    CHECK(LinksTestStr(s.doc.links[4].target) == "sp ace.md");
    CHECK(LinksTestStr(s.doc.links[5].target) == "f(1).md");
    REQUIRE(s.doc.refs.Size() == 1);
    CHECK(LinksTestStr(s.doc.refs[0].target) == "jobs::Counter");
    CHECK(s.doc.diagnostics.Size() == 0);
}

TEST_CASE("Markdown scan: malformed references are errors", "[docgen][links][md]")
{
    LinksTestScanned s("a [[ open\n\nb [[]] c\n");
    REQUIRE(s.doc.diagnostics.Size() == 2);
    CHECK(s.doc.diagnostics[0].line == 1);
    CHECK(s.doc.diagnostics[1].line == 3);
}

TEST_CASE("Markdown scan: [review] prose that merely looks like link syntax is left alone", "[docgen][links][md]")
{
    LinksTestScanned s("Index with x[i][j] and a footnote[^1].\n\n[^1]: A footnote about things.\n");
    CHECK(s.doc.links.Size() == 0);
    CHECK(s.doc.diagnostics.Size() == 0);
}

TEST_CASE("Markdown scan: [review] code spans and references wrap across lines", "[docgen][links][md]")
{
    LinksTestScanned s(
        "start `see [[Bad::Thing]] or\n[x](nope.md) wrapped` end\nthen [[Good::\nName]] and [l](ok.md)\n");
    REQUIRE(s.doc.refs.Size() == 1);
    CHECK(LinksTestStr(s.doc.refs[0].target) == "Good::Name");
    CHECK(s.doc.refs[0].line == 3);
    REQUIRE(s.doc.links.Size() == 1);
    CHECK(LinksTestStr(s.doc.links[0].target) == "ok.md");
    CHECK(s.doc.links[0].line == 4);
}

TEST_CASE("Markdown scan: [review] <a id> anchors count only outside fences and as real attributes",
          "[docgen][links][md]")
{
    LinksTestScanned s("```html\n<a id=\"fake\"></a>\n```\n<a data-id=\"nope\" name=\"real\"></a>\n");
    CHECK_FALSE(s.doc.HasAnchor(StringView("fake")));
    CHECK_FALSE(s.doc.HasAnchor(StringView("nope")));
    CHECK(s.doc.HasAnchor(StringView("real")));
}

TEST_CASE("Links: a consistent tree is clean", "[docgen][links]")
{
    Tree t(
        {{"docs/README.md", kHome},
         {"docs/guide/a.md",
          "# A\n\n## Part one\n\nBack to [home](../README.md#docs), [part](#part-one), [b](b.md#b-title)\n![pic](../img/p.png)\n"},
         {"docs/guide/b.md", "# B Title\n\nSee [the repo](../../PLAN.md) and <https://example.com>.\n"},
         {"docs/img/p.png", "x"},
         {"PLAN.md", "# Plan\n"}});
    Checked c(t);
    REQUIRE(c.read_ok);
    for (auto& l : c.lines)
    {
        INFO(l.CStr());
    }
    CHECK(c.Errors() == 0);
    CHECK(c.stats.files == 4);
    CHECK(c.stats.external == 1);
}

TEST_CASE("Links: each class of broken link is caught", "[docgen][links]")
{
    Tree t({{"docs/README.md", kHome},
            {"docs/guide/a.md", "# A\n"
                                "[gone](missing.md)\n"          // 2
                                "[bad anchor](b.md#nothing)\n"  // 3
                                "[self](#nothing-here)\n"       // 4
                                "![img](nope.png)\n"            // 5
                                "[up](../../../outside.md)\n"   // 6
                                "[empty]()\n"                   // 7
                                "[dir](../guide)\n"             // 8 (fine)
                                "[ok](b.md#b)\n"                // 9 (fine)
                                "```\n[in fence](ignored.md)\n```\n"},
            {"docs/guide/b.md", "# B\n"}});
    Checked c(t);
    REQUIRE(c.read_ok);
    CHECK(c.Errors() == 6);
    CHECK(c.Has("docs/guide/a.md:2: error: broken link: missing.md does not exist"));
    CHECK(c.Has("docs/guide/a.md:3: error: broken anchor: no heading '#nothing' in docs/guide/b.md"));
    CHECK(c.Has("docs/guide/a.md:4: error: broken anchor: no heading '#nothing-here' in this file"));
    CHECK(c.Has("docs/guide/a.md:5: error: missing image: nope.png does not exist"));
    CHECK(c.Has("docs/guide/a.md:6: error: link escapes the repository"));
    CHECK(c.Has("docs/guide/a.md:7: error: empty link target"));
}

TEST_CASE("Links: percent-escapes, queries and root-relative links resolve", "[docgen][links]")
{
    Tree t({{"docs/README.md",
             "# Docs\n[a](guide/my%20page.md) [b](/docs/guide/my%20page.md?x=1#top) [c](guide/Other.md)\n"},
            {"docs/guide/my page.md", "# Top\n"},
            {"docs/guide/Other.md", "# O\n"}});
    Checked c(t);
    CHECK(c.Errors() == 0);
}

TEST_CASE("Links: orphan pages under docs are errors, reachability is transitive", "[docgen][links]")
{
    Tree t({{"docs/README.md", kHome},
            {"docs/guide/a.md", "# A\n[b](b.md)\n"},
            {"docs/guide/b.md", "# B\n"},
            {"docs/guide/lonely.md", "# Lonely\n[a](a.md)\n"},  // links out, nobody links in
            {"NOTES.md", "# top-level pages are not orphans\n"}});
    Checked c(t);
    REQUIRE(c.Errors() == 1);
    CHECK(c.Has("docs/guide/lonely.md:1: error: orphan page"));
}

TEST_CASE("Links: external links are counted, listed on request, never fetched", "[docgen][links]")
{
    Tree t(
        {{"docs/README.md", "# Docs\n[x](https://example.invalid/a) [m](mailto:a@b.c) <http://example.invalid/b>\n"}});
    Checked quiet(t);
    CHECK(quiet.lines.Empty());
    CHECK(quiet.stats.external == 3);
    Checked loud(t, false, true);
    CHECK(loud.lines.Size() == 3);
    CHECK(loud.Errors() == 0);
}

TEST_CASE("Links: [[refs]] resolve against the symbol index, with candidates on ambiguity", "[docgen][links][refs]")
{
    Tree t(
        {{"docs/README.md", "# Docs\n"
                            "[[geo::Circle]] [[Circle]] [[Circle::Area]]\n"  // 2: ok, ambiguous, ok
                            "[[geo::Nothing]]\n"                             // 3: unresolved
                            "`[[inline::code]]`\n"                           // 4: ignored
                            "```\n[[in::fence]]\n```\n"},
         {"docs/reference/modules.txt", "module m \"M\"\n  header h/*.h\n"},
         {"h/a.h",
          "#pragma once\nnamespace engine_placeholder {\nnamespace geo {\n/// A circle.\nstruct Circle { /// Area.\n double Area() const; };\n}\n"
          "namespace other {\n/// Another circle.\nstruct Circle {};\n}\n}\n"}});
    Checked c(t, true);
    REQUIRE(c.read_ok);
    CHECK(c.stats.refs == 4);
    CHECK(c.Has("docs/README.md:2: error: ambiguous reference 'Circle' (candidates: geo::Circle, other::Circle)"));
    CHECK(c.Has("docs/README.md:3: error: unresolved reference 'geo::Nothing'"));
    CHECK_FALSE(c.Has("README.md:4"));
    CHECK_FALSE(c.Has("README.md:6"));
    usize in_docs = 0;
    for (auto& l : c.lines)
    {
        in_docs += l.RFind("docs/README.md", 0) == 0;
    }
    CHECK(in_docs == 2);
}

TEST_CASE("Links: generated reference anchors match what the checker computes", "[docgen][links][refs]")
{
    // every symbols.txt anchor of a generated page must be a heading/anchor the checker knows
    Tree t(
        {{"docs/README.md", "# Docs\n[ref](reference/README.md)\n"},
         {"docs/reference/modules.txt", "module m \"M\"\n  header h/*.h\n"},
         {"h/a.h",
          "#pragma once\nnamespace engine_placeholder {\n/// A thing.\nstruct Thing {\n  /// Makes one.\n  Thing();\n  /// Drops one.\n  ~Thing();\n"
          "  /// Compares.\n  bool operator==(const Thing& o) const;\n  /// Adds.\n  void Add(int x, int* p);\n};\n"
          "/// Colors.\nenum class Color { Red, ///< The red one.\n Green ///< The green one.\n};\n}\n"}});
    ArenaAllocator arena{usize{1} << 28};
    ReferenceBuild build(arena);
    Str map = t.root + "/docs/reference/modules.txt";
    REQUIRE(BuildReference(Sv(t.root), Sv(map), arena, build));
    Str idx = "# Docs\n";
    Str readme = "# Reference\n\n";
    Str syms;
    for (usize i = 0; i < build.files.Size(); ++i)
    {
        const Str name = LinksTestStr(build.files[i].name);
        t.Put("docs/reference/" + name, LinksTestStr(build.files[i].content));
        if (name == "symbols.txt")
        {
            syms = LinksTestStr(build.files[i].content);
        }
    }
    usize linked = 0;
    usize at = 0;
    while ((at = syms.Find('\n', at)) != Str::npos && at + 1 < syms.Size())
    {
        ++at;
        usize e = syms.Find('\n', at);
        Str line = syms.Substr(at, e - at);
        StrList f;
        usize p = 0;
        for (usize q; (q = line.Find('\t', p)) != Str::npos; p = q + 1)
        {
            f.Push(line.Substr(p, q - p));
        }
        f.Push(line.Substr(p));
        if (f.Size() < 5)
        {
            continue;
        }
        readme += "[" + f[0] + "](" + f[4] + "#" + f[3] + ")\n";
        ++linked;
    }
    readme += "[home](../README.md)\n";
    t.Put("docs/reference/README.md", readme);
    REQUIRE(linked >= 5);
    Checked c(t);
    for (auto& l : c.lines)
    {
        INFO(l.CStr());
    }
    CHECK(c.Errors() == 0);
}

TEST_CASE("Links: the repository's own docs tree is clean", "[docgen][links][repo]")
{
    const Str repo = Str(DOCGEN_ENGINE_DIR) + "/..";
    ArenaAllocator arena{usize{1} << 29};
    ReferenceBuild build(arena);
    Str map = repo + "/docs/reference/modules.txt";
    BuildReference(Sv(repo), Sv(map), arena, build);
    Vector<FileDiag, ArenaAllocator> diags(arena);
    LinkStats stats;
    REQUIRE(CheckLinks(Sv(repo), &build.index, arena, LinkOptions(), diags, stats));
    for (usize i = 0; i < diags.Size(); ++i)
    {
        INFO(LinksTestStr(diags[i].path).CStr()
             << ":" << diags[i].d.line << ": " << LinksTestStr(diags[i].d.message).CStr());
        CHECK(diags[i].d.severity != Severity::Error);
    }
    CHECK(stats.files > 10);
}

TEST_CASE("Links: [review] other URI schemes are external, not file paths", "[docgen][links]")
{
    Tree t({{"docs/README.md", "# Docs\n[a](tel:123) ![d](data:image/png;base64,AAAA) [f](file:///x) [w](c:/dir)\n"}});
    Checked c(t);
    // "c:/dir" is a one-letter drive, not a scheme: it is a (missing) relative file
    CHECK(c.Errors() == 1);
    CHECK(c.stats.external == 3);
}

TEST_CASE("Links: [review] an encoded NUL does not truncate the checked path", "[docgen][links]")
{
    Tree t({{"docs/README.md", "# Docs\n[x](a.md%00.png)\n"}, {"docs/a.md", "# A\n"}});
    Checked c(t);
    CHECK(c.Has("docs/README.md:2: error: broken link"));
}

TEST_CASE("Links: [review] a docs tree without docs/README.md is an error, not a pass", "[docgen][links]")
{
    Tree t({{"docs/a.md", "# A\n"}});
    Checked c(t);
    CHECK(c.Has("docs/README.md:1: error: missing"));
}

TEST_CASE("Links: [review] wrapped [[refs]] resolve", "[docgen][links][refs]")
{
    Tree t(
        {{"docs/README.md", "# Docs\nsee [[geo::\nCircle]] here\n"},
         {"docs/reference/modules.txt", "module m \"M\"\n  header h/*.h\n"},
         {"h/a.h",
          "#pragma once\nnamespace engine_placeholder {\nnamespace geo {\n/// A circle.\nstruct Circle {};\n}\n}\n"}});
    Checked c(t, true);
    CHECK(c.Errors() == 0);
    CHECK(c.stats.refs == 1);
}
