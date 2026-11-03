#include <cstdio>
#include <cstring>

#include <sys/stat.h>

#include "doc_parser.h"
#include "links.h"
#include "reference.h"
#include "extractor.h"
#include "scanner.h"
#include "source_file.h"

using namespace docgen;

namespace {

int Usage() {
    std::fputs("usage: docgen tokens FILE.h                 (scanner testing aid: dump tokens, docs, diagnostics)\n"
               "       docgen dump [--all] [--keep-going] FILE.h  (declaration tree; --all includes hidden declarations)\n"
               "       docgen docs [--keep-going] FILE.h          (parsed doc comments, validation, undocumented symbols)\n"
               "       docgen reference|check [--root DIR] [--modules FILE] [--out DIR] [--quiet]\n"
               "       docgen links [--root DIR] [--modules FILE] [--external] [--quiet]\n",
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
    StableSort(all, [](const Diagnostic* x, const Diagnostic* y) { return x->line < y->line; });
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

void PrintFileDiags(const engine_placeholder::Array<FileDiag, ArenaAllocator>& ds, bool quiet) {
    for (usize i = 0; i < ds.Size(); ++i) {
        const FileDiag& f = ds[i];
        if (quiet && f.d.severity != Severity::Error) continue;
        std::fprintf(stderr, "%.*s:%u: %s: %.*s\n", static_cast<int>(f.path.Length()), f.path.Data(), f.d.line, SeverityName(f.d.severity),
                     static_cast<int>(f.d.message.Length()), f.d.message.Data());
    }
}

// docgen reference | check
int Reference(int argc, char** argv, bool check) {
    const char* root = ".";
    const char* modules = nullptr;
    const char* out = nullptr;
    bool quiet = false;
    for (int i = 2; i < argc; ++i) {
        if (std::strcmp(argv[i], "--root") == 0 && i + 1 < argc) root = argv[++i];
        else if (std::strcmp(argv[i], "--modules") == 0 && i + 1 < argc) modules = argv[++i];
        else if (std::strcmp(argv[i], "--out") == 0 && i + 1 < argc) out = argv[++i];
        else if (std::strcmp(argv[i], "--quiet") == 0) quiet = true;
        else if (std::strcmp(argv[i], "--keep-going") == 0) {}
        else return Usage();
    }
    ArenaAllocator arena(usize{1} << 30);
    auto join = [&](const char* a, const char* b) {
        usize la = std::strlen(a), lb = std::strlen(b);
        char* buf = static_cast<char*>(arena.Alloc(la + lb + 2, 1));
        CopyBytes(buf, a, la);
        buf[la] = '/';
        CopyBytes(buf + la + 1, b, lb + 1);
        return StringView(buf, la + 1 + lb);
    };
    StringView root_v(root, std::strlen(root));
    StringView map_path = modules != nullptr ? StringView(modules, std::strlen(modules)) : join(root, "docs/reference/modules.txt");
    StringView out_dir = out != nullptr ? StringView(out, std::strlen(out)) : join(root, "docs/reference");

    ReferenceBuild build(arena);
    const bool built = BuildReference(root_v, map_path, arena, build);
    PrintFileDiags(build.index.diagnostics, quiet);
    if (!built) return build.io_failure ? 2 : 1;

    engine_placeholder::Array<FileDiag, ArenaAllocator> problems(arena);
    bool ok = check ? CheckReference(build, out_dir, arena, problems) : WriteReference(build, out_dir, arena, problems);
    PrintFileDiags(problems, false);
    if (!ok) return check ? 1 : 2;
    if (!quiet && !check) {
        std::fprintf(stderr, "docgen: wrote %zu files to %.*s\n", static_cast<size_t>(build.files.Size()), static_cast<int>(out_dir.Length()),
                     out_dir.Data());
    }
    return 0;
}

// docgen links
int Links(int argc, char** argv) {
    const char* root = ".";
    const char* modules = nullptr;
    bool quiet = false;
    LinkOptions options;
    for (int i = 2; i < argc; ++i) {
        if (std::strcmp(argv[i], "--root") == 0 && i + 1 < argc) root = argv[++i];
        else if (std::strcmp(argv[i], "--modules") == 0 && i + 1 < argc) modules = argv[++i];
        else if (std::strcmp(argv[i], "--external") == 0) options.list_external = true;
        else if (std::strcmp(argv[i], "--quiet") == 0) quiet = true;
        else return Usage();
    }
    ArenaAllocator arena(usize{1} << 30);
    StringView root_v(root, std::strlen(root));
    StringView map_path = modules != nullptr ? StringView(modules, std::strlen(modules)) : ArenaCat(arena, root_v, StringView("/docs/reference/modules.txt"));
    // `[[Symbol]]` references are checked against the index of the module map; a missing default map just skips them.
    struct stat st;
    const bool have_map = modules != nullptr || stat(map_path.Data(), &st) == 0;
    ReferenceBuild build(arena);
    const SymbolIndex* index = nullptr;
    bool header_errors = false;
    if (have_map) {
        const bool built = BuildReference(root_v, map_path, arena, build);
        if (build.io_failure) {
            PrintFileDiags(build.index.diagnostics, false);
            return 2;
        }
        // Header errors leave the index usable but possibly incomplete; show them (they fail this run too)
        // so a spurious "unresolved reference" can be traced to its cause.
        if (!built) {
            PrintFileDiags(build.index.diagnostics, false);
            header_errors = true;
        }
        index = &build.index;
    }
    engine_placeholder::Array<FileDiag, ArenaAllocator> problems(arena);
    LinkStats stats;
    if (!CheckLinks(root_v, index, arena, options, problems, stats)) {
        std::fprintf(stderr, "docgen: cannot read the documentation tree under %s\n", root);
        return 2;
    }
    bool errors = false;
    for (usize i = 0; i < problems.Size(); ++i) errors = errors || problems[i].d.severity == Severity::Error;
    PrintFileDiags(problems, quiet);
    if (!quiet) {
        std::fprintf(stderr, "docgen: %zu files, %zu links (%zu external, not fetched), %zu references%s\n", static_cast<size_t>(stats.files),
                     static_cast<size_t>(stats.links), static_cast<size_t>(stats.external), static_cast<size_t>(stats.refs),
                     index == nullptr ? " (not checked: no module map)" : "");
    }
    return errors || header_errors ? 1 : 0;
}

} // namespace

int main(int argc, char** argv) {
    if (argc == 3 && std::strcmp(argv[1], "tokens") == 0) return Tokens(argv[2]);
    if (argc >= 3 && std::strcmp(argv[1], "dump") == 0) return Dump(argc, argv);
    if (argc >= 3 && std::strcmp(argv[1], "docs") == 0) return Docs(argc, argv);
    if (argc >= 2 && std::strcmp(argv[1], "reference") == 0) return Reference(argc, argv, false);
    if (argc >= 2 && std::strcmp(argv[1], "check") == 0) return Reference(argc, argv, true);
    if (argc >= 2 && std::strcmp(argv[1], "links") == 0) return Links(argc, argv);
    return Usage();
}
