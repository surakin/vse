// Script call status (Phase 27 Day 446): how a call into a language state ended. Errors are values: a hostile or broken
// script never aborts the process.
#pragma once

#include "containers/vector.h"
#include "mem/heap_allocator.h"
#include "string/string.h"
#include "string_view.h"
#include "types.h"

namespace engine_placeholder::script
{

/// How a script call ended.
///
/// - Thread-safety: thread-compatible plain data.
enum class Status : u8
{
    /// The call succeeded.
    Ok,
    /// The chunk's source did not parse.
    SyntaxError,
    /// The script raised an error while running (including a call to a function that does not exist).
    RuntimeError,
    /// The instruction or wall-clock budget ran out; the state stays usable but the call did not finish.
    BudgetExceeded,
    /// The state's memory cap (or the heap) refused an allocation.
    OutOfMemory,
    /// The arguments are not valid for the call (a [[script::Value]] over its limits, a name that is empty or too long,
    /// a coroutine id that is not suspended).
    Invalid,
    /// The backend cannot do what was asked (a budget it cannot honor, a suspension without coroutines); never silently
    /// ignored.
    Unsupported,
};

/// The name of a status, for logs and test output (`"Ok"`, `"SyntaxError"`, ...).
///
/// - Thread-safety: thread-safe.
/// - Ownership: the returned text is a literal that lives for the whole program.
/// - Fails: never.
inline const char* StatusName(Status status)
{
    switch (status)
    {
    case Status::Ok: return "Ok";
    case Status::SyntaxError: return "SyntaxError";
    case Status::RuntimeError: return "RuntimeError";
    case Status::BudgetExceeded: return "BudgetExceeded";
    case Status::OutOfMemory: return "OutOfMemory";
    case Status::Invalid: return "Invalid";
    case Status::Unsupported: return "Unsupported";
    }
    return "Unknown";
}

/// The result of a script call: a status, and for a failure a message and a stack trace.
///
/// - Thread-safety: thread-compatible.
/// - Ownership: owns its two strings; movable, not copyable.
struct Outcome
{
    /// How the call ended.
    Status status = Status::Ok;
    /// What went wrong, in the backend's words (empty when `status` is `Ok`).
    String<HeapAllocator> message;
    /// The script stack at the failure, one frame per line, innermost first (empty when none is known).
    String<HeapAllocator> trace;

    /// Creates a success.
    Outcome() : message(HeapAllocator::Default()), trace(HeapAllocator::Default()) {}
    /// Creates a failure (or a success with `status` `Ok`) with a message.
    Outcome(Status status_, StringView message_)
        : status(status_), message(HeapAllocator::Default(), message_), trace(HeapAllocator::Default())
    {
    }

    /// Whether the call succeeded.
    ///
    /// - Fails: never.
    bool IsOk() const { return status == Status::Ok; }
};

}  // namespace engine_placeholder::script
