// The scene's components in the storage registry (Phase 27 Day 456): they register, save and load.
#include <catch2/catch_test_macros.hpp>

#include "ecs/transform.h"
#include "scene/components.h"
#include "scene/scene_components.h"
#include "storage/builtin_components.h"
#include "storage/identity.h"
#include "storage/memory_store.h"
#include "storage/world_storage.h"

using namespace engine_placeholder;

TEST_CASE("Scene components: a light, a camera and a hidden tag are saved and loaded", "[scene][storage]")
{
    storage::ComponentRegistry registry;
    REQUIRE(storage::RegisterBuiltinComponents(&registry).Ok());
    REQUIRE(scene::RegisterSceneComponents(&registry).Ok());
    CHECK(registry.FindByName("light_source") != nullptr);
    CHECK(registry.FindByName("camera") != nullptr);
    CHECK(registry.FindByName("hidden")->IsTag());
    CHECK_FALSE(scene::RegisterSceneComponents(&registry).Ok());  // registered already

    ecs::World world;
    storage::KeyIndex index;
    storage::KeyGenerator generator = storage::KeyGenerator::Seeded(3);
    scene::LightSource light;
    light.type = renderer::LightType::Point;
    light.intensity = 3.5f;
    light.radius = 7.0f;
    const ecs::Entity e = world.Create(storage::EntityKeyComponent{generator.Next()}, light, scene::Camera(),
                                       scene::Hidden());
    REQUIRE(e.IsValid());
    storage::MemoryStore store;
    storage::WorldStorage storage(registry, store);
    REQUIRE(storage.Save(world, index, storage::SaveOptions(), nullptr).Ok());

    ecs::World loaded;
    storage::KeyIndex loaded_index;
    REQUIRE(storage.Load(loaded, loaded_index, storage::LoadOptions(), nullptr).Ok());
    usize found = 0;
    loaded.Query<ecs::Read<scene::LightSource>, ecs::Read<scene::Camera>, ecs::With<scene::Hidden>>().ForEach(
        [&](ecs::Entity, const scene::LightSource& l, const scene::Camera& c)
        {
            CHECK(l.type == renderer::LightType::Point);
            CHECK(l.intensity == 3.5f);
            CHECK(l.radius == 7.0f);
            CHECK(c.far_plane == 1000.0f);
            ++found;
        });
    CHECK(found == 1);
}
