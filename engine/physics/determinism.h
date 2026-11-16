// What the build does to floating point, observed (Phase 25 Day 374): the rules of the physics step hold only if the
// compiler follows them, so the engine asks it with a few fixed expressions
// (docs/superpowers/specs/2026-11-16-physics-design.md, section 3).
#pragma once

#include "types.h"

namespace engine_placeholder::physics
{

/// Hashes the results of a fixed set of expressions that a changed rounding, contraction or flush mode would change.
///
/// The set holds sums and products that a fused multiply-add rounds differently, a chain of `Sqrt`, conversions between
/// integers and floats, and denormal inputs and results. Every operation is one IEEE 754 defines exactly (`+ - * /
/// Sqrt`, conversions), so the value is the same on every machine and every compiler whose flags follow the rules; the
/// tests compare it with a committed constant and the Windows and macOS machines compare it with the same constant.
///
/// - Returns: the hash.
/// - Thread-safety: thread-safe; pure.
u64 ProbeHash();

/// Returns whether the compiler fused a multiply and an add in this library's own code.
///
/// It evaluates `a * a + c` on values for which the fused and the separately rounded results differ. False when the
/// build forbids contraction, as the project's flags do.
///
/// - Returns: true if the result was the fused one.
/// - Thread-safety: thread-safe; pure.
bool ContractionObserved();

/// Returns whether denormal floats are kept: a denormal times one is still nonzero.
///
/// False when the process runs with flush-to-zero or denormals-are-zero, which a library linked with `-ffast-math` (its
/// `crtfastmath.o`) or a host calling `_MM_SET_FLUSH_ZERO_MODE` sets for the whole process. The physics step requires
/// true (the engine itself never sets either mode); the step will check it (Day 378).
///
/// - Returns: true if denormals are preserved.
/// - Thread-safety: thread-safe; reads no shared state (the mode is per thread).
bool DenormalsPreserved();

/// The mode the project's flags give, as text for a report: `contraction=off denormals=kept` and so on.
///
/// - Returns: a static string.
/// - Thread-safety: thread-safe.
/// - Ownership: the string is static; the caller does not free it.
const char* ModeReport();

}  // namespace engine_placeholder::physics
