#include "samples.h"

#include "os/signal.h"
#include "cstr/format.h"
#include "cstr/cstr.h"

#include "child_process.h"
#include "io/file.h"
#include "io/path.h"
#include "os/os.h"
#include "source_file.h"
#include "build_defs.h"

namespace docgen
{

namespace
{

bool IsWordChar(char c)
{
    return c != ' ' && c != '\t';
}

// Splits an info string into words.
void Words(StringView info, Vector<StringView, ArenaAllocator>& out)
{
    usize i = 0;
    while (i < info.Length())
    {
        while (i < info.Length() && !IsWordChar(info[i]))
        {
            ++i;
        }
        usize e = i;
        while (e < info.Length() && IsWordChar(info[e]))
        {
            ++e;
        }
        if (e > i)
        {
            out.Push(StringView(info.Data() + i, e - i));
        }
        i = e;
    }
}

// Byte offsets of the start of every line of `text` (line n starts at starts[n-1]).
void LineStarts(StringView text, Vector<usize, ArenaAllocator>& starts)
{
    starts.Push(0);
    for (usize i = 0; i < text.Length(); ++i)
    {
        if (text[i] == '\n')
        {
            starts.Push(i + 1);
        }
    }
}

// Are all lines strictly between `a` and `b` (1-based) blank?
bool BlankBetween(StringView text, const Vector<usize, ArenaAllocator>& starts, u32 a, u32 b)
{
    for (u32 line = a + 1; line < b && line <= starts.Size(); ++line)
    {
        for (usize i = starts[line - 1]; i < text.Length() && text[i] != '\n'; ++i)
        {
            if (text[i] != ' ' && text[i] != '\t' && text[i] != '\r')
            {
                return false;
            }
        }
    }
    return true;
}

double Now()
{
    return static_cast<double>(vse::os::MonotonicTimeNanos()) / 1e9;
}

StringView Num(ArenaAllocator& arena, u32 n)
{
    vse::StringBuilder<ArenaAllocator> sb(arena);
    sb.AppendUInt(n);
    return sb.View();
}

#if ENGINE_PLATFORM_WINDOWS
StringView Hex(ArenaAllocator& arena, u32 n)
{
    char text[16];
    vse::FormatTo(text, sizeof(text), "0x%08X", n);
    return ArenaCat(arena, StringView(text, vse::CStrLength(text)), StringView());
}
#endif

// The code with `indent` leading columns removed from every line.
StringView Dedent(StringView code, u32 indent, ArenaAllocator& arena)
{
    vse::StringBuilder<ArenaAllocator> sb(arena);
    usize i = 0;
    while (i <= code.Length())
    {
        usize e = i;
        while (e < code.Length() && code[e] != '\n')
        {
            ++e;
        }
        usize skip = 0;
        while (skip < indent && i + skip < e && (code[i + skip] == ' ' || code[i + skip] == '\t'))
        {
            ++skip;
        }
        sb.Append(StringView(code.Data() + i + skip, e - i - skip));
        if (e < code.Length())
        {
            sb.Append('\n');
        }
        i = e + 1;
    }
    return sb.View();
}

StringView ReadSmall(StringView path, ArenaAllocator& arena)
{
    // Captured output can be huge (a template error cascade): the first 1 MiB is kept, the rest dropped.
    constexpr usize kLimit = usize{1} << 20;
    vse::io::Path file_path;
    vse::io::FilePtr file;
    if (!vse::io::Path::Parse(path, vse::io::NativePathStyle(), &file_path) ||
        vse::io::File::Open(file_path, vse::io::OpenMode::Read, vse::io::kOpenNone, &file).Failed())
    {
        return StringView();
    }
    vse::StringBuilder<ArenaAllocator> sb(arena);
    char buf[4096];
    usize total = 0;
    while (total < kLimit)
    {
        usize n = 0;
        if (file->Read(buf, sizeof(buf), total, &n).Failed() || n == 0)
        {
            break;
        }
        sb.Append(StringView(buf, n));
        total += n;
    }
    return sb.View();
}

// `s` with the carriage return of every CR LF line end removed: a program's text output on Windows ends its lines with
// CR LF, and the expected output in the Markdown with LF.
StringView WithoutCarriageReturns(StringView s, ArenaAllocator& arena)
{
    vse::StringBuilder<ArenaAllocator> sb(arena);
    for (usize i = 0; i < s.Length(); ++i)
    {
        if (s[i] == '\r' && i + 1 < s.Length() && s[i + 1] == '\n')
        {
            continue;
        }
        sb.Append(s[i]);
    }
    return sb.View();
}

StringView TrimEnd(StringView s)
{
    usize e = s.Length();
    while (e > 0 && (s[e - 1] == '\n' || s[e - 1] == '\r' || s[e - 1] == ' ' || s[e - 1] == '\t'))
    {
        --e;
    }
    return StringView(s.Data(), e);
}

// At most `max_lines` lines of `s`, each indented, for embedding in a diagnostic.
StringView Excerpt(StringView s, u32 max_lines, ArenaAllocator& arena)
{
    s = TrimEnd(s);
    vse::StringBuilder<ArenaAllocator> sb(arena);
    u32 lines = 0;
    usize i = 0;
    while (i < s.Length() && lines < max_lines)
    {
        usize e = i;
        while (e < s.Length() && s[e] != '\n')
        {
            ++e;
        }
        sb.Append(StringView("\n  | "));
        sb.Append(StringView(s.Data() + i, e - i));
        ++lines;
        i = e + 1;
    }
    if (i < s.Length())
    {
        sb.Append(StringView("\n  | ..."));
    }
    return sb.View();
}

enum class Stage : vse::u8
{
    Pending,
    Compiling,
    Compiled,
    Running,
    Done
};

#if ENGINE_PLATFORM_WINDOWS
constexpr char kObjectSuffix[] = ".obj";
#else
constexpr char kObjectSuffix[] = ".o";
#endif

struct Task
{
    const Sample* sample = nullptr;
    Stage stage = Stage::Pending;
    ChildProcess child;
    double deadline = 0;
    bool ok = false;
    StringView src, exe, obj, cc_out, run_out, run_err;
    StringView failure;  // set when the sample failed; reported in document order after the run
};

volatile sig_atomic_t g_stop = 0;
void OnSignal(int)
{
    g_stop = 1;
}

class Runner
{
public:
    Runner(const SampleOptions& o, ArenaAllocator& arena, Vector<FileDiag, ArenaAllocator>& out, SampleStats& stats)
        : opt_(o), arena_(arena), out_(out), stats_(stats), tasks_(arena)
    {
    }

