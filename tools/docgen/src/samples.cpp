#include "samples.h"

#include <fcntl.h>
#include <signal.h>
#include <spawn.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>

#include <cerrno>
#include <cstdio>
#include <cstdlib>
#include <cstring>

#include "source_file.h"

extern char** environ;

namespace docgen {

namespace {

bool IsWordChar(char c) { return c != ' ' && c != '\t'; }

// Splits an info string into words.
void Words(StringView info, Array<StringView, ArenaAllocator>& out) {
    usize i = 0;
    while (i < info.Length()) {
        while (i < info.Length() && !IsWordChar(info[i])) ++i;
        usize e = i;
        while (e < info.Length() && IsWordChar(info[e])) ++e;
        if (e > i) out.Push(StringView(info.Data() + i, e - i));
        i = e;
    }
}

// Byte offsets of the start of every line of `text` (line n starts at starts[n-1]).
void LineStarts(StringView text, Array<usize, ArenaAllocator>& starts) {
    starts.Push(0);
    for (usize i = 0; i < text.Length(); ++i) {
        if (text[i] == '\n') starts.Push(i + 1);
    }
}

// Are all lines strictly between `a` and `b` (1-based) blank?
bool BlankBetween(StringView text, const Array<usize, ArenaAllocator>& starts, u32 a, u32 b) {
    for (u32 line = a + 1; line < b && line <= starts.Size(); ++line) {
        for (usize i = starts[line - 1]; i < text.Length() && text[i] != '\n'; ++i) {
            if (text[i] != ' ' && text[i] != '\t' && text[i] != '\r') return false;
        }
    }
    return true;
}

double Now() {
    timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return static_cast<double>(ts.tv_sec) + static_cast<double>(ts.tv_nsec) / 1e9;
}

StringView Num(ArenaAllocator& arena, u32 n) {
    engine_placeholder::StringBuilder<ArenaAllocator> sb(arena);
    sb.AppendUInt(n);
    return sb.View();
}

// The code with `indent` leading columns removed from every line.
StringView Dedent(StringView code, u32 indent, ArenaAllocator& arena) {
    engine_placeholder::StringBuilder<ArenaAllocator> sb(arena);
    usize i = 0;
    while (i <= code.Length()) {
        usize e = i;
        while (e < code.Length() && code[e] != '\n') ++e;
        usize skip = 0;
        while (skip < indent && i + skip < e && (code[i + skip] == ' ' || code[i + skip] == '\t')) ++skip;
        sb.Append(StringView(code.Data() + i + skip, e - i - skip));
        if (e < code.Length()) sb.Append('\n');
        i = e + 1;
    }
    return sb.View();
}

bool WriteFile(StringView path, StringView content) {
    FILE* f = std::fopen(path.Data(), "wb");
    if (f == nullptr) return false;
    const bool ok = content.Empty() || std::fwrite(content.Data(), 1, content.Length(), f) == content.Length();
    return std::fclose(f) == 0 && ok;
}

StringView ReadSmall(StringView path, ArenaAllocator& arena) {
    FILE* f = std::fopen(path.Data(), "rb");
    if (f == nullptr) return StringView();
    engine_placeholder::StringBuilder<ArenaAllocator> sb(arena);
    char buf[4096];
    usize n;
    usize total = 0;
    while ((n = std::fread(buf, 1, sizeof(buf), f)) > 0 && total < (usize{1} << 20)) {
        sb.Append(StringView(buf, n));
        total += n;
    }
    std::fclose(f);
    return sb.View();
}

StringView TrimEnd(StringView s) {
    usize e = s.Length();
    while (e > 0 && (s[e - 1] == '\n' || s[e - 1] == '\r' || s[e - 1] == ' ' || s[e - 1] == '\t')) --e;
    return StringView(s.Data(), e);
}

// At most `max_lines` lines of `s`, each indented, for embedding in a diagnostic.
StringView Excerpt(StringView s, u32 max_lines, ArenaAllocator& arena) {
    s = TrimEnd(s);
    engine_placeholder::StringBuilder<ArenaAllocator> sb(arena);
    u32 lines = 0;
    usize i = 0;
    while (i < s.Length() && lines < max_lines) {
        usize e = i;
        while (e < s.Length() && s[e] != '\n') ++e;
        sb.Append(StringView("\n  | "));
        sb.Append(StringView(s.Data() + i, e - i));
        ++lines;
        i = e + 1;
    }
    if (i < s.Length()) sb.Append(StringView("\n  | ..."));
    return sb.View();
}

enum class Stage : engine_placeholder::u8 { Pending, Compiling, Compiled, Running, Done };

struct Task {
    const Sample* sample = nullptr;
    Stage stage = Stage::Pending;
    pid_t pid = -1;
    double deadline = 0;
    bool ok = false;
    StringView src, exe, obj, cc_out, run_out, run_err;
};

volatile sig_atomic_t g_stop = 0;
void OnSignal(int) { g_stop = 1; }

class Runner {
public:
    Runner(const SampleOptions& o, ArenaAllocator& arena, Array<FileDiag, ArenaAllocator>& out, SampleStats& stats)
        : opt_(o), arena_(arena), out_(out), stats_(stats), tasks_(arena) {}

