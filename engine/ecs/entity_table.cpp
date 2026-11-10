#include "ecs/entity_table.h"

#include "log/assert.h"

namespace engine_placeholder::ecs {

Entity EntityTable::Allocate(EntityLocation where) {
    ENGINE_ASSERT(where.archetype != kNoArchetype, "EntityTable::Allocate needs a real archetype");
    u32 index;
    if (free_.Size() != 0) {
        index = free_[free_.Size() - 1];
        free_.Pop();
    } else {
        if (slots_.Size() >= max_slots_) {
            ++refused_;
            return Entity{};
        }
        if (!slots_.TryResize(slots_.Size() + 1)) {
            ++refused_;
            return Entity{};
        }
        index = static_cast<u32>(slots_.Size() - 1);
    }
    Slot& slot = slots_[index];
    slot.where = where;
    ++alive_;
    return Entity{index, slot.generation};
}

bool EntityTable::Free(Entity entity) {
    if (!IsAlive(entity)) {
        return false;
    }
    Slot& slot = slots_[entity.index];
    slot.where = EntityLocation{};
    ++slot.generation;
    if (slot.generation == 0 || slot.generation == Entity::kPlaceholderGeneration) {
        slot.generation = 1;
        ++wraps_;
    }
    free_.Push(entity.index);
    --alive_;
    return true;
}

bool EntityTable::IsAlive(Entity entity) const {
    if (entity.generation == 0 || entity.index >= slots_.Size()) {
        return false;
    }
    const Slot& slot = slots_[entity.index];
    return slot.generation == entity.generation && slot.where.archetype != kNoArchetype;
}

EntityLocation* EntityTable::Location(Entity entity) {
    return IsAlive(entity) ? &slots_[entity.index].where : nullptr;
}

const EntityLocation* EntityTable::Location(Entity entity) const {
    return IsAlive(entity) ? &slots_[entity.index].where : nullptr;
}

EntityTableStats EntityTable::Stats() const {
    EntityTableStats stats;
    stats.alive = alive_;
    stats.slots = static_cast<u32>(slots_.Size());
    stats.free = static_cast<u32>(free_.Size());
    stats.refused = refused_;
    stats.generation_wraps = wraps_;
    return stats;
}

void EntityTable::SetGenerationForTesting(u32 index, u32 generation) {
    ENGINE_ASSERT(index < slots_.Size() && generation != 0, "SetGenerationForTesting: no such slot, or generation 0");
    slots_[index].generation = generation;
}

} // namespace engine_placeholder::ecs
