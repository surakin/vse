

#include <catch2/catch_test_macros.hpp>

#include "fs_fixture.h"
#include "scanner.h"
#include "source_file.h"
#include "cstr/cstr.h"

using namespace docgen;
using docgen_test::Num;
using docgen_test::Str;
using docgen_test::StrList;
using docgen_test::StrPairList;

namespace {

struct Scanned {
    ArenaAllocator arena{usize{64} << 20};
    Str src;
    ScanResult r{arena};
    explicit Scanned(const Str& s) : src(s) { Scan(StringView(src.Data(), src.Size()), arena, r); }

    Str Text(usize i) const { return Str(r.tokens[i].text.Data(), r.tokens[i].text.Length()); }
    StrList Texts() const {
        StrList v;
        for (usize i = 0; i < r.tokens.Size(); ++i) v.Push(Text(i));
        return v;
    }
    Str DocText(usize i) const { return Str(r.docs[i].text.Data(), r.docs[i].text.Length()); }
    Str Diag(usize i) const {
        const Diagnostic& d = r.diagnostics[i];
        return Num(d.line) + ": " + SeverityName(d.severity) + ": " + Str(d.message.Data(), d.message.Length());
    }
    usize Diags() const { return r.diagnostics.Size(); }
};

using Strs = StrList;

Str ReadWhole(const Str& path) { return docgen_test::ReadFileText(path); }

void ListFiles(const Str& dir, const char* suffix, StrList& out) {
    for (const Str& n : docgen_test::ListNamesSorted(dir)) {
        Str p = dir + "/" + n;
        if (docgen_test::IsDirectoryAt(p)) ListFiles(p, suffix, out);
        else if (p.Size() >= engine_placeholder::CStrLength(suffix) && p.Compare(p.Size() - engine_placeholder::CStrLength(suffix), engine_placeholder::CStrLength(suffix), suffix) == 0)
            out.Push(p);
    }
}

} // namespace

TEST_CASE("Tokens, positions and punctuation", "[docgen][scanner]") {
    Scanned s("int x = a::b->c;\n  Foo<Bar<int>> v; auto f() -> int; x <=> y; f(...);\n");
    REQUIRE(s.Diags() == 0);
    REQUIRE(s.Texts() == Strs{"int", "x", "=", "a", "::", "b", "->", "c", ";", "Foo", "<", "Bar", "<", "int", ">", ">", "v", ";",
                              "auto", "f", "(", ")", "->", "int", ";", "x", "<=>", "y", ";", "f", "(", "...", ")", ";"});
    REQUIRE(s.r.tokens[0].line == 1);
    REQUIRE(s.r.tokens[0].column == 1);
    REQUIRE(s.r.tokens[1].column == 5);
    REQUIRE(s.r.tokens[9].line == 2);
    REQUIRE(s.r.tokens[9].column == 3);
}

TEST_CASE("Comments are skipped and block comments count lines", "[docgen][scanner]") {
    Scanned s("a // line comment }{\n/* block\n   comment { */ b\n// trailing \\\ncontinued comment\nc");
    REQUIRE(s.Diags() == 0);
    REQUIRE(s.Texts() == Strs{"a", "b", "c"});
    REQUIRE(s.r.tokens[1].line == 3);
    REQUIRE(s.r.tokens[2].line == 6);
}

TEST_CASE("String, char and raw-string literals hide their contents", "[docgen][scanner]") {
    Scanned s(
        "const char* a = \"http://x//y\";\n"
        "const char* b = \"/* not */\";\n"
        "const char* c = \"}{ \\\" \\\\ // \";\n"
        "char d = '/'; char e = '*'; char f = '\\''; char g = '\"';\n"
        "const char* h = R\"delim(raw ) \" } // )delim\";\n"
        "const char* i = u8\"x\"; auto j = L'x'; auto k = R\"(a\nb)\";\n"
        "int l;\n");
    REQUIRE(s.Diags() == 0);
    auto t = s.Texts();
    // Every literal is exactly one token.
    REQUIRE(t[5] == "\"http://x//y\"");
    REQUIRE(t[12] == "\"/* not */\"");
    int raw = 0, lit = 0;
    for (usize i = 0; i < s.r.tokens.Size(); ++i) {
        if (s.r.tokens[i].kind == TokenKind::StringLiteral) ++lit;
        if (s.Text(i).RFind("R\"", 0) == 0) ++raw;
    }
    REQUIRE(raw == 2);
    REQUIRE(lit == 6);
    // The multi-line raw string advanced the line counter: `int l;` is on line 8.
    REQUIRE(s.r.tokens[s.r.tokens.Size() - 2].line == 8);
}

