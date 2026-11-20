#pragma once

#include "build_defs.h"
#include "debug/compiler.h"
#include "types.h"

#include "sync/atomic.h"

//! The assertion family and its failure handler, split out of log.h so low-level headers can assert without depending
//! on the string module.
//!
//! - [[ENGINE_ASSERT]]: a contract the code relies on; always checked, fatal.
//! - [[ENGINE_ENSURE]]: a condition that should hold but that the caller can survive; reported once per call site, then
//!   execution continues.
//! - [[ENGINE_VERIFY]]: like assert but the expression is always evaluated and the check exists only in debug builds.
//! - [[ENGINE_CHECK_SLOW]]: an expensive check, compiled out unless slow checks are on.
//! - [[ENGINE_UNREACHABLE]] and [[ENGINE_NOT_IMPLEMENTED]].
//!
//! Every failure goes to one handler (see [[SetAssertHandler]]), which tests, tools and the crash handler can replace.

/// Whether [[ENGINE_VERIFY]] and [[ENGINE_UNREACHABLE]] check in this translation unit: 1 or 0.
///
/// Defaults to 1 in a build without `NDEBUG` (the CMake Debug configuration) and 0 with it. The CMake option
/// `ENGINE_FORCE_DEBUG_CHECKS` defines it as 1 in every configuration. A translation unit may define it before
/// including this header.
#ifndef ENGINE_DEBUG_CHECKS
#ifdef NDEBUG
#define ENGINE_DEBUG_CHECKS 0
#else
#define ENGINE_DEBUG_CHECKS 1
#endif
#endif

/// Whether [[ENGINE_CHECK_SLOW]] checks in this translation unit: 1 or 0.
///
/// Same default as [[ENGINE_DEBUG_CHECKS]]; the CMake option `ENGINE_FORCE_SLOW_CHECKS` defines it as 1 in every
/// configuration.
#ifndef ENGINE_SLOW_CHECKS
#ifdef NDEBUG
#define ENGINE_SLOW_CHECKS 0
#else
#define ENGINE_SLOW_CHECKS 1
#endif
#endif

