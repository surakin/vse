// The record store and the reference MemoryStore (Phase 19 Day 259).

#include "store_conformance.h"
#include "storage/memory_store.h"
#include "storage/storage_options.h"
#include "storage/world_storage.h"

namespace engine_placeholder::storage {
namespace {

struct MemoryStoreFixture {
    static constexpr bool kInjectsFailure = true;
    static constexpr bool kCanCorrupt = true;
    static constexpr bool kReadersSeeCommitted = true;
    static const char* Name() { return "memory"; }
    Store& Db() { return store_; }
    void FailAfter(u32 n) { store_.FailAfter(n); }
    bool Corrupt(EntityKey key) { return store_.CorruptRecord(key); }

private:
    MemoryStore store_;
};

STORE_CONFORMANCE_CASES(MemoryStoreFixture)

TEST_CASE("Store: the hash is FNV-1a and the record clones", "[storage][store]") {
    CHECK(HashBytes(ByteSpan()) == 14695981039346656037ull);
    const byte a[] = {static_cast<byte>('a')};
    CHECK(HashBytes(ByteSpan(a, 1)) == 0xaf63dc4c8601ec8cull); // the published FNV-1a 64 of "a"
    const EntityRecord r = conformance::Rec(7, 3, 5);
    const EntityRecord c = r.Clone();
    CHECK(c.key == r.key);
    CHECK(c.hash == r.hash);
    CHECK(c.blob.Size() == 5);
    CHECK(c.blob.Data() != r.blob.Data());
}

TEST_CASE("Store: a cancel token stays set until reset; options start as a full save of everything", "[storage][store]") {
    CancelToken token;
    CHECK_FALSE(token.IsCancelled());
    token.Cancel();
    CHECK(token.IsCancelled());
    token.Reset();
    CHECK_FALSE(token.IsCancelled());
    const SaveOptions save;
    CHECK(save.mode == SaveMode::Full);
    CHECK(save.selection.kind == Selection::Kind::AllKeyed);
    const LoadOptions load;
    CHECK(load.mode == LoadMode::Create);
    CHECK(load.unknown == UnknownPolicy::Fail);
    CHECK(load.missing == MissingPolicy::Fail);
}

TEST_CASE("Store: WorldStorage lists and deletes through the store; with no registered component save and load are Invalid", "[storage][store]") {
    ComponentRegistry registry;
    MemoryStore store;
    WorldStorage storage(registry, store);
    for (u64 k = 1; k <= 5; ++k) {
        REQUIRE(store.Put(conformance::Rec(k, 1)).Ok());
    }
    const EntityKey gone[] = {EntityKey{0, 2}, EntityKey{0, 4}, EntityKey{0, 99}}; // 99 is not there: not an error
    REQUIRE(storage.Delete(Span<const EntityKey>(gone, 3)).Ok());
    Vector<EntityKey> keys;
    REQUIRE(storage.List(KeyRange(), 10, &keys).Ok());
    REQUIRE(keys.Size() == 3);
    CHECK(keys[0].low == 1);
    CHECK(keys[1].low == 3);
    CHECK(keys[2].low == 5);
    ecs::World world;
    KeyIndex index;
    Report report;
    CHECK(storage.Save(world, index, SaveOptions(), &report).Is(ErrorCode::Invalid)); // an empty registry
    CHECK(storage.Load(world, index, LoadOptions(), &report).Is(ErrorCode::Invalid));
}

TEST_CASE("Store: a failed Delete leaves the store as it was", "[storage][store]") {
    ComponentRegistry registry;
    MemoryStore store;
    WorldStorage storage(registry, store);
    for (u64 k = 1; k <= 3; ++k) {
        REQUIRE(store.Put(conformance::Rec(k, 1)).Ok());
    }
    const EntityKey gone[] = {EntityKey{0, 1}, EntityKey{0, 2}};
    store.FailAfter(3); // Begin, Remove, then the second Remove fails
    CHECK(storage.Delete(Span<const EntityKey>(gone, 2)).Is(ErrorCode::Io));
    CHECK(conformance::CountOf(store) == 3);
}

} // namespace
} // namespace engine_placeholder::storage