TEST_CASE("Numbers, digit separators and exponents are single tokens", "[docgen][scanner]") {
    Scanned s("int a = 1'000'000; int b = 0xFF'FF; double c = 1.5e-3; double d = .5; double e = 0x1.8p+3; int f = 12u;");
    REQUIRE(s.Diags() == 0);
    auto t = s.Texts();
    REQUIRE(t[3] == "1'000'000");
    REQUIRE(t[8] == "0xFF'FF");
    REQUIRE(t[13] == "1.5e-3");
    REQUIRE(t[18] == ".5");
    REQUIRE(t[23] == "0x1.8p+3");
    REQUIRE(t[28] == "12u");
}

TEST_CASE("#if 0 blocks are skipped without lexing their content", "[docgen][scanner]") {
    Scanned s(
        "a\n"
        "#if 0\n"
        "this is { not valid \" C++\n"
        "/// ignored doc\n"
        "#if defined(X)\n"
        "nested\n"
        "#endif\n"
        "still skipped\n"
        "#else\n"
        "b\n"
        "#endif\n"
        "c\n");
    REQUIRE(s.Diags() == 0);
    REQUIRE(s.Texts() == Strs{"a", "b", "c"});
    REQUIRE(s.r.docs.Size() == 0);
    REQUIRE(s.r.tokens[2].line == 12);
}

TEST_CASE("Preprocessor lines: includes are skipped, #define is captured with continuations", "[docgen][scanner]") {
    Scanned s(
        "#include <vector>\n"
        "#pragma once\n"
        "/// A macro.\n"
        "#define SQUARE(x) \\\n"
        "    ((x) * (x))\n"
        "#define PLAIN 1\n"
        "int after;\n");
    REQUIRE(s.Diags() == 0);
    REQUIRE(s.r.tokens.Size() == 5);
    REQUIRE(s.r.tokens[0].kind == TokenKind::Define);
    REQUIRE(s.Text(0).RFind("SQUARE(x)", 0) == 0);
    REQUIRE(s.Text(0).Find("((x) * (x))") != Str::npos);
    REQUIRE(s.r.tokens[0].doc == 0); // the doc comment attaches to the define
    REQUIRE(s.Text(1) == "PLAIN 1");
    REQUIRE(s.r.tokens[1].doc == -1);
    REQUIRE(s.r.tokens[2].line == 7);
}

TEST_CASE("Doc comments: runs merge, markers and one space are stripped", "[docgen][scanner]") {
    Scanned s(
        "/// Summary line.\n"
        "///\n"
        "///   indented detail\n"
        "///no space\n"
        "void F();\n"
        "    /// Indented doc.\n"
        "    void G();\n");
    REQUIRE(s.Diags() == 0);
    REQUIRE(s.r.docs.Size() == 2);
    REQUIRE(s.DocText(0) == "Summary line.\n\n  indented detail\nno space");
    REQUIRE(s.r.docs[0].kind == DocKind::Declaration);
    REQUIRE(s.r.docs[0].first_line == 1);
    REQUIRE(s.r.docs[0].last_line == 4);
    REQUIRE(s.Text(static_cast<usize>(s.r.docs[0].token)) == "void");
    REQUIRE(s.r.tokens[static_cast<usize>(s.r.docs[0].token)].doc == 0);
    REQUIRE(s.DocText(1) == "Indented doc.");
}

TEST_CASE("Banner comments (////) and plain comments are not doc comments", "[docgen][scanner]") {
    Scanned s(
        "//////////////////\n"
        "// plain\n"
        "/** javadoc style is not a doc comment */\n"
        "void F();\n");
    REQUIRE(s.Diags() == 0);
    REQUIRE(s.r.docs.Size() == 0);
}

