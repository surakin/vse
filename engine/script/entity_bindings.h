// The entity and transform bindings (Phase 27 Day 455): `entity.*` and `transform.*` over the Phase 17 components
// (`LocalTransform`, `WorldTransform`, `Parent`). Reads see the tick-start world plus the script's own writes (the
// overlay); writes are commands applied by the merge (docs/guide/scripting.md, "Entities and transforms").
#pragma once

#include "ecs/world.h"
#include "script/bindings.h"
#include "script/merge.h"
#include "string_view.h"
#include "types.h"

namespace vse::script
{

/// The command kinds the entity and transform bindings record (see [[script::RegisterEntityCommandKinds]]).
enum EntityCommandKind : u16
{
    /// Sets an entity's local position.
    kCommandSetPosition = 10,
    /// Sets an entity's local rotation.
    kCommandSetRotation = 11,
    /// Sets an entity's local scale.
    kCommandSetScale = 12,
    /// Creates an entity (target: a reserved placeholder; payload: the prefab name, possibly empty).
    kCommandSpawn = 13,
    /// Destroys an entity and everything below it.
    kCommandDestroy = 14,
    /// Sets or clears an entity's parent (payload: the parent entity; the null entity clears it).
    kCommandSetParent = 15,
};

/// The overlay fields the transform bindings use (see [[script::Overlay]]).
enum TransformField : u32
{
    /// The local position, as a `Vec3` value.
    kFieldPosition = 0x101,
    /// The local rotation, as a `Quat` value.
    kFieldRotation = 0x102,
    /// The local scale, as a `Vec3` value.
    kFieldScale = 0x103,
};

/// The names the scene gives its entities and its prefabs, which the script module cannot know (a level keeps them in
/// `asset::EntityName`; the host adapts). Every function is optional.
///
/// - Thread-safety: thread-compatible plain data; the functions must be callable from every partition's thread at once
/// (they read the tick-start world).
/// - Ownership: `user` is borrowed and must outlive the scene's use by scripts.
struct SceneServices
{
    /// Writes the name of `entity` into `out` (at most `capacity` bytes, no terminator needed) and returns its length;
    /// 0 for an entity without a name.
    usize (*name_of)(void* user, const ecs::World& world, ecs::Entity entity, char* out, usize capacity) = nullptr;
    /// Returns the entity with this name, or the null entity.
    ecs::Entity (*find)(void* user, const ecs::World& world, StringView name) = nullptr;
    /// Whether `prefab` names a prefab `entity.spawn` may use.
    bool (*has_prefab)(void* user, StringView prefab) = nullptr;
    /// Passed to every function.
    void* user = nullptr;
};

/// Registers the commands that apply the entity and transform writes at the merge (kinds 10 to 15). The merge's `user`
/// should be a [[script::MergeServices]] when prefabs are used.
///
/// - Param kinds: the table to add to; not null.
/// - Returns: false when an id is taken.
/// - Ownership: borrowed for the call.
/// - Fails: returns false, as described.
bool RegisterEntityCommandKinds(CommandKindTable* kinds);

/// Adds `entity.spawn/destroy/valid/self/name/find/set_parent` and `transform.get/position/rotation/scale/world/
/// set_position/set_rotation/set_scale/translate`. Reads are `world-read`, writes `world-write`.
///
/// - Param registry: the registry to add to; not null.
/// - Returns: true when all were added.
/// - Ownership: borrowed for the call.
/// - Fails: returns false when a name is taken.
bool RegisterEntityBindings(BindingRegistry* registry);

}  // namespace vse::script
