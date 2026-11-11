// The crash report hook on Windows (Phase 13 Day 155; os/windows/debug/BACKEND.md). sentry-native's Breakpad backend
// handles the fault: its unhandled-exception filter hands the exception to Breakpad's own handler thread, which writes
// the minidump and then calls the library's `on_crash` with the fault's EXCEPTION_POINTERS (the faulting thread waits
// meanwhile). The neutral half (crash_handler_sentry.cpp) passes them here, and this file turns them into the
// CrashEvent the report needs. No engine filter handles an exception: the crash gate below only keeps a second crashing
// thread from ending the process while the first is being dumped.
//
// Two things the EXCEPTION_POINTERS do not say, and how they are found:
//   - which thread faulted, and its name: an observer, a vectored handler that only notes "this exception record was
//   raised on this thread,
//     named so" in a ring and always lets the exception go on, answers for every real exception; abort() raises none
//     (sentry builds the record on the aborting thread), so the thread whose registered stack holds the context's stack
//     pointer answers then; else the thread is unknown, and the report says so.
//   - the stack: walked from the faulting thread's CONTEXT with RtlVirtualUnwind (the x64 unwind tables), no DbgHelp,
//   no allocation, guarded
//     by __try so a corrupted stack ends the walk instead of the report.
#include "debug/crash_event_platform.h"
#include "debug/crash_handler.h"
#include "debug/thread_info.h"
#include "os/os.h"
#include "os/windows/utf16.h"
#include "os/windows/win32.h"

#include "sync/atomic.h"
#include "build_defs.h"

