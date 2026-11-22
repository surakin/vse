#pragma once
//! The scene bindings of a world's scripts: `environment.set`, `light.set` and `light.create`. Internal to the module
//! `engine_c` (the environment and the light component are the C API's own data), built only where the script module
//! is. A script changes the sky, the ambient light and exposure of its world, and the colour, intensity and reach of
//! its lights; the changes are commands applied at the sync point, so every surface shows them from its next frame.
#include "ecs/entity.h"
#include "script/bindings.h"
#include "script/merge.h"
#include "types.h"

namespace vse::engine_c
{

struct WorldObject;

/// The command kinds the scene bindings record (ids the other bindings do not use).
///
/// - Thread-safety: thread-compatible plain data.
enum SceneCommandKind : u16
{
    kCommandEnvironmentSet = 130,  ///< `environment.set`: a global command (it names no entity).
    kCommandLightSet = 131,        ///< `light.set`: changes fields of an entity's light.
    kCommandLightCreate = 132,     ///< `light.create`: puts a light on an entity.
};

/// What the scene bindings need: its address goes in `MergeServices::extensions[script::kServiceScene]`.
///
/// - Thread-safety: thread-compatible plain data; the frame holds the world's lock while scripts run and merge.
/// - Ownership: borrows the world, which must outlive the runtime.
struct SceneScriptServices
{
    WorldObject* object = nullptr;  ///< The world whose environment and lights the bindings change.
};

/// Registers `environment.set`, `light.set` and `light.create`.
/// - Returns: false when a name is already taken.
bool RegisterSceneBindings(script::BindingRegistry* registry);

/// Registers the command kinds of the scene bindings.
/// - Returns: false when an id is already taken.
bool RegisterSceneCommandKinds(script::CommandKindTable* kinds);

}  // namespace vse::engine_c
