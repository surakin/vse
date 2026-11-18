// The LuaJIT backend (Phase 27 Day 448): one `lua_State` per script state behind the language-neutral interface. This
// header names no Lua type; only the files of this directory include LuaJIT's headers (`luajit_boundary_check`).
#pragma once

#include "script/backend.h"

namespace engine_placeholder::script::luajit
{

/// Creates the LuaJIT backend.
///
/// Its states hold one LuaJIT state each, allocate from the engine heap through a counting allocator (so the memory
/// cap is exact for Lua objects), run every call as a coroutine (so `wait`, `wait_frames` and `wait_event` suspend),
/// and report errors with a script stack trace. Numbers are Lua numbers: an integer value outside +-2^53 is refused
/// with `Invalid`, and a float with an integral value within that range returns as an integer. A map returns with its
/// keys sorted by bytes. Budgets are not honored yet and are refused with `Unsupported` (Day 449).
///
/// - Returns: the backend.
/// - Thread-safety: thread-safe; the backend is immutable and its states are independent.
/// - Ownership: the caller owns the backend, which must outlive its states' creation calls only.
/// - Fails: never.
UniquePtr<Backend> CreateLuaJitBackend();

/// Registers the backend as `luajit` in the backend registry; true when it is registered (also when it already was).
///
/// - Thread-safety: thread-safe.
/// - Fails: returns false when the registry is full.
bool RegisterLuaJitBackend();

}  // namespace engine_placeholder::script::luajit
