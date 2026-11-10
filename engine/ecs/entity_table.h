#pragma once

//! The table that says which entities are alive and where their rows are stored.

#include "containers/vector.h"
#include "ecs/entity.h"
#include "lang/utility.h"
#include "types.h"

namespace engine_placeholder::ecs {

/// The largest number of entity slots a table holds (index 0 to this minus one).
inline constexpr u32 kMaxEntitySlots = 0xFFFFFFFEu;

/// The value of [[ecs::EntityLocation::archetype]] that marks a free slot.
inline constexpr u32 kNoArchetype = 0xFFFFFFFFu;

/// Where an alive entity's components are stored.
///
/// Thread-safety: thread-compatible plain data.
/// Ownership: owns nothing.
struct EntityLocation {
    /// The archetype's index in the world, never [[ecs::kNoArchetype]] for an alive entity.
    u32 archetype = kNoArchetype;
    /// The chunk's index in the archetype.
    u32 chunk = 0;
    /// The row in the chunk.
    u32 row = 0;
};

/// Counters of an [[ecs::EntityTable]].
///
/// Thread-safety: thread-compatible plain data.
/// Ownership: owns nothing.
struct EntityTableStats {
    /// Entities alive now.
    u32 alive = 0;
    /// Slots ever created (alive plus free).
    u32 slots = 0;
    /// Free slots waiting for reuse.
    u32 free = 0;
    /// Allocations refused because the table was full.
    u64 refused = 0;
    /// Times a slot's generation wrapped past the 32-bit maximum (and skipped the invalid value 0).
    u64 generation_wraps = 0;
};

/// Slots with generations: allocates and frees entity ids and remembers where each alive entity is stored.
///
/// A freed slot is reused last-in first-out and its generation is incremented, so the old id no longer matches. Generation 0 is never
/// issued: after 2^32 - 1 reuses of one slot the generation wraps to 1 (counted), and an id older than that wrap could match again, which
/// is documented and accepted.
///
/// Thread-safety: thread-compatible. `IsAlive`, `Location` (const) and `Stats` are safe to call concurrently with each other; `Allocate`,
///   `Free` and the mutable `Location` are exclusive (the world calls them at sync points or through its command buffer).
/// Ownership: owns its slots. A pointer from `Location` is valid until the next `Allocate`.
class EntityTable {
public:
    /// Creates an empty table.
    EntityTable() = default;
    EntityTable(const EntityTable&) = delete;
    EntityTable& operator=(const EntityTable&) = delete;
    /// Moves the slots out of `other`, which is left empty.
    EntityTable(EntityTable&& other) noexcept
        : slots_(static_cast<Vector<Slot>&&>(other.slots_)), free_(static_cast<Vector<u32>&&>(other.free_)), alive_(other.alive_),
          max_slots_(other.max_slots_), refused_(other.refused_), wraps_(other.wraps_) {
        other.ResetCounters();
    }
    /// Moves the slots out of `other`, which is left empty.
    EntityTable& operator=(EntityTable&& other) noexcept {
        if (this != &other) {
            slots_ = static_cast<Vector<Slot>&&>(other.slots_);
            free_ = static_cast<Vector<u32>&&>(other.free_);
            alive_ = other.alive_;
            max_slots_ = other.max_slots_;
            refused_ = other.refused_;
            wraps_ = other.wraps_;
            other.ResetCounters();
        }
        return *this;
    }

    /// Allocates an entity at `where`.
    ///
    /// Param where: the entity's initial location; its archetype must not be [[ecs::kNoArchetype]].
    /// Returns: the new id, or the invalid id when the table is full (counted in `Stats().refused`).
    /// Aborts: `where.archetype` is [[ecs::kNoArchetype]].
    /// Fails: returns the invalid id when all [[ecs::kMaxEntitySlots]] slots are alive, or the memory for a new slot cannot be allocated.
    Entity Allocate(EntityLocation where);

    /// Frees `entity` if it is alive; its slot becomes reusable with a new generation.
    ///
    /// Returns: true when the entity was alive.
    /// Fails: returns false for a dead, stale or invalid id; nothing changes.
    bool Free(Entity entity);

    /// Returns whether `entity` names an alive entity.
    /// Fails: never; an invalid, stale or out-of-range id is simply not alive.
    bool IsAlive(Entity entity) const;

    /// The location of `entity`, or null when it is not alive.
    ///
    /// Fails: returns null for a dead, stale or invalid id.
    /// Ownership: the pointer is borrowed from the table and valid until the next `Allocate`.
    EntityLocation* Location(Entity entity);
    /// The location of `entity`, or null when it is not alive.
    ///
    /// Fails: returns null for a dead, stale or invalid id.
    /// Ownership: the pointer is borrowed from the table and valid until the next `Allocate`.
    const EntityLocation* Location(Entity entity) const;

    /// The counters.
    EntityTableStats Stats() const;

    /// Sets the slot limit, for tests that need a full table without allocating four billion slots; `count` must be at least the slots in use.
    void SetMaxSlotsForTesting(u32 count) { max_slots_ = count; }
    /// Sets the generation the next id of slot `index` will carry minus one: the slot's current generation, for tests of the wrap-around.
    /// The slot must exist.
    void SetGenerationForTesting(u32 index, u32 generation);

private:
    void ResetCounters() {
        alive_ = 0;
        refused_ = 0;
        wraps_ = 0;
    }

    struct Slot {
        u32 generation = 1;
        EntityLocation where;
    };

    Vector<Slot> slots_;
    Vector<u32> free_;
    u32 alive_ = 0;
    u32 max_slots_ = kMaxEntitySlots;
    u64 refused_ = 0;
    u64 wraps_ = 0;
};

} // namespace engine_placeholder::ecs