namespace engine_placeholder::debug
{

namespace
{

Atomic<detail::CrashEventFn> g_crash_event_fn{nullptr};
Atomic<int> g_reports_in_progress{0};
PVOID g_observer = nullptr;  // the vectored handler, added by InstallCrashEventHook (under the facade's install lock)
// abort()'s flags before InstallCrashEventHook cleared them, restored by RemoveCrashEventHook (both under the facade's
// install lock).
constexpr unsigned int kAbortFlags = _WRITE_ABORT_MSG | _CALL_REPORTFAULT;
unsigned int g_abort_flags_before = kAbortFlags;
bool g_abort_flags_saved = false;

// One exception the observer saw: the record's address, the thread it was raised on, and that thread's crash name at
// the time (the report is written on another thread, which cannot read the faulting thread's thread_local name).
struct SeenException
{
    Atomic<const void*> record{nullptr};  // null while the slot is being written
    Atomic<u32> thread{0};
    char name[kMaxThreadNameBytes] = {};
};
constexpr u32 kSeenExceptions = 128;  // room for the exceptions other threads raise between the fault and the report
SeenException g_seen[kSeenExceptions];
Atomic<u32> g_seen_next{0};

struct ThreadStack
{
    Atomic<u32> thread{0};
    Atomic<u64> low{0};
    Atomic<u64> high{0};
    Atomic<const char*> name{
        nullptr};  // the thread's CrashThreadName buffer (thread_local: valid while the thread runs)
};
constexpr u32 kRegisteredThreads = 256;
ThreadStack g_threads[kRegisteredThreads];
Atomic<u32> g_thread_count{0};

// SIGNAL-SAFE-BEGIN
// The observer: runs first for every exception on the thread that raised it (also on an overflowed stack, within the
// stack guarantee). It records and passes the exception on; it never handles one.
LONG CALLBACK ObserveException(EXCEPTION_POINTERS* info)
{
    SeenException& seen = g_seen[g_seen_next.FetchAdd(1, MemoryOrder::Relaxed) % kSeenExceptions];
    seen.record.Store(nullptr, MemoryOrder::Relaxed);
    AtomicThreadFence(MemoryOrder::Release);  // a reader that still sees the old record sees it before these writes
    seen.thread.Store(GetCurrentThreadId(), MemoryOrder::Relaxed);
    const char* name = CrashThreadName();
    usize n = 0;
    for (; n + 1 < kMaxThreadNameBytes && name[n] != '\0'; ++n)
    {
        seen.name[n] = name[n];
    }
    seen.name[n] = '\0';
    seen.record.Store(info->ExceptionRecord, MemoryOrder::Release);
    return EXCEPTION_CONTINUE_SEARCH;
}

// Copies a registered thread's name; the buffer is that thread's, so a thread that has ended (its slot matched by
// mistake) faults the copy, which then gives an empty name instead of ending the report.
void CopyNameGuarded(const char* from, char* to)
{
    to[0] = '\0';
    if (from == nullptr)
    {
        return;
    }
    __try
    {
        usize n = 0;
        for (; n + 1 < kMaxThreadNameBytes && from[n] != '\0'; ++n)
        {
            to[n] = from[n];
        }
        to[n] = '\0';
    }
    __except (EXCEPTION_EXECUTE_HANDLER)
    {
        to[0] = '\0';
    }
}

u64 StackPointerOf(const CONTEXT& context)
{
#if ENGINE_ARCH_X64
    return context.Rsp;
#elif ENGINE_ARCH_ARM64
    return context.Sp;
#else
    return context.Esp;
#endif
}

// The faulting thread's id (0 when unknown) and its name (into `name`, empty when unknown). Both tables are searched
// newest first: an exception record's address and a stack range can be reused by a later exception or thread, and the
// newest entry is the live one.
u32 FaultingThread(const EXCEPTION_POINTERS& info, char* name)
{
    name[0] = '\0';
    const u32 next = g_seen_next.Load(MemoryOrder::Acquire);
    for (u32 k = 1; k <= kSeenExceptions; ++k)
    {
        SeenException& seen = g_seen[(next - k) % kSeenExceptions];
        if (seen.record.Load(MemoryOrder::Acquire) != info.ExceptionRecord)
        {
            continue;
        }
        const u32 thread = seen.thread.Load(MemoryOrder::Relaxed);
        for (usize n = 0; n < kMaxThreadNameBytes; ++n)
        {
            name[n] = seen.name[n];
        }
        name[kMaxThreadNameBytes - 1] = '\0';
        AtomicThreadFence(MemoryOrder::Acquire);
        if (seen.record.Load(MemoryOrder::Relaxed) == info.ExceptionRecord)
        {
            return thread;  // not rewritten while it was read
        }
        name[0] = '\0';
    }
    const u64 sp = StackPointerOf(*info.ContextRecord);
    const u32 count = g_thread_count.Load(MemoryOrder::Acquire);
    for (u32 k = count < kRegisteredThreads ? count : kRegisteredThreads; k > 0; --k)
    {
        const ThreadStack& entry = g_threads[k - 1];
        const u64 low = entry.low.Load(MemoryOrder::Relaxed);
        const u64 high = entry.high.Load(MemoryOrder::Relaxed);
        if (sp >= low && sp < high)
        {
            CopyNameGuarded(entry.name.Load(MemoryOrder::Relaxed), name);
            return entry.thread.Load(MemoryOrder::Relaxed);
        }
    }
    return 0;
}

CrashReason ReasonOf(DWORD code)
{
    switch (code)
    {
    case EXCEPTION_ACCESS_VIOLATION: return CrashReason::AccessViolation;
    case EXCEPTION_IN_PAGE_ERROR: return CrashReason::BusError;
    case EXCEPTION_ILLEGAL_INSTRUCTION:
    case EXCEPTION_PRIV_INSTRUCTION: return CrashReason::IllegalInstruction;
    case EXCEPTION_INT_DIVIDE_BY_ZERO:
    case EXCEPTION_INT_OVERFLOW:
    case EXCEPTION_FLT_DIVIDE_BY_ZERO:
    case EXCEPTION_FLT_INVALID_OPERATION:
    case EXCEPTION_FLT_OVERFLOW:
    case EXCEPTION_FLT_UNDERFLOW: return CrashReason::ArithmeticFault;
    case EXCEPTION_STACK_OVERFLOW: return CrashReason::StackOverflow;
    case 0x40000015u: return CrashReason::Abort;  // STATUS_FATAL_APP_EXIT: sentry's record for abort()
    default: return CrashReason::None;  // a breakpoint or another code: the report prints the reason as unknown
    }
}

// Walks the faulting thread's stack from its CONTEXT; stops at the end of the stack, at the capacity, or when a frame
// cannot be read.
void WalkStack(CONTEXT context, StackTrace& trace)
{
#if ENGINE_ARCH_X64
    __try
    {
        while (trace.count < kMaxStackFrames)
        {
            const DWORD64 pc = context.Rip;
            if (pc == 0 && trace.count > 0)
            {
                return;  // the end of the stack (a first frame at 0 is a call through a null pointer: kept below)
            }
            trace.frames[trace.count++] = pc;
            DWORD64 image_base = 0;
            PRUNTIME_FUNCTION function = RtlLookupFunctionEntry(pc, &image_base, nullptr);
            if (function == nullptr)
            {
                // A leaf function has no unwind data, nor has address 0 after a call through a null pointer: the return
                // address is on top of the stack.
                context.Rip = *reinterpret_cast<const DWORD64*>(context.Rsp);
                context.Rsp += 8;
            }
            else
            {
                PVOID handler_data = nullptr;
                DWORD64 establisher_frame = 0;
                RtlVirtualUnwind(UNW_FLAG_NHANDLER, image_base, pc, function, &context, &handler_data,
                                 &establisher_frame, nullptr);
            }
        }
        trace.truncated = context.Rip != 0;
    }
    __except (EXCEPTION_EXECUTE_HANDLER)
    {
        // A frame pointed into memory that cannot be read: keep what was walked.
    }
#elif ENGINE_ARCH_ARM64
    trace.frames[trace.count++] = context.Pc;  // the faulting address only, until the arm64 port walks the stack
#else
    trace.frames[trace.count++] = context.Eip;
#endif
}
// Runs the report; a fault inside it (a state provider that faults) ends the report there instead of reaching
// Breakpad's filter again on its own handler thread. The dump was written before `on_crash` ran, so only the rest of
// the report is lost.
void RunReportGuarded(detail::CrashEventFn fn, const detail::CrashEvent* event)
{
    __try
    {
        fn(*event);
    }
    __except (EXCEPTION_EXECUTE_HANDLER)
    {
        static const char kText[] = "double fault: the crash report itself faulted; the dump was already written\n";
        os::Write(2, kText, sizeof(kText) - 1);
    }
}

// The gate: one crash at a time. Breakpad's filter, while it handles a crash, sets the process's top-level filter back
// to the one that was there before it (and restores its own afterwards), so a second thread faulting meanwhile would
// get the system's default handling and end the process before the dump is written (seen on Day 155: about one run in
// three of two threads faulting together left no dump). The gate is installed both before the library (so it is the
// filter Breakpad restores while it works) and after it (so every crash passes it first); it handles nothing: the first
// crash goes to the library, a later one waits for the process to end, and a crash the library hands back goes on to
// the system.
Atomic<bool> g_gate_active{false};
Atomic<LPTOP_LEVEL_EXCEPTION_FILTER> g_filter_before_gate{
    nullptr};                                                    // what was installed before the gate (normally none)
Atomic<LPTOP_LEVEL_EXCEPTION_FILTER> g_library_filter{nullptr};  // Breakpad's, found when the gate goes on top of it
Atomic<u32> g_crashing_thread{0};
constexpr DWORD kGateWaitMs = 60 * 1000;  // the first crash normally ends the process long before this

LONG PassOn(LPTOP_LEVEL_EXCEPTION_FILTER filter, EXCEPTION_POINTERS* info)
{
    return filter != nullptr ? filter(info) : EXCEPTION_CONTINUE_SEARCH;
}

LONG WINAPI CrashGate(EXCEPTION_POINTERS* info)
{
    if (!g_gate_active.Load(MemoryOrder::Acquire))
    {
        return PassOn(g_filter_before_gate.Load(MemoryOrder::Acquire), info);
    }
    const u32 self = GetCurrentThreadId();
    u32 first = 0;
    if (g_crashing_thread.CompareExchangeStrong(first, self, MemoryOrder::AcqRel))
    {
        const LONG action = PassOn(g_library_filter.Load(MemoryOrder::Acquire),
                                   info);  // the first crash: the library writes the dump
        if (action == EXCEPTION_CONTINUE_EXECUTION)
        {
            g_crashing_thread.Store(0, MemoryOrder::Release);  // the program goes on: not a crash
        }
        return action;
    }
    if (first == self)
    {
        // The library gave this thread's crash back (it wrote no dump) and called the filter it replaced, the gate: the
        // system's turn.
        return PassOn(g_filter_before_gate.Load(MemoryOrder::Acquire), info);
    }
    Sleep(kGateWaitMs);  // another thread's crash is being handled; the process ends when it is done
    return EXCEPTION_CONTINUE_SEARCH;
}
// SIGNAL-SAFE-END

}  // namespace

namespace detail
{

void PrepareCrashEventHook()
{
    const LPTOP_LEVEL_EXCEPTION_FILTER previous = SetUnhandledExceptionFilter(&CrashGate);
    if (previous != &CrashGate)
    {
        g_filter_before_gate.Store(previous, MemoryOrder::Release);  // (installed again after an uninstall)
    }
}

bool InstallCrashEventHook(CrashEventFn fn)
{
    g_crash_event_fn.Store(fn, MemoryOrder::Release);
    if (g_observer == nullptr)
    {
        g_observer = AddVectoredExceptionHandler(1, &ObserveException);
    }
    // abort() reaches the library's SIGABRT handler only after the C runtime's message: a dialog box in a Debug build,
    // which waits for a click (a windowed program has nowhere else to print it). The report is the message; no dialog
    // and no Windows Error Reporting call.
    if (!g_abort_flags_saved)
    {
        g_abort_flags_before = _set_abort_behavior(0, kAbortFlags) & kAbortFlags;
        g_abort_flags_saved = true;
    }
    const LPTOP_LEVEL_EXCEPTION_FILTER library = SetUnhandledExceptionFilter(&CrashGate);
    if (library != &CrashGate)
    {
        g_library_filter.Store(library, MemoryOrder::Release);
    }
    g_crashing_thread.Store(0, MemoryOrder::Release);
    g_gate_active.Store(true, MemoryOrder::Release);
    return g_observer != nullptr;
}

bool CrashReportInProgress()
{
    return g_reports_in_progress.Load(MemoryOrder::Acquire) != 0;
}

void RemoveCrashEventHook()
{
    // The gate stays the top-level filter (the library restores it when it closes) but passes everything on to what was
    // there before it.
    g_gate_active.Store(false, MemoryOrder::Release);
    g_library_filter.Store(nullptr, MemoryOrder::Release);
    g_crash_event_fn.Store(nullptr, MemoryOrder::Release);
    if (g_abort_flags_saved)
    {
        _set_abort_behavior(g_abort_flags_before, kAbortFlags);
        g_abort_flags_saved = false;
    }
    if (g_observer != nullptr)
    {
        RemoveVectoredExceptionHandler(g_observer);
        g_observer = nullptr;
    }
}

// SIGNAL-SAFE-BEGIN
void ReportCrashFromExceptionPointers(const void* exception_pointers)
{
    const CrashEventFn fn = g_crash_event_fn.Load(MemoryOrder::Acquire);
    if (fn == nullptr || exception_pointers == nullptr)
    {
        return;
    }
    const EXCEPTION_POINTERS& info = *static_cast<const EXCEPTION_POINTERS*>(exception_pointers);
    if (info.ExceptionRecord == nullptr || info.ContextRecord == nullptr)
    {
        return;
    }
    g_reports_in_progress.FetchAdd(1, MemoryOrder::AcqRel);
    CrashEvent event;
    const EXCEPTION_RECORD& record = *info.ExceptionRecord;
    event.reason = ReasonOf(record.ExceptionCode);
    if (record.ExceptionCode == EXCEPTION_ACCESS_VIOLATION || record.ExceptionCode == EXCEPTION_IN_PAGE_ERROR)
    {
        if (record.NumberParameters >= 2)
        {  // [0]: 0 read, 1 write, 8 execute; [1]: the address
            event.has_fault_address = true;
            event.fault_address = static_cast<u64>(record.ExceptionInformation[1]);
        }
    }
    else if (record.ExceptionAddress != nullptr && event.reason != CrashReason::Abort)
    {
        // The instruction that faulted; an abort has none (the record is the library's own, raised where it caught
        // abort()), as on Linux.
        event.has_fault_address = true;
        event.fault_address = reinterpret_cast<u64>(record.ExceptionAddress);
    }
    char thread_name[kMaxThreadNameBytes];
    event.thread_id = FaultingThread(info, thread_name);
    event.thread_known = event.thread_id !=
                         0;  // unknown: the report must not print the handler thread's own id or name
    event.thread_name = thread_name;
    WalkStack(*info.ContextRecord, event.trace);
    RunReportGuarded(fn, &event);
    g_reports_in_progress.FetchSub(1, MemoryOrder::AcqRel);
}
// SIGNAL-SAFE-END

}  // namespace detail

// The stack a crashed thread keeps for the observer and the library's filter after a stack overflow (the report itself
// is written on Breakpad's handler thread, with its own stack). Measured on Day 155: see BACKEND.md,
// RegisterThreadForCrashHandling.
constexpr ULONG kStackGuaranteeBytes = 64 * 1024;

bool RegisterThreadForCrashHandling(const char* name)
{
    if (name != nullptr)
    {
        SetCrashThreadName(name);
        wchar_t wide[64];
        os::windows::Utf8ToUtf16(StringView(name), wide, sizeof(wide) / sizeof(wide[0]));
        SetThreadDescription(GetCurrentThread(), wide);
    }
    // The thread's stack, so an abort() on it (which raises no exception the observer sees) is reported for the right
    // thread.
    thread_local bool recorded = false;
    if (!recorded)
    {
        ULONG_PTR low = 0;
        ULONG_PTR high = 0;
        GetCurrentThreadStackLimits(&low, &high);
        const u32 slot = g_thread_count.FetchAdd(1, MemoryOrder::AcqRel);
        if (slot < kRegisteredThreads)
        {
            g_threads[slot].thread.Store(GetCurrentThreadId(), MemoryOrder::Relaxed);
            g_threads[slot].low.Store(low, MemoryOrder::Relaxed);
            g_threads[slot].high.Store(high, MemoryOrder::Relaxed);
            g_threads[slot].name.Store(CrashThreadName(),
                                       MemoryOrder::Relaxed);  // read at crash time, so a later rename shows
        }
        else
        {
            g_thread_count.Store(kRegisteredThreads, MemoryOrder::Release);
        }
        recorded = true;
    }
    if (!CrashHandlerInstalled())
    {
        return false;  // nothing handles faults yet (only the name and the stack above)
    }
    ULONG guarantee = kStackGuaranteeBytes;
    return SetThreadStackGuarantee(&guarantee) != FALSE;
}

}  // namespace engine_placeholder::debug
