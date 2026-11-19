// The level format, the loader and the level minter (Phase 22 Day 327).

#include <catch2/catch_test_macros.hpp>

#include "asset/level.h"
#include "comma_locale.h"
#include "cstr/cstr.h"
#include "cstr/number.h"
#include "os/console.h"
#include "os/os.h"
#include "cstr/format.h"
#include "ecs/transform.h"
#include "level_source.h"
#include "storage/builtin_components.h"
#include "storage/component_record.h"
#include "support.h"

namespace engine_placeholder::asset
{

namespace
{

using test::AsBytes;
using test::Contains;
using test::Rid;
using test::Vp;

constexpr u64 kLevelId = 1;

// Resources under paths, as the minter's resolver. A blob is a bare container of the type: the level only reads the
// header and the dependency list.
class LvResolver final : public LevelReferenceResolver
{
public:
    struct Entry
    {
        Array<char, 64> path = {};
        ResourceId id;
        Vector<byte> blob;
    };

    Status ResolvePath(StringView path, ResourceId* id, ByteSpan* blob) const override
    {
        for (usize i = 0; i < entries.Size(); ++i)
        {
            if (StringView(entries[i].path.Data(), CStrLength(entries[i].path.Data())) == path)
            {
                *id = entries[i].id;
                *blob = AsBytes(entries[i].blob);
                return Status::Success();
            }
        }
        return Status::Make(ErrorCode::NotFound, "no such resource");
    }

    Status ResolveId(ResourceId id, ByteSpan* blob) const override
    {
        for (usize i = 0; i < entries.Size(); ++i)
        {
            if (entries[i].id == id)
            {
                *blob = AsBytes(entries[i].blob);
                return Status::Success();
            }
        }
        return Status::Make(ErrorCode::NotFound, "no such id");
    }

    Status PathOf(ResourceId id, String<HeapAllocator>* path) const override
    {
        for (usize i = 0; i < entries.Size(); ++i)
        {
            if (entries[i].id == id)
            {
                *path = String<HeapAllocator>(HeapAllocator::Default(),
                                              StringView(entries[i].path.Data(), CStrLength(entries[i].path.Data())));
                return Status::Success();
            }
        }
        return Status::Make(ErrorCode::NotFound, "no such id");
    }

    void Add(const TypeRegistry& types, const char* path, u64 id, ResourceType type)
    {
        Entry entry;
        FormatTo(entry.path.Data(), entry.path.Size(), "%s", path);
        entry.id = Rid(id);
        BlobWriter writer(types, type, entry.id);
        REQUIRE(writer.Finish(&entry.blob).Ok());
        entries.Push(static_cast<Entry&&>(entry));
    }

    void AddBlob(const char* path, u64 id, const Vector<byte>& blob)
    {
        Entry entry;
        FormatTo(entry.path.Data(), entry.path.Size(), "%s", path);
        entry.id = Rid(id);
        for (usize i = 0; i < blob.Size(); ++i)
        {
            entry.blob.Push(blob[i]);
        }
        entries.Push(static_cast<Entry&&>(entry));
    }

    Vector<Entry> entries;
};

// The number of entities that have a key: what a level creates.
usize KeyedCount(ecs::World& world)
{
    usize count = 0;
    world.Query<ecs::Read<storage::EntityKeyComponent>>().ForEach(
        [&count](ecs::Entity, const storage::EntityKeyComponent&) { ++count; });
    return count;
}

struct Rig
{
    TypeRegistry types;
    storage::ComponentRegistry registry;
    LvResolver resolver;

    Rig()
    {
        REQUIRE(storage::RegisterBuiltinComponents(&registry).Ok());
        REQUIRE(RegisterLevelComponents(&registry).Ok());
        resolver.Add(types, "/levels/sky", 0x100, types::kLevel);
        resolver.Add(types, "/meshes/rock", 0x201, types::kMesh);
        resolver.Add(types, "/meshes/tree", 0x202, types::kMesh);
        resolver.Add(types, "/materials/stone", 0x301, types::kMaterial);
        resolver.Add(types, "/textures/grass", 0x401, types::kTexture);
    }

