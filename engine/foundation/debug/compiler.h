#pragma once

#include "build_defs.h"

//! The compiler- and architecture-specific constructs of the whole engine, in one place.
//!
//! Engine code names no compiler builtin, attribute spelling or inline assembly of its own: it uses these macros, so a
//! port to another compiler (MSVC and clang-cl are supported) changes this header only. Begun for the debugging
//! facilities, hence its directory; it is a base-layer header that every layer may include.

/// Stops a debugger at the line that uses it, as if a breakpoint were set there.
///
/// Expands to an instruction (`int3`, `brk`) or the compiler's own intrinsic, so the debugger stops in the function
/// that uses the macro, not in a helper. With no debugger attached the process receives a fatal trap signal.
#if ENGINE_COMPILER_MSVC_ABI
#define ENGINE_DEBUG_BREAK() __debugbreak()
#elif ENGINE_COMPILER_CLANG_FAMILY && ENGINE_HAS_BUILTIN(__builtin_debugtrap)
#define ENGINE_DEBUG_BREAK() __builtin_debugtrap()
#elif ENGINE_ARCH_X64 || ENGINE_ARCH_X86
#define ENGINE_DEBUG_BREAK() __asm__ volatile("int3")
#elif ENGINE_ARCH_ARM64
#define ENGINE_DEBUG_BREAK() __asm__ volatile("brk #0xf000")
#else
#define ENGINE_DEBUG_BREAK() __builtin_trap()
#endif

/// Terminates the process at once with an illegal-instruction trap, without calling any handler first.
///
/// For code that must not continue even where an assertion was compiled out. Unlike [[ENGINE_DEBUG_BREAK]], resuming is
/// not possible.
#if ENGINE_COMPILER_MSVC
#include <intrin.h>
// <intrin.h> declares __ud2 as a plain void function, so __assume(0) tells MSVC that nothing follows (no "must return a
// value").
#define ENGINE_TRAP() (__ud2(), __assume(0))
#else
#define ENGINE_TRAP() __builtin_trap()
#endif

/// Lets the compiler check the arguments of a `printf`-style function against its format string, placed after the
/// declarator: `void Log(const char* fmt, ...) ENGINE_PRINTF_FORMAT(1, 2);`.
///
/// `fmt_index` is the 1-based position of the format parameter and `args_index` that of the `...`; for a non-static
/// member function, `this` is position 1. Empty on MSVC, which has no attribute of this kind (clang-cl takes the GNU
/// form).
#if ENGINE_COMPILER_GCC || ENGINE_COMPILER_CLANG_FAMILY
#define ENGINE_PRINTF_FORMAT(fmt_index, args_index) __attribute__((format(printf, fmt_index, args_index)))
#else
#define ENGINE_PRINTF_FORMAT(fmt_index, args_index)
#endif

/// Lets a data member of an empty type (a stateless deleter or allocator) take no space in its class:
/// `ENGINE_NO_UNIQUE_ADDRESS Deleter d_;`.
///
/// The standard `[[no_unique_address]]` is ignored by MSVC and by clang-cl (the MSVC ABI keeps it inert for
/// compatibility); both honor `[[msvc::no_unique_address]]`.
#if ENGINE_COMPILER_MSVC_ABI
#define ENGINE_NO_UNIQUE_ADDRESS [[msvc::no_unique_address]]
#else
#define ENGINE_NO_UNIQUE_ADDRESS [[no_unique_address]]
#endif

/// Tells the compiler that control never reaches this point, so it may drop the code that would follow.
///
/// Only for places that really cannot be reached; in debug builds use [[ENGINE_UNREACHABLE]], which checks.
#if ENGINE_COMPILER_MSVC_ABI
#define ENGINE_UNREACHABLE_HINT() __assume(0)
#else
#define ENGINE_UNREACHABLE_HINT() __builtin_unreachable()
#endif

/// Marks the function it is placed on as rarely called, so the compiler keeps calls to it out of the hot path.
#if ENGINE_COMPILER_MSVC_ABI
#define ENGINE_COLD
#else
#define ENGINE_COLD __attribute__((cold))
#endif

/// Keeps the compiler from moving or removing memory accesses across this point, and from turning a call just before it
/// into a tail call, so the calling function keeps its frame in a stack trace.
#if ENGINE_COMPILER_MSVC_ABI
#include <intrin.h>
#define ENGINE_COMPILER_BARRIER() _ReadWriteBarrier()
#else
#define ENGINE_COMPILER_BARRIER() __asm__ volatile("" ::: "memory")
#endif

/// Keeps the compiler from inlining a lambda, placed between the lambda's parameter list and its `->`: `[](int x)
/// ENGINE_NOINLINE_LAMBDA -> int`.
#if ENGINE_COMPILER_MSVC
// MSVC ignores [[msvc::noinline]] in this position (warning C4649, and the lambda is inlined) but honors
// __declspec(noinline) (checked on MSVC 14.51, Phase 13 Day 148).
#define ENGINE_NOINLINE_LAMBDA __declspec(noinline)
#else
// (clang, including clang-cl and clang targeting the MSVC ABI, rejects [[msvc::noinline]] on a lambda but takes the GNU
// attribute)
#define ENGINE_NOINLINE_LAMBDA __attribute__((noinline))
#endif

/// Keeps the compiler from inlining the function it is placed on, so the function stays a distinct frame in a stack
/// trace.
#if ENGINE_COMPILER_MSVC_ABI
#define ENGINE_NOINLINE __declspec(noinline)
#else
#define ENGINE_NOINLINE __attribute__((noinline))
#endif
