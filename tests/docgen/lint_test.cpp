

#include <catch2/catch_test_macros.hpp>

#include "fs_fixture.h"
#include "lint.h"
#include "reference.h"
#include "source_file.h"

using namespace docgen;
using docgen_test::Num;
using docgen_test::Str;
using docgen_test::StrList;
using docgen_test::StrPairList;

namespace
{

Str LintTestStr(StringView v)
{
    return Str(v.Data(), v.Length());
}
StringView LintTestSv(const Str& s)
{
    return StringView(s.Data(), s.Size());
}

struct LintTestTree
{
    Str root;
    LintTestTree()
    {
        root = docgen_test::MakeTempRoot("docgen_lint");
        docgen_test::MakeDirectory(root + "/h");
    }
    ~LintTestTree() { docgen_test::RemoveTreeAt(root); }
    void Put(const Str& rel, const Str& content) { docgen_test::WriteFileText(root + "/" + rel, content); }
};

// Indexes one header `h/a.h` under a module with the given `strict` lines.
struct Linted
{
    LintTestTree tree;
    ArenaAllocator arena{usize{1} << 28};
    ReferenceBuild build{arena};
    LintResult result{arena};
    bool built = false;

    Linted(const Str& header, const Str& strict = "")
    {
        tree.Put("h/a.h", "#pragma once\nnamespace engine_placeholder {\n" + header + "\n}\n");
        tree.Put("modules.txt", "module m \"M\"\n  header h/*.h\n" + strict);
        Str map = tree.root + "/modules.txt";
        built = BuildReference(LintTestSv(tree.root), LintTestSv(map), arena, build);
        REQUIRE(built);
        RunLint(build.index, arena, result);
    }

    StrList Findings() const
    {
        StrList v;
        for (usize i = 0; i < result.findings.Size(); ++i)
        {
            v.Push(Str(LintRuleName(result.findings[i].rule)) + " " + LintTestStr(result.findings[i].symbol->id));
        }
        return v;
    }
    bool Has(const Str& f) const
    {
        for (auto& x : Findings())
        {
            if (x == f)
            {
                return true;
            }
        }
        return false;
    }
};

const char kAllStrict[] = "  strict thread-safety\n  strict ownership\n  strict failure\n";

}  // namespace

TEST_CASE("Lint: coverage counts documented symbols; deleted and defaulted members are exempt", "[docgen][lint]")
{
    Linted l(
        "/// A widget.\nstruct Widget {\n  /// Makes one.\n  Widget();\n  Widget(const Widget&) = delete;\n  Widget& operator=(const Widget&) = delete;\n"
        "  ~Widget() = default;\n  void Undoc();\n  int field;\n};\n");
    CHECK(l.result.total == 4);  // Widget, Widget(), Undoc, field
    CHECK(l.result.documented == 2);
    CHECK(l.Has("undocumented Widget::Undoc()"));
    CHECK(l.Has("undocumented Widget::field"));
    REQUIRE(l.result.modules.Size() == 1);
    CHECK(l.result.modules[0].documented == 2);
}

TEST_CASE("Lint: a trailing ///< comment documents an enumerator; an undocumented override inherits", "[docgen][lint]")
{
    Linted l("/// A base.\nstruct Base {\n  /// Does it.\n  virtual void Go();\n};\n"
             "/// A derived.\nstruct Derived : Base {\n  void Go() override;\n};\n"
             "/// Colors.\nenum class Color { Red, ///< The red one.\n Green };\n");
    CHECK(l.Findings() == StrList{"undocumented Color::Green"});
}

TEST_CASE("Lint: thread-safety is required on classes of strict modules only", "[docgen][lint]")
{
    const Str h =
        "/// Documented.\nstruct S {};\n/// Documented too.\n/// - Thread-safety: thread-safe. Anywhere.\nstruct T {};\n";
    Linted strict(h, "  strict thread-safety\n");
    CHECK(strict.Findings() == StrList{"thread-safety S"});
    Linted lax(h);
    CHECK(lax.Findings().Empty());
}

TEST_CASE("Lint: ownership is required for raw non-const pointers, pointer returns and destructors", "[docgen][lint]")
{
    Linted l("/// A class with a destructor.\nclass Owner {\npublic:\n  /// Dtor.\n  ~Owner();\n};\n"
             "/// A plain class.\nclass Plain {};\n"
             "/// Returns a pointer.\nint* Make();\n"
             "/// Takes a pointer.\nvoid Take(int* p);\n"
             "/// Takes a const pointer.\nvoid Read(const char* s);\n"
             "/// Takes a callback.\nvoid Call(void (*fn)(int));\n"
             "/// Documented.\n/// - Ownership: the caller frees it.\nint* Made();\n",
             "  strict ownership\n");
    CHECK(l.Has("ownership Owner"));
    CHECK(l.Has("ownership Make()"));
    CHECK(l.Has("ownership Take(int*)"));
    CHECK_FALSE(l.Has("ownership Plain"));
    CHECK_FALSE(l.Has("ownership Read(const char*)"));
    CHECK_FALSE(l.Has("ownership Call(void(*)(int))"));
    CHECK_FALSE(l.Has("ownership Made()"));
    CHECK(l.Findings().Size() == 3);
}

