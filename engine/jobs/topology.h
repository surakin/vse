#pragma once

#include "types.h"

//! Which CPUs the process may use and how many job-system participants to run.
//!
//! The platform-specific queries sit behind these functions so that ports
//! provide their own implementation without touching callers.
namespace engine_placeholder::jobs {

/// Upper bound on job-system participants (main thread plus workers).
inline constexpr usize kMaxParticipants = 256;

/// Lists the logical CPU ids this process is allowed to run on, in ascending order.
///
/// On Linux it honours the affinity mask (`sched_getaffinity`), not
/// `std::thread::hardware_concurrency`, which ignores the mask. On Windows the
/// ids are `group * 64 + bit` over the processor groups the process may use,
/// restricted by the process affinity mask when it has one group.
///
/// - Param out: receives up to `capacity` CPU ids.
/// - Param capacity: number of entries available in `out`.
/// - Returns: the number of ids written. If the query fails (or yields no CPU) it reports CPU 0 and returns 1.
/// - Ownership: `out` is written, not retained.
/// - Thread-safety: thread-safe.
usize GetAllowedCpus(u32* out, usize capacity);

/// Parses an `ENGINE_JOB_THREADS` value into a participant count; has no side effects.
///
/// An integer in `[1, kMaxParticipants]` is used as is; larger values clamp
/// to [[jobs::kMaxParticipants]]; null, empty, garbage, zero and negative
/// values yield `fallback`.
///
/// - Param text: the raw text, or null if the variable is unset.
/// - Param fallback: the result for an unset or rejected value.
/// - Param warned: if non-null, set to `true` when a non-null value was rejected or clamped, else `false`.
/// - Returns: the participant count to use.
/// - Ownership: `warned` is written, not retained.
/// - Thread-safety: thread-safe.
usize ParseParticipantOverride(const char* text, usize fallback, bool* warned);

/// Chooses the default participant count.
///
/// It is `ENGINE_JOB_THREADS` if valid, else the number of allowed logical
/// CPUs (at least 1). A bad override is logged as a warning.
///
/// - Returns: the participant count, in `[1, kMaxParticipants]`.
/// - Thread-safety: thread-safe.
usize DefaultParticipantCount();

} // namespace engine_placeholder::jobs
