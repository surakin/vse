#pragma once

// The ECS benchmark (Phase 17 Day 235): the workload of docs/superpowers/specs/2026-11-10-ecs-design.md section 6,
// implemented twice, once on the ECS and once as the naive object-oriented baseline (heap-allocated game objects with
// virtual Update, optional components behind owning pointers, visited in shuffled order). Both do the same work and
// return a checksum so that neither can win by doing less. The program (main.cpp) prints the tables; tests/ecs checks
// the checksums and the ratio.

#include "containers/vector.h"
#include "types.h"

namespace ecs_bench
{

using namespace engine_placeholder;

// Which implementation to run.
enum class Impl
{
    Ecs,
    Oop
};

// What to run.
struct Options
{
    u32 entities = 100000;
    u32 frames = 200;        // timed frames
    u32 warmup = 20;         // discarded frames
    usize participants = 1;  // job-system participants for the parallel scenarios (the ECS side only)
    u32 seed = 1;
};

// Per-frame time of the timed frames.
struct Timing
{
    double median_ms = 0.0;
    double p95_ms = 0.0;
};

// What a scenario returns.
struct Result
{
    Timing time;
    // Order-independent sum of the final state; equal (within ChecksumsMatch) for the two implementations.
    double checksum = 0.0;
    // A scenario-specific count: entities alive at the end, or rows the reader visited.
    u64 count = 0;
    // False when the ECS world failed `Validate` or a refusal was counted.
    bool valid = true;
};

// The four-step workload on all entities, one thread: integrate position by velocity; a damped spring; transform to
// matrix; a conditional on a tag.
Result RunIteration(Impl impl, const Options& options);
// A heavy per-entity computation over the positions, split over `options.participants` for the ECS.
Result RunHeavy(Impl impl, const Options& options);
// Create and destroy 10 percent of the entities a frame, and add or remove a tag on 10 percent.
Result RunChurn(Impl impl, const Options& options);
// A system that touches the 1 percent of entities that carry a tag.
Result RunSparse(Impl impl, const Options& options);
// 1 percent of the positions written a frame, and a reader that visits what changed; clustered writes hit the first
// rows, otherwise they are random.
Result RunChanged(Impl impl, const Options& options, bool clustered);
// A hierarchy of chains of five at `options.entities` entities, every root moved each frame.
Result RunHierarchy(Impl impl, const Options& options);

// Memory and throughput of creating and destroying entities with the workload's components.
struct Lifecycle
{
    double bytes_per_entity = 0.0;  // chunk bytes (ECS) or an estimate from sizeof and the owning vectors (baseline)
    double create_ns = 0.0;         // per entity
    double destroy_ns = 0.0;        // per entity
};
Lifecycle RunLifecycle(Impl impl, const Options& options);

// The cost of change stamping: the same pass over the positions through a read-only term and through a write term
// (which stamps each chunk). `read` and `write` are the two medians; the checksum is the same for both passes.
struct Stamping
{
    Timing read;
    Timing write;
};
Stamping RunStamping(const Options& options);

// Whether two checksums agree: relative difference below 1e-4 (the two sides add floats in different orders).
bool ChecksumsMatch(double a, double b);

// Median and 95th percentile of `samples_ms` (sorts the vector); zeros for an empty one.
Timing Summarize(Vector<double>* samples_ms);

// One row of the result table written by --csv.
struct Row
{
    char scenario[48] = {};
    char impl[8] = {};
    u64 entities = 0;
    u64 participants = 0;
    double median_ms = 0.0;
    double p95_ms = 0.0;
    double checksum = 0.0;
};

// Fills a row. `scenario` and `impl` are copied (truncated to fit).
Row MakeRow(const char* scenario, Impl impl, const Options& options, const Result& result);
// The CSV text of `rows`, a header line first; an empty vector if formatting failed.
Vector<char> FormatCsv(const Vector<Row>& rows);
// Parses text made by FormatCsv; false if a line is malformed.
bool ParseCsv(const char* text, usize size, Vector<Row>* rows);

}  // namespace ecs_bench
