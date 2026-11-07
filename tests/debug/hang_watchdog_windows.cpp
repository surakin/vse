#include "hang_watchdog_windows.h"

#include "debug/stack_trace.h"

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <thread>

#include "os/windows/win32.h"

#include "unity/windows_enable.h"
#include <tlhelp32.h>
#include "unity/windows_disable.h"

namespace hang_watchdog {

namespace {

using namespace engine_placeholder;
using namespace engine_placeholder::debug;

void WalkFrom(CONTEXT context, StackTrace& trace) {
    __try {
        while (trace.count < kMaxStackFrames && context.Rip != 0) {
            const DWORD64 pc = context.Rip;
            trace.frames[trace.count++] = pc;
            DWORD64 image_base = 0;
            PRUNTIME_FUNCTION function = RtlLookupFunctionEntry(pc, &image_base, nullptr);
            if (function == nullptr) {
                context.Rip = *reinterpret_cast<const DWORD64*>(context.Rsp);
                context.Rsp += 8;
            } else {
                PVOID handler_data = nullptr;
                DWORD64 establisher = 0;
                RtlVirtualUnwind(UNW_FLAG_NHANDLER, image_base, pc, function, &context, &handler_data, &establisher, nullptr);
            }
        }
    } __except (EXCEPTION_EXECUTE_HANDLER) {
    }
}

void WriteError(const char* text, int length) {
    if (length > 0) WriteFile(GetStdHandle(STD_ERROR_HANDLE), text, static_cast<DWORD>(length), nullptr, nullptr);
}

void DumpThreads() {
    const DWORD self = GetCurrentThreadId();
    const DWORD process = GetCurrentProcessId();
    const HANDLE snapshot = CreateToolhelp32Snapshot(TH32CS_SNAPTHREAD, 0);
    if (snapshot == INVALID_HANDLE_VALUE) return;
    THREADENTRY32 entry{};
    entry.dwSize = sizeof(entry);
    char line[96];
    for (BOOL more = Thread32First(snapshot, &entry); more; more = Thread32Next(snapshot, &entry)) {
        if (entry.th32OwnerProcessID != process || entry.th32ThreadID == self) continue;
        const HANDLE thread = OpenThread(THREAD_SUSPEND_RESUME | THREAD_GET_CONTEXT, FALSE, entry.th32ThreadID);
        if (thread == nullptr) continue;
        StackTrace trace;
        if (SuspendThread(thread) != static_cast<DWORD>(-1)) {
            CONTEXT context{};
            context.ContextFlags = CONTEXT_FULL;
            if (GetThreadContext(thread, &context)) WalkFrom(context, trace);
            // Left suspended: the process ends below.
        }
        CloseHandle(thread);
        // Raw addresses only, written with WriteFile: naming them takes locks the suspended thread may hold. The parent names them
        // (NameFrames): it runs the same image at the same base, since Windows relocates an image once per boot.
        WriteError(line, std::snprintf(line, sizeof(line), "HANG-WATCHDOG thread %lu:\n", static_cast<unsigned long>(entry.th32ThreadID)));
        for (u32 i = 0; i < trace.count; ++i) {
            WriteError(line, std::snprintf(line, sizeof(line), "HANG-FRAME 0x%016llx\n", static_cast<unsigned long long>(trace.frames[i])));
        }
    }
    CloseHandle(snapshot);
}

} // namespace

void Start(unsigned seconds) {
    std::thread([seconds] {
        Sleep(seconds * 1000);
        DumpThreads();
        TerminateProcess(GetCurrentProcess(), 0xDEAD0002u);
    }).detach();
}

void NameFrames(const char* text, unsigned long long length) {
    static char out[8192];
    const char* end = text + length;
    for (const char* p = text; p + 13 < end; ++p) {
        if (std::strncmp(p, "HANG-WATCHDOG", 13) == 0) {
            const char* eol = static_cast<const char*>(std::memchr(p, '\n', static_cast<usize>(end - p)));
            std::fprintf(stderr, "%.*s\n", static_cast<int>((eol != nullptr ? eol : end) - p), p);
            continue;
        }
        if (std::strncmp(p, "HANG-FRAME 0x", 13) != 0) continue;
        StackTrace one;
        one.frames[0] = std::strtoull(p + 13, nullptr, 16);
        one.count = 1;
        FormatStackTrace(one, out, sizeof(out));
        std::fputs(out, stderr);
    }
}

} // namespace hang_watchdog
