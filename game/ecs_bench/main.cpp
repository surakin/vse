// ecs_bench: the Phase 17 Day 235 benchmark of the ECS against a naive object-oriented baseline (see bench.h). Prints
// one table per scenario; nothing here is asserted in CI except by tests/ecs (checksums and the ratio guard). Numbers
// are recorded in plans/phase-17-ecs/day-235-benchmarks.md and docs/guide/ecs.md ("Cost").
//
//   ecs_bench [--entities N]   the entity count of the single-size scenarios (default 100000)
//             [--frames N]     timed frames per measurement (default 200)
//             [--threads N]    the largest worker count of the scaling scenario (default: the machine's participant
//             count)
//             [--csv PATH]     also write every measurement to a CSV file

#include "bench.h"

#include "cstr/cstr.h"
#include "cstr/format.h"
#include "cstr/number.h"
#include "debug/program_setup.h"
#include "io/file.h"
#include "io/path.h"
#include "jobs/scheduler.h"
#include "os/console.h"
#include "os/os.h"

using namespace engine_placeholder;
using namespace ecs_bench;

namespace
{

Vector<Row> g_rows;
bool g_ok = true;

void Record(const char* scenario, Impl impl, const Options& options, const Result& result)
{
    g_rows.Push(MakeRow(scenario, impl, options, result));
    if (!result.valid)
    {
        os::PrintError("FAILED: %s (%s) did not validate\n", scenario, impl == Impl::Ecs ? "ecs" : "oop");
        g_ok = false;
    }
}

// Runs both implementations, prints a line and returns the baseline-to-ECS ratio of the medians.
double Compare(const char* scenario, Options options, Result (*run)(Impl, const Options&))
{
    const Result e = run(Impl::Ecs, options);
    const Result o = run(Impl::Oop, options);
    Record(scenario, Impl::Ecs, options, e);
    Record(scenario, Impl::Oop, options, o);
    const bool match = ChecksumsMatch(e.checksum, o.checksum);
    if (!match)
    {
        g_ok = false;
    }
    const double ratio = e.time.median_ms > 0.0 ? o.time.median_ms / e.time.median_ms : 0.0;
    os::Print("%-22s %9u  ecs %9.4f ms (p95 %9.4f)  oop %9.4f ms (p95 %9.4f)  x%6.2f  checksum %s\n", scenario,
              options.entities, e.time.median_ms, e.time.p95_ms, o.time.median_ms, o.time.p95_ms, ratio,
              match ? "ok" : "MISMATCH");
    return ratio;
}

}  // namespace

