// The macOS half of crash reporting (debug/crash_event_platform.h; Phase 21 Day 291). Breakpad takes hardware faults as
// Mach exceptions on its own handler thread (the crashed thread and the others suspended) and aborts through a SIGABRT
// handler on the crashing thread; either way sentry calls `on_crash` with the crashed thread's register context and
// nothing else (no exception type, no thread). So the report is built from that context: which path it came from (the
// context's stack pointer on the current thread's stack: the abort signal), the reason from the trap the context
// records, the crashed thread found among the task's threads by its registers, and the stack walked from its frame
// pointer, every read through vm_read_overwrite so a bad pointer cannot fault the handler. No Linux-style signal
// recorder is installed.
//
// A host's own handlers (crash handler chaining Day 3): Breakpad would _exit after the dump, so a handler the host
// installed before the crash handler never ran. NoteHostSignalHandlers keeps them; after the report, when the host has
// a handler for the crash's signal, ArmHostRelay puts a one-shot relay on that signal and sets the vendored Breakpad's
// local switch (breakpad_mac_continue_after_dump, VENDORED.md), so Breakpad writes the envelope and does not exit: a
// Mach exception goes back to the kernel unhandled, which delivers the BSD signal on the faulting thread, and an abort
// returns from Breakpad's SIGABRT handler into a SIGABRT this file raised and held. The relay restores the default
// action and calls the host's handler once, with the signal's own siginfo and context; then the fault ends the process.
#include "debug/crash_event_platform.h"
#include "debug/crash_handler.h"
#include "debug/thread_info.h"
#include "os/macos/debug/thread_id_macos.h"

#include "build_defs.h"
#include "cstr/mem_ops.h"
#include "sync/atomic.h"

#include <mach/mach.h>
#include <pthread.h>
#include "os/signal.h"
#include <sys/ucontext.h>
#if ENGINE_ARCH_ARM64
#include <sys/_types/_ucontext64.h>
#endif

// The vendored Breakpad's local switch: nonzero, set during the dump callback, keeps it from _exiting after the dump
// (third_party/sentry_native/VENDORED.md).
extern "C" volatile int breakpad_mac_continue_after_dump;

