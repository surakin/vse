#pragma once

#include "types.h"

// The number the macOS debugging backend prints for a thread (Phase 26 Day 307): pthread_threadid_np's /
// THREAD_IDENTIFIER_INFO's 64-bit id cut to its low 31 bits, never 0, the same rule as the job system's
// (jobs/macos/thread_platform.cpp), so a thread reads the same in a crash report and in the job logs. Internal to
// os/macos/debug/.
namespace engine_placeholder::debug::macos
{

inline u64 ThreadNumber(u64 id)
{
    const u64 low = id & 0x7FFFFFFFu;
    return low != 0 ? low : 1;
}

}  // namespace engine_placeholder::debug::macos
