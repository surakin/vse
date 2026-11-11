

#include <catch2/catch_test_macros.hpp>

#include "fs_fixture.h"
#include "doc_parser.h"
#include "cstr/cstr.h"

using namespace docgen;
using docgen_test::Num;
using docgen_test::Str;
using docgen_test::StrList;

namespace
{

Str DocParserTestStr(StringView v)
{
    return Str(v.Data(), v.Length());
}

struct Parsed
{
    ArenaAllocator arena{usize{64} << 20};
    Str src;
    ScanResult scan{arena};
    ExtractResult ex{arena};
    DocResult docs{arena};

    explicit Parsed(const Str& s) : src(s)
    {
        Scan(StringView(src.Data(), src.Size()), arena, scan);
        Extract(scan, arena, ex);
        ParseDocs(scan, ex, arena, docs);
    }

    usize Diags() const { return docs.diagnostics.Size(); }
    Str Diag(usize i) const
    {
        const Diagnostic& d = docs.diagnostics[i];
        return Num(d.line) + ": " + SeverityName(d.severity) + ": " + DocParserTestStr(d.message);
    }
    bool HasDiag(const Str& text) const
    {
        for (usize i = 0; i < Diags(); ++i)
        {
            if (Diag(i) == text)
            {
                return true;
            }
        }
        return false;
    }
    // Whether any doc diagnostic starts with `prefix` (e.g. "4: error: unknown label").
    bool HasDiagPrefix(const Str& prefix) const
    {
        for (usize i = 0; i < Diags(); ++i)
        {
            if (Diag(i).Compare(0, prefix.Size(), prefix) == 0)
            {
                return true;
            }
        }
        return false;
    }
    const ParsedDoc& Doc(usize i = 0) const { return *docs.parsed[i]; }
    Str Emit(usize i = 0)
    {
        engine_placeholder::StringBuilder<ArenaAllocator> sb(arena);
        EmitDoc(Doc(i), sb);
        return DocParserTestStr(sb.View());
    }
    StrList Undocumented() const
    {
        StrList v;
        for (usize i = 0; i < docs.undocumented.Size(); ++i)
        {
            v.Push(DocParserTestStr(docs.undocumented[i]->qualified));
        }
        return v;
    }
    const Decl* Find(const Str& q) const { return FindIn(ex.root, q); }

private:
    static const Decl* FindIn(const Decl* d, const Str& q)
    {
        if (DocParserTestStr(d->qualified) == q && d->parent != nullptr)
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

// "a\nb" -> "/// a\n/// b\n"; empty lines become "///".
Str AsComment(const Str& text)
{
    Str out;
    usize pos = 0;
    while (pos <= text.Size())
    {
        usize nl = text.Find('\n', pos);
        if (nl == Str::npos)
        {
            nl = text.Size();
        }
        Str line = text.Substr(pos, nl - pos);
        out += line.Empty() ? "///\n" : "/// " + line + "\n";
        pos = nl + 1;
    }
    return out;
}

// A function with `comment` above it.
Str Fn(const Str& comment, const Str& decl = "int f(int a, int b);")
{
    return AsComment(comment) + decl + "\n";
}

Str DocParserTestReadWhole(const Str& path)
{
    return docgen_test::ReadFileText(path);
}

void DocParserTestListFiles(const Str& dir, const char* suffix, StrList& out)
{
    for (const Str& n : docgen_test::ListNamesSorted(dir))
    {
        Str p = dir + "/" + n;
        if (docgen_test::IsDirectoryAt(p))
        {
            DocParserTestListFiles(p, suffix, out);
        }
        else if (p.Size() >= engine_placeholder::CStrLength(suffix) &&
                 p.Compare(p.Size() - engine_placeholder::CStrLength(suffix), engine_placeholder::CStrLength(suffix),
                           suffix) == 0)
        {
            out.Push(p);
        }
    }
}

bool DocOwns(const Str& message)
{
    static const char* prefixes[] = {"unknown label",
                                     "label is not a list item",
                                     "duplicate label",
                                     "Param name does not match",
                                     "Returns on a void",
                                     "missing summary",
                                     "empty label",
                                     "invalid thread-safety",
                                     "malformed",
                                     "unterminated code",
                                     "doc comment is not attached",
                                     "summary"};
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

// ---- fixtures -----------------------------------------------------------------------------

TEST_CASE("Good fixtures have no doc diagnostics", "[docgen][docs][fixtures]")
{
    StrList files;
    DocParserTestListFiles(Str(DOCGEN_FIXTURE_DIR) + "/good", ".h", files);
    REQUIRE(files.Size() >= 13);
    for (auto& f : files)
    {
        INFO(f.CStr());
        Parsed p(DocParserTestReadWhole(f));
        if (p.Diags() > 0)
        {
            INFO(p.Diag(0).CStr());
        }
        REQUIRE(p.Diags() == 0);
        for (usize i = 0; i < p.scan.docs.Size(); ++i)
        {
            REQUIRE(p.docs.parsed[i] != nullptr);
        }
    }
}

TEST_CASE("Bad fixtures: the doc parser reports exactly the diagnostics it owns", "[docgen][docs][fixtures]")
{
    StrList files;
    DocParserTestListFiles(Str(DOCGEN_FIXTURE_DIR) + "/bad", ".h", files);
    int checked = 0;
    for (auto& f : files)
    {
        INFO(f.CStr());
        StrList expected;
        Str exp = DocParserTestReadWhole(f + ".expect");
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
            if (DocOwns(line.Substr(c2 + 2)))
            {
                expected.Push(line);
            }
        }
        Parsed p(DocParserTestReadWhole(f));
        REQUIRE(p.Diags() == expected.Size());
        for (usize i = 0; i < p.Diags(); ++i)
        {
            REQUIRE(p.Diag(i).Compare(0, expected[i].Size(), expected[i]) == 0);
        }
        if (!expected.Empty())
        {
            ++checked;
        }
    }
    REQUIRE(checked >= 5);
}

TEST_CASE("The contract_labels fixture parses into the expected structure", "[docgen][docs]")
{
    Parsed p(DocParserTestReadWhole(Str(DOCGEN_FIXTURE_DIR) + "/good/contract_labels.h"));
    REQUIRE(p.Diags() == 0);
    const ParsedDoc& d = p.Doc(0);
    REQUIRE(DocParserTestStr(d.summary) == "Reads a value from a handle.");
    REQUIRE(d.has_summary);
    REQUIRE(d.blocks.Size() == 2);
    REQUIRE(d.blocks[0].kind == BlockKind::Paragraph);
    REQUIRE(DocParserTestStr(d.blocks[0].text) ==
            "Details paragraph: this explains the behaviour in more depth and may span several lines.");
    REQUIRE(d.blocks[1].kind == BlockKind::List);
    REQUIRE(d.blocks[1].item_count == 2);
    REQUIRE(DocParserTestStr(d.items[d.blocks[1].first_item + 1]) ==
            "Second bullet, which continues on a second line.");

    const LabelKind order[] = {LabelKind::Param,     LabelKind::Param,   LabelKind::Returns,
                               LabelKind::Fails,     LabelKind::Aborts,  LabelKind::ThreadSafety,
                               LabelKind::Ownership, LabelKind::Example, LabelKind::See};
    REQUIRE(d.labels.Size() == 9);
    for (usize i = 0; i < 9; ++i)
    {
        REQUIRE(d.labels[i].kind == order[i]);
    }
    REQUIRE(DocParserTestStr(d.labels[0].name) == "handle");
    REQUIRE(DocParserTestStr(d.labels[1].name) == "out");
    REQUIRE(DocParserTestStr(d.labels[5].text) == "thread-compatible. Distinct handles may be read concurrently.");
    REQUIRE(d.labels[7].has_code);
    REQUIRE(DocParserTestStr(d.labels[7].code_lang) == "cpp");
    REQUIRE(DocParserTestStr(d.labels[7].code) == "int v;\nRead(3, &v);");
    REQUIRE(d.labels[8].ref_count == 2);
    REQUIRE(d.refs.Size() == 2);
    REQUIRE(DocParserTestStr(d.refs[0].target) == "Read");
    REQUIRE(DocParserTestStr(d.refs[1].target) == "Write");
    REQUIRE(d.labels[0].line == 12);

    REQUIRE(p.Doc(1).Find(LabelKind::Deprecated) != nullptr);
    REQUIRE(p.Doc(2).internal);
    REQUIRE_FALSE(p.Doc(3).internal);
}

// ---- structure ------------------------------------------------------------------------------

TEST_CASE("Summary is the first paragraph; later paragraphs are details", "[docgen][docs]")
{
    Parsed p(Fn("First line of\nthe summary.\n\nSecond paragraph.\nstill second.\n\nThird."));
    REQUIRE(p.Diags() == 0);
    REQUIRE(DocParserTestStr(p.Doc().summary) == "First line of the summary.");
    REQUIRE(p.Doc().blocks.Size() == 2);
    REQUIRE(DocParserTestStr(p.Doc().blocks[0].text) == "Second paragraph. still second.");
    REQUIRE(DocParserTestStr(p.Doc().blocks[1].text) == "Third.");
}

TEST_CASE("Lists, code blocks and inline markup", "[docgen][docs]")
{
    Parsed p(Fn(
        "Summary.\n\n- one\n- two with `[[not a ref]]`\n  and a continuation\n\n```cpp\nint x;\n\nint y;\n```\n\nAfter."));
    REQUIRE(p.Diags() == 0);
    const ParsedDoc& d = p.Doc();
    REQUIRE(d.blocks.Size() == 3);
    REQUIRE(d.blocks[0].kind == BlockKind::List);
    REQUIRE(d.blocks[0].item_count == 2);
    REQUIRE(DocParserTestStr(d.items[1]) == "two with `[[not a ref]]` and a continuation");
    REQUIRE(d.refs.Size() == 0);  // inside a code span
    REQUIRE(d.blocks[1].kind == BlockKind::Code);
    REQUIRE(DocParserTestStr(d.blocks[1].lang) == "cpp");
    REQUIRE(DocParserTestStr(d.blocks[1].text) == "int x;\n\nint y;");  // blank line kept inside the fence
    REQUIRE(DocParserTestStr(d.blocks[2].text) == "After.");
}

TEST_CASE("Labels continue on indented lines and end at a blank line", "[docgen][docs]")
{
    Parsed p(Fn("Summary.\n- Returns: the value,\n  or zero\n  on failure.\n- Fails: never.\n\nTrailing paragraph."));
    REQUIRE(p.Diags() == 0);
    const ParsedDoc& d = p.Doc();
    REQUIRE(d.labels.Size() == 2);
    REQUIRE(DocParserTestStr(d.Find(LabelKind::Returns)->text) == "the value, or zero on failure.");
    REQUIRE(DocParserTestStr(d.Find(LabelKind::Fails)->text) == "never.");
    REQUIRE(d.blocks.Size() == 1);
    REQUIRE(DocParserTestStr(d.blocks[0].text) == "Trailing paragraph.");
}

TEST_CASE("A label written as a list item is a label, and its flush continuation lines belong to it", "[docgen][docs]")
{
    // The form clang-format's comment reflow leaves alone: `- Label:` on its own line, wrapped text on flush lines
    // below it.
    Parsed p(Fn(
        "Summary.\n- Returns: the value,\nor zero\non failure.\n- Fails: never.\n- Param a: the input,\nwrapped.\n\nTrailing paragraph."));
    REQUIRE(p.Diags() == 0);
    const ParsedDoc& d = p.Doc();
    REQUIRE(d.labels.Size() == 3);
    REQUIRE(DocParserTestStr(d.Find(LabelKind::Returns)->text) == "the value, or zero on failure.");
    REQUIRE(DocParserTestStr(d.Find(LabelKind::Fails)->text) == "never.");
    REQUIRE(DocParserTestStr(d.Find(LabelKind::Param)->name) == "a");
    REQUIRE(DocParserTestStr(d.Find(LabelKind::Param)->text) == "the input, wrapped.");
    REQUIRE(d.blocks.Size() == 1);
    REQUIRE(DocParserTestStr(d.blocks[0].text) == "Trailing paragraph.");
    REQUIRE(d.items.Size() == 0);
}

TEST_CASE("The list-item form and the bare form read the same", "[docgen][docs]")
{
    Parsed bare(
        Fn("Summary.\n- Returns: r,\n  more.\n- Fails: never.\n- Thread-safety: thread-safe.\n- Ownership: borrowed."));
    Parsed dashed(
        Fn("Summary.\n- Returns: r,\nmore.\n- Fails: never.\n- Thread-safety: thread-safe.\n- Ownership: borrowed."));
    REQUIRE(bare.Diags() == 0);
    REQUIRE(dashed.Diags() == 0);
    REQUIRE(bare.Doc().labels.Size() == dashed.Doc().labels.Size());
    for (usize i = 0; i < bare.Doc().labels.Size(); ++i)
    {
        REQUIRE(bare.Doc().labels[i].kind == dashed.Doc().labels[i].kind);
        REQUIRE(DocParserTestStr(bare.Doc().labels[i].text) == DocParserTestStr(dashed.Doc().labels[i].text));
    }
}

TEST_CASE("A list-item label takes the fence of Example, and an unknown one is still an error", "[docgen][docs]")
{
    Parsed p(Fn("Summary.\n- Example: basic use.\n```cpp compile\nf(1, 2);\n```\n- See: [[f]]"));
    REQUIRE(p.Diags() == 0);
    const Label* ex = p.Doc().Find(LabelKind::Example);
    REQUIRE(ex != nullptr);
    REQUIRE(DocParserTestStr(ex->code) == "f(1, 2);");
    Parsed unknown(Fn("Summary.\n- Frobnicates: not real,\nand continues.\n- Returns: ok."));
    REQUIRE(unknown.Diags() == 1);
    REQUIRE(unknown.Diag(0) == "2: error: unknown label 'Frobnicates'");
    REQUIRE(unknown.Doc().labels.Size() == 1);
}

TEST_CASE("An ordinary list item continues on flush lines; a blank line ends it", "[docgen][docs]")
{
    Parsed p(Fn("Summary.\n\n- first item,\nwrapped\n- second\n\nAfter."));
    REQUIRE(p.Diags() == 0);
    const ParsedDoc& d = p.Doc();
    REQUIRE(d.items.Size() == 2);
    REQUIRE(DocParserTestStr(d.items[0]) == "first item, wrapped");
    REQUIRE(DocParserTestStr(d.items[1]) == "second");
    REQUIRE(d.blocks.Size() == 2);
    REQUIRE(DocParserTestStr(d.blocks[1].text) == "After.");
}

TEST_CASE("A label that is not a list item is an error that says how to fix it", "[docgen][docs]")
{
    Parsed p(Fn("Summary.\nFails: never.\n- Returns: ok."));
    REQUIRE(p.Diags() == 1);
    REQUIRE(p.Diag(0).Compare(0, 40, "2: error: label is not a list item: writ") == 0);
    REQUIRE(p.Doc().labels.Size() == 2);  // still read, so one mistake is one error
}

TEST_CASE("Example takes an immediately following fenced block", "[docgen][docs]")
{
    Parsed p(Fn("Summary.\n- Example: basic use.\n```cpp compile\nf(1, 2);\n```\n- See: [[f]]"));
    REQUIRE(p.Diags() == 0);
    const Label* ex = p.Doc().Find(LabelKind::Example);
    REQUIRE(ex != nullptr);
    REQUIRE(DocParserTestStr(ex->text) == "basic use.");
    REQUIRE(DocParserTestStr(ex->code_lang) == "cpp compile");
    REQUIRE(DocParserTestStr(ex->code) == "f(1, 2);");
    REQUIRE(p.Doc().blocks.Size() == 0);
    REQUIRE(p.Doc().Find(LabelKind::See) != nullptr);
}

TEST_CASE("Prose that merely contains a colon is not a label", "[docgen][docs]")
{
    Parsed p(
        Fn("Returns the sum: a plus b.\n\nSee the std::vector docs, e.g. http://x.y/z: ok.\nlower: case is prose."));
    REQUIRE(p.Diags() == 0);
    REQUIRE(p.Doc().labels.Size() == 0);
}

// ---- label errors ---------------------------------------------------------------------------

TEST_CASE("Unknown labels are errors at their line and swallow their continuation", "[docgen][docs]")
{
    Parsed p(Fn("Summary.\nFrobnicates: not real,\n  and continues.\n- Returns: ok."));
    REQUIRE(p.Diags() == 1);
    REQUIRE(p.Diag(0) == "2: error: unknown label 'Frobnicates'");
    REQUIRE(p.Doc().labels.Size() == 1);
}

TEST_CASE("Duplicate labels are errors; Param is per parameter", "[docgen][docs]")
{
    {
        Parsed p(Fn("Summary.\n- Returns: a.\n- Returns: b."));
        REQUIRE(p.Diags() == 1);
        REQUIRE(p.Diag(0) == "3: error: duplicate label 'Returns'");
    }
    {
        Parsed p(Fn("Summary.\n- Param a: one.\n- Param b: two."));
        REQUIRE(p.Diags() == 0);
    }
    {
        Parsed p(Fn("Summary.\n- Param a: one.\n- Param a: again."));
        REQUIRE(p.Diags() == 1);
        REQUIRE(p.Diag(0) == "3: error: duplicate label 'a'");
    }
}

TEST_CASE("The Backend label is known and keeps its text", "[docgen][docs]")
{
    Parsed p(Fn("Summary.\n- Backend: Windows IsDebuggerPresent; macOS sysctl P_TRACED."));
    REQUIRE(p.Diags() == 0);
    REQUIRE(Str(LabelName(LabelKind::Backend)) == "Backend");
}

TEST_CASE("Empty labels are errors", "[docgen][docs]")
{
    Parsed p(Fn("Summary.\n- Returns:\n- Internal:\n- Example:"));
    REQUIRE(p.Diags() == 3);
    REQUIRE(p.Diag(0) == "2: error: empty label 'Returns'");
    REQUIRE(p.Diag(1) == "3: error: empty label 'Internal'");
    REQUIRE(p.Diag(2) == "4: error: empty label 'Example'");
}

TEST_CASE("Thread-safety must start with a known category", "[docgen][docs]")
{
    for (const char* c :
         {"immutable", "thread-safe", "thread-compatible", "owner-thread-only", "single-consumer", "not-thread-safe"})
    {
        Parsed p(Fn(Str("Summary.\n- Thread-safety: ") + c + ". Details."));
        INFO(c);
        REQUIRE(p.Diags() == 0);
    }
    Parsed bad(Fn("Summary.\n- Thread-safety: safe-ish."));
    REQUIRE(bad.Diags() == 1);
    REQUIRE(bad.Diag(0) == "2: error: invalid thread-safety category 'safe-ish'");
    Parsed caps(Fn("Summary.\n- Thread-safety: Thread-safe."));
    REQUIRE(caps.Diags() == 1);
}

TEST_CASE("See needs a reference; malformed references are errors", "[docgen][docs]")
{
    {
        Parsed p(Fn("Summary.\n- See: the other function."));
        REQUIRE(p.Diags() == 1);
        REQUIRE(p.Diag(0).Compare(0, 24, "2: error: malformed labe") == 0);
    }
    {
        Parsed p(Fn("Summary with [[Broken."));
        REQUIRE(p.Diags() == 1);
        REQUIRE(p.Diag(0) == "1: error: malformed reference: missing ]]");
    }
    {
        Parsed p(Fn("Summary with [[ ]] nothing."));
        REQUIRE(p.Diags() == 1);
        REQUIRE(p.Diag(0).Compare(0, 33, "1: error: malformed reference: em") == 0);
    }
    {
        Parsed p(Fn("Summary with [[a::B::c(Counter&)]] and [[X]]."));
        REQUIRE(p.Diags() == 0);
        REQUIRE(DocParserTestStr(p.Doc().refs[0].target) == "a::B::c(Counter&)");
        REQUIRE(p.Doc().refs.Size() == 2);
    }
}

TEST_CASE("An unterminated code fence is an error at the fence", "[docgen][docs]")
{
    Parsed p(Fn("Summary.\n\n```cpp\nint x;"));
    REQUIRE(p.Diags() == 1);
    REQUIRE(p.Diag(0) == "3: error: unterminated code fence");
    REQUIRE(DocParserTestStr(p.Doc().blocks[0].text) == "int x;");
}

TEST_CASE("Summary rules", "[docgen][docs]")
{
    {
        Parsed p(Fn("\n- Param a: only a label."));
        REQUIRE(p.HasDiag("1: error: missing summary"));
        REQUIRE_FALSE(p.Doc().has_summary);
    }
    {
        Parsed p(Fn("- starts with a list"));
        REQUIRE(p.HasDiag("1: error: missing summary"));
    }
    {
        Parsed p(Fn("No period at the end"));
        REQUIRE(p.Diags() == 1);
        REQUIRE(p.Diag(0) == "1: warning: summary does not end in a period");
    }
    {
        Parsed p(Fn(Str(170, 'x') + "."));
        REQUIRE(p.Diags() == 1);
        REQUIRE(p.Diag(0).Compare(0, 25, "1: warning: summary too l") == 0);
    }
}

// ---- validation against the declaration -------------------------------------------------------

TEST_CASE("Param names must match real parameters", "[docgen][docs]")
{
    {
        Parsed p(Fn("Adds.\n- Param a: first.\n- Param b: second."));
        REQUIRE(p.Diags() == 0);
    }
    {
        Parsed p(Fn("Adds.\n- Param c: nope."));
        REQUIRE(p.Diags() == 1);
        REQUIRE(p.Diag(0) == "2: error: Param name does not match a parameter: 'c'");
    }
    {
        Parsed p(Fn("Adds.\n- Param: no name."));
        REQUIRE(p.Diags() == 1);
        REQUIRE(p.Diag(0).Compare(0, 40, "2: error: malformed label: Param needs a") == 0);
    }
    {  // typo: case matters
        Parsed p(Fn("Adds.\n- Param A: x."));
        REQUIRE(p.Diags() == 1);
    }
}

TEST_CASE("Parameter names are extracted from every declarator form", "[docgen][docs]")
{
    Parsed p(
        "/// F.\nvoid F(const char* name, unsigned count, bool (*done)(void* user), int (&arr)[3], std::usize, const Foo&, "
        "Foo<int, 2>& tmpl, int def = 4, Args... rest);\n"
        "/// G.\nvoid G(void);\n"
        "/// H.\nvoid H(int, ...);\n");
    const Decl* f = p.Find("F");
    Str names;
    for (usize i = 0; i < f->params.Size(); ++i)
    {
        names += DocParserTestStr(f->params[i]) + ",";
    }
    REQUIRE(names == "name,count,done,arr,tmpl,def,rest,");
    REQUIRE(p.Find("G")->params.Size() == 0);
    REQUIRE(p.Find("H")->params.Size() == 0);
}

TEST_CASE("Param works on macros; other declarations have no parameters", "[docgen][docs]")
{
    {
        Parsed p("/// Squares.\n/// - Param x: the value.\n#define SQ(x) ((x) * (x))\n");
        REQUIRE(p.Diags() == 0);
    }
    {
        Parsed p("/// Squares.\n/// - Param y: oops.\n#define SQ(x) ((x) * (x))\n");
        REQUIRE(p.Diags() == 1);
    }
    {
        Parsed p("/// A class.\n/// - Param x: classes have none.\nclass C {};\n");
        REQUIRE(p.Diags() == 1);
    }
}

TEST_CASE("Returns on a void function is a warning", "[docgen][docs]")
{
    {
        Parsed p(Fn("Does.\n- Returns: nothing.", "void f();"));
        REQUIRE(p.Diags() == 1);
        REQUIRE(p.Diag(0) == "2: warning: Returns on a void function");
    }
    {
        Parsed p(Fn("Does.\n- Returns: ok.", "static inline void f();"));
        REQUIRE(p.Diags() == 1);
    }
    {
        Parsed p(Fn("Does.\n- Returns: nothing.", "auto f() -> void;"));
        REQUIRE(p.Diags() == 1);
    }
    {
        Parsed p(
            "struct S {\n/// Makes one.\n/// - Returns: nothing.\nS();\n/// Destroys.\n/// - Returns: nothing.\n~S();\n};\n");
        REQUIRE(p.Diags() == 2);
    }
    for (const char* decl :
         {"int f();", "void* f();", "const void* f();", "auto f() -> void*;", "void_t f();", "std::vector<void> f();"})
    {
        Parsed p(Fn("Does.\n- Returns: a value.", decl));
        INFO(decl);
        REQUIRE(p.Diags() == 0);
    }
}

// ---- undocumented symbols and inheritance --------------------------------------------------------

TEST_CASE("Undocumented public symbols are listed, not errors", "[docgen][docs]")
{
    Parsed p(
        "/// Documented.\nvoid A();\n"
        "void B();\n"
        "namespace n { void C(); }\n"
        "namespace detail { void D(); }\n"
        "/// Class.\nclass K {\npublic:\n  void M();\n  int v; ///< Trailing doc.\n  int w;\nprivate:\n  void P();\n};\n"
        "enum class E { X, ///< Doc.\n Y };\n");
    REQUIRE(p.Diags() == 0);
    StrList want = {"B", "n::C", "K::M", "K::w", "E", "E::Y"};
    REQUIRE(p.Undocumented() == want);
}

TEST_CASE("An override without a doc comment inherits from the base", "[docgen][docs]")
{
    Parsed p("/// A.\nstruct A {\n /// Base summary.\n virtual int F(int) const;\n /// Other.\n virtual void G();\n};\n"
             "struct B : A { int F(int) const override; void G() override; int Other(long); };\n"
             "struct C : B {\n int F(int) const override;\n /// Own docs.\n void G() override;\n};\n");
    REQUIRE(p.Diags() == 0);
    const Decl* bf = p.Find("B::F");
    const Decl* base = ResolveInherited(*bf, p.ex);
    REQUIRE(base != nullptr);
    REQUIRE(DocParserTestStr(base->qualified) == "A::F");
    // Through a chain: C::F -> B::F (undocumented) -> A::F.
    REQUIRE(DocParserTestStr(ResolveInherited(*p.Find("C::F"), p.ex)->qualified) == "A::F");
    // Not an override, or no matching signature: nothing to inherit.
    REQUIRE(ResolveInherited(*p.Find("A::F"), p.ex) == nullptr);
    // Inheriting members are not reported as undocumented.
    for (auto& n : p.Undocumented())
    {
        INFO(n.CStr());
        REQUIRE(n != "B::F");
        REQUIRE(n != "C::F");
    }
}

TEST_CASE("An override whose bases have no docs stays undocumented; constness must match", "[docgen][docs]")
{
    Parsed p("struct A { virtual void F(); virtual void H() const; };\n"
             "struct B : A { void F() override; void H() override; };\n");
    REQUIRE(ResolveInherited(*p.Find("B::F"), p.ex) == nullptr);
    REQUIRE(ResolveInherited(*p.Find("B::H"), p.ex) == nullptr);
    auto u = p.Undocumented();
    REQUIRE(u.Has("B::F"));
}

// ---- attachment ---------------------------------------------------------------------------

TEST_CASE("A doc comment on a non-declaration is an error", "[docgen][docs]")
{
    {
        Parsed p("struct S;\n/// Oops.\nstatic_assert(true);\nvoid g();\n");
        REQUIRE(p.HasDiag("2: error: doc comment is not attached to a declaration"));
    }
    {
        Parsed p("/// Doc.\nusing namespace std;\n");
        REQUIRE(p.HasDiag("1: error: doc comment is not attached to a declaration"));
    }
    {  // declarations that were extracted do not trigger it, even when reopened
        Parsed p("/// One.\nnamespace n { void A(); }\n/// Two.\nnamespace n { void B(); }\n");
        REQUIRE(p.Diags() == 0);
    }
    {  // an extraction error suppresses the follow-up
        Parsed p("/// Doc.\nDECLARE(x);\n");
        REQUIRE_FALSE(p.HasDiagPrefix("1: error: doc comment"));
    }
}

// ---- round trip ---------------------------------------------------------------------------

TEST_CASE("Emit then parse then emit is stable", "[docgen][docs][roundtrip]")
{
    StrList files;
    DocParserTestListFiles(Str(DOCGEN_FIXTURE_DIR) + "/good", ".h", files);
    int docs = 0;
    for (auto& f : files)
    {
        INFO(f.CStr());
        Parsed p(DocParserTestReadWhole(f));
        for (usize i = 0; i < p.scan.docs.Size(); ++i)
        {
            if (p.scan.docs[i].kind != DocKind::Declaration)
            {
                continue;
            }
            Str e1 = p.Emit(i);
            Parsed q(AsComment(e1.Substr(0, e1.Size() - 1)) + "void g();\n");  // drop the final '\n'
            INFO(e1.CStr());
            Str e2 = q.Emit(0);
            REQUIRE(e1 == e2);
            ++docs;
        }
    }
    REQUIRE(docs >= 30);
}

TEST_CASE("Round trip preserves a rich comment exactly", "[docgen][docs][roundtrip]")
{
    Parsed p(Fn("Summary line.\n\nDetails with [[Ref]] and `code`.\n\n- a\n- b\n  more\n\n```cpp\nx;\n\ny;\n```\n\n"
                "- Param a: first,\n  continued.\n- Returns: r.\n- Example: e.\n```cpp\nz;\n```\n- See: [[A]]"));
    Str e1 = p.Emit();
    REQUIRE(e1 == "Summary line.\n\nDetails with [[Ref]] and `code`.\n\n- a\n- b more\n\n```cpp\nx;\n\ny;\n```\n\n"
                  "- Param a: first, continued.\n- Returns: r.\n- Example: e.\n```cpp\nz;\n```\n- See: [[A]]\n");
    Parsed q(Fn(e1.Substr(0, e1.Size() - 1)));
    REQUIRE(q.Emit() == e1);
}

// ---- robustness -------------------------------------------------------------------------------

TEST_CASE("Mutated comments never crash the doc parser", "[docgen][docs][fuzz]")
{
    StrList files;
    DocParserTestListFiles(Str(DOCGEN_FIXTURE_DIR) + "/good", ".h", files);
    DocParserTestListFiles(Str(DOCGEN_FIXTURE_DIR) + "/bad", ".h", files);
    unsigned long long seed = 0x2545F4914F6CDD1Dull;
    auto next = [&]()
    {
        seed = seed * 6364136223846793005ull + 1442695040888963407ull;
        return static_cast<unsigned>(seed >> 33);
    };
    const char* junk[] = {"[[", "]]", "`",  "```",   "Param ", "Returns:", "Example:",      "See: ",
                          ":",  "- ", "  ", "///\n", "/// ",   "\n\n",     "Thread-safety:"};
    for (auto& f : files)
    {
        Str base = DocParserTestReadWhole(f);
        for (int round = 0; round < 40; ++round)
        {
            Str s = base;
            int edits = 1 + static_cast<int>(next() % 5);
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
            Parsed p(s);
            for (usize i = 0; i < p.scan.docs.Size(); ++i)
            {
                (void)p.Emit(i);
            }
        }
    }
    SUCCEED();
}

// ---- code-review regressions (Day 39) ---------------------------------------------------------

TEST_CASE("[review] overrides inherit even when parameter names differ from the base", "[docgen][docs][review]")
{
    Parsed p("struct A {\n /// Hook.\n virtual void Hook(int);\n /// Get.\n virtual void Get(int a, int b);\n};\n"
             "struct D : A { void Hook(int x) override; void Get(int, int b) override; };\n");
    REQUIRE(p.Find("D::Hook")->param_count == 1);
    REQUIRE(p.Find("A::Hook")->param_count == 1);
    REQUIRE(p.Find("D::Get")->params.Size() == 1);
    REQUIRE(p.Find("D::Get")->param_count == 2);
    REQUIRE(ResolveInherited(*p.Find("D::Hook"), p.ex) != nullptr);
    REQUIRE(ResolveInherited(*p.Find("D::Get"), p.ex) != nullptr);
    for (auto& n : p.Undocumented())
    {
        INFO(n.CStr());
        REQUIRE(n != "D::Hook");
        REQUIRE(n != "D::Get");
    }
}

TEST_CASE("[review] pointer-to-member parameters are named; trailing const is not a name", "[docgen][docs][review]")
{
    Parsed p("/// F.\n/// - Param pm: the member.\n/// - Param n: count.\nvoid F(void (S::*pm)(int), int n);\n"
             "/// G.\nvoid G(int const, Foo const&, int const x);\n");
    REQUIRE(p.Diags() == 0);
    REQUIRE(p.Find("F")->param_count == 2);
    REQUIRE(p.Find("G")->param_count == 3);
    REQUIRE(p.Find("G")->params.Size() == 1);
    REQUIRE(DocParserTestStr(p.Find("G")->params[0]) == "x");
    Parsed q("/// G.\n/// - Param const: bogus.\nvoid G(int const);\n");
    REQUIRE(q.Diags() == 1);
}

TEST_CASE("[review] comments on forward declarations, definitions and friends are accepted", "[docgen][docs][review]")
{
    Parsed p("struct A { void Hook(int); static int s; };\n"
             "/// Out of line.\nvoid A::Hook(int) {}\n"
             "/// Static.\nint A::s = 3;\n"
             "/// Forward.\nclass Fwd;\n"
             "/// Opaque.\nenum class E : int;\n"
             "struct B {\n  /// Friend.\n  friend class A;\n};\n");
    REQUIRE(p.Diags() == 0);
    // ...but a comment on a statement that is not a declaration at all is still refused.
    Parsed q("/// No.\nstatic_assert(true);\n");
    REQUIRE(q.HasDiag("1: error: doc comment is not attached to a declaration"));
}

TEST_CASE("[review] macro parameters: GNU named variadics and cut-off signatures", "[docgen][docs][review]")
{
    Parsed p(
        "/// M.\n/// - Param x: x.\n/// - Param y: rest.\n#define M(x, y...) x\n/// N.\n/// - Param z: z.\n#define N(z, ...) z\n");
    REQUIRE(p.Diags() == 0);
    REQUIRE(p.Find("M")->param_count == 2);
    REQUIRE(p.Find("N")->param_count == 2);
    REQUIRE(p.Find("N")->params.Size() == 1);
    Parsed q("/// Cut.\n#define CUT(a, bb\nint x;\n");
    REQUIRE(q.Find("CUT") != nullptr);  // does not read past the directive
}

TEST_CASE("[review] a reference may wrap across comment lines", "[docgen][docs][review]")
{
    Parsed p(Fn("Summary.\n\nSee also [[jobs::Scheduler::\nHelpUntil]] for details."));
    REQUIRE(p.Diags() == 0);
    REQUIRE(p.Doc().refs.Size() == 1);
    REQUIRE(DocParserTestStr(p.Doc().refs[0].target) == "jobs::Scheduler:: HelpUntil");
    REQUIRE(p.Doc().refs[0].line == 3);
    // Errors are reported on the line where the reference starts.
    Parsed q(Fn("Summary.\n\nFirst line.\nSecond [[Broken line."));
    REQUIRE(q.HasDiag("4: error: malformed reference: missing ]]"));
    // A See: label whose reference wraps still counts it.
    Parsed r(Fn("Summary.\n- See: [[a::\n  b]]"));
    REQUIRE(r.Diags() == 0);
}
