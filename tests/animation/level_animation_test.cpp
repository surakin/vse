// Skeletal components in levels (Phase 22 Day 327): a level with a skinned mesh, its skeleton and a looping clip mints,
// loads and plays; a clip made for another skeleton is a mint error.

#include <catch2/catch_test_macros.hpp>

#include "animation/components.h"
#include "asset/level.h"
#include "cstr/cstr.h"
#include "cstr/format.h"
#include "clip_source.h"
#include "level_source.h"
#include "skeleton_source.h"
#include "storage/builtin_components.h"

namespace engine_placeholder::animation
{
namespace
{

using asset::ResourceId;

bool Has(const char* haystack, const char* needle)
{
    return StringView(haystack, CStrLength(haystack)).Find(StringView(needle, CStrLength(needle))) !=
           CStrLength(haystack);
}

class LaResolver final : public asset::LevelReferenceResolver, public AnimationResolver
{
public:
    struct Entry
    {
        const char* path = "";
        ResourceId id;
        Vector<byte> blob;
    };

    asset::Status ResolvePath(StringView path, ResourceId* id, ByteSpan* blob) const override
    {
        for (usize i = 0; i < entries.Size(); ++i)
        {
            if (StringView(entries[i].path, CStrLength(entries[i].path)) == path)
            {
                *id = entries[i].id;
                *blob = ByteSpan(entries[i].blob.Data(), entries[i].blob.Size());
                return asset::Status::Success();
            }
        }
        return asset::Status::Make(asset::ErrorCode::NotFound, "no such resource");
    }
    asset::Status ResolveId(ResourceId id, ByteSpan* blob) const override
    {
        for (usize i = 0; i < entries.Size(); ++i)
        {
            if (entries[i].id == id)
            {
                *blob = ByteSpan(entries[i].blob.Data(), entries[i].blob.Size());
                return asset::Status::Success();
            }
        }
        return asset::Status::Make(asset::ErrorCode::NotFound, "no such id");
    }
    asset::Status PathOf(ResourceId id, String<HeapAllocator>* path) const override
    {
        for (usize i = 0; i < entries.Size(); ++i)
        {
            if (entries[i].id == id)
            {
                *path = String<HeapAllocator>(HeapAllocator::Default(),
                                              StringView(entries[i].path, CStrLength(entries[i].path)));
                return asset::Status::Success();
            }
        }
        return asset::Status::Make(asset::ErrorCode::NotFound, "no such id");
    }
    const asset::SkeletonView* FindSkeleton(ResourceId id) const override
    {
        return id == skeleton_id ? &skeleton : nullptr;
    }
    const asset::ClipView* FindClip(ResourceId id) const override { return id == clip_id ? &clip : nullptr; }

    Entry* Add(const char* path, u64 id)
    {
        Entry entry;
        entry.path = path;
        entry.id = ResourceId::FromValue(id);
        entries.Push(static_cast<Entry&&>(entry));
        return &entries[entries.Size() - 1];
    }

    Vector<Entry> entries;
    asset::SkeletonView skeleton;
    asset::ClipView clip;
    ResourceId skeleton_id;
    ResourceId clip_id;
};

const char* kSkeletonText(u64 id, char (&buffer)[256])
{
    FormatTo(buffer, sizeof(buffer),
             "id %016llx\njoint root - 0 0 0  0 0 0 1  1 1 1\njoint mid root 0 1 0  0 0 0 1  1 1 1\n"
             "joint tip mid 0 1 0  0 0 0 1  1 1 1\n",
             static_cast<unsigned long long>(id));
    return buffer;
}

struct World
{
    asset::TypeRegistry types;
    storage::ComponentRegistry registry;
    LaResolver resolver;

