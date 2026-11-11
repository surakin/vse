

#include <catch2/catch_test_macros.hpp>

#include "fs_fixture.h"
#include "extractor.h"
#include "scanner.h"
#include "cstr/cstr.h"

using namespace docgen;
using docgen_test::Num;
using docgen_test::Str;
using docgen_test::StrList;

namespace
{

struct Extracted
{
    ArenaAllocator arena{usize{64} << 20};
    Str src;
    ScanResult scan{arena};
    ExtractResult ex{arena};

    explicit Extracted(const Str& s) : src(s)
    {
        Scan(StringView(src.Data(), src.Size()), arena, scan);
        Extract(scan, arena, ex);
    }

    Str Dump(bool hidden = false)
    {
        engine_placeholder::StringBuilder<ArenaAllocator> sb(arena);
        DumpTree(ex, scan, sb, hidden);
        StringView v = sb.View();
        return Str(v.Data(), v.Length());
    }
    Str Diag(usize i) const
    {
        const Diagnostic& d = ex.diagnostics[i];
        return Num(d.line) + ": " + SeverityName(d.severity) + ": " + Str(d.message.Data(), d.message.Length());
    }
    usize Diags() const { return ex.diagnostics.Size(); }

    // Finds a declaration by qualified name (depth-first, hidden included).
    const Decl* Find(const Str& qualified) const { return FindIn(ex.root, qualified); }

private:
    static const Decl* FindIn(const Decl* d, const Str& q)
    {
        if (Str(d->qualified.Data(), d->qualified.Length()) == q && d->parent != nullptr)
        {
            return d;
        }
        for (usize i = 0; i < d->children.Size(); ++i)
        {
            if (const Decl* r = FindIn(d->children[i], q))
            {
                return r;
            }
        }
        return nullptr;
    }
};

Str ExtractorTestReadWhole(const Str& path)
{
    return docgen_test::ReadFileText(path);
}

void ExtractorTestListFiles(const Str& dir, const char* suffix, StrList& out)
{
    for (const Str& n : docgen_test::ListNamesSorted(dir))
    {
        Str p = dir + "/" + n;
        if (docgen_test::IsDirectoryAt(p))
        {
            ExtractorTestListFiles(p, suffix, out);
        }
        else if (p.Size() >= engine_placeholder::CStrLength(suffix) &&
                 p.Compare(p.Size() - engine_placeholder::CStrLength(suffix), engine_placeholder::CStrLength(suffix),
                           suffix) == 0)
        {
            out.Push(p);
        }
    }
}

bool ExtractorOwns(const Str& message)
{
    static const char* prefixes[] = {"unbalanced braces", "unrecognized declaration", "macro-generated declaration"};
    for (const char* p : prefixes)
    {
        if (message.RFind(p, 0) == 0)
        {
            return true;
        }
    }
    return false;
}

}  // namespace

// ---- fixtures ---------------------------------------------------------------------------

TEST_CASE("Good fixtures extract with no diagnostics and match their golden trees", "[docgen][extractor][fixtures]")
{
    StrList files;
    ExtractorTestListFiles(Str(DOCGEN_FIXTURE_DIR) + "/good", ".h", files);
    REQUIRE(files.Size() >= 13);
    for (auto& f : files)
    {
        INFO(f.CStr());
        Extracted e(ExtractorTestReadWhole(f));
        REQUIRE(e.scan.diagnostics.Size() == 0);
        REQUIRE(e.Diags() == 0);
        Str golden = ExtractorTestReadWhole(f.Substr(0, f.Size() - 2) + ".tree");
        INFO("missing or empty golden tree");
        REQUIRE_FALSE(golden.Empty());
        REQUIRE(e.Dump() == golden);
    }
}