    bool Run(const Vector<Sample, ArenaAllocator>& samples)
    {
        stats_.found += samples.Size();
        if (samples.Size() == 0)
        {
            return true;
        }
        if (opt_.compiler.Empty())
        {
            return false;
        }
        if (!MakeScratch())
        {
            return false;
        }
        // Children run in their own process groups (Windows: job objects), so Ctrl-C would not reach them: stop them
        // ourselves.
        g_stop = 0;
        void (*const old_int)(int) = vse::os::SetSignalHandler(SIGINT, &OnSignal);
        void (*const old_term)(int) = vse::os::SetSignalHandler(SIGTERM, &OnSignal);
        for (usize i = 0; i < samples.Size(); ++i)
        {
            Task* t = static_cast<Task*>(arena_.Alloc(sizeof(Task), alignof(Task)));
            new (t) Task();
            t->sample = &samples[i];
            vse::StringBuilder<ArenaAllocator> base(arena_);
            base.Append(scratch_);
            base.Append(StringView("/s"));
            base.AppendUInt(i);
            StringView b = ArenaCat(arena_, base.View(), StringView());
            t->src = ArenaCat(arena_, b, StringView(".cpp"));
            t->obj = ArenaCat(arena_, b, StringView(kObjectSuffix));
            t->exe = ArenaCat(arena_, b, StringView(".exe"));
            t->cc_out = ArenaCat(arena_, b, StringView(".cc.txt"));
            t->run_out = ArenaCat(arena_, b, StringView(".out.txt"));
            t->run_err = ArenaCat(arena_, b, StringView(".err.txt"));
            tasks_.Push(t);
        }
        Drive();
        for (usize i = 0; i < tasks_.Size(); ++i)
        {  // completion order varies with --jobs; the report must not
            const Task& t = *tasks_[i];
            if (!t.failure.Empty())
            {
                out_.Push(FileDiag{t.sample->path, Diagnostic{Severity::Error, t.sample->line, t.failure}});
            }
        }
        vse::os::SetSignalHandler(SIGINT, old_int == SIG_ERR ? SIG_DFL : old_int);
        vse::os::SetSignalHandler(SIGTERM, old_term == SIG_ERR ? SIG_DFL : old_term);
        Cleanup();
        return true;
    }

private:
    bool MakeScratch()
    {
        return MakeScratchDirectory(opt_.scratch_base, StringView("docgen-samples"), arena_, scratch_);
    }

