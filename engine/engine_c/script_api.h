#pragma once
//! The script half of the C API (Phase 27 Day 471): a world's script runtime, the values that cross the boundary and
//! the table functions. Internal to the module `engine_c`. A library built without the script module (a platform with
//! no LuaJIT yet) answers every function with UNSUPPORTED and has no `ENGINE_CAP_SCRIPT`.
#include "build_defs.h"
#include "containers/array.h"
#include "containers/unique_ptr.h"
#include "ecs/entity.h"
#include "ecs/world.h"
#include "engine/engine_c.h"
#include "settings/registry.h"
#include "types.h"

namespace engine_placeholder::engine_c
{

class Runtime;
struct WorldObject;

/// What the `script.*` settings said when the engine was created; every world's script runtime is made from it.
///
/// - Thread-safety: thread-compatible plain data.
struct ScriptSettingValues
{
    u32 partitions = 8;        ///< `script.partitions`.
    bool budgeted = true;      ///< `script.budget`: the interpreter with an instruction budget (no JIT).
    bool jit = true;           ///< `script.jit`.
    u32 jit_code_kb = 0;       ///< `script.jit_code_kb`.
    u32 random_seed = 1;       ///< `script.random_seed`.
    u64 partition_memory = 0;  ///< `script.partition_memory` in bytes (0: the backend's default).
    /// `script.physics_layers`: the names scripts give physics layers (its first `physics_layers_length` bytes).
    Array<char, 1024> physics_layers{};
    usize physics_layers_length = 0;  ///< The text's length (0: the setting is not declared).
};

/// Declares the `script.*` settings in the engine's registry (nothing in a library without scripts).
void RegisterScriptSettingsInEngine(settings::Registry* registry);

/// Reads the `script.*` settings the worlds' runtimes are made from.
///
/// - Param registry: the engine's registry after `RegisterScriptSettingsInEngine`.
ScriptSettingValues ReadScriptSettings(const settings::Registry& registry);

/// Whether this library has the script module (the build defined `ENGINE_C_WITH_SCRIPT`).
///
/// - Fails: never.
bool ScriptBuilt();

/// A world's scripts: the runtime, the host functions and the budget, made by the first use. The details are in
/// `script_api.cpp`, so that only it needs the script module's headers.
///
/// - Thread-safety: thread-compatible; the runtime's world mutex guards every use.
/// - Ownership: owned by its [[engine_c::WorldObject]].
class ScriptHost
{
public:
    /// Makes an empty host.
    ScriptHost();
    /// Destroys the runtime (and with it the states).
    ~ScriptHost();
    ScriptHost(const ScriptHost&) = delete;
    ScriptHost& operator=(const ScriptHost&) = delete;

    struct Impl;
    UniquePtr<Impl> impl;  ///< Everything the host holds.
};

/// Runs the world's scripts for one frame (the callbacks of its behaviors, the merge, the events), on the calling
/// thread. Does nothing for a world with no runtime.
///
/// - Param object: the world; the runtime's world mutex is held.
/// - Param dt: the frame's time in seconds.
/// - Fails: never; a callback that fails is counted and logged.
void ScriptTickWorld(WorldObject& object, f64 dt);

/// Hands the frame's physics events (contacts and triggers) to the world's scripts (`on_collision_*`, `on_trigger_*`),
/// after the physics step (C API script physics, 2026-11-20). Does nothing for a world with no runtime.
///
/// - Param object: the world; the runtime's world mutex is held.
/// - Fails: never.
void ScriptPhysicsEvents(WorldObject& object);

/// Sets or replaces the script component of an entity.
///
/// - Param runtime: the engine's runtime (for the settings values).
/// - Param object: the world; the world mutex is held.
/// - Returns: OK, or INVALID_ARGUMENT (a name that is empty or has no NUL in 64 bytes, params that are not a map or
/// break the limits), STRUCT_SIZE, LIMIT_EXCEEDED, UNSUPPORTED.
engine_status ScriptComponentSet(Runtime& runtime, WorldObject& object, ecs::Entity entity, const void* value);

/// Reads the script component of an entity into an `engine_script_component` (its `params` is NULL).
///
/// - Returns: OK, NOT_FOUND (no such component), UNSUPPORTED.
engine_status ScriptComponentGet(WorldObject& object, ecs::Entity entity, void* out);

/// Removes the script component of an entity.
///
/// - Returns: OK, NOT_FOUND, UNSUPPORTED.
engine_status ScriptComponentRemove(WorldObject& object, ecs::Entity entity);

/// The component id of the script component, for queries and `component_has`.
///
/// - Returns: the id, or `ecs::kInvalidComponent` when the library has no script module.
ecs::ComponentId ScriptComponentId();

/// The table functions.
///
/// - Thread-safety: as the header says for each.
engine_status ScriptLoadFn(engine_handle world, const engine_script_load* info, u64* out_script);
/// See `engine_api.script_call`.
engine_status ScriptCallFn(engine_handle world, engine_string function, const engine_value_pool* args,
                           engine_value_pool* results);
/// See `engine_api.script_register_function`.
engine_status ScriptRegisterFunctionFn(engine_handle world, engine_string name, u32 arity, u32 flags,
                                       engine_script_fn function, void* user);
/// See `engine_api.script_set_budget`.
engine_status ScriptSetBudgetFn(engine_handle world, const engine_script_budget* budget);
/// See `engine_api.script_stats`.
engine_status ScriptStatsFn(engine_handle world, engine_script_stats* out);
/// See `engine_api.script_event_emit`.
engine_status ScriptEventEmitFn(engine_handle world, engine_string name, const engine_value_pool* payload, u64 target);
/// See `engine_api.script_event_subscribe`.
engine_status ScriptEventSubscribeFn(engine_handle world, engine_string name, engine_script_event_fn callback,
                                     void* user, u32* out_id);
/// See `engine_api.script_event_unsubscribe`.
engine_status ScriptEventUnsubscribeFn(engine_handle world, u32 id);
/// See `engine_api.script_task_run`.
engine_status ScriptTaskRunFn(engine_handle world, const engine_script_task* task, engine_value_pool* results);

}  // namespace engine_placeholder::engine_c
