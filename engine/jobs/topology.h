#pragma once

#include "types.h"

// Phase 2 Day 29: which CPUs the process may use and how many job-system
// participants to run (design decision D1). Linux-specific calls are
// confined to topology.cpp / worker_pool.cpp; Windows/macOS/console
// ports provide their own behind the same functions.
namespace engine_placeholder::jobs {

inline constexpr usize kMaxParticipants = 256;

// Fills `out` (capacity `capacity`) with the logical CPU ids this process
// is allowed to run on, ascending, per sched_getaffinity (NOT
// std::thread::hardware_concurrency, which ignores the mask). Returns the
// number written. If the query fails, returns 1 and reports CPU 0.
usize GetAllowedCpus(u32* out, usize capacity);

// Pure parse/clamp of an ENGINE_JOB_THREADS value: an integer in
// [1, kMaxParticipants] is used as-is; larger values clamp to
// kMaxParticipants; null/empty/garbage/0/negative return `fallback`.
// `*warned` (if non-null) is set when a non-null value was rejected or
// clamped.
usize ParseParticipantOverride(const char* text, usize fallback, bool* warned);

// Participant count to use by default: ENGINE_JOB_THREADS if valid, else
// the number of allowed logical CPUs (at least 1). Logs a warning for a
// bad override.
usize DefaultParticipantCount();

} // namespace engine_placeholder::jobs