int main(int argc, char** argv)
{
    const debug::ProgramDebugSetup debug_setup = debug::StartProgramDebugging(argc, argv);
    debug::RunCrashTest(debug_setup.crash_test);
    Options options;
    usize max_threads = jobs::DefaultParticipantCount();
    const char* csv = nullptr;
    static const os::CommandLineOption kCliOptions[] = {{"entities", true},         {"frames", true},
                                                        {"threads", true},          {"csv", true},
                                                        debug::kCrashHandlerOption, debug::kCrashTestOption};
    const os::CommandLine<HeapAllocator> line(HeapAllocator::Default(), argc, argv,
                                              Span<const os::CommandLineOption>(kCliOptions, 6));
    const char* program = line.ProgramName().Data();
    if (!line.Ok() || line.ArgCount() != 0)
    {
        os::PrintError("usage: %s [--entities N] [--frames N] [--threads N] [--csv PATH]\n", program);
        return 2;
    }
    StringView value;
    if (line.TryGetOption("entities", value))
    {
        options.entities = static_cast<u32>(CStrToLong(value.Data()));
    }
    if (line.TryGetOption("frames", value))
    {
        options.frames = static_cast<u32>(CStrToLong(value.Data()));
    }
    if (line.TryGetOption("threads", value))
    {
        max_threads = static_cast<usize>(CStrToLong(value.Data()));
    }
    if (line.TryGetOption("csv", value))
    {
        csv = value.Data();
    }
    if (max_threads == 0)
    {
        max_threads = 1;
    }

    os::Print("== 1. iteration (four steps, one thread) ==\n");
    double ratio_100k = 0.0;
    const u32 sizes[] = {1000, 10000, 100000, 1000000};
    for (u32 n : sizes)
    {
        Options o = options;
        o.entities = n;
        if (n >= 1000000)
        {
            o.frames = options.frames < 50 ? options.frames : 50;
        }
        const double r = Compare("iteration", o, RunIteration);
        if (n == 100000)
        {
            ratio_100k = r;
        }
    }

    os::Print("== 2. parallel scaling of a heavy system (%u entities) ==\n", options.entities);
    double efficiency4 = 0.0;
    double base_ms = 0.0;
    {
        Options o = options;
        o.participants = 1;
        const Result oop = RunHeavy(Impl::Oop, o);
        Record("heavy", Impl::Oop, o, oop);
        os::Print("%-22s oop one thread %9.4f ms\n", "heavy", oop.time.median_ms);
        usize counts[16];
        usize nc = 0;
        for (usize w = 1; w <= max_threads && nc < 15; w *= 2)
        {
            counts[nc++] = w;
        }
        if (counts[nc - 1] != max_threads)
        {
            counts[nc++] = max_threads;
        }
        for (usize k = 0; k < nc; ++k)
        {
            o.participants = counts[k];
            const Result e = RunHeavy(Impl::Ecs, o);
            Record("heavy", Impl::Ecs, o, e);
            if (k == 0)
            {
                base_ms = e.time.median_ms;
            }
            const double speedup = e.time.median_ms > 0.0 ? base_ms / e.time.median_ms : 0.0;
            const double eff = speedup / static_cast<double>(counts[k]);
            if (counts[k] == 4)
            {
                efficiency4 = eff;
            }
            os::Print("%-22s workers %2llu  ecs %9.4f ms  speedup x%5.2f  efficiency %5.1f%%  checksum %s\n", "heavy",
                      static_cast<unsigned long long>(counts[k]), e.time.median_ms, speedup, eff * 100.0,
                      ChecksumsMatch(e.checksum, oop.checksum) ? "ok" : "MISMATCH");
            if (!ChecksumsMatch(e.checksum, oop.checksum))
            {
                g_ok = false;
            }
        }
    }

    os::Print("== 3. structural churn (10%% created and destroyed, 10%% tag toggled, a frame) ==\n");
    Compare("churn", options, RunChurn);
    os::Print("== 4. sparse access (1%% of the entities) ==\n");
    const double sparse_ratio = Compare("sparse", options, RunSparse);
    os::Print("== 5. Changed<T>, 1%% written a frame ==\n");
    Compare("changed_clustered", options, [](Impl i, const Options& o) { return RunChanged(i, o, true); });
    Compare("changed_random", options, [](Impl i, const Options& o) { return RunChanged(i, o, false); });
    os::Print("== 6. hierarchy propagation ==\n");
    Compare("hierarchy", options, RunHierarchy);

    os::Print("== 7. memory and lifecycle (%u entities) ==\n", options.entities);
    for (Impl impl : {Impl::Ecs, Impl::Oop})
    {
        const Lifecycle l = RunLifecycle(impl, options);
        os::Print("%-22s %s  %7.1f bytes/entity  create %7.1f ns  destroy %7.1f ns\n", "lifecycle",
                  impl == Impl::Ecs ? "ecs" : "oop", l.bytes_per_entity, l.create_ns, l.destroy_ns);
    }

    os::Print("== 8. change stamping (%u entities) ==\n", options.entities);
    {
        const Stamping st = RunStamping(options);
        os::Print("%-22s read-only %9.4f ms  write term %9.4f ms  overhead %+.1f%%\n", "stamping", st.read.median_ms,
                  st.write.median_ms,
                  st.read.median_ms > 0.0 ? (st.write.median_ms / st.read.median_ms - 1.0) * 100.0 : 0.0);
    }

    os::Print("== pass conditions ==\n");
    const bool p1 = ratio_100k >= 3.0;
    const bool p2 = efficiency4 >= 0.7 || max_threads < 4;
    os::Print("iteration at 100k: ecs x%.2f faster than the baseline (need >= 3.0): %s\n", ratio_100k,
              p1 ? "PASS" : "MISS");
    os::Print("parallel efficiency at 4 workers: %.1f%% (need >= 70%%): %s\n", efficiency4 * 100.0,
              p2 ? (max_threads < 4 ? "SKIPPED (fewer than 4 workers)" : "PASS") : "MISS");
    os::Print("sparse access: ecs x%.2f the baseline's speed (a sparse set is considered when this is below 0.5): %s\n",
              sparse_ratio, sparse_ratio >= 0.5 ? "no sparse set needed" : "CONSIDER A SPARSE SET");

    if (csv != nullptr)
    {
        const Vector<char> text = FormatCsv(g_rows);
        io::Path file;
        if (!io::Path::Parse(StringView(csv), io::NativePathStyle(), &file) ||
            !io::WriteFile(file, text.Data(), text.Size()).Ok())
        {
            os::PrintError("could not write %s\n", csv);
            return 1;
        }
    }
    return g_ok ? 0 : 1;
}
