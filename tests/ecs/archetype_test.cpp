#include <catch2/catch_test_macros.hpp>

#include "containers/vector.h"
#include "ecs/archetype.h"
#include "ecs/chunk.h"
#include "ecs/component.h"
#include "ecs/component_set.h"
#include "ecs/entity_table.h"
#include "lang/new.h"
#include "test_rng.h"

using namespace vse;
using namespace vse::ecs;

namespace
{

struct Byte1
{
    u8 v;
};
struct Word4
{
    u32 v;
};
struct alignas(16) Vec16
{
    float v[4];
};
struct alignas(64) Line64
{
    u8 v[64];
};
struct Tag1
{
};
struct Payload
{
    u64 a;
    u32 b;
};
struct Big
{
    byte v[20000];
};
struct Huge
{
    byte v[40000];
};

struct ArchCounted
{
    static inline i64 live = 0;
    static inline i64 moves = 0;
    static inline i64 destroys = 0;
    u32 v = 0;
    ArchCounted() { ++live; }
    explicit ArchCounted(u32 value) : v(value) { ++live; }
    ArchCounted(ArchCounted&& other) noexcept : v(other.v)
    {
        ++live;
        ++moves;
    }
    ArchCounted(const ArchCounted&) = delete;
    ~ArchCounted()
    {
        --live;
        ++destroys;
    }
};

template <class... Ts> ComponentSet SetOf()
{
    ComponentSet set;
    (set.Add(ComponentOf<Ts>()), ...);
    return set;
}

template <class T> T* Cell(const Archetype& a, const RowRef& r)
{
    return static_cast<T*>(a.CellPtr(r.chunk, ComponentOf<T>(), r.row));
}

}  // namespace

TEST_CASE("Archetype layout: mixed alignments are aligned, disjoint and maximal", "[ecs][archetype]")
{
    ChunkPool pool;
    Archetype a(0, SetOf<Byte1, Word4, Vec16, Line64>(), ComponentRegistry::Global(), pool);
    REQUIRE(a.Valid());
    const ArchetypeLayout& l = a.Layout();
    CHECK(l.column_count == 5);
    // 8 (entity) + 1 + 4 + 16 + 64 per row
    CHECK(l.row_bytes == 93);
    CHECK(l.capacity == (kChunkBytes - sizeof(Chunk)) / 93);
    CHECK(l.capacity * (l.row_bytes + 1) > kChunkBytes - sizeof(Chunk));
    RowRef row;
    REQUIRE(a.AddRow(Entity{1, 1}, &row));
    for (u32 c = 0; c < l.column_count; ++c)
    {
        const usize align = c == 0 ? alignof(Entity) : l.info[c]->align;
        CHECK(reinterpret_cast<usize>(row.chunk->Data() + l.offset[c]) % align == 0);
        for (u32 d = 0; d < l.column_count; ++d)
        {
            if (c != d)
            {
                const bool disjoint = l.offset[c] + l.size[c] * l.capacity <= l.offset[d] ||
                                      l.offset[d] + l.size[d] * l.capacity <= l.offset[c];
                CHECK(disjoint);
            }
        }
        CHECK(l.offset[c] + l.size[c] * l.capacity <= kChunkBytes - sizeof(Chunk));
    }
    CHECK(reinterpret_cast<usize>(row.chunk) % 64 == 0);
}

TEST_CASE("Archetype: tags occupy no bytes and have no column", "[ecs][archetype]")
{
    ChunkPool pool;
    Archetype with_tag(0, SetOf<Word4, Tag1>(), ComponentRegistry::Global(), pool);
    Archetype without(1, SetOf<Word4>(), ComponentRegistry::Global(), pool);
    CHECK(with_tag.Layout().row_bytes == without.Layout().row_bytes);
    CHECK(with_tag.Layout().capacity == without.Layout().capacity);
    CHECK(with_tag.Has(ComponentOf<Tag1>()));
    RowRef row;
    REQUIRE(with_tag.AddRow(Entity{1, 1}, &row));
    CHECK(with_tag.ColumnPtr(row.chunk, ComponentOf<Tag1>()) == nullptr);
    CHECK(with_tag.ColumnPtr(row.chunk, ComponentOf<Word4>()) != nullptr);
    CHECK(with_tag.ColumnPtr(row.chunk, ComponentOf<Byte1>()) == nullptr);
}

