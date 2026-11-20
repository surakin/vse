// The pattern-matching string functions with a step limit (Phase 27 Day 449). LuaJIT's `string.find`, `match`,
// `gmatch` and `gsub` can backtrack for a minute inside one C call, where no hook can stop them; these replace them
// with the same semantics (Lua 5.1 patterns) and an error once a call has taken too many matcher steps.
#pragma once

struct lua_State;

namespace vse::script::luajit
{

/// Replaces `string.find`, `string.match`, `string.gmatch` and `string.gsub` in the `string` table at the top of the
/// stack with the step-limited versions. May raise a Lua error on out-of-memory (call it under a protected call).
///
/// - Fails: never returns a failure; raises only on out-of-memory.
void InstallSafePatterns(lua_State* L);

}  // namespace vse::script::luajit