TEST_CASE("Bad fixtures: the extractor reports exactly the diagnostics it owns", "[docgen][extractor][fixtures]")
{
    StrList files;
    ExtractorTestListFiles(Str(DOCGEN_FIXTURE_DIR) + "/bad", ".h", files);
    int checked = 0;
    for (auto& f : files)
    {
        INFO(f.CStr());
        StrList expected;
        Str exp = ExtractorTestReadWhole(f + ".expect");
        usize pos = 0;
        while (pos < exp.Size())
        {
            usize nl = exp.Find('\n', pos);
            if (nl == Str::npos)
            {
                nl = exp.Size();
            }
            Str line = exp.Substr(pos, nl - pos);
            pos = nl + 1;
            if (line.Empty())
            {
                continue;
            }
            usize c1 = line.Find(": ");
            usize c2 = line.Find(": ", c1 + 2);
            REQUIRE(c2 != Str::npos);
            if (ExtractorOwns(line.Substr(c2 + 2)))
            {
                expected.Push(line);
            }
        }
        Extracted e(ExtractorTestReadWhole(f));
        REQUIRE(e.Diags() == expected.Size());
        for (usize i = 0; i < e.Diags(); ++i)
        {
            REQUIRE(e.Diag(i).Compare(0, expected[i].Size(), expected[i]) == 0);
        }
        if (!expected.Empty())
        {
            ++checked;
        }
    }
    REQUIRE(checked >= 2);
}

TEST_CASE("Every engine header extracts with zero diagnostics", "[docgen][extractor][engine]")
{
    StrList files;
    ExtractorTestListFiles(DOCGEN_ENGINE_DIR, ".h", files);
    REQUIRE(files.Size() >= 40);
    usize functions = 0, classes = 0;
    for (auto& f : files)
    {
        INFO(f.CStr());
        Extracted e(ExtractorTestReadWhole(f));
        REQUIRE(e.scan.diagnostics.Size() == 0);
        if (e.Diags() != 0)
        {
            INFO(e.Diag(0).CStr());
        }
        REQUIRE(e.Diags() == 0);
        Str dump = e.Dump(true);
        for (usize p = dump.Find(" function "); p != Str::npos; p = dump.Find(" function ", p + 1))
        {
            ++functions;
        }
        for (usize p = dump.Find("class "); p != Str::npos; p = dump.Find("class ", p + 1))
        {
            ++classes;
        }
    }
    // Guards against the extractor silently dropping everything.
    REQUIRE(functions > 300);
    REQUIRE(classes > 20);
}

// ---- declaration forms -------------------------------------------------------------------

TEST_CASE("Visibility: private, protected, detail and anonymous are hidden", "[docgen][extractor]")
{
    Extracted e("class C {\n"
                "public:\n  void A();\n"
                "protected:\n  void B();\n"
                "private:\n  void P();\n"
                "};\n"
                "struct S { void A(); private: void P(); };\n"
                "namespace detail { void D(); }\n"
                "namespace { void F(); }\n"
                "namespace n { namespace detail { struct X { void M(); }; } void Ok(); }\n");
    REQUIRE(e.Diags() == 0);
    REQUIRE_FALSE(e.Find("C::A")->hidden);
    REQUIRE(e.Find("C::B")->hidden);
    REQUIRE(e.Find("C::B")->access == Access::Protected);
    REQUIRE(e.Find("C::P")->hidden);
    REQUIRE_FALSE(e.Find("S::A")->hidden);
    REQUIRE(e.Find("S::P")->hidden);
    REQUIRE(e.Find("detail::D")->hidden);
    REQUIRE(e.Find("(anonymous)::F")->hidden);
    REQUIRE(e.Find("n::detail::X::M")->hidden);
    REQUIRE_FALSE(e.Find("n::Ok")->hidden);
    Str pub = e.Dump(false);
    REQUIRE(pub.Find("C::P") == Str::npos);
    REQUIRE(pub.Find("detail") == Str::npos);
    REQUIRE(e.Dump(true).Find("C::P") != Str::npos);
}

