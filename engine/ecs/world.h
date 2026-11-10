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

template <class... Terms>
class Query;
class CommandBuffer;

namespace detail {
/// The next number of a resource type: one per type per process, in order of first use (thread-safe).
u32 NextResourceTypeId();
/// The next world number (thread-safe, never reused).
u64 NextWorldId();
/// The number of the resource type `T`.
template <class T>
u32 ResourceTypeId() {
    static const u32 id = NextResourceTypeId();
    return id;
}
} // namespace detail

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

/// What applying a [[ecs::CommandBuffer]] did.
///
/// Thread-safety: thread-compatible plain data.
struct PlaybackReport {
    /// Commands that changed the world.
    u32 applied = 0;
    /// Commands that did nothing: the entity was dead, lacked or already had the component, or a limit stopped it.
    u32 ignored = 0;
    /// Entities created by the applied `Create` commands (also counted in `applied`).
    u32 created = 0;
    /// Adds another report to this one.
    /// Fails: never.
    PlaybackReport& operator+=(const PlaybackReport& other) {
        applied += other.applied;
        ignored += other.ignored;
        created += other.created;
        return *this;
    }
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
    /// A number no other world of this program has had (never reused), for objects that cache something about a world.
    /// Fails: never.
    u64 Id() const { return id_; }
    /// Destroys the resources in reverse order of creation, then the entities and their archetypes. A resource's destructor must not use the world.
    ~World();

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

    /// The entity's `T`, or null when the entity is dead or lacks `T` (also null for a tag: ask `Has`). The non-const form stamps the chunk's `T` column as
    /// written at the current tick, since the caller may write through the pointer.
    /// Fails: returns null as described.
    /// Aborts: an active query (`BeginIteration(writes)`) writes `T`; read components the query only reads, or use the query's own arguments.
    /// Ownership: the pointer views the entity's row; valid until the next structural call.
    template <class T>
    T* Get(Entity entity);
    /// The entity's `T` (const), or null as above.
    /// Fails: returns null as described.
    /// Aborts: as for the non-const form.
    /// Ownership: the pointer views the entity's row; valid until the next structural call.
    template <class T>
    const T* Get(Entity entity) const;

    /// The current change-detection tick (starts at 1). Writes, additions and arrivals are stamped with it.
    /// Fails: never.
    u64 Tick() const { return tick_; }
    /// Moves to the next tick; changes stamped from now on are newer than any query's `since` taken before. The scheduler calls it between
    /// frames, and [[ecs::Query::Finish]] calls it so that a write made after a query ran is not lost.
    /// Fails: never.
    void Advance() { ++tick_; }
    /// Sets the tick (tests of tick arithmetic near the largest value).
    /// Fails: never.
    void SetTick(u64 tick) { tick_ = tick; }

    /// Begins a parallel phase: the schedule's jobs run queries of this world on several threads. Structural calls are refused (it counts as an iteration),
    /// and queries skip their own iteration bookkeeping, which is not thread-safe. Call it on the thread that submits the jobs, before submitting.
    /// Fails: never.
    void BeginParallel() {
        BeginIteration();
        parallel_ = true;
    }
    /// Ends the parallel phase begun by `BeginParallel`, after every job has finished.
    /// Aborts: no parallel phase is active.
    void EndParallel() {
        ENGINE_ASSERT(parallel_, "World::EndParallel without BeginParallel");
        parallel_ = false;
        EndIteration();
    }
    /// Returns whether a parallel phase is active.
    /// Fails: never.
    bool IsParallel() const { return parallel_; }

