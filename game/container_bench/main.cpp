// container_bench: the container audit's benchmark (plans/maintenance/container-audit.md, Day 1). One line per case:
// the median time of a run and the time per operation. Nothing is asserted; the numbers are recorded in the series'
// day files.
//
//   container_bench [--section micro|paths|all]   which cases (default all)
//                   [--runs N]                    runs per case, the median is reported (default 5)
//                   [--quick]                     small sizes only (a smoke run)
//                   [--csv PATH]                  also write the rows to a CSV file

#include "bench.h"

#include "cstr/cstr.h"
#include "cstr/format.h"
#include "cstr/number.h"
#include "debug/program_setup.h"
#include "io/file.h"
#include "io/path.h"
#include "os/command_line.h"
#include "os/console.h"

using namespace engine_placeholder;
using namespace container_bench;

namespace container_bench
{

namespace
{

volatile u64 g_sink = 0;

}  // namespace

void Keep(u64 value)
{
    g_sink = g_sink + value;
}

void Report::Add(const char* section, const char* name, u64 size, double ms, u64 operations)
{
    Row row;
    row.section.Append(StringView(section));
    row.name.Append(StringView(name));
    row.size = size;
    row.ms = ms;
    row.ns_per = operations > 0 ? ms * 1.0e6 / static_cast<double>(operations) : 0.0;
    os::Print("%-6s %-56s %10.3f ms %12.1f ns/op\n", section, name, ms, row.ns_per);
    rows_.Push(Move(row));
}

}  // namespace container_bench

int main(int argc, char** argv)
{
    const debug::ProgramDebugSetup debug_setup = debug::StartProgramDebugging(argc, argv);
    debug::RunCrashTest(debug_setup.crash_test);
    static const os::CommandLineOption kOptions[] = {{"section", true},          {"runs", true},
                                                     {"quick", false},           {"csv", true},
                                                     debug::kCrashHandlerOption, debug::kCrashTestOption};
    const os::CommandLine<HeapAllocator> line(HeapAllocator::Default(), argc, argv,
                                              Span<const os::CommandLineOption>(kOptions, 6));
    const char* program = line.ProgramName().Data();
    const auto usage = [&]()
    {
        os::PrintError("usage: %s [--section micro|paths|all] [--runs N] [--quick] [--csv PATH]\n", program);
        return 2;
    };
    if (!line.Ok() || line.ArgCount() != 0)
    {
        return usage();
    }
    Options options;
    options.quick = line.HasFlag("quick");
    StringView value;
    if (line.TryGetOption("runs", value))
    {
        u64 runs = 0;
        runs = static_cast<u64>(CStrToLong(value.Data()));
        if (runs == 0 || runs > 1000)
        {
            return usage();
        }
        options.runs = static_cast<u32>(runs);
    }
    StringView section("all");
    line.TryGetOption("section", section);
    const bool micro = section == StringView("micro") || section == StringView("all");
    const bool paths = section == StringView("paths") || section == StringView("all");
    if (!micro && !paths)
    {
        return usage();
    }
    Report report;
    if (micro)
    {
        RunMicro(options, &report);
    }
    if (paths)
    {
        RunPaths(options, &report);
    }
    StringView csv;
    if (line.TryGetOption("csv", csv))
    {
        Vector<char> text;
        const auto append = [&](StringView s)
        {
            for (usize i = 0; i < s.Length(); ++i)
            {
                text.Push(s[i]);
            }
        };
        append(StringView("section,name,size,ms,ns_per_op\n"));
        for (usize i = 0; i < report.Rows().Size(); ++i)
        {
            const Row& r = report.Rows()[i];
            char numbers[96];
            FormatTo(numbers, sizeof(numbers), ",%llu,%.4f,%.2f\n", static_cast<unsigned long long>(r.size), r.ms,
                     r.ns_per);
            append(r.section.View());
            append(StringView(","));
            append(StringView("\""));  // the names hold commas: quoted (they hold no quote)
            append(r.name.View());
            append(StringView("\""));
            append(StringView(numbers));
        }
        io::Path file;
        if (!io::Path::Parse(csv, io::NativePathStyle(), &file) || !io::WriteFile(file, text.Data(), text.Size()).Ok())
        {
            os::PrintError("could not write the CSV file\n");
            return 1;
        }
    }
    return 0;
}
