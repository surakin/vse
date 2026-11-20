// container_bench: the container audit's benchmark (maintenance series, Day 1; plans/maintenance/container-audit.md).
// Shared helpers: a clock, a random source, the result table. Nothing is asserted: the numbers are recorded in the
// series' day files.
#pragma once

#include "containers/vector.h"
#include "os/os.h"
#include "string/string.h"
#include "types.h"

namespace container_bench
{

using namespace vse;

/// A small deterministic random source (xorshift64*).
struct Rng
{
    u64 state = 0x9E3779B97F4A7C15ull;

    u64 Next()
    {
        state ^= state >> 12;
        state ^= state << 25;
        state ^= state >> 27;
        return state * 2685821657736338717ull;
    }
};

/// One measurement: the section, the case, its size and the median time of one run.
struct Row
{
    String<HeapAllocator> section{HeapAllocator::Default()};
    String<HeapAllocator> name{HeapAllocator::Default()};
    u64 size = 0;
    double ms = 0.0;      // median milliseconds of one run of the case
    double ns_per = 0.0;  // ms divided by the operation count, in nanoseconds
};

/// The options every section reads.
struct Options
{
    u32 runs = 5;        // the runs of each case; the median is reported
    bool quick = false;  // smaller sizes, for a smoke run
};

/// Collects rows and prints one line per row.
class Report
{
public:
    void Add(const char* section, const char* name, u64 size, double ms, u64 operations);
    const Vector<Row>& Rows() const { return rows_; }

private:
    Vector<Row> rows_;
};

/// Runs `body` `runs` times and returns the median milliseconds; `setup` runs before each run, untimed.
template <typename Setup, typename Body> double MedianMs(u32 runs, Setup setup, Body body)
{
    Vector<double> times;
    for (u32 r = 0; r < runs; ++r)
    {
        setup();
        const u64 t0 = os::MonotonicTimeNanos();
        body();
        times.Push(static_cast<double>(os::MonotonicTimeNanos() - t0) / 1.0e6);
    }
    for (usize i = 1; i < times.Size(); ++i)  // a handful of values: insertion sort
    {
        for (usize j = i; j > 0 && times[j] < times[j - 1]; --j)
        {
            const double t = times[j];
            times[j] = times[j - 1];
            times[j - 1] = t;
        }
    }
    return times.Size() == 0 ? 0.0 : times[times.Size() / 2];
}

/// Keeps a value alive so the optimizer cannot drop the work that made it.
void Keep(u64 value);

void RunMicro(const Options& options, Report* report);
void RunPaths(const Options& options, Report* report);

}  // namespace container_bench