    /// Marks the start of an iteration: structural calls are refused until the matching `EndIteration`. Nestable.
    void BeginIteration() { ++iterating_; }
    /// Marks the end of an iteration.
    /// Aborts: no iteration is active.
    void EndIteration() {
        ENGINE_ASSERT(iterating_ != 0, "World::EndIteration without BeginIteration");
        --iterating_;
    }
    /// Marks the start of an iteration that writes the components in `writes`: like the no-argument form, and in addition `World::Get` of
    /// one of those components aborts until the matching `EndIteration` (the iteration holds them for writing). Queries use this.
    ///
    /// Param writes: the component ids the iteration writes.
    /// Returns: the write set of the enclosing iteration, to pass back to `EndIteration(const ComponentSet&)`.
    ComponentSet BeginIteration(const ComponentSet& writes) {
        ++iterating_;
        const ComponentSet previous = iteration_writes_;
        for (ComponentId id = writes.NextFrom(0); id != kInvalidComponent; id = writes.NextFrom(static_cast<u32>(id) + 1u)) {
            iteration_writes_.Add(id);
        }
        return previous;
    }
    /// Marks the end of an iteration begun with `BeginIteration(const ComponentSet&)`.
    ///
    /// Param previous: the value `BeginIteration` returned.
    /// Aborts: no iteration is active.
    void EndIteration(const ComponentSet& previous) {
        EndIteration();
        iteration_writes_ = previous;
    }
    /// Returns whether an iteration is active.
    /// Fails: never.
    bool IsIterating() const { return iterating_ != 0; }

    /// Makes a [[ecs::Query]] over this world with the given terms (`Read<T>`, `Write<T>`, `Optional<T>`, `OptionalWrite<T>`, `With<T>`, `Without<T>`).
    ///
    /// Include `ecs/query.h` to call it. The query keeps a pointer to this world, which must outlive it.
    /// A term list that is empty, longer than 16, names a component twice or reads a tag as data does not compile: see [[ecs::Query]].
    template <class... Terms>
    ecs::Query<Terms...> Query();

    /// Applies the recorded commands of `buffer` in recording order, at a sync point.
    ///
    /// A command on a dead entity, a missing or already present component, or a refused change is skipped and counted, never a crash. The buffer's
    /// placeholders resolve to the created entities ([[ecs::CommandBuffer::Resolve]]) until the buffer is cleared. Several buffers are applied by
    /// calling this once per buffer in the order the caller chooses.
    ///
    /// Param buffer: the commands; consumed (its values are moved into the world and destroyed).
    /// Returns: how many commands changed the world, were skipped and created entities.
    /// Aborts: the world is being iterated.
    PlaybackReport Apply(CommandBuffer& buffer);

    /// Stores the world's one resource of type `T`, replacing the previous one in place (its address does not change).
    ///
    /// A resource is a singleton per type that is not an entity and never matches a query. A new resource is destroyed after those created later.
    ///
    /// Param value: the value; moved in.
    /// Returns: the stored resource.
    /// Fails: never.
    /// Ownership: the world owns the resource; the reference is valid until `RemoveResource<T>()` or the world's destruction.
    template <class T>
    T& SetResource(T value);
    /// The resource of type `T`.
    /// Aborts: there is none (ask `HasResource<T>()` or use `TryResource<T>()`).
    template <class T>
    T& Resource();
    /// The resource of type `T` (const).
    /// Aborts: there is none.
    template <class T>
    const T& Resource() const;
    /// The resource of type `T`, or null.
    /// Fails: returns null when there is none.
    /// Ownership: the world owns the resource; the pointer is valid until it is removed or the world is destroyed.
    template <class T>
    T* TryResource();
    /// The resource of type `T` (const), or null.
    /// Fails: returns null when there is none.
    /// Ownership: the world owns the resource; the pointer is valid until it is removed or the world is destroyed.
    template <class T>
    const T* TryResource() const;
    /// Returns whether a resource of type `T` is stored.
    /// Fails: never.
    template <class T>
    bool HasResource() const { return TryResource<T>() != nullptr; }
    /// Destroys the resource of type `T`.
    /// Returns: whether there was one.
    /// Fails: returns false when there is none.
    template <class T>
    bool RemoveResource();

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