    World()
    {
        REQUIRE(storage::RegisterBuiltinComponents(&registry).Ok());
        REQUIRE(asset::RegisterLevelComponents(&registry).Ok());
        REQUIRE(RegisterAnimationComponents(&registry).Ok());

        char buffer[256];
        LaResolver::Entry* skel = resolver.Add("/skeletons/hero", 0xb0);
        asset::SkeletonSourceError skeleton_error;
        const char* text = kSkeletonText(0xb0, buffer);
        REQUIRE(asset::MintSkeleton(StringView(text, CStrLength(text)), types, &skel->blob, &skeleton_error).Ok());
        REQUIRE(resolver.skeleton.Open(ByteSpan(skel->blob.Data(), skel->blob.Size()), types).Ok());
        resolver.skeleton_id = skel->id;

        LaResolver::Entry* other = resolver.Add("/skeletons/other", 0xb1);
        text = kSkeletonText(0xb1, buffer);
        REQUIRE(asset::MintSkeleton(StringView(text, CStrLength(text)), types, &other->blob, &skeleton_error).Ok());

        LaResolver::Entry* walk = resolver.Add("/clips/walk", 0xc0);
        const char* clip_text =
            "id 00000000000000c0\nskeleton 00000000000000b0\nduration 1\nrate 30\nflags loop\ntrack root\n"
            "r 0 0 0 0 1\nr 0.5 0 0 0.3826834 0.9238795\nr 1 0 0 0 1\ntrack mid\nr 0 0 0 0 1\nr 1 0 0 0.2 0.9797959\n";
        asset::ClipSourceError clip_error;
        const asset::Status clip_status = asset::MintClip(StringView(clip_text, CStrLength(clip_text)),
                                                          resolver.skeleton, types, asset::ClipLimits(), &walk->blob,
                                                          &clip_error);
        INFO(clip_error.message.Data());
        REQUIRE(clip_status.Ok());
        REQUIRE(resolver.clip.Open(ByteSpan(walk->blob.Data(), walk->blob.Size()), types).Ok());
        resolver.clip_id = walk->id;

        LaResolver::Entry* mesh = resolver.Add("/meshes/hero", 0x201);
        asset::BlobWriter writer(types, asset::types::kMesh, mesh->id);
        REQUIRE(writer.Finish(&mesh->blob).Ok());
    }
};

const char* kHero = "id 00000000000000d0\n"
                    "entity hero\n"
                    "  component local_transform\n"
                    "    position = 0 1 0\n"
                    "  component mesh_reference\n"
                    "    mesh = /meshes/hero\n"
                    "  skeleton /skeletons/hero\n"
                    "  animation /clips/walk loop speed 2\n";

}  // namespace

TEST_CASE("level: a skinned mesh with its skeleton and a looping clip mints, loads and plays", "[animation][level]")
{
    World w;
    Vector<byte> blob;
    asset::LevelSourceError error;
    const asset::Status minted = asset::MintLevel(StringView(kHero, CStrLength(kHero)), w.resolver, w.types, w.registry,
                                                  &blob, &error);
    INFO(error.message.Data() << " at line " << error.line);
    REQUIRE(minted.Ok());

    ecs::World world;
    storage::KeyIndex index;
    asset::LevelData data;
    REQUIRE(asset::LoadLevelBlob(ByteSpan(blob.Data(), blob.Size()), w.types, w.registry, world, index, &data).Ok());
    // The skeleton and the clip are listed with their types, beside the mesh.
    REQUIRE(data.resources.Size() == 3);
    CHECK(data.resources[0].id == ResourceId::FromValue(0xb0));
    CHECK(data.resources[0].type == asset::types::kSkeleton);
    CHECK(data.resources[1].id == ResourceId::FromValue(0xc0));
    CHECK(data.resources[1].type == asset::types::kAnimation);
    CHECK(data.resources[2].type == asset::types::kMesh);

    const ecs::Entity hero = index.Find(asset::LevelEntityKey(ResourceId::FromValue(0xd0), StringView("hero")));
    REQUIRE(hero.IsValid());
    const AnimationPlayer* player = world.Peek<AnimationPlayer>(hero);
    REQUIRE(player != nullptr);
    CHECK(player->clip == 0xc0);
    CHECK(player->looping);
    CHECK(player->speed == 2.0f);
    CHECK(player->playing);  // the component's default
    const SkeletonInstance* instance = world.Peek<SkeletonInstance>(hero);
    REQUIRE(instance != nullptr);
    CHECK(instance->skeleton == 0xb0);

    // It plays: the palette is filled and time advances at twice the frame time.
    AnimationSystem animator(&w.resolver);
    const AnimationStats stats = animator.Update(&world, nullptr, 0.25f);
    CHECK(stats.players_updated == 1);
    CHECK(stats.refused == 0);
    CHECK(world.Peek<SkeletonInstance>(hero)->palette.Size() == 3);
    CHECK(world.Peek<AnimationPlayer>(hero)->time == 0.5f);

    // The long form is what prints back, and it mints to the same blob.
    String<HeapAllocator> text(HeapAllocator::Default());
    REQUIRE(asset::FormatLevelBlob(ByteSpan(blob.Data(), blob.Size()), w.types, w.registry, w.resolver, &text, &error)
                .Ok());
    CHECK(Has(text.View().Data(), "component animation_player"));
    CHECK(Has(text.View().Data(), "clip = /clips/walk"));
    CHECK(Has(text.View().Data(), "looping = true"));
    CHECK(Has(text.View().Data(), "skeleton = /skeletons/hero"));
    Vector<byte> again;
    const asset::Status remint = asset::MintLevel(text.View(), w.resolver, w.types, w.registry, &again, &error);
    INFO(remint.Message() << ": " << error.message.Data() << " at line " << error.line << "\n" << text.View().Data());
    REQUIRE(remint.Ok());
    REQUIRE(again.Size() == blob.Size());
    CHECK(MemCompare(again.Data(), blob.Data(), blob.Size()) == 0);
}

TEST_CASE("level: a clip made for another skeleton than the entity's is a mint error", "[animation][level]")
{
    World w;
    const char* text = "id 00000000000000d0\n"
                       "entity good\n"
                       "  skeleton /skeletons/hero\n"
                       "  animation /clips/walk\n"
                       "entity bad\n"
                       "  skeleton /skeletons/other\n"
                       "  animation /clips/walk once\n";
    Vector<byte> blob;
    asset::LevelSourceError error;
    REQUIRE(
        asset::MintLevel(StringView(text, CStrLength(text)), w.resolver, w.types, w.registry, &blob, &error).Failed());
    CHECK(error.line == 7);  // the animation line of the entity that has the mismatch
    CHECK(Has(error.message.Data(), "another skeleton"));
}

TEST_CASE("level: the skeleton and the animation lines refuse what the components refuse", "[animation][level]")
{
    World w;
    struct Case
    {
        const char* body;
        u32 line;
        const char* needle;
    };
    const Case cases[] = {
        {"skeleton /meshes/hero\n", 3, "is a mesh but the field takes a skeleton"},
        {"skeleton null\n", 3, "may not be null"},
        {"animation /skeletons/hero\n", 3, "is a skeleton but the field takes a animation"},
        {"animation /clips/walk loop loop\n", 3, "set twice"},
        {"animation /clips/walk fast\n", 3, "loop, once and speed"},
        {"animation /clips/walk speed\n", 3, "loop, once and speed"},
        {"animation\n", 3, "path is expected"},
        {"skeleton /skeletons/hero\nskeleton /skeletons/hero\n", 4, "this component already"},
    };
    for (const Case& c : cases)
    {
        char text[512];
        FormatTo(text, sizeof(text), "id 00000000000000d0\nentity e\n%s", c.body);
        Vector<byte> blob;
        asset::LevelSourceError error;
        INFO(text);
        REQUIRE(asset::MintLevel(StringView(text, CStrLength(text)), w.resolver, w.types, w.registry, &blob, &error)
                    .Failed());
        INFO(error.message.Data());
        CHECK(error.line == c.line);
        CHECK(Has(error.message.Data(), c.needle));
    }
}

}  // namespace engine_placeholder::animation
