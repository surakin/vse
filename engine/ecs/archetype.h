#pragma once

//! An archetype: every entity that has exactly one set of components, stored as rows in structure-of-arrays chunks.

#include "containers/array.h"
#include "containers/span.h"
#include "containers/vector.h"
#include "ecs/chunk.h"
#include "ecs/component.h"
#include "ecs/component_set.h"
#include "ecs/entity.h"
#include "log/assert.h"
#include "types.h"

namespace engine_placeholder::ecs {

/// The value of a column index that does not exist.
inline constexpr u32 kNoColumn = 0xFFFFFFFFu;

/// Where the columns of an archetype sit inside a chunk.
///
/// Column 0 is the entity column; the others are the non-tag components in ascending id order. In memory the columns are ordered by
/// descending alignment, so none needs padding, and the capacity is the chunk's room divided by the bytes of one row.
///
/// Thread-safety: thread-compatible plain data.
/// Ownership: `info` points into the component registry, which outlives it.
struct ArchetypeLayout {
    /// The columns, entity column included.
    u32 column_count = 0;
    /// The most rows a chunk holds; 0 when the archetype is refused.
    u32 capacity = 0;
    /// The bytes of one row (the sum of the column element sizes).
    u32 row_bytes = 0;
    /// The component of each column (`kInvalidComponent` for the entity column).
    Array<ComponentId, kMaxArchetypeComponents + 1> component;
    /// The byte offset of each column from the chunk's `Data()`.
    Array<u32, kMaxArchetypeComponents + 1> offset;
    /// The element size of each column.
    Array<u32, kMaxArchetypeComponents + 1> size;
    /// The registry entry of each column's component (null for the entity column).
    Array<const TypeInfo*, kMaxArchetypeComponents + 1> info;
    /// The column of each component id, or [[ecs::kNoColumn]] (tags and absent components).
    Array<u32, kMaxComponentTypes> column_of;
};

/// Where a row is.
///
/// Thread-safety: thread-compatible plain data.
/// Ownership: owns nothing; valid until the next structural change of the archetype.
struct RowRef {
    /// The chunk's index in the archetype.
    u32 chunk_index = 0;
    /// The row in the chunk.
    u32 row = 0;
    /// The chunk.
    Chunk* chunk = nullptr;
};

/// What the components of a row are when [[ecs::Archetype::RemoveRow]] is called.
enum class RowContents : u8 {
    /// The components are alive; the call destroys them.
    Alive,
    /// The caller already moved the components out and destroyed them; the call only closes the gap.
    Vacated,
};

/// All entities with one set of components, in 32 KiB chunks of columns.
///
/// Rows are added at the end of the last chunk and removed by moving the archetype's last row into the gap, so rows stay dense and only
/// the last chunk is partly filled. A new chunk is taken from the pool when the last is full; an emptied last chunk goes back at once.
/// The archetype never allocates per entity. Row positions change on a removal: the caller keeps its entity table in step (it is told
/// which entity moved).
///
/// Thread-safety: thread-compatible. Reading columns from several threads is safe while no thread adds or removes rows.
/// Ownership: owns its chunks (returned to the pool by the destructor, which destroys the live components). The pool and the registry
///   must outlive it.
class Archetype {
public:
    /// Creates an archetype for `components`, computing its layout. Check [[ecs::Archetype::Valid]].
    ///
    /// Param index: this archetype's index in its world (stored in its chunks).
    /// Param components: the set; at most [[ecs::kMaxArchetypeComponents]] ids, each registered.
    /// Aborts: `components` holds more than the maximum, or an id that is not registered.
    Archetype(u32 index, const ComponentSet& components, const ComponentRegistry& registry, ChunkPool& pool);
    Archetype(const Archetype&) = delete;
    Archetype& operator=(const Archetype&) = delete;
    /// Destroys the components still alive and returns every chunk to the pool.
    ///
    /// Fails: never.
    ~Archetype();

    /// Returns whether the layout fits: false (and counted in the pool) when one row is larger than a chunk, in which case no row can be added.
    /// Fails: never.
    bool Valid() const { return layout_.capacity != 0; }
    /// The index given at creation.
    u32 Index() const { return index_; }
    /// The set of components.
    const ComponentSet& Components() const { return components_; }
    /// The chunk layout.
    const ArchetypeLayout& Layout() const { return layout_; }
    /// Returns whether the archetype has `id` (a tag included).
    /// Fails: never.
    bool Has(ComponentId id) const { return components_.Has(id); }

    /// Adds a row at the end and returns where it is.
    ///
    /// The component cells are uninitialized: the caller constructs every one (`CellPtr`) before the next structural change; the entity column is set.
    ///
    /// Returns: false when the archetype is not valid or the pool could not supply a chunk (the pool counts it); `row` is then untouched.
    /// Fails: out of memory.
    /// Ownership: the row's cells belong to the archetype; `row->chunk` is valid until the next add or remove.
    bool AddRow(Entity entity, RowRef* row);

    /// Removes a row. Its components are destroyed (for `Alive`), then the last row of the last chunk moves into the gap.
    ///
    /// Returns: the entity of the row that moved into the gap (the caller updates its location to `chunk_index`/`row`), or the invalid
    ///   entity when nothing moved (the removed row was the last).
    /// Aborts: the row does not exist.
    Entity RemoveRow(u32 chunk_index, u32 row, RowContents contents = RowContents::Alive);

    /// The number of chunks.
    u32 ChunkCount() const { return static_cast<u32>(chunks_.Size()); }
    /// The number of rows.
    u32 EntityCount() const { return entity_count_; }
    /// The bytes of the chunks held.
    usize BytesUsed() const { return chunks_.Size() * kChunkBytes; }
    /// The chunk at `index`.
    /// Aborts: `index` is not below `ChunkCount()`.
    /// Ownership: the chunk belongs to the archetype and is valid until the next add or remove.
    Chunk* ChunkAt(u32 chunk_index) const;

    /// The entities of a chunk, in row order.
    /// Ownership: the span views the chunk; valid until the next add or remove.
    Span<Entity> Entities(Chunk* chunk) const;

    /// The start of the column of `id` in `chunk`, or null for a tag or a component the archetype lacks.
    /// Fails: returns null when there is no column.
    /// Ownership: the pointer views the chunk; valid until the next add or remove.
    void* ColumnPtr(Chunk* chunk, ComponentId id) const;

    /// The address of one cell: the column of `id` at `row`; null for a tag or absent component.
    /// Fails: returns null when there is no column.
    /// Ownership: the pointer views the chunk; valid until the next add or remove.
    void* CellPtr(Chunk* chunk, ComponentId id, u32 row) const;

    /// The column of component type `T` in `chunk` as a span of the chunk's rows.
    /// Aborts: the archetype has no column for `T` (it lacks it, or it is a tag).
    /// Ownership: the span views the chunk; valid until the next add or remove.
    template <class T>
    Span<T> Column(Chunk* chunk) const {
        T* data = static_cast<T*>(ColumnPtr(chunk, ComponentOf<T>()));
        ENGINE_ASSERT(data != nullptr, "Archetype::Column: the archetype has no column for this type");
        return Span<T>(data, chunk->count);
    }

private:
    void DestroyCells(Chunk* chunk, u32 row);
    void MoveRow(Chunk* from, u32 from_row, Chunk* to, u32 to_row);

    u32 index_;
    ComponentSet components_;
    ArchetypeLayout layout_;
    ChunkPool* pool_;
    Vector<Chunk*> chunks_;
    u32 entity_count_ = 0;
};

} // namespace engine_placeholder::ecs
