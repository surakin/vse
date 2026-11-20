
#include "cstr/cstr.h"
#include "log/output.h"

#include "cstr/number.h"

#include "doc_parser.h"
#include "lint.h"
#include "links.h"
#include "samples.h"
#include "reference.h"
#include "extractor.h"
#include "scanner.h"
#include "source_file.h"
#include "os/command_line.h"
#include "debug/failure_hooks.h"
#include "os/console.h"

using namespace docgen;

namespace
{

int Usage()
{
    vse::out::PrintErrorText(
        "usage: docgen tokens FILE.h                 (scanner testing aid: dump tokens, docs, diagnostics)\n"
        "       docgen dump [--all] [--keep-going] FILE.h  (declaration tree; --all includes hidden declarations)\n"
        "       docgen docs [--keep-going] FILE.h          (parsed doc comments, validation, undocumented symbols)\n"
        "       docgen reference|check [--root DIR] [--modules FILE] [--out DIR] [--quiet]\n"
        "       docgen links [--root DIR] [--modules FILE] [--external] [--quiet]\n"
        "       docgen lint [--root DIR] [--modules FILE] [--baseline FILE [--update-baseline [--allow-growth]]] [--quiet]\n"
        "       docgen samples --compiler CXX [--root DIR] [--build-dir DIR] [--jobs N] [-I DIR]... [--flag F]... [--link ARG]...\n"
        "                      [--prelude FILE] [--timeout SEC] [--compile-timeout SEC] [--keep] [--quiet] [FILE.md...]\n");
    return 2;
}

const char* KindName(TokenKind k)
{
    switch (k)
    {
    case TokenKind::Identifier: return "ident";
    case TokenKind::Number: return "number";
    case TokenKind::StringLiteral: return "string";
    case TokenKind::CharLiteral: return "char";
    case TokenKind::Punct: return "punct";
    case TokenKind::Define: return "define";
    }
    return "?";
}

int Tokens(const char* path)
{
    ArenaAllocator arena(usize{256} << 20);
    StringView src;
    if (!LoadSourceFile(StringView(path, vse::CStrLength(path)), arena, src))
    {
        vse::out::PrintError("%s: error: cannot read file\n", path);
        return 2;
    }
    ScanResult r(arena);
    Scan(src, arena, r);
    for (usize i = 0; i < r.tokens.Size(); ++i)
    {
        const Token& t = r.tokens[i];
        vse::out::Print("%u:%u %s '%.*s'", t.line, t.column, KindName(t.kind), static_cast<int>(t.text.Length()),
                        t.text.Data());
        if (t.doc >= 0)
        {
            vse::out::Print(" doc=%d", t.doc);
        }
        vse::out::Print("\n");
    }
    for (usize i = 0; i < r.docs.Size(); ++i)
    {
        const DocComment& d = r.docs[i];
        vse::out::Print("doc %zu kind=%d lines=%u-%u token=%d text='%.*s'\n", static_cast<size_t>(i),
                        static_cast<int>(d.kind), d.first_line, d.last_line, static_cast<int>(d.token),
                        static_cast<int>(d.text.Length()), d.text.Data());
    }
    for (usize i = 0; i < r.diagnostics.Size(); ++i)
    {
        const Diagnostic& d = r.diagnostics[i];
        vse::out::PrintError("%s:%u: %s: %.*s\n", path, d.line, SeverityName(d.severity),
                             static_cast<int>(d.message.Length()), d.message.Data());
    }
    return r.HasErrors() ? 1 : 0;
}

// Prints diagnostics from both phases, sorted by line. Without keep_going
// only the first error is shown.
using DiagList = vse::Vector<Diagnostic, ArenaAllocator>;

bool PrintDiagnostics(const char* path, const DiagList* const* lists, usize list_count, bool keep_going,
                      ArenaAllocator& arena)
{
    // Each list is already nearly ordered by line, so a stable insertion sort is cheap.
    vse::Vector<const Diagnostic*, ArenaAllocator> all(arena);
    for (usize l = 0; l < list_count; ++l)
    {
        for (usize i = 0; i < lists[l]->Size(); ++i)
        {
            all.Push(&(*lists[l])[i]);
        }
    }
    StableSort(all, [](const Diagnostic* x, const Diagnostic* y) { return x->line < y->line; });
    bool had_error = false;
    for (usize n = 0; n < all.Size(); ++n)
    {
        const Diagnostic& d = *all[n];
        if (d.severity == Severity::Error)
        {
            if (had_error && !keep_going)
            {
                continue;
            }
            had_error = true;
        }
        vse::out::PrintError("%s:%u: %s: %.*s\n", path, d.line, SeverityName(d.severity),
                             static_cast<int>(d.message.Length()), d.message.Data());
    }
    return had_error;
}

using Line = vse::os::CommandLine<vse::HeapAllocator>;
using vse::Span;
using vse::os::CommandLineOption;

int Dump(int argc, char** argv)
{
    bool all = false;
    bool keep_going = false;
    const char* path = nullptr;
    static const CommandLineOption options[] = {{"all", false}, {"keep-going", false}};
    const Line line(vse::HeapAllocator::Default(), argc, argv, Span<const CommandLineOption>(options, 2));
    if (!line.Ok())
    {
        vse::out::PrintError("docgen: %s\n", line.Error().Data());
        return Usage();
    }
    all = line.HasFlag("all");
    keep_going = line.HasFlag("keep-going");
    path = line.ArgCount() == 2 ? line.Arg(1).Data() : nullptr;
    if (path == nullptr)
    {
        return Usage();
    }
    ArenaAllocator arena(usize{256} << 20);
    StringView src;
    if (!LoadSourceFile(StringView(path, vse::CStrLength(path)), arena, src))
    {
        vse::out::PrintError("%s: error: cannot read file\n", path);
        return 2;
    }
    ScanResult scan(arena);
    Scan(src, arena, scan);
    ExtractResult ex(arena);
    Extract(scan, arena, ex);
    vse::StringBuilder<ArenaAllocator> sb(arena);
    DumpTree(ex, scan, sb, all);
    StringView text = sb.View();
    vse::out::PrintBytes(text.Data(), text.Length());
    const DiagList* lists[] = {&scan.diagnostics, &ex.diagnostics};
    return PrintDiagnostics(path, lists, 2, keep_going, arena) ? 1 : 0;
}

int Docs(int argc, char** argv)
{
    bool keep_going = false;
    const char* path = nullptr;
    static const CommandLineOption options[] = {{"keep-going", false}};
    const Line line(vse::HeapAllocator::Default(), argc, argv, Span<const CommandLineOption>(options, 1));
    if (!line.Ok())
    {
        vse::out::PrintError("docgen: %s\n", line.Error().Data());
        return Usage();
    }
    keep_going = line.HasFlag("keep-going");
    path = line.ArgCount() == 2 ? line.Arg(1).Data() : nullptr;
    if (path == nullptr)
    {
        return Usage();
    }
    ArenaAllocator arena(usize{256} << 20);
    StringView src;
    if (!LoadSourceFile(StringView(path, vse::CStrLength(path)), arena, src))
    {
        vse::out::PrintError("%s: error: cannot read file\n", path);
        return 2;
    }
    ScanResult scan(arena);
    Scan(src, arena, scan);
    ExtractResult ex(arena);
    Extract(scan, arena, ex);
    DocResult docs(arena);
    ParseDocs(scan, ex, arena, docs);
    vse::StringBuilder<ArenaAllocator> sb(arena);
    for (usize i = 0; i < scan.docs.Size(); ++i)
    {
        sb.Append(StringView("== doc "));
        sb.AppendUInt(i);
        sb.Append(StringView(" lines "));
        sb.AppendUInt(scan.docs[i].first_line);
        sb.Append('-');
        sb.AppendUInt(scan.docs[i].last_line);
        sb.Append('\n');
        EmitDoc(*docs.parsed[i], sb);
    }
    for (usize i = 0; i < docs.undocumented.Size(); ++i)
    {
        sb.Append(StringView("undocumented: "));
        sb.Append(docs.undocumented[i]->qualified);
        sb.Append('\n');
    }
    StringView text = sb.View();
    vse::out::PrintBytes(text.Data(), text.Length());
    const DiagList* lists[] = {&scan.diagnostics, &ex.diagnostics, &docs.diagnostics};
    return PrintDiagnostics(path, lists, 3, keep_going, arena) ? 1 : 0;
}

void PrintFileDiags(const vse::Vector<FileDiag, ArenaAllocator>& ds, bool quiet)
{
    for (usize i = 0; i < ds.Size(); ++i)
    {
        const FileDiag& f = ds[i];
        if (quiet && f.d.severity != Severity::Error)
        {
            continue;
        }
        vse::out::PrintError("%.*s:%u: %s: %.*s\n", static_cast<int>(f.path.Length()), f.path.Data(), f.d.line,
                             SeverityName(f.d.severity), static_cast<int>(f.d.message.Length()), f.d.message.Data());
    }
}

// docgen reference | check
int Reference(int argc, char** argv, bool check)
{
    const char* root = ".";
    const char* modules = nullptr;
    const char* out = nullptr;
    bool quiet = false;
    static const CommandLineOption options[] = {
        {"root", true}, {"modules", true}, {"out", true}, {"quiet", false}, {"keep-going", false}};
    const Line line(vse::HeapAllocator::Default(), argc, argv, Span<const CommandLineOption>(options, 5));
    if (!line.Ok())
    {
        vse::out::PrintError("docgen: %s\n", line.Error().Data());
        return Usage();
    }
    if (line.ArgCount() != 1)
    {
        return Usage();
    }
    root = line.OptionOr("root", root);
    modules = line.OptionOr("modules", nullptr);
    out = line.OptionOr("out", nullptr);
    quiet = line.HasFlag("quiet");
    ArenaAllocator arena(usize{1} << 30);
    auto join = [&](const char* a, const char* b)
    {
        usize la = vse::CStrLength(a), lb = vse::CStrLength(b);
        char* buf = static_cast<char*>(arena.Alloc(la + lb + 2, 1));
        CopyBytes(buf, a, la);
        buf[la] = '/';
        CopyBytes(buf + la + 1, b, lb + 1);
        return StringView(buf, la + 1 + lb);
    };
    StringView root_v(root, vse::CStrLength(root));
    StringView map_path = modules != nullptr ? StringView(modules, vse::CStrLength(modules))
                                             : join(root, "docs/reference/modules.txt");
    StringView out_dir = out != nullptr ? StringView(out, vse::CStrLength(out)) : join(root, "docs/reference");

    ReferenceBuild build(arena);
    const bool built = BuildReference(root_v, map_path, arena, build);
    PrintFileDiags(build.index.diagnostics, quiet);
    if (!built)
    {
        return build.io_failure ? 2 : 1;
    }

    vse::Vector<FileDiag, ArenaAllocator> problems(arena);
    bool ok = check ? CheckReference(build, out_dir, arena, problems) : WriteReference(build, out_dir, arena, problems);
    PrintFileDiags(problems, false);
    if (!ok)
    {
        return check ? 1 : 2;
    }
    if (!quiet && !check)
    {
        vse::out::PrintError("docgen: wrote %zu files to %.*s\n", static_cast<size_t>(build.files.Size()),
                             static_cast<int>(out_dir.Length()), out_dir.Data());
    }
    return 0;
}

// docgen links
int Links(int argc, char** argv)
{
    const char* root = ".";
    const char* modules = nullptr;
    bool quiet = false;
    LinkOptions options;
    static const CommandLineOption cli_options[] = {
        {"root", true}, {"modules", true}, {"external", false}, {"quiet", false}};
    const Line line(vse::HeapAllocator::Default(), argc, argv, Span<const CommandLineOption>(cli_options, 4));
    if (!line.Ok())
    {
        vse::out::PrintError("docgen: %s\n", line.Error().Data());
        return Usage();
    }
    if (line.ArgCount() != 1)
    {
        return Usage();
    }
    root = line.OptionOr("root", root);
    modules = line.OptionOr("modules", nullptr);
    options.list_external = line.HasFlag("external");
    quiet = line.HasFlag("quiet");
    ArenaAllocator arena(usize{1} << 30);
    StringView root_v(root, vse::CStrLength(root));
    StringView map_path = modules != nullptr ? StringView(modules, vse::CStrLength(modules))
                                             : ArenaCat(arena, root_v, StringView("/docs/reference/modules.txt"));
    // `[[Symbol]]` references are checked against the index of the module map; a missing default map just skips them.
    const bool have_map = modules != nullptr || KindOf(map_path) != EntryKind::Missing;
    ReferenceBuild build(arena);
    const SymbolIndex* index = nullptr;
    bool header_errors = false;
    if (have_map)
    {
        const bool built = BuildReference(root_v, map_path, arena, build);
        if (build.io_failure)
        {
            PrintFileDiags(build.index.diagnostics, false);
            return 2;
        }
        // Header errors leave the index usable but possibly incomplete; show them (they fail this run too)
        // so a spurious "unresolved reference" can be traced to its cause.
        if (!built)
        {
            PrintFileDiags(build.index.diagnostics, false);
            header_errors = true;
        }
        index = &build.index;
    }
    vse::Vector<FileDiag, ArenaAllocator> problems(arena);
    LinkStats stats;
    if (!CheckLinks(root_v, index, arena, options, problems, stats))
    {
        vse::out::PrintError("docgen: cannot read the documentation tree under %s\n", root);
        return 2;
    }
    bool errors = false;
    for (usize i = 0; i < problems.Size(); ++i)
    {
        errors = errors || problems[i].d.severity == Severity::Error;
    }
    PrintFileDiags(problems, quiet);
    if (!quiet)
    {
        vse::out::PrintError("docgen: %zu files, %zu links (%zu external, not fetched), %zu references%s\n",
                             static_cast<size_t>(stats.files), static_cast<size_t>(stats.links),
                             static_cast<size_t>(stats.external), static_cast<size_t>(stats.refs),
                             index == nullptr ? " (not checked: no module map)" : "");
    }
    return errors || header_errors ? 1 : 0;
}

// docgen lint
int Lint(int argc, char** argv)
{
    const char* root = ".";
    const char* modules = nullptr;
    const char* baseline_path = nullptr;
    bool update = false;
    bool allow_growth = false;
    bool quiet = false;
    static const CommandLineOption options[] = {{"root", true},          {"modules", true},          {"baseline", true},
                                                {"allow-growth", false}, {"update-baseline", false}, {"quiet", false}};
    const Line line(vse::HeapAllocator::Default(), argc, argv, Span<const CommandLineOption>(options, 6));
    if (!line.Ok())
    {
        vse::out::PrintError("docgen: %s\n", line.Error().Data());
        return Usage();
    }
    if (line.ArgCount() != 1)
    {
        return Usage();
    }
    root = line.OptionOr("root", root);
    modules = line.OptionOr("modules", nullptr);
    baseline_path = line.OptionOr("baseline", nullptr);
    allow_growth = line.HasFlag("allow-growth");
    update = line.HasFlag("update-baseline");
    quiet = line.HasFlag("quiet");
    if (update && baseline_path == nullptr)
    {
        return Usage();
    }
    ArenaAllocator arena(usize{1} << 30);
    StringView root_v(root, vse::CStrLength(root));
    StringView map_path = modules != nullptr ? StringView(modules, vse::CStrLength(modules))
                                             : ArenaCat(arena, root_v, StringView("/docs/reference/modules.txt"));
    ReferenceBuild build(arena);
    BuildReference(root_v, map_path, arena, build);
    PrintFileDiags(build.index.diagnostics, quiet);
    if (build.io_failure)
    {
        return 2;
    }
    if (build.index.HasErrors())
    {
        return 1;  // fix the documentation errors first: coverage of a broken index is meaningless
    }

    LintResult result(arena);
    RunLint(build.index, arena, result);
    vse::StringBuilder<ArenaAllocator> table(arena);
    RenderCoverage(result, table);
    if (!quiet)
    {
        vse::out::PrintBytes(table.View().Data(), table.View().Length());
    }

    Baseline baseline(arena);
    Baseline* use = nullptr;
    const bool first_baseline = update && baseline_path != nullptr &&
                                KindOf(StringView(baseline_path, vse::CStrLength(baseline_path))) ==
                                    EntryKind::Missing;  // nothing to compare with yet
    if (baseline_path != nullptr && !first_baseline)
    {
        StringView text;
        if (!LoadSourceFile(StringView(baseline_path, vse::CStrLength(baseline_path)), arena, text))
        {
            vse::out::PrintError("%s: error: cannot read the baseline\n", baseline_path);
            return 2;
        }
        StringView error;
        if (!ParseBaseline(text, arena, baseline, error))
        {
            vse::out::PrintError("%s:%.*s\n", baseline_path, static_cast<int>(error.Length()), error.Data());
            return 2;
        }
        use = &baseline;
    }
    if (update)
    {
        // The baseline may only shrink: an update that would record a new finding or a lower ratio needs
        // --allow-growth.
        if (use != nullptr && !allow_growth)
        {
            vse::Vector<FileDiag, ArenaAllocator> growth(arena);
            CompareToBaseline(result, use, arena, growth);
            bool grew = false;
            for (usize i = 0; i < growth.Size(); ++i)
            {
                const StringView m = growth[i].d.message;
                if (growth[i].d.severity == Severity::Error && !m.Contains(StringView("no longer applies")) &&
                    !m.Contains(StringView("no baseline entry")) && !m.Contains(StringView("no longer exists")))
                {
                    grew = true;
                    PrintFileDiags(growth, true);
                    break;
                }
            }
            if (grew)
            {
                vse::out::PrintErrorText(
                    "docgen: refusing to update the baseline: it would record new findings or lower coverage (pass --allow-growth to override)\n");
                return 1;
            }
        }
        vse::StringBuilder<ArenaAllocator> text(arena);
        RenderBaseline(result, text);
        // atomic: a failed write cannot leave a truncated baseline behind
        if (!WriteTextAtomic(StringView(baseline_path, vse::CStrLength(baseline_path)), text.View()))
        {
            vse::out::PrintError("%s: error: cannot write the baseline\n", baseline_path);
            return 2;
        }
        if (!quiet)
        {
            vse::out::PrintError("docgen: baseline written to %s (%zu known findings)\n", baseline_path,
                                 static_cast<size_t>(result.findings.Size()));
        }
        return 0;
    }
    vse::Vector<FileDiag, ArenaAllocator> problems(arena);
    CompareToBaseline(result, use, arena, problems);
    bool errors = false;
    for (usize i = 0; i < problems.Size(); ++i)
    {
        errors = errors || problems[i].d.severity == Severity::Error;
    }
    PrintFileDiags(problems, quiet);
    return errors ? 1 : 0;
}

// An absolute path for `p` if it exists (the compiler runs in a scratch directory), otherwise `p` unchanged.
StringView Absolute(const char* p, ArenaAllocator& arena)
{
    return CanonicalOrSame(StringView(p, vse::CStrLength(p)), arena);
}

// docgen samples
int Samples(int argc, char** argv)
{
    ArenaAllocator arena(usize{1} << 30);
    SampleOptions opt(arena);
    const char* root = ".";
    bool quiet = false;
    vse::Vector<StringView, ArenaAllocator> only(arena);
    static const CommandLineOption options[] = {{"root", true},     {"compiler", true},   {"build-dir", true},
                                                {"jobs", true},     {"timeout", true},    {"compile-timeout", true},
                                                {"-I", true, true}, {"flag", true, true}, {"link", true, true},
                                                {"prelude", true},  {"keep", false},      {"quiet", false}};
    const Line line(vse::HeapAllocator::Default(), argc, argv, Span<const CommandLineOption>(options, 12));
    if (!line.Ok())
    {
        vse::out::PrintError("docgen: %s\n", line.Error().Data());
        return Usage();
    }
    root = line.OptionOr("root", root);
    const char* compiler = line.OptionOr("compiler", nullptr);
    if (compiler != nullptr)
    {
        opt.compiler = vse::CStrFindChar(compiler, '/') != nullptr
                           ? AbsoluteKeepingLinks(StringView(compiler, vse::CStrLength(compiler)), arena)
                           : ArenaCat(arena, StringView(compiler, vse::CStrLength(compiler)), StringView());
    }
    if (const char* build_dir = line.OptionOr("build-dir", nullptr))
    {
        opt.scratch_base = Absolute(build_dir, arena);
    }
    if (const char* jobs = line.OptionOr("jobs", nullptr))
    {
        opt.jobs = static_cast<u32>(vse::CStrToULong(jobs, nullptr, 10));
    }
    if (const char* timeout = line.OptionOr("timeout", nullptr))
    {
        opt.run_timeout_s = static_cast<u32>(vse::CStrToULong(timeout, nullptr, 10));
    }
    if (const char* timeout = line.OptionOr("compile-timeout", nullptr))
    {
        opt.compile_timeout_s = static_cast<u32>(vse::CStrToULong(timeout, nullptr, 10));
    }
    for (usize k = 0; k < line.OptionCount("-I"); ++k)
    {
        opt.includes.Push(Absolute(line.OptionAt("-I", k).Data(), arena));
    }
    for (usize k = 0; k < line.OptionCount("flag"); ++k)
    {
        opt.flags.Push(ArenaCat(arena, line.OptionAt("flag", k), StringView()));
    }
    for (usize k = 0; k < line.OptionCount("link"); ++k)
    {
        opt.links.Push(Absolute(line.OptionAt("link", k).Data(), arena));
    }
    if (const char* prelude = line.OptionOr("prelude", nullptr))
    {
        opt.prelude = Absolute(prelude, arena);
    }
    opt.keep_scratch = line.HasFlag("keep");
    quiet = line.HasFlag("quiet");
    for (usize k = 1; k < line.ArgCount(); ++k)
    {
        only.Push(ArenaCat(arena, line.Arg(k), StringView()));
    }
    if (opt.compiler.Empty())
    {
        return Usage();
    }
    if (opt.jobs == 0)
    {
        opt.jobs = 1;
    }
    StringView root_v(root, vse::CStrLength(root));
    opt.root = root_v;

    vse::Vector<StringView, ArenaAllocator> files(arena);
    if (only.Size() > 0)
    {
        for (usize i = 0; i < only.Size(); ++i)
        {
            files.Push(only[i]);
        }
    }
    else if (!CollectMarkdownFiles(root_v, arena, files))
    {
        vse::out::PrintError("docgen: cannot read the documentation tree under %s\n", root);
        return 2;
    }
    vse::Vector<FileDiag, ArenaAllocator> problems(arena);
    vse::Vector<Sample, ArenaAllocator> samples(arena);
    for (usize i = 0; i < files.Size(); ++i)
    {
        StringView text;
        if (!LoadSourceFile(root_v.Empty() ? files[i] : ArenaCat(arena, root_v, StringView("/"), files[i]), arena,
                            text))
        {
            problems.Push(FileDiag{files[i], Diagnostic{Severity::Error, 1, StringView("cannot read file")}});
            continue;
        }
        MdDoc doc(arena);
        ScanMarkdown(text, arena, doc);
        for (usize d = 0; d < doc.diagnostics.Size(); ++d)
        {
            problems.Push(FileDiag{files[i], doc.diagnostics[d]});
        }
        ExtractSamples(files[i], text, doc, arena, samples, problems);
    }
    // run from the repo root so a sample's #line path matches what the user sees
    SampleStats stats;
    if (!RunSamples(opt, samples, arena, problems, stats))
    {
        PrintFileDiags(problems, quiet);
        vse::out::PrintError("docgen: cannot set up the sample build (scratch directory or compiler)\n");
        return 2;
    }
    bool errors = false;
    for (usize i = 0; i < problems.Size(); ++i)
    {
        errors = errors || problems[i].d.severity == Severity::Error;
    }
    PrintFileDiags(problems, quiet);
    if (!quiet)
    {
        vse::out::PrintError("docgen: %zu samples, %zu compiled, %zu run, %zu failed\n",
                             static_cast<size_t>(stats.found), static_cast<size_t>(stats.compiled),
                             static_cast<size_t>(stats.ran), static_cast<size_t>(stats.failed));
    }
    return errors ? 1 : 0;
}

}  // namespace

int main(int argc, char** argv)
{
    vse::debug::InstallDiagnosticsHooks();  // a failed assert prints its stack trace
    // The first word is the command; each command declares its own options when it parses them.
    const vse::os::CommandLine<vse::HeapAllocator> words(vse::HeapAllocator::Default(), argc, argv);
    if (words.ArgCount() < 1)
    {
        return Usage();
    }
    const StringView command = words.Arg(0);
    if (command == StringView("tokens"))
    {
        return words.ArgCount() == 2 && words.FlagCount() == 0 ? Tokens(words.Arg(1).Data()) : Usage();
    }
    if (command == StringView("dump") && words.ArgCount() >= 2)
    {
        return Dump(argc, argv);
    }
    if (command == StringView("docs") && words.ArgCount() >= 2)
    {
        return Docs(argc, argv);
    }
    if (command == StringView("reference"))
    {
        return Reference(argc, argv, false);
    }
    if (command == StringView("check"))
    {
        return Reference(argc, argv, true);
    }
    if (command == StringView("links"))
    {
        return Links(argc, argv);
    }
    if (command == StringView("lint"))
    {
        return Lint(argc, argv);
    }
    if (command == StringView("samples"))
    {
        return Samples(argc, argv);
    }
    return Usage();
}
