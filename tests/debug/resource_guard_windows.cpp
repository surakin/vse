// Windows implementation of ChildResourceGuard (Phase 13 Day 154): counts the process's handles and looks for a child
// process still running.
#include "resource_guard.h"

#include "debug/death_test.h"

#include "os/console.h"

#include "os/windows/win32.h"
#include "types.h"

#include "unity/windows_enable.h"
#include <tlhelp32.h>
#include "unity/windows_disable.h"

namespace
{

unsigned long CountOpenHandles()
{
    DWORD count = 0;
    return GetProcessHandleCount(GetCurrentProcess(), &count) ? static_cast<unsigned long>(count) : 0;
}

// True when a child of this process is still running. A snapshot entry's parent id alone is not enough: Windows reuses
// process ids, and a process whose parent ended long ago keeps that parent's id, so an unrelated older process (seen on
// Day 157: `vctip.exe`, `csrss.exe`, `winlogon.exe`) can name this process as its parent. A child is a process listed
// with this parent that was created after this process and has not exited (an ended child can stay listed while another
// program holds a handle to it).
bool AnyChildRunning(bool verbose = false)
{
    const HANDLE snapshot = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    if (snapshot == INVALID_HANDLE_VALUE)
    {
        return false;
    }
    const DWORD self = GetCurrentProcessId();
    FILETIME self_created{}, unused_a{}, unused_b{}, unused_c{};
    GetProcessTimes(GetCurrentProcess(), &self_created, &unused_a, &unused_b, &unused_c);
    PROCESSENTRY32W entry{};
    entry.dwSize = sizeof(entry);
    bool found = false;
    for (BOOL more = Process32FirstW(snapshot, &entry); more; more = Process32NextW(snapshot, &entry))
    {
        if (entry.th32ParentProcessID != self || entry.th32ProcessID == self)
        {
            continue;
        }
        // A process this one started can always be opened by it; one that cannot be opened belongs to someone else.
        const HANDLE process = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, entry.th32ProcessID);
        if (process == nullptr)
        {
            continue;
        }
        DWORD code = 0;
        FILETIME created{};
        const bool times = GetProcessTimes(process, &created, &unused_a, &unused_b, &unused_c) != 0;
        const bool running = GetExitCodeProcess(process, &code) != 0 && code == STILL_ACTIVE;
        CloseHandle(process);
        const bool ours = times && CompareFileTime(&created, &self_created) >= 0;
        if (verbose)
        {
            vse::os::PrintError("ChildResourceGuard: listed child %lu '%ls' running %d created after this process %d\n",
                                entry.th32ProcessID, entry.szExeFile, running ? 1 : 0, ours ? 1 : 0);
        }
        if (ours && running)
        {
            found = true;
        }
    }
    CloseHandle(snapshot);
    return found;
}

// The first child a program starts leaves a few handles open for good (measured on Day 155: 6 after the first
// CreateProcess, and one more that appears a moment later, at a different point each run; none after later children).
// The guard starts one throw-away child through the same path as the tests (RunInChild with a name no case has: the
// child exits at once), lets it settle, and only then counts, so that cost is not taken for a leak.
void WarmUpProcessCreation()
{
    static bool done = false;
    if (done)
    {
        return;
    }
    done = true;
    (void)vse::debug::RunInChild("resource-guard-warm-up");
    Sleep(200);
}

unsigned long CountAfterWarmUp()
{
    WarmUpProcessCreation();
    return CountOpenHandles();
}

}  // namespace

ChildResourceGuard::ChildResourceGuard() : descriptors_before_(CountAfterWarmUp()) {}

// A child that has ended can still be listed, and its last handles still counted, for a moment: Windows tears a process
// down asynchronously (seen on Day 155: a different case failed on each run). Clean waits up to a second for both to
// settle before it says something leaked.
bool ChildResourceGuard::Clean() const
{
    for (int attempt = 0; attempt < 100; ++attempt)
    {
        if (!AnyChildRunning() && CountOpenHandles() == descriptors_before_)
        {
            return true;
        }
        Sleep(10);
    }
    vse::os::PrintError("ChildResourceGuard: handles before %lu, now %lu, child running %d\n", descriptors_before_,
                        CountOpenHandles(), AnyChildRunning(true) ? 1 : 0);
    return false;
}