TEST_CASE("Archetype: 100k rows are stored and read back", "[ecs][archetype]")
{
    ChunkPool pool;
    Archetype a(0, SetOf<Payload, Word4>(), ComponentRegistry::Global(), pool);
    constexpr u32 kRows = 100000;
    for (u32 i = 0; i < kRows; ++i)
    {
        RowRef row;
        REQUIRE(a.AddRow(Entity{i, 1}, &row));
        ConstructAt<Payload>(Cell<Payload>(a, row), Payload{u64{i} * 3, i});
        ConstructAt<Word4>(Cell<Word4>(a, row), Word4{i ^ 0x5555u});
    }
    CHECK(a.EntityCount() == kRows);
    CHECK(a.ChunkCount() == (kRows + a.Layout().capacity - 1) / a.Layout().capacity);
    CHECK(a.BytesUsed() == a.ChunkCount() * kChunkBytes);
    CHECK(pool.Stats().chunks == a.ChunkCount());
    u32 expected = 0;
    for (u32 c = 0; c < a.ChunkCount(); ++c)
    {
        Chunk* chunk = a.ChunkAt(c);
        Span<Payload> p = a.Column<Payload>(chunk);
        Span<Word4> w = a.Column<Word4>(chunk);
        Span<Entity> e = a.Entities(chunk);
        for (usize r = 0; r < p.Size(); ++r, ++expected)
        {
            REQUIRE(e[r] == Entity{expected, 1});
            REQUIRE(p[r].a == u64{expected} * 3);
            REQUIRE(p[r].b == expected);
            REQUIRE(w[r].v == (expected ^ 0x5555u));
        }
    }
    CHECK(expected == kRows);
}

TEST_CASE("Archetype: removing from the middle, the end and the only row", "[ecs][archetype]")
{
    ChunkPool pool;
    Archetype a(0, SetOf<Word4>(), ComponentRegistry::Global(), pool);
    RowRef r;
    for (u32 i = 0; i < 5; ++i)
    {
        REQUIRE(a.AddRow(Entity{i, 1}, &r));
        ConstructAt<Word4>(Cell<Word4>(a, r), Word4{i * 10});
    }
    // middle: row 1 is replaced by the last (entity 4)
    Entity moved = a.RemoveRow(0, 1);
    CHECK(moved == Entity{4, 1});
    CHECK(a.Entities(a.ChunkAt(0))[1] == Entity{4, 1});
    CHECK(a.Column<Word4>(a.ChunkAt(0))[1].v == 40);
    CHECK(a.EntityCount() == 4);
    // end: nothing moves
    moved = a.RemoveRow(0, 3);
    CHECK_FALSE(moved.IsValid());
    CHECK(a.EntityCount() == 3);
    while (a.EntityCount() > 1)
    {
        a.RemoveRow(0, 0);
    }
    CHECK(a.ChunkCount() == 1);
    // the only row
    moved = a.RemoveRow(0, 0);
    CHECK_FALSE(moved.IsValid());
    CHECK(a.EntityCount() == 0);
    CHECK(a.ChunkCount() == 0);
    CHECK(pool.Stats().chunks == 0);
}

TEST_CASE("Archetype: non-trivial components are moved and destroyed exactly once", "[ecs][archetype]")
{
    ArchCounted::live = ArchCounted::moves = ArchCounted::destroys = 0;
    {
        ChunkPool pool;
        Archetype a(0, SetOf<ArchCounted, Word4>(), ComponentRegistry::Global(), pool);
        RowRef r;
        for (u32 i = 0; i < 4; ++i)
        {
            REQUIRE(a.AddRow(Entity{i, 1}, &r));
            ConstructAt<ArchCounted>(Cell<ArchCounted>(a, r), i + 100);
            ConstructAt<Word4>(Cell<Word4>(a, r), Word4{i});
        }
        CHECK(ArchCounted::live == 4);
        const Entity moved = a.RemoveRow(0, 0);  // destroys row 0's, moves row 3 into it
        CHECK(moved == Entity{3, 1});
        CHECK(ArchCounted::live == 3);
        CHECK(ArchCounted::moves == 1);
        CHECK(ArchCounted::destroys == 2);  // the removed cell and the moved-from source
        CHECK(a.Column<ArchCounted>(a.ChunkAt(0))[0].v == 103);
        // a vacated removal destroys nothing: the caller already did
        ArchCounted* cell = Cell<ArchCounted>(a, RowRef{0, 2, a.ChunkAt(0)});
        cell->~ArchCounted();
        const i64 destroys = ArchCounted::destroys;
        a.RemoveRow(0, 2, RowContents::Vacated);
        CHECK(ArchCounted::destroys == destroys);
        CHECK(ArchCounted::live == 2);
    }
    CHECK(ArchCounted::live == 0);  // the destructor destroyed the rest
}

