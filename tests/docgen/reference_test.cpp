#include <sys/stat.h>
#include <unistd.h>

#include <catch2/catch_test_macros.hpp>

#include "fs_fixture.h"
#include "links.h"
#include "reference.h"
#include "source_file.h"

using namespace docgen;
using docgen_test::Num;
using docgen_test::Str;
using docgen_test::StrList;
using docgen_test::StrPairList;
namespace io = vse::io;

namespace
{

Str ReferenceTestStr(StringView v)
{
    return Str(v.Data(), v.Length());
}

Str ReferenceTestReadWhole(const Str& path)
{
    return docgen_test::ReadFileText(path);
}

void WriteWhole(const Str& path, const Str& content)
{
    docgen_test::WriteFileText(path, content);
}

StrList ListDir(const Str& dir)
{
    return docgen_test::ListNamesSorted(dir);
}

// A scratch directory removed with its (flat) contents.
struct TempDir
{
    Str path;
    TempDir() { path = docgen_test::MakeTempRoot("docgen_ref"); }
    ~TempDir() { docgen_test::RemoveTreeAt(path); }
};

const Str kRoot = Str(DOCGEN_FIXTURE_DIR);
const Str kMap = kRoot + "/ref/modules.txt";
const Str kExpected = kRoot + "/ref/expected";

struct Built
{
    ArenaAllocator arena{usize{1} << 28};
    ReferenceBuild build{arena};
    bool ok;
    explicit Built(const Str& root = kRoot, const Str& map = kMap)
        : ok(BuildReference(StringView(root.Data(), root.Size()), StringView(map.Data(), map.Size()), arena, build))
    {
    }

    Str File(const Str& name) const
    {
        for (usize i = 0; i < build.files.Size(); ++i)
        {
            if (ReferenceTestStr(build.files[i].name) == name)
            {
                return ReferenceTestStr(build.files[i].content);
            }
        }
        return "<none>";
    }
    StrList Diags() const
    {
        StrList v;
        for (usize i = 0; i < build.index.diagnostics.Size(); ++i)
        {
            const FileDiag& f = build.index.diagnostics[i];
            v.Push(ReferenceTestStr(f.path) + ":" + Num(f.d.line) + ": " + SeverityName(f.d.severity) + ": " +
                   ReferenceTestStr(f.d.message));
        }
        return v;
    }
};

bool HasDiag(const Built& b, const Str& needle)
{
    for (auto& d : b.Diags())
    {
        if (d.Find(needle) != Str::npos)
        {
            return true;
        }
    }
    return false;
}

// Builds a throwaway project: files = {relative path -> content}, plus the map text.
struct Project
{
    TempDir dir;
    explicit Project(const StrPairList& files, const Str& map)
    {
        for (auto& f : files)
        {
            WriteWhole(dir.path + "/" + f.first, f.second);
        }
        WriteWhole(dir.path + "/modules.txt", map);
    }
    // the TempDir removes the whole tree, nested directories included
    Str Map() const { return dir.path + "/modules.txt"; }
};

}  // namespace

// ---- module map and globbing ---------------------------------------------------------------

TEST_CASE("Glob matching: * stays in a segment, ** spans segments", "[docgen][reference][glob]")
{
    auto m = [](const char* p, const char* s) { return GlobMatch(StringView(p), StringView(s)); };
    REQUIRE(m("engine/jobs/*.h", "engine/jobs/counter.h"));
    REQUIRE_FALSE(m("engine/jobs/*.h", "engine/jobs/sub/counter.h"));
    REQUIRE_FALSE(m("engine/jobs/*.h", "engine/jobs/counter.cpp"));
    REQUIRE(m("engine/**/*.h", "engine/a.h"));
    REQUIRE(m("engine/**/*.h", "engine/a/b/c.h"));
    REQUIRE_FALSE(m("engine/**/*.h", "other/a.h"));
    REQUIRE(m("a/**", "a/b/c"));
    REQUIRE(m("*.h", "x.h"));
    REQUIRE_FALSE(m("*.h", "d/x.h"));
    REQUIRE(m("a*b*c.h", "aXXbYYc.h"));
    REQUIRE_FALSE(m("a*b*c.h", "aXXbYY.h"));
    REQUIRE(m("engine/types.h", "engine/types.h"));
    REQUIRE_FALSE(m("engine/types.h", "engine/types.hh"));
    REQUIRE(ReferenceTestStr(GlobPrefix(StringView("engine/jobs/*.h"))) == "engine/jobs");
    REQUIRE(ReferenceTestStr(GlobPrefix(StringView("engine/**/*.h"))) == "engine");
    REQUIRE(ReferenceTestStr(GlobPrefix(StringView("*.h"))) == "");
    REQUIRE(ReferenceTestStr(GlobPrefix(StringView("engine/types.h"))) == "engine");
}

