// Physics events for scripts (Phase 27 Day 467): the contact and trigger events of a physics step, turned into script
// events on the Day 459 event bus. A script receives them with `event.on`, as any event:
//
//     event.on('on_collision_begin', function(self, hit) ... hit.other, hit.point, hit.normal ... end)
//
// The bus delivers them at the start of the next script tick, after the step that produced them, in the order they were
// emitted: the order of the physics events (a fixed order of the step's collider keys), and for one contact the
// entity with the smaller stable id first. Nothing here depends on the thread count.
#pragma once

#include "physics/world.h"
#include "script/script_runtime.h"
#include "script_physics/character_set.h"
#include "types.h"

namespace engine_placeholder::script_physics
{

/// Which events become script events.
///
/// - Thread-safety: thread-compatible plain data.
struct PhysicsEventOptions
{
    /// Contact begin, stay and end (`on_collision_begin`, `on_collision_stay`, `on_collision_end`). `stay` events exist
    /// only when the world's step config asks for them.
    bool contacts = true;
    /// Trigger enter and exit (`on_trigger_enter`, `on_trigger_exit`).
    bool triggers = true;
    /// Only events whose other collider is on one of these layers (bit `n` is layer `n`) reach scripts; the filter a
    /// game uses to keep, say, bullets out of the player's handlers.
    u32 layer_mask = 0xFFFFFFFFu;
    /// The characters of the world, so an event that names a character's body names its entity; null when there are
    /// none (a contact with a character's body is then skipped, as it names no entity).
    const CharacterSet* characters = nullptr;
};

/// What an emit pass did.
///
/// - Thread-safety: thread-compatible plain data.
struct PhysicsEventReport
{
    /// Events put on the bus (one per receiving entity).
    u32 emitted = 0;
    /// Receivers skipped because the entity has no script handler for the event (nothing was built for them).
    u32 unhandled = 0;
    /// Receivers skipped by `layer_mask`.
    u32 filtered = 0;
};

/// Puts the physics world's events of the last `Update` or `Step` on the runtime's event bus.
///
/// An event is built only for an entity that listens (`ScriptRuntime::HasHandler`: an `event.on` handler or a coroutine
/// in `wait_event`): a step that makes 100,000 contact events costs the lookup per receiver for the entities whose
/// scripts do not listen. A native subscriber of the bus (`EventBus::Subscribe`) sees only the events that reach a
/// listening entity. The payload of a contact event (the receiver is
/// `self`): `other` (the entity), `point` and `normal` (the normal points from `self` toward `other`), `depth`,
/// `impulse` (the solver's normal impulse over the pair), `points` (the manifold's size). A trigger event has `other`
/// (the entity that entered or left) and goes to the trigger's entity only.
///
/// Call it after the physics step and before the next script tick, from the thread that runs them.
///
/// - Param world: the physics world; its event lists are read.
/// - Param runtime: the runtime whose bus receives the events and whose handlers are asked.
/// - Param options: which events and layers.
/// - Returns: the counts.
/// - Thread-safety: thread-compatible; not while a script tick runs.
/// - Ownership: borrows both for the call; nothing is kept.
/// - Fails: never; an event with no entity (a body made by hand) is skipped.
PhysicsEventReport EmitPhysicsEvents(const physics::PhysicsWorld& world, script::ScriptRuntime* runtime,
                                     const PhysicsEventOptions& options = PhysicsEventOptions());

/// The names of the events, for a host that subscribes natively.
inline constexpr const char* kEventCollisionBegin = "on_collision_begin";  ///< Two colliders began to touch.
inline constexpr const char* kEventCollisionStay = "on_collision_stay";    ///< They still touch (with `stay_events`).
inline constexpr const char* kEventCollisionEnd = "on_collision_end";      ///< They stopped touching.
inline constexpr const char* kEventTriggerEnter = "on_trigger_enter";      ///< A body entered a trigger volume.
inline constexpr const char* kEventTriggerExit = "on_trigger_exit";        ///< A body left a trigger volume.

}  // namespace engine_placeholder::script_physics
