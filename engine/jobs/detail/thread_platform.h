#pragma once

#include "types.h"

// The per-platform half of the job system (Phase 13 Day 153): which CPUs the process may use, the OS id of a thread, pinning and naming.
// Internal to engine/jobs; implemented by jobs/<platform>/thread_platform.cpp, so the shared topology and worker pool include no OS header.
namespace engine_placeholder::jobs::detail {

/// Writes the logical CPU ids this process may run on, ascending, up to `capacity`.
/// Linux: the affinity mask. Windows: `group * 64 + bit` over the processor groups the process may use (jobs/windows/thread_platform.cpp).
/// macOS: `0` to `hw.activecpu - 1` (no per-process mask).
/// Returns: the number written; 0 when the query failed.
usize QueryAllowedCpus(u32* out, usize capacity);

/// Returns the calling thread's OS id (Linux: the kernel tid; Windows: GetCurrentThreadId; macOS: pthread_threadid_np's low 31 bits), for logs and tests.
i32 CurrentThreadId();

/// Returns whether the platform can restrict a thread to one CPU at all (Linux and Windows: true; macOS: false, it has no affinity API), so the
/// worker pool does not ask, and warn, where pinning cannot exist.
bool CanPinThreads();

/// Restricts the calling thread to the CPU `cpu` (an id from QueryAllowedCpus). Returns false when the OS refuses.
bool PinCurrentThread(u32 cpu);

/// Gives the calling thread a name debuggers and profilers show (Windows: SetThreadDescription; macOS: pthread_setname_np, at most 63 bytes, cut
/// on a UTF-8 boundary; Linux: not set, as before Phase 13).
void NameCurrentThread(const char* name);

} // namespace engine_placeholder::jobs::detail