    void Cleanup()
    {
        if (opt_.keep_scratch)
        {
            return;
        }
        RemoveTree(scratch_);  // the sources, objects, executables and captured output of every task
    }

    void Fail(Task& t, StringView msg)
    {
        t.failure = msg;
        ++stats_.failed;
    }

    // The translation unit for a sample; `#line` makes compiler diagnostics name the Markdown file and line.
    StringView Generate(const Sample& s)
    {
        vse::StringBuilder<ArenaAllocator> sb(arena_);
        StringView code = Dedent(s.code, s.indent, arena_);
        auto line_directive = [&]()
        {
            sb.Append(StringView("#line "));
            sb.AppendUInt(s.body_line);
            sb.Append(StringView(" \""));
            sb.Append(s.path);
            sb.Append(StringView("\"\n"));
        };
        if (s.standalone)
        {
            line_directive();
            sb.Append(code);
            sb.Append('\n');
            return sb.View();
        }
        if (!opt_.prelude.Empty())
        {
            sb.Append(StringView("#include \""));
            sb.Append(opt_.prelude);
            sb.Append(StringView("\"\n"));
        }
        sb.Append(s.run ? StringView("int main() {\n") : StringView("[[maybe_unused]] static void DocSample() {\n"));
        line_directive();
        sb.Append(code);
        sb.Append(s.run ? StringView("\n    return 0;\n}\n") : StringView("\n}\n"));
        return sb.View();
    }

