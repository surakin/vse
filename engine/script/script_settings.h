// The script settings (Phase 27 Day 447): which language backend a world uses. Later days add the partition count, the
// budget mode and the JIT switch.
#pragma once

#include "settings/registry.h"
#include "string/string.h"
#include "mem/heap_allocator.h"

namespace engine_placeholder::script
{

/// Declares the script settings: the backend, the budget mode and the random seed.
///
/// They are `script.backend` (text, default `luajit`), `script.budget` (`budgeted` or `trusted`, default `budgeted`)
/// and `script.random_seed` (default 1). Each has an environment variable (`ENGINE_SCRIPT_BACKEND`,
/// `ENGINE_SCRIPT_BUDGET`, `ENGINE_SCRIPT_RANDOM_SEED`) and a flag (`--script-backend`, `--script-budget`,
/// `--script-random-seed`), read when a script world is made.
///
/// - Param registry: the registry to declare them in; not null.
/// - Ownership: borrowed for the call.
/// - Fails: never.
void RegisterScriptSettings(settings::Registry* registry);

/// Reads `script.backend` from a registry.
///
/// - Param registry: a registry in which [[script::RegisterScriptSettings]] was called; a missing setting gives the
/// default, `luajit`.
/// - Returns: the backend name; the name is not checked against the backend registry (that is `CreateBackend`'s
/// refusal).
/// - Ownership: returns a new string.
/// - Fails: never.
String<HeapAllocator> ScriptBackendName(const settings::Registry& registry);

/// Reads `script.budget`: whether scripts run budgeted (no JIT, instruction and time limits) or trusted.
///
/// - Param registry: a registry in which [[script::RegisterScriptSettings]] was called; a missing setting, or any value
/// other than `trusted`, gives `true`.
/// - Returns: true for budgeted.
/// - Fails: never.
bool ScriptBudgeted(const settings::Registry& registry);

/// Reads `script.random_seed`.
///
/// - Param registry: a registry in which [[script::RegisterScriptSettings]] was called; a missing setting gives 1.
/// - Returns: the seed.
/// - Fails: never.
u32 ScriptRandomSeed(const settings::Registry& registry);

}  // namespace engine_placeholder::script
