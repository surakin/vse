#include <cstdio>
#include <cstring>

#include "scanner.h"
#include "source_file.h"

using namespace docgen;

namespace {

int Usage() {
    std::fputs("usage: docgen tokens FILE.h     (scanner testing aid: dump tokens, docs, diagnostics)\n", stderr);
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

} // namespace

int main(int argc, char** argv) {
    if (argc == 3 && std::strcmp(argv[1], "tokens") == 0) return Tokens(argv[2]);
    return Usage();
}