    void StartCompile(Task& t)
    {
        const Sample& s = *t.sample;
        if (!WriteText(t.src, Generate(s)))
        {
            t.stage = Stage::Done;
            Fail(t, StringView("sample: cannot write the scratch source file"));
            return;
        }
        Vector<StringView, ArenaAllocator> a(arena_);
        a.Push(opt_.compiler);
        for (usize i = 0; i < opt_.flags.Size(); ++i)
        {
            a.Push(opt_.flags[i]);
        }
        for (usize i = 0; i < opt_.includes.Size(); ++i)
        {
            a.Push(ArenaCat(arena_, StringView("-I"), opt_.includes[i]));
        }
#if ENGINE_PLATFORM_WINDOWS
        // cl and clang-cl (the compilers of the Windows build) name their outputs with /Fe and /Fo; arguments after the
        // source that are not options (the libraries) go to the linker, and so does everything after /link.
        if (s.run)
        {
            a.Push(t.src);
            a.Push(ArenaCat(arena_, StringView("/Fe"), t.exe));
            for (usize i = 0; i < opt_.links.Size(); ++i)
            {
                a.Push(opt_.links[i]);
            }
        }
        else
        {
            a.Push(StringView("/c"));
            a.Push(t.src);
            a.Push(ArenaCat(arena_, StringView("/Fo"), t.obj));
        }
#else
        if (s.run)
        {
            a.Push(t.src);
            a.Push(StringView("-o"));
            a.Push(t.exe);
            for (usize i = 0; i < opt_.links.Size(); ++i)
            {
                a.Push(opt_.links[i]);
            }
        }
        else
        {
            a.Push(StringView("-c"));
            a.Push(t.src);
            a.Push(StringView("-o"));
            a.Push(t.obj);
        }
#endif
        // Compilers keep the caller's directory (relative compiler/flag paths still work); programs run in the scratch
        // directory.
        if (!StartChild(a, t.cc_out, StringView(), StringView(), arena_, &t.child))
        {
            t.stage = Stage::Done;
            Fail(t,
                 ArenaCat(arena_, StringView("sample: cannot start the compiler '"), opt_.compiler, StringView("'")));
            return;
        }
        t.stage = Stage::Compiling;
        t.deadline = Now() + opt_.compile_timeout_s;
    }

    void StartRun(Task& t)
    {
        Vector<StringView, ArenaAllocator> a(arena_);
        a.Push(t.exe);
        if (!StartChild(a, t.run_out, t.run_err, scratch_, arena_, &t.child))
        {
            t.stage = Stage::Done;
            Fail(t, StringView("sample: cannot start the compiled program"));
            return;
        }
        t.stage = Stage::Running;
        t.deadline = Now() + opt_.run_timeout_s;
    }

    static StringView FirstDifference(StringView expected, StringView got, ArenaAllocator& arena)
    {
        usize line = 1;
        usize ep = 0, gp = 0;
        for (;; ++line)
        {
            usize ee = ep, ge = gp;
            while (ee < expected.Length() && expected[ee] != '\n')
            {
                ++ee;
            }
            while (ge < got.Length() && got[ge] != '\n')
            {
                ++ge;
            }
            StringView el(expected.Data() + ep, ee - ep), gl(got.Data() + gp, ge - gp);
            const bool e_end = ep >= expected.Length(), g_end = gp >= got.Length();
            if (e_end && g_end)
            {
                return StringView("(no difference)");
            }
            if (e_end || g_end || !(el == gl))
            {
                vse::StringBuilder<ArenaAllocator> sb(arena);
                sb.Append(StringView("first difference at output line "));
                sb.AppendUInt(line);
                sb.Append(StringView(": expected "));
                sb.Append(e_end ? StringView("end of output") : ArenaCat(arena, StringView("'"), el, StringView("'")));
                sb.Append(StringView(", got "));
                sb.Append(g_end ? StringView("end of output") : ArenaCat(arena, StringView("'"), gl, StringView("'")));
                return sb.View();
            }
            ep = ee + 1;
            gp = ge + 1;
        }
    }

