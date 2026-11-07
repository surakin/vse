#pragma once

// The process's resident memory on Windows (its working set), for tests that check memory is not committed early (Phase 13 Day 153). Kept
// in a Windows directory because it needs <psapi.h> (platform_neutral_check rule 4); link psapi.
#include "os/windows/win32.h"

#include "unity/windows_enable.h"
#include <psapi.h>
#include "unity/windows_disable.h"

namespace test_support {

/// The working set in KiB, or 0 when it cannot be read.
inline long ResidentKb() {
    PROCESS_MEMORY_COUNTERS counters{};
    counters.cb = sizeof(counters);
    if (!GetProcessMemoryInfo(GetCurrentProcess(), &counters, sizeof(counters))) return 0;
    return static_cast<long>(counters.WorkingSetSize / 1024);
}

} // namespace test_support
