// The character controller (Phase 25 Day 404): a capsule on a kinematic body that walks, slides along walls, climbs
// steps and slopes, stands on moving platforms and pushes dynamic bodies.
#pragma once

#include "math/vector.h"
#include "physics/ids.h"
#include "physics/world.h"
#include "types.h"

namespace vse::physics
{

/// The shape and the rules of a character.
///
/// - Thread-safety: thread-compatible plain data.
struct CharacterConfig
{
    /// The capsule's radius in metres.
    f32 radius = 0.4f;
    /// The capsule's total height in metres (at least twice the radius).
    f32 height = 1.8f;
    /// The highest ledge the character steps up onto, in metres.
    f32 step_height = 0.3f;
    /// The steepest slope that counts as ground, as the smallest allowed `y` of its unit normal (the cosine of the
    /// angle; 0.64 is about 50 degrees). A steeper surface is a wall.
    f32 min_ground_y = 0.64f;
    /// The gap kept between the capsule and what it touches, in metres; at least 0.0001 (a smaller gap makes the ground
    /// probe too short to hit anything, and `Create` refuses it).
    f32 skin_width = 0.02f;
    /// Multiplies the world's gravity.
    f32 gravity_scale = 1.0f;
};

/// What a move did.
///
/// - Thread-safety: thread-compatible plain data.
struct CharacterEvents
{
    /// The character was in the air and now stands on ground.
    bool began_ground = false;
    /// The character stood on ground and now does not.
    bool ended_ground = false;
    /// How many walls the horizontal move ran into.
    u32 wall_hits = 0;
    /// The normal of the last wall hit (zero if none).
    Vec3 wall_normal = Vec3(0.0f, 0.0f, 0.0f);
};

/// Where the character stands.
///
/// - Thread-safety: thread-compatible plain data.
struct CharacterState
{
    /// Whether the character stands on ground.
    bool on_ground = false;
    /// The body it stands on (null in the air).
    BodyId ground_body;
    /// The ground's unit normal.
    Vec3 ground_normal = Vec3(0.0f, 1.0f, 0.0f);
    /// The vertical speed the controller keeps (gravity and jumps), in metres per second.
    f32 vertical_speed = 0.0f;
};

/// A character: a kinematic body the program moves with `Move` once per step, before `PhysicsWorld::Step`.
///
/// The character sweeps its capsule against static and kinematic bodies (dynamic bodies are not obstacles: the solver
/// pushes them, because the character's body is kinematic with the speed `Move` set), slides along what it meets, steps
/// up ledges by casting up, forward and down, snaps to the ground when walking down, and takes the velocity of the
/// body it stands on.
///
/// - Thread-safety: thread-compatible; one controller is used by one thread.
/// - Ownership: the controller holds the id of a body of the world it was created in; it does not own the world.
class CharacterController
{
public:
    /// Creates the character's body (kinematic, one upright capsule collider) in a world; a body the controller already
    /// had is destroyed first.
    ///
    /// - Param world: the world.
    /// - Param config: the shape and the rules.
    /// - Param position: where the capsule's centre starts.
    /// - Returns: `Ok`, or `Invalid` for a bad config (radius or height too small, a non-finite number).
    /// - Ownership: the controller keeps the world's body id.
    /// - Fails: as the return says.
    BodyStatus Create(PhysicsWorld* world, const CharacterConfig& config, const Vec3d& position);

    /// Changes the capsule's total height (crouching, standing up): the body is made again with the new capsule, its
    /// feet where they were (the centre moves by half the change), and the ground state and the kept vertical speed
    /// stay. The body gets a new id.
    ///
    /// - Param world: the world the controller was created in.
    /// - Param height: the new height in metres (at least twice the radius, finite).
    /// - Returns: `Ok`, `StaleId` before `Create`, `Invalid` for a height that is too small or not finite, or when a
    /// taller capsule would start inside static or kinematic geometry (a ceiling: the character cannot stand up there);
    /// the character is unchanged in both cases.
    /// - Ownership: the controller keeps the new body id.
    /// - Fails: as the return says.
    BodyStatus SetHeight(PhysicsWorld* world, f32 height);

    /// Returns the config the character was made with (the height as last set).
    ///
    /// - Returns: the config.
    /// - Fails: never.
    const CharacterConfig& Config() const { return config_; }

    /// Destroys the character's body.
    ///
    /// - Param world: the world the controller was created in.
    /// - Ownership: the controller lets go of the body id.
    /// - Fails: a stale id does nothing.
    void Destroy(PhysicsWorld* world);

    /// Stops the character: the body's speed and the kept vertical speed become zero. The body keeps the speed `Move`
    /// last set (it is kinematic), so a program that stops calling `Move` calls this.
    ///
    /// - Param world: the world the controller was created in.
    /// - Ownership: nothing is kept.
    /// - Fails: a stale body does nothing.
    void Stop(PhysicsWorld* world);

    /// Moves the character for one step: the capsule goes where `velocity` (horizontal; the `y` part is ignored) and
    /// gravity take it, and the body's speed is set so the next `Step` carries it there and pushes what it meets.
    ///
    /// - Param world: the world.
    /// - Param velocity: the wanted horizontal velocity in metres per second.
    /// - Param dt: the step length (the world's fixed step).
    /// - Returns: what happened.
    /// - Ownership: nothing is kept.
    /// - Fails: a bad `dt` or a stale body returns no events and moves nothing.
    CharacterEvents Move(PhysicsWorld* world, const Vec3& velocity, f32 dt);

    /// Starts a jump: the vertical speed becomes `speed` and the character leaves the ground.
    ///
    /// - Param speed: the upward speed in metres per second.
    /// - Fails: never.
    void Jump(f32 speed);

    /// Returns the body.
    ///
    /// - Returns: its id (null before `Create`).
    /// - Fails: never.
    BodyId BodyHandle() const { return body_; }

    /// Returns where the character stands.
    ///
    /// - Returns: the state.
    /// - Fails: never.
    const CharacterState& State() const { return state_; }

    /// Puts back what a snapshot kept (a physics snapshot restore, C API script physics, 2026-11-20): the body the
    /// character drives and where it stands. The caller has checked that `body` is a kinematic body of the world.
    ///
    /// - Param body: the character's body in the restored world.
    /// - Param state: the kept state.
    /// - Ownership: the controller keeps the body id.
    /// - Fails: never.
    void RestoreState(BodyId body, const CharacterState& state)
    {
        body_ = body;
        state_ = state;
    }

private:
    BodyId body_;
    CharacterConfig config_;
    CharacterState state_;
};

}  // namespace vse::physics
