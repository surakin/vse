// Windows implementation of ChildResourceGuard (Phase 13 Day 154): counts the process's handles and looks for a child process still running.
#include "resource_guard.h"

#include "debug/death_test.h"

#include <cstdio>

#include "os/windows/win32.h"
#include "types.h"

#include "unity/windows_enable.h"
#include <tlhelp32.h>
#include "unity/windows_disable.h"

namespace {

unsigned long CountOpenHandles() {
    DWORD count = 0;
    return GetProcessHandleCount(GetCurrentProcess(), &count) ? static_cast<unsigned long>(count) : 0;
}

// True when a process whose parent is this one is still running (an exited child is gone from the snapshot).
bool AnyChildRunning() {
    const HANDLE snapshot = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    if (snapshot == INVALID_HANDLE_VALUE) return false;
    const DWORD self = GetCurrentProcessId();
    PROCESSENTRY32W entry{};
    entry.dwSize = sizeof(entry);
    bool found = false;
    for (BOOL more = Process32FirstW(snapshot, &entry); more && !found; more = Process32NextW(snapshot, &entry)) {
        found = entry.th32ParentProcessID == self && entry.th32ProcessID != self;
    }
    CloseHandle(snapshot);
    return found;
}

// The first child a program starts leaves a few handles open for good (measured on Day 155: 6 after the first CreateProcess, and one more that
// appears a moment later, at a different point each run; none after later children). The guard starts one throw-away child through the
// same path as the tests (RunInChild with a name no case has: the child exits at once), lets it settle, and only then counts, so that cost is
// not taken for a leak.
void WarmUpProcessCreation() {
    static bool done = false;
    if (done) return;
    done = true;
    (void)engine_placeholder::debug::RunInChild("resource-guard-warm-up");
    Sleep(200);
}

unsigned long CountAfterWarmUp() {
    WarmUpProcessCreation();
    return CountOpenHandles();
}

} // namespace

ChildResourceGuard::ChildResourceGuard() : descriptors_before_(CountAfterWarmUp()) {}

// A child that has ended can still be listed, and its last handles still counted, for a moment: Windows tears a process down asynchronously
// (seen on Day 155: a different case failed on each run). Clean waits up to a second for both to settle before it says something leaked.
bool ChildResourceGuard::Clean() const {
    for (int attempt = 0; attempt < 100; ++attempt) {
        if (!AnyChildRunning() && CountOpenHandles() == descriptors_before_) return true;
        Sleep(10);
    }
    std::fprintf(stderr, "ChildResourceGuard: handles before %lu, now %lu, child running %d\n", descriptors_before_, CountOpenHandles(),
                 AnyChildRunning() ? 1 : 0);
    return false;
}
