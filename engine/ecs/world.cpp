#include "ecs/world.h"

#include "cstr/mem_ops.h"
#include "sync/atomic.h"

namespace engine_placeholder::ecs {

namespace detail {
u32 NextResourceTypeId() {
    static Atomic<u32> next{0};
    return next.FetchAdd(1, MemoryOrder::Relaxed);
}
u64 NextWorldId() {
    static Atomic<u64> next{1};
    return next.FetchAdd(1, MemoryOrder::Relaxed);
}
} // namespace detail

World::~World() {
    DestroyAllResources();
}

World::ResourceSlot* World::FindResource(u32 type_id) {
    if (type_id >= resources_.Size() || resources_[type_id].object == nullptr) {
        return nullptr;
    }
    return &resources_[type_id];
}

const World::ResourceSlot* World::FindResource(u32 type_id) const {
    return const_cast<World*>(this)->FindResource(type_id);
}

void World::DestroyResource(ResourceSlot& slot) {
    slot.destroy(slot.object);
    HeapAllocator::Default().Free(slot.object, slot.size, slot.align);
    slot = ResourceSlot{};
}

void World::DestroyAllResources() {
    // Newest first: a resource may refer to those made before it.
    for (;;) {
        ResourceSlot* newest = nullptr;
        for (usize i = 0; i < resources_.Size(); ++i) {
            if (resources_[i].object != nullptr && (newest == nullptr || resources_[i].serial > newest->serial)) {
                newest = &resources_[i];
            }
        }
        if (newest == nullptr) {
            return;
        }
        DestroyResource(*newest);
    }
}

World::World() : id_(detail::NextWorldId()), by_set_(HeapAllocator::Default()), edges_(HeapAllocator::Default()) {
    const u32 empty = FindOrCreateArchetype(ComponentSet{});
    ENGINE_ASSERT(empty == 0, "the empty archetype is archetype 0");
}

u32 World::FindOrCreateArchetype(const ComponentSet& set) {
    if (const u32* found = by_set_.Find(set)) {
        return *found;
    }
    if (set.Count() > kMaxArchetypeComponents) {
        return kNone;
    }
    const u32 index = static_cast<u32>(archetypes_.Size());
    UniquePtr<Archetype> fresh = MakeUnique<Archetype>(index, set, ComponentRegistry::Global(), pool_);
    if (!fresh->Valid() || !archetypes_.TryReserve(archetypes_.Size() + 1)) {
        return kNone;
    }
    archetypes_.Push(Move(fresh));
    by_set_.Insert(set, index);
    return index;
}

u32 World::Transition(u32 from, ComponentId id, bool add) {
    const u64 key = (static_cast<u64>(from) << 16) | (static_cast<u64>(id) << 1) | (add ? 1u : 0u);
    if (const u32* cached = edges_.Find(key)) {
        ++graph_hits_;
        return *cached;
    }
    ComponentSet set = archetypes_[from]->Components();
    if (add) {
        set.Add(id);
    } else {
        set.Remove(id);
    }
    const u32 target = FindOrCreateArchetype(set);
    if (target != kNone) {
        edges_.Insert(key, target);
    }
    return target;
}

Entity World::CreateIn(u32 archetype, RowRef* row) {
    Entity entity = table_.Allocate(EntityLocation{archetype, 0, 0});
    if (!entity.IsValid()) {
        ++refused_;
        return Entity{};
    }
    if (!archetypes_[archetype]->AddRow(entity, row)) {
        table_.Free(entity);
        ++refused_;
        return Entity{};
    }
    archetypes_[archetype]->MarkArrived(row->chunk, tick_);
    EntityLocation* where = table_.Location(entity);
    where->chunk = row->chunk_index;
    where->row = row->row;
    return entity;
}

Entity World::Create() {
    NotIterating("World::Create during iteration");
    RowRef row;
    return CreateIn(0, &row);
}

void World::FixMoved(Entity moved, u32 chunk, u32 row) {
    if (moved.IsValid()) {
        EntityLocation* where = table_.Location(moved);
        where->chunk = chunk;
        where->row = row;
    }
}

Status World::Destroy(Entity entity) {
    NotIterating("World::Destroy during iteration");
    const EntityLocation* where = table_.Location(entity);
    if (where == nullptr) {
        return Status::DeadEntity;
    }
    const EntityLocation at = *where;
    Archetype& home = *archetypes_[at.archetype];
    const Entity moved = home.RemoveRow(at.chunk, at.row);
    FixMoved(moved, at.chunk, at.row);
    if (moved.IsValid()) {
        home.MarkAllWritten(home.ChunkAt(at.chunk), tick_); // the last row now sits in the gap
    }
    table_.Free(entity);
    ++destroyed_;
    return Status::Ok;
}

// Moves the entity's row to `target`: components in both are relocated, a dropped one is destroyed. The caller constructs the added component
// in the returned row (none for a removal).
bool World::Migrate(Entity entity, u32 target, RowRef* new_row) {
    EntityLocation* where = table_.Location(entity);
    const EntityLocation from = *where;
    Archetype& source = *archetypes_[from.archetype];
    Archetype& dest = *archetypes_[target];
    if (!dest.AddRow(entity, new_row)) {
        return false;
    }
    dest.MarkArrived(new_row->chunk, tick_);
    Chunk* source_chunk = source.ChunkAt(from.chunk);
    const ArchetypeLayout& layout = source.Layout();
    for (u32 c = 1; c < layout.column_count; ++c) {
        const ComponentId id = layout.component[c];
        const TypeInfo& info = *layout.info[c];
        void* from_cell = source.CellPtr(source_chunk, id, from.row);
        void* to_cell = dest.CellPtr(new_row->chunk, id, new_row->row);
        if (to_cell == nullptr) {
            if (!info.trivially_relocatable) {
                info.destroy(from_cell);
            }
        } else if (info.trivially_relocatable) {
            MemCopy(to_cell, from_cell, info.size);
        } else {
            info.move_construct(to_cell, from_cell);
            info.destroy(from_cell);
        }
    }
    const Entity moved = source.RemoveRow(from.chunk, from.row, RowContents::Vacated);
    FixMoved(moved, from.chunk, from.row);
    if (moved.IsValid()) {
        source.MarkAllWritten(source.ChunkAt(from.chunk), tick_);
    }
    // The table slot may have been touched by FixMoved only for another entity; re-fetch ours.
    where = table_.Location(entity);
    where->archetype = target;
    where->chunk = new_row->chunk_index;
    where->row = new_row->row;
    ++migrations_;
    return true;
}

Archetype& World::ArchetypeAt(u32 index) {
    ENGINE_ASSERT(index < archetypes_.Size(), "World::ArchetypeAt: no such archetype");
    return *archetypes_[index];
}

const Archetype& World::ArchetypeAt(u32 index) const {
    ENGINE_ASSERT(index < archetypes_.Size(), "World::ArchetypeAt: no such archetype");
    return *archetypes_[index];
}

WorldStats World::Stats() const {
    WorldStats stats;
    stats.entities = table_.Stats().alive;
    stats.archetypes = static_cast<u32>(archetypes_.Size());
    stats.migrations = migrations_;
    stats.graph_hits = graph_hits_;
    stats.refused = refused_;
    stats.destroyed = destroyed_;
    stats.pool = pool_.Stats();
    return stats;
}

ValidationReport World::Validate() const {
    ValidationReport report;
    auto fail = [&](const char* what) {
        if (report.errors++ == 0) {
            report.first = what;
        }
    };
    u32 rows = 0;
    for (usize ai = 0; ai < archetypes_.Size(); ++ai) {
        const Archetype& a = *archetypes_[ai];
        u32 counted = 0;
        for (u32 c = 0; c < a.ChunkCount(); ++c) {
            Chunk* chunk = a.ChunkAt(c);
            if (chunk->count == 0 || chunk->count > chunk->capacity) {
                fail("a chunk is empty or over capacity");
            }
            if (c + 1 < a.ChunkCount() && chunk->count != chunk->capacity) {
                fail("a chunk before the last is not full");
            }
            if (chunk->archetype != ai) {
                fail("a chunk names the wrong archetype");
            }
            Span<Entity> entities = a.Entities(chunk);
            for (usize r = 0; r < entities.Size(); ++r) {
                const EntityLocation* where = table_.Location(entities[r]);
                if (where == nullptr) {
                    fail("a row holds a dead entity");
                } else if (where->archetype != ai || where->chunk != c || where->row != r) {
                    fail("an entity's location does not point at its row");
                }
            }
            counted += chunk->count;
        }
        if (counted != a.EntityCount()) {
            fail("an archetype's entity count differs from its chunks");
        }
        const ArchetypeLayout& layout = a.Layout();
        for (ComponentId id = a.Components().NextFrom(0); id != kInvalidComponent; id = a.Components().NextFrom(static_cast<u32>(id) + 1)) {
            const bool tag = ComponentRegistry::Global().Info(id).is_tag;
            if (tag != (layout.column_of[id] == kNoColumn)) {
                fail("a tag has a column or a component lacks one");
            }
        }
        rows += counted;
    }
    if (rows != table_.Stats().alive) {
        fail("the rows do not add up to the alive entities");
    }
    return report;
}

} // namespace engine_placeholder::ecs
