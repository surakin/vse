#pragma once

//! The few compiler- and architecture-specific constructs the debugging facilities need, in one place.
//!
//! Nothing else in the debugging code names a compiler builtin or inline assembly, so a port to another compiler
//! (MSVC, clang-cl) changes this header only.

/// Stops a debugger at the line that uses it, as if a breakpoint were set there.
///
/// Expands to an instruction (`int3`, `brk`) or the compiler's own intrinsic, so the debugger stops in the function
/// that uses the macro, not in a helper. With no debugger attached the process receives a fatal trap signal.
#if defined(_MSC_VER)
#define ENGINE_DEBUG_BREAK() __debugbreak()
#elif defined(__clang__) && __has_builtin(__builtin_debugtrap)
#define ENGINE_DEBUG_BREAK() __builtin_debugtrap()
#elif defined(__x86_64__) || defined(__i386__)
#define ENGINE_DEBUG_BREAK() __asm__ volatile("int3")
#elif defined(__aarch64__)
#define ENGINE_DEBUG_BREAK() __asm__ volatile("brk #0xf000")
#else
#define ENGINE_DEBUG_BREAK() __builtin_trap()
#endif

/// Tells the compiler that control never reaches this point, so it may drop the code that would follow.
///
/// Only for places that really cannot be reached; in debug builds use [[ENGINE_UNREACHABLE]], which checks.
#if defined(_MSC_VER)
#define ENGINE_UNREACHABLE_HINT() __assume(0)
#else
#define ENGINE_UNREACHABLE_HINT() __builtin_unreachable()
#endif

/// Marks the function it is placed on as rarely called, so the compiler keeps calls to it out of the hot path.
#if defined(_MSC_VER)
#define ENGINE_COLD
#else
#define ENGINE_COLD __attribute__((cold))
#endif

/// Keeps the compiler from moving or removing memory accesses across this point, and from turning a call just before it into
/// a tail call, so the calling function keeps its frame in a stack trace.
#if defined(_MSC_VER)
#include <intrin.h>
#define ENGINE_COMPILER_BARRIER() _ReadWriteBarrier()
#else
#define ENGINE_COMPILER_BARRIER() __asm__ volatile("" ::: "memory")
#endif

/// Keeps the compiler from inlining a lambda, placed between the lambda's parameter list and its `->`: `[](int x) ENGINE_NOINLINE_LAMBDA -> int`.
#if defined(_MSC_VER) && !defined(__clang__)
#define ENGINE_NOINLINE_LAMBDA [[msvc::noinline]]
#else
// (clang, including clang-cl and clang targeting the MSVC ABI, rejects [[msvc::noinline]] on a lambda but takes the GNU attribute)
#define ENGINE_NOINLINE_LAMBDA __attribute__((noinline))
#endif

/// Keeps the compiler from inlining the function it is placed on, so the function stays a distinct frame in a stack trace.
#if defined(_MSC_VER)
#define ENGINE_NOINLINE __declspec(noinline)
#else
#define ENGINE_NOINLINE __attribute__((noinline))
#endif
