// The physics bindings (Phase 27 Day 466): scripts drive and question the physics world. This is a separate target
// above `script` and `physics` (layer 14), so `script` never includes `physics`; the host registers the bindings, the
// command kinds, the components and the services below with a [[script::ScriptRuntime]]
// (docs/guide/scripting.md, "Physics").
//
// The frame order is fixed (the host's schedule keeps it; the test `script_physics_test` checks it):
//
//  1. the script phase: scripts read the world and the physics world as of the tick start (the world is read-only, the
//     physics world is not stepping, so a query is safe from every partition at once), and record commands;
//  2. the merge: the commands are applied in the fixed order, the physics commands to the physics world through
//     `physics::BodyLink` (forces, impulses, velocities, wake and sleep) or to the components (`gravity_scale`,
//     joints);
//  3. `physics::SyncBodies` and `physics::SyncJoints`: the physics world follows the components;
//  4. `physics::DriveKinematicBodies` (a script moves a kinematic body by setting its transform);
//  5. the physics step (`PhysicsWorld::Update`): each fixed step first moves the characters (`physics::CharacterSet`),
//  then
//     steps; then the write-back of the transforms (`physics::CharacterSet::WriteBack` for the characters), transform
//     propagation and the render sync;
//  6. `EmitPhysicsEvents`: the step's contact and trigger events go to the event bus and reach the scripts that
//     listen at the start of the next script phase.
//
// A script therefore sees the results of the last step, and what it asks for happens in the next one.
#pragma once

#include "physics/world.h"
#include "script/bindings.h"
#include "script/component_catalog.h"
#include "script/merge.h"
#include "script/service_slots.h"
#include "physics/character_set.h"
#include "script_physics/physics_layers.h"
#include "storage/component_registry.h"
#include "types.h"

namespace vse::script_physics
{

/// The command kinds the physics bindings record (ids the built-in kinds do not use).
///
/// - Thread-safety: thread-compatible plain data.
enum PhysicsCommandKind : u16
{
    kCommandPhysicsForce = 100,         ///< `physics.add_force`, `add_force_at`: summed in the merge order.
    kCommandPhysicsImpulse = 101,       ///< `physics.add_impulse`, `add_impulse_at`: summed in the merge order.
    kCommandPhysicsTorque = 102,        ///< `physics.add_torque`: summed in the merge order.
    kCommandPhysicsSetLinear = 103,     ///< `physics.set_velocity`.
    kCommandPhysicsSetAngular = 104,    ///< `physics.set_angular_velocity`.
    kCommandPhysicsGravityScale = 105,  ///< `physics.set_gravity_scale`: changes the `rigid_body` component.
    kCommandPhysicsWake = 106,          ///< `physics.wake`.
    kCommandPhysicsSleep = 107,         ///< `physics.sleep`.
    kCommandPhysicsAddJoint = 108,      ///< `physics.add_joint`: puts a `physics::Joint` on an entity.
    kCommandPhysicsRemoveJoint = 109,   ///< `physics.remove_joint`: takes it off.
    kCommandCharacterCreate = 110,      ///< `character.create`: a character body at the entity's transform.
    kCommandCharacterDestroy = 111,     ///< `character.destroy`.
    kCommandCharacterMove = 112,        ///< `character.move`: the wished horizontal velocity (holds until changed).
    kCommandCharacterJump = 113,        ///< `character.jump`: at the start of the next step.
    kCommandCharacterHeight = 114,      ///< `character.set_height`: at the start of the next step.
    kCommandCharacterPosition = 115,    ///< `character.set_position`: a teleport, at the merge (C API script physics).
};

/// The stored ids of the physics components scripts can name (never change or reuse one).
///
/// - Thread-safety: thread-compatible plain data.
enum PhysicsComponentId : u32
{
    kRigidBodyId = 100,  ///< `physics::RigidBody`, name `rigid_body`.
    kVelocityId = 101,   ///< `physics::Velocity`, name `velocity`.
    kColliderId = 102,   ///< `physics::Collider`, name `collider`.
};

/// What the physics bindings need: put its address in `MergeServices::extensions[script::kServicePhysics]`.
///
/// - Thread-safety: thread-compatible plain data; scripts read the world during the script phase, the merge changes
/// it; the two never overlap.
/// - Ownership: borrows both; they must outlive the runtime.
struct PhysicsScriptServices
{
    /// The physics world the bindings read and the merge changes.
    physics::PhysicsWorld* world = nullptr;
    /// The layer names; not null (a table that holds `default` is one `Parse("")` away).
    const PhysicsLayers* layers = nullptr;
    /// The characters scripts create and drive (`character.*`); null when the host has none (the calls then fail with
    /// "this world has no characters").
    physics::CharacterSet* characters = nullptr;
};

/// Registers `rigid_body`, `velocity` and `collider` in the storage registry, with the fields scripts see (the
/// collider's shape fields are flat: `shape` (0 sphere, 1 box, 2 capsule, 3 hull, 4 mesh, 5 compound), `radius`,
/// `half_height`, `half_extents`, `hull`, `mesh`, `compound`, then `offset`, `rotation`, the material's `friction`,
/// `restitution`, `density`, `friction_rule`, `restitution_rule`, and `layer`, `trigger`, `group`, `mask`).
///
/// - Param registry: the registry to add to; not null.
/// - Returns: whether all three were added.
/// - Ownership: borrowed for the call.
/// - Fails: returns false when an id, a name or a type is taken.
bool RegisterPhysicsComponents(storage::ComponentRegistry* registry);

/// Makes `component.set/add` check the physics components with their `Validate()` functions, so a script gets the error
/// at the call.
///
/// - Param catalog: the catalog scripts use; not null; call before it is frozen.
/// - Ownership: borrowed for the call.
/// - Fails: never.
void RegisterPhysicsValidators(script::ComponentCatalog* catalog);

/// Registers the commands that apply the physics writes at the merge (kinds 100 to 115).
///
/// - Param kinds: the runtime's table ([[script::ScriptRuntime::Kinds]]); not null.
/// - Returns: false when an id is taken.
/// - Ownership: borrowed for the call.
/// - Fails: returns false, as described.
bool RegisterPhysicsCommandKinds(script::CommandKindTable* kinds);

/// Adds the `physics.*` functions: the writes (`add_force`, `add_force_at`, `add_impulse`, `add_impulse_at`,
/// `add_torque`, `set_velocity`, `set_angular_velocity`, `set_gravity_scale`, `wake`, `sleep`, `add_joint`,
/// `remove_joint`: `world-write`), the reads (`get_velocity`, `get_angular_velocity`, `is_sleeping`, `raycast`,
/// `raycast_ex`, `shape_cast`, `overlap_sphere`, `overlap_box`, `closest_point`: `world-read`, against the tick-start
/// physics state) and the layer helpers (`layer`, `layer_mask`: `world-read`), and the `character.*` functions
///: `create`, `destroy`, `move`, `jump`, `set_height`, `set_position` (C API script physics, 2026-11-20)
/// (`world-write`; the moves run inside the physics step
/// through [[physics::CharacterSet]]) and `exists`, `is_grounded`, `ground_normal`, `ground_velocity`,
/// `vertical_speed` (`world-read`).
///
/// - Param registry: the registry to add to; not null.
/// - Returns: true when all were added.
/// - Ownership: borrowed for the call.
/// - Fails: returns false when a name is taken.
bool RegisterPhysicsBindings(script::BindingRegistry* registry);

}  // namespace vse::script_physics
