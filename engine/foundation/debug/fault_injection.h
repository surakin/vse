#pragma once

#include "types.h"

//! Deliberate faults for testing crash handling.
//!
//! Tests call [[debug::InjectFault]] inside a child process (see death_test.h) and check the reported
//! [[debug::CrashReason]]; they never name a signal or exception code. Each platform implements the faults it has.
namespace engine_placeholder::debug {

/// A kind of fault a test can cause on purpose.
enum class FaultKind : u8 {
    NullWrite,          ///< Write through a null pointer.
    Abort,              ///< Call `abort`.
    IllegalInstruction, ///< Execute an instruction the CPU rejects.
    DivideByZero,       ///< Integer division by zero, where the hardware traps on it.
    StackOverflow,      ///< Recurse until the stack is exhausted.
    BusError            ///< Touch a mapped page that has no backing store.
};

/// Reports whether the current platform can cause `kind`.
///
/// Param kind: the fault to ask about.
/// Returns: true if [[debug::InjectFault]] produces that fault here. Integer division does not trap on some CPUs, so
///   [[debug::FaultKind::DivideByZero]] is not available everywhere.
/// Thread-safety: thread-safe.
bool FaultSupported(FaultKind kind);

/// Causes the fault `kind` and never returns.
///
/// Param kind: the fault to cause; must satisfy [[debug::FaultSupported]].
/// Aborts: `kind` is not supported on this platform.
/// Thread-safety: thread-safe. The fault ends the whole process.
[[noreturn]] void InjectFault(FaultKind kind);

} // namespace engine_placeholder::debug