TEST_CASE("Module map parsing and its errors", "[docgen][reference][map]")
{
    ArenaAllocator arena(usize{1} << 24);
    auto parse = [&](const char* text, ModuleMap& map) { ParseModuleMap(StringView(text), arena, map); };
    {
        ModuleMap mm(arena);
        auto* m = &mm;
        parse(
            "# c\nmodule a \"Title A\"   # trailing\n  header x/*.h\n  exclude x/y.h\n  strict ownership\n\nmodule b/c \"B # C\"\n  header z.h\n",
            mm);
        REQUIRE_FALSE(m->HasErrors());
        REQUIRE(m->modules.Size() == 2);
        REQUIRE(ReferenceTestStr(m->modules[0]->title) == "Title A");
        REQUIRE(m->modules[0]->headers.Size() == 1);
        REQUIRE(m->modules[0]->excludes.Size() == 1);
        REQUIRE(ReferenceTestStr(m->modules[0]->strict[0]) == "ownership");
        REQUIRE(ReferenceTestStr(m->modules[1]->title) == "B # C");  // '#' inside quotes is part of the title
    }
    auto err = [&](const char* text, const char* expect, u32 line)
    {
        ModuleMap mm(arena);
        auto* m = &mm;
        parse(text, mm);
        INFO(text);
        REQUIRE(m->diagnostics.Size() >= 1);
        REQUIRE(m->diagnostics[0].line == line);
        REQUIRE(ReferenceTestStr(m->diagnostics[0].message).Find(expect) != Str::npos);
    };
    err("header x.h\n", "outside a module", 1);
    err("module a\n", "malformed module line", 1);
    err("module a Title\n", "malformed module line", 1);
    err("module a \"A\"\nmodule a \"B\"\n", "duplicate module", 2);
    err("module a \"A\"\n  strict speed\n", "unknown strict rule", 2);
    err("module a \"A\"\n  header a?.h\n", "invalid pattern", 2);
    err("module a \"A\"\n  header a**.h\n", "invalid pattern", 2);
    err("module a \"A\"\n  header /abs.h\n", "invalid pattern", 2);
    err("module a \"A\"\n  frobnicate x\n", "unknown directive", 2);
    err("module a \"A\"\n  header a.h b.h\n", "exactly one argument", 2);
}

TEST_CASE("Module expansion is sorted, deduplicated and independent of input order", "[docgen][reference][map]")
{
    ArenaAllocator arena(usize{1} << 24);
    ModuleMap map(arena);
    ParseModuleMap(StringView("module m \"M\"\n header a/*.h\n header a/b.h\n exclude a/skip.h\n"), arena, map);
    REQUIRE_FALSE(map.HasErrors());
    StrList names = {"a/z.h", "a/b.h", "a/skip.h", "a/c.h", "q/other.h", "a/sub/deep.h", "a/b.h"};
    StrList first;
    for (int round = 0; round < 6; ++round)
    {
        Vector<StringView, ArenaAllocator> files(arena), out(arena);
        for (auto& n : names)
        {
            files.Push(StringView(n.Data(), n.Size()));
        }
        ExpandModule(*map.modules[0], files, arena, out);
        StrList got;
        for (usize i = 0; i < out.Size(); ++i)
        {
            got.Push(ReferenceTestStr(out[i]));
        }
        if (round == 0)
        {
            first = got;
        }
        REQUIRE(got == first);
        names.NextPermutation();
        names.RotateLeft(1);
    }
    REQUIRE(first == (StrList{"a/b.h", "a/c.h", "a/z.h"}));
}

TEST_CASE("File collection walks directories deterministically after sorting", "[docgen][reference][map]")
{
    ArenaAllocator arena(usize{1} << 24);
    Vector<StringView, ArenaAllocator> files(arena);
    REQUIRE(CollectFiles(StringView(kRoot.Data(), kRoot.Size()), StringView("ref"), arena, files));
    StrList got;
    for (usize i = 0; i < files.Size(); ++i)
    {
        got.Push(ReferenceTestStr(files[i]));
    }
    REQUIRE(got.Has("ref/headers/counter.h"));
    REQUIRE(got.Has("ref/modules.txt"));
    REQUIRE_FALSE(CollectFiles(StringView(kRoot.Data(), kRoot.Size()), StringView("nonexistent"), arena, files));
}

// ---- symbol IDs, anchors, resolution --------------------------------------------------------

TEST_CASE("Symbol IDs drop the outer namespace, normalize parameters and keep qualifiers", "[docgen][reference][index]")
{
    Project p(
        {{"h/a.h",
          "#pragma once\n"
          "namespace vse {\nnamespace n {\n"
          "/// A class.\nclass K {\npublic:\n"
          "  /// Sum.\n  int Sum(const std::atomic<int>&  value,  unsigned long long n = 4, char* const names[], Foo<int, 2> f) const noexcept;\n"
          "  /// Plain.\n  void Plain(void);\n"
          "  /// Pointer.\n  void Fn(bool (*done)(void* user), int (&arr)[3]);\n"
          "  /// Op.\n  bool operator==(const K& other) const;\n"
          "};\n}\n}\n"}},
        "module m \"M\"\n header h/a.h\n");
    Built b(p.dir.path, p.Map());
    REQUIRE(b.ok);
    StrList ids;
    for (usize i = 0; i < b.build.index.symbols.Size(); ++i)
    {
        ids.Push(ReferenceTestStr(b.build.index.symbols[i]->id));
    }
    REQUIRE(ids[0] == "n::K");
    REQUIRE(ids[1] == "n::K::Sum(const std::atomic<int>&,unsigned long long,char* const[],Foo<int,2>) const noexcept");
    REQUIRE(ids[2] == "n::K::Plain()");
    REQUIRE(ids[3] == "n::K::Fn(bool(*)(void*),int(&)[3])");
    REQUIRE(ids[4] == "n::K::operator==(const K&) const");
}

