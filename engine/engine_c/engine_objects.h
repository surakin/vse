#pragma once
//! The count of objects the C API has handed out (Phase 23 Day 348). Internal to the module `engine_c`: every kind of
//! object a later day adds (a world, a loaded resource, an import) takes one when it is created and gives it back when
//! the host releases it, and `engine_destroy` refuses while any is out.

namespace vse::engine_c
{

/// Counts one more outstanding object on the engine that exists.
///
/// - Returns: false if no engine exists (nothing is counted).
/// - Thread-safety: thread-safe.
/// - Fails: reports a missing engine by returning false.
bool EngineObjectAcquire();

/// Gives one object back.
///
/// - Thread-safety: thread-safe.
/// - Fails: never; with no engine or a zero count it does nothing.
void EngineObjectRelease();

}  // namespace vse::engine_c
