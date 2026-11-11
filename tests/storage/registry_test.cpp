// The component registry (Phase 19 Day 256): registration, lookup, the manifest check, the builtin components and the
// unregistered-component list.

#include <catch2/catch_test_macros.hpp>

#include "ecs/transform.h"
#include "ecs/world.h"
#include "storage/builtin_components.h"
#include "storage/component_registry.h"

namespace engine_placeholder::storage
{
namespace
{

struct Health
{
    i32 current = 0;
    i32 max = 100;
};
struct Marker
{
};
struct Scratch
{
    u32 value = 0;
};
struct Forgotten
{
    u32 value = 0;
};
struct Named
{
    u32 a = 0;
};
enum class Kind : u8
{
    X,
    Y
};
struct WithEnum
{
    Kind kind = Kind::X;
};

Status RegisterHealth(ComponentRegistry* r, u32 id = 1100, const char* name = "health")
{
    return r->Register<Health>(
        {.id = id, .name = name, .fields = {Field<&Health::current>("current"), Field<&Health::max>("max")}});
}

TEST_CASE("Registry: a component registers once and is found by id, name and type", "[storage][registry]")
{
    ComponentRegistry r;
    REQUIRE(RegisterHealth(&r).Ok());
    REQUIRE(r.Register<Marker>({.id = 1101, .name = "marker"}).Ok());
    CHECK(r.Count() == 2);
    const RegisteredComponent* h = r.FindById(1100);
    REQUIRE(h != nullptr);
    CHECK(r.FindByName(StringView("health")) == h);
    CHECK(r.Find<Health>() == h);
    CHECK(r.At(0).Id() == 1100);
    CHECK(h->Fields().Size() == 2);
    CHECK(h->IsStored());
    CHECK_FALSE(h->IsTag());
    CHECK(r.Find<Marker>()->IsTag());
    CHECK(r.FindById(9) == nullptr);
    CHECK(r.FindByName(StringView("nope")) == nullptr);
    CHECK(r.Find<Scratch>() == nullptr);
    CHECK(r.FindByEcsId(ecs::kInvalidComponent) == nullptr);
}

TEST_CASE("Registry: every registration error is a Status", "[storage][registry]")
{
    ComponentRegistry r;
    REQUIRE(RegisterHealth(&r).Ok());
    CHECK(RegisterHealth(&r).Is(ErrorCode::Exists));  // the same id, name and type
    CHECK(r.Register<Named>({.id = 1100, .name = "other", .fields = {Field<&Named::a>("a")}})
              .Is(ErrorCode::Exists));  // duplicate id
    CHECK(r.Register<Named>({.id = 1200, .name = "health", .fields = {Field<&Named::a>("a")}})
              .Is(ErrorCode::Exists));  // duplicate name
    CHECK(r.Register<Health>({.id = 1201, .name = "health2", .fields = {Field<&Health::max>("max")}})
              .Is(ErrorCode::Exists));  // the type again
    CHECK(r.Register<Named>({.id = 0, .name = "zero", .fields = {Field<&Named::a>("a")}}).Is(ErrorCode::Invalid));
    CHECK(r.Register<Named>({.id = 1202, .name = "", .fields = {Field<&Named::a>("a")}}).Is(ErrorCode::Invalid));
    CHECK(r.Register<Named>({.id = 1202, .name = "named", .version = 0, .fields = {Field<&Named::a>("a")}})
              .Is(ErrorCode::Invalid));
    CHECK(r.Register<Named>({.id = 1202, .name = "named"}).Is(ErrorCode::Invalid));  // data without fields, not flagged
    CHECK(r.Register<Named>({.id = 1202, .name = "named", .flags = kComponentSkip, .fields = {Field<&Named::a>("a")}})
              .Is(ErrorCode::Invalid));
    CHECK(r.Register<Named>({.id = 1202, .name = "named", .fields = {Field<&Named::a>("a"), Field<&Named::a>("a")}})
              .Is(ErrorCode::Invalid));  // two fields, one name
    CHECK(r.Register<WithEnum>({.id = 1203, .name = "with_enum", .fields = {Field<&WithEnum::kind>("kind")}})
              .Is(ErrorCode::Invalid));  // enum without range
    CHECK(r.Register<Marker>({.id = 1204, .name = "marker2", .fields = {Field<&Named::a>("a")}})
              .Is(ErrorCode::Invalid));  // a tag has no fields
    CHECK(r.Count() == 1);
}

TEST_CASE("Registry: a derived or skipped component is registered without fields and refuses to encode",
          "[storage][registry]")
{
    ComponentRegistry r;
    REQUIRE(r.Register<Scratch>({.id = 1300, .name = "scratch", .flags = kComponentSkip}).Ok());
    REQUIRE(r.Register<Forgotten>({.id = 1301, .name = "derived", .flags = kComponentDerived}).Ok());
    const RegisteredComponent* s = r.FindById(1300);
    CHECK_FALSE(s->IsStored());
    bytes::ByteWriter w;
    Scratch value;
    CHECK(s->Encode(&value, w, CodecLimits()).Is(ErrorCode::Invalid));
    bytes::ByteReader in(w.Bytes());
    CHECK(s->Decode(in, &value, CodecLimits()).Is(ErrorCode::Invalid));
}

TEST_CASE("Registry: the manifest check keeps ids and names stable", "[storage][registry]")
{
    ComponentRegistry r;
    REQUIRE(RegisterHealth(&r).Ok());
    Vector<ManifestEntry> made;
    r.Manifest(&made);
    REQUIRE(made.Size() == 1);
    CHECK(made[0].id == 1100);
    CHECK(made[0].name == StringView("health"));
    CHECK(r.CheckManifest(Span<const ManifestEntry>(made.Data(), made.Size())).Ok());

    auto check = [&](u32 id, const char* name, u32 version)
    {
        ManifestEntry e;
        e.id = id;
        e.name = StringView(name);
        e.version = version;
        return r.CheckManifest(Span<const ManifestEntry>(&e, 1));
    };
    CHECK(check(1100, "health", 1).Ok());
    CHECK(check(1100, "health", 0).Ok());                     // recorded with an older version
    CHECK(check(1100, "hp", 1).Is(ErrorCode::Mismatch));      // the id now has another name
    CHECK(check(1999, "health", 1).Is(ErrorCode::Mismatch));  // the name now has another id
    CHECK(check(1100, "health", 2).Is(ErrorCode::TooNew));    // written by a newer version
    CHECK(check(5000, "removed", 4).Ok());                    // not registered now: the load policy decides
    CHECK(r.CheckManifest(Span<const ManifestEntry>()).Ok());
}

TEST_CASE("Registry: the builtin components register once and flag the derived ones", "[storage][registry]")
{
    ComponentRegistry r;
    REQUIRE(RegisterBuiltinComponents(&r).Ok());
    CHECK(r.Count() == 12);
    CHECK(r.FindById(kEntityKeyId)->IsStored());
    CHECK(r.FindById(kParentLinkId)->IsStored());
    CHECK(r.FindById(kLocalTransformId)->IsStored());
    CHECK(r.FindById(kLocalTransformId)->Fields().Size() == 3);
    CHECK_FALSE(r.FindById(kWorldTransformId)->IsStored());
    CHECK(r.FindById(kWorldTransformId)->Flags() == (kComponentDerived | kComponentPresence));
    CHECK(r.FindById(kSpatialBoundsId)->IsStored());
    CHECK(r.FindById(kSpatialStaticId)->IsTag());
    CHECK(r.FindById(kSpatialProxyId)->Flags() == kComponentDerived);
    CHECK(RegisterBuiltinComponents(&r).Is(ErrorCode::Exists));
}

TEST_CASE("Registry: a component the world holds and nobody registered is listed", "[storage][registry]")
{
    ComponentRegistry r;
    REQUIRE(RegisterBuiltinComponents(&r).Ok());
    REQUIRE(RegisterHealth(&r).Ok());
    ecs::World world;
    Vector<ecs::ComponentId> none;
    r.UnregisteredWithData(world, &none);
    CHECK(none.Size() == 0);

    world.Create(Health{5, 10});
    world.Create(ecs::LocalTransform{});
    r.UnregisteredWithData(world, &none);
    CHECK(none.Size() == 0);

    world.Create(Health{1, 2}, Forgotten{3});
    world.Create(Forgotten{4}, Marker{});
    Vector<ecs::ComponentId> found;
    r.UnregisteredWithData(world, &found);
    REQUIRE(found.Size() == 2);
    bool has_forgotten = false;
    bool has_marker = false;
    for (usize i = 0; i < found.Size(); ++i)
    {
        has_forgotten = has_forgotten || found[i] == ecs::ComponentOf<Forgotten>();
        has_marker = has_marker || found[i] == ecs::ComponentOf<Marker>();
    }
    CHECK(has_forgotten);
    CHECK(has_marker);

    // Flagging them decides them: the list is empty again.
    REQUIRE(r.Register<Forgotten>({.id = 1400, .name = "forgotten", .flags = kComponentSkip}).Ok());
    REQUIRE(r.Register<Marker>({.id = 1401, .name = "marker"}).Ok());
    Vector<ecs::ComponentId> after;
    r.UnregisteredWithData(world, &after);
    CHECK(after.Size() == 0);
}

}  // namespace
}  // namespace engine_placeholder::storage