TEST_CASE("Anchors: lowercase, punctuation words for * & operators, remaining ties get -2",
          "[docgen][reference][index]")
{
    Project p({{"h/a.h",
                "#pragma once\n"
                "/// A.\nvoid Do(int);\n"
                "/// B.\nvoid Do(int*);\n"  // different ID, same anchor "do-int"
                "/// C.\nvoid Do(int&);\n"  // and again
                "/// D.\nvoid Do_Int();\n"}},
              "module m \"M\"\n header h/a.h\n");
    Built b(p.dir.path, p.Map());
    REQUIRE(b.ok);
    StrList anchors;
    for (usize i = 0; i < b.build.index.symbols.Size(); ++i)
    {
        anchors.Push(ReferenceTestStr(b.build.index.symbols[i]->anchor));
    }
    REQUIRE(anchors == (StrList{"do-int", "do-int-star", "do-int-amp", "do-int-2"}));
}

TEST_CASE("Hidden, Internal and namespace-less declarations are indexed correctly", "[docgen][reference][index]")
{
    Built b;
    REQUIRE(b.ok);
    StrList ids;
    for (usize i = 0; i < b.build.index.symbols.Size(); ++i)
    {
        ids.Push(ReferenceTestStr(b.build.index.symbols[i]->id));
    }
    auto has = [&](const char* id) { return ids.Has(id); };
    REQUIRE(has("demo::Counter::Increment(unsigned long)"));
    REQUIRE_FALSE(has("demo::Counter::count_"));   // private
    REQUIRE_FALSE(has("demo::HelperForTests()"));  // Internal:
    REQUIRE_FALSE(has("ForTests()"));              // Internal: in the corpus
    REQUIRE(has("DEMO_SQUARE"));
    // Source order within a header: the macro sits between MakeCounter and Count.
    auto pos = [&](const char* id) { return ids.IndexOf(id); };
    REQUIRE(pos("demo::MakeCounter(unsigned long)") < pos("DEMO_SQUARE"));
    REQUIRE(pos("DEMO_SQUARE") < pos("demo::Count"));
}

TEST_CASE("Reference resolution: qualified, partial, hinted, ambiguous, missing", "[docgen][reference][resolve]")
{
    Project p({{"h/a.h", "#pragma once\n"
                         "namespace vse {\nnamespace jobs {\n"
                         "/// Counter.\nclass Counter {\npublic:\n  /// Ctor.\n  Counter();\n"
                         "  /// Inc.\n  void Increment(unsigned long n = 1);\n"
                         "  /// Help.\n  void Help(int a);\n  /// Help.\n  void Help(Counter& c);\n};\n"
                         "/// Other.\nclass Other {\npublic:\n  /// Inc.\n  void Increment();\n};\n"
                         "}\n}\n"}},
              "module m \"M\"\n header h/a.h\n");
    Built b(p.dir.path, p.Map());
    REQUIRE(b.ok);
    auto r = [&](const char* t, const char* expect_id)
    {
        const Symbol* s = nullptr;
        ResolveStatus st = ResolveRef(b.build.index, StringView(t), s);
        INFO(t);
        if (expect_id == nullptr)
        {
            return st;
        }
        REQUIRE(st == ResolveStatus::Ok);
        REQUIRE(ReferenceTestStr(s->id) == expect_id);
        return st;
    };
    r("jobs::Counter", "jobs::Counter");
    r("Counter", "jobs::Counter");  // the class, not its constructor
    r("vse::jobs::Counter", "jobs::Counter");
    r("Counter::Counter", "jobs::Counter::Counter()");
    r("Counter::Increment", "jobs::Counter::Increment(unsigned long)");
    r("jobs::Counter::Help(int)", "jobs::Counter::Help(int)");
    r("Help(Counter&)", "jobs::Counter::Help(Counter&)");
    r("Help( Counter & )", "jobs::Counter::Help(Counter&)");           // whitespace-insensitive hint
    REQUIRE(r("Increment", nullptr) == ResolveStatus::Ambiguous);      // Counter::Increment vs Other::Increment
    REQUIRE(r("Counter::Help", nullptr) == ResolveStatus::Ambiguous);  // overloads without a hint
    REQUIRE(r("Help(long)", nullptr) == ResolveStatus::NotFound);
    REQUIRE(r("Nope", nullptr) == ResolveStatus::NotFound);
    REQUIRE(r("", nullptr) == ResolveStatus::NotFound);
    REQUIRE(r("jobs::Count", nullptr) == ResolveStatus::NotFound);  // a prefix of a name is not a match
}

// ---- build diagnostics ----------------------------------------------------------------------

TEST_CASE("Unresolved and ambiguous references in comments are errors at their line", "[docgen][reference][index]")
{
    Project p({{"h/a.h", "#pragma once\n"
                         "/// Fine, see [[Real]].\nvoid Real();\n"
                         "/// Bad ref to [[Missing]].\nvoid A();\n"
                         "/// Two of them.\n/// - See: [[Dup]]\nvoid B();\n"
                         "namespace x { /// Dup.\n void Dup(); }\n"
                         "namespace y { /// Dup.\n void Dup(); }\n"}},
              "module m \"M\"\n header h/a.h\n");
    Built b(p.dir.path, p.Map());
    REQUIRE_FALSE(b.ok);
    REQUIRE(b.build.files.Size() == 0);
    REQUIRE(HasDiag(b, "h/a.h:4: error: unresolved reference 'Missing'"));
    REQUIRE(HasDiag(b, "h/a.h:7: error: ambiguous reference 'Dup'"));
    REQUIRE_FALSE(HasDiag(b, "'Real'"));
}

