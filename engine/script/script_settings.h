// The script settings (Phase 27 Day 447): which language backend a world uses. Later days add the partition count, the
// budget mode and the JIT switch.
#pragma once

#include "settings/registry.h"
#include "string/string.h"
#include "mem/heap_allocator.h"

namespace engine_placeholder::script
{

/// Declares the script settings: `script.backend` (text, default `luajit`).
///
/// Each has an environment variable (`ENGINE_SCRIPT_BACKEND`) and a flag (`--script-backend`), read when a script
/// world is made.
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

}  // namespace engine_placeholder::script
