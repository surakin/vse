// Joints as ECS components (Phase 25 Day 393): an entity with a `Joint` names the two entities it ties, and
// `SyncJoints` makes the physics joint.
#pragma once

#include "ecs/entity.h"
#include "ecs/world.h"
#include "math/vector.h"
#include "physics/ids.h"
#include "physics/joint.h"
#include "physics/world.h"
#include "types.h"

namespace engine_placeholder::physics
{

/// A joint between two entities that have bodies; put it on any entity (usually a third one that owns the joint).
///
/// The joint is made at the poses the bodies have when `SyncJoints` creates it, so it starts at rest. An invalid entity
/// means the world. The settings beyond the anchors are the fields of `JointDesc` of the same name.
///
/// - Thread-safety: thread-compatible plain data.
struct Joint
{
    /// The first entity, or an invalid one for the world.
    ecs::Entity entity_a;
    /// The second entity, or an invalid one for the world (not both).
    ecs::Entity entity_b;
    /// The kind.
    JointType type = JointType::Ball;
    /// The anchor on A in the world (the anchor of a ball, fixed or hinge joint).
    Vec3 point_a = Vec3(0.0f, 0.0f, 0.0f);
    /// The anchor on B in the world (a distance joint only; the others use `point_a` for both).
    Vec3 point_b = Vec3(0.0f, 0.0f, 0.0f);
    /// The hinge axis in the world.
    Vec3 axis = Vec3(0.0f, 1.0f, 0.0f);
    /// The shortest and longest a distance joint lets the anchors get; negative keeps the distance they have.
    f32 min_length = -1.0f;
    /// The longest (see `min_length`).
    f32 max_length = -1.0f;
    /// The frequency of a distance joint's spring in hertz, 0 for none.
    f32 hertz = 0.0f;
    /// The damping ratio of that spring.
    f32 damping_ratio = 0.0f;
    /// The force above which the joint breaks, 0 for never.
    f32 break_force = 0.0f;
    /// The torque above which it breaks, 0 for never.
    f32 break_torque = 0.0f;
    /// Whether the two bodies still collide with each other.
    bool collide_connected = false;
    /// Whether a hinge limits its angle.
    bool enable_limit = false;
    /// The lowest hinge angle in radians.
    f32 lower_angle = 0.0f;
    /// The highest hinge angle.
    f32 upper_angle = 0.0f;
    /// Whether a hinge has a motor.
    bool enable_motor = false;
    /// The motor's aim in radians per second.
    f32 motor_speed = 0.0f;
    /// The most torque the motor may apply.
    f32 max_motor_torque = 0.0f;
};

/// Added to an entity with a `Joint` by `SyncJoints`: the physics joint that belongs to it.
///
/// - Thread-safety: thread-compatible plain data.
struct JointLink
{
    /// The joint in the physics world.
    JointId id;
};

/// What one `SyncJoints` call did.
///
/// - Thread-safety: thread-compatible plain data.
struct JointSyncReport
{
    /// Joints made.
    u32 created = 0;
    /// Joints destroyed because their `Joint` component or an entity is gone.
    u32 destroyed = 0;
    /// Joints that are gone from the physics world (broken, or lost with a destroyed body): their `Joint` component
    /// was removed, so they are not made again.
    u32 broken = 0;
    /// Entities whose joint waits for a body (an entity named by it has no `BodyLink` yet).
    u32 pending = 0;
    /// Joints refused (a description that is not valid).
    u32 refused = 0;
};

/// Makes the physics world's joints follow the `Joint` components.
///
/// Call it after `SyncBodies`, at a sync point: it adds and removes `JointLink` (and removes the `Joint` of a joint
/// that broke). Entities are visited in entity order, so two worlds built the same way get the same joint ids.
///
/// - Param ecs: the ECS world.
/// - Param physics: the physics world.
/// - Returns: what was done.
/// - Aborts: called while the ECS world is being iterated.
/// - Fails: a joint whose description is not valid is counted in `refused` and skipped.
JointSyncReport SyncJoints(ecs::World& ecs, PhysicsWorld& physics);

}  // namespace engine_placeholder::physics
