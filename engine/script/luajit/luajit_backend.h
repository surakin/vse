// The LuaJIT backend (Phase 27 Day 448): one `lua_State` per script state behind the language-neutral interface. This
// header names no Lua type; only the files of this directory include LuaJIT's headers (`luajit_boundary_check`).
#pragma once

#include "script/backend.h"
#include "script/script_settings.h"

namespace vse::script::luajit
{

/// How a backend's states run scripts.
///
/// - Thread-safety: thread-compatible plain data.
struct LuaJitOptions
{
    /// Whether states run budgeted: the JIT is off, and a call may carry an instruction budget and a time budget
    /// (a count hook checks them). This is the mode for mods and other untrusted scripts. When false the states are
    /// trusted: the JIT is on (a count hook cannot stop compiled loops) and instruction and time budgets are refused
    /// with `Unsupported`. The memory budget works in both modes.
    bool budgeted = true;
    /// Whether a trusted state may use the JIT compiler (`script.jit`). A budgeted state never does, and a build with
    /// `ENGINE_LUAJIT_JIT=OFF` has no compiler to use; [[script::State::Caps]] reports what a state really does.
    bool jit = true;
    /// The most kilobytes of machine code one state keeps (`script.jit_code_kb`); 0 is LuaJIT's default.
    u32 jit_code_kb = 0;
    /// The seed of `math.random` in each new state (every state starts from the same seed, so runs repeat).
    u32 random_seed = 1;
};

/// The options the settings ask for: `script.budget`, `script.jit`, `script.jit_code_kb` and `script.random_seed`.
///
/// - Param registry: a registry in which [[script::RegisterScriptSettings]] was called (missing settings give the
/// defaults).
/// - Returns: the options.
/// - Fails: never.
LuaJitOptions OptionsFromSettings(const settings::Registry& registry);

/// Whether this build of LuaJIT contains the JIT compiler (`ENGINE_LUAJIT_JIT`, default ON; OFF defines
/// `LUAJIT_DISABLE_JIT`, for platforms that forbid run-time code generation).
///
/// - Returns: true when states can compile.
/// - Fails: never.
bool JitCompiledIn();

/// Creates the LuaJIT backend.
///
/// Its states hold one LuaJIT state each, allocate from the engine heap through a counting allocator (so the memory
/// cap is exact for Lua objects), run every call as a coroutine (so `wait`, `wait_frames` and `wait_event` suspend),
/// and report errors with a script stack trace. A state exposes a whitelist environment (no files, no operating system,
/// no loaders, `print` goes to the log) and its pattern functions stop after a step limit. Numbers are Lua numbers: an
/// integer value outside +-2^53 is refused with `Invalid`, and a float with an integral value within that range returns
/// as an integer. A map returns with its keys sorted by bytes. An instruction budget is counted in blocks of 1000
/// instructions.
///
/// - Param options: how its states run; copied.
/// - Returns: the backend.
/// - Thread-safety: thread-safe; the backend is immutable and its states are independent.
/// - Ownership: the caller owns the backend, which must outlive its states' creation calls only.
/// - Fails: never.
UniquePtr<Backend> CreateLuaJitBackend(const LuaJitOptions& options = LuaJitOptions());

/// Registers the backend as `luajit` (budgeted, default options) in the backend registry; true when it is registered
/// (also when it already was).
///
/// - Thread-safety: thread-safe.
/// - Fails: returns false when the registry is full.
bool RegisterLuaJitBackend();

}  // namespace vse::script::luajit