namespace vse
{

/// Signature of the function that receives log and assert output: file
/// descriptor, buffer, byte count; returns bytes written or a negative value.
using WriteSinkFn = long (*)(int fd, const void* buf, usize len);
/// Replaces the output sink shared by the default assert handler and [[Log]], so tests can
/// capture output without redirecting file descriptors.
///
/// - Param sink: replacement function; pass the default OS write function to
///   restore normal behavior. Not for production use.
/// - Thread-safety: not-thread-safe. Call only while no thread logs or asserts.
void SetWriteSinkForTesting(WriteSinkFn sink);

/// Which macro reported a failure.
enum class AssertKind : u8
{
    Assert,         ///< [[ENGINE_ASSERT]] (and [[ENGINE_CHECK_SLOW]], and the legacy [[AssertFail]]).
    Ensure,         ///< [[ENGINE_ENSURE]].
    Verify,         ///< [[ENGINE_VERIFY]].
    Unreachable,    ///< [[ENGINE_UNREACHABLE]].
    NotImplemented  ///< [[ENGINE_NOT_IMPLEMENTED]].
};

/// Everything known about a failed check; passed to the assert handler.
///
/// - Thread-safety: thread-compatible. A plain value that lives for the duration of the handler call.
/// - Ownership: every string is borrowed: literals from the macro expansion, valid for the whole program.
struct AssertInfo
{
    /// Which macro reported it.
    AssertKind kind;
    /// Source file (`__FILE__`).
    const char* file;
    /// Source line.
    int line;
    /// Enclosing function (`__func__`); may be empty.
    const char* function;
    /// The stringified condition; empty for [[ENGINE_UNREACHABLE]] and [[ENGINE_NOT_IMPLEMENTED]].
    const char* condition;
    /// The message passed to the macro.
    const char* message;
};

/// What the assert handler wants done after a failure.
enum class AssertAction : u8
{
    Abort,     ///< Terminate the process with `abort()`, so a debugger or crash handler sees the abort.
    Continue,  ///< Carry on after the macro. For tests and for [[ENGINE_ENSURE]]; code after a failed [[ENGINE_ASSERT]]
               ///< may assume the condition held, so only continue where it tolerates the violation.
    Break      ///< Stop in the debugger at the failing line; if the debugger resumes, execution carries on.
};

/// The function that decides what happens after a check fails. It must not itself fail an assertion.
using AssertHandler = AssertAction (*)(const AssertInfo& info);

/// The handler used until [[SetAssertHandler]] replaces it.
///
/// Prints one line to the write sink (file descriptor 1), `ASSERT FAILED: file:line function: condition: message`
/// (the leading words depend on the kind; empty parts are left out; truncated at 512 bytes, always ending in a
/// newline), then returns [[AssertAction::Break]] if a debugger is attached, otherwise [[AssertAction::Abort]] for
/// every kind except [[AssertKind::Ensure]], which gets [[AssertAction::Continue]].
///
/// The line is followed by the stack trace (see [[debug::FormatStackTrace]]), starting at the function that failed;
/// names are left mangled because demangling allocates. Called through a wrapper, the first lines of the trace are the
/// wrapper's frames. A custom handler can call it to keep the printed report and then choose its own action.
///
/// - Param info: the failure.
/// - Returns: the action described above.
/// - Thread-safety: thread-safe. Formats into a stack buffer; output goes through the shared write sink.
AssertAction DefaultAssertHandler(const AssertInfo& info);

/// Replaces the assert handler.
///
/// The handler is called on the thread that failed, with no lock held, so it may be called concurrently. Handlers that
/// unwind (throw) or jump out are allowed; the library keeps no state that needs cleanup across the call.
///
/// - Param handler: the new handler, or `nullptr` to restore [[DefaultAssertHandler]].
/// - Returns: the handler that was installed before.
/// - Thread-safety: thread-safe. The handler pointer is atomic; a failure on another thread sees the new handler.
AssertHandler SetAssertHandler(AssertHandler handler);

/// Reports an assertion failure through the handler chain and then aborts, whatever the handler returns.
///
/// Kept for code that wants to fail without a condition (the RHI null device uses it); prefer the macros.
///
/// - Param file: source file name, usually `__FILE__`.
/// - Param line: source line number.
/// - Param message: explanation text.
/// - Aborts: always; this function never returns. The process ends with `abort()`, so it dies with SIGABRT (earlier
/// versions
///   ended with exit code 1).
/// - Thread-safety: thread-safe.
[[noreturn]] void AssertFail(const char* file, int line, const char* message);

/// One [[ENGINE_ENSURE]] call site that has failed at least once.
///
/// - Thread-safety: thread-compatible. A plain value passed to an [[EnsureVisitor]].
/// - Ownership: the strings are literals with program lifetime.
struct EnsureSiteInfo
{
    /// Source file of the call site.
    const char* file;
    /// Source line of the call site.
    int line;
    /// Enclosing function of the call site.
    const char* function;
    /// The stringified condition.
    const char* condition;
    /// The message passed to the macro.
    const char* message;
    /// How many times the condition has failed there, including the one that was reported.
    u32 failure_count;
};

/// Receives each failed call site from [[ForEachFailedEnsure]].
using EnsureVisitor = void (*)(const EnsureSiteInfo& site, void* context);

/// Calls `visitor` for every [[ENGINE_ENSURE]] call site that has failed, most recently first reported first.
///
/// - Param visitor: called once per site.
/// - Param context: passed through to `visitor`; borrowed for the call.
/// - Thread-safety: thread-safe. Sites that fail while it runs may or may not be visited; counts are read atomically.
void ForEachFailedEnsure(EnsureVisitor visitor, void* context);

/// Returns how many distinct [[ENGINE_ENSURE]] call sites have failed.
/// - Thread-safety: thread-safe.
u32 EnsureFailedSiteCount();

/// Forgets every recorded ensure failure, so the next failure at each site is reported again. For tests.
/// - Thread-safety: not-thread-safe. Call only while no thread is inside an [[ENGINE_ENSURE]].
void ResetEnsureStateForTesting();

namespace detail
{

// The per-call-site record of ENGINE_ENSURE: a function-local static, constant-initialized, linked into a global list
// the first time the site fails.
struct EnsureSite
{
    const char* file;
    int line;
    const char* condition;
    const char* message;
    const char* function = nullptr;
    Atomic<u32> count{0};          // every failure; may wrap, which is harmless
    Atomic<bool> reported{false};  // set by the first failure; decides who links and reports, whatever `count` does
    EnsureSite* next = nullptr;
};

// Reports a failed check through the handler and acts on its answer. Returns Continue or Break; for Abort it does not
// return.
ENGINE_NOINLINE ENGINE_COLD AssertAction AssertFailed(AssertKind kind, const char* file, int line, const char* function,
                                                      const char* condition, const char* message);

// Ends the process after ENGINE_UNREACHABLE was reached (and a debugger, if any, was given its chance to look).
[[noreturn]] ENGINE_NOINLINE ENGINE_COLD void UnreachableAbort();

// Called when an ENGINE_ENSURE condition is false: counts it, and on the first failure at the site links the site into
// the list and reports it. Returns Break if the debugger should stop at the site, otherwise Continue.
ENGINE_NOINLINE ENGINE_COLD AssertAction EnsureFailed(EnsureSite& site, const char* function);

}  // namespace detail

}  // namespace vse