TEST_CASE("Constructor initializer lists and member initializers do not end the declaration", "[docgen][extractor]")
{
    Extracted e("struct W {\n"
                "  W(int v) : a_{v}, b_(v), c_{v + 1} {}\n"
                "  explicit W(long v) : W{static_cast<int>(v)} {}\n"
                "  int a_{0};\n"
                "  int b_ = 3;\n"
                "  int c_ = int{4};\n"
                "  int Next();\n"
                "};\n");
    REQUIRE(e.Diags() == 0);
    REQUIRE(e.ex.root->children.Size() == 1);
    const Decl* w = e.Find("W");
    REQUIRE(w->children.Size() == 6);
    REQUIRE(w->children[0]->Is(kFlagConstructor));
    REQUIRE(Str(w->children[0]->signature) == "W(int v)");
    REQUIRE(w->children[1]->Is(kFlagConstructor | kFlagExplicit));
    REQUIRE(Str(w->children[2]->signature) == "int a_");
    REQUIRE(Str(w->children[3]->signature) == "int b_");
    REQUIRE(Str(w->children[4]->signature) == "int c_");
    REQUIRE(Str(w->children[5]->name) == "Next");
}

TEST_CASE("A requires clause after a template header belongs to the header", "[docgen][extractor]")
{
    Extracted e("template <typename T> requires std::integral<T> T Twice(T v);\n"
                "template <typename T>\n  requires (sizeof(T) > 2) && Other<T>\nvoid G(T);\n"
                "template <typename T> void H(T) requires std::integral<T>;\n");
    REQUIRE(e.Diags() == 0);
    const Decl* twice = e.Find("Twice");
    REQUIRE(Str(twice->template_header) == "template <typename T> requires std::integral<T>");
    REQUIRE(Str(twice->signature) == "T Twice(T v)");
    REQUIRE(Str(e.Find("G")->template_header).Find("requires (sizeof(T) > 2) && Other<T>") != Str::npos);
    REQUIRE(Str(e.Find("G")->signature) == "void G(T)");
    // A trailing requires-clause stays in the signature.
    REQUIRE(Str(e.Find("H")->signature) == "void H(T) requires std::integral<T>");
}

TEST_CASE("Anonymous unions/structs in a class contribute their members to it", "[docgen][extractor]")
{
    Extracted e("struct V {\n"
                "  union { int i; float f; };\n"
                "  struct { int x; int y; };\n"
                "  int tail;\n"
                "};\n");
    REQUIRE(e.Diags() == 0);
    REQUIRE(e.Find("V::i") != nullptr);
    REQUIRE(e.Find("V::f") != nullptr);
    REQUIRE(e.Find("V::tail") != nullptr);
}

TEST_CASE("Out-of-line definitions and forward declarations add nothing", "[docgen][extractor]")
{
    Extracted e("struct Fwd;\n"
                "class Fwd2;\n"
                "enum class E : int;\n"
                "template <typename T> struct TFwd;\n"
                "struct B { void M(); static int s; };\n"
                "inline void B::M() {}\n"
                "int B::s = 4;\n"
                "template <typename T> void Q<T>::Put(const T&) {}\n");
    REQUIRE(e.Diags() == 0);
    REQUIRE(e.ex.root->children.Size() == 1);
    REQUIRE(e.Find("B")->children.Size() == 2);
}

TEST_CASE("Function pointers, typedefs and elaborated types", "[docgen][extractor]")
{
    Extracted e("void (*callback)(int);\n"
                "typedef void (*Fn)(int, int);\n"
                "typedef struct { int a; } Plain;\n"
                "struct stat* GetStat();\n"
                "enum Color* ColorPtr();\n"
                "using Cmp = bool (*)(int, int);\n");
    REQUIRE(e.Diags() == 0);
    REQUIRE(e.Find("callback")->kind == DeclKind::Variable);
    REQUIRE(e.Find("Fn")->kind == DeclKind::Alias);
    REQUIRE(e.Find("Plain")->kind == DeclKind::Alias);
    REQUIRE(e.Find("GetStat")->kind == DeclKind::Function);
    REQUIRE(e.Find("ColorPtr")->kind == DeclKind::Function);
    REQUIRE(e.Find("Cmp")->kind == DeclKind::Alias);
}