TEST_CASE("A blank line detaches a doc comment (warning)", "[docgen][scanner]") {
    Scanned s("/// Detached.\n\nvoid F();\n");
    REQUIRE(s.Diags() == 1);
    REQUIRE(s.Diag(0) == "1: warning: detached doc comment");
    REQUIRE(s.r.tokens[0].doc == -1);
}

TEST_CASE("A plain comment or a directive between doc and declaration does not detach", "[docgen][scanner]") {
    Scanned s("/// Doc A.\n// note\nvoid A();\n/// Doc B.\n#if X\nvoid B();\n#endif\n");
    REQUIRE(s.Diags() == 0);
    REQUIRE(s.r.docs.Size() == 2);
    REQUIRE(s.r.tokens[0].doc == 0);
    REQUIRE(s.Text(static_cast<usize>(s.r.docs[1].token)) == "void");
}

TEST_CASE("Two doc runs separated by a blank line: the first is detached", "[docgen][scanner]") {
    Scanned s("/// First.\n\n/// Second.\nvoid F();\n");
    REQUIRE(s.Diags() == 1);
    REQUIRE(s.Diag(0) == "1: warning: detached doc comment");
    REQUIRE(s.r.tokens[0].doc == 1);
}

TEST_CASE("A doc comment at end of file is detached", "[docgen][scanner]") {
    Scanned s("void F();\n/// Nothing follows.\n");
    REQUIRE(s.Diags() == 1);
    REQUIRE(s.Diag(0) == "2: warning: detached doc comment");
}

TEST_CASE("Trailing ///< comments attach to the previous token", "[docgen][scanner]") {
    Scanned s("enum E {\n  A, ///< First.\n  B = 2 ///< Second.\n};\n");
    REQUIRE(s.Diags() == 0);
    REQUIRE(s.r.docs.Size() == 2);
    REQUIRE(s.r.docs[0].kind == DocKind::Trailing);
    REQUIRE(s.DocText(0) == "First.");
    REQUIRE(s.Text(static_cast<usize>(s.r.docs[0].token)) == ",");
    REQUIRE(s.DocText(1) == "Second.");
    REQUIRE(s.Text(static_cast<usize>(s.r.docs[1].token)) == "2");
}

TEST_CASE("Misplaced trailing and declaration comments warn", "[docgen][scanner]") {
    Scanned s("///< No declaration on this line.\nint a; /// should be ///<\nint b;\n");
    REQUIRE(s.Diags() == 2);
    REQUIRE(s.Diag(0) == "1: warning: trailing doc comment without a declaration on the same line");
    REQUIRE(s.Diag(1) == "2: warning: trailing doc comment must use ///<");
    REQUIRE(s.r.docs.Size() == 0);
}

TEST_CASE("//! file overviews are collected separately", "[docgen][scanner]") {
    Scanned s("#pragma once\n//! Overview one.\n//!\n//! Overview two.\n\nvoid F();\n");
    REQUIRE(s.Diags() == 0);
    REQUIRE(s.r.docs.Size() == 1);
    REQUIRE(s.r.docs[0].kind == DocKind::File);
    REQUIRE(s.DocText(0) == "Overview one.\n\nOverview two.");
    REQUIRE(s.r.docs[0].token == -1);
    REQUIRE(s.r.tokens[0].doc == -1);

    Scanned late("void F();\n//! Too late.\n");
    REQUIRE(late.Diags() == 1);
    REQUIRE(late.Diag(0) == "2: warning: misplaced file overview");
}

