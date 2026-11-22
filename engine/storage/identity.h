#pragma once

//! Stable identities for entities: [[storage::KeyGenerator]] draws keys,
//! [[storage::EntityKeyComponent]] gives an entity one, [[storage::KeyIndex]] maps keys to entities and back, and
//! [[storage::ResolveReference]], [[storage::CaptureParents]] and [[storage::ApplyParents]] turn stored references back
//! into entities.

#include "containers/hash_map.h"
#include "containers/vector.h"
#include "ecs/entity.h"
#include "ecs/world.h"
#include "mem/heap_allocator.h"
#include "storage/entity_ref.h"
#include "storage/status.h"
#include "types.h"

namespace vse::storage
{

/// Draws entity keys: from the operating system's random source, or from a seeded sequence that gives the same keys
/// every run (for tests).
///
/// A key is never null. Two random keys collide with probability 2^-128 per pair; the seeded sequence repeats only
/// after 2^64 draws.
///
/// - Thread-safety: thread-compatible; use one generator per thread.
/// - Ownership: owns its state; copying it copies the position in the sequence.
class KeyGenerator
{
public:
    /// Creates a generator that draws from the operating system.
    /// - Fails: never.
    KeyGenerator() = default;
    /// Creates a deterministic generator: the same seed gives the same keys.
    ///
    /// - Param seed: any value.
    /// - Fails: never.
    static KeyGenerator Seeded(u64 seed);
    /// Returns the next key (never null).
    /// - Fails: never.
    EntityKey Next();

private:
    bool seeded_ = false;
    u64 state_ = 0;
};

/// The durable identity of an entity: a component, so it is stored and loaded like any other. A null key means no
/// identity. Registered with id
/// [[storage::kEntityKeyId]].
///
/// - Thread-safety: thread-compatible plain data.
struct EntityKeyComponent
{
    /// The world logs removals of this component; [[storage::KeyIndex::Sync]] reads the log.
    static constexpr bool kTrackRemoval = true;
    /// The entity's key.
    EntityKey key;
};

/// The stored form of [[ecs::Parent]]: the parent as a key. It exists in a world only between
/// [[storage::CaptureParents]] (before a save) and
/// [[storage::ApplyParents]] (after a load). Registered with id [[storage::kParentLinkId]].
///
/// - Thread-safety: thread-compatible plain data.
struct ParentLink
{
    /// The parent's key.
    EntityRef parent;
};

/// What to do with a reference to a key that is in no world.
enum class MissingPolicy : u8
{
    Fail,          ///< Report `MissingReference` and change nothing.
    NullReference  ///< Turn the reference into a null reference and count it.
};

/// What resolving references found.
///
/// - Thread-safety: thread-compatible plain data.
struct ReferenceReport
{
    /// The most keys kept in [[storage::ReferenceReport::first_missing]].
    static constexpr usize kMaxReported = 8;
    /// How many references named a key that is not there.
    usize missing = 0;
    /// How many of them were turned into null references ([[storage::MissingPolicy::NullReference]]).
    usize nulled = 0;
    /// The first missing keys, in the order met, up to [[storage::ReferenceReport::kMaxReported]].
    Vector<EntityKey> first_missing;
};

/// Maps keys to entities and entities to keys for one world, and keeps both in step with the world's
/// [[storage::EntityKeyComponent]]s.
///
/// [[storage::KeyIndex::Sync]] reads what changed since the last call (the change stamps) and what was removed (the
/// removal log), so its cost follows the number of changes, not the number of entities. Call it once per frame, and
/// before a lookup that must see this frame's changes. Two entities with one key are never both indexed: the first
/// keeps the key, the other stays out and is reported by `Sync` until its key changes or it goes.
///
/// - Thread-safety: thread-compatible; one index per world, used from one thread at a time.
/// - Ownership: owns its tables; stores entity ids, not pointers into the world.
class KeyIndex
{
public:
    /// Creates an empty index; the first `Sync` reads every key in the world.
    KeyIndex() : by_key_(HeapAllocator::Default()), by_entity_(HeapAllocator::Default()) {}