TEST_CASE("Map problems: unmatched patterns, shared headers, duplicate IDs, bad headers", "[docgen][reference][index]")
{
    {
        Project p({{"h/a.h", "#pragma once\n/// A.\nvoid A();\n"}},
                  "module m \"M\"\n header h/*.h\n header nothing/*.h\n");
        Built b(p.dir.path, p.Map());
        REQUIRE_FALSE(b.ok);
        REQUIRE(HasDiag(b, "modules.txt:1: error: header pattern matches no files: nothing/*.h"));
    }
    {
        Project p({{"h/a.h", "#pragma once\n/// A.\nvoid A();\n"}},
                  "module m \"M\"\n header h/a.h\nmodule n \"N\"\n header h/*.h\n");
        Built b(p.dir.path, p.Map());
        REQUIRE_FALSE(b.ok);
        REQUIRE(HasDiag(b, "header listed in two modules: h/a.h"));
    }
    {
        Project p(
            {{"h/a.h", "#pragma once\n/// A.\nvoid A();\n"}, {"h/b.h", "#pragma once\n/// A again.\nvoid A();\n"}},
            "module m \"M\"\n header h/*.h\n");
        Built b(p.dir.path, p.Map());
        REQUIRE_FALSE(b.ok);
        REQUIRE(HasDiag(b, "h/b.h:3: error: duplicate symbol ID 'A()'"));
    }
    {
        // A declaration followed by its inline definition in one header is a single symbol.
        Project p({{"h/a.h", "#pragma once\nvoid A();\n/// Defined here.\ninline void A() {}\n"}},
                  "module m \"M\"\n header h/a.h\n");
        Built b(p.dir.path, p.Map());
        REQUIRE(b.ok);
        REQUIRE(b.build.index.symbols.Size() == 1);
        REQUIRE(b.build.index.symbols[0]->doc != nullptr);  // the documented one wins
    }
    {
        Project p({{"h/a.h", "#pragma once\nstruct S {\n"}}, "module m \"M\"\n header h/a.h\n");
        Built b(p.dir.path, p.Map());
        REQUIRE_FALSE(b.ok);
        REQUIRE(HasDiag(b, "h/a.h:2: error: unbalanced braces"));
    }
    {
        Built b(kRoot, kRoot + "/ref/does-not-exist.txt");
        REQUIRE_FALSE(b.ok);
        REQUIRE(HasDiag(b, "cannot read module map"));
    }
}

TEST_CASE("Excluded headers are never parsed", "[docgen][reference][index]")
{
    Built b;
    REQUIRE(b.ok);  // ref/headers/skipped.h is deliberately invalid C++
    for (usize i = 0; i < b.build.index.units.Size(); ++i)
    {
        REQUIRE(ReferenceTestStr(b.build.index.units[i]->path) != "ref/headers/skipped.h");
    }
}

// ---- rendering and goldens ---------------------------------------------------------------------

TEST_CASE("Generated pages match the committed golden output", "[docgen][reference][golden]")
{
    Built b;
    REQUIRE(b.ok);
    StrList names;
    for (usize i = 0; i < b.build.files.Size(); ++i)
    {
        names.Push(ReferenceTestStr(b.build.files[i].name));
    }
    REQUIRE(names == (StrList{"README.md", "demo.md", "extras.md", "symbols.txt"}));
    for (auto& n : names)
    {
        INFO(n.CStr());
        REQUIRE(b.File(n) == ReferenceTestReadWhole(kExpected + "/" + n));
    }
    // No stray files in the golden directory.
    REQUIRE(ListDir(kExpected) == names);
}

TEST_CASE("Rendering details", "[docgen][reference][render]")
{
    Built b;
    REQUIRE(b.ok);
    const Str demo = b.File("demo.md");
    auto has = [&](const char* s) { return demo.Find(s) != Str::npos; };
    REQUIRE(has("# Demo Module\n\n_Generated by docgen. Do not edit._\n"));
    REQUIRE(has("**Headers:** `ref/headers/counter.h`, `ref/headers/shapes.h`"));
    REQUIRE(has("The join primitive of the demo module."));  // file overview
    REQUIRE(has("## `Counter` {#demo-counter}"));
    REQUIRE(has("### `Increment(unsigned long)` {#demo-counter-increment-unsigned-long}"));
    REQUIRE(has("#### `resumes` {#demo-counter-stats-resumes}"));  // nested member level
    REQUIRE(has("| Parameter `n` | how many units to add. |"));
    REQUIRE(has("[`demo::Counter::Increment`](#demo-counter-increment-unsigned-long)"));  // same-page link
    REQUIRE(has("<a id=\"demo-wakemode-requeue\"></a>`Requeue` | `3` |"));                // enumerator table
    REQUIRE(has("_Inherited from [`demo::Shape::Area() const`](#demo-shape-area-const)._"));
    REQUIRE(has("Draws the circle with its own words."));  // own doc wins, no inheritance note
    REQUIRE(has("**Example** — counting two jobs."));
    REQUIRE(has("#define DEMO_SQUARE(x)"));
    REQUIRE_FALSE(has("count_"));
    REQUIRE_FALSE(has("HelperForTests"));

    const Str extras = b.File("extras.md");
    REQUIRE(extras.Find("Base(const Base&) = delete;") != Str::npos);
    REQUIRE(extras.Find("virtual void Hook() = 0;") != Str::npos);
    REQUIRE(extras.Find("template <>\nclass Buffer<char, 1> { ... }") != Str::npos);
    REQUIRE(extras.Find("| Deprecated | use [`Read`](#read-int-int-star) instead") != Str::npos);
    REQUIRE(extras.Find("_Undocumented._") != Str::npos);

    const Str syms = b.File("symbols.txt");
    REQUIRE(syms.Compare(0, 20, "# docgen symbols v1\n") == 0);
    REQUIRE(
        syms.Find(
            "demo::Counter::Count() const\tfunction\tdemo\tdemo-counter-count-const\tdemo.md\tref/headers/counter.h:47\n") !=
        Str::npos);
}

