// Maintenance (2026-11-20): level_spawn checks the lights and morph weights a level brings before the world is touched,
// as component_set checks a host's. A level blob is the host's input; the render scene asserts on a light that is not
// finite, so a level with one must be refused, not drawn. The test imports a glTF with a spot light through the C API,
// rewrites the level's light record with a NaN radius (a valid blob: rebuilt, so its hashes match) and spawns it.
#include <catch2/catch_test_macros.hpp>

#include "engine/engine_c.h"
#include "asset/level.h"
#include "asset/resource_type.h"
#include "cstr/cstr.h"
#include "cstr/mem_ops.h"
#include "lang/limits.h"
#include "scene/scene_components.h"
#include "storage/builtin_components.h"
#include "animation/components.h"

using namespace engine_placeholder;

namespace
{

const char kSpotLevel
    [] = "{\"asset\":{\"version\":\"2.0\"},\"extensionsUsed\":[\"KHR_lights_punctual\"],"
         "\"extensions\":{\"KHR_lights_punctual\":{\"lights\":[{\"name\":\"torch\",\"type\":\"spot\",\"intensity\":10,"
         "\"range\":6,\"spot\":{\"innerConeAngle\":0.2,\"outerConeAngle\":0.6}}]}},"
         "\"scene\":0,\"scenes\":[{\"nodes\":[0]}],"
         "\"nodes\":[{\"name\":\"torch\",\"extensions\":{\"KHR_lights_punctual\":{\"light\":0}}}]}";

engine_string S(const char* text)
{
    return engine_string{text, CStrLength(text)};
}

usize CountEntities(const engine_api* api, engine_handle world)
{
    size_t total = 0;
    REQUIRE(api->query(world, 0, nullptr, 0, &total) == ENGINE_OK);
    return total;
}

}  // namespace

TEST_CASE("level_spawn refuses a level whose light is not valid, and leaves the world as it was", "[engine_c][level]")
{
    const engine_api* api = nullptr;
    REQUIRE(engine_get_api(ENGINE_ABI_VERSION_MAJOR, ENGINE_ABI_VERSION_MINOR, sizeof(engine_api), &api) == ENGINE_OK);
    engine_create_info info = {};
    info.struct_size = sizeof(info);
    info.flags = ENGINE_CREATE_LOG_SUPPRESS_CONSOLE;
    engine_handle engine = {};
    REQUIRE(api->create(&info, &engine) == ENGINE_OK);
    engine_import_options options = {};
    options.struct_size = sizeof(options);
    options.flags = ENGINE_IMPORT_EMIT_LEVEL;
    options.id_seed = 99;
    options.path_prefix = S("/spot");
    engine_import_result result = {};
    result.struct_size = sizeof(result);
    REQUIRE(api->import_gltf(engine, kSpotLevel, sizeof(kSpotLevel) - 1, &options, &result) == ENGINE_OK);
    REQUIRE(result.resource_count == 1);  // the level alone
    u64 level = 0;
    MemCopy(&level, result.ids.data, sizeof(level));

    engine_handle world = {};
    REQUIRE(api->world_create(engine, &world) == ENGINE_OK);
    u64 root = 0;
    REQUIRE(api->level_spawn(world, level, nullptr, &root, nullptr, 0, nullptr) == ENGINE_OK);  // valid as imported
    const usize before = CountEntities(api, world);

    // The level as a description; its one light record gets a NaN radius; the blob is built again.
    engine_buffer blob = {};
    REQUIRE(api->resource_read(engine, level, 0, &blob) == ENGINE_OK);
    asset::TypeRegistry types;
    storage::ComponentRegistry registry;
    REQUIRE(storage::RegisterBuiltinComponents(&registry).Ok());
    REQUIRE(asset::RegisterLevelComponents(&registry).Ok());
    REQUIRE(animation::RegisterAnimationComponents(&registry).Ok());
    REQUIRE(scene::RegisterSceneComponents(&registry).Ok());
    asset::LevelData data;
    asset::ResourceId id;
    REQUIRE(asset::ParseLevelBlob(ByteSpan(reinterpret_cast<const byte*>(blob.data), blob.size), types, registry, &data,
                                  &id)
                .Ok());
    const f32 six = 6.0f;
    const f32 nan = Limits<f32>::QuietNaN();
    usize patched = 0;
    for (storage::EntityRecord& record : data.entities)
    {
        Vector<byte> bytes;
        bytes.Resize(record.blob.Size());
        MemCopy(bytes.Data(), record.blob.Data(), bytes.Size());
        for (usize at = 0; at + sizeof(f32) <= bytes.Size(); ++at)
        {
            if (MemCompare(bytes.Data() + at, &six, sizeof(f32)) == 0)
            {
                MemCopy(bytes.Data() + at, &nan, sizeof(f32));
                ++patched;
            }
        }
        record = storage::EntityRecord::Make(record.key, ByteSpan(bytes.Data(), bytes.Size()));
    }
    REQUIRE(patched == 1);  // the spot's radius (its range)
    Vector<byte> bad;
    REQUIRE(asset::BuildLevelBlob(types, registry, id, data, &bad).Ok());
    REQUIRE(api->resource_remove(engine, level, ENGINE_RESOURCE_FORCE, nullptr) == ENGINE_OK);
    engine_resource_add_info add = {};
    add.struct_size = sizeof(add);
    add.id = level;
    add.bytes = bad.Data();
    add.size = bad.Size();
    REQUIRE(api->resource_add(engine, &add) == ENGINE_OK);

    u64 none = 0;
    CHECK(api->level_spawn(world, level, nullptr, &none, nullptr, 0, nullptr) == ENGINE_ERROR_INVALID_ARGUMENT);
    CHECK(none == 0);
    CHECK(CountEntities(api, world) == before);

    engine_buffer_release(&blob);
    engine_buffer_release(&result.ids);
    engine_buffer_release(&result.types);
    engine_buffer_release(&result.paths);
    engine_buffer_release(&result.warnings);
    engine_buffer_release(&result.manifest);
    engine_buffer_release(&result.summary);
    REQUIRE(api->world_destroy(world) == ENGINE_OK);
    REQUIRE(api->destroy(engine) == ENGINE_OK);
}