    void Finish(Task& t, const ChildEnd& end, bool timed_out)
    {
        const Sample& s = *t.sample;
        if (t.stage == Stage::Compiling)
        {
            StringView log = ReadSmall(t.cc_out, arena_);
            if (timed_out)
            {
                Fail(t, ArenaCat(arena_, StringView("sample: compiler timed out after "),
                                 Num(arena_, opt_.compile_timeout_s), StringView(" s")));
                t.stage = Stage::Done;
            }
            else if (!end.exited || end.exit_code != 0)
            {
                Fail(t, ArenaCat(arena_, StringView("sample failed to compile"), Excerpt(log, 25, arena_)));
                t.stage = Stage::Done;
            }
            else
            {
                ++stats_.compiled;
                t.stage = s.run ? Stage::Compiled : Stage::Done;
                t.ok = !s.run;
            }
            return;
        }
        // Running
        ++stats_.ran;
        t.stage = Stage::Done;
        if (timed_out)
        {
            Fail(t, ArenaCat(arena_, StringView("sample timed out after "), Num(arena_, opt_.run_timeout_s),
                             StringView(" s")));
            return;
        }
        if (end.killed)
        {
#if ENGINE_PLATFORM_WINDOWS
            Fail(t, ArenaCat(arena_, StringView("sample was ended by exception "), Hex(arena_, end.cause)));
#else
            Fail(t, ArenaCat(arena_, StringView("sample was terminated by signal "), Num(arena_, end.cause)));
#endif
            return;
        }
        if (!end.exited || end.exit_code != 0)
        {
            Fail(t, ArenaCat(arena_, StringView("sample exited with status "),
                             Num(arena_, static_cast<u32>(end.exit_code)),
                             Excerpt(ReadSmall(t.run_err, arena_), 10, arena_)));
            return;
        }
        if (s.has_expected)
        {
            StringView got = TrimEnd(WithoutCarriageReturns(ReadSmall(t.run_out, arena_), arena_));
            StringView want = TrimEnd(s.expected);
            if (!(got == want))
            {
                Fail(t, ArenaCat(arena_, StringView("sample output does not match the output block: "),
                                 FirstDifference(want, got, arena_)));
                return;
            }
        }
        t.ok = true;
    }

    void Interrupt()
    {
        for (usize i = 0; i < tasks_.Size(); ++i)
        {
            Task& t = *tasks_[i];
            if (t.stage == Stage::Compiling || t.stage == Stage::Running)
            {
                KillChild(t.child);
            }
            t.stage = Stage::Done;
        }
        out_.Push(FileDiag{StringView("docgen samples"),
                           Diagnostic{Severity::Error, 0, StringView("interrupted; running samples were killed")}});
    }

    void Drive()
    {
        usize running = 0;
        usize done = 0;
        const usize jobs = opt_.jobs == 0 ? 1 : opt_.jobs;
        while (done < tasks_.Size())
        {
            if (g_stop != 0)
            {
                Interrupt();
                return;
            }
            for (usize i = 0; i < tasks_.Size() && running < jobs; ++i)
            {
                Task& t = *tasks_[i];
                if (t.stage == Stage::Pending)
                {
                    StartCompile(t);
                    if (t.stage == Stage::Compiling)
                    {
                        ++running;
                    }
                }
                else if (t.stage == Stage::Compiled)
                {
                    StartRun(t);
                    if (t.stage == Stage::Running)
                    {
                        ++running;
                    }
                }
            }
            bool progress = false;
            for (usize i = 0; i < tasks_.Size(); ++i)
            {
                Task& t = *tasks_[i];
                if (t.stage != Stage::Compiling && t.stage != Stage::Running)
                {
                    continue;
                }
                ChildEnd end;
                bool timed_out = false;
                const ChildPoll polled = PollChild(t.child, &end);
                if (polled == ChildPoll::Lost)
                {
                    --running;
                    progress = true;
                    t.stage = Stage::Done;
                    Fail(t, StringView("sample: lost track of the child process"));
                    continue;
                }
                if (polled == ChildPoll::Running)
                {
                    if (Now() < t.deadline)
                    {
                        continue;
                    }
                    KillChild(t.child);
                    timed_out = true;
                }
                --running;
                progress = true;
                Finish(t, end, timed_out);
            }
            done = 0;
            for (usize i = 0; i < tasks_.Size(); ++i)
            {
                done += tasks_[i]->stage == Stage::Done;
            }
            if (!progress && done < tasks_.Size())
            {
                vse::os::SleepNanos(2 * 1000 * 1000);
            }
        }
    }