TEST_CASE("Operators, conversions, destructors and deleted/defaulted members", "[docgen][extractor]")
{
    Extracted e("struct O {\n"
                "  ~O() = default;\n"
                "  O& operator=(O&&) noexcept = delete;\n"
                "  bool operator<(const O&) const;\n"
                "  O& operator<<(int);\n"
                "  explicit operator const char*() const;\n"
                "  static void* operator new[](unsigned long);\n"
                "  bool operator()() const;\n"
                "  virtual void V() = 0;\n"
                "};\n");
    REQUIRE(e.Diags() == 0);
    REQUIRE(e.Find("O::~O")->Is(kFlagDestructor | kFlagDefaulted));
    REQUIRE(e.Find("O::operator=")->Is(kFlagDeleted | kFlagNoexcept));
    REQUIRE(e.Find("O::operator<") != nullptr);
    REQUIRE(e.Find("O::operator<<") != nullptr);
    REQUIRE(e.Find("O::operator const char*")->Is(kFlagConversion | kFlagExplicit | kFlagConst));
    REQUIRE(e.Find("O::operator new[]")->Is(kFlagStatic));
    REQUIRE(e.Find("O::operator()") != nullptr);
    REQUIRE(e.Find("O::V")->Is(kFlagPureVirtual | kFlagVirtual));
}

TEST_CASE("Namespaces merge when reopened and keep the first doc comment", "[docgen][extractor]")
{
    Extracted e("/// First.\nnamespace n { void A(); }\n"
                "/// Second.\nnamespace n { void B(); }\n");
    REQUIRE(e.Diags() == 0);
    REQUIRE(e.ex.root->children.Size() == 1);
    const Decl* n = e.Find("n");
    REQUIRE(n->children.Size() == 2);
    REQUIRE(Str(e.scan.docs[static_cast<usize>(n->doc)].text) == "First.");
}

TEST_CASE("Doc comments attach to the declaration, trailing docs to enumerators and members", "[docgen][extractor]")
{
    Extracted e("/// The macro.\n#define M(x) (x)\n"
                "/// Doc for f.\n[[nodiscard]] int f();\n"
                "enum class E {\n  A, ///< first\n  B ///< second\n};\n"
                "struct S { int a; ///< member\n int b; };\n");
    REQUIRE(e.Diags() == 0);
    REQUIRE(e.Find("M")->doc >= 0);
    REQUIRE(Str(e.Find("M")->signature) == "M(x)");
    REQUIRE(e.Find("f")->doc >= 0);
    REQUIRE(Str(e.Find("f")->attributes) == "[[nodiscard]]");
    REQUIRE(Str(e.scan.docs[static_cast<usize>(e.Find("E::A")->trailing_doc)].text) == "first");
    REQUIRE(Str(e.scan.docs[static_cast<usize>(e.Find("E::B")->trailing_doc)].text) == "second");
    REQUIRE(e.Find("S::a")->trailing_doc >= 0);
    REQUIRE(e.Find("S::b")->trailing_doc < 0);
}

TEST_CASE("Unscoped enumerators are qualified by the enclosing scope", "[docgen][extractor]")
{
    Extracted e("namespace n { enum Color { Red, Green }; enum class Mode { On }; }\n");
    REQUIRE(e.Diags() == 0);
    REQUIRE(e.Find("n::Red") != nullptr);
    REQUIRE(e.Find("n::Mode::On") != nullptr);
    REQUIRE(e.Find("n::Color::Red") == nullptr);
}

// ---- code-review regressions (Day 38) -------------------------------------------------------

TEST_CASE("[review] template-ids with function types do not start a parameter list", "[docgen][extractor][review]")
{
    Extracted e("struct S {\n"
                "  std::function<void()> cb = [] {};\n"
                "  Foo<void(int), 3> m2;\n"
                "  std::map<int, int> table;\n"
                "  int f(int);\n"
                "};\n");
    REQUIRE(e.Diags() == 0);
    REQUIRE(e.Find("S::cb")->kind == DeclKind::Variable);
    REQUIRE(e.Find("S::m2")->kind == DeclKind::Variable);
    REQUIRE(e.Find("S::table")->kind == DeclKind::Variable);
    REQUIRE(e.Find("S::f")->kind == DeclKind::Function);
    REQUIRE(e.Find("S")->children.Size() == 4);
}