TEST_CASE("Lexical errors are reported and scanning resumes", "[docgen][scanner]") {
    SECTION("unterminated block comment") {
        Scanned s("a;\n/* never ends\nb;\n");
        REQUIRE(s.Diags() == 1);
        REQUIRE(s.Diag(0) == "2: error: unterminated comment");
        REQUIRE(s.Texts() == Strs{"a", ";"});
    }
    SECTION("unterminated string resumes on the next line") {
        Scanned s("const char* a = \"oops;\nint b;\n");
        REQUIRE(s.Diags() == 1);
        REQUIRE(s.Diag(0) == "1: error: unterminated string literal");
        auto t = s.Texts();
        REQUIRE(t.Back() == ";");
        REQUIRE(t[t.Size() - 2] == "b");
    }
    SECTION("unterminated char literal") {
        Scanned s("char c = 'x;\nint d;\n");
        REQUIRE(s.Diags() == 1);
        REQUIRE(s.Diag(0) == "1: error: unterminated character literal");
    }
    SECTION("unterminated raw string") {
        Scanned s("auto s = R\"x(never closed\nmore\n");
        REQUIRE(s.Diags() == 1);
        REQUIRE(s.Diag(0) == "1: error: unterminated raw string literal");
    }
}

TEST_CASE("CRLF line endings: positions right, no \\r in doc text", "[docgen][scanner]") {
    Scanned s("/// Doc A.\r\n/// More.\r\nvoid F();\r\n\r\nint x; ///< Trailing.\r\n");
    REQUIRE(s.Diags() == 0);
    REQUIRE(s.DocText(0) == "Doc A.\nMore.");
    REQUIRE(s.DocText(1) == "Trailing.");
    REQUIRE(s.r.tokens[0].line == 3);
    REQUIRE(s.r.tokens[0].doc == 0);
    REQUIRE(s.Text(s.r.tokens.Size() - 1) == ";");
    REQUIRE(s.r.tokens[s.r.tokens.Size() - 1].line == 5);
}

TEST_CASE("Empty and tiny inputs", "[docgen][scanner]") {
    Scanned a("");
    REQUIRE(a.r.tokens.Size() == 0);
    REQUIRE(a.Diags() == 0);
    Scanned b("/");
    REQUIRE(b.Texts() == Strs{"/"});
    Scanned c("//");
    REQUIRE(c.r.tokens.Size() == 0);
    Scanned d("#");
    REQUIRE(d.r.tokens.Size() == 0);
}

// ---- regressions from the Day 37 code review -----------------------------------------

TEST_CASE("A trailing ///< on a #define attaches to the macro, and comments do not leak into its text", "[docgen][scanner][review]") {
    Scanned s("#define A 1 ///< trailing doc\n#define B 2 // plain\n#define C 3 /* block */ ///< after block\nint x;\n");
    REQUIRE(s.Diags() == 0);
    REQUIRE(s.Text(0) == "A 1");
    REQUIRE(s.Text(1) == "B 2");
    REQUIRE(s.Text(2) == "C 3");
    REQUIRE(s.r.docs.Size() == 2);
    REQUIRE(s.r.docs[0].kind == DocKind::Trailing);
    REQUIRE(s.DocText(0) == "trailing doc");
    REQUIRE(s.r.docs[0].token == 0);
    REQUIRE(s.DocText(1) == "after block");
    REQUIRE(s.r.docs[1].token == 2);
}

TEST_CASE("#if 0 with a trailing comment still skips", "[docgen][scanner][review]") {
    Scanned s("#if 0 // disabled\nbad \" quote or { brace\n#endif /* done */\nok;\n");
    REQUIRE(s.Diags() == 0);
    REQUIRE(s.Texts() == Strs{"ok", ";"});
    Scanned t("#if 0 /* why */\nbad ' char\n#else\nlive;\n#endif\n");
    REQUIRE(t.Diags() == 0);
    REQUIRE(t.Texts() == Strs{"live", ";"});
}

TEST_CASE("A block comment spanning lines inside a directive is handled", "[docgen][scanner][review]") {
    Scanned s("#define FOO 1 /* multi\n   line comment with a quote ' */\nint x;\n");
    REQUIRE(s.Diags() == 0);
    REQUIRE(s.Text(0) == "FOO 1");
    REQUIRE(s.Text(1) == "int");
    REQUIRE(s.r.tokens[1].line == 3);
    Scanned t("#if 0\nx\n#endif /* a\n b ' */\ny;\n");
    REQUIRE(t.Diags() == 0);
    REQUIRE(t.Texts() == Strs{"y", ";"});
    Scanned u("#define BAD /* never closed\nint y;\n");
    REQUIRE(u.Diags() == 1);
    REQUIRE(u.Diag(0) == "1: error: unterminated comment");
}

