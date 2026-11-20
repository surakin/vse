#include "hang_watchdog_windows.h"
#include "log/output.h"

#include "debug/stack_trace.h"

#include "os/console.h"
#include "cstr/number.h"
#include "cstr/cstr.h"
#include "sync/thread.h"

#include "os/windows/win32.h"

#include "unity/windows_enable.h"
#include <tlhelp32.h>
#include "unity/windows_disable.h"
#include "cstr/format.h"
#include "cstr/mem_ops.h"

namespace hang_watchdog
{

namespace
{

using namespace vse;
using namespace vse::debug;

void WalkFrom(CONTEXT context, StackTrace& trace)
{
    __try
    {
        while (trace.count < kMaxStackFrames && context.Rip != 0)
        {
            const DWORD64 pc = context.Rip;
            trace.frames[trace.count++] = pc;
            DWORD64 image_base = 0;
            PRUNTIME_FUNCTION function = RtlLookupFunctionEntry(pc, &image_base, nullptr);
            if (function == nullptr)
            {
                context.Rip = *reinterpret_cast<const DWORD64*>(context.Rsp);
                context.Rsp += 8;
            }
            else
            {
                PVOID handler_data = nullptr;
                DWORD64 establisher = 0;
                RtlVirtualUnwind(UNW_FLAG_NHANDLER, image_base, pc, function, &context, &handler_data, &establisher,
                                 nullptr);
            }
        }
    }
    __except (EXCEPTION_EXECUTE_HANDLER)
    {
    }
}

void WriteError(const char* text, int length)
{
    if (length > 0)
    {
        WriteFile(GetStdHandle(STD_ERROR_HANDLE), text, static_cast<DWORD>(length), nullptr, nullptr);
    }
}

void DumpThreads()
{
    const DWORD self = GetCurrentThreadId();
    const DWORD process = GetCurrentProcessId();
    const HANDLE snapshot = CreateToolhelp32Snapshot(TH32CS_SNAPTHREAD, 0);
    if (snapshot == INVALID_HANDLE_VALUE)
    {
        return;
    }
    THREADENTRY32 entry{};
    entry.dwSize = sizeof(entry);
    char line[96];
    for (BOOL more = Thread32First(snapshot, &entry); more; more = Thread32Next(snapshot, &entry))
    {
        if (entry.th32OwnerProcessID != process || entry.th32ThreadID == self)
        {
            continue;
        }
        const HANDLE thread = OpenThread(THREAD_SUSPEND_RESUME | THREAD_GET_CONTEXT, FALSE, entry.th32ThreadID);
        if (thread == nullptr)
        {
            continue;
        }
        StackTrace trace;
        if (SuspendThread(thread) != static_cast<DWORD>(-1))
        {
            CONTEXT context{};
            context.ContextFlags = CONTEXT_FULL;
            if (GetThreadContext(thread, &context))
            {
                WalkFrom(context, trace);
            }
            // Left suspended: the process ends below.
        }
        CloseHandle(thread);
        // Raw addresses only, written with WriteFile: naming them takes locks the suspended thread may hold. The parent
        // names them (NameFrames): it runs the same image at the same base, since Windows relocates an image once per
        // boot.
        WriteError(line, FormatTo(line, sizeof(line), "HANG-WATCHDOG thread %lu:\n",
                                  static_cast<unsigned long>(entry.th32ThreadID)));
        for (u32 i = 0; i < trace.count; ++i)
        {
            WriteError(line, FormatTo(line, sizeof(line), "HANG-FRAME 0x%016llx\n",
                                      static_cast<unsigned long long>(trace.frames[i])));
        }
    }
    CloseHandle(snapshot);
}

}  // namespace

void Start(unsigned seconds)
{
    Thread(
        [seconds]
        {
            Sleep(seconds * 1000);
            DumpThreads();
            TerminateProcess(GetCurrentProcess(), 0xDEAD0002u);
        })
        .Detach();
}

void NameFrames(const char* text, unsigned long long length)
{
    static char out[8192];
    const char* end = text + length;
    for (const char* p = text; p + 13 < end; ++p)
    {
        if (CStrCompareN(p, "HANG-WATCHDOG", 13) == 0)
        {
            const char* eol = static_cast<const char*>(MemFind(p, '\n', static_cast<usize>(end - p)));
            out::PrintError("%.*s\n", static_cast<int>((eol != nullptr ? eol : end) - p), p);
            continue;
        }
        if (CStrCompareN(p, "HANG-FRAME 0x", 13) != 0)
        {
            continue;
        }
        StackTrace one;
        one.frames[0] = CStrToULongLong(p + 13, nullptr, 16);
        one.count = 1;
        FormatStackTrace(one, out, sizeof(out));
        out::PrintErrorText(out);
    }
}

}  // namespace hang_watchdog