TEST_CASE("ChunkPool: reuses released chunks, honors the budget, and Trim frees them", "[ecs][archetype]")
{
    ChunkPool pool(2);
    Chunk* a = pool.Acquire(0, 10);
    Chunk* b = pool.Acquire(0, 10);
    Chunk* c = pool.Acquire(0, 10);
    CHECK(pool.Stats().chunks == 3);
    CHECK(pool.Stats().bytes == 3 * kChunkBytes);
    pool.Release(a);
    pool.Release(b);
    pool.Release(c);  // over the budget: freed
    CHECK(pool.Stats().pooled == 2);
    CHECK(pool.Stats().chunks == 0);
    Chunk* d = pool.Acquire(7, 5);
    CHECK((d == a || d == b || d == c));
    CHECK(pool.Stats().reused == 1);
    CHECK(d->archetype == 7);
    CHECK(d->capacity == 5);
    CHECK(d->count == 0);
    pool.Release(d);
    pool.Trim();
    CHECK(pool.Stats().pooled == 0);
}

TEST_CASE("Archetype: a row larger than a chunk is refused and counted", "[ecs][archetype]")
{
    ChunkPool pool;
    Archetype ok(0, SetOf<Big>(), ComponentRegistry::Global(), pool);
    CHECK(ok.Valid());
    CHECK(ok.Layout().capacity == 1);
    Archetype huge(1, SetOf<Huge>(), ComponentRegistry::Global(), pool);
    CHECK_FALSE(huge.Valid());
    CHECK(pool.Stats().refused_layouts == 1);
    RowRef r;
    CHECK_FALSE(huge.AddRow(Entity{1, 1}, &r));
    CHECK(huge.EntityCount() == 0);
}

TEST_CASE("Archetype: randomized add and remove agree with a model", "[ecs][archetype]")
{
    ArchCounted::live = 0;
    ChunkPool pool(4);
    {
        Archetype a(0, SetOf<Payload, ArchCounted>(), ComponentRegistry::Global(), pool);
        EntityTable table;
        ecs_test::Rng rng(0xA11CE5u);
        Vector<Entity> alive;
        Vector<u64> value_of;  // by entity index
        auto check_all = [&]
        {
            REQUIRE(a.EntityCount() == alive.Size());
            REQUIRE(ArchCounted::live == static_cast<i64>(alive.Size()));
            for (usize i = 0; i < alive.Size(); ++i)
            {
                const EntityLocation* loc = table.Location(alive[i]);
                REQUIRE(loc != nullptr);
                Chunk* chunk = a.ChunkAt(loc->chunk);
                REQUIRE(a.Entities(chunk)[loc->row] == alive[i]);
                REQUIRE(a.Column<Payload>(chunk)[loc->row].a == value_of[alive[i].index]);
                REQUIRE(a.Column<ArchCounted>(chunk)[loc->row].v == static_cast<u32>(value_of[alive[i].index]));
            }
            u32 rows = 0;
            for (u32 c = 0; c < a.ChunkCount(); ++c)
            {
                rows += a.ChunkAt(c)->count;
                if (c + 1 < a.ChunkCount())
                {
                    REQUIRE(a.ChunkAt(c)->count == a.Layout().capacity);
                }
            }
            REQUIRE(rows == a.EntityCount());
        };
        for (u32 op = 1; op <= 200000; ++op)
        {
            const bool add = alive.Size() == 0 || rng.Below(100) < 55;
            if (add)
            {
                RowRef r;
                Entity e = table.Allocate(EntityLocation{0, 0, 0});
                REQUIRE(e.IsValid());
                REQUIRE(a.AddRow(e, &r));
                table.Location(e)->chunk = r.chunk_index;
                table.Location(e)->row = r.row;
                const u64 v = rng.Next();
                ConstructAt<Payload>(Cell<Payload>(a, r), Payload{v, 0});
                ConstructAt<ArchCounted>(Cell<ArchCounted>(a, r), static_cast<u32>(v));
                if (value_of.Size() <= e.index)
                {
                    value_of.Resize(e.index + 1);
                }
                value_of[e.index] = v;
                alive.Push(e);
            }
            else
            {
                const usize pick = rng.Below(static_cast<u32>(alive.Size()));
                const Entity e = alive[pick];
                const EntityLocation where = *table.Location(e);
                const Entity moved = a.RemoveRow(where.chunk, where.row);
                table.Free(e);
                if (moved.IsValid())
                {
                    table.Location(moved)->chunk = where.chunk;
                    table.Location(moved)->row = where.row;
                }
                alive[pick] = alive[alive.Size() - 1];
                alive.Pop();
            }
            if (op % 1000 == 0)
            {
                check_all();
            }
        }
        check_all();
    }
    CHECK(ArchCounted::live == 0);
    CHECK(pool.Stats().chunks == 0);
}