    bool Run(const Array<Sample, ArenaAllocator>& samples) {
        stats_.found += samples.Size();
        if (samples.Size() == 0) return true;
        if (opt_.compiler.Empty()) return false;
        if (!MakeScratch()) return false;
        // Children run in their own process groups, so Ctrl-C would not reach them: stop them ourselves.
        g_stop = 0;
        struct sigaction sa, old_int, old_term;
        std::memset(&sa, 0, sizeof(sa));
        sa.sa_handler = OnSignal;
        sigemptyset(&sa.sa_mask);
        sigaction(SIGINT, &sa, &old_int);
        sigaction(SIGTERM, &sa, &old_term);
        for (usize i = 0; i < samples.Size(); ++i) {
            Task* t = static_cast<Task*>(arena_.Alloc(sizeof(Task), alignof(Task)));
            new (t) Task();
            t->sample = &samples[i];
            engine_placeholder::StringBuilder<ArenaAllocator> base(arena_);
            base.Append(scratch_);
            base.Append(StringView("/s"));
            base.AppendUInt(i);
            StringView b = ArenaCat(arena_, base.View(), StringView());
            t->src = ArenaCat(arena_, b, StringView(".cpp"));
            t->obj = ArenaCat(arena_, b, StringView(".o"));
            t->exe = ArenaCat(arena_, b, StringView(".exe"));
            t->cc_out = ArenaCat(arena_, b, StringView(".cc.txt"));
            t->run_out = ArenaCat(arena_, b, StringView(".out.txt"));
            t->run_err = ArenaCat(arena_, b, StringView(".err.txt"));
            tasks_.Push(t);
        }
        Drive();
        sigaction(SIGINT, &old_int, nullptr);
        sigaction(SIGTERM, &old_term, nullptr);
        Cleanup();
        return true;
    }

private:
    bool MakeScratch() {
        StringView base = opt_.scratch_base.Empty() ? StringView("/tmp") : opt_.scratch_base;
        StringView tmpl = ArenaCat(arena_, base, StringView("/docgen-samples-XXXXXX"));
        if (mkdtemp(const_cast<char*>(tmpl.Data())) == nullptr) return false;
        scratch_ = tmpl;
        return true;
    }

    void Cleanup() {
        if (opt_.keep_scratch) return;
        for (usize i = 0; i < tasks_.Size(); ++i) {
            const Task& t = *tasks_[i];
            for (StringView p : {t.src, t.obj, t.exe, t.cc_out, t.run_out, t.run_err}) unlink(p.Data());
        }
        rmdir(scratch_.Data());
    }

    void Fail(const Task& t, StringView msg) {
        out_.Push(FileDiag{t.sample->path, Diagnostic{Severity::Error, t.sample->line, msg}});
        ++stats_.failed;
    }

