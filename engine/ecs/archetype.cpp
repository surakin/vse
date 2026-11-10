#include "ecs/archetype.h"

#include "cstr/mem_ops.h"
#include "log/assert.h"

namespace engine_placeholder::ecs {

Archetype::Archetype(u32 index, const ComponentSet& components, const ComponentRegistry& registry, ChunkPool& pool)
    : index_(index), components_(components), pool_(&pool) {
    ENGINE_ASSERT(components.Count() <= kMaxArchetypeComponents, "Archetype: too many components");
    for (u32 i = 0; i < kMaxComponentTypes; ++i) {
        layout_.column_of[i] = kNoColumn;
    }
    // Logical columns: 0 is the entity column, then the non-tag components in ascending id order.
    u32 n = 0;
    layout_.component[n] = kInvalidComponent;
    layout_.size[n] = sizeof(Entity);
    layout_.info[n] = nullptr;
    Array<u32, kMaxArchetypeComponents + 1> align;
    align[n] = alignof(Entity);
    ++n;
    for (ComponentId id = components.NextFrom(0); id != kInvalidComponent; id = components.NextFrom(static_cast<u32>(id) + 1)) {
        const TypeInfo& info = registry.Info(id);
        if (info.is_tag) {
            continue;
        }
        layout_.component[n] = id;
        layout_.size[n] = static_cast<u32>(info.size);
        layout_.info[n] = &info;
        align[n] = static_cast<u32>(info.align);
        layout_.column_of[id] = n;
        ++n;
    }
    layout_.column_count = n;
    u32 row_bytes = 0;
    for (u32 c = 0; c < n; ++c) {
        row_bytes += layout_.size[c];
    }
    layout_.row_bytes = row_bytes;
    const u32 room = static_cast<u32>(kChunkBytes - sizeof(Chunk));
    if (row_bytes > room) {
        pool.CountRefusedLayout();
        return;
    }
    const u32 capacity = room / row_bytes;
    // Memory order: descending alignment (stable), so each column starts aligned with no padding.
    Array<u32, kMaxArchetypeComponents + 1> order;
    for (u32 i = 0; i < n; ++i) {
        u32 j = i;
        while (j > 0 && align[order[j - 1]] < align[i]) {
            order[j] = order[j - 1];
            --j;
        }
        order[j] = i;
    }
    u32 at = 0;
    for (u32 k = 0; k < n; ++k) {
        const u32 c = order[k];
        ENGINE_ASSERT(at % align[c] == 0, "Archetype: a column would start misaligned");
        layout_.offset[c] = at;
        at += layout_.size[c] * capacity;
    }
    layout_.capacity = capacity;
}

Archetype::~Archetype() {
    for (usize i = 0; i < chunks_.Size(); ++i) {
        Chunk* chunk = chunks_[i];
        for (u32 row = 0; row < chunk->count; ++row) {
            DestroyCells(chunk, row);
        }
        pool_->Release(chunk);
    }
}

void Archetype::DestroyCells(Chunk* chunk, u32 row) {
    for (u32 c = 1; c < layout_.column_count; ++c) {
        const TypeInfo* info = layout_.info[c];
        if (!info->trivially_relocatable) {
            info->destroy(chunk->Data() + layout_.offset[c] + static_cast<usize>(layout_.size[c]) * row);
        }
    }
}

void Archetype::MoveRow(Chunk* from, u32 from_row, Chunk* to, u32 to_row) {
    for (u32 c = 0; c < layout_.column_count; ++c) {
        const usize size = layout_.size[c];
        byte* source = from->Data() + layout_.offset[c] + size * from_row;
        byte* destination = to->Data() + layout_.offset[c] + size * to_row;
        const TypeInfo* info = layout_.info[c];
        if (info == nullptr || info->trivially_relocatable) {
            MemCopy(destination, source, size);
        } else {
            info->move_construct(destination, source);
            info->destroy(source);
        }
    }
}

bool Archetype::AddRow(Entity entity, RowRef* row) {
    if (!Valid()) {
        return false;
    }
    if (chunks_.Empty() || chunks_[chunks_.Size() - 1]->count == layout_.capacity) {
        if (!chunks_.TryReserve(chunks_.Size() + 1)) {
            return false;
        }
        Chunk* fresh = pool_->Acquire(index_, layout_.capacity);
        if (fresh == nullptr) {
            return false;
        }
        chunks_.Push(fresh);
    }
    Chunk* chunk = chunks_[chunks_.Size() - 1];
    const u32 at = chunk->count++;
    reinterpret_cast<Entity*>(chunk->Data() + layout_.offset[0])[at] = entity;
    ++entity_count_;
    row->chunk_index = static_cast<u32>(chunks_.Size() - 1);
    row->row = at;
    row->chunk = chunk;
    return true;
}

Entity Archetype::RemoveRow(u32 chunk_index, u32 row, RowContents contents) {
    ENGINE_ASSERT(chunk_index < chunks_.Size() && row < chunks_[chunk_index]->count, "Archetype::RemoveRow: the row does not exist");
    Chunk* chunk = chunks_[chunk_index];
    Chunk* last = chunks_[chunks_.Size() - 1];
    const u32 last_row = last->count - 1;
    if (contents == RowContents::Alive) {
        DestroyCells(chunk, row);
    }
    Entity moved;
    if (chunk != last || row != last_row) {
        MoveRow(last, last_row, chunk, row);
        moved = reinterpret_cast<Entity*>(chunk->Data() + layout_.offset[0])[row];
    }
    --last->count;
    --entity_count_;
    if (last->count == 0) {
        pool_->Release(last);
        chunks_.Pop();
    }
    return moved;
}

Chunk* Archetype::ChunkAt(u32 chunk_index) const {
    ENGINE_ASSERT(chunk_index < chunks_.Size(), "Archetype::ChunkAt: no such chunk");
    return chunks_[chunk_index];
}

Span<Entity> Archetype::Entities(Chunk* chunk) const {
    return Span<Entity>(reinterpret_cast<Entity*>(chunk->Data() + layout_.offset[0]), chunk->count);
}

void* Archetype::ColumnPtr(Chunk* chunk, ComponentId id) const {
    if (id >= kMaxComponentTypes || layout_.column_of[id] == kNoColumn) {
        return nullptr;
    }
    return chunk->Data() + layout_.offset[layout_.column_of[id]];
}

void* Archetype::CellPtr(Chunk* chunk, ComponentId id, u32 row) const {
    if (id >= kMaxComponentTypes || layout_.column_of[id] == kNoColumn) {
        return nullptr;
    }
    const u32 c = layout_.column_of[id];
    return chunk->Data() + layout_.offset[c] + static_cast<usize>(layout_.size[c]) * row;
}

void Archetype::MarkArrived(Chunk* chunk, u64 tick) const {
    for (u32 c = 1; c < layout_.column_count; ++c) {
        chunk->written[c - 1] = tick;
        chunk->added[c - 1] = tick;
    }
}

void Archetype::MarkAllWritten(Chunk* chunk, u64 tick) const {
    for (u32 c = 1; c < layout_.column_count; ++c) {
        chunk->written[c - 1] = tick;
    }
}

void Archetype::MarkWritten(Chunk* chunk, ComponentId id, u64 tick) const {
    const u32 column = id < kMaxComponentTypes ? layout_.column_of[id] : kNoColumn;
    if (column != kNoColumn) {
        chunk->written[column - 1] = tick;
    }
}

u64 Archetype::WrittenTick(const Chunk* chunk, ComponentId id) const {
    const u32 column = id < kMaxComponentTypes ? layout_.column_of[id] : kNoColumn;
    return column == kNoColumn ? 0 : chunk->written[column - 1];
}

u64 Archetype::AddedTick(const Chunk* chunk, ComponentId id) const {
    const u32 column = id < kMaxComponentTypes ? layout_.column_of[id] : kNoColumn;
    return column == kNoColumn ? 0 : chunk->added[column - 1];
}

} // namespace engine_placeholder::ecs