TEST_CASE("Lint: failure labels are required for bool, pointer and nodiscard functions", "[docgen][lint]")
{
    Linted l(
        "/// A.\nbool Try();\n/// B.\n/// - Returns: true on success.\nbool Tried();\n/// C.\nint* Find();\n/// D.\n/// - Fails: returns null.\nint* Found();\n"
        "/// E.\n[[nodiscard]] int Count();\n/// F.\nvoid Fine();\n/// G.\nint Plain();\n"
        "/// H.\nstruct S {\n  /// I.\n  S();\n  /// J.\n  bool operator==(const S&) const;\n};\n",
        "  strict failure\n");
    CHECK(l.Has("failure Try()"));
    CHECK(l.Has("failure Find()"));
    CHECK(l.Has("failure Count()"));
    CHECK_FALSE(l.Has("failure Tried()"));
    CHECK_FALSE(l.Has("failure Found()"));
    CHECK_FALSE(l.Has("failure Fine()"));
    CHECK_FALSE(l.Has("failure Plain()"));
    CHECK_FALSE(l.Has("failure S::operator==(const S&) const"));  // operators are exempt
    CHECK(l.Findings().Size() == 3);
}

TEST_CASE("Lint: labels are not demanded of undocumented symbols (they get one finding, not two)", "[docgen][lint]")
{
    Linted l("struct S {};\nbool Try();\n", kAllStrict);
    CHECK(l.Findings() == (StrList{"undocumented S", "undocumented Try()"}));
}

TEST_CASE("Lint baseline: render, parse, and tolerate exactly the recorded findings", "[docgen][lint][baseline]")
{
    Linted l("/// A.\nstruct S {};\nvoid Undoc();\nvoid Undoc2();\n");
    StringBuilder<ArenaAllocator> text(l.arena);
    RenderBaseline(l.result, text);
    Baseline b(l.arena);
    StringView err;
    REQUIRE(ParseBaseline(text.View(), l.arena, b, err));
    CHECK(b.known.Size() == 2);
    CHECK(b.modules.Size() == 1);
    Vector<FileDiag, ArenaAllocator> out(l.arena);
    CompareToBaseline(l.result, &b, l.arena, out);
    CHECK(out.Size() == 0);
}

TEST_CASE("Lint baseline: a new finding, a coverage drop and no baseline at all are errors", "[docgen][lint][baseline]")
{
    Linted old("/// A.\nstruct S {};\n/// B.\nvoid Done();\nvoid Undoc();\n");
    StringBuilder<ArenaAllocator> text(old.arena);
    RenderBaseline(old.result, text);
    Baseline b(old.arena);
    StringView err;
    REQUIRE(ParseBaseline(text.View(), old.arena, b, err));

    // one more undocumented symbol, and a formerly documented one lost its comment
    Linted now("/// A.\nstruct S {};\nvoid Done();\nvoid Undoc();\nvoid Fresh();\n");
    Vector<FileDiag, ArenaAllocator> out(now.arena);
    CompareToBaseline(now.result, &b, now.arena, out);
    StrList msgs;
    for (usize i = 0; i < out.Size(); ++i)
    {
        msgs.Push(LintTestStr(out[i].d.message));
    }
    REQUIRE(msgs.Size() == 4);
    CHECK(msgs[0] == "undocumented public symbol: Done()");
    CHECK(msgs[1] == "undocumented public symbol: Fresh()");
    CHECK(msgs[2] == "coverage of m dropped from 2/3 to 1/4");
    CHECK(msgs[3] == "overall coverage dropped from 2/3 to 1/4");

    Vector<FileDiag, ArenaAllocator> none(now.arena);
    CompareToBaseline(now.result, nullptr, now.arena, none);
    CHECK(none.Size() == 3);  // Done, Undoc, Fresh
}