    const SampleOptions& opt_;
    ArenaAllocator& arena_;
    Vector<FileDiag, ArenaAllocator>& out_;
    SampleStats& stats_;
    Vector<Task*, ArenaAllocator> tasks_;
    StringView scratch_;
};

}  // namespace

void ExtractSamples(StringView path, StringView text, const MdDoc& doc, ArenaAllocator& arena,
                    Vector<Sample, ArenaAllocator>& samples, Vector<FileDiag, ArenaAllocator>& problems)
{
    Vector<usize, ArenaAllocator> starts(arena);
    LineStarts(text, starts);
    for (usize i = 0; i < doc.fences.Size(); ++i)
    {
        const MdFence& f = doc.fences[i];
        Vector<StringView, ArenaAllocator> w(arena);
        Words(f.info, w);
        if (w.Size() == 0 || !(w[0] == StringView("cpp") || w[0] == StringView("c++")))
        {
            continue;
        }
        bool compile = false, run = false, standalone = false, unknown = false;
        StringView bad;
        for (usize k = 1; k < w.Size(); ++k)
        {
            if (w[k] == StringView("compile"))
            {
                compile = true;
            }
            else if (w[k] == StringView("run"))
            {
                run = true;
            }
            else if (w[k] == StringView("standalone"))
            {
                standalone = true;
            }
            else if (!unknown)
            {
                unknown = true;
                bad = w[k];
            }
        }
        if (!compile && !run && !standalone && !unknown)
        {
            continue;  // plain `cpp`: illustrative, never built or run
        }
        auto err = [&](StringView m) { problems.Push(FileDiag{path, Diagnostic{Severity::Error, f.line, m}}); };
        if (unknown)
        {
            err(ArenaCat(arena, StringView("unknown sample tag '"), bad,
                         StringView("' (expected compile, standalone, run)")));
            continue;
        }
        if (!compile)
        {
            err(StringView("sample tag 'run' or 'standalone' needs 'compile': write ```cpp compile run"));
            continue;
        }
        if (!f.closed)
        {
            continue;  // already reported as an unterminated fence
        }
        Sample s;
        s.path = path;
        s.line = f.line;
        s.body_line = f.body_line;
        s.indent = f.indent;
        s.standalone = standalone;
        s.run = run;
        s.code = f.body;
        if (run && i + 1 < doc.fences.Size())
        {
            const MdFence& n = doc.fences[i + 1];
            Vector<StringView, ArenaAllocator> nw(arena);
            Words(n.info, nw);
            if (nw.Size() == 1 && nw[0] == StringView("output") && n.line > f.end_line &&
                BlankBetween(text, starts, f.end_line, n.line))
            {
                s.has_expected = true;
                s.expected = Dedent(n.body, n.indent, arena);
            }
        }
        samples.Push(s);
    }
}

void RequestStop()
{
    g_stop = 1;
}

bool RunSamples(const SampleOptions& options, const Vector<Sample, ArenaAllocator>& samples, ArenaAllocator& arena,
                Vector<FileDiag, ArenaAllocator>& out, SampleStats& stats)
{
    Runner r(options, arena, out, stats);
    return r.Run(samples);
}

StringView AbsoluteKeepingLinks(StringView path, ArenaAllocator& arena)
{
    vse::io::Path parsed;  // (on Windows `C:/...` is absolute too)
    if ((path.Length() > 0 && path.Data()[0] == '/') ||
        (vse::io::Path::Parse(path, vse::io::NativePathStyle(), &parsed) && parsed.IsAbsolute()))
    {
        return ArenaCat(arena, path, StringView());
    }
    StringView cwd;
    if (!WorkingDirectory(arena, cwd))
    {
        return ArenaCat(arena, path, StringView());
    }
    StringView rel = path;
    while (rel.Length() >= 2 && rel.Data()[0] == '.' && rel.Data()[1] == '/')
    {
        rel = StringView(rel.Data() + 2, rel.Length() - 2);
    }
    return ArenaCat(arena, cwd, StringView("/"), rel);
}

}  // namespace docgen