TEST_CASE("Cross-module links name the other page", "[docgen][reference][render]")
{
    Project p({{"h/a.h", "#pragma once\n/// Uses [[B]].\nvoid A();\n"}, {"g/b.h", "#pragma once\n/// B.\nvoid B();\n"}},
              "module one \"One\"\n header h/a.h\nmodule two/x \"Two\"\n header g/b.h\n");
    Built b(p.dir.path, p.Map());
    REQUIRE(b.ok);
    REQUIRE(b.File("one.md").Find("Uses [`B`](two-x.md#b).") != Str::npos);
    REQUIRE(b.File("README.md").Find("[Two](two-x.md)") != Str::npos);
}

// ---- determinism, idempotence, staleness -------------------------------------------------------

TEST_CASE("Two builds are byte-identical", "[docgen][reference][determinism]")
{
    Built a, b;
    REQUIRE(a.ok);
    REQUIRE(b.ok);
    REQUIRE(a.build.files.Size() == b.build.files.Size());
    for (usize i = 0; i < a.build.files.Size(); ++i)
    {
        REQUIRE(a.build.files[i].name == b.build.files[i].name);
        REQUIRE(a.build.files[i].content == b.build.files[i].content);
    }
}

TEST_CASE("reference writes, then check passes; stale output is detected", "[docgen][reference][check]")
{
    TempDir out;
    Built b;
    REQUIRE(b.ok);
    {
        Vector<FileDiag, ArenaAllocator> problems(b.arena);
        StringView dir(out.path.Data(), out.path.Size());
        REQUIRE(WriteReference(b.build, dir, b.arena, problems));
        REQUIRE(problems.Size() == 0);
        REQUIRE(ListDir(out.path) == (StrList{"README.md", "demo.md", "extras.md", "symbols.txt"}));

        // idempotent: a second write changes nothing, and check is clean
        Str before = ReferenceTestReadWhole(out.path + "/demo.md");
        REQUIRE(WriteReference(b.build, dir, b.arena, problems));
        REQUIRE(ReferenceTestReadWhole(out.path + "/demo.md") == before);
        REQUIRE(CheckReference(b.build, dir, b.arena, problems));
        REQUIRE(problems.Size() == 0);
    }
    auto check = [&](StrList& msgs)
    {
        Vector<FileDiag, ArenaAllocator> problems(b.arena);
        bool ok = CheckReference(b.build, StringView(out.path.Data(), out.path.Size()), b.arena, problems);
        for (usize i = 0; i < problems.Size(); ++i)
        {
            msgs.Push(ReferenceTestStr(problems[i].path).Substr(out.path.Size() + 1) + ":" + Num(problems[i].d.line) +
                      ": " + ReferenceTestStr(problems[i].d.message));
        }
        return ok;
    };
    // 1. a changed page
    Str demo = ReferenceTestReadWhole(out.path + "/demo.md");
    WriteWhole(out.path + "/demo.md", demo + "extra\n");
    {
        StrList msgs;
        REQUIRE_FALSE(check(msgs));
        REQUIRE(msgs.Size() == 1);
        REQUIRE(msgs[0].Compare(0, 8, "demo.md:") == 0);
        REQUIRE(msgs[0].Find("out of date") != Str::npos);
        // reports the first differing line
        usize lines = 0;
        for (usize i = 0; i < demo.Size(); ++i)
        {
            lines += demo[i] == '\n' ? 1 : 0;
        }
        REQUIRE(msgs[0].Find("line " + Num(lines + 1)) != Str::npos);
    }
    WriteWhole(out.path + "/demo.md", demo);
    // 2. a missing file
    {
        io::Path symbols;
        REQUIRE(io::Path::Parse(StringView((out.path + "/symbols.txt").CStr()), io::NativePathStyle(), &symbols));
        REQUIRE(io::Remove(symbols).Ok());
    }
    {
        StrList msgs;
        REQUIRE_FALSE(check(msgs));
        REQUIRE(msgs.Size() == 1);
        REQUIRE(msgs[0].Find("missing") != Str::npos);
    }
    // 3. a generated page that is no longer produced; a hand-written md is left alone
    Vector<FileDiag, ArenaAllocator> problems(b.arena);
    REQUIRE(WriteReference(b.build, StringView(out.path.Data(), out.path.Size()), b.arena, problems));
    WriteWhole(out.path + "/old.md", Str("# Old\n\n") + kGeneratedMarker + "\n");
    WriteWhole(out.path + "/notes.md", "# Hand written\n");
    {
        StrList msgs;
        REQUIRE_FALSE(check(msgs));
        REQUIRE(msgs.Size() == 1);
        REQUIRE(msgs[0].Compare(0, 7, "old.md:") == 0);
    }
    // reference removes the stale generated page and keeps the hand-written one
    REQUIRE(WriteReference(b.build, StringView(out.path.Data(), out.path.Size()), b.arena, problems));
    REQUIRE(ListDir(out.path) == (StrList{"README.md", "demo.md", "extras.md", "notes.md", "symbols.txt"}));
    StrList msgs;
    REQUIRE(check(msgs));
}