// For the static analyzer a failed assertion ends the path (the default handler aborts), so it does not report the code
// after an assert as reachable with the asserted condition false. The handler can be replaced to continue (tests do);
// that is a test's choice, not a path the analyzer should follow.
#if ENGINE_STATIC_ANALYSIS
#define ENGINE_ASSUME_ASSERT_HELD_() __builtin_unreachable()
#else
#define ENGINE_ASSUME_ASSERT_HELD_() static_cast<void>(0)
#endif

/// Checks `cond` in every build configuration; if it is false, reports the failure and, by default, aborts.
///
/// Use it for a contract the code relies on: a precondition whose violation is a bug in the caller. The failure goes to
/// the assert handler ([[SetAssertHandler]]); the default handler prints a line and aborts the process (or breaks into
/// an attached debugger).
///
/// - Param cond: expression that must be true; evaluated exactly once.
/// - Param msg: `const char*` message describing the violated condition; a string literal in practice.
/// - Aborts: `cond` evaluates to false (unless a test handler chose to continue).
#define ENGINE_ASSERT(cond, msg)                                                                                       \
    do                                                                                                                 \
    {                                                                                                                  \
        if (!(cond)) [[unlikely]]                                                                                      \
        {                                                                                                              \
            if (::vse::detail::AssertFailed(::vse::AssertKind::Assert, __FILE__, __LINE__, __func__, #cond, msg) ==    \
                ::vse::AssertAction::Break)                                                                            \
            {                                                                                                          \
                ENGINE_DEBUG_BREAK();                                                                                  \
            }                                                                                                          \
            ENGINE_ASSUME_ASSERT_HELD_();                                                                              \
        }                                                                                                              \
    } while (false)

/// Evaluates `cond` once and returns it; if it is false, reports the failure once for this call site and returns false.
///
/// Use it where the condition should hold but the caller can recover: `if (!ENGINE_ENSURE(p != nullptr, "no p"))
/// return;`. The first failure at each call site goes to the assert handler (the default handler prints a line and the
/// program goes on); later failures there are only counted. See [[ForEachFailedEnsure]]. Not usable in a `constexpr`
/// function.
///
/// - Param cond: expression; evaluated exactly once.
/// - Param msg: a string literal describing the condition.
/// - Returns: the value of `cond`.
#define ENGINE_ENSURE(cond, msg)                                                                                       \
    (static_cast<bool>(cond) || [](const char* engine_ensure_function_) ENGINE_NOINLINE_LAMBDA -> bool                 \
     {                                                                                                                 \
         static ::vse::detail::EnsureSite engine_ensure_site_{__FILE__, __LINE__, #cond, msg};                         \
         if (::vse::detail::EnsureFailed(engine_ensure_site_, engine_ensure_function_) == ::vse::AssertAction::Break)  \
         {                                                                                                             \
             ENGINE_DEBUG_BREAK();                                                                                     \
         }                                                                                                             \
         return false;                                                                                                 \
     }(__func__))

#define ENGINE_VERIFY_IMPL_1_(expr, msg)                                                                               \
    do                                                                                                                 \
    {                                                                                                                  \
        if (!(expr)) [[unlikely]]                                                                                      \
        {                                                                                                              \
            if (::vse::detail::AssertFailed(::vse::AssertKind::Verify, __FILE__, __LINE__, __func__, #expr, msg) ==    \
                ::vse::AssertAction::Break)                                                                            \
            {                                                                                                          \
                ENGINE_DEBUG_BREAK();                                                                                  \
            }                                                                                                          \
            ENGINE_ASSUME_ASSERT_HELD_();                                                                              \
        }                                                                                                              \
    } while (false)
#define ENGINE_VERIFY_IMPL_0_(expr, msg) static_cast<void>(expr)
// Pastes three tokens after expanding them, so a macro whose value is 0 or 1 can select ENGINE_..._IMPL_0_ or _1_.
#define ENGINE_PASTE3_(a, b, c) ENGINE_PASTE3_IMPL_(a, b, c)
#define ENGINE_PASTE3_IMPL_(a, b, c) a##b##c

/// Evaluates `expr` in every configuration and, when [[ENGINE_DEBUG_CHECKS]] is 1, checks that it is true.
///
/// [[ENGINE_DEBUG_CHECKS]] is a per-translation-unit setting, so this macro, [[ENGINE_CHECK_SLOW]] and
/// [[ENGINE_UNREACHABLE]] must not be used in header-inline code (the same inline function would differ between
/// translation units); a gate enforces it. Use [[ENGINE_ASSERT]] or [[ENGINE_ENSURE]] in headers.
///
/// For a call that must run for its effect but whose result should be true: `ENGINE_VERIFY(Init(), "init failed")`. In
/// a build without debug checks the expression still runs and the result is ignored.
///
/// - Param expr: expression; evaluated exactly once in every configuration.
/// - Param msg: a string literal describing the failure.
/// - Aborts: debug checks are on and `expr` evaluates to false.
#define ENGINE_VERIFY(expr, msg) ENGINE_PASTE3_(ENGINE_VERIFY_IMPL_, ENGINE_DEBUG_CHECKS, _)(expr, msg)

#define ENGINE_CHECK_SLOW_IMPL_1_(cond, msg)                                                                           \
    do                                                                                                                 \
    {                                                                                                                  \
        if (!(cond)) [[unlikely]]                                                                                      \
        {                                                                                                              \
            if (::vse::detail::AssertFailed(::vse::AssertKind::Assert, __FILE__, __LINE__, __func__, #cond, msg) ==    \
                ::vse::AssertAction::Break)                                                                            \
            {                                                                                                          \
                ENGINE_DEBUG_BREAK();                                                                                  \
            }                                                                                                          \
            ENGINE_ASSUME_ASSERT_HELD_();                                                                              \
        }                                                                                                              \
    } while (false)
// Off: the condition is still parsed and type-checked, but never evaluated.
#define ENGINE_CHECK_SLOW_IMPL_0_(cond, msg) static_cast<void>(sizeof(static_cast<bool>(cond)))

/// An expensive check, compiled out unless [[ENGINE_SLOW_CHECKS]] is 1.
///
/// When off, the condition is not evaluated (it is only type-checked), so it may be as slow as needed.
///
/// - Param cond: expression that must be true; evaluated once when on, never when off.
/// - Param msg: a string literal describing the violated condition.
/// - Aborts: slow checks are on and `cond` evaluates to false.
#define ENGINE_CHECK_SLOW(cond, msg) ENGINE_PASTE3_(ENGINE_CHECK_SLOW_IMPL_, ENGINE_SLOW_CHECKS, _)(cond, msg)

#define ENGINE_UNREACHABLE_IMPL_1_(msg)                                                                                \
    do                                                                                                                 \
    {                                                                                                                  \
        if (::vse::detail::AssertFailed(::vse::AssertKind::Unreachable, __FILE__, __LINE__, __func__, "", msg) ==      \
            ::vse::AssertAction::Break)                                                                                \
        {                                                                                                              \
            ENGINE_DEBUG_BREAK();                                                                                      \
        }                                                                                                              \
        ::vse::detail::UnreachableAbort(); /* a debugger that resumes does not get to continue */                      \
    } while (false)
#define ENGINE_UNREACHABLE_IMPL_0_(msg) ENGINE_UNREACHABLE_HINT()

/// Marks a place that must never be reached.
///
/// With debug checks on ([[ENGINE_DEBUG_CHECKS]] is 1) reaching it reports the failure and aborts (a handler cannot
/// make it continue, and a debugger that resumes after the break is followed by the abort). With them off it tells the
/// compiler the place is unreachable, which is undefined behavior if it is not.
///
/// - Param msg: a string literal saying why it cannot happen.
/// - Aborts: debug checks are on and the place is reached.
#define ENGINE_UNREACHABLE(msg) ENGINE_PASTE3_(ENGINE_UNREACHABLE_IMPL_, ENGINE_DEBUG_CHECKS, _)(msg)

/// Marks code that has not been written yet; reaching it reports the failure and, by default, aborts, in every
/// configuration.
///
/// - Aborts: the code is reached (unless a test handler chose to continue).
#define ENGINE_NOT_IMPLEMENTED()                                                                                       \
    do                                                                                                                 \
    {                                                                                                                  \
        if (::vse::detail::AssertFailed(::vse::AssertKind::NotImplemented, __FILE__, __LINE__, __func__, "",           \
                                        "not implemented") == ::vse::AssertAction::Break)                              \
        {                                                                                                              \
            ENGINE_DEBUG_BREAK();                                                                                      \
        }                                                                                                              \
    } while (false)
