// script_expr (Phase 27 Day 447): a minimal second language, written against the backend interface only. It exists to
// prove the seam (the conformance suite runs on it and on LuaJIT) and as the worked example in the guide; it is not for
// gameplay.
//
// The language, one statement per line (or separated by `;`), `#` starts a comment:
//   x = 1 + 2 * 3            at the top level of a chunk: assign a global
//   def f(a, b)  ...  end    define a function; inside one, `x = ...` makes a local and `global x = ...` writes a
//   global if c ... else ... end    while c ... end        return e1, e2, ... f(1, "two", true, nil)   call a script
//   function or a registered native wait(seconds)  wait_frames(n)  wait_event("name")   suspend the call (the value of
//   the call is the resumed value)
// Values: nil, true, false, integers, floats, strings. Operators: + - * / % == != < <= > >= and or not, unary minus.
// `/` is a float division; `%` is on integers; `+` joins two strings. Other value kinds pass through parameters,
// returns and natives unchanged.
#pragma once

#include "containers/unique_ptr.h"
#include "script/backend.h"

namespace vse::script::expr
{

/// Makes the `expr` backend.
///
/// The backend honors instruction and wall-clock budgets and supports suspension; it does not count memory (a string
/// longer than 1 MiB, a call stack deeper than 64 frames or a value stack larger than 65,536 values is a runtime error
/// instead), has no JIT and no bytecode form.
///
/// - Returns: the backend.
/// - Thread-safety: the backend is thread-safe; its states follow the [[script::State]] owner rule.
/// - Ownership: returns a new backend.
/// - Fails: never.
UniquePtr<Backend> CreateExprBackend();

/// Registers `expr` with the backend registry (idempotent).
///
/// - Returns: true if it is registered (now or already).
/// - Thread-safety: thread-safe.
/// - Fails: returns false when the registry is full.
bool RegisterExprBackend();

}  // namespace vse::script::expr
