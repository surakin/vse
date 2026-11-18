// The script settings (Phase 27 Day 447): which language backend a world uses. Later days add the partition count, the
// budget mode and the JIT switch.
#pragma once

#include "settings/registry.h"
#include "string/string.h"
#include "mem/heap_allocator.h"

namespace engine_placeholder::script
{

/// Declares the script settings: the backend, the budget mode, the random seed, the log limit, the partition count and
/// the partition memory cap.
///
/// They are `script.backend` (text, default `luajit`), `script.budget` (`budgeted` or `trusted`, default `budgeted`)
/// and `script.random_seed` (default 1). Each has an environment variable (`ENGINE_SCRIPT_BACKEND`,
/// `ENGINE_SCRIPT_BUDGET`, `ENGINE_SCRIPT_RANDOM_SEED`, `ENGINE_SCRIPT_PARTITIONS`, `ENGINE_SCRIPT_PARTITION_MEMORY`)
/// and a flag (`--script-backend`, `--script-budget`,
/// `--script-random-seed`, `--script-partitions`, `--script-partition-memory`), read when a script world is made.
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

/// Reads `script.conflicts`: whether a write conflict between partitions is an error.
///
/// - Param registry: a registry in which [[script::RegisterScriptSettings]] was called; a missing setting, or any value
/// other than `error`, gives `false` (last wins).
/// - Returns: true for `error`.
/// - Fails: never.
bool ScriptConflictsAreErrors(const settings::Registry& registry);

/// Reads `script.log_limit`: how many `log.*` messages one script may write per tick.
///
/// - Param registry: a registry in which [[script::RegisterScriptSettings]] was called; a missing setting gives 64.
/// - Returns: the limit; 0 is no limit.
/// - Fails: never.
u32 ScriptLogLimit(const settings::Registry& registry);

/// Reads `script.partitions`: how many partitions a script world has.
///
/// - Param registry: a registry in which [[script::RegisterScriptSettings]] was called; a missing setting gives 64.
/// - Returns: 1 to 1024.
/// - Fails: never.
u32 ScriptPartitions(const settings::Registry& registry);

/// Reads `script.partition_memory`: the memory cap of one partition's state, in bytes.
///
/// - Param registry: a registry in which [[script::RegisterScriptSettings]] was called; a missing setting gives 64 MiB.
/// - Returns: 1 MiB to 4 GiB.
/// - Fails: never.
u64 ScriptPartitionMemory(const settings::Registry& registry);

}  // namespace engine_placeholder::script
