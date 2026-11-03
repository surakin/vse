#pragma once

#include "jobs/counter.h"
#include "jobs/job.h"
#include "jobs/scheduler.h"
#include "types.h"

// Phase 2 Day 33: fan-out/join built purely from Job, Counter and the
// scheduler — no new synchronization.
//
// CALLABLE REPRESENTATION. Job bodies must not capture by reference
// (job.h's parameter-lifetime rule) and the foundation has no
// std::function, so the primitive form is a plain function pointer plus
// a `void* user` context:
//
//     void Fn(void* user, usize begin, usize end);   // processes [begin, end)
//
// LIFETIME: `user` and everything it points to must outlive the join
// (i.e. until ParallelForBlocking returns, or until the Counter/awaiter
// that joined the Job returned by ParallelFor has resumed). The typed
// wrappers below store the callable by pointer and have the same rule.
//
// CHUNKING. [0, count) is split into chunks of `chunk_size` (the last
// may be short); chunk_size == 0 selects kDefaultChunkSize, a fixed
// constant — adaptive chunking is deferred until a real workload shows
// it is needed (Phase 2 notes). Each chunk is one job; the chunk count is
// capped at kMaxChunks by raising chunk_size (see EffectiveChunkSize).
namespace engine_placeholder::jobs {

using ParallelForFn = void (*)(void* user, usize begin, usize end);

inline constexpr usize kDefaultChunkSize = 1024;

// Upper bound on chunk jobs per call, well under one deque's capacity, so
// a fan-out from inside a job can never exhaust the queues on its own.
// If count / chunk_size would exceed it, chunk_size is raised.
inline constexpr usize kMaxChunks = 2048;

// The chunk size actually used for (count, requested): 0 = default, then
// raised if needed to respect kMaxChunks. Callers that index per-chunk
// result slots by `begin / chunk` should use this value.
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

// Returns a Job that, when run on a participant, spawns the chunk jobs
// and `co_await`s their completion (so it is itself joinable with a
// Counter via Spawn). count == 0 completes immediately.
inline Job ParallelFor(usize count, usize chunk_size, ParallelForFn fn, void* user) {
    if (count == 0) co_return;
    const usize chunk = EffectiveChunkSize(count, chunk_size);
    Counter counter; // lives in this frame; bound to the running participant's scheduler
    detail::SpawnChunks(counter, count, chunk, fn, user);
    co_await counter;
}

// For callers that are not jobs (the main thread): submits the chunk
// jobs, then helps run work until they have all completed.
inline void ParallelForBlocking(Scheduler& scheduler, usize count, usize chunk_size, ParallelForFn fn, void* user) {
    if (count == 0) return;
    const usize chunk = EffectiveChunkSize(count, chunk_size);
    Counter counter(scheduler);
    detail::SpawnChunks(counter, count, chunk, fn, user);
    scheduler.HelpUntil(counter);
}

// Typed convenience: `callable(begin, end)`. Blocking form only — a
// lazily-started Job holding a pointer to a caller-owned callable would
// dangle too easily (see job.h's parameter-lifetime rule). The callable
// is referenced, not copied; it outlives the call because the call joins.
template <typename F>
void ParallelForBlocking(Scheduler& scheduler, usize count, usize chunk_size, F& callable) {
    ParallelForBlocking(scheduler, count, chunk_size, &detail::Trampoline<F>, &callable);
}

} // namespace engine_placeholder::jobs
