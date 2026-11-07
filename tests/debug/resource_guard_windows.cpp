// Windows implementation of ChildResourceGuard (Phase 13 Day 154): counts the process's handles and looks for a child process still running.
#include "resource_guard.h"

#include "os/windows/win32.h"

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

} // namespace

ChildResourceGuard::ChildResourceGuard() : descriptors_before_(CountOpenHandles()) {}

bool ChildResourceGuard::Clean() const { return !AnyChildRunning() && CountOpenHandles() == descriptors_before_; }
