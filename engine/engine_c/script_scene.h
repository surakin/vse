#pragma once
//! The scene bindings of a world's scripts: `environment.set`, `light.set` and `light.create`. Internal to the module
//! `engine_c` (the environment, the light component and the world's instanced sets and trees are the C API's own data),
//! built only where the script module is. A script changes the sky, the ambient light and exposure of its world, and
//! the colour, intensity and reach of its lights; the changes are commands applied at the sync point, so every surface
//! shows them from its next frame.
#include "ecs/entity.h"
#include "script/bindings.h"
#include "script/entity_bindings.h"
#include "script/merge.h"
#include "sync/mutex.h"
#include "types.h"

namespace vse::engine_c
{

struct WorldObject;

/// The command kinds the scene bindings record (ids the other bindings do not use).
///
/// - Thread-safety: thread-compatible plain data.
enum SceneCommandKind : u16
{
    kCommandEnvironmentSet = 130,   ///< `environment.set`: a global command (it names no entity).
    kCommandLightSet = 131,         ///< `light.set`: changes fields of an entity's light.
    kCommandLightCreate = 132,      ///< `light.create`: puts a light on an entity.
    kCommandInstancesAdd = 133,     ///< `instances.add`: a global command, applied in the merge order.
    kCommandInstancesSet = 134,     ///< `instances.set`: moves one instance of a set.
    kCommandInstancesRemove = 135,  ///< `instances.remove`: removes one instance of a set.
    kCommandTreeSetLocal = 136,     ///< `tree.set_local`: sets a node's local transform.
    kCommandSkyCubeSet = 137,       ///< `environment.set_sky_cube`: names the texture resource of a cube sky.
};

/// What the scene bindings need: its address goes in `MergeServices::extensions[script::kServiceScene]`.
///
/// - Thread-safety: thread-compatible plain data; the frame holds the world's lock while scripts run and merge.
/// - Ownership: borrows the world, which must outlive the runtime.
struct SceneScriptServices
{
    WorldObject* object = nullptr;  ///< The world whose environment and lights the bindings change.
};

/// The names a world's scripts see (`entity.name` and `entity.find`): the entity names of the C API, the ones
/// `entity_name_set` writes and `level_spawn` gives its nodes. An entity made by a script's `entity.spawn` is named
/// from the merge on, if at all.
///
/// - Thread-safety: the functions may be called from every partition at once (a lookup holds `mutex`: a query is not
///   made by two threads at once).
/// - Ownership: the world is borrowed per call; `services.user` points at this object, which must stay where it is.
struct SceneNames
{
    SceneNames();
    SceneNames(const SceneNames&) = delete;
    SceneNames& operator=(const SceneNames&) = delete;

    Mutex mutex;                     ///< Guards the query of a lookup.
    script::SceneServices services;  ///< Give its address to `ScriptRuntimeConfig::scene`.
};

/// Registers `environment.set`, `light.set`, `light.create`, `instances.add`, `instances.set`, `instances.remove` and
/// `tree.set_local`.
/// - Returns: false when a name is already taken.
bool RegisterSceneBindings(script::BindingRegistry* registry);

/// Registers the command kinds of the scene bindings.
/// - Returns: false when an id is already taken.
bool RegisterSceneCommandKinds(script::CommandKindTable* kinds);

}  // namespace vse::engine_c
