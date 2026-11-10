#pragma once

//! The world: entities, their components and the structural changes that add, remove and move them.

#include "containers/hash_map.h"
#include "containers/unique_ptr.h"
#include "containers/vector.h"
#include "ecs/archetype.h"
#include "ecs/chunk.h"
#include "ecs/component.h"
#include "ecs/component_set.h"
#include "ecs/entity.h"
#include "ecs/entity_table.h"
#include "lang/new.h"
#include "lang/utility.h"
#include "log/assert.h"
#include "mem/heap_allocator.h"
#include "types.h"

namespace engine_placeholder::ecs {

/// The outcome of a structural call on one entity.
enum class Status : u8 {
    /// The change was made.
    Ok,
    /// The entity is dead, stale or invalid.
    DeadEntity,
    /// The entity lacks the component (`Remove`).
    MissingComponent,
    /// The entity already has the component (`Add`; use `Set` to overwrite).
    AlreadyHas,
    /// A limit stopped it: component or archetype limits, an unregistered type or no memory (counted in `Stats().refused`).
    Refused,
};

/// What [[ecs::World::Validate]] found.
///
/// Thread-safety: thread-compatible plain data.
/// Ownership: `first` is a string literal.
struct ValidationReport {
    /// The number of broken invariants.
    u32 errors = 0;
    /// A description of the first one, or null.
    const char* first = nullptr;
    /// Returns whether nothing is broken.
    /// Fails: never.
    bool Ok() const { return errors == 0; }
};

/// Counters of a [[ecs::World]].
///
/// Thread-safety: thread-compatible plain data.
/// Ownership: owns nothing.
struct WorldStats {
    /// Entities alive.
    u32 entities = 0;
    /// Archetypes created.
    u32 archetypes = 0;
    /// Structural calls that moved an entity to another archetype.
    u64 migrations = 0;
    /// Migrations whose target archetype came from the cached transition graph.
    u64 graph_hits = 0;
    /// Calls refused by a limit (see [[ecs::Status::Refused]]) or entity creations that failed.
    u64 refused = 0;
    /// The chunk pool's counters.
    ChunkPoolStats pool;
};

/// Entities with components, grouped into archetypes.
///
/// Structural calls (`Create`, `Destroy`, `Add`, `Remove`, `Set` that adds) move rows, so a pointer from `Get` or a span from a chunk is valid only
/// until the next structural call. They may not be made while the world is being iterated (`BeginIteration` / `EndIteration`, used by queries); a
/// command buffer (Day 228) defers them.
///
/// Thread-safety: thread-compatible. Const members may run on several threads while no thread makes a structural call.
/// Ownership: owns the entity table, archetypes and chunks; destroying the world destroys every component.
class World {
public:
    /// Creates an empty world with the empty archetype.
    World();
    World(const World&) = delete;
    World& operator=(const World&) = delete;
    ~World() = default;

    /// Creates an entity with no components.
    /// Returns: the entity, or the invalid entity when a limit stopped it.
    /// Aborts: the world is being iterated.
    Entity Create();

    /// Creates an entity with the given components.
    /// Returns: the entity, or the invalid entity when a limit stopped it (nothing is left behind).
    /// Aborts: the world is being iterated, or a type is listed twice.
    template <class... Ts>
    Entity Create(Ts... values);

    /// Creates `count` entities with default-constructed `Ts`, filled in by `fn`.
    ///
    /// Calls `fn(index, Ts&...)` for each. Looks the archetype up once. Stops early if a limit stops it.
    /// Returns: how many were created.
    /// Aborts: the world is being iterated.
    template <class... Ts, class Fn>
    u32 CreateMany(u32 count, Fn&& fn);

    /// Destroys an entity and its components.
    /// Returns: [[ecs::Status::Ok]], or `DeadEntity`.
    /// Aborts: the world is being iterated.
    Status Destroy(Entity entity);

    /// Returns whether `entity` is alive.
    /// Fails: never.
    bool Exists(Entity entity) const { return table_.IsAlive(entity); }

    /// Adds component `T` to a live entity that lacks it, moving it to another archetype.
    /// Returns: `Ok`, `DeadEntity`, `AlreadyHas` or `Refused`.
    /// Aborts: the world is being iterated.
    template <class T>
    Status Add(Entity entity, T value);

    /// Removes component `T`, moving the entity to another archetype.
    /// Returns: `Ok`, `DeadEntity` or `MissingComponent`.
    /// Aborts: the world is being iterated.
    template <class T>
    Status Remove(Entity entity);