namespace vse::debug
{

namespace
{

constexpr u64 kStackOverflowWindow = 64 * 1024;  // a fault this close below the stack pointer is the stack running out
constexpr u64 kPageZeroEnd = 0x100000000ull;     // __PAGEZERO covers the first 4 GiB of a 64-bit process

Atomic<detail::CrashEventFn> g_event_fn{nullptr};
Atomic<bool> g_reported{false};
Atomic<bool> g_reporting{false};
detail::CrashEvent g_event;  // filled by the one report
char g_thread_name[64];      // the crashed thread's name, when the report runs on Breakpad's thread

// The host's handlers from before the crash library's (NoteHostSignalHandlers), called once by the relay after the
// dump.
constexpr int kHostSignals[] = {SIGSEGV, SIGBUS, SIGILL, SIGFPE, SIGTRAP, SIGABRT};
struct sigaction g_host[NSIG];
bool g_host_noted[NSIG];
Atomic<bool> g_host_called{false};

// SIGNAL-SAFE-BEGIN
// The registers the report needs, from either architecture's context.
struct Registers
{
    u64 pc = 0;
    u64 fp = 0;
    u64 sp = 0;
    u64 fault_address = 0;
    CrashReason reason = CrashReason::None;
    bool has_fault_address = false;
};

bool ReadWord(u64 address, u64* out)
{
    mach_vm_size_t got = 0;
    return ::vm_read_overwrite(::mach_task_self(), static_cast<vm_address_t>(address), sizeof(u64),
                               reinterpret_cast<vm_address_t>(out),
                               reinterpret_cast<vm_size_t*>(&got)) == KERN_SUCCESS &&
           got == sizeof(u64);
}

// A fault in a region that is mapped with no access (above __PAGEZERO) is what macOS delivers as SIGBUS; anything else
// is SIGSEGV's.
CrashReason AccessReason(u64 address)
{
    if (address < kPageZeroEnd)
    {
        return CrashReason::AccessViolation;
    }
    vm_address_t region = static_cast<vm_address_t>(address);
    vm_size_t size = 0;
    vm_region_basic_info_data_64_t info;
    mach_msg_type_number_t count = VM_REGION_BASIC_INFO_COUNT_64;
    mach_port_t object = MACH_PORT_NULL;
    const kern_return_t kr = ::vm_region_64(::mach_task_self(), &region, &size, VM_REGION_BASIC_INFO_64,
                                            reinterpret_cast<vm_region_info_t>(&info), &count, &object);
    if (object != MACH_PORT_NULL)
    {
        ::mach_port_deallocate(::mach_task_self(), object);
    }
    if (kr == KERN_SUCCESS && region <= address && address < region + size && info.protection == VM_PROT_NONE)
    {
        return CrashReason::BusError;
    }
    return CrashReason::AccessViolation;
}

// The reason and fault address from a thread's exception state. Breakpad's context carries only the thread state (its
// exception part is zero), so this is read from the crashed thread itself, where the kernel keeps the state of the
// exception it took.
void ReasonFromException([[maybe_unused]] thread_act_t thread, Registers& r)
{
    r.reason = CrashReason::Abort;
#if ENGINE_ARCH_X64
    x86_exception_state64_t es;
    mach_msg_type_number_t n = x86_EXCEPTION_STATE64_COUNT;
    if (::thread_get_state(thread, x86_EXCEPTION_STATE64, reinterpret_cast<thread_state_t>(&es), &n) != KERN_SUCCESS)
    {
        return;
    }
    r.fault_address = es.__faultvaddr;
    switch (es.__trapno)
    {
    case 0: r.reason = CrashReason::ArithmeticFault; break;     // #DE, divide error
    case 6: r.reason = CrashReason::IllegalInstruction; break;  // #UD (ud2)
    case 3: r.reason = CrashReason::Abort; break;               // int3, as Linux names SIGTRAP
    case 13:                                                    // #GP: a non-canonical address
    case 14:                                                    // #PF
        r.reason = AccessReason(r.fault_address);
        r.has_fault_address = true;
        break;
    default: break;
    }
#elif ENGINE_ARCH_ARM64
    arm_exception_state64_t es;
    mach_msg_type_number_t n = ARM_EXCEPTION_STATE64_COUNT;
    if (::thread_get_state(thread, ARM_EXCEPTION_STATE64, reinterpret_cast<thread_state_t>(&es), &n) != KERN_SUCCESS)
    {
        return;
    }
    r.fault_address = es.__far;
    switch (es.__esr >> 26)
    {  // the exception class
    case 0x20:
    case 0x21:
    case 0x24:
    case 0x25:  // instruction or data abort
        r.reason = AccessReason(r.fault_address);
        r.has_fault_address = true;
        break;
    case 0x00: r.reason = CrashReason::IllegalInstruction; break;  // udf
    default: break;                                                // brk, as Linux names SIGTRAP
    }
#else
#endif
    if (r.has_fault_address && r.sp != 0 && r.fault_address + kStackOverflowWindow >= r.sp &&
        r.fault_address < r.sp + 4096)
    {
        r.reason = CrashReason::StackOverflow;
    }
}

Registers FromContext(const void* user_context)
{
    Registers r;
#if ENGINE_ARCH_X64
    const ucontext_t* uc = static_cast<const ucontext_t*>(user_context);
    const auto* mc = uc->uc_mcontext;
    r.pc = mc->__ss.__rip;
    r.fp = mc->__ss.__rbp;
    r.sp = mc->__ss.__rsp;
#elif ENGINE_ARCH_ARM64
    const ucontext64_t* uc = static_cast<const ucontext64_t*>(user_context);
    const auto* mc = uc->uc_mcontext64;
    r.pc = mc->__ss.__pc;
    r.fp = mc->__ss.__fp;
    r.sp = mc->__ss.__sp;
#endif
    return r;
}

// The crashed thread's stack from its frame pointer: [fp] is the caller's frame pointer, [fp + 8] the return address.
void WalkFrames(const Registers& r, StackTrace& trace)
{
    trace = StackTrace();
    trace.frames[trace.count++] = r.pc;
    u64 fp = r.fp;
    while (fp != 0 && (fp & 7) == 0)
    {
        u64 next = 0;
        u64 ret = 0;
        if (!ReadWord(fp, &next) || !ReadWord(fp + 8, &ret) || ret == 0)
        {
            break;
        }
        if (trace.count == kMaxStackFrames)
        {
            trace.truncated = true;
            break;
        }
        trace.frames[trace.count++] = ret;
        if (next <= fp)
        {
            break;  // frames go up the stack; anything else is a corrupt chain
        }
        fp = next;
    }
}

// True when `sp` is on this thread's stack or its signal stack: the context is this thread's own, so the report runs on
// the crashing thread (Breakpad's SIGABRT handler), not on Breakpad's exception thread. The signal stack counts so that
// an abort from a handler running there (SA_ONSTACK) is still recognized (Day 291 review).
bool OnCurrentThreadStack(u64 sp)
{
    pthread_t self = ::pthread_self();
    const u64 top = reinterpret_cast<u64>(::pthread_get_stackaddr_np(self));
    const u64 size = ::pthread_get_stacksize_np(self);
    if (sp <= top && sp + size >= top)
    {
        return true;
    }
    stack_t alternate;
    if (::sigaltstack(nullptr, &alternate) == 0 && (alternate.ss_flags & SS_DISABLE) == 0)
    {
        const u64 base = reinterpret_cast<u64>(alternate.ss_sp);
        if (sp >= base && sp <= base + alternate.ss_size)
        {
            return true;
        }
    }
    return false;
}

// Finds, among the task's threads other than this one, the one whose registers are the context's; fills its id and
// name.
bool FindCrashedThread(Registers& r, u64* id)
{
    thread_act_array_t threads = nullptr;
    mach_msg_type_number_t count = 0;
    if (::task_threads(::mach_task_self(), &threads, &count) != KERN_SUCCESS)
    {
        return false;
    }
    const thread_act_t self = ::mach_thread_self();
    bool found = false;
    for (mach_msg_type_number_t i = 0; i < count; ++i)
    {
        const thread_act_t t = threads[i];
        if (!found && t != self)
        {
#if ENGINE_ARCH_X64
            x86_thread_state64_t state;
            mach_msg_type_number_t n = x86_THREAD_STATE64_COUNT;
            const bool read = ::thread_get_state(t, x86_THREAD_STATE64, reinterpret_cast<thread_state_t>(&state), &n) ==
                              KERN_SUCCESS;
            const bool match = read && state.__rip == r.pc && state.__rsp == r.sp;
#elif ENGINE_ARCH_ARM64
            arm_thread_state64_t state;
            mach_msg_type_number_t n = ARM_THREAD_STATE64_COUNT;
            const bool read = ::thread_get_state(t, ARM_THREAD_STATE64, reinterpret_cast<thread_state_t>(&state), &n) ==
                              KERN_SUCCESS;
            const bool match = read && arm_thread_state64_get_pc(state) == r.pc &&
                               arm_thread_state64_get_sp(state) == r.sp;
#else
            const bool match = false;
#endif
            if (match)
            {
                thread_identifier_info_data_t info;
                mach_msg_type_number_t m = THREAD_IDENTIFIER_INFO_COUNT;
                ReasonFromException(t, r);
                if (::thread_info(t, THREAD_IDENTIFIER_INFO, reinterpret_cast<thread_info_t>(&info), &m) ==
                    KERN_SUCCESS)
                {
                    *id = macos::ThreadNumber(info.thread_id);
                    found = true;
                }
                // The name through the kernel (THREAD_EXTENDED_INFO), not pthread_getname_np: libpthread's calls take
                // its thread-list lock, which a thread Breakpad suspended may hold.
                g_thread_name[0] = '\0';
                thread_extended_info_data_t extended;
                mach_msg_type_number_t e = THREAD_EXTENDED_INFO_COUNT;
                if (::thread_info(t, THREAD_EXTENDED_INFO, reinterpret_cast<thread_info_t>(&extended), &e) ==
                    KERN_SUCCESS)
                {
                    usize k = 0;
                    for (; k + 1 < sizeof g_thread_name && k < sizeof extended.pth_name && extended.pth_name[k] != '\0';
                         ++k)
                    {
                        g_thread_name[k] = extended.pth_name[k];
                    }
                    g_thread_name[k] = '\0';
                }
            }
        }
        ::mach_port_deallocate(::mach_task_self(), t);
    }
    ::mach_port_deallocate(::mach_task_self(), self);
    ::vm_deallocate(::mach_task_self(), reinterpret_cast<vm_address_t>(threads), count * sizeof(thread_act_t));
    return found;
}

// The fault types Breakpad takes as Mach exceptions (its ports are cleared and restored around the report, below).
constexpr exception_mask_t kFaultMask = EXC_MASK_BAD_ACCESS | EXC_MASK_BAD_INSTRUCTION | EXC_MASK_ARITHMETIC |
                                        EXC_MASK_BREAKPOINT;

// Whether the kernel delivers `signal_number` again by itself when the handler returns: a hardware fault (the
// faulting instruction runs again). An abort, a breakpoint (the PC is already past int3) and anything sent with
// kill/raise (macOS: si_code SI_USER and up, 0x10001 and above, or 0) are not repeated.
bool RepeatsByItself(int signal_number, const siginfo_t* info)
{
    if (info == nullptr || info->si_code <= 0 || info->si_code >= SI_USER)
    {
        return false;
    }
    return signal_number == SIGSEGV || signal_number == SIGBUS || signal_number == SIGILL || signal_number == SIGFPE;
}

// The signal relay: the default action back first (a fault inside the host's handler, or the fault running again after
// it returns, then ends the process), then the host's handler, once per process; a signal nothing repeats is raised
// again afterwards, so the process always ends.
void HostRelay(int signal_number, siginfo_t* info, void* user_context)
{
    struct sigaction dfl;
    MemSet(&dfl, 0, sizeof(dfl));
    dfl.sa_handler = SIG_DFL;
    ::sigaction(signal_number, &dfl, nullptr);
    breakpad_mac_continue_after_dump = 0;  // used: a later dump in this process ends it as Breakpad always does
    const bool first = signal_number > 0 && signal_number < NSIG && g_host_noted[signal_number] &&
                       !g_host_called.Exchange(true, MemoryOrder::AcqRel);
    if (first)
    {
        // A fault inside the host's handler must not reach Breakpad again (on the abort path it is still installed):
        // with the task's ports for the fault types cleared, it goes to the kernel and ends the process. The dump is
        // written already.
        ::task_set_exception_ports(::mach_task_self(), kFaultMask, MACH_PORT_NULL, EXCEPTION_DEFAULT,
                                   THREAD_STATE_NONE);
        const struct sigaction& host = g_host[signal_number];
        if ((host.sa_flags & SA_SIGINFO) != 0)
        {
            if (host.sa_sigaction != nullptr)
            {
                host.sa_sigaction(signal_number, info, user_context);
            }
        }
        else if (host.sa_handler != SIG_DFL && host.sa_handler != SIG_IGN && host.sa_handler != nullptr)
        {
            host.sa_handler(signal_number);
        }
    }
    if (!RepeatsByItself(signal_number, info))
    {
        // Nothing delivers it again: raise it once more, now with the default action.
        ::raise(signal_number);
    }
}

// After the report: puts the relay on each of `signals` the host had a handler for and, when there is one, sets
// Breakpad's switch so the process goes on to the signal instead of exiting. An abort (`raise_abort`, on the crashing
// thread inside Breakpad's SIGABRT handler, where SIGABRT is blocked) also raises the SIGABRT the relay will take once
// that handler has returned, after the envelope is written.
void ArmHostRelay(const int* signals, usize count, bool raise_abort)
{
    if (g_host_called.Load(MemoryOrder::Acquire))
    {
        return;
    }
    bool armed = false;
    for (usize i = 0; i < count; ++i)
    {
        const int signal_number = signals[i];
        if (!g_host_noted[signal_number])
        {
            continue;
        }
        struct sigaction relay;
        MemSet(&relay, 0, sizeof(relay));
        relay.sa_sigaction = &HostRelay;
        // The host's own flags (its stack choice). For a hardware fault also SA_NODEFER, as Linux's recorder has: a
        // fault of the same kind inside the host's handler is then delivered (to the default action the relay put back)
        // instead of running the blocked faulting instruction again forever. An abort keeps the usual deferral, as on
        // Linux, where the recorder holds a raised signal: a raise inside the host's SIGABRT handler waits until it
        // returns.
        const bool fault = signal_number != SIGABRT;
        relay.sa_flags = (g_host[signal_number].sa_flags | SA_SIGINFO | (fault ? SA_NODEFER : 0)) & ~SA_RESETHAND;
        relay.sa_mask = g_host[signal_number].sa_mask;
        armed = ::sigaction(signal_number, &relay, nullptr) == 0 || armed;
    }
    if (!armed)
    {
        return;  // no host handler for this crash: Breakpad exits after the dump, as without chaining
    }
    breakpad_mac_continue_after_dump = 1;
    if (raise_abort)
    {
        ::raise(SIGABRT);
    }
}

// The signals the kernel may deliver for a Mach exception of the given reason (an access fault is SIGSEGV or SIGBUS by
// the kind of mapping; the guess in AccessReason is not the kernel's rule). A reason this file could not classify (an
// unknown trap number, an alignment fault, a thread state it could not read: Abort) arms them all.
usize SignalsFor(CrashReason reason, bool thread_known, int* out)
{
    if (!thread_known || reason == CrashReason::Abort)
    {
        out[0] = SIGSEGV;
        out[1] = SIGBUS;
        out[2] = SIGILL;
        out[3] = SIGFPE;
        out[4] = SIGTRAP;
        return 5;
    }
    switch (reason)
    {
    case CrashReason::AccessViolation:
    case CrashReason::BusError:
    case CrashReason::StackOverflow:
        out[0] = SIGSEGV;
        out[1] = SIGBUS;
        return 2;
    case CrashReason::IllegalInstruction: out[0] = SIGILL; return 1;
    case CrashReason::ArithmeticFault: out[0] = SIGFPE; return 1;
    default:
        out[0] = SIGSEGV;
        out[1] = SIGBUS;
        out[2] = SIGILL;
        out[3] = SIGFPE;
        out[4] = SIGTRAP;
        return 5;
    }
}

struct SavedPorts
{
    exception_mask_t masks[EXC_TYPES_COUNT];
    mach_port_t ports[EXC_TYPES_COUNT];
    exception_behavior_t behaviors[EXC_TYPES_COUNT];
    thread_state_flavor_t flavors[EXC_TYPES_COUNT];
    mach_msg_type_number_t count = 0;
};
SavedPorts g_saved_ports;

// While the report runs on Breakpad's exception thread, a fault in it (a crash-state provider, say) would be sent to
// Breakpad's port, which only this thread serves: the process would hang. With the task's ports for the fault types
// cleared, such a fault goes to the system's handler and becomes a signal whose default action ends the process (the
// dump is already written). Restored after the report.
void ClearTaskFaultPorts()
{
    g_saved_ports.count = EXC_TYPES_COUNT;
    if (::task_get_exception_ports(::mach_task_self(), kFaultMask, g_saved_ports.masks, &g_saved_ports.count,
                                   g_saved_ports.ports, g_saved_ports.behaviors, g_saved_ports.flavors) != KERN_SUCCESS)
    {
        g_saved_ports.count = 0;
    }
    ::task_set_exception_ports(::mach_task_self(), kFaultMask, MACH_PORT_NULL, EXCEPTION_DEFAULT, THREAD_STATE_NONE);
}

void RestoreTaskFaultPorts()
{
    for (mach_msg_type_number_t i = 0; i < g_saved_ports.count; ++i)
    {
        ::task_set_exception_ports(::mach_task_self(), g_saved_ports.masks[i], g_saved_ports.ports[i],
                                   g_saved_ports.behaviors[i], g_saved_ports.flavors[i]);
    }
    g_saved_ports.count = 0;
}

}  // namespace

namespace detail
{

void ReportCrashFromMachContext(const void* user_context)
{
    const CrashEventFn fn = g_event_fn.Load(MemoryOrder::Acquire);
    if (fn == nullptr || user_context == nullptr)
    {
        return;
    }
    bool expected = false;
    if (g_reported.Load(MemoryOrder::Acquire) ||
        !g_reporting.CompareExchangeStrong(expected, true, MemoryOrder::AcqRel))
    {
        return;
    }
    g_reported.Store(true, MemoryOrder::Release);
    Registers r = FromContext(user_context);
    CrashEvent& event = g_event;
    event = CrashEvent();
    WalkFrames(r, event.trace);
    if (OnCurrentThreadStack(r.sp))
    {
        // The SIGABRT handler on the crashing thread itself: abort(), an assert, a failed check.
        event.reason = CrashReason::Abort;
        event.has_fault_address = false;
        event.thread_id = CurrentThreadId();
    }
    else
    {
        // A Mach exception, on Breakpad's handler thread: the thread, and from it the reason.
        u64 id = 0;
        event.thread_known = FindCrashedThread(r, &id);
        event.reason = event.thread_known ? r.reason : CrashReason::Abort;
        event.has_fault_address = r.has_fault_address;
        event.fault_address = r.fault_address;
        event.thread_id = id;
        event.thread_name = event.thread_known ? g_thread_name : "";
    }
    const bool on_handler_thread = !OnCurrentThreadStack(r.sp);
    if (on_handler_thread)
    {
        ClearTaskFaultPorts();
    }
    fn(event);
    if (on_handler_thread)
    {
        RestoreTaskFaultPorts();
        int signals[5];
        const usize count = SignalsFor(event.reason, event.thread_known, signals);
        ArmHostRelay(signals, count, false);
    }
    else
    {
        const int abort_signal[1] = {SIGABRT};
        ArmHostRelay(abort_signal, 1, true);
    }
    g_reporting.Store(false, MemoryOrder::Release);
}

// SIGNAL-SAFE-END

void NoteHostSignalHandlers()
{
    g_host_called.Store(false, MemoryOrder::Release);
    breakpad_mac_continue_after_dump = 0;
    for (int signal_number : kHostSignals)
    {
        struct sigaction current;
        MemSet(&current, 0, sizeof(current));
        const bool read = ::sigaction(signal_number, nullptr, &current) == 0;
        const bool ours = (current.sa_flags & SA_SIGINFO) != 0 && current.sa_sigaction == &HostRelay;
        const bool none = (current.sa_flags & SA_SIGINFO) == 0 &&
                          (current.sa_handler == SIG_DFL || current.sa_handler == SIG_IGN);
        g_host_noted[signal_number] = read && !ours && !none;
        g_host[signal_number] = current;
    }
}

bool InstallCrashEventHook(CrashEventFn fn)
{
    // Nothing to install: the library's on_crash hook calls ReportCrashFromMachContext (crash_handler_sentry.cpp).
    g_reported.Store(false, MemoryOrder::Release);
    g_event_fn.Store(fn, MemoryOrder::Release);
    return true;
}

void ReleaseThreadAlternateStack()
{
    // This backend keeps nothing per thread that a thread_local destructor would have to release.
}

bool CrashReportInProgress()
{
    return g_reporting.Load(MemoryOrder::Acquire);
}

void RemoveCrashEventHook()
{
    g_event_fn.Store(nullptr, MemoryOrder::Release);
    for (int signal_number : kHostSignals)
    {
        g_host_noted[signal_number] = false;  // the library's close puts the host's SIGABRT handler back
    }
    g_reported.Store(false, MemoryOrder::Release);
}

}  // namespace detail

bool RegisterThreadForCrashHandling(const char* name)
{
    // Mach exceptions are taken on Breakpad's own thread, so a thread needs no alternate stack to report its stack
    // overflow: only the name. It is also the thread's pthread name, which the report reads from Breakpad's thread (the
    // engine's own is per thread; FindCrashedThread).
    if (name != nullptr)
    {
        SetCrashThreadName(name);
        ::pthread_setname_np(name);
    }
    return CrashHandlerInstalled();
}

}  // namespace vse::debug

#if ENGINE_SANITIZER_ADDRESS
// ASan on macOS (Phase 21 Day 291): Breakpad copies the crashed thread's stack into the minidump with libc's write(),
// and ASan's write interceptor reports the redzones between that stack's frames as a stack-buffer-underflow, which ends
// the process before the report (Linux's Breakpad writes with raw system calls, which no interceptor sees). The dump's
// copy of the stack is what it is for: the reports from that one call are suppressed, through ASan's
// default-suppressions hook (a program that defines its own replaces this one). Breakpad itself is not instrumented.
extern "C" const char* __asan_default_suppressions()
{
    return "interceptor_via_fun:*MinidumpFileWriter*Copy*\n";
}
#endif
