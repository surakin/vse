// The Linux half of crash reporting: a recorder installed in front of the crash library's signal handlers (see
// debug/crash_event_platform.h), and the alternate signal stacks threads need to survive a stack overflow.
#include "debug/crash_event_platform.h"
#include "debug/crash_handler.h"
#include "debug/thread_info.h"
#include "os/os.h"

#include <atomic>
#include <csignal>
#include <cstring>
#include <sys/mman.h>
#include <unistd.h>
#include "build_defs.h"

namespace engine_placeholder::debug {

namespace {

constexpr int kFatalSignals[] = {SIGSEGV, SIGBUS, SIGILL, SIGFPE, SIGABRT, SIGTRAP};
constexpr usize kAlternateStackBytes = 128 * 1024;
// A fault this close below the stack pointer is the stack running out, not a wild pointer.
constexpr u64 kStackOverflowWindow = 64 * 1024;

std::atomic<detail::CrashEventFn> g_event_fn{nullptr};
std::atomic<bool> g_reported{false}; // a report was written in this process; a fatal signal does not need a second
std::atomic<u64> g_chaining_thread{0};  // the thread that finished its report and is inside the library's handler
std::atomic<u64> g_reporting_thread{0}; // thread id of the fault being reported, 0 when none
struct sigaction g_previous[NSIG];
bool g_installed_signal[NSIG];
detail::CrashEvent g_event; // used only by the thread that owns g_reporting_thread

// SIGNAL-SAFE-BEGIN
CrashReason ReasonFor(int signal_number) {
    switch (signal_number) {
    case SIGSEGV: return CrashReason::AccessViolation;
    case SIGBUS: return CrashReason::BusError;
    case SIGILL: return CrashReason::IllegalInstruction;
    case SIGFPE: return CrashReason::ArithmeticFault;
    default: return CrashReason::Abort; // SIGABRT, and SIGTRAP from a breakpoint instruction
    }
}

u64 StackPointer(const void* user_context) {
    if (user_context == nullptr) {
        return 0;
    }
    const ucontext_t* context = static_cast<const ucontext_t*>(user_context);
#if ENGINE_ARCH_X64
    return static_cast<u64>(context->uc_mcontext.gregs[REG_RSP]);
#elif ENGINE_ARCH_ARM64
    return static_cast<u64>(context->uc_mcontext.sp);
#else
    return 0;
#endif
}

// Passes the signal on to whatever was installed before the recorder (the library's handler, normally).
void Chain(int signal_number, siginfo_t* info, void* user_context) {
    const bool from_hardware = info != nullptr && info->si_code > 0; // a fault, not kill/raise/abort (those have si_code <= 0)
    const struct sigaction& previous = g_previous[signal_number];
    if ((previous.sa_flags & SA_SIGINFO) != 0 && previous.sa_sigaction != nullptr) {
        previous.sa_sigaction(signal_number, info, user_context);
    } else if (previous.sa_handler != SIG_DFL && previous.sa_handler != SIG_IGN && previous.sa_handler != nullptr) {
        previous.sa_handler(signal_number);
    } else {
        // SIG_DFL or SIG_IGN before us: put the default back so that returning (the faulting instruction runs again) ends
        // the process. (An ignored hardware fault would otherwise run the same instruction forever.)
        struct sigaction dfl;
        std::memset(&dfl, 0, sizeof(dfl));
        dfl.sa_handler = SIG_DFL;
        ::sigaction(signal_number, &dfl, nullptr);
        if (signal_number == SIGABRT || signal_number == SIGTRAP || !from_hardware) {
            ::raise(signal_number); // nothing will re-run a faulting instruction: deliver the signal again with the default action
        }
    }
}

void Recorder(int signal_number, siginfo_t* info, void* user_context) {
    detail::CrashEventFn fn = g_event_fn.load(std::memory_order_acquire);
    if (fn != nullptr) {
        const u64 thread = CurrentThreadId();
        const u64 owner = g_reporting_thread.load(std::memory_order_acquire);
        u64 expected = 0;
        if (owner == thread && g_chaining_thread.load(std::memory_order_acquire) == thread) {
            // A fault inside the library's own handler (it already has the lock this fault would wait for): give up. The
            // default action ends the process when the faulting instruction runs again.
            static const char kText[] = "double fault: the crash library's handler faulted; giving up\n";
            os::Write(2, kText, sizeof(kText) - 1);
            struct sigaction dfl;
            std::memset(&dfl, 0, sizeof(dfl));
            dfl.sa_handler = SIG_DFL;
            ::sigaction(signal_number, &dfl, nullptr);
            return;
        } else if (owner == thread) {
            // A fault inside the report this thread is writing: no second report, but the library still gets the fault.
            static const char kText[] = "double fault: the crash report itself faulted; continuing to the crash dump\n";
            os::Write(2, kText, sizeof(kText) - 1);
        } else if (owner == 0 && !g_reported.load(std::memory_order_acquire) &&
                   g_reporting_thread.compare_exchange_strong(expected, thread, std::memory_order_acq_rel)) {
            g_reported.store(true, std::memory_order_release);
            detail::CrashEvent& event = g_event;
            event = detail::CrashEvent();
            event.reason = ReasonFor(signal_number);
            event.thread_id = thread;
            if (info != nullptr && (signal_number == SIGSEGV || signal_number == SIGBUS || signal_number == SIGILL || signal_number == SIGFPE)) {
                event.has_fault_address = true;
                event.fault_address = reinterpret_cast<u64>(info->si_addr);
                const u64 sp = StackPointer(user_context);
                if (signal_number == SIGSEGV && sp != 0 && event.fault_address + kStackOverflowWindow >= sp &&
                    event.fault_address < sp + 4096) {
                    event.reason = CrashReason::StackOverflow;
                }
            }
            // Skip the recorder itself and the kernel's signal trampoline; the first frame is the one that faulted.
            CaptureStackTrace(event.trace, 2);
            fn(event);
            g_chaining_thread.store(thread, std::memory_order_release);
            Chain(signal_number, info, user_context);
            g_chaining_thread.store(0, std::memory_order_release);
            g_reporting_thread.store(0, std::memory_order_release);
            return;
        } else if (g_reporting_thread.load(std::memory_order_acquire) != 0) { // read again: it may have been claimed since `owner`
            // Another thread is writing the report. Calling the library now would end the process (it writes the dump and the
            // fault is delivered again) in the middle of that report, so wait for the reporting thread to finish; it calls the
            // library itself and the process ends there. Bounded, in case that thread is stuck.
            const u64 give_up = os::MonotonicTimeNanos() + 20ull * 1000 * 1000 * 1000;
            while (g_reporting_thread.load(std::memory_order_acquire) != 0 && os::MonotonicTimeNanos() < give_up) {
                os::SleepNanos(1000 * 1000);
            }
        }
        // (Else the report was already written for an earlier fault: only the library is called.)
    }
    Chain(signal_number, info, user_context);
}

// SIGNAL-SAFE-END
struct AlternateStack {
    void* memory = nullptr;
    ~AlternateStack() {
        if (memory != nullptr) {
            stack_t current;
            if (::sigaltstack(nullptr, &current) == 0 && current.ss_sp == memory) { // still ours, not replaced by someone else
                stack_t disable;
                std::memset(&disable, 0, sizeof(disable));
                disable.ss_flags = SS_DISABLE;
                ::sigaltstack(&disable, nullptr);
            }
            ::munmap(memory, kAlternateStackBytes);
        }
    }
};

} // namespace

namespace detail {

bool InstallCrashEventHook(CrashEventFn fn) {
    bool any = false;
    g_reported.store(false, std::memory_order_release);
    for (int signal_number : kFatalSignals) {
        if (g_installed_signal[signal_number]) {
            any = true; // the recorder is still in the chain (something was installed over it when it was removed): reuse it
            continue;
        }
        struct sigaction action;
        std::memset(&action, 0, sizeof(action));
        action.sa_sigaction = &Recorder;
        // SA_NODEFER: a fault inside the recorder must reach it again (to print "double fault") instead of killing the process
        // outright, which is what a fault of a blocked signal does.
        action.sa_flags = SA_SIGINFO | SA_ONSTACK | SA_NODEFER;
        sigemptyset(&action.sa_mask);
        g_installed_signal[signal_number] = ::sigaction(signal_number, &action, &g_previous[signal_number]) == 0;
        any = any || g_installed_signal[signal_number];
    }
    g_event_fn.store(any ? fn : nullptr, std::memory_order_release);
    return any;
}

bool CrashReportInProgress() {
    return g_reporting_thread.load(std::memory_order_acquire) != 0;
}

void RemoveCrashEventHook() {
    g_event_fn.store(nullptr, std::memory_order_release);
    g_reported.store(false, std::memory_order_release); // a later install may report again
    for (int signal_number : kFatalSignals) {
        if (!g_installed_signal[signal_number]) {
            continue;
        }
        struct sigaction current;
        if (::sigaction(signal_number, nullptr, &current) == 0 && (current.sa_flags & SA_SIGINFO) != 0 &&
            current.sa_sigaction == &Recorder) {
            ::sigaction(signal_number, &g_previous[signal_number], nullptr);
            g_installed_signal[signal_number] = false;
        }
        // Otherwise a handler was installed over ours; it stays, and ours (still in its chain) passes through. What it would call
        // next is the library's handler, which the caller is about to close: make that the default action instead.
        if (g_installed_signal[signal_number]) {
            std::memset(&g_previous[signal_number], 0, sizeof(g_previous[signal_number]));
        }
    }
}

} // namespace detail

bool RegisterThreadForCrashHandling(const char* name) {
    if (name != nullptr) {
        SetCrashThreadName(name);
    }
    if (!CrashHandlerInstalled()) {
        return false; // nothing handles faults yet: no stack to set up (only the name, above)
    }
    thread_local AlternateStack stack;
    if (stack.memory != nullptr) {
        return true;
    }
    stack_t existing;
    if (::sigaltstack(nullptr, &existing) == 0 && (existing.ss_flags & SS_DISABLE) == 0 && existing.ss_size >= kAlternateStackBytes) {
        return true; // the thread already has a stack at least as big as ours (the host's, a sanitizer's); leave it
    }
    void* memory = ::mmap(nullptr, kAlternateStackBytes, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS | MAP_NORESERVE, -1, 0);
    if (memory == MAP_FAILED) {
        return false;
    }
    stack_t alternate;
    std::memset(&alternate, 0, sizeof(alternate));
    alternate.ss_sp = memory;
    alternate.ss_size = kAlternateStackBytes;
    if (::sigaltstack(&alternate, nullptr) != 0) {
        ::munmap(memory, kAlternateStackBytes);
        return false;
    }
    stack.memory = memory;
    return true;
}

} // namespace engine_placeholder::debug
