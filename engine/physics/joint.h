// Joints of the physics module (Phase 25 Day 398): what two bodies (or a body and the world) are tied by, and the
// record the world keeps for each.
#pragma once

#include "math/quaternion.h"
#include "math/vector.h"
#include "physics/ids.h"
#include "physics/validation.h"
#include "types.h"

namespace vse::physics
{

/// The kinds of joint.
enum class JointType : u8
{
    Ball = 0,      ///< The two anchors stay together; the bodies turn freely about them (three rows).
    Distance = 1,  ///< The anchors stay a length apart: rigid, a spring or a rope between limits.
    Fixed = 2,     ///< The anchors stay together and the two frames keep their relative orientation (six rows).
    Hinge = 3,     ///< The anchors stay together and the bodies turn about the Z axis of the joint frames (five rows).
};

/// What a joint is made from. Anchors are in the frame of each body (its origin and axes); when `body_b` is null the
/// joint is to the world and `anchor_b` and `frame_b` are in the world.
///
/// Use `MakeBallJoint`, `MakeDistanceJoint` and `MakeFixedJoint` (`PhysicsWorld`) to fill the anchors and frames from
/// the bodies' poses at creation, so the joint starts at rest.
///
/// - Thread-safety: thread-compatible plain data.
struct JointDesc
{
    /// The kind.
    JointType type = JointType::Ball;
    /// The first body, or null for the world (then `anchor_a` and `frame_a` are in the world).
    BodyId body_a;
    /// The second body, or null for the world (not both null). A motor and an angle spring act on B relative to A, so
    /// the moving body of a hinge to the world is usually B.
    BodyId body_b;
    /// The anchor on body A, in its frame.
    Vec3 anchor_a = Vec3(0.0f, 0.0f, 0.0f);
    /// The anchor on body B, in its frame (in the world for a joint to the world).
    Vec3 anchor_b = Vec3(0.0f, 0.0f, 0.0f);
    /// The orientation of the joint's frame in body A (fixed joints).
    Quaternion frame_a = Quaternion::Identity();
    /// The orientation of the joint's frame in body B (in the world for a joint to the world).
    Quaternion frame_b = Quaternion::Identity();
    /// The length a distance joint holds or a spring rests at, in metres, above 0.
    f32 length = 1.0f;
    /// The shortest a distance joint lets the anchors get (0 to `length`).
    f32 min_length = 1.0f;
    /// The longest it lets them get (`length` or more). With `min_length == max_length == length` and no spring the
    /// joint is rigid; with a range it is a rope between the limits.
    f32 max_length = 1.0f;
    /// The frequency of a distance joint's spring in hertz, 0 for none.
    f32 hertz = 0.0f;
    /// The damping ratio of the spring.
    f32 damping_ratio = 0.0f;
    /// The force (newtons) above which the joint breaks and is removed, 0 for never.
    f32 break_force = 0.0f;
    /// The torque (newton metres) above which it breaks, 0 for never.
    f32 break_torque = 0.0f;
    /// Whether the two bodies still collide with each other.
    bool collide_connected = false;
    /// Whether a hinge limits its angle to `[lower_angle, upper_angle]`.
    bool enable_limit = false;
    /// The lowest angle of a hinge, in radians (beyond plus or minus pi is fine: the angle is tracked across turns).
    f32 lower_angle = 0.0f;
    /// The highest angle of a hinge.
    f32 upper_angle = 0.0f;
    /// Whether a hinge has a motor.
    bool enable_motor = false;
    /// The angular velocity the motor aims for, in radians per second.
    f32 motor_speed = 0.0f;
    /// The most torque the motor may apply, in newton metres.
    f32 max_motor_torque = 0.0f;

    /// Checks the numbers.
    ///
    /// - Returns: `Ok`, `NotFinite`, `NotPositive` or `OutOfRange`.
    /// - Fails: as the return says.
    Validation Validate() const;
};

/// A joint in the world: its description, the impulses that warm start it and what it carried in the last step.
///
/// - Thread-safety: thread-compatible plain data.
struct JointRecord
{
    /// The description.
    JointDesc desc;
    /// The impulse of the point rows (linear), kept from substep to substep and step to step.
    Vec3 linear_impulse = Vec3(0.0f, 0.0f, 0.0f);
    /// The impulse of the angle rows.
    Vec3 angular_impulse = Vec3(0.0f, 0.0f, 0.0f);
    /// The impulses of a distance joint's rigid or spring row and its lower and upper limits.
    f32 axial_impulse = 0.0f;
    /// The lower limit's impulse.
    f32 lower_impulse = 0.0f;
    /// The upper limit's impulse.
    f32 upper_impulse = 0.0f;
    /// The motor's impulse (hinge).
    f32 motor_impulse = 0.0f;
    /// The impulse of the angle spring (hinge).
    f32 spring_impulse = 0.0f;
    /// The hinge angle in radians, tracked across full turns: zero when the joint was made at rest.
    f32 angle = 0.0f;
    /// The last angle from the arc tangent alone, in (-pi, pi], from which the turns are counted.
    f32 angle_raw = 0.0f;
    /// The force the joint carried in the last step (the impulse of the last substep over its length).
    Vec3 reaction_force = Vec3(0.0f, 0.0f, 0.0f);
    /// The torque it carried.
    Vec3 reaction_torque = Vec3(0.0f, 0.0f, 0.0f);
    /// Set when a threshold was passed; the step removes the joint.
    bool broken = false;
};

/// The angle of a hinge from the two orientations alone, in (-pi, pi]: the twist of the joint frame of B relative to
/// that of A about the hinge axis. It uses `Atan2`, so the last bits depend on the C library (the one non-exact
/// operation of the step; `docs/guide/physics.md`).
///
/// - Param desc: the hinge.
/// - Param orientation_a: the orientation of body A.
/// - Param orientation_b: the orientation of body B (identity for the world).
/// - Returns: the angle.
/// - Fails: never.
f32 HingeRawAngle(const JointDesc& desc, const Quaternion& orientation_a, const Quaternion& orientation_b);

/// Wraps an angle difference into [-pi, pi).
///
/// - Param angle: any finite angle.
/// - Returns: the equivalent angle in the range.
/// - Fails: never.
f32 WrapAngle(f32 angle);

}  // namespace vse::physics
