// The characters scripts drive (Phase 27 Day 467): one `CharacterController` per entity, kept in entity order.
// A script says where the character should go (`character.move`, `jump`, `set_height`); the set keeps that as the
// character's wish, and runs the moves inside the physics step (a pre-step hook of the world), so a frame with three
// fixed steps moves the character three times, the way the controller was made to be used.
#pragma once

#include "bytes/byte_writer.h"
#include "containers/vector.h"
#include "ecs/entity.h"
#include "ecs/world.h"
#include "math/vector.h"
#include "physics/character.h"
#include "physics/world.h"
#include "types.h"

namespace vse::physics
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
    explicit CharacterSet(PhysicsWorld* world);
    /// Removes the hook and destroys every character body.
    ~CharacterSet();

    CharacterSet(const CharacterSet&) = delete;
    CharacterSet& operator=(const CharacterSet&) = delete;

    /// Makes a character for an entity: a kinematic capsule body (the events name it through
    /// [[physics::CharacterSet::EntityOfBody]]).
    ///
    /// - Param entity: the entity; at most one character each.
    /// - Param config: the shape and the rules.
    /// - Param position: where the capsule's centre starts.
    /// - Returns: false (and nothing is made) for an invalid entity, an entity that has a character, or a bad config.
    /// - Fails: returns false, as described.
    bool Add(ecs::Entity entity, const CharacterConfig& config, const Vec3& position);

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
    const CharacterController* Find(ecs::Entity entity) const;

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
    ecs::Entity EntityOfBody(BodyId body) const;

    /// How many characters there are.
    ///
    /// - Fails: never.
    usize Count() const { return entries_.Size(); }

    /// The entity of the character at `index`, in entity order (to walk the set: 0 to `Count() - 1`).
    ///
    /// - Param index: below `Count()`.
    /// - Returns: the entity.
    /// - Aborts: `index` is not below `Count()`.
    ecs::Entity EntityAt(usize index) const { return entries_[index].entity; }

    /// Copies each character's position into its entity's `LocalTransform` (a kinematic body is not written back by
    /// the step's own write-back).
    ///
    /// - Param ecs: the ECS world.
    /// - Returns: how many transforms were written.
    /// - Aborts: called while the ECS world is being iterated.
    /// - Fails: an entity that is gone or has no `LocalTransform` is skipped.
    u32 WriteBack(ecs::World& ecs) const;
    /// Copies each character's position into its entity's `LocalTransform`, interpolated between the last two fixed
    /// steps by the world's `Alpha()` when `interpolate` is true (the C API's frame: a camera that follows the entity
    /// moves smoothly when frames and steps do not line up), the last step's position otherwise.
    ///
    /// - Param ecs: the ECS world.
    /// - Param interpolate: whether to blend the last two steps.
    /// - Returns: how many transforms were written.
    /// - Aborts: called while the ECS world is being iterated.
    /// - Fails: an entity that is gone or has no `LocalTransform` is skipped.
    u32 WriteBack(ecs::World& ecs, bool interpolate) const;
    /// Puts an entity's character somewhere else at once (a teleport; C API script physics, 2026-11-20): the capsule's
    /// centre moves to `position`, the body's speed and the kept vertical speed become zero, a pending jump is dropped,
    /// and the held move stays. The character is in the air (no ground, no platform) until the next step looks.
    ///
    /// - Param entity: the entity.
    /// - Param position: the new centre, finite.
    /// - Returns: false for no such character, a non-finite position, or a position the world refuses.
    /// - Fails: returns false, as described.
    bool SetPosition(ecs::Entity entity, const Vec3& position);
    /// What the last fixed step's move found for an entity's character (ground begun or ended, walls).
    ///
    /// - Param entity: the entity.
    /// - Returns: the events, or null for no such character.
    /// - Ownership: borrowed; valid until the set changes.
    /// - Fails: returns null, as described.
    const CharacterEvents* LastEvents(ecs::Entity entity) const;
    /// Makes again the bodies of characters whose body is gone (a physics snapshot restore replaces the bodies), at
    /// their entity's `LocalTransform`.
    ///
    /// - Param ecs: the ECS world.
    /// - Returns: how many were made again.
    /// - Fails: a character whose entity is gone or has no transform, or whose body cannot be made, is left as it is.
    u32 Recreate(const ecs::World& ecs);

    /// Writes what the physics snapshot does not hold: each character's body, ground state, kept vertical speed, held
    /// move and pending jump or height change, in entity order (C API script physics, 2026-11-20).
    ///
    /// - Param out: the writer; the bytes are appended.
    /// - Ownership: nothing is kept.
    /// - Fails: never.
    void Snapshot(bytes::ByteWriter* out) const;

    /// Whether `data` is a whole, well-formed `Snapshot` (finite numbers, entities in increasing order, nothing left
    /// over). It does not look at the world.
    ///
    /// - Param data: the bytes.
    /// - Returns: whether `Restore` would accept them.
    /// - Fails: returns false, as described.
    static bool ValidSnapshot(ByteSpan data);

    /// Puts back what `Snapshot` wrote, after the physics world was restored from the snapshot of the same moment: a
    /// character of the set takes its kept state, and its kept body when the restored world has it as a kinematic body
    /// (else `Recreate` makes one); a kept body whose entity has no character any more is destroyed. A character the
    /// data does not name keeps its state.
    ///
    /// - Param data: the bytes.
    /// - Returns: false (and nothing changes) when `ValidSnapshot` refuses them.
    /// - Fails: returns false, as described.
    bool Restore(ByteSpan data);

private:
    struct Entry
    {
        u64 key = 0;
        ecs::Entity entity;
        CharacterController controller;
        Vec3 wish = Vec3(0.0f, 0.0f, 0.0f);
        bool jump = false;
        f32 jump_speed = 0.0f;
        bool resize = false;
        f32 height = 0.0f;
        CharacterEvents events;  ///< The last step's move (C API script physics, 2026-11-20).
    };

    usize LowerBound(u64 key) const;
    Entry* FindEntry(ecs::Entity entity);
    const Entry* FindEntry(ecs::Entity entity) const;
    static void PreStep(void* user, PhysicsWorld* world, f32 dt);

    PhysicsWorld* world_;
    Vector<Entry> entries_;
};

}  // namespace vse::physics
