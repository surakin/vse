// Fault injection on POSIX, Linux and macOS (Phase 21 Day 291: moved from os/linux/debug/fault_injection.cpp): the same
// signals, with the bus error from each system's hook (posix::TriggerBusError).
#include "debug/fault_injection.h"
#include "os/posix/debug/posix_debug.h"

#include "log/assert.h"

#include "os/console.h"
#include "build_defs.h"

namespace vse::debug
{

namespace
{

volatile int* volatile g_fault_null_pointer = nullptr;
volatile int g_fault_zero = 0;
volatile int g_fault_one = 1;
volatile unsigned g_fault_depth = 0;

// Recurses with a sizeable frame so the stack runs out quickly; the volatile counter and the use of the result keep the
// compiler from turning the recursion into a loop.
[[gnu::noinline]] unsigned FaultRecurse(unsigned n)
{
    if (g_fault_zero != 0)
    {
        return n;  // never taken; the compiler cannot prove the recursion is unconditional
    }
    volatile char frame[2048];
    frame[0] = static_cast<char>(n);
    g_fault_depth = g_fault_depth + 1;
    return FaultRecurse(n + 1) + static_cast<unsigned>(frame[0]);
}

}  // namespace

bool FaultSupported(FaultKind kind)
{
    switch (kind)
    {
    case FaultKind::DivideByZero:
#if ENGINE_ARCH_X64 || ENGINE_ARCH_X86
        return true;
#else
        return false;  // AArch64 integer division by zero returns 0 instead of trapping
#endif
    default: return true;
    }
}

void InjectFault(FaultKind kind)
{
    ENGINE_ASSERT(FaultSupported(kind), "InjectFault: this fault is not available on this platform");
    switch (kind)
    {
    case FaultKind::NullWrite: *g_fault_null_pointer = 1; break;
    case FaultKind::Abort: os::Abort();
    case FaultKind::IllegalInstruction:
        // An undefined instruction, not __builtin_trap(): on AArch64 that is `brk`, which raises SIGTRAP, not SIGILL.
#if ENGINE_ARCH_X64 || ENGINE_ARCH_X86
        asm volatile("ud2");
#elif ENGINE_ARCH_ARM64
        asm volatile(".inst 0x00000000");  // udf #0
#else
        __builtin_trap();
#endif
        break;
    case FaultKind::DivideByZero:
    {
        // Both operands volatile: with a constant numerator an optimizing compiler turns `1 / x` into compares and
        // never emits a divide instruction.
        volatile int result = g_fault_one / g_fault_zero;
        (void)result;
        break;
    }
    case FaultKind::StackOverflow: FaultRecurse(0); break;
    case FaultKind::BusError: posix::TriggerBusError(); break;
    }
    // The fault did not end the process (it should have): make sure this function never returns.
    os::Abort();
}

}  // namespace vse::debug
