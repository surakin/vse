#pragma once

#include "jobs/counter.h"
#include "jobs/job.h"
#include "jobs/scheduler.h"
#include "types.h"

//! Fan-out and join, built purely from jobs, counters and the scheduler.
//!
//! Job bodies must not capture by reference and the foundation has no
//! `std::function`, so the primitive form of a work function is a plain
//! function pointer plus a `void* user` context. It processes the half-open
//! range `[begin, end)`:
//!
//! ```
//! void Fn(void* user, usize begin, usize end);
//! ```
//!
//! `user` and everything it points to must outlive the join, that is until
//! the blocking call returns, or until the counter or awaiter that joined the
//! job returned by [[jobs::ParallelFor]] has resumed. The typed wrapper
//! stores the callable by pointer and has the same rule.
//!
//! The range `[0, count)` is split into chunks of `chunk_size` (the last may
//! be short); a `chunk_size` of 0 selects [[jobs::kDefaultChunkSize]], a
//! fixed constant, because adaptive chunking is deferred until a real
//! workload shows it is needed. Each chunk is one job. The chunk count is
//! capped at [[jobs::kMaxChunks]] by raising the chunk size; see
//! [[jobs::EffectiveChunkSize]].
namespace engine_placeholder::jobs {

/// Work function of a parallel loop: processes the half-open range `[begin, end)` using the `user` context.
///
/// It runs on any participant, possibly concurrently with other chunks.
using ParallelForFn = void (*)(void* user, usize begin, usize end);

/// Chunk size, in iterations, used when the caller passes 0.
inline constexpr usize kDefaultChunkSize = 1024;

/// Upper bound on chunk jobs per call.
///
/// It is well under one deque's capacity, so a fan-out from inside a job can
/// never exhaust the queues on its own. If `count / chunk_size` would exceed
/// it, the chunk size is raised.
inline constexpr usize kMaxChunks = 2048;

/// Computes the chunk size a parallel loop actually uses.
///
/// A request of 0 means [[jobs::kDefaultChunkSize]]; the result is then
/// raised if needed to respect [[jobs::kMaxChunks]]. Callers that index
/// per-chunk result slots by `begin / chunk` should use this value.
///
/// - Param count: total number of iterations.
/// - Param requested: the chunk size asked for, or 0 for the default.
/// - Returns: the chunk size in iterations.
/// - Thread-safety: thread-safe. A pure function.
inline usize EffectiveChunkSize(usize count, usize requested) {
    usize chunk = requested == 0 ? kDefaultChunkSize : requested;
    if (count > 0) {
        usize min_chunk = count / kMaxChunks + (count % kMaxChunks != 0 ? 1 : 0);
        if (chunk < min_chunk) chunk = min_chunk;
    }
    return chunk;
}

namespace detail {

inline Job ChunkJob(ParallelForFn fn, void* user, usize begin, usize end) {
    fn(user, begin, end);
    co_return;
}

// Submits every chunk, each counted against `counter`. Loops on a chunk
// index so `begin` can never overflow.
inline void SpawnChunks(Counter& counter, usize count, usize chunk, ParallelForFn fn, void* user) {
    const usize chunks = count / chunk + (count % chunk != 0 ? 1 : 0);
    for (usize i = 0; i < chunks; ++i) {
        const usize begin = i * chunk;
        const usize remaining = count - begin;
        const usize end = begin + (remaining < chunk ? remaining : chunk);
        Spawn(counter, ChunkJob(fn, user, begin, end));
    }
}

template <typename F>
void Trampoline(void* user, usize begin, usize end) {
    (*static_cast<F*>(user))(begin, end);
}

} // namespace detail

/// Creates a job that runs a parallel loop and completes when every chunk has finished.
///
/// When run on a participant, the job spawns the chunk jobs and `co_await`s
/// their completion, so it is itself joinable with a [[jobs::Counter]] via
/// [[jobs::Spawn]]. A `count` of 0 completes immediately. Nothing runs until
/// the returned job is submitted.
///
/// - Param count: total number of iterations.
/// - Param chunk_size: iterations per chunk, or 0 for [[jobs::kDefaultChunkSize]]; raised if the chunk count would exceed [[jobs::kMaxChunks]].
/// - Param fn: the work function; called once per chunk.
/// - Param user: context passed to `fn`; it and everything it points to must outlive the join.
/// - Aborts: the job runs on a thread that is not a scheduler participant, or the queues overflow (see [[jobs::Scheduler::Submit]]).
/// - Thread-safety: thread-safe. The returned job is a normal job; chunks run concurrently on participants.
/// - Ownership: `user` is borrowed, not owned; the caller keeps it alive until the join.
inline Job ParallelFor(usize count, usize chunk_size, ParallelForFn fn, void* user) {
    if (count == 0) co_return;
    const usize chunk = EffectiveChunkSize(count, chunk_size);
    Counter counter; // lives in this frame; bound to the running participant's scheduler
    detail::SpawnChunks(counter, count, chunk, fn, user);
    co_await counter;
}

/// Runs a parallel loop to completion from the main thread.
///
/// For callers that are not jobs: it submits the chunk jobs, then helps run
/// work until they have all completed. A `count` of 0 returns immediately.
///
/// - Param scheduler: the scheduler that runs the chunks.
/// - Param count: total number of iterations.
/// - Param chunk_size: iterations per chunk, or 0 for [[jobs::kDefaultChunkSize]]; raised if the chunk count would exceed [[jobs::kMaxChunks]].
/// - Param fn: the work function; called once per chunk.
/// - Param user: context passed to `fn`; it and everything it points to must outlive the call.
/// - Aborts: not called from participant 0 (the main thread) outside any job, or the queues overflow (see [[jobs::Scheduler::HelpUntil(Counter&)]]).
/// - Thread-safety: owner-thread-only. Only participant 0, outside a job.
/// - Ownership: `user` is borrowed, not owned; the call joins before returning.
inline void ParallelForBlocking(Scheduler& scheduler, usize count, usize chunk_size, ParallelForFn fn, void* user) {
    if (count == 0) return;
    const usize chunk = EffectiveChunkSize(count, chunk_size);
    Counter counter(scheduler);
    detail::SpawnChunks(counter, count, chunk, fn, user);
    scheduler.HelpUntil(counter);
}

/// Runs a parallel loop to completion, invoking a typed callable as `callable(begin, end)`.
///
/// Blocking form only: a lazily-started job holding a pointer to a
/// caller-owned callable would dangle too easily (see the parameter-lifetime
/// rule in [[jobs::Job]]). The callable is referenced, not copied, and
/// outlives the call because the call joins.
///
/// - Param scheduler: the scheduler that runs the chunks.
/// - Param count: total number of iterations.
/// - Param chunk_size: iterations per chunk, or 0 for [[jobs::kDefaultChunkSize]].
/// - Param callable: invoked concurrently as `callable(begin, end)` for each chunk; must be safe to call from several threads.
/// - Aborts: same preconditions as the function-pointer overload: participant 0 outside a job, and the queues must not overflow.
/// - Thread-safety: owner-thread-only. Only participant 0, outside a job.
template <typename F>
void ParallelForBlocking(Scheduler& scheduler, usize count, usize chunk_size, F& callable) {
    ParallelForBlocking(scheduler, count, chunk_size, &detail::Trampoline<F>, &callable);
}

} // namespace engine_placeholder::jobs
