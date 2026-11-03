#include <cstdio>
#include <cstring>

#include "doc_parser.h"
#include "extractor.h"
#include "scanner.h"
#include "source_file.h"

using namespace docgen;

namespace {

int Usage() {
    std::fputs("usage: docgen tokens FILE.h                 (scanner testing aid: dump tokens, docs, diagnostics)\n"
               "       docgen dump [--all] [--keep-going] FILE.h  (declaration tree; --all includes hidden declarations)\n"
               "       docgen docs [--keep-going] FILE.h          (parsed doc comments, validation, undocumented symbols)\n",
               stderr);
    return 2;
}

const char* KindName(TokenKind k) {
    switch (k) {
    case TokenKind::Identifier: return "ident";
    case TokenKind::Number: return "number";
    case TokenKind::StringLiteral: return "string";
    case TokenKind::CharLiteral: return "char";
    case TokenKind::Punct: return "punct";
    case TokenKind::Define: return "define";
    }
    return "?";
}

int Tokens(const char* path) {
    ArenaAllocator arena(usize{256} << 20);
    StringView src;
    if (!LoadSourceFile(StringView(path, std::strlen(path)), arena, src)) {
        std::fprintf(stderr, "%s: error: cannot read file\n", path);
        return 2;
    }
    ScanResult r(arena);
    Scan(src, arena, r);
    for (usize i = 0; i < r.tokens.Size(); ++i) {
        const Token& t = r.tokens[i];
        std::printf("%u:%u %s '%.*s'", t.line, t.column, KindName(t.kind), static_cast<int>(t.text.Length()), t.text.Data());
        if (t.doc >= 0) std::printf(" doc=%d", t.doc);
        std::printf("\n");
    }
    for (usize i = 0; i < r.docs.Size(); ++i) {
        const DocComment& d = r.docs[i];
        std::printf("doc %zu kind=%d lines=%u-%u token=%d text='%.*s'\n", static_cast<size_t>(i), static_cast<int>(d.kind),
                    d.first_line, d.last_line, static_cast<int>(d.token), static_cast<int>(d.text.Length()), d.text.Data());
    }
    for (usize i = 0; i < r.diagnostics.Size(); ++i) {
        const Diagnostic& d = r.diagnostics[i];
        std::fprintf(stderr, "%s:%u: %s: %.*s\n", path, d.line, SeverityName(d.severity), static_cast<int>(d.message.Length()),
                     d.message.Data());
    }
    return r.HasErrors() ? 1 : 0;
}

// Prints diagnostics from both phases, sorted by line. Without keep_going
// only the first error is shown.
using DiagList = engine_placeholder::Array<Diagnostic, ArenaAllocator>;

bool PrintDiagnostics(const char* path, const DiagList* const* lists, usize list_count, bool keep_going, ArenaAllocator& arena) {
    // Each list is already nearly ordered by line, so a stable insertion sort is cheap.
    engine_placeholder::Array<const Diagnostic*, ArenaAllocator> all(arena);
    for (usize l = 0; l < list_count; ++l) {
        for (usize i = 0; i < lists[l]->Size(); ++i) all.Push(&(*lists[l])[i]);
    }
    for (usize i = 1; i < all.Size(); ++i) {
        const Diagnostic* d = all[i];
        usize j = i;
        while (j > 0 && all[j - 1]->line > d->line) {
            all[j] = all[j - 1];
            --j;
        }
        all[j] = d;
    }
    bool had_error = false;
    for (usize n = 0; n < all.Size(); ++n) {
        const Diagnostic& d = *all[n];
        if (d.severity == Severity::Error) {
            if (had_error && !keep_going) continue;
            had_error = true;
        }
        std::fprintf(stderr, "%s:%u: %s: %.*s\n", path, d.line, SeverityName(d.severity), static_cast<int>(d.message.Length()),
                     d.message.Data());
    }
    return had_error;
}

int Dump(int argc, char** argv) {
    bool all = false;
    bool keep_going = false;
    const char* path = nullptr;
    for (int i = 2; i < argc; ++i) {
        if (std::strcmp(argv[i], "--all") == 0) all = true;
        else if (std::strcmp(argv[i], "--keep-going") == 0) keep_going = true;
        else if (path == nullptr) path = argv[i];
        else return Usage();
    }
    if (path == nullptr) return Usage();
    ArenaAllocator arena(usize{256} << 20);
    StringView src;
    if (!LoadSourceFile(StringView(path, std::strlen(path)), arena, src)) {
        std::fprintf(stderr, "%s: error: cannot read file\n", path);
        return 2;
    }
    ScanResult scan(arena);
    Scan(src, arena, scan);
    ExtractResult ex(arena);
    Extract(scan, arena, ex);
    engine_placeholder::StringBuilder<ArenaAllocator> sb(arena);
    DumpTree(ex, scan, sb, all);
    StringView text = sb.View();
    std::fwrite(text.Data(), 1, text.Length(), stdout);
    const DiagList* lists[] = {&scan.diagnostics, &ex.diagnostics};
    return PrintDiagnostics(path, lists, 2, keep_going, arena) ? 1 : 0;
}

int Docs(int argc, char** argv) {
    bool keep_going = false;
    const char* path = nullptr;
    for (int i = 2; i < argc; ++i) {
        if (std::strcmp(argv[i], "--keep-going") == 0) keep_going = true;
        else if (path == nullptr) path = argv[i];
        else return Usage();
    }
    if (path == nullptr) return Usage();
    ArenaAllocator arena(usize{256} << 20);
    StringView src;
    if (!LoadSourceFile(StringView(path, std::strlen(path)), arena, src)) {
        std::fprintf(stderr, "%s: error: cannot read file\n", path);
        return 2;
    }
    ScanResult scan(arena);
    Scan(src, arena, scan);
    ExtractResult ex(arena);
    Extract(scan, arena, ex);
    DocResult docs(arena);
    ParseDocs(scan, ex, arena, docs);
    engine_placeholder::StringBuilder<ArenaAllocator> sb(arena);
    for (usize i = 0; i < scan.docs.Size(); ++i) {
        sb.Append(StringView("== doc "));
        sb.AppendUInt(i);
        sb.Append(StringView(" lines "));
        sb.AppendUInt(scan.docs[i].first_line);
        sb.Append('-');
        sb.AppendUInt(scan.docs[i].last_line);
        sb.Append('\n');
        EmitDoc(*docs.parsed[i], sb);
    }
    for (usize i = 0; i < docs.undocumented.Size(); ++i) {
        sb.Append(StringView("undocumented: "));
        sb.Append(docs.undocumented[i]->qualified);
        sb.Append('\n');
    }
    StringView text = sb.View();
    std::fwrite(text.Data(), 1, text.Length(), stdout);
    const DiagList* lists[] = {&scan.diagnostics, &ex.diagnostics, &docs.diagnostics};
    return PrintDiagnostics(path, lists, 3, keep_going, arena) ? 1 : 0;
}

} // namespace

int main(int argc, char** argv) {
    if (argc == 3 && std::strcmp(argv[1], "tokens") == 0) return Tokens(argv[2]);
    if (argc >= 3 && std::strcmp(argv[1], "dump") == 0) return Dump(argc, argv);
    if (argc >= 3 && std::strcmp(argv[1], "docs") == 0) return Docs(argc, argv);
    return Usage();
}
