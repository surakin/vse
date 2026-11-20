#pragma once

#include <cstdarg>

//! Variable argument lists (Phase 16): the engine's spelling of `<cstdarg>`. The macros wrap the language's `va_*`
//! macros, which are macros and cannot be functions.
namespace vse
{

/// The state of a walk through a function's variable arguments (`va_list`).
using VarArgs = va_list;

}  // namespace vse

/// Starts reading the variable arguments after the parameter `last` (`va_start`).
#define ENGINE_VA_START(args, last) va_start(args, last)
/// Finishes reading the variable arguments (`va_end`); every `ENGINE_VA_START` and `ENGINE_VA_COPY` needs one.
#define ENGINE_VA_END(args) va_end(args)
/// Copies the state of one walk to another (`va_copy`).
#define ENGINE_VA_COPY(destination, source) va_copy(destination, source)