    // One slot per resource type number; the object lives in its own heap block so its address is stable.
    struct ResourceSlot {
        void* object = nullptr;
        void (*destroy)(void*) = nullptr;
        usize size = 0;
        usize align = 0;
        u64 serial = 0;
    };
    ResourceSlot* FindResource(u32 type_id);
    const ResourceSlot* FindResource(u32 type_id) const;
    void DestroyResource(ResourceSlot& slot);
    void DestroyAllResources();

    // The pool is declared first so it is destroyed last: archetypes return their chunks to it.
    ChunkPool pool_;
    EntityTable table_;
    Vector<UniquePtr<Archetype>> archetypes_;
    u64 id_;
    HashMap<ComponentSet, u32, HeapAllocator> by_set_;
    HashMap<u64, u32, HeapAllocator> edges_;
    template <class T>
    T* GetCell(Entity entity, bool mark);
    u32 iterating_ = 0;
    bool parallel_ = false;
    u64 tick_ = 1;
    ComponentSet iteration_writes_;
    Vector<ResourceSlot> resources_;
    u64 next_resource_serial_ = 1;
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
            a.MarkWritten(a.ChunkAt(where->chunk), id, tick_);
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
    return GetCell<T>(entity, true);
}

template <class T>
const T* World::Get(Entity entity) const {
    return const_cast<World*>(this)->GetCell<T>(entity, false);
}

template <class T>
T* World::GetCell(Entity entity, bool mark) {
    const EntityLocation* where = table_.Location(entity);
    if (where == nullptr) {
        return nullptr;
    }
    const ComponentId id = ComponentOf<T>();
    ENGINE_ASSERT(!iteration_writes_.Has(id), "World::Get of a component that an active query writes");
    Archetype& a = *archetypes_[where->archetype];
    Chunk* chunk = a.ChunkAt(where->chunk);
    if (mark) {
        a.MarkWritten(chunk, id, tick_);
    }
    return static_cast<T*>(a.CellPtr(chunk, id, where->row));
}

template <class T>
T& World::SetResource(T value) {
    static_assert(!IsReference<T> && !IsConst<T> && !IsArray<T>, "a resource type must be a plain object type");
    const u32 id = detail::ResourceTypeId<T>();
    if (id >= resources_.Size()) {
        resources_.Resize(id + 1);
    }
    ResourceSlot& slot = resources_[id];
    if (slot.object != nullptr) {
        T* current = static_cast<T*>(slot.object);
        current->~T();
        ConstructAt<T>(current, Move(value));
        return *current;
    }
    void* memory = HeapAllocator::Default().Alloc(sizeof(T), alignof(T));
    ENGINE_ASSERT(memory != nullptr, "World::SetResource: out of memory");
    ConstructAt<T>(memory, Move(value));
    slot.object = memory;
    slot.size = sizeof(T);
    slot.align = alignof(T);
    slot.destroy = [](void* object) { static_cast<T*>(object)->~T(); };
    slot.serial = next_resource_serial_++;
    return *static_cast<T*>(memory);
}

template <class T>
T* World::TryResource() {
    ResourceSlot* slot = FindResource(detail::ResourceTypeId<T>());
    return slot == nullptr ? nullptr : static_cast<T*>(slot->object);
}

template <class T>
const T* World::TryResource() const {
    const ResourceSlot* slot = FindResource(detail::ResourceTypeId<T>());
    return slot == nullptr ? nullptr : static_cast<const T*>(slot->object);
}

template <class T>
T& World::Resource() {
    T* found = TryResource<T>();
    ENGINE_ASSERT(found != nullptr, "World::Resource: the world has no resource of this type");
    return *found;
}

template <class T>
const T& World::Resource() const {
    const T* found = TryResource<T>();
    ENGINE_ASSERT(found != nullptr, "World::Resource: the world has no resource of this type");
    return *found;
}

template <class T>
bool World::RemoveResource() {
    ResourceSlot* slot = FindResource(detail::ResourceTypeId<T>());
    if (slot == nullptr) {
        return false;
    }
    DestroyResource(*slot);
    return true;
}

} // namespace engine_placeholder::ecs
