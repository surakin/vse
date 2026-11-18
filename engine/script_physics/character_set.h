// The characters scripts drive (Phase 27 Day 467): one `physics::CharacterController` per entity, kept in entity order.
// A script says where the character should go (`character.move`, `jump`, `set_height`); the set keeps that as the
// character's wish, and runs the moves inside the physics step (a pre-step hook of the world), so a frame with three
// fixed steps moves the character three times, the way the controller was made to be used.
#pragma once

#include "containers/vector.h"
#include "ecs/entity.h"
#include "ecs/world.h"
#include "math/vector.h"
#include "physics/character.h"
#include "physics/world.h"
#include "types.h"

namespace engine_placeholder::script_physics
{

/// The characters of one physics world.
///
/// The set belongs to the host, beside the physics world (`PhysicsScriptServices::characters`). While the script phase
/// runs nothing changes it, so every partition reads it at once; the merge changes the wishes (a command of
/// `character.*`), and the fixed step changes the states.
///
/// - Thread-safety: thread-compatible; the host serializes `Add`, `Remove`, `Prune` and the step (the merge and the
/// physics step run on one thread), and the script phase only calls the const members.
/// - Ownership: owns the controllers; the bodies belong to the physics world, which keeps a pointer to the set (its
/// pre-step hook) until the set is destroyed, so the world must outlive the set.
class CharacterSet
{
public:
    /// Makes the set and installs its pre-step hook in `world`: every fixed step moves the characters first.
    ///
    /// - Param world: the world; borrowed, it outlives the set.
    /// - Ownership: the world keeps a pointer to this set.
    explicit CharacterSet(physics::PhysicsWorld* world);
    /// Removes the hook and destroys every character body.
    ~CharacterSet();

    CharacterSet(const CharacterSet&) = delete;
    CharacterSet& operator=(const CharacterSet&) = delete;

    /// Makes a character for an entity: a kinematic capsule body (the events name it through
    /// [[script_physics::CharacterSet::EntityOfBody]]).
    ///
    /// - Param entity: the entity; at most one character each.
    /// - Param config: the shape and the rules.
    /// - Param position: where the capsule's centre starts.
    /// - Returns: false (and nothing is made) for an invalid entity, an entity that has a character, or a bad config.
    /// - Fails: returns false, as described.
    bool Add(ecs::Entity entity, const physics::CharacterConfig& config, const Vec3& position);

    /// Destroys an entity's character.
    ///
    /// - Param entity: the entity.
    /// - Returns: whether it had one.
    /// - Fails: returns false, as described.
    bool Remove(ecs::Entity entity);

    /// Destroys the characters of entities that are gone from the ECS world (call it at the sync point).
    ///
    /// - Param ecs: the ECS world.
    /// - Returns: how many were destroyed.
    /// - Fails: never.
    u32 Prune(const ecs::World& ecs);

    /// Sets the horizontal velocity the character tries to walk at (its `y` is ignored); it holds until set again.
    ///
    /// - Param entity: the entity.
    /// - Param velocity: metres per second, finite.
    /// - Returns: false for no such character or a non-finite velocity.
    /// - Fails: returns false, as described.
    bool SetMove(ecs::Entity entity, const Vec3& velocity);

    /// Asks for a jump at the start of the next step (a jump in the air also works: the caller checks the ground).
    ///
    /// - Param entity: the entity.
    /// - Param speed: the upward speed in metres per second, finite.
    /// - Returns: false for no such character or a non-finite speed.
    /// - Fails: returns false, as described.
    bool Jump(ecs::Entity entity, f32 speed);

    /// Asks for a new capsule height at the start of the next step; standing up under a ceiling waits, and is tried
    /// again at each step until there is room.
    ///
    /// - Param entity: the entity.
    /// - Param height: the total height in metres, at least twice the radius.
    /// - Returns: false for no such character or a height that is too small or not finite.
    /// - Fails: returns false, as described.
    bool SetHeight(ecs::Entity entity, f32 height);

    /// The character of an entity.
    ///
    /// - Param entity: the entity.
    /// - Returns: its controller, or null.
    /// - Ownership: borrowed; valid until the set changes.
    /// - Fails: returns null, as described.
    const physics::CharacterController* Find(ecs::Entity entity) const;

    /// The wish of an entity's character: the velocity it was asked to walk at.
    ///
    /// - Param entity: the entity.
    /// - Param velocity: receives the wish (zero for no such character).
    /// - Returns: whether the entity has a character.
    /// - Ownership: `velocity` is the caller's; nothing is kept.
    /// - Fails: returns false, as described.
    bool Wish(ecs::Entity entity, Vec3* velocity) const;

    /// The entity whose character owns a body: the physics events name a character's body with no entity (the body is
    /// not the entity's `RigidBody`, and `SyncBodies` would destroy a body that named an entity without one).
    ///
    /// - Param body: the body.
    /// - Returns: the entity, or an invalid one when the body is no character's.
    /// - Fails: returns an invalid entity, as described.
    ecs::Entity EntityOfBody(physics::BodyId body) const;

    /// How many characters there are.
    ///
    /// - Fails: never.
    usize Count() const { return entries_.Size(); }

    /// Copies each character's position into its entity's `LocalTransform` (a kinematic body is not written back by
    /// the step's own write-back).
    ///
    /// - Param ecs: the ECS world.
    /// - Returns: how many transforms were written.
    /// - Aborts: called while the ECS world is being iterated.
    /// - Fails: an entity that is gone or has no `LocalTransform` is skipped.
    u32 WriteBack(ecs::World& ecs) const;

private:
    struct Entry
    {
        u64 key = 0;
        ecs::Entity entity;
        physics::CharacterController controller;
        Vec3 wish = Vec3(0.0f, 0.0f, 0.0f);
        bool jump = false;
        f32 jump_speed = 0.0f;
        bool resize = false;
        f32 height = 0.0f;
    };

    usize LowerBound(u64 key) const;
    Entry* FindEntry(ecs::Entity entity);
    const Entry* FindEntry(ecs::Entity entity) const;
    static void PreStep(void* user, physics::PhysicsWorld* world, f32 dt);

    physics::PhysicsWorld* world_;
    Vector<Entry> entries_;
};

}  // namespace engine_placeholder::script_physics
