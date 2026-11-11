// Entity keys, the key index and references (Phase 19 Day 258).

#include <catch2/catch_test_macros.hpp>

#include "ecs/transform.h"
#include "storage/builtin_components.h"
#include "storage/identity.h"

namespace engine_placeholder::storage {
namespace {

using ecs::Entity;
using ecs::World;

CodecLimits IdentityLimits() { return CodecLimits(); }

Entity MakeKeyed(World& world, EntityKey key) {
    return world.Create(EntityKeyComponent{key}, ecs::LocalTransform{});
}

TEST_CASE("Identity: keys are unique over a million draws, and never null", "[storage][identity]") {
    HashMap<EntityKey, u8, HeapAllocator> seen(HeapAllocator::Default());
    KeyGenerator random;
    for (u32 i = 0; i < 1000000; ++i) {
        const EntityKey k = random.Next();
        REQUIRE_FALSE(k.IsNull());
        REQUIRE(seen.Find(k) == nullptr);
        seen.Insert(k, 1);
    }
}

TEST_CASE("Identity: a seeded generator repeats its sequence; the text form round trips", "[storage][identity]") {
    KeyGenerator a = KeyGenerator::Seeded(42);
    KeyGenerator b = KeyGenerator::Seeded(42);
    KeyGenerator c = KeyGenerator::Seeded(43);
    const EntityKey first = a.Next();
    CHECK(first == b.Next());
    CHECK(first != c.Next());
    CHECK(a.Next() == b.Next());
    const String<HeapAllocator> text = first.ToText();
    REQUIRE(text.Length() == 32);
    EntityKey back;
    REQUIRE(EntityKey::FromText(text.View(), &back));
    CHECK(back == first);
    CHECK_FALSE(EntityKey::FromText("zz", &back));
    CHECK_FALSE(EntityKey::FromText("0123456789abcdef0123456789abcdeg", &back));
    CHECK(back == first); // unchanged on failure
    EntityKey ordered_low{1, 5}, ordered_high{2, 0};
    CHECK(ordered_low < ordered_high);
    CHECK_FALSE(ordered_high < ordered_low);
}

TEST_CASE("Identity: the index follows create, destroy and key change like a brute-force scan", "[storage][identity]") {
    World world;
    KeyIndex index;
    KeyGenerator gen = KeyGenerator::Seeded(7);
    Vector<Entity> live;
    u64 rng = 12345;
    auto next = [&rng]() {
        rng = rng * 6364136223846793005ull + 1442695040888963407ull;
        return static_cast<u32>(rng >> 33);
    };
    for (u32 frame = 0; frame < 1000; ++frame) {
        for (u32 op = 0; op < 8; ++op) {
            switch (next() % 4) {
            case 0: live.Push(MakeKeyed(world, gen.Next())); break;
            case 1:
                if (!live.Empty()) {
                    const usize i = next() % live.Size();
                    REQUIRE(world.Destroy(live[i]) == ecs::Status::Ok);
                    live.Remove(i);
                }
                break;
            case 2:
                if (!live.Empty()) {
                    REQUIRE(world.Set(live[next() % live.Size()], EntityKeyComponent{gen.Next()}) == ecs::Status::Ok);
                }
                break;
            default:
                if (!live.Empty()) {
                    REQUIRE(world.Remove<EntityKeyComponent>(live[next() % live.Size()]) != ecs::Status::DeadEntity);
                }
                break;
            }
        }
        REQUIRE(index.Sync(world).Ok());
        world.EndFrame();
        usize with_key = 0;
        world.Query<ecs::Read<EntityKeyComponent>>().ForEach([&](Entity e, const EntityKeyComponent& c) {
            ++with_key;
            REQUIRE(index.Find(c.key) == e);
            REQUIRE(index.KeyOf(e) == c.key);
        });
        REQUIRE(index.Size() == with_key);
    }
}

TEST_CASE("Identity: two entities with one key are never both indexed", "[storage][identity]") {
    World world;
    KeyIndex index;
    KeyGenerator gen = KeyGenerator::Seeded(1);
    const EntityKey shared = gen.Next();
    const Entity a = MakeKeyed(world, shared);
    const Entity b = MakeKeyed(world, shared);
    const Status s = index.Sync(world);
    CHECK(s.Is(ErrorCode::Duplicate));
    CHECK(index.Size() == 1);
    CHECK(index.DuplicateCount() == 1);
    const Entity holder = index.Find(shared);
    CHECK((holder == a || holder == b));
    const Entity other = holder == a ? b : a;
    CHECK(index.KeyOf(other).IsNull());
    CHECK(index.Sync(world).Is(ErrorCode::Duplicate)); // still reported while it lasts

    SECTION("destroying the holder lets the other in") {
        REQUIRE(world.Destroy(holder) == ecs::Status::Ok);
        CHECK(index.Sync(world).Ok());
        CHECK(index.Find(shared) == other);
        CHECK(index.DuplicateCount() == 0);
    }
    SECTION("changing the other's key clears the report") {
        REQUIRE(world.Set(other, EntityKeyComponent{gen.Next()}) == ecs::Status::Ok);
        CHECK(index.Sync(world).Ok());
        CHECK(index.Size() == 2);
    }
    SECTION("Assign refuses a key another entity has") {
        const Entity c = world.Create(ecs::LocalTransform{});
        CHECK(index.Assign(world, c, shared).Is(ErrorCode::Duplicate));
        CHECK_FALSE(world.Has<EntityKeyComponent>(c));
        CHECK(index.Assign(world, c, gen.Next()).Is(ErrorCode::Duplicate)); // it succeeds, but Sync still reports the pair
        CHECK(world.Has<EntityKeyComponent>(c));
        CHECK(index.Assign(world, c, EntityKey{}).Is(ErrorCode::Invalid));
    }
}

TEST_CASE("Identity: the key component and the parent link store through the registry", "[storage][identity]") {
    ComponentRegistry registry;
    REQUIRE(RegisterBuiltinComponents(&registry).Ok());
    KeyGenerator gen = KeyGenerator::Seeded(3);
    const CodecLimits limits = IdentityLimits();
    {
        const EntityKeyComponent in{gen.Next()};
        bytes::ByteWriter w;
        REQUIRE(registry.FindById(kEntityKeyId)->Encode(&in, w, limits).Ok());
        CHECK(w.Size() == 16);
        bytes::ByteReader r(w.Bytes());
        EntityKeyComponent out;
        REQUIRE(registry.FindById(kEntityKeyId)->Decode(r, &out, limits).Ok());
        CHECK(out.key == in.key);
    }
    {
        const ParentLink in{EntityRef{gen.Next()}};
        bytes::ByteWriter w;
        REQUIRE(registry.FindById(kParentLinkId)->Encode(&in, w, limits).Ok());
        bytes::ByteReader r(w.Bytes());
        ParentLink out;
        REQUIRE(registry.FindById(kParentLinkId)->Decode(r, &out, limits).Ok());
        CHECK(out.parent.key == in.parent.key);
    }
}

// A hierarchy of five levels: 1 root, 9 children, ... filled to 10,000 entities, each parent chosen from the level above.
struct Tree {
    Vector<Entity> entities;
    Vector<u32> parent_of; // index into entities, or ~0u for the root
};

Tree BuildTree(World& world, KeyIndex& index, KeyGenerator& gen) {
    Tree tree;
    const u32 counts[5] = {1, 9, 90, 900, 9000};
    u32 level_start = 0;
    u32 previous_start = 0;
    u64 rng = 99;
    for (u32 level = 0; level < 5; ++level) {
        for (u32 i = 0; i < counts[level]; ++i) {
            const Entity e = MakeKeyed(world, gen.Next());
            tree.entities.Push(e);
            u32 parent = ~0u;
            if (level > 0) {
                rng = rng * 6364136223846793005ull + 1442695040888963407ull;
                parent = previous_start + static_cast<u32>((rng >> 33) % counts[level - 1]);
                // Not ecs::SetParent, which scans every link per call: 10,000 calls would dominate the test.
                REQUIRE(world.Set(e, ecs::Parent{tree.entities[parent]}) == ecs::Status::Ok);
                REQUIRE(world.Set(e, ecs::HierarchyDepth{level}) == ecs::Status::Ok);
            }
            tree.parent_of.Push(parent);
        }
        previous_start = level_start;
        level_start += counts[level];
    }
    REQUIRE(index.Sync(world).Ok());
    return tree;
}

TEST_CASE("Identity: parents round trip through keys on 5 levels and 10,000 entities", "[storage][identity]") {
    World source;
    KeyIndex source_index;
    KeyGenerator gen = KeyGenerator::Seeded(11);
    const Tree tree = BuildTree(source, source_index, gen);
    REQUIRE(tree.entities.Size() == 10000);

    ParentReport saved;
    REQUIRE(CaptureParents(source, source_index, &saved).Ok());
    CHECK(saved.captured == 9999);
    CHECK(saved.unkeyed == 0);

    // The "loaded" world has the same keys on other entities (created in the opposite order), and the stored links.
    World target;
    KeyIndex target_index;
    Vector<Entity> loaded;
    loaded.Resize(tree.entities.Size());
    for (usize i = tree.entities.Size(); i-- > 0;) {
        loaded[i] = MakeKeyed(target, source_index.KeyOf(tree.entities[i]));
    }
    for (usize i = 0; i < tree.entities.Size(); ++i) {
        const ParentLink* link = source.Get<ParentLink>(tree.entities[i]);
        if (link != nullptr) {
            REQUIRE(target.Set(loaded[i], *link) == ecs::Status::Ok);
        }
    }
    REQUIRE(target_index.Sync(target).Ok());
    ParentReport applied;
    REQUIRE(ApplyParents(target, target_index, MissingPolicy::Fail, &applied).Ok());
    CHECK(applied.applied == 9999);
    CHECK(applied.rejected == 0);
    for (usize i = 0; i < tree.entities.Size(); ++i) {
        const ecs::Parent* p = target.Get<ecs::Parent>(loaded[i]);
        if (tree.parent_of[i] == ~0u) {
            CHECK(p == nullptr);
        } else {
            REQUIRE(p != nullptr);
            CHECK(p->entity == loaded[tree.parent_of[i]]);
        }
        CHECK_FALSE(target.Has<ParentLink>(loaded[i]));
        const ecs::HierarchyDepth* d = target.Get<ecs::HierarchyDepth>(loaded[i]);
        const ecs::HierarchyDepth* want = source.Get<ecs::HierarchyDepth>(tree.entities[i]);
        CHECK((d == nullptr) == (want == nullptr));
        if (d != nullptr && want != nullptr) {
            CHECK(d->depth == want->depth);
        }
    }
}

TEST_CASE("Identity: a cycle of links is rejected and reported", "[storage][identity]") {
    World world;
    KeyIndex index;
    KeyGenerator gen = KeyGenerator::Seeded(5);
    const EntityKey ka = gen.Next(), kb = gen.Next(), kc = gen.Next();
    const Entity a = MakeKeyed(world, ka);
    const Entity b = MakeKeyed(world, kb);
    const Entity c = MakeKeyed(world, kc);
    REQUIRE(world.Set(a, ParentLink{EntityRef{kb}}) == ecs::Status::Ok);
    REQUIRE(world.Set(b, ParentLink{EntityRef{ka}}) == ecs::Status::Ok);
    REQUIRE(world.Set(c, ParentLink{EntityRef{ka}}) == ecs::Status::Ok); // a child of the loop: also never reached
    REQUIRE(index.Sync(world).Ok());
    ParentReport report;
    REQUIRE(ApplyParents(world, index, MissingPolicy::Fail, &report).Ok());
    CHECK(report.applied == 0);
    CHECK(report.rejected == 3);
    CHECK_FALSE(world.Has<ecs::Parent>(a));
    CHECK_FALSE(world.Has<ecs::Parent>(b));
    CHECK_FALSE(world.Has<ecs::Parent>(c));
    CHECK_FALSE(world.Has<ParentLink>(a));
}

TEST_CASE("Identity: a missing target is an error, or a null reference by option", "[storage][identity]") {
    World world;
    KeyIndex index;
    KeyGenerator gen = KeyGenerator::Seeded(9);
    const Entity real_parent = MakeKeyed(world, gen.Next());
    const EntityKey real_key = index.Sync(world).Ok() ? index.KeyOf(real_parent) : EntityKey{};
    Vector<Entity> orphans;
    for (u32 i = 0; i < 20; ++i) { // 20 children whose parent is nowhere
        const Entity e = MakeKeyed(world, gen.Next());
        REQUIRE(world.Set(e, ParentLink{EntityRef{gen.Next()}}) == ecs::Status::Ok);
        orphans.Push(e);
    }
    const Entity fine = MakeKeyed(world, gen.Next());
    REQUIRE(world.Set(fine, ParentLink{EntityRef{real_key}}) == ecs::Status::Ok);
    REQUIRE(index.Sync(world).Ok());

    SECTION("Fail changes nothing and names the first missing keys, up to a cap") {
        ParentReport report;
        const Status s = ApplyParents(world, index, MissingPolicy::Fail, &report);
        CHECK(s.Is(ErrorCode::MissingReference));
        CHECK(report.references.missing == 20);
        CHECK(report.references.first_missing.Size() == ReferenceReport::kMaxReported);
        CHECK(report.applied == 0);
        CHECK_FALSE(world.Has<ecs::Parent>(fine));
        CHECK(world.Has<ParentLink>(fine));
    }
    SECTION("NullReference makes the orphans roots and counts them") {
        ParentReport report;
        REQUIRE(ApplyParents(world, index, MissingPolicy::NullReference, &report).Ok());
        CHECK(report.references.nulled == 20);
        CHECK(report.applied == 1);
        CHECK(world.Get<ecs::Parent>(fine)->entity == real_parent);
        CHECK_FALSE(world.Has<ecs::Parent>(orphans[0]));
        CHECK_FALSE(world.Has<ParentLink>(orphans[0]));
    }
    SECTION("ResolveReference: the null key is no error") {
        Entity out = real_parent;
        CHECK(ResolveReference(index, EntityKey{}, MissingPolicy::Fail, nullptr, &out).Ok());
        CHECK_FALSE(out.IsValid());
        CHECK(ResolveReference(index, gen.Next(), MissingPolicy::Fail, nullptr, &out).Is(ErrorCode::MissingReference));
    }
}

TEST_CASE("Identity: a hostile chain of 2,000 links is ordered in linear time and limited by the depth rule", "[storage][identity]") {
    World world;
    KeyIndex index;
    KeyGenerator gen = KeyGenerator::Seeded(77);
    Vector<EntityKey> keys;
    Vector<Entity> chain;
    for (u32 i = 0; i < 2000; ++i) {
        keys.Push(gen.Next());
        chain.Push(MakeKeyed(world, keys[i]));
    }
    for (u32 i = 1; i < 2000; ++i) { // listed child-first, the worst order for a naive repeated pass
        REQUIRE(world.Set(chain[2000 - i], ParentLink{EntityRef{keys[2000 - i - 1]}}) == ecs::Status::Ok);
    }
    REQUIRE(index.Sync(world).Ok());
    ParentReport report;
    REQUIRE(ApplyParents(world, index, MissingPolicy::Fail, &report).Ok());
    CHECK(report.rejected > 0); // a link that would be deeper than the limit is refused; the child stays a root
    CHECK(report.applied + report.rejected == 1999);
}

TEST_CASE("Identity: CaptureParents counts a parent without a key and drops stale links", "[storage][identity]") {
    World world;
    KeyIndex index;
    KeyGenerator gen = KeyGenerator::Seeded(21);
    const Entity keyed_parent = MakeKeyed(world, gen.Next());
    const Entity plain_parent = world.Create(ecs::LocalTransform{});
    const Entity a = world.Create(ecs::LocalTransform{});
    const Entity b = world.Create(ecs::LocalTransform{});
    const Entity c = world.Create(ecs::LocalTransform{});
    REQUIRE(ecs::SetParent(world, a, keyed_parent) == ecs::HierarchyStatus::Ok);
    REQUIRE(ecs::SetParent(world, b, plain_parent) == ecs::HierarchyStatus::Ok);
    REQUIRE(world.Set(c, ParentLink{EntityRef{gen.Next()}}) == ecs::Status::Ok); // a link with no parent behind it
    REQUIRE(index.Sync(world).Ok());
    ParentReport report;
    REQUIRE(CaptureParents(world, index, &report).Ok());
    CHECK(report.captured == 1);
    CHECK(report.unkeyed == 1);
    CHECK(world.Has<ParentLink>(a));
    CHECK_FALSE(world.Has<ParentLink>(b));
    CHECK_FALSE(world.Has<ParentLink>(c));
    CHECK(world.Get<ParentLink>(a)->parent.key == index.KeyOf(keyed_parent));
}

} // namespace
} // namespace engine_placeholder::storage
