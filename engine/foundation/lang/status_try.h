#pragma once

//! [[ENGINE_TRY]]: propagate a failed status to the caller in one line. The engine reports errors as values (exceptions are off), and this is the
//! early return that makes a sequence of fallible calls readable.

/// Evaluates `expr` once; if the resulting status `Failed()`, returns it from the enclosing function. Works for any status type with a `Failed()` member
/// when the enclosing function returns that same type (`database::Status`, `storage::Status`). It hides a `return`: use it only for pure propagation, and
/// keep an explicit `if` where the function must clean up before it returns.
///
/// - Param expr: an expression whose value is a status.
/// - Fails: returns the status of `expr` from the enclosing function when it failed.
#define ENGINE_TRY(expr)                                    \
    do {                                                    \
        const auto engine_try_status_ = (expr);             \
        if (engine_try_status_.Failed()) {                  \
            return engine_try_status_;                      \
        }                                                   \
    } while (0)