    Status Mint(const char* text, Vector<byte>* blob, LevelSourceError* error)
    {
        return MintLevel(Vp(text), resolver, types, registry, blob, error);
    }
    Status Mint(const String<HeapAllocator>& text, Vector<byte>* blob, LevelSourceError* error)
    {
        return MintLevel(text.View(), resolver, types, registry, blob, error);
    }
};

const char* kSmall = "id 0000000000000001\n"
                     "include /levels/sky\n"
                     "entity ground\n"
                     "  component local_transform\n"
                     "    position = 0 -1 0\n"
                     "entity rock\n"
                     "  parent ground\n"
                     "  component local_transform\n"
                     "    position = 1 0 2\n"
                     "    scale = 2 2 2\n"
                     "  component mesh_reference\n"
                     "    mesh = /meshes/rock\n"
                     "    material = /materials/stone\n"
                     "entity tree key @00000000000000000000000000000009  # an explicit key\n"
                     "  component mesh_reference\n"
                     "    mesh = /meshes/tree\n"
                     "    material = null\n"
                     "    flags = 4\n";

}  // namespace

TEST_CASE("Level names are checked and keys derive from the level and the name", "[asset][level]")
{
    CHECK(LevelNameValid(Vp("rock_01")));
    CHECK(LevelNameValid(Vp("a/b:c.d-e")));
    CHECK_FALSE(LevelNameValid(Vp("")));
    CHECK_FALSE(LevelNameValid(Vp("key")));
    CHECK_FALSE(LevelNameValid(Vp("two words")));
    CHECK_FALSE(LevelNameValid(Vp("#rock")));
    CHECK_FALSE(LevelNameValid(Vp("@rock")));
    char longest[130];
    for (u32 i = 0; i < 129; ++i)
    {
        longest[i] = 'a';
    }
    longest[129] = '\0';
    CHECK(LevelNameValid(StringView(longest, 128)));
    CHECK_FALSE(LevelNameValid(StringView(longest, 129)));

    const storage::EntityKey a = LevelEntityKey(Rid(1), Vp("rock"));
    CHECK_FALSE(a.IsNull());
    CHECK(a == LevelEntityKey(Rid(1), Vp("rock")));
    CHECK(a != LevelEntityKey(Rid(2), Vp("rock")));
    CHECK(a != LevelEntityKey(Rid(1), Vp("rocks")));
}

TEST_CASE("A level source mints to a blob that loads into a world with its references listed", "[asset][level]")
{
    Rig rig;
    Vector<byte> blob;
    LevelSourceError error;
    const Status minted = rig.Mint(kSmall, &blob, &error);
    INFO(error.message.Data() << " at line " << error.line);
    REQUIRE(minted.Ok());

    BlobReader reader;
    REQUIRE(reader.Open(AsBytes(blob), rig.types).Ok());
    CHECK(reader.Type() == types::kLevel);
    CHECK(reader.Id() == Rid(kLevelId));
    CHECK(reader.Verify().Ok());

    ecs::World world;
    storage::KeyIndex index;
    LevelData data;
    REQUIRE(LoadLevelBlob(AsBytes(blob), rig.types, rig.registry, world, index, &data).Ok());
    CHECK(data.entities.Size() == 3);

    // The dependency list: the sky is included; the two meshes and the material are referenced, the null one is not.
    REQUIRE(data.resources.Size() == 4);
    CHECK(data.resources[0].id == Rid(0x100));
    CHECK(data.resources[0].roles == kLevelRoleInclude);
    CHECK(data.resources[1].id == Rid(0x201));
    CHECK(data.resources[1].roles == kLevelRoleReference);
    CHECK(data.resources[1].type == types::kMesh);
    CHECK(data.resources[2].id == Rid(0x202));
    CHECK(data.resources[3].id == Rid(0x301));
    CHECK(data.resources[3].type == types::kMaterial);
    CHECK(data.references.Size() == 3);

    const storage::EntityKey rock_key = LevelEntityKey(Rid(kLevelId), Vp("rock"));
    const ecs::Entity rock = index.Find(rock_key);
    const ecs::Entity ground = index.Find(LevelEntityKey(Rid(kLevelId), Vp("ground")));
    REQUIRE(rock.IsValid());
    REQUIRE(ground.IsValid());
    const ecs::LocalTransform* t = world.Peek<ecs::LocalTransform>(rock);
    REQUIRE(t != nullptr);
    CHECK(t->position.x == 1.0f);
    CHECK(t->position.z == 2.0f);
    CHECK(t->scale.x == 2.0f);
    CHECK(t->rotation.w == 1.0f);  // not in the source: the default
    const MeshReference* mesh = world.Peek<MeshReference>(rock);
    REQUIRE(mesh != nullptr);
    CHECK(mesh->mesh.id == 0x201);
    CHECK(mesh->material.id == 0x301);
    const ecs::Parent* parent = world.Peek<ecs::Parent>(rock);
    REQUIRE(parent != nullptr);
    CHECK(parent->entity == ground);
    const EntityName* name = world.Peek<EntityName>(rock);
    REQUIRE(name != nullptr);
    CHECK(name->name.View() == Vp("rock"));

    storage::EntityKey explicit_key;
    explicit_key.low = 9;
    const ecs::Entity tree = index.Find(explicit_key);
    REQUIRE(tree.IsValid());
    CHECK(world.Peek<MeshReference>(tree)->flags == 4);
    CHECK(world.Peek<MeshReference>(tree)->material.id == 0);
}

TEST_CASE("Text, blob and world print back to the same canonical text", "[asset][level]")
{
    Rig rig;
    Vector<byte> blob;
    LevelSourceError error;
    REQUIRE(rig.Mint(kSmall, &blob, &error).Ok());

    String<HeapAllocator> text(HeapAllocator::Default());
    REQUIRE(FormatLevelBlob(AsBytes(blob), rig.types, rig.registry, rig.resolver, &text, &error).Ok());
    INFO(text.View().Data());
    CHECK(Contains(text.View().Data(), "include /levels/sky"));
    CHECK(Contains(text.View().Data(), "entity rock\n"));
    CHECK(Contains(text.View().Data(), "entity tree key @00000000000000000000000000000009"));
    CHECK(Contains(text.View().Data(), "parent ground"));
    CHECK(Contains(text.View().Data(), "mesh = /meshes/rock"));

    // The canonical text mints to the identical blob, and prints as itself.
    Vector<byte> again;
    REQUIRE(rig.Mint(text, &again, &error).Ok());
    REQUIRE(again.Size() == blob.Size());
    CHECK(MemCompare(again.Data(), blob.Data(), blob.Size()) == 0);
    String<HeapAllocator> text2(HeapAllocator::Default());
    REQUIRE(FormatLevelBlob(AsBytes(again), rig.types, rig.registry, rig.resolver, &text2, &error).Ok());
    CHECK(text2.View() == text.View());

    // The loaded world prints the same text.
    ecs::World world;
    storage::KeyIndex index;
    REQUIRE(LoadLevelBlob(AsBytes(blob), rig.types, rig.registry, world, index).Ok());
    const ResourceId includes[1] = {Rid(0x100)};
    String<HeapAllocator> from_world(HeapAllocator::Default());
    REQUIRE(FormatWorld(Rid(kLevelId), world, index, Span<const ResourceId>(includes, 1), rig.resolver, rig.registry,
                        &from_world, &error)
                .Ok());
    CHECK(from_world.View() == text.View());
}

// Maintenance (2026-11-18): the level text is the same under a host's decimal-comma locale.
TEST_CASE("Level text prints and mints the same under a decimal-comma locale", "[asset][level][locale]")
{
    const char* source = "id 0000000000000001\n"
                         "entity rock\n"
                         "  component local_transform\n"
                         "    position = 0.5 -1.25 2.75\n"
                         "    scale = 0.1 0.2 0.3\n";
    Rig rig;
    Vector<byte> blob;
    LevelSourceError error;
    REQUIRE(rig.Mint(source, &blob, &error).Ok());
    String<HeapAllocator> in_c(HeapAllocator::Default());
    REQUIRE(FormatLevelBlob(AsBytes(blob), rig.types, rig.registry, rig.resolver, &in_c, &error).Ok());
    INFO(in_c.View().Data());
    CHECK(Contains(in_c.View().Data(), "position = 0.5 -1.25 2.75"));

    ::engine_placeholder::test::CommaLocale comma;
    if (!comma.Active())
    {
        SKIP("no decimal-comma locale is installed (tried nb_NO, de_DE and fr_FR)");
    }
    String<HeapAllocator> in_comma(HeapAllocator::Default());
    REQUIRE(FormatLevelBlob(AsBytes(blob), rig.types, rig.registry, rig.resolver, &in_comma, &error).Ok());
    CHECK(in_comma.View() == in_c.View());
    Vector<byte> again;
    REQUIRE(rig.Mint(in_comma, &again, &error).Ok());
    REQUIRE(again.Size() == blob.Size());
    CHECK(MemCompare(again.Data(), blob.Data(), blob.Size()) == 0);
}

TEST_CASE("A level of 1,000 entities round-trips through text, blob and world", "[asset][level]")
{
    Rig rig;
    String<HeapAllocator> source(HeapAllocator::Default());
    source.Append(StringView("id 0000000000000001\n"));
    for (u32 i = 0; i < 1000; ++i)
    {
        char buffer[256];
        FormatTo(buffer, sizeof(buffer),
                 "entity e%u\n"
                 "  component local_transform\n"
                 "    position = %u 0.25 -%u\n"
                 "  component mesh_reference\n"
                 "    mesh = %s\n"
                 "    material = %s\n",
                 static_cast<unsigned>(i), static_cast<unsigned>(i), static_cast<unsigned>(i),
                 i % 2 == 0 ? "/meshes/rock" : "/meshes/tree", i % 3 == 0 ? "/materials/stone" : "null");
        source.Append(Vp(buffer));
        if (i > 0 && i % 10 != 0)
        {
            FormatTo(buffer, sizeof(buffer), "  parent e%u\n", static_cast<unsigned>(i - 1));
            source.Append(Vp(buffer));
        }
    }
    Vector<byte> blob;
    LevelSourceError error;
    const Status minted = rig.Mint(source, &blob, &error);
    INFO(error.message.Data() << " at line " << error.line);
    REQUIRE(minted.Ok());

    ecs::World world;
    storage::KeyIndex index;
    LevelData data;
    REQUIRE(LoadLevelBlob(AsBytes(blob), rig.types, rig.registry, world, index, &data).Ok());
    CHECK(data.entities.Size() == 1000);
    CHECK(data.references.Size() == 1000 + 334);

    String<HeapAllocator> text(HeapAllocator::Default());
    REQUIRE(FormatLevelBlob(AsBytes(blob), rig.types, rig.registry, rig.resolver, &text, &error).Ok());
    String<HeapAllocator> from_world(HeapAllocator::Default());
    REQUIRE(FormatWorld(Rid(kLevelId), world, index, Span<const ResourceId>(), rig.resolver, rig.registry, &from_world,
                        &error)
                .Ok());
    CHECK(from_world.View() == text.View());
    Vector<byte> again;
    REQUIRE(rig.Mint(text, &again, &error).Ok());
    REQUIRE(again.Size() == blob.Size());
    CHECK(MemCompare(again.Data(), blob.Data(), blob.Size()) == 0);
}

TEST_CASE("Minting is deterministic and independent of the order of entities in the text", "[asset][level]")
{
    Rig rig;
    const char* a = "id 0000000000000001\n"
                    "entity one\n  component local_transform\n    position = 1 2 3\n"
                    "entity two\n  component local_transform\n    position = 4 5 6\n";
    const char* b = "id 0000000000000001\n"
                    "entity two\n  component local_transform\n    position = 4 5 6\n"
                    "entity one\n  component local_transform\n    position = 1 2 3\n";
    Vector<byte> first;
    Vector<byte> second;
    Vector<byte> third;
    LevelSourceError error;
    REQUIRE(rig.Mint(a, &first, &error).Ok());
    REQUIRE(rig.Mint(a, &second, &error).Ok());
    REQUIRE(rig.Mint(b, &third, &error).Ok());
    REQUIRE(first.Size() == second.Size());
    CHECK(MemCompare(first.Data(), second.Data(), first.Size()) == 0);
    REQUIRE(first.Size() == third.Size());
    CHECK(MemCompare(first.Data(), third.Data(), first.Size()) == 0);
}

TEST_CASE("Every refusal of the source names its line", "[asset][level]")
{
    Rig rig;
    struct Case
    {
        const char* text;
        u32 line;
        const char* needle;
    };
    const Case cases[] = {
        {"entity a\n", 1, "id line must come first"},
        {"id 0000000000000000\n", 1, "16 hexadecimal digits"},
        {"id 1234\n", 1, "16 hexadecimal digits"},
        {"id 0000000000000001\nid 0000000000000001\n", 2, "comes first, once"},
        {"id 0000000000000001\nfrobnicate\n", 2, "unknown line"},
        {"id 0000000000000001\ncomponent local_transform\n", 2, "entity line comes first"},
        {"id 0000000000000001\nentity a\ncomponent nonsense\n", 3, "local_transform"},
        {"id 0000000000000001\nentity a\ncomponent entity_name\n", 3, "set by the entity line"},
        {"id 0000000000000001\nentity a\ncomponent entity_key\n", 3, "registry has"},
        {"id 0000000000000001\nentity a\ncomponent parent_link\n", 3, "parent line"},
        {"id 0000000000000001\nentity a\ncomponent world_transform\n", 3, "registry has"},
        {"id 0000000000000001\nentity a\ncomponent local_transform\ncomponent local_transform\n", 4,
         "this component already"},
        {"id 0000000000000001\nentity a\ncomponent local_transform\nposition = 1 2\n", 4, "3 numbers"},
        {"id 0000000000000001\nentity a\ncomponent local_transform\nposition = 1 2 x\n", 4, "not a finite number"},
        {"id 0000000000000001\nentity a\ncomponent local_transform\nposition = 1 2 3 4\n", 4, "more values"},
        {"id 0000000000000001\nentity a\ncomponent local_transform\nposition = 1e999 2 3\n", 4, "not a finite number"},
        {"id 0000000000000001\nentity a\ncomponent local_transform\nheight = 1\n", 4, "position, rotation, scale"},
        {"id 0000000000000001\nentity a\ncomponent local_transform\nposition = 1 2 3\nposition = 1 2 3\n", 5,
         "set twice"},
        {"id 0000000000000001\nentity a\nentity a\n", 3, "two entities have this name"},
        {"id 0000000000000001\nentity a b c\n", 2, "entity NAME"},
        {"id 0000000000000001\nentity bad\\name\n", 2, "entity name is"},
        {"id 0000000000000001\nentity key\n", 2, "entity name is"},
        {"id 0000000000000001\nentity a key @12\n", 2, "32 hexadecimal"},
        {"id 0000000000000001\nentity a\nposition = 1 2 3\n", 3, "follows a component line"},
        {"id 0000000000000001\nentity a\n  parent nobody\ncomponent local_transform\n", 0, ""},
        {"id 0000000000000001\nentity a\ncomponent mesh_reference\nmesh = /meshes/missing\n", 4,
         "not a resource of the project"},
        {"id 0000000000000001\nentity a\ncomponent mesh_reference\nmesh = /materials/stone\n", 4,
         "is a material but the field takes a mesh"},
        {"id 0000000000000001\nentity a\ncomponent mesh_reference\nmesh = null\n", 4, "may not be null"},
        {"id 0000000000000001\nentity a\ncomponent mesh_reference\nmesh = @00000000000000ff\n", 4,
         "no resource with the id"},
        {"id 0000000000000001\nentity a\ncomponent mesh_reference\nmesh = @xyz\n", 4, "16 hexadecimal"},
        {"id 0000000000000001\nentity a\ncomponent mesh_reference\nflags = 4294967296\n", 4, "does not fit 32 bits"},
        {"id 0000000000000001\nentity a\ncomponent mesh_reference\nflags = -1\n", 4, "not an unsigned integer"},
        {"id 0000000000000001\nentity a\ncomponent mesh_reference\n", 3, "mesh"},  // a null mesh from the default
        {"id 0000000000000001\nentity a\n  parent a\n", 3, "own parent"},
        {"id 0000000000000001\nentity a\n  parent @00000000000000000000000000000077\n", 0, ""},
        {"id 0000000000000001\ninclude /meshes/rock\n", 2, "not a level"},
        {"id 0000000000000001\ninclude /levels/nothing\n", 2, "not a resource"},
        {"id 0000000000000001\nentity a\ninclude /levels/sky\n", 3, "comes before the first entity"},
        {"id 0000000000000001\nentity \"a\n", 2, "not closed"},
    };
    for (const Case& c : cases)
    {
        Vector<byte> blob;
        LevelSourceError error;
        INFO(c.text);
        const Status s = rig.Mint(c.text, &blob, &error);
        if (c.needle[0] == '\0')
        {
            // Refused by the level as a whole or by a reference to an entity that is nowhere.
            CHECK(s.Failed());
            continue;
        }
        REQUIRE(s.Failed());
        INFO(error.message.Data());
        CHECK(error.line == c.line);
        CHECK(Contains(error.message.Data(), c.needle));
    }
}

TEST_CASE("A source without an id line names asset_mint adopt", "[asset][level]")
{
    Rig rig;
    Vector<byte> blob;
    LevelSourceError error;
    REQUIRE(rig.Mint("# nothing here\n", &blob, &error).Failed());
    CHECK(error.line == 0);
    CHECK(Contains(error.message.Data(), "asset_mint adopt"));
}

TEST_CASE("Includes may not form a cycle", "[asset][level]")
{
    Rig rig;
    Vector<byte> blob_b;
    LevelSourceError error;
    // B (id 0x20) includes nothing; A (id 0x10) includes B; then B is re-minted to include A.
    REQUIRE(rig.Mint("id 0000000000000020\nentity b\n", &blob_b, &error).Ok());
    rig.resolver.AddBlob("/levels/b", 0x20, blob_b);
    Vector<byte> blob_a;
    REQUIRE(rig.Mint("id 0000000000000010\ninclude /levels/b\nentity a\n", &blob_a, &error).Ok());
    rig.resolver.AddBlob("/levels/a", 0x10, blob_a);

    Vector<byte> cyclic;
    const Status s = rig.Mint("id 0000000000000020\ninclude /levels/a\nentity b\n", &cyclic, &error);
    REQUIRE(s.Failed());
    CHECK(error.line == 2);
    CHECK(Contains(error.message.Data(), "cycle"));

    const Status self = rig.Mint("id 0000000000000020\ninclude @0000000000000020\n", &cyclic, &error);
    REQUIRE(self.Failed());
    CHECK(Contains(error.message.Data(), "include itself"));
}

TEST_CASE("A level of a mismatched type, or an old component version, is refused", "[asset][level]")
{
    Rig rig;
    Vector<byte> blob;
    LevelSourceError error;
    REQUIRE(rig.Mint(kSmall, &blob, &error).Ok());
    LevelData data;

    // A blob of another type.
    Vector<byte> mesh;
    {
        BlobWriter writer(rig.types, types::kMesh, Rid(5));
        REQUIRE(writer.Finish(&mesh).Ok());
    }
    CHECK(ParseLevelBlob(AsBytes(mesh), rig.types, rig.registry, &data).Is(ErrorCode::TypeMismatch));

    // The same entities with a component record at an older version: refused with a pointer to the fix.
    storage::EntityRecord record = data.entities.Size() == 0 ? storage::EntityRecord() : storage::EntityRecord();
    REQUIRE(ParseLevelBlob(AsBytes(blob), rig.types, rig.registry, &data).Ok());
    bytes::ByteWriter container;
    container.WriteU32(1);
    storage::ComponentRecord old;
    old.id = kMeshReferenceId;
    old.version = 0;  // not a version the registry has
    const byte payload[1] = {0};
    old.bytes = ByteSpan(payload, 1);
    REQUIRE(storage::WriteComponentRecord(old, storage::CodecLimits(), container).Failed());  // version 0 is not framed
    old.version = 2;
    container.Clear();
    container.WriteU32(1);
    REQUIRE(storage::WriteComponentRecord(old, storage::CodecLimits(), container).Ok());
    LevelData bad;
    storage::EntityKey key;
    key.low = 1;
    bad.entities.Push(storage::EntityRecord::Make(key, container.Bytes()));
    Vector<byte> out;
    const Status built = BuildLevelBlob(rig.types, rig.registry, Rid(kLevelId), bad, &out);
    CHECK(built.Is(ErrorCode::UnsupportedVersion));
    CHECK(Contains(built.Message(), "re-mint"));
    static_cast<void>(record);
}

TEST_CASE("Hostile level blobs are refused and the world is untouched", "[asset][level]")
{
    Rig rig;
    Vector<byte> blob;
    LevelSourceError error;
    REQUIRE(rig.Mint(kSmall, &blob, &error).Ok());

    // Every truncation fails; every flipped byte either fails or still loads, and a failure leaves the world empty.
    for (usize cut = 0; cut < blob.Size(); cut += 7)
    {
        ecs::World world;
        storage::KeyIndex index;
        CHECK(LoadLevelBlob(ByteSpan(blob.Data(), cut), rig.types, rig.registry, world, index).Failed());
        CHECK(KeyedCount(world) == 0);
    }
    usize refused = 0;
    for (usize at = 0; at < blob.Size(); ++at)
    {
        Vector<byte> damaged;
        for (usize i = 0; i < blob.Size(); ++i)
        {
            damaged.Push(blob[i]);
        }
        damaged[at] = static_cast<byte>(damaged[at] ^ 0x5a);
        ecs::World world;
        storage::KeyIndex index;
        const Status s = LoadLevelBlob(AsBytes(damaged), rig.types, rig.registry, world, index);
        if (s.Failed())
        {
            ++refused;
            CHECK(KeyedCount(world) == 0);
        }
    }
    CHECK(refused > blob.Size() / 2);  // the padding and the content hash are the bytes that may change unnoticed

    // The reference table must agree with the records: one id changed in the table, the dependency list untouched.
    LevelData data;
    REQUIRE(ParseLevelBlob(AsBytes(blob), rig.types, rig.registry, &data).Ok());
    BlobReader reader;
    REQUIRE(reader.Open(AsBytes(blob), rig.types).Ok());
    ByteSpan refs;
    REQUIRE(reader.FindSection(19, &refs));
    Vector<byte> tampered;
    for (usize i = 0; i < blob.Size(); ++i)
    {
        tampered.Push(blob[i]);
    }
    const usize refs_at = static_cast<usize>(refs.Data() - blob.Data());
    tampered[refs_at + 16] = static_cast<byte>(tampered[refs_at + 16] ^ 1);  // the id of the first reference
    LevelData after;
    const Status s = ParseLevelBlob(AsBytes(tampered), rig.types, rig.registry, &after);
    CHECK(s.Is(ErrorCode::Corrupt));

    // The count of one entity in the table must be its container's, not only the total.
    ByteSpan table;
    REQUIRE(reader.FindSection(17, &table));
    const usize table_at = static_cast<usize>(table.Data() - blob.Data());
    Vector<byte> counts;
    for (usize i = 0; i < blob.Size(); ++i)
    {
        counts.Push(blob[i]);
    }
    counts[table_at + 28] = static_cast<byte>(counts[table_at + 28] + 1);            // entity 0: one record more
    counts[table_at + 32 + 28] = static_cast<byte>(counts[table_at + 32 + 28] - 1);  // entity 1: one less
    const Status counted = ParseLevelBlob(AsBytes(counts), rig.types, rig.registry, &after);
    CHECK(counted.Is(ErrorCode::Corrupt));
    CHECK(Contains(counted.Message(), "record count"));
}

TEST_CASE("A record that fails to decode leaves the world as it was", "[asset][level]")
{
    Rig rig;
    // Three entities; the last has an entity_name that is not UTF-8, which the walker cannot see and the decoder can.
    LevelData data;
    for (u32 i = 1; i <= 3; ++i)
    {
        storage::EntityKey key;
        key.low = i;
        bytes::ByteWriter container;
        container.WriteU32(1);
        storage::ComponentRecord record;
        record.id = kEntityNameId;
        record.version = 1;
        const byte good[3] = {2, 'o', 'k'};
        const byte bad[2] = {1, 0xff};
        record.bytes = i == 3 ? ByteSpan(bad, 2) : ByteSpan(good, 3);
        REQUIRE(storage::WriteComponentRecord(record, storage::CodecLimits(), container).Ok());
        data.entities.Push(storage::EntityRecord::Make(key, container.Bytes()));
    }
    Vector<byte> blob;
    REQUIRE(BuildLevelBlob(rig.types, rig.registry, Rid(kLevelId), data, &blob).Ok());
    ecs::World world;
    storage::KeyIndex index;
    const Status s = LoadLevelBlob(AsBytes(blob), rig.types, rig.registry, world, index);
    CHECK(s.Failed());
    CHECK(KeyedCount(world) == 0);
}

TEST_CASE("The builder checks what the parser checks", "[asset][level]")
{
    Rig rig;
    auto entity = [](u64 low, const byte* bytes, usize size, u32 component)
    {
        storage::EntityKey key;
        key.low = low;
        bytes::ByteWriter container;
        container.WriteU32(1);
        storage::ComponentRecord record;
        record.id = component;
        record.version = 1;
        record.bytes = ByteSpan(bytes, size);
        REQUIRE(storage::WriteComponentRecord(record, storage::CodecLimits(), container).Ok());
        return storage::EntityRecord::Make(key, container.Bytes());
    };
    // A mesh_reference naming 0x201 (mesh), material null.
    byte mesh_ref[20] = {};
    mesh_ref[0] = 0x01;
    mesh_ref[1] = 0x02;
    Vector<byte> out;
    {
        LevelData d;  // the resource is not listed
        d.entities.Push(entity(1, mesh_ref, sizeof(mesh_ref), kMeshReferenceId));
        const Status s = BuildLevelBlob(rig.types, rig.registry, Rid(kLevelId), d, &out);
        CHECK(s.Failed());
        CHECK(Contains(s.Message(), "does not list"));
    }
    LevelResource mesh;
    mesh.id = Rid(0x201);
    mesh.type = types::kMesh;
    mesh.type_version = 1;
    mesh.roles = kLevelRoleReference;
    {
        LevelData d;  // listed with a type the field does not take
        d.entities.Push(entity(1, mesh_ref, sizeof(mesh_ref), kMeshReferenceId));
        LevelResource wrong = mesh;
        wrong.type = types::kTexture;
        d.resources.Push(wrong);
        const Status s = BuildLevelBlob(rig.types, rig.registry, Rid(kLevelId), d, &out);
        CHECK(s.Is(ErrorCode::DependencyTypeMismatch));
    }
    {
        LevelData d;  // listed and used: builds
        d.entities.Push(entity(1, mesh_ref, sizeof(mesh_ref), kMeshReferenceId));
        d.resources.Push(mesh);
        CHECK(BuildLevelBlob(rig.types, rig.registry, Rid(kLevelId), d, &out).Ok());
    }
    {
        LevelData d;  // listed and used by nothing
        const byte name[2] = {1, 'a'};
        d.entities.Push(entity(1, name, 2, kEntityNameId));
        d.resources.Push(mesh);
        const Status s = BuildLevelBlob(rig.types, rig.registry, Rid(kLevelId), d, &out);
        CHECK(Contains(s.Message(), "named by no component"));
    }
    {
        LevelData d;  // keys out of order
        const byte name[2] = {1, 'a'};
        d.entities.Push(entity(2, name, 2, kEntityNameId));
        d.entities.Push(entity(1, name, 2, kEntityNameId));
        CHECK(BuildLevelBlob(rig.types, rig.registry, Rid(kLevelId), d, &out).Failed());
    }
    {
        LevelData d;  // a derived component's record, and an unknown id
        d.entities.Push(entity(1, nullptr, 0, storage::kWorldTransformId));
        CHECK(BuildLevelBlob(rig.types, rig.registry, Rid(kLevelId), d, &out).Failed());
        LevelData e;
        e.entities.Push(entity(1, nullptr, 0, 777));
        CHECK(BuildLevelBlob(rig.types, rig.registry, Rid(kLevelId), e, &out).Failed());
    }
    CHECK(BuildLevelBlob(rig.types, rig.registry, ResourceId(), LevelData(), &out).Failed());
}

TEST_CASE("Two entities with one name cannot be printed", "[asset][level]")
{
    Rig rig;
    LevelData d;
    const byte name[2] = {1, 'a'};
    for (u64 key_low = 1; key_low <= 2; ++key_low)
    {
        storage::EntityKey key;
        key.low = key_low;
        bytes::ByteWriter container;
        container.WriteU32(1);
        storage::ComponentRecord record;
        record.id = kEntityNameId;
        record.version = 1;
        record.bytes = ByteSpan(name, 2);
        REQUIRE(storage::WriteComponentRecord(record, storage::CodecLimits(), container).Ok());
        d.entities.Push(storage::EntityRecord::Make(key, container.Bytes()));
    }
    Vector<byte> blob;
    REQUIRE(BuildLevelBlob(rig.types, rig.registry, Rid(kLevelId), d, &blob).Ok());
    String<HeapAllocator> text(HeapAllocator::Default());
    LevelSourceError error;
    CHECK(FormatLevelBlob(AsBytes(blob), rig.types, rig.registry, rig.resolver, &text, &error).Failed());
    CHECK(Contains(error.message.Data(), "two entities are called 'a'"));
}

TEST_CASE("The level summary checks its header against its sections", "[asset][level]")
{
    Rig rig;
    Vector<byte> blob;
    LevelSourceError error;
    REQUIRE(rig.Mint(kSmall, &blob, &error).Ok());
    LevelSummary summary;
    REQUIRE(ReadLevelSummary(AsBytes(blob), rig.types, &summary).Ok());
    CHECK(summary.entity_count == 3);
    CHECK(summary.resources.Size() == 4);

    BlobReader reader;
    REQUIRE(reader.Open(AsBytes(blob), rig.types).Ok());
    ByteSpan header;
    REQUIRE(reader.FindSection(16, &header));
    const usize header_at = static_cast<usize>(header.Data() - blob.Data());
    // A record count no entity count can hold.
    Vector<byte> many;
    for (usize i = 0; i < blob.Size(); ++i)
    {
        many.Push(blob[i]);
    }
    many[header_at + 7] = 0x7f;
    CHECK(ReadLevelSummary(AsBytes(many), rig.types, &summary).Is(ErrorCode::Corrupt));
    // An entity table whose last entry does not end where the containers end.
    ByteSpan table;
    REQUIRE(reader.FindSection(17, &table));
    const usize table_at = static_cast<usize>(table.Data() - blob.Data());
    Vector<byte> shifted;
    for (usize i = 0; i < blob.Size(); ++i)
    {
        shifted.Push(blob[i]);
    }
    shifted[table_at + 2 * 32 + 24] = static_cast<byte>(shifted[table_at + 2 * 32 + 24] + 1);  // the last size
    CHECK(ReadLevelSummary(AsBytes(shifted), rig.types, &summary).Is(ErrorCode::Corrupt));
}

// The figures behind the decision about column images (Phase 22 Day 334, spike 5): how long the loader takes for
// 100,000 entities, and how much of that is entity creation that any loader pays.
TEST_CASE("A level of 100,000 entities: load time and the floor of creating the entities", "[.][asset-bench]")
{
    Rig rig;
    const char* count_env = os::GetEnvironment("LEVEL_BENCH_ENTITIES");
    const u32 kCount = count_env != nullptr ? static_cast<u32>(CStrToLong(count_env)) : 100000;
    String<HeapAllocator> source(HeapAllocator::Default());
    source.Append(StringView("id 0000000000000001\n"));
    for (u32 i = 0; i < kCount; ++i)
    {
        char buffer[256];
        FormatTo(
            buffer, sizeof(buffer),
            "entity key @%032x\n  component local_transform\n    position = %u 0.25 -%u\n  component mesh_reference\n"
            "    mesh = /meshes/rock\n    material = /materials/stone\n",
            static_cast<unsigned>(i + 1), static_cast<unsigned>(i), static_cast<unsigned>(i));
        source.Append(Vp(buffer));
    }
    Vector<byte> blob;
    LevelSourceError error;
    const u64 m0 = os::MonotonicTimeNanos();
    REQUIRE(rig.Mint(source, &blob, &error).Ok());
    const u64 m1 = os::MonotonicTimeNanos();
    u64 best = ~u64{0};
    for (u32 round = 0; round < 3; ++round)
    {
        ecs::World world;
        storage::KeyIndex index;
        const u64 t0 = os::MonotonicTimeNanos();
        REQUIRE(LoadLevelBlob(AsBytes(blob), rig.types, rig.registry, world, index).Ok());
        const u64 t1 = os::MonotonicTimeNanos();
        best = t1 - t0 < best ? t1 - t0 : best;
    }
    // The decoding alone (the records parsed and checked, no world).
    u64 parse = ~u64{0};
    for (u32 round = 0; round < 3; ++round)
    {
        LevelData data;
        const u64 t0 = os::MonotonicTimeNanos();
        REQUIRE(ParseLevelBlob(AsBytes(blob), rig.types, rig.registry, &data).Ok());
        const u64 t1 = os::MonotonicTimeNanos();
        parse = t1 - t0 < parse ? t1 - t0 : parse;
    }
    // The floor: the entities and their two components made directly, with no decoding and no key index.
    u64 floor = ~u64{0};
    for (u32 round = 0; round < 3; ++round)
    {
        ecs::World world;
        const u64 t0 = os::MonotonicTimeNanos();
        for (u32 i = 0; i < kCount; ++i)
        {
            world.Create(ecs::LocalTransform(), MeshReference());
        }
        const u64 t1 = os::MonotonicTimeNanos();
        floor = t1 - t0 < floor ? t1 - t0 : floor;
    }
    os::Print(
        "level of %u entities: %zu bytes; mint %.1f ms; load %.1f ms (%.0f ns per entity), of which parsing %.1f ms; "
        "creating the entities alone %.1f ms (%.0f ns per entity)\n",
        static_cast<unsigned>(kCount), static_cast<size_t>(blob.Size()), static_cast<double>(m1 - m0) / 1e6,
        static_cast<double>(best) / 1e6, static_cast<double>(best) / kCount, static_cast<double>(parse) / 1e6,
        static_cast<double>(floor) / 1e6, static_cast<double>(floor) / kCount);
}

}  // namespace engine_placeholder::asset