TEST_CASE("A leading UTF-8 BOM is skipped", "[docgen][scanner][review]") {
    Scanned s("\xEF\xBB\xBF#pragma once\n//! Overview.\nvoid F();\n");
    REQUIRE(s.Diags() == 0);
    REQUIRE(s.Text(0) == "void");
    REQUIRE(s.r.docs.Size() == 1);
}

TEST_CASE("Char literals accept line continuations (LF and CRLF)", "[docgen][scanner][review]") {
    Scanned s("char a = '\\\n';\nchar b = '\\\r\n';\nint c;\n");
    REQUIRE(s.Diags() == 0);
    Scanned t("const char* s = \"abc\\\ndef\";\nint d;\n");
    REQUIRE(t.Diags() == 0);
    REQUIRE(t.r.tokens[t.r.tokens.Size() - 2].line == 3);
}

TEST_CASE("Hex literals do not treat e/E as an exponent", "[docgen][scanner][review]") {
    Scanned s("a = 0xE+1; b = 0x1E-1; c = 1e+5; d = 0x1.8p-3;");
    REQUIRE(s.Diags() == 0);
    auto t = s.Texts();
    REQUIRE(t == Strs{"a", "=", "0xE", "+", "1", ";", "b", "=", "0x1E", "-", "1", ";", "c", "=", "1e+5", ";", "d", "=", "0x1.8p-3", ";"});
}

TEST_CASE("Columns are right after line continuations", "[docgen][scanner][review]") {
    Scanned s("int a = \\\n  foo;\n");
    REQUIRE(s.r.tokens[3].line == 2);
    REQUIRE(s.r.tokens[3].column == 3);
    Scanned t("#define M(x) \\\n    x\nint after;\n");
    REQUIRE(t.r.tokens[1].line == 3);
    REQUIRE(t.r.tokens[1].column == 1);
}

TEST_CASE("Code before a multi-line block comment does not make a later /// look trailing", "[docgen][scanner][review]") {
    Scanned s("int x; /* a\n b */ /// Doc for y\nint y;\n");
    REQUIRE(s.Diags() == 0);
    REQUIRE(s.r.docs.Size() == 1);
    REQUIRE(s.DocText(0) == "Doc for y");
    REQUIRE(s.Text(static_cast<usize>(s.r.docs[0].token)) == "int");
}

TEST_CASE("Raw string delimiters are limited to 16 characters", "[docgen][scanner][review]") {
    Scanned ok("auto a = R\"0123456789abcdef(x)0123456789abcdef\";");
    REQUIRE(ok.Diags() == 0);
    Scanned bad("auto a = R\"0123456789abcdefg(x)0123456789abcdefg\";\nint b;\n");
    REQUIRE(bad.Diags() >= 1);
}

// ---- fixture corpus ----------------------------------------------------------------

TEST_CASE("Good fixtures scan with zero diagnostics", "[docgen][scanner][fixtures]") {
    StrList files;
    ListFiles(Str(DOCGEN_FIXTURE_DIR) + "/good", ".h", files);
    REQUIRE(files.Size() >= 13);
    for (auto& f : files) {
        INFO(f.CStr());
        Scanned s(ReadWhole(f));
        REQUIRE(s.r.tokens.Size() > 0);
        for (usize i = 0; i < s.Diags(); ++i) INFO("diagnostic: " << s.Diag(i).CStr());
        REQUIRE(s.Diags() == 0);
    }
}

namespace {
// Diagnostics the scanner itself is responsible for; the rest are checked
// on later days.
bool ScannerOwns(const Str& message) {
    static const char* prefixes[] = {"unterminated", "detached doc comment", "misplaced file overview",
                                     "trailing doc comment"};
    for (const char* p : prefixes)
        if (message.RFind(p, 0) == 0) return true;
    return false;
}
} // namespace