    /// Brings the index up to date with `world`.
    ///
    /// Marks the changes read as handled with `ecs::Query::Finish`, which advances the world's tick; call it where no
    /// other writer runs (a sync point).
    ///
    /// - Param world: the world to read; the index is for this world only.
    /// - Returns: success, or `Duplicate` when entities with one key exist (the index holds one of each pair; see
    /// [[storage::KeyIndex::DuplicateCount]]).
    /// - Fails: `Duplicate`.
    /// - Aborts: the world is being iterated by a query that writes `EntityKeyComponent`.
    /// - Ownership: `world` is borrowed for the call.
    Status Sync(ecs::World& world);
    /// Gives `entity` the key `key` at once, refusing a key another entity has.
    ///
    /// - Param world: the world; synced first so the check sees every key.
    /// - Param entity: a live entity.
    /// - Param key: not null.
    /// - Returns: success, `Invalid` for a null key or a dead entity, `Duplicate` when another entity has the key
    /// (nothing changes), or the error of a `Sync` that found other duplicates only if this call did not succeed.
    /// - Fails: `Invalid`, `Duplicate`.
    /// - Aborts: the world is being iterated.
    /// - Ownership: `world` is borrowed for the call.
    Status Assign(ecs::World& world, ecs::Entity entity, EntityKey key);

    /// Returns the entity with `key`, or the invalid entity.
    /// - Fails: returns the invalid entity when the key is null or unknown.
    ecs::Entity Find(EntityKey key) const;
    /// Returns the key of `entity`, or the null key.
    /// - Fails: returns the null key when the entity has none in the index.
    EntityKey KeyOf(ecs::Entity entity) const;
    /// The number of entities in the index.
    /// - Fails: never.
    usize Size() const { return by_key_.Size(); }
    /// How many entities are left out because their key belongs to another entity.
    /// - Fails: never.
    usize DuplicateCount() const { return duplicates_.Size(); }

private:
    void Drop(ecs::Entity entity);
    bool Index(ecs::World& world, ecs::Entity entity, EntityKey key);

    HashMap<EntityKey, ecs::Entity, HeapAllocator> by_key_;
    HashMap<ecs::Entity, EntityKey, HeapAllocator> by_entity_;
    Vector<ecs::Entity> duplicates_;
    u64 since_ = 0;
    u64 last_removal_ = 0;
};

/// Turns one stored reference into an entity.
///
/// - Param index: the keys of the target world, synced.
/// - Param key: the stored target; the null key resolves to no entity and is not an error.
/// - Param policy: what a missing target does.
/// - Param report: counts and remembers missing keys; may be null.
/// - Param out: receives the entity, or the invalid entity (null key, or a missing target under `NullReference`).
/// - Returns: success, or `MissingReference` under `Fail`.
/// - Fails: `MissingReference`.
/// - Ownership: nothing is retained.
Status ResolveReference(const KeyIndex& index, EntityKey key, MissingPolicy policy, ReferenceReport* report,
                        ecs::Entity* out);

/// What [[storage::CaptureParents]] and [[storage::ApplyParents]] did.
///
/// - Thread-safety: thread-compatible plain data.
struct ParentReport
{
    /// Links written by `CaptureParents`.
    usize captured = 0;
    /// Children whose parent has no key (their link is not written).
    usize unkeyed = 0;
    /// Parents set by `ApplyParents`.
    usize applied = 0;
    /// Links in a cycle, or that `SetParent` refused (too deep); they were not applied.
    usize rejected = 0;
    /// Missing parents.
    ReferenceReport references;
};

/// Writes a [[storage::ParentLink]] on every entity that has an [[ecs::Parent]] whose parent has a key, and removes
/// links that no longer match a parent.
///
/// - Param world: the world to prepare for a save.
/// - Param index: the keys of `world`, synced.
/// - Param report: counts; may be null.
/// - Returns: success, or `Other` when the world refused a component.
/// - Fails: `Other`.
/// - Aborts: the world is being iterated.
/// - Ownership: both are borrowed for the call.
Status CaptureParents(ecs::World& world, const KeyIndex& index, ParentReport* report);

/// Resolves every [[storage::ParentLink]] in `world` to an [[ecs::Parent]] (with the cycle and depth checks of
/// [[ecs::SetParent]], without its pass over every link) and removes the links.
///
/// All targets are resolved before anything changes: under `Fail` one missing parent changes nothing. A parent that is
/// itself a child is applied first.
///
/// - Param world: the world after a load, every entity created and keyed.
/// - Param index: the keys of `world`, synced.
/// - Param policy: what a missing parent does (`NullReference` leaves the child a root).
/// - Param report: counts and the first missing keys; may be null.
/// - Returns: success, or `MissingReference` under `Fail`.
/// - Fails: `MissingReference`.
/// - Aborts: the world is being iterated.
/// - Ownership: both are borrowed for the call.
Status ApplyParents(ecs::World& world, const KeyIndex& index, MissingPolicy policy, ParentReport* report);

}  // namespace vse::storage
