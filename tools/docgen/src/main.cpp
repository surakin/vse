#include <cstdio>
#include <cstring>

#include "extractor.h"
#include "scanner.h"
#include "source_file.h"

using namespace docgen;

namespace {

int Usage() {
    std::fputs("usage: docgen tokens FILE.h                 (scanner testing aid: dump tokens, docs, diagnostics)\n"
               "       docgen dump [--all] [--keep-going] FILE.h  (declaration tree; --all includes hidden declarations)\n",
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
bool PrintDiagnostics(const char* path, const ScanResult& scan, const ExtractResult& ex, bool keep_going, ArenaAllocator& arena) {
    // Both lists are already nearly ordered by line, so a stable insertion sort is cheap.
    engine_placeholder::Array<const Diagnostic*, ArenaAllocator> all(arena);
    for (usize i = 0; i < scan.diagnostics.Size(); ++i) all.Push(&scan.diagnostics[i]);
    for (usize i = 0; i < ex.diagnostics.Size(); ++i) all.Push(&ex.diagnostics[i]);
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
    return PrintDiagnostics(path, scan, ex, keep_going, arena) ? 1 : 0;
}

} // namespace

int main(int argc, char** argv) {
    if (argc == 3 && std::strcmp(argv[1], "tokens") == 0) return Tokens(argv[2]);
    if (argc >= 3 && std::strcmp(argv[1], "dump") == 0) return Dump(argc, argv);
    return Usage();
}