    // The translation unit for a sample; `#line` makes compiler diagnostics name the Markdown file and line.
    StringView Generate(const Sample& s) {
        engine_placeholder::StringBuilder<ArenaAllocator> sb(arena_);
        StringView code = Dedent(s.code, s.indent, arena_);
        auto line_directive = [&]() {
            sb.Append(StringView("#line "));
            sb.AppendUInt(s.body_line);
            sb.Append(StringView(" \""));
            sb.Append(s.path);
            sb.Append(StringView("\"\n"));
        };
        if (s.standalone) {
            line_directive();
            sb.Append(code);
            sb.Append('\n');
            return sb.View();
        }
        if (!opt_.prelude.Empty()) {
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

    bool Spawn(const Array<StringView, ArenaAllocator>& args, StringView out_path, StringView err_path, bool merge_err, bool in_scratch,
               pid_t& pid) {
        Array<char*, ArenaAllocator> argv(arena_);
        for (usize i = 0; i < args.Size(); ++i) argv.Push(const_cast<char*>(args[i].Data()));
        argv.Push(nullptr);
        posix_spawn_file_actions_t fa;
        posix_spawn_file_actions_init(&fa);
        posix_spawn_file_actions_addopen(&fa, 0, "/dev/null", O_RDONLY, 0);
        posix_spawn_file_actions_addopen(&fa, 1, out_path.Data(), O_WRONLY | O_CREAT | O_TRUNC, 0600);
        if (merge_err) posix_spawn_file_actions_adddup2(&fa, 1, 2);
        else posix_spawn_file_actions_addopen(&fa, 2, err_path.Data(), O_WRONLY | O_CREAT | O_TRUNC, 0600);
        // Compilers keep the caller's directory (relative compiler/flag paths still work); programs run in the scratch directory.
        if (in_scratch) posix_spawn_file_actions_addchdir_np(&fa, scratch_.Data());
        posix_spawnattr_t attr;
        posix_spawnattr_init(&attr);
        posix_spawnattr_setflags(&attr, POSIX_SPAWN_SETPGROUP); // own process group: a timeout kills the whole tree
        posix_spawnattr_setpgroup(&attr, 0);
        const bool has_slash = args[0].IndexOf('/') != args[0].Length();
        const int rc = has_slash ? posix_spawn(&pid, args[0].Data(), &fa, &attr, &argv[0], environ)
                                 : posix_spawnp(&pid, args[0].Data(), &fa, &attr, &argv[0], environ);
        posix_spawn_file_actions_destroy(&fa);
        posix_spawnattr_destroy(&attr);
        return rc == 0;
    }

    void StartCompile(Task& t) {
        const Sample& s = *t.sample;
        if (!WriteFile(t.src, Generate(s))) {
            t.stage = Stage::Done;
            Fail(t, StringView("sample: cannot write the scratch source file"));
            return;
        }
        Array<StringView, ArenaAllocator> a(arena_);
        a.Push(opt_.compiler);
        for (usize i = 0; i < opt_.flags.Size(); ++i) a.Push(opt_.flags[i]);
        for (usize i = 0; i < opt_.includes.Size(); ++i) a.Push(ArenaCat(arena_, StringView("-I"), opt_.includes[i]));
        if (s.run) {
            a.Push(t.src);
            a.Push(StringView("-o"));
            a.Push(t.exe);
            for (usize i = 0; i < opt_.links.Size(); ++i) a.Push(opt_.links[i]);
        } else {
            a.Push(StringView("-c"));
            a.Push(t.src);
            a.Push(StringView("-o"));
            a.Push(t.obj);
        }
        if (!Spawn(a, t.cc_out, StringView(), true, false, t.pid)) {
            t.stage = Stage::Done;
            Fail(t, ArenaCat(arena_, StringView("sample: cannot start the compiler '"), opt_.compiler, StringView("'")));
            return;
        }
        t.stage = Stage::Compiling;
        t.deadline = Now() + opt_.compile_timeout_s;
    }

    void StartRun(Task& t) {
        Array<StringView, ArenaAllocator> a(arena_);
        a.Push(t.exe);
        if (!Spawn(a, t.run_out, t.run_err, false, true, t.pid)) {
            t.stage = Stage::Done;
            Fail(t, StringView("sample: cannot start the compiled program"));
            return;
        }
        t.stage = Stage::Running;
        t.deadline = Now() + opt_.run_timeout_s;
    }

    static StringView FirstDifference(StringView expected, StringView got, ArenaAllocator& arena) {
        usize line = 1;
        usize ep = 0, gp = 0;
        for (;; ++line) {
            usize ee = ep, ge = gp;
            while (ee < expected.Length() && expected[ee] != '\n') ++ee;
            while (ge < got.Length() && got[ge] != '\n') ++ge;
            StringView el(expected.Data() + ep, ee - ep), gl(got.Data() + gp, ge - gp);
            const bool e_end = ep >= expected.Length(), g_end = gp >= got.Length();
            if (e_end && g_end) return StringView("(no difference)");
            if (e_end || g_end || !(el == gl)) {
                engine_placeholder::StringBuilder<ArenaAllocator> sb(arena);
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

    void Finish(Task& t, int status, bool timed_out) {
        const Sample& s = *t.sample;
        if (t.stage == Stage::Compiling) {
            StringView log = ReadSmall(t.cc_out, arena_);
            if (timed_out) {
                Fail(t, ArenaCat(arena_, StringView("sample: compiler timed out after "), Num(arena_, opt_.compile_timeout_s), StringView(" s")));
                t.stage = Stage::Done;
            } else if (!WIFEXITED(status) || WEXITSTATUS(status) != 0) {
                Fail(t, ArenaCat(arena_, StringView("sample failed to compile"), Excerpt(log, 25, arena_)));
                t.stage = Stage::Done;
            } else {
                ++stats_.compiled;
                t.stage = s.run ? Stage::Compiled : Stage::Done;
                t.ok = !s.run;
            }
            return;
        }
        // Running
        ++stats_.ran;
        t.stage = Stage::Done;
        if (timed_out) {
            Fail(t, ArenaCat(arena_, StringView("sample timed out after "), Num(arena_, opt_.run_timeout_s), StringView(" s")));
            return;
        }
        if (WIFSIGNALED(status)) {
            Fail(t, ArenaCat(arena_, StringView("sample was terminated by signal "), Num(arena_, static_cast<u32>(WTERMSIG(status)))));
            return;
        }
        if (WEXITSTATUS(status) != 0) {
            Fail(t, ArenaCat(arena_, StringView("sample exited with status "), Num(arena_, static_cast<u32>(WEXITSTATUS(status))),
                             Excerpt(ReadSmall(t.run_err, arena_), 10, arena_)));
            return;
        }
        if (s.has_expected) {
            StringView got = TrimEnd(ReadSmall(t.run_out, arena_));
            StringView want = TrimEnd(s.expected);
            if (!(got == want)) {
                Fail(t, ArenaCat(arena_, StringView("sample output does not match the output block: "), FirstDifference(want, got, arena_)));
                return;
            }
        }
        t.ok = true;
    }

    void Interrupt() {
        for (usize i = 0; i < tasks_.Size(); ++i) {
            Task& t = *tasks_[i];
            if (t.stage == Stage::Compiling || t.stage == Stage::Running) {
                kill(-t.pid, SIGKILL);
                int status;
                waitpid(t.pid, &status, 0);
            }
            t.stage = Stage::Done;
        }
        out_.Push(FileDiag{StringView("docgen samples"), Diagnostic{Severity::Error, 0, StringView("interrupted; running samples were killed")}});
    }

    void Drive() {
        usize running = 0;
        usize done = 0;
        const usize jobs = opt_.jobs == 0 ? 1 : opt_.jobs;
        while (done < tasks_.Size()) {
            if (g_stop != 0) {
                Interrupt();
                return;
            }
            for (usize i = 0; i < tasks_.Size() && running < jobs; ++i) {
                Task& t = *tasks_[i];
                if (t.stage == Stage::Pending) {
                    StartCompile(t);
                    if (t.stage == Stage::Compiling) ++running;
                } else if (t.stage == Stage::Compiled) {
                    StartRun(t);
                    if (t.stage == Stage::Running) ++running;
                }
            }
            bool progress = false;
            for (usize i = 0; i < tasks_.Size(); ++i) {
                Task& t = *tasks_[i];
                if (t.stage != Stage::Compiling && t.stage != Stage::Running) continue;
                int status = 0;
                bool timed_out = false;
                pid_t r = waitpid(t.pid, &status, WNOHANG);
                if (r < 0) {
                    if (errno == EINTR) continue;
                    // The child cannot be waited for (reaped elsewhere, SIGCHLD ignored): never read that as success.
                    kill(-t.pid, SIGKILL);
                    --running;
                    progress = true;
                    t.stage = Stage::Done;
                    Fail(t, StringView("sample: lost track of the child process (waitpid failed)"));
                    continue;
                }
                if (r == 0) {
                    if (Now() < t.deadline) continue;
                    kill(-t.pid, SIGKILL);
                    waitpid(t.pid, &status, 0);
                    timed_out = true;
                }
                --running;
                progress = true;
                Finish(t, status, timed_out);
            }
            done = 0;
            for (usize i = 0; i < tasks_.Size(); ++i) done += tasks_[i]->stage == Stage::Done;
            if (!progress && done < tasks_.Size()) {
                timespec ts{0, 2 * 1000 * 1000};
                nanosleep(&ts, nullptr);
            }
        }
    }

    const SampleOptions& opt_;
    ArenaAllocator& arena_;
    Array<FileDiag, ArenaAllocator>& out_;
    SampleStats& stats_;
    Array<Task*, ArenaAllocator> tasks_;
    StringView scratch_;
};

} // namespace

void ExtractSamples(StringView path, StringView text, const MdDoc& doc, ArenaAllocator& arena, Array<Sample, ArenaAllocator>& samples,
                    Array<FileDiag, ArenaAllocator>& problems) {
    Array<usize, ArenaAllocator> starts(arena);
    LineStarts(text, starts);
    for (usize i = 0; i < doc.fences.Size(); ++i) {
        const MdFence& f = doc.fences[i];
        Array<StringView, ArenaAllocator> w(arena);
        Words(f.info, w);
        if (w.Size() == 0 || !(w[0] == StringView("cpp") || w[0] == StringView("c++"))) continue;
        bool compile = false, run = false, standalone = false, unknown = false;
        StringView bad;
        for (usize k = 1; k < w.Size(); ++k) {
            if (w[k] == StringView("compile")) compile = true;
            else if (w[k] == StringView("run")) run = true;
            else if (w[k] == StringView("standalone")) standalone = true;
            else if (!unknown) {
                unknown = true;
                bad = w[k];
            }
        }
        if (!compile && !run && !standalone && !unknown) continue; // plain `cpp`: illustrative, never built or run
        auto err = [&](StringView m) { problems.Push(FileDiag{path, Diagnostic{Severity::Error, f.line, m}}); };
        if (unknown) {
            err(ArenaCat(arena, StringView("unknown sample tag '"), bad, StringView("' (expected compile, standalone, run)")));
            continue;
        }
        if (!compile) {
            err(StringView("sample tag 'run' or 'standalone' needs 'compile': write ```cpp compile run"));
            continue;
        }
        if (!f.closed) continue; // already reported as an unterminated fence
        Sample s;
        s.path = path;
        s.line = f.line;
        s.body_line = f.body_line;
        s.indent = f.indent;
        s.standalone = standalone;
        s.run = run;
        s.code = f.body;
        if (run && i + 1 < doc.fences.Size()) {
            const MdFence& n = doc.fences[i + 1];
            Array<StringView, ArenaAllocator> nw(arena);
            Words(n.info, nw);
            if (nw.Size() == 1 && nw[0] == StringView("output") && n.line > f.end_line && BlankBetween(text, starts, f.end_line, n.line)) {
                s.has_expected = true;
                s.expected = Dedent(n.body, n.indent, arena);
            }
        }
        samples.Push(s);
    }
}

void RequestStop() { g_stop = 1; }

bool RunSamples(const SampleOptions& options, const Array<Sample, ArenaAllocator>& samples, ArenaAllocator& arena,
                Array<FileDiag, ArenaAllocator>& out, SampleStats& stats) {
    Runner r(options, arena, out, stats);
    return r.Run(samples);
}

} // namespace docgen