TEST_CASE("[review] an export macro between the class keyword and the name", "[docgen][extractor][review]")
{
    Extracted e(
        "struct ENGINE_API Bar { int z; };\nclass ENGINE_API Baz final : public Bar { public: void M(); };\nstruct stat st;\n");
    REQUIRE(e.Diags() == 0);
    REQUIRE(e.Find("Bar")->kind == DeclKind::Class);
    REQUIRE(e.Find("Bar::z") != nullptr);
    REQUIRE(e.Find("Baz::M") != nullptr);
    REQUIRE(e.Find("Baz")->Is(kFlagFinal));
    // `struct stat st;` is a variable, not a forward declaration of `st`.
    REQUIRE(e.Find("st")->kind == DeclKind::Variable);
}

TEST_CASE("[review] multiple declarators are refused, never silently dropped", "[docgen][extractor][review]")
{
    Extracted e("int v, w;\n"
                "int x_ = 0, y_;\n"
                "struct S { int a; int b, c; } s;\n"
                "enum { kA } mode;\n"
                "std::map<int, int> ok;\n"
                "inline constexpr auto k = Pair<1, 2>::value;\n");
    // Line 3 has two: `int b, c;` inside the body and the `s` after the closing brace.
    REQUIRE(e.Diags() == 5);
    const char* lines[] = {"1", "2", "3", "3", "4"};
    for (usize i = 0; i < 5; ++i)
    {
        INFO(e.Diag(i).CStr());
        REQUIRE(e.Diag(i).Compare(0, 2, Str(lines[i]) + ":") == 0);
        REQUIRE(e.Diag(i).Find("error: unrecognized declaration") != Str::npos);
    }
    REQUIRE(e.Find("ok") != nullptr);
    REQUIRE(e.Find("k") != nullptr);
}

TEST_CASE("[review] const/noexcept inside a trailing return type are not member qualifiers",
          "[docgen][extractor][review]")
{
    Extracted e("struct S { auto f() -> const char*; auto g() const noexcept -> int; auto h() -> int override; };\n");
    REQUIRE(e.Diags() == 0);
    REQUIRE_FALSE(e.Find("S::f")->Is(kFlagConst));
    REQUIRE(e.Find("S::g")->Is(kFlagConst | kFlagNoexcept));
    REQUIRE(e.Find("S::h")->Is(kFlagOverride));
}

TEST_CASE("[review] function-try-blocks are one declaration", "[docgen][extractor][review]")
{
    Extracted e("void m() try { risky(); } catch (...) { recover(); }\n"
                "struct S { S() try : a_(1) { } catch (const E&) { } int a_; };\n"
                "int after;\n");
    REQUIRE(e.Diags() == 0);
    REQUIRE(e.Find("m") != nullptr);
    REQUIRE(e.Find("after") != nullptr);
}

TEST_CASE("[review] only the innermost segment of a nested namespace gets the doc", "[docgen][extractor][review]")
{
    Extracted e("/// Inner docs.\nnamespace a::b { void F(); }\n");
    REQUIRE(e.Diags() == 0);
    REQUIRE(e.Find("a")->doc < 0);
    REQUIRE(e.Find("a::b")->doc >= 0);
}

TEST_CASE("[review] an unterminated class does not steal a trailing doc from the last token",
          "[docgen][extractor][review]")
{
    Extracted e("class X {\n int a; ///< member\n");
    REQUIRE(e.Diags() == 1);
    REQUIRE(e.Find("X")->trailing_doc < 0);
    REQUIRE(e.Find("X::a")->trailing_doc >= 0);
}

TEST_CASE("[review] direct initialization with a literal is a variable", "[docgen][extractor][review]")
{
    Extracted e("struct S { int v(5); int f(int); };\nconst char* name(\"x\");\n");
    REQUIRE(e.Diags() == 0);
    REQUIRE(e.Find("S::v")->kind == DeclKind::Variable);
    REQUIRE(e.Find("S::f")->kind == DeclKind::Function);
    REQUIRE(e.Find("name")->kind == DeclKind::Variable);
}

// ---- diagnostics -------------------------------------------------------------------------