TEST_CASE("The repository's own module map builds (headers and patterns all resolve)", "[docgen][reference][engine]")
{
    const Str root = Str(DOCGEN_ENGINE_DIR) + "/..";
    const Str map = root + "/docs/reference/modules.txt";
    Built b(root, map);
    for (auto& d : b.Diags())
    {
        INFO(d.CStr());
    }
    REQUIRE(b.ok);
    REQUIRE(b.build.index.symbols.Size() > 500);
    // The generated Wayland protocol headers are excluded.
    for (usize i = 0; i < b.build.index.units.Size(); ++i)
    {
        REQUIRE(ReferenceTestStr(b.build.index.units[i]->path).Find("/generated/") == Str::npos);
    }
}

// ---- code-review regressions (Day 40) -----------------------------------------------------------

TEST_CASE("[review] operators, destructors and pointer overloads keep distinct, stable anchors",
          "[docgen][reference][review]")
{
    auto anchors_for = [](const Str& body)
    {
        Project p({{"h/a.h", "#pragma once\n" + body}}, "module m \"M\"\n header h/a.h\n");
        Built b(p.dir.path, p.Map());
        REQUIRE(b.ok);
        StrList v;
        for (usize i = 0; i < b.build.index.symbols.Size(); ++i)
        {
            v.Push(ReferenceTestStr(b.build.index.symbols[i]->id) + " => " +
                   ReferenceTestStr(b.build.index.symbols[i]->anchor));
        }
        return v;
    };
    const Str ops = "/// V.\nstruct V {\n"
                    "  /// Default.\n  V();\n  /// Dtor.\n  ~V();\n"
                    "  /// Minus.\n  V operator-(const V&) const;\n"
                    "  /// Eq.\n  bool operator==(const V&) const;\n"
                    "  /// Ne.\n  bool operator!=(const V&) const;\n"
                    "  /// Lt.\n  bool operator<(const V&) const;\n"
                    "  /// Call.\n  int operator()(int) const;\n"
                    "  /// Index.\n  int operator[](int) const;\n"
                    "};\n";
    auto first = anchors_for(ops);
    // No anchor needed a -2 tie-break suffix.
    for (auto& a : first)
    {
        INFO(a.CStr());
        REQUIRE(a.Find("-2") == Str::npos);
    }
    REQUIRE(first[1] == "V::V() => v-v");
    REQUIRE(first[2] == "V::~V() => v-tilde-v");
    REQUIRE(first[3] == "V::operator-(const V&) const => v-operator-minus-const-v-amp-const");
    REQUIRE(first[7] == "V::operator()(int) const => v-operator-call-int-const");
    REQUIRE(first[8] == "V::operator[](int) const => v-operator-index-int-const");
    // Reordering the operators does not change any anchor.
    const Str swapped = "/// V.\nstruct V {\n"
                        "  /// Dtor.\n  ~V();\n  /// Default.\n  V();\n"
                        "  /// Ne.\n  bool operator!=(const V&) const;\n"
                        "  /// Eq.\n  bool operator==(const V&) const;\n"
                        "  /// Minus.\n  V operator-(const V&) const;\n"
                        "  /// Index.\n  int operator[](int) const;\n"
                        "  /// Call.\n  int operator()(int) const;\n"
                        "  /// Lt.\n  bool operator<(const V&) const;\n"
                        "};\n";
    auto second = anchors_for(swapped);
    first.Sort();
    second.Sort();
    REQUIRE(first == second);
}

TEST_CASE("[review] only a real marker line makes a page 'generated'", "[docgen][reference][review]")
{
    TempDir out;
    Built b;
    REQUIRE(b.ok);
    Vector<FileDiag, ArenaAllocator> problems(b.arena);
    StringView dir(out.path.Data(), out.path.Size());
    REQUIRE(WriteReference(b.build, dir, b.arena, problems));
    // Hand-written pages that quote the marker (mid-file, or as an indented line) are not touched.
    WriteWhole(out.path + "/design.md", Str("# Design\n\nThe tool writes `") + kGeneratedMarker + "` at the top.\n");
    WriteWhole(out.path + "/quote.md", Str("# Quote\n\n    ") + kGeneratedMarker + "\n");
    WriteWhole(out.path + "/nolines.md", kGeneratedMarker);
    REQUIRE(CheckReference(b.build, dir, b.arena, problems));
    REQUIRE(problems.Size() == 0);
    REQUIRE(WriteReference(b.build, dir, b.arena, problems));
    REQUIRE(ListDir(out.path) ==
            (StrList{"README.md", "demo.md", "design.md", "extras.md", "nolines.md", "quote.md", "symbols.txt"}));
}

TEST_CASE("[review] a missing module map is an I/O failure; documentation errors are not",
          "[docgen][reference][review]")
{
    Built missing(kRoot, kRoot + "/ref/does-not-exist.txt");
    REQUIRE_FALSE(missing.ok);
    REQUIRE(missing.build.io_failure);
    Project p({{"h/a.h", "#pragma once\nstruct S {\n"}}, "module m \"M\"\n header h/a.h\n");
    Built broken(p.dir.path, p.Map());
    REQUIRE_FALSE(broken.ok);
    REQUIRE_FALSE(broken.build.io_failure);
}

