// A replaced height map must not leave its predecessors pinned: ten replacements of a 1024 by 1024 map hold the newest
// map and one earlier generation, not eleven maps (each a few megabytes with its mip chain).
#include <catch2/catch_test_macros.hpp>

#include "engine/engine_c.h"
#include "asset/height_map.h"
#include "engine_c/resources.h"
#include "containers/vector.h"
#include "cstr/mem_ops.h"

using namespace vse;

TEST_CASE("Replacing a height map ten times keeps at most two pins and the right hash", "[engine_c][terrain]")
{
    const engine_api* api = nullptr;
    REQUIRE(engine_get_api(ENGINE_ABI_VERSION_MAJOR, ENGINE_ABI_VERSION_MINOR, sizeof(engine_api), &api) == ENGINE_OK);
    engine_create_info info = {};
    info.struct_size = sizeof(info);
    info.flags = ENGINE_CREATE_LOG_SUPPRESS_CONSOLE;
    engine_handle engine = {};
    REQUIRE(api->create(&info, &engine) == ENGINE_OK);
    engine_c::ResourceHost* host = nullptr;
    REQUIRE(engine_c::HostOfEngine(engine, &host) == ENGINE_OK);

    constexpr u32 kSide = 1024;
    constexpr u64 kId = 0x7e11;
    Vector<u16> samples;
    samples.Resize(usize{kSide} * kSide);
    for (usize i = 0; i < samples.Size(); ++i)
    {
        samples[i] = static_cast<u16>(i % 1000);
    }
    REQUIRE(api->height_map_create(engine, kId, samples.Data(), kSide, kSide, 0.0f, 10.0f, 1.0f) == ENGINE_OK);
    const asset::HeightMapView* view = nullptr;
    REQUIRE(host->PinHeightMap(kId, &view, nullptr) == ENGINE_OK);
    CHECK(host->HeightMapPinCount() == 1);

    for (u32 round = 1; round <= 10; ++round)
    {
        samples[0] = static_cast<u16>(round);  // a different content hash each time
        asset::HeightMapHeader header;
        header.width = kSide;
        header.height = kSide;
        header.range_min = 0.0f;
        header.range_max = 10.0f;
        Vector<byte> little;
        little.Resize(samples.Size() * 2);
        for (usize i = 0; i < samples.Size(); ++i)
        {
            little[2 * i] = static_cast<byte>(samples[i] & 0xFFu);
            little[2 * i + 1] = static_cast<byte>(samples[i] >> 8);
        }
        Vector<byte> blob;
        REQUIRE(asset::BuildHeightMapBlob(host->Types(), asset::ResourceId::FromValue(kId), header,
                                          ByteSpan(little.Data(), little.Size()), &blob)
                    .Ok());
        engine_resource_add_info replace = {};
        replace.struct_size = sizeof(replace);
        replace.id = kId;
        replace.bytes = blob.Data();
        replace.size = blob.Size();
        engine_buffer dependents = {};
        REQUIRE(api->resource_replace(engine, &replace, &dependents) == ENGINE_OK);
        engine_buffer_release(&dependents);
        asset::ContentHash hash;
        REQUIRE(host->PinHeightMap(kId, &view, &hash) == ENGINE_OK);
        CHECK(view->Sample(0, 0) == round);
        CHECK(MemCompare(hash.bytes, asset::HashBytes(view->Bytes()->Bytes()).bytes, sizeof(hash.bytes)) == 0);
        CHECK(host->HeightMapPinCount() <= 2);
    }
    CHECK(host->HeightMapPinCount() <= 2);
    REQUIRE(api->destroy(engine) == ENGINE_OK);
}
