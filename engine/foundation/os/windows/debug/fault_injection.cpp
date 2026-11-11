// Fault injection on Windows (Phase 13 Day 155; os/windows/debug/BACKEND.md, FaultSupported and InjectFault). Each kind
// raises the exception tests/debug/BACKEND-CONFORMANCE.md names: 0xC0000005, 0xC000001D, 0xC0000094, 0xC00000FD, and
// abort().
#include "debug/fault_injection.h"
#include "log/assert.h"

#include "os/console.h"
#include <intrin.h>
#include "build_defs.h"

namespace engine_placeholder::debug
{

namespace
{

volatile int* volatile g_fault_null_pointer = nullptr;
volatile int g_fault_zero = 0;
volatile int g_fault_one = 1;
volatile unsigned g_fault_depth = 0;

// Recurses with a sizeable frame so the stack runs out quickly; the volatile counter and the use of the result keep the
// compiler from turning the recursion into a loop, and the volatile condition (always true) from warning that it never
// ends (MSVC C4717, clang -Winfinite-recursion).
__declspec(noinline) unsigned FaultRecurse(unsigned n)
{
    volatile char frame[2048];
    frame[0] = static_cast<char>(n);
    g_fault_depth = g_fault_depth + 1;
    if (g_fault_one == 0)
    {
        return 0;
    }
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
        return false;  // arm64: an integer divide by zero returns 0 instead of trapping
#endif
    case FaultKind::BusError:
        return false;  // Windows has no direct counterpart of SIGBUS for a mapped page without backing store
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
#if ENGINE_ARCH_X64 || ENGINE_ARCH_X86
        __ud2();
#else
        __debugbreak();
#endif
        break;
    case FaultKind::DivideByZero:
    {
        // Both operands volatile: with a constant numerator an optimizing compiler emits compares instead of a divide.
        volatile int result = g_fault_one / g_fault_zero;
        (void)result;
        break;
    }
    case FaultKind::StackOverflow: FaultRecurse(0); break;
    case FaultKind::BusError: break;
    }
    // The fault did not end the process (it should have): make sure this function never returns.
    os::Abort();
}

}  // namespace engine_placeholder::debug
