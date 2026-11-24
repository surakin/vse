// The rules a stored environment must meet whatever the sky mode is, and the extra ones of a procedural sky. The script
// merge refuses an environment that fails them (a sky mode switched on over sky fields a host left at zero).
#include <catch2/catch_test_macros.hpp>

#include "engine/engine_c.h"
#include "engine_c/runtime.h"
#include "asset/texture_mint.h"
#include "containers/vector.h"
#include "engine_c/resources.h"
#include "lang/limits.h"

using namespace vse;
using namespace vse::engine_c;

TEST_CASE("EnvironmentUsable: finite fields always, a real sun for the procedural sky", "[engine_c][environment]")
{
    Environment e;
    CHECK(EnvironmentUsable(e));

    Environment nan_zenith = e;
    nan_zenith.sky_zenith[2] = Limits<f32>::QuietNaN();
    CHECK_FALSE(EnvironmentUsable(nan_zenith));  // in the flat-colour mode as well

    Environment infinite_sun = e;
    infinite_sun.sun_direction[1] = Limits<f32>::Infinity();
    CHECK_FALSE(EnvironmentUsable(infinite_sun));

    Environment host_zeroed = e;  // what a zero-filled struct stores
    host_zeroed.sun_direction[0] = host_zeroed.sun_direction[1] = host_zeroed.sun_direction[2] = 0.0f;
    host_zeroed.sun_size = 0.0f;
    host_zeroed.sky_horizon_blend = 0.0f;
    host_zeroed.sky_exposure = 0.0f;
    CHECK(EnvironmentUsable(host_zeroed));         // a flat sky needs none of them
    host_zeroed.sky_mode = ENGINE_SKY_PROCEDURAL;  // a script switching the sky on over them
    CHECK_FALSE(EnvironmentUsable(host_zeroed));

    Environment bad_mode = e;
    bad_mode.sky_mode = 9;
    CHECK_FALSE(EnvironmentUsable(bad_mode));
}

TEST_CASE("world_set_sky_cube names a mounted texture, and the rotation must be finite", "[engine_c][environment]")
{
    const engine_api* api = nullptr;
    REQUIRE(engine_get_api(ENGINE_ABI_VERSION_MAJOR, ENGINE_ABI_VERSION_MINOR, sizeof(engine_api), &api) == ENGINE_OK);
    engine_create_info info = {};
    info.struct_size = sizeof(info);
    info.flags = ENGINE_CREATE_LOG_SUPPRESS_CONSOLE;
    engine_handle engine = {};
    REQUIRE(api->create(&info, &engine) == ENGINE_OK);
    ResourceHost* host = nullptr;
    REQUIRE(HostOfEngine(engine, &host) == ENGINE_OK);

    constexpr u64 kCube = 0x5c01;
    Vector<byte> face;
    face.Resize(usize{4} * 4 * 4);
    for (usize i = 0; i < face.Size(); ++i)
    {
        face[i] = static_cast<byte>(40 + i);
    }
    ByteSpan faces[6];
    for (ByteSpan& f : faces)
    {
        f = ByteSpan(face.Data(), face.Size());
    }
    asset::TextureMintSpec spec;
    spec.kind = asset::TextureKind::Cube;
    Vector<byte> blob;
    REQUIRE(asset::MintTextureFromPixels(host->Types(), asset::ResourceId::FromValue(kCube), spec, 4, 4,
                                         Span<const ByteSpan>(faces, 6), &blob)
                .Ok());
    engine_resource_add_info add = {};
    add.struct_size = sizeof(add);
    add.id = kCube;
    add.bytes = blob.Data();
    add.size = blob.Size();
    REQUIRE(api->resource_add(engine, &add) == ENGINE_OK);
    const u16 flat[4] = {1, 2, 3, 4};
    REQUIRE(api->height_map_create(engine, 0x5c02, flat, 2, 2, 0.0f, 1.0f, 1.0f) == ENGINE_OK);

    engine_handle world = {};
    REQUIRE(api->world_create(engine, &world) == ENGINE_OK);
    CHECK(api->world_set_sky_cube(world, kCube) == ENGINE_OK);
    CHECK(api->world_set_sky_cube(world, 0x5c99) == ENGINE_ERROR_NOT_FOUND);
    CHECK(api->world_set_sky_cube(world, 0x5c02) == ENGINE_ERROR_TYPE_MISMATCH);
    CHECK(api->world_set_sky_cube(world, 0) == ENGINE_OK);

    engine_environment e = {};
    e.struct_size = sizeof(e);
    e.exposure = 1.0f;
    CHECK(api->world_set_environment(world, &e) == ENGINE_OK);
    e.sky_rotation = Limits<f32>::QuietNaN();
    CHECK(api->world_set_environment(world, &e) == ENGINE_ERROR_INVALID_ARGUMENT);
    e.struct_size = offsetof(engine_environment, sky_rotation);  // the earlier struct: no rotation to check
    CHECK(api->world_set_environment(world, &e) == ENGINE_OK);

    REQUIRE(api->world_destroy(world) == ENGINE_OK);
    REQUIRE(api->destroy(engine) == ENGINE_OK);
}