TEST_CASE("Unbalanced braces are reported once at the unclosed brace", "[docgen][extractor]")
{
    {
        // The brace opened on line 4 is closed by the `}` on line 6 (the lexer cannot know
        // better); what is left open at end of file is `struct S` (line 2), reported once.
        Extracted e("namespace n {\n  struct S {\n    void f() {\n      if (x) {\n    }\n};\n");
        REQUIRE(e.Diags() == 1);
        REQUIRE(e.Diag(0).Compare(0, 24, "2: error: unbalanced bra") == 0);
    }
    {
        Extracted e("void f();\n}\nvoid g();\n");
        REQUIRE(e.Diags() == 1);
        REQUIRE(e.Diag(0).Compare(0, 24, "2: error: unbalanced bra") == 0);
        REQUIRE(e.Find("g") != nullptr);  // recovered
    }
    {
        Extracted e("void f(int a, \n");
        REQUIRE(e.Diags() >= 1);
        REQUIRE(e.Diag(0).Find("error: unbalanced braces") != Str::npos);
    }
}

TEST_CASE("Unclassifiable statements are errors that do not stop extraction", "[docgen][extractor]")
{
    Extracted e("int ok1;\n"
                "Generated;\n"
                "DECLARE(x);\n"
                "struct C { ENGINE_NONCOPYABLE(C); void m(); };\n"
                "int ok2;\n");
    REQUIRE(e.Diags() == 3);
    REQUIRE(e.Diag(0) == "2: error: unrecognized declaration");
    REQUIRE(e.Diag(1) == "3: error: macro-generated declaration");
    REQUIRE(e.Diag(2) == "4: error: macro-generated declaration");
    REQUIRE(e.Find("ok1") != nullptr);
    REQUIRE(e.Find("ok2") != nullptr);
    REQUIRE(e.Find("C::m") != nullptr);
}

TEST_CASE("A declaration cut off by a closing brace or end of file is an error", "[docgen][extractor]")
{
    {
        Extracted e("struct S { int a }\n;\nint z;\n");
        REQUIRE(e.Diags() >= 1);
        REQUIRE(e.Diag(0) == "1: error: unrecognized declaration");
    }
    {
        Extracted e("int a;\nint b");
        REQUIRE(e.Diags() == 1);
        REQUIRE(e.Diag(0) == "2: error: unrecognized declaration");
    }
}

TEST_CASE("Extraction is deterministic", "[docgen][extractor]")
{
    Str src = ExtractorTestReadWhole(Str(DOCGEN_FIXTURE_DIR) + "/good/classes.h");
    Extracted a(src), b(src);
    REQUIRE(a.Dump(true) == b.Dump(true));
}

// ---- robustness --------------------------------------------------------------------------

TEST_CASE("Mutated headers never crash or hang the extractor", "[docgen][extractor][fuzz]")
{
    StrList files;
    ExtractorTestListFiles(Str(DOCGEN_FIXTURE_DIR) + "/good", ".h", files);
    ExtractorTestListFiles(Str(DOCGEN_FIXTURE_DIR) + "/bad", ".h", files);
    unsigned long long seed = 0x9E3779B97F4A7C15ull;
    auto next = [&]()
    {
        seed = seed * 6364136223846793005ull + 1442695040888963407ull;
        return static_cast<unsigned>(seed >> 33);
    };
    const char* junk[] = {"{",
                          "}",
                          "(",
                          ")",
                          "[",
                          "]",
                          "<",
                          ">",
                          ";",
                          "template <",
                          "operator",
                          "requires",
                          "enum",
                          "class ",
                          "::",
                          "= ",
                          ": ",
                          "namespace ",
                          "extern \"C\" {",
                          "///",
                          "///<",
                          "\n"};
    for (auto& f : files)
    {
        Str base = ExtractorTestReadWhole(f);
        for (int round = 0; round < 40; ++round)
        {
            Str s = base;
            int edits = 1 + static_cast<int>(next() % 4);
            for (int k = 0; k < edits && !s.Empty(); ++k)
            {
                usize at = next() % s.Size();
                switch (next() % 3)
                {
                case 0: s.Erase(at, 1 + next() % 8); break;
                case 1: s.Insert(at, junk[next() % (sizeof(junk) / sizeof(junk[0]))]); break;
                default: s.Resize(at); break;
                }
            }
            Extracted e(s);
            (void)e.Dump(true);
        }
    }
    SUCCEED();
}
