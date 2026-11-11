#pragma once

/// An object-like macro.
#define ENGINE_VERSION 3

/// A function-like macro.
/// - Param x: the value to square.
#define ENGINE_SQUARE(x) ((x) * (x))

// Undocumented macro (private helper): ignored, no diagnostic.
#define ENGINE_PRIVATE_HELPER 1

#if defined(SOMETHING)
/// A conditionally compiled function.
void Conditional();
#endif