TEST_CASE("[review] reference creates a nested output directory", "[docgen][reference][review]")
{
    TempDir base;
    Built b;
    REQUIRE(b.ok);
    Vector<FileDiag, ArenaAllocator> problems(b.arena);
    Str nested = base.path + "/a/b/c";
    REQUIRE(WriteReference(b.build, StringView(nested.Data(), nested.Size()), b.arena, problems));
    REQUIRE(problems.Size() == 0);
    REQUIRE(ListDir(nested).Size() == 4);
    // the TempDir removes the nested tree
    // a path that cannot be created is reported once, not once per file
    Str bad = base.path + "/file";
    WriteWhole(bad, "x");
    Str under_file = bad + "/sub";
    Vector<FileDiag, ArenaAllocator> problems2(b.arena);
    REQUIRE_FALSE(WriteReference(b.build, StringView(under_file.Data(), under_file.Size()), b.arena, problems2));
    REQUIRE(problems2.Size() == 1);
}

TEST_CASE("[review] same-ID declarations: a function redeclaration merges, other kinds are duplicates",
          "[docgen][reference][review]")
{
    {
        // The first declaration keeps its location; the documented one supplies the comment.
        Project p({{"h/a.h", "#pragma once\nvoid A();\n/// Defined here.\ninline void A() {}\n"}},
                  "module m \"M\"\n header h/a.h\n");
        Built b(p.dir.path, p.Map());
        REQUIRE(b.ok);
        REQUIRE(b.build.index.symbols.Size() == 1);
        REQUIRE(b.build.index.symbols[0]->decl->line == 2);
        REQUIRE(b.build.index.symbols[0]->doc != nullptr);
    }
}

TEST_CASE("[review] duplicate classes, enums and variables in one header are errors", "[docgen][reference][review]")
{
    for (const char* body : {"/// A.\nstruct S {};\n/// B.\nstruct S {};\n", "/// A.\nint v;\n/// B.\nint v;\n",
                             "/// A.\nenum E { X };\n/// B.\nenum E { Y };\n"})
    {
        Project p({{"h/a.h", Str("#pragma once\n") + body}}, "module m \"M\"\n header h/a.h\n");
        Built b(p.dir.path, p.Map());
        INFO(body);
        REQUIRE_FALSE(b.ok);
        REQUIRE(HasDiag(b, "error: duplicate symbol ID"));
    }
}

TEST_CASE("[review] references to operator() and long overload hints resolve", "[docgen][reference][review]")
{
    Str params;
    for (int i = 0; i < 80; ++i)
    {
        params += Str(i ? ", " : "") + "SomeLongTypeName" + Num(i) + " p" + Num(i);
    }
    Project p(
        {{"h/a.h",
          "#pragma once\n"
          "/// Hash.\nstruct Hash {\n  /// Call.\n  unsigned long operator()(int v) const;\n  /// Index.\n  int operator[](int i) const;\n"
          "  /// Wide.\n  void Wide(" +
              params + ");\n};\n"}},
        "module m \"M\"\n header h/a.h\n");
    Built b(p.dir.path, p.Map());
    REQUIRE(b.ok);
    auto id = [&](const Str& t)
    {
        const Symbol* s = nullptr;
        ResolveStatus st = ResolveRef(b.build.index, StringView(t.Data(), t.Size()), s);
        return st == ResolveStatus::Ok ? ReferenceTestStr(s->id).Substr(0, 40)
                                       : Str("<") + Num(static_cast<int>(st)) + ">";
    };
    REQUIRE(id("Hash::operator()") == "Hash::operator()(int) const");
    REQUIRE(id("Hash::operator()(int)") == "Hash::operator()(int) const");
    REQUIRE(id("operator()(int)") == "Hash::operator()(int) const");
    REQUIRE(id("Hash::operator[](int)") == "Hash::operator[](int) const");
    Str hint = "Wide(";
    for (int i = 0; i < 80; ++i)
    {
        hint += Str(i ? "," : "") + "SomeLongTypeName" + Num(i);
    }
    hint += ")";
    REQUIRE(id(hint).Compare(0, 10, "Hash::Wide") == 0);           // > 512 characters of hint
    REQUIRE(id(hint.Substr(0, hint.Size() - 2) + "x)") == "<1>");  // a long hint that differs at the end is NotFound
}

TEST_CASE("[review] file collection does not follow directory symlinks and reports unreadable subdirectories",
          "[docgen][reference][review]")
{
    TempDir dir;
    WriteWhole(dir.path + "/real/a.h", "x");  // creates the directory
    // The links and the unreadable directory below are raw on purpose: they build the exact file-system shapes the walk
    // must survive.
    REQUIRE(symlink(dir.path.CStr(), (dir.path + "/real/loop").CStr()) == 0);  // a cycle
    REQUIRE(symlink("a.h", (dir.path + "/real/link.h").CStr()) == 0);          // a file symlink is still a file
    ArenaAllocator arena(usize{1} << 24);
    Vector<StringView, ArenaAllocator> files(arena);
    REQUIRE(CollectFiles(StringView(dir.path.Data(), dir.path.Size()), StringView(), arena, files));
    StrList got;
    for (usize i = 0; i < files.Size(); ++i)
    {
        got.Push(ReferenceTestStr(files[i]));
    }
    got.Sort();
    REQUIRE(got == (StrList{"real/a.h", "real/link.h"}));
    // an unreadable subdirectory makes the walk report failure (running as root ignores modes)
    if (geteuid() != 0)
    {
        mkdir((dir.path + "/locked").CStr(), 0000);
        Vector<StringView, ArenaAllocator> files2(arena);
        REQUIRE_FALSE(CollectFiles(StringView(dir.path.Data(), dir.path.Size()), StringView(), arena, files2));
        chmod((dir.path + "/locked").CStr(), 0700);  // so the TempDir can remove it
    }
}