    /// Sets component `T`: overwrites it, or adds it when absent.
    /// Returns: `Ok`, `DeadEntity` or `Refused`.
    /// Aborts: the world is being iterated and the entity lacks `T` (overwriting is allowed).
    template <class T>
    Status Set(Entity entity, T value);

    /// Returns whether a live entity has `T` (false for a dead entity).
    /// Fails: never.
    template <class T>
    bool Has(Entity entity) const;

    /// The entity's `T`, or null when the entity is dead or lacks `T` (also null for a tag: ask `Has`).
    /// Fails: returns null as described.
    /// Ownership: the pointer views the entity's row; valid until the next structural call.
    template <class T>
    T* Get(Entity entity);
    /// The entity's `T` (const), or null as above.
    /// Fails: returns null as described.
    /// Ownership: the pointer views the entity's row; valid until the next structural call.
    template <class T>
    const T* Get(Entity entity) const;

    /// Marks the start of an iteration: structural calls are refused until the matching `EndIteration`. Nestable.
    void BeginIteration() { ++iterating_; }
    /// Marks the end of an iteration.
    /// Aborts: no iteration is active.
    void EndIteration() {
        ENGINE_ASSERT(iterating_ != 0, "World::EndIteration without BeginIteration");
        --iterating_;
    }
    /// Returns whether an iteration is active.
    /// Fails: never.
    bool IsIterating() const { return iterating_ != 0; }

    /// The number of archetypes.
    u32 ArchetypeCount() const { return static_cast<u32>(archetypes_.Size()); }
    /// The archetype at `index`.
    /// Aborts: `index` is not below `ArchetypeCount()`.
    Archetype& ArchetypeAt(u32 index);
    /// The archetype at `index`.
    /// Aborts: `index` is not below `ArchetypeCount()`.
    const Archetype& ArchetypeAt(u32 index) const;
    /// The entity table.
    const EntityTable& Entities() const { return table_; }

    /// The counters.
    WorldStats Stats() const;

    /// Checks the world's invariants.
    ///
    /// Every alive entity's location holds that entity, no row holds a dead or duplicated entity, counts add up, and every
    /// archetype's chunks are full except the last. For tests and debug builds.
    ValidationReport Validate() const;

private:
    static constexpr u32 kNone = 0xFFFFFFFFu;

    void NotIterating(const char* call) const {
        ENGINE_ASSERT(iterating_ == 0, call);
    }
    u32 FindOrCreateArchetype(const ComponentSet& set);
    u32 Transition(u32 from, ComponentId id, bool add);
    Entity CreateIn(u32 archetype, RowRef* row);
    void FixMoved(Entity moved, u32 chunk, u32 row);
    bool Migrate(Entity entity, u32 target, RowRef* new_row);
    template <class T>
    void ConstructCell(const Archetype& a, const RowRef& r, T&& value);