TEST_CASE("Lint baseline: [review] entries that no longer apply are errors (the baseline only shrinks)",
          "[docgen][lint][baseline]")
{
    Linted old("/// A.\nstruct S {};\nvoid Undoc();\n");
    StringBuilder<ArenaAllocator> text(old.arena);
    RenderBaseline(old.result, text);
    Baseline b(old.arena);
    StringView err;
    REQUIRE(ParseBaseline(text.View(), old.arena, b, err));
    Linted now("/// A.\nstruct S {};\n/// Now documented.\nvoid Undoc();\n");
    Vector<FileDiag, ArenaAllocator> out(now.arena);
    CompareToBaseline(now.result, &b, now.arena, out);
    REQUIRE(out.Size() == 1);
    CHECK(out[0].d.severity == Severity::Error);
    CHECK(LintTestStr(out[0].d.message).Find("no longer applies: undocumented Undoc()") != Str::npos);
}

TEST_CASE("Lint baseline: malformed files are rejected with a line number", "[docgen][lint][baseline]")
{
    ArenaAllocator arena{usize{1} << 24};
    Baseline b(arena);
    StringView err;
    CHECK_FALSE(ParseBaseline(StringView("module\tm\t1\t2\n"), arena, b, err));  // no header
    CHECK_FALSE(ParseBaseline(StringView("# docgen lint baseline v1\nbogus\n"), arena, b, err));
    CHECK(LintTestStr(err).RFind("2:", 0) == 0);
    CHECK_FALSE(ParseBaseline(StringView("# docgen lint baseline v1\nmodule\tm\tx\t2\n"), arena, b, err));
    CHECK(ParseBaseline(StringView("# docgen lint baseline v1\n# comment\n\nknown\tundocumented\tA::B(int)\n"), arena,
                        b, err));
}

TEST_CASE("Lint: the committed baseline matches the repository's headers", "[docgen][lint][repo]")
{
    const Str repo = Str(DOCGEN_ENGINE_DIR) + "/..";
    ArenaAllocator arena{usize{1} << 29};
    ReferenceBuild build(arena);
    Str map = repo + "/docs/reference/modules.txt";
    BuildReference(LintTestSv(repo), LintTestSv(map), arena, build);
    REQUIRE_FALSE(build.index.HasErrors());
    LintResult result(arena);
    RunLint(build.index, arena, result);
    StringView text;
    Str bp = repo + "/docs/reference/lint-baseline.txt";
    REQUIRE(LoadSourceFile(LintTestSv(bp), arena, text));
    Baseline baseline(arena);
    StringView err;
    REQUIRE(ParseBaseline(text, arena, baseline, err));
    Vector<FileDiag, ArenaAllocator> out(arena);
    CompareToBaseline(result, &baseline, arena, out);
    for (usize i = 0; i < out.Size(); ++i)
    {
        INFO(LintTestStr(out[i].path).CStr() << ":" << out[i].d.line << ": " << LintTestStr(out[i].d.message).CStr());
        CHECK(out[i].d.severity != Severity::Error);
    }
}

TEST_CASE("Lint baseline: [review] renamed, removed and new modules, and an overall drop, are reported",
          "[docgen][lint][baseline]")
{
    ArenaAllocator arena{usize{1} << 24};
    StringView err;
    Baseline b(arena);
    REQUIRE(
        ParseBaseline(StringView("# docgen lint baseline v1\nmodule\told\t5\t5\nmodule\tm\t1\t1\n"), arena, b, err));
    Linted now("/// A.\nstruct S {};\nvoid Undoc();\nvoid U2();\n");
    Vector<FileDiag, ArenaAllocator> out(now.arena);
    CompareToBaseline(now.result, &b, now.arena, out);
    StrList msgs;
    for (usize i = 0; i < out.Size(); ++i)
    {
        msgs.Push(LintTestStr(out[i].d.message));
    }
    auto has = [&](const Str& n)
    {
        for (auto& m : msgs)
        {
            if (m.Find(n) != Str::npos)
            {
                return true;
            }
        }
        return false;
    };
    CHECK(has("baseline lists module 'old' which no longer exists"));
    CHECK(has("coverage of m dropped from 1/1 to 1/3"));
    CHECK(has("overall coverage dropped from 6/6 to 1/3"));
}

TEST_CASE("Lint: [review] Aborts satisfies the failure rule; an inheriting override needs no labels of its own",
          "[docgen][lint]")
{
    Linted l("/// A.\n/// - Aborts: the pool is empty.\nint* Take();\n"
             "/// Base.\nstruct B {\n  /// Go.\n  /// - Returns: true when done.\n  virtual bool Go();\n};\n"
             "/// Derived 2.\nstruct E : B {\n  bool Go() override;\n};\n",
             "  strict failure\n");
    CHECK(l.Findings().Empty());
}

TEST_CASE("Lint: [review] `T* const` parameters count as raw pointers; unlocatable names judge nothing",
          "[docgen][lint]")
{
    Linted l("/// A.\nvoid Take(int* const p);\n", "  strict ownership\n");
    CHECK((l.Has("ownership Take(int*const)") || l.Has("ownership Take(int* const)")));
}