TEST_CASE("Reference: [review] deleted and defaulted members render as such, not as undocumented",
          "[docgen][reference]")
{
    Project p(
        {{"h/a.h",
          "#pragma once\nnamespace vse {\n/// A thing.\nstruct T {\n  T() = default;\n  T(const T&) = delete;\n  /// Doc.\n  void F();\n  void G();\n};\n}\n"}},
        "module m \"M\"\n  header h/*.h\n");
    Str map = p.dir.path + "/modules.txt";
    Built b(p.dir.path, map);
    REQUIRE(b.ok);
    Str page = b.File("m.md");
    CHECK(page.Find("_Defaulted._") != Str::npos);
    CHECK(page.Find("_Deleted._") != Str::npos);
    CHECK(page.Find("_Undocumented._") != Str::npos);  // G()
}

namespace
{
// Restores the real directory order even when a REQUIRE aborts the test case.
struct NameOrderGuard
{
    ~NameOrderGuard() { SetNameOrderSeedForTesting(0); }
};
}  // namespace

TEST_CASE("Reference output and the collected file lists do not depend on directory iteration order",
          "[docgen][reference][determinism]")
{
    NameOrderGuard guard;
    StrPairList files;
    for (int d = 0; d < 6; ++d)
    {
        for (int f = 0; f < 6; ++f)
        {
            const Str name = "h/d" + Num(d) + "/f" + Num(f) + ".h";
            files.Push({name, "#pragma once\nnamespace vse {\n/// Thing " + Num(d * 10 + f) + ".\nstruct T" +
                                  Num(d * 10 + f) + " {};\n}\n"});
        }
    }
    for (int m = 0; m < 5; ++m)
    {
        files.Push({"docs/page" + Num(m) + ".md", "# Page\n"});
    }
    files.Push({"top.md", "# Top\n"});
    Project p(files, "module m \"M\"\n  header h/*/*.h\n");
    const Str root = p.dir.path;
    const Str map = p.Map();
    auto snapshot = [&](u32 seed, StrList* markdown)
    {
        SetNameOrderSeedForTesting(seed);
        Built b(root, map);
        REQUIRE(b.ok);
        StrList out;
        for (usize i = 0; i < b.build.files.Size(); ++i)
        {
            out.Push(ReferenceTestStr(b.build.files[i].name) + "\n" + ReferenceTestStr(b.build.files[i].content));
        }
        Vector<StringView, ArenaAllocator> md(b.arena);
        REQUIRE(CollectMarkdownFiles(StringView(root.Data(), root.Size()), b.arena, md));
        markdown->Clear();
        for (usize i = 0; i < md.Size(); ++i)
        {
            markdown->Push(ReferenceTestStr(md[i]));
        }
        return out;
    };
    StrList base_markdown;
    const StrList base = snapshot(0, &base_markdown);
    REQUIRE(base.Size() >= 3);
    REQUIRE(base_markdown.Size() == 6);
    for (u32 seed : {1u, 7u, 99u, 12345u})
    {
        INFO("seed " << seed);
        StrList markdown;
        CHECK(snapshot(seed, &markdown) == base);
        CHECK(markdown == base_markdown);
    }
}

TEST_CASE("Reference: a directory that cannot be written keeps the previous output", "[docgen][reference][errors]")
{
    if (geteuid() == 0)
    {
        SUCCEED("root writes anywhere");
        return;
    }
    TempDir out;
    Built b;
    REQUIRE(b.ok);
    Vector<FileDiag, ArenaAllocator> problems(b.arena);
    REQUIRE(WriteReference(b.build, StringView(out.path.Data(), out.path.Size()), b.arena, problems));
    const StrList before = ListDir(out.path);
    StrList contents;
    for (const Str& n : before)
    {
        contents.Push(ReferenceTestReadWhole(out.path + "/" + n));
    }
    // The files are about to change, and the directory can no longer take new entries (a temporary file cannot be
    // created).
    REQUIRE(chmod(out.path.CStr(), 0500) == 0);
    Built changed;
    REQUIRE(changed.ok);
    changed.build.files[0].content = StringView("replaced\n");
    Vector<FileDiag, ArenaAllocator> failures(changed.arena);
    CHECK_FALSE(WriteReference(changed.build, StringView(out.path.Data(), out.path.Size()), changed.arena, failures));
    CHECK(failures.Size() >= 1);
    REQUIRE(chmod(out.path.CStr(), 0700) == 0);
    CHECK(ListDir(out.path) == before);  // no stray temporary files
    for (usize i = 0; i < before.Size(); ++i)
    {
        CHECK(ReferenceTestReadWhole(out.path + "/" + before[i]) == contents[i]);
    }
}