    // The pool is declared first so it is destroyed last: archetypes return their chunks to it.
    ChunkPool pool_;
    EntityTable table_;
    Vector<UniquePtr<Archetype>> archetypes_;
    HashMap<ComponentSet, u32, HeapAllocator> by_set_;
    HashMap<u64, u32, HeapAllocator> edges_;
    u32 iterating_ = 0;
    u64 migrations_ = 0;
    u64 graph_hits_ = 0;
    u64 refused_ = 0;
};

template <class T>
void World::ConstructCell(const Archetype& a, const RowRef& r, T&& value) {
    using U = RemoveReference<T>;
    void* cell = a.CellPtr(r.chunk, ComponentOf<U>(), r.row);
    if (cell != nullptr) {
        ConstructAt<U>(cell, Move(value));
    }
}

template <class... Ts>
Entity World::Create(Ts... values) {
    NotIterating("World::Create during iteration");
    ComponentSet set;
    bool registered = ((ComponentOf<Ts>() != kInvalidComponent) && ...);
    if (!registered) {
        ++refused_;
        return Entity{};
    }
    (set.Add(ComponentOf<Ts>()), ...);
    ENGINE_ASSERT(set.Count() == sizeof...(Ts), "World::Create: a component type is listed twice");
    const u32 archetype = FindOrCreateArchetype(set);
    if (archetype == kNone) {
        ++refused_;
        return Entity{};
    }
    RowRef row;
    const Entity entity = CreateIn(archetype, &row);
    if (entity.IsValid()) {
        (ConstructCell(*archetypes_[archetype], row, Move(values)), ...);
    }
    return entity;
}

template <class... Ts, class Fn>
u32 World::CreateMany(u32 count, Fn&& fn) {
    NotIterating("World::CreateMany during iteration");
    static_assert(((IsDefaultConstructible<Ts>) && ...), "CreateMany default-constructs the components first");
    ComponentSet set;
    if (!((ComponentOf<Ts>() != kInvalidComponent) && ...)) {
        ++refused_;
        return 0;
    }
    (set.Add(ComponentOf<Ts>()), ...);
    ENGINE_ASSERT(set.Count() == sizeof...(Ts), "World::CreateMany: a component type is listed twice");
    const u32 archetype = FindOrCreateArchetype(set);
    if (archetype == kNone) {
        ++refused_;
        return 0;
    }
    Archetype& a = *archetypes_[archetype];
    u32 made = 0;
    for (; made < count; ++made) {
        RowRef row;
        const Entity entity = CreateIn(archetype, &row);
        if (!entity.IsValid()) {
            break;
        }
        // A tag has no cell; fn is handed one shared scratch object for it.
        auto cell = [&]<class U>(U*) -> U& {
            void* where = a.CellPtr(row.chunk, ComponentOf<U>(), row.row);
            if (where != nullptr) {
                return *ConstructAt<U>(where);
            }
            static U tag;
            return tag;
        };
        fn(made, cell(static_cast<Ts*>(nullptr))...);
    }
    return made;
}

template <class T>
Status World::Add(Entity entity, T value) {
    NotIterating("World::Add during iteration");
    const EntityLocation* where = table_.Location(entity);
    if (where == nullptr) {
        return Status::DeadEntity;
    }
    const ComponentId id = ComponentOf<T>();
    if (id == kInvalidComponent) {
        ++refused_;
        return Status::Refused;
    }
    if (archetypes_[where->archetype]->Has(id)) {
        return Status::AlreadyHas;
    }
    const u32 target = Transition(where->archetype, id, true);
    if (target == kNone) {
        ++refused_;
        return Status::Refused;
    }
    RowRef row;
    if (!Migrate(entity, target, &row)) {
        ++refused_;
        return Status::Refused;
    }
    ConstructCell(*archetypes_[target], row, Move(value));
    return Status::Ok;
}

template <class T>
Status World::Remove(Entity entity) {
    NotIterating("World::Remove during iteration");
    const EntityLocation* where = table_.Location(entity);
    if (where == nullptr) {
        return Status::DeadEntity;
    }
    const ComponentId id = ComponentOf<T>();
    if (id == kInvalidComponent || !archetypes_[where->archetype]->Has(id)) {
        return Status::MissingComponent;
    }
    const u32 target = Transition(where->archetype, id, false);
    RowRef row;
    if (target == kNone || !Migrate(entity, target, &row)) {
        ++refused_;
        return Status::Refused;
    }
    return Status::Ok;
}

template <class T>
Status World::Set(Entity entity, T value) {
    const EntityLocation* where = table_.Location(entity);
    if (where == nullptr) {
        return Status::DeadEntity;
    }
    const ComponentId id = ComponentOf<T>();
    if (id != kInvalidComponent && archetypes_[where->archetype]->Has(id)) {
        Archetype& a = *archetypes_[where->archetype];
        void* cell = a.CellPtr(a.ChunkAt(where->chunk), id, where->row);
        if (cell != nullptr) {
            static_cast<T*>(cell)->~T();
            ConstructAt<T>(cell, Move(value));
        }
        return Status::Ok;
    }
    return Add<T>(entity, Move(value));
}

template <class T>
bool World::Has(Entity entity) const {
    const EntityLocation* where = table_.Location(entity);
    return where != nullptr && archetypes_[where->archetype]->Has(ComponentOf<T>());
}

template <class T>
T* World::Get(Entity entity) {
    const EntityLocation* where = table_.Location(entity);
    if (where == nullptr) {
        return nullptr;
    }
    Archetype& a = *archetypes_[where->archetype];
    return static_cast<T*>(a.CellPtr(a.ChunkAt(where->chunk), ComponentOf<T>(), where->row));
}

template <class T>
const T* World::Get(Entity entity) const {
    return const_cast<World*>(this)->Get<T>(entity);
}

} // namespace engine_placeholder::ecs