// glTF colliders Day 5: a level whose static collider is not valid (a box with a NaN size) is refused whole.
TEST_CASE("level_spawn refuses a level whose static collider is not valid", "[engine_c][level]")
{
    const char box_level
        [] = "{\"asset\":{\"version\":\"2.0\"},\"extensionsUsed\":[\"OMI_physics_body\",\"OMI_physics_shape\"],"
             "\"extensions\":{\"OMI_physics_shape\":{\"shapes\":[{\"type\":\"box\",\"box\":{\"size\":[2,4,6]}}]}},"
             "\"scene\":0,\"scenes\":[{\"nodes\":[0]}],"
             "\"nodes\":[{\"name\":\"crate\",\"extensions\":{\"OMI_physics_body\":{\"collider\":{\"shape\":0}}}}]}";
    const engine_api* api = nullptr;
    REQUIRE(engine_get_api(ENGINE_ABI_VERSION_MAJOR, ENGINE_ABI_VERSION_MINOR, sizeof(engine_api), &api) == ENGINE_OK);
    engine_create_info info = {};
    info.struct_size = sizeof(info);
    info.flags = ENGINE_CREATE_LOG_SUPPRESS_CONSOLE;
    engine_handle engine = {};
    REQUIRE(api->create(&info, &engine) == ENGINE_OK);
    engine_import_options options = {};
    options.struct_size = sizeof(options);
    options.flags = ENGINE_IMPORT_EMIT_LEVEL;
    options.id_seed = 98;
    options.path_prefix = S("/box");
    engine_import_result result = {};
    result.struct_size = sizeof(result);
    REQUIRE(api->import_gltf(engine, box_level, sizeof(box_level) - 1, &options, &result) == ENGINE_OK);
    REQUIRE(result.resource_count == 1);
    u64 level = 0;
    MemCopy(&level, result.ids.data, sizeof(level));
    engine_handle world = {};
    REQUIRE(api->world_create(engine, &world) == ENGINE_OK);
    u64 root = 0;
    REQUIRE(api->level_spawn(world, level, nullptr, &root, nullptr, 0, nullptr) == ENGINE_OK);
    const usize before = CountEntities(api, world);

    engine_buffer blob = {};
    REQUIRE(api->resource_read(engine, level, 0, &blob) == ENGINE_OK);
    asset::TypeRegistry types;
    storage::ComponentRegistry registry;
    REQUIRE(storage::RegisterBuiltinComponents(&registry).Ok());
    REQUIRE(asset::RegisterLevelComponents(&registry).Ok());
    REQUIRE(animation::RegisterAnimationComponents(&registry).Ok());
    REQUIRE(scene::RegisterSceneComponents(&registry).Ok());
    asset::LevelData data;
    asset::ResourceId id;
    REQUIRE(asset::ParseLevelBlob(ByteSpan(reinterpret_cast<const byte*>(blob.data), blob.size), types, registry, &data,
                                  &id)
                .Ok());
    const f32 three = 3.0f;  // the box's half size along z
    const f32 nan = Limits<f32>::QuietNaN();
    usize patched = 0;
    for (storage::EntityRecord& record : data.entities)
    {
        Vector<byte> bytes;
        bytes.Resize(record.blob.Size());
        MemCopy(bytes.Data(), record.blob.Data(), bytes.Size());
        for (usize at = 0; at + sizeof(f32) <= bytes.Size(); ++at)
        {
            if (MemCompare(bytes.Data() + at, &three, sizeof(f32)) == 0)
            {
                MemCopy(bytes.Data() + at, &nan, sizeof(f32));
                ++patched;
            }
        }
        record = storage::EntityRecord::Make(record.key, ByteSpan(bytes.Data(), bytes.Size()));
    }
    REQUIRE(patched == 1);
    Vector<byte> bad;
    REQUIRE(asset::BuildLevelBlob(types, registry, id, data, &bad).Ok());
    REQUIRE(api->resource_remove(engine, level, ENGINE_RESOURCE_FORCE, nullptr) == ENGINE_OK);
    engine_resource_add_info add = {};
    add.struct_size = sizeof(add);
    add.id = level;
    add.bytes = bad.Data();
    add.size = bad.Size();
    REQUIRE(api->resource_add(engine, &add) == ENGINE_OK);
    u64 none = 0;
    CHECK(api->level_spawn(world, level, nullptr, &none, nullptr, 0, nullptr) == ENGINE_ERROR_INVALID_ARGUMENT);
    CHECK(none == 0);
    CHECK(CountEntities(api, world) == before);

    engine_buffer_release(&blob);
    engine_buffer_release(&result.ids);
    engine_buffer_release(&result.types);
    engine_buffer_release(&result.paths);
    engine_buffer_release(&result.warnings);
    engine_buffer_release(&result.manifest);
    engine_buffer_release(&result.summary);
    REQUIRE(api->world_destroy(world) == ENGINE_OK);
    REQUIRE(api->destroy(engine) == ENGINE_OK);
}
