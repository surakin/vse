// What gameplay sees of the simulation (Phase 25 Day 405): contact begin, stay and end, trigger enter and exit, and
// joint breaks, in a fixed order, with the entities of the bodies.
#pragma once

#include "containers/vector.h"
#include "ecs/entity.h"
#include "math/vector.h"
#include "physics/contact.h"
#include "physics/ids.h"
#include "types.h"

namespace engine_placeholder::physics
{

/// What happened to a pair of touching colliders.
enum class ContactEventKind : u8
{
    Begin = 0,  ///< The colliders touch now and did not in the step before.
    Stay = 1,   ///< They touched and still touch (reported only with `StepConfig::stay_events`).
    End = 2,    ///< They touched and no longer do (the manifold fields are zero).
};

/// One event of a pair of colliders touching, with the summary of the contact.
///
/// - Thread-safety: thread-compatible plain data.
struct ContactEvent
{
    /// Begin, stay or end.
    ContactEventKind kind = ContactEventKind::Begin;
    /// The collider with the smaller key.
    ColliderId a;
    /// The other collider.
    ColliderId b;
    /// The body of `a`.
    BodyId body_a;
    /// The body of `b`.
    BodyId body_b;
    /// The entity of `body_a` (invalid for a body made by hand, or one that is gone).
    ecs::Entity entity_a;
    /// The entity of `body_b`.
    ecs::Entity entity_b;
    /// The number of points in the manifold (0 for an end).
    u32 points = 0;
    /// The deepest point, on the surface of `a`, in the world.
    Vec3 point = Vec3(0.0f, 0.0f, 0.0f);
    /// The normal, from `a` toward `b`.
    Vec3 normal = Vec3(0.0f, 0.0f, 0.0f);
    /// The depth of the deepest point (negative while the shapes are still apart by less than the contact margin).
    f32 depth = 0.0f;
    /// The solver's total normal impulse over the pair's points in the step (newton seconds per substep).
    f32 normal_impulse = 0.0f;
};

/// A body entered or left a trigger volume.
///
/// - Thread-safety: thread-compatible plain data.
struct TriggerEvent
{
    /// True for enter, false for exit.
    bool enter = true;
    /// The trigger collider.
    ColliderId trigger;
    /// The collider that is inside it.
    ColliderId other;
    /// The body of the trigger.
    BodyId body_trigger;
    /// The body of the other collider.
    BodyId body_other;
    /// The entity of the trigger's body.
    ecs::Entity entity_trigger;
    /// The entity of the other body.
    ecs::Entity entity_other;
};

/// A joint carried more than its break force or torque and was removed.
///
/// - Thread-safety: thread-compatible plain data.
struct JointBreakEvent
{
    /// The joint (its id is stale now).
    JointId joint;
    /// The first body, or null for the world.
    BodyId body_a;
    /// The second body, or null for the world.
    BodyId body_b;
    /// The entity of `body_a`.
    ecs::Entity entity_a;
    /// The entity of `body_b`.
    ecs::Entity entity_b;
    /// The force the joint carried in its last step.
    Vec3 force = Vec3(0.0f, 0.0f, 0.0f);
    /// The torque it carried.
    Vec3 torque = Vec3(0.0f, 0.0f, 0.0f);
};

/// The events of the last `Update` (or of the last `Step` when it is called by hand), each list in the order of the
/// steps and, within a step, of the collider keys.
///
/// - Thread-safety: thread-compatible; read it after the update, not while the world steps.
/// - Ownership: owned by the world; the lists are valid until the next `Update`, `Step` or `ClearEvents`.
struct PhysicsEvents
{
    /// Contact begin, stay and end.
    Vector<ContactEvent> contacts;
    /// Trigger enter and exit.
    Vector<TriggerEvent> triggers;
    /// Joints that broke.
    Vector<JointBreakEvent> joint_breaks;

    /// The number of events of every kind.
    /// - Fails: never.
    usize Count() const { return contacts.Size() + triggers.Size() + joint_breaks.Size(); }
};

/// A pair of colliders that touch now, with the manifold of the last step.
///
/// - Thread-safety: thread-compatible plain data.
struct ContactRecord
{
    /// The collider with the smaller key.
    ColliderId a;
    /// The other collider.
    ColliderId b;
    /// The body of `a`.
    BodyId body_a;
    /// The body of `b`.
    BodyId body_b;
    /// The contact.
    ContactManifold manifold;
    /// The solver's total normal impulse over the points.
    f32 normal_impulse = 0.0f;
};

}  // namespace engine_placeholder::physics
