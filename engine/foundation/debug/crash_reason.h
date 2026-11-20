#pragma once

#include "types.h"

//! Platform-neutral description of why a process or thread was stopped abnormally.
//!
//! Debugging code, the crash handler and the tests speak [[debug::CrashReason]]; the platform backends translate their
//! own codes (signals, exception codes) into it.
namespace vse::debug
{

/// Why a process died or a fault was reported, independent of the operating system.
///
/// A backend maps its native code to the closest value and keeps the raw code beside it. A stack overflow is a hardware
/// access violation on every platform, so a process that only left an exit status reports
/// [[debug::CrashReason::AccessViolation]];
/// [[debug::CrashReason::StackOverflow]] is reported only by code that saw the fault happen.
enum class CrashReason : u8
{
    None = 0,            ///< The process did not die from a fault (it exited normally, or was killed from outside).
    AccessViolation,     ///< An invalid memory access: a null or wild pointer, or a write to read-only memory.
    BusError,            ///< An access the hardware could not complete, such as a page beyond the end of a mapped file.
    IllegalInstruction,  ///< The CPU met an instruction it cannot execute.
    ArithmeticFault,     ///< A trapping arithmetic error such as an integer divide by zero.
    StackOverflow,       ///< The stack ran out; only reported by code that saw the fault happen.
    Abort,               ///< The program called `abort` (or an equivalent) on purpose.
    AssertFailed,        ///< An `ENGINE_ASSERT` failed.
    EnsureFailed         ///< An `ENGINE_ENSURE` failed and execution continued.
};

/// Returns a short stable name for a reason, for logs and test messages.
/// - Param reason: the value to name.
/// - Returns: a static string such as `"AccessViolation"`; never null.
/// - Ownership: the string has static storage; the caller does not free it.
/// - Thread-safety: thread-safe.
const char* CrashReasonName(CrashReason reason);

}  // namespace vse::debug