TEST_CASE("Bad fixtures: the scanner reports exactly the diagnostics it owns", "[docgen][scanner][fixtures]") {
    StrList files;
    ListFiles(Str(DOCGEN_FIXTURE_DIR) + "/bad", ".h", files);
    REQUIRE(files.Size() >= 10);
    for (auto& f : files) {
        INFO(f.CStr());
        // Expected, restricted to scanner-owned diagnostics.
        StrList expected;
        Str exp = ReadWhole(f + ".expect");
        REQUIRE_FALSE(exp.Empty());
        usize pos = 0;
        while (pos < exp.Size()) {
            usize nl = exp.Find('\n', pos);
            if (nl == Str::npos) nl = exp.Size();
            Str line = exp.Substr(pos, nl - pos);
            pos = nl + 1;
            if (line.Empty()) continue;
            // "<line>: <severity>: <message prefix>"
            usize c1 = line.Find(": ");
            usize c2 = line.Find(": ", c1 + 2);
            REQUIRE(c1 != Str::npos);
            REQUIRE(c2 != Str::npos);
            if (ScannerOwns(line.Substr(c2 + 2))) expected.Push(line);
        }
        Scanned s(ReadWhole(f));
        StrList actual;
        for (usize i = 0; i < s.Diags(); ++i) actual.Push(s.Diag(i));
        REQUIRE(actual.Size() == expected.Size());
        for (usize i = 0; i < actual.Size(); ++i) {
            // expected message is a prefix of the actual one
            REQUIRE(actual[i].Compare(0, expected[i].Size(), expected[i]) == 0);
        }
    }
}

// ---- robustness ------------------------------------------------------------------------

namespace {
Str Dump(const Scanned& s) {
    Str out;
    for (usize i = 0; i < s.r.tokens.Size(); ++i) {
        const Token& t = s.r.tokens[i];
        out += Num(t.line) + ":" + Num(t.column) + ":" + Num(static_cast<int>(t.kind)) + ":" +
               s.Text(i) + ":" + Num(t.doc) + "\n";
    }
    for (usize i = 0; i < s.r.docs.Size(); ++i) out += "doc:" + s.DocText(i) + "\n";
    for (usize i = 0; i < s.Diags(); ++i) out += s.Diag(i) + "\n";
    return out;
}
} // namespace

TEST_CASE("Every header under engine/ scans cleanly and deterministically", "[docgen][scanner][engine]") {
    StrList files;
    ListFiles(DOCGEN_ENGINE_DIR, ".h", files);
    REQUIRE(files.Size() > 40);
    for (auto& f : files) {
        INFO(f.CStr());
        Str text = ReadWhole(f);
        Scanned a(text), b(text);
        for (usize i = 0; i < a.Diags(); ++i) INFO("diagnostic: " << a.Diag(i).CStr());
        REQUIRE(a.Diags() == 0);
        REQUIRE(Dump(a) == Dump(b));
    }
}

TEST_CASE("Truncated and garbage inputs terminate without crashing", "[docgen][scanner][fuzz]") {
    StrList files;
    ListFiles(DOCGEN_ENGINE_DIR, ".h", files);
    ListFiles(DOCGEN_FIXTURE_DIR, ".h", files);
    unsigned long long rng = 0x2545F4914F6CDD1Dull;
    auto next = [&]() {
        rng ^= rng << 13;
        rng ^= rng >> 7;
        rng ^= rng << 17;
        return rng;
    };
    for (auto& f : files) {
        Str text = ReadWhole(f);
        if (text.Empty()) continue;
        for (int i = 0; i < 12; ++i) {
            Scanned s(text.Substr(0, next() % (text.Size() + 1)));
            (void)s;
        }
    }
    // Random byte soup biased toward the characters the scanner cares about.
    const char alphabet[] = "/*\"'\\\n\r #<>(){}[];,.:!R()abcxyz019_-+=?\t";
    for (int i = 0; i < 400; ++i) {
        Str junk;
        usize len = next() % 300;
        for (usize k = 0; k < len; ++k) junk.Push(alphabet[next() % (sizeof(alphabet) - 1)]);
        Scanned s(junk);
        (void)s;
    }
    for (int i = 0; i < 100; ++i) {
        Str junk;
        usize len = next() % 200;
        for (usize k = 0; k < len; ++k) junk.Push(static_cast<char>(next() & 0xFF));
        Scanned s(junk);
        (void)s;
    }
    SUCCEED();
}
