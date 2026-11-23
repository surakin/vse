// The terrain material's CPU rules (Phase 28 Day 489): weight normalisation, the layer weights the shader mirrors, and
// the validation. The shader itself is compared on a real device in tests/rhi/terrain_gpu_test.cpp.
#include "lang/scalar.h"

#include <catch2/catch_test_macros.hpp>

#include "renderer/terrain_material.h"

using namespace vse;
using namespace vse::renderer;

TEST_CASE("terrain material: splat weights are normalised to exactly 255", "[renderer][terrain]")
{
    u32 state = 12345;
    for (u32 n = 0; n < 5000; ++n)
    {
        u8 w[4];
        for (u32 i = 0; i < 4; ++i)
        {
            state = state * 1664525u + 1013904223u;
            // a mix of zeros, small and large values
            w[i] = (state >> 24) % 3 == 0 ? 0 : static_cast<u8>(state >> 16);
        }
        const u8 before[4] = {w[0], w[1], w[2], w[3]};
        TerrainNormalizeWeights(w);
        CHECK(static_cast<u32>(w[0]) + w[1] + w[2] + w[3] == 255);
        for (u32 i = 0; i < 4; ++i)
        {
            if (before[i] == 0 && (before[0] | before[1] | before[2] | before[3]) != 0)
            {
                CHECK(w[i] == 0);
            }
        }
    }
    u8 zero[4] = {0, 0, 0, 0};
    TerrainNormalizeWeights(zero);
    CHECK(zero[0] == 255);
    CHECK(zero[1] + zero[2] + zero[3] == 0);
    u8 equal[4] = {10, 10, 10, 10};  // 63.75 each: the largest remainders go to the lowest channels
    TerrainNormalizeWeights(equal);
    CHECK(equal[0] == 64);
    CHECK(equal[1] == 64);
    CHECK(equal[2] == 64);
    CHECK(equal[3] == 63);
    u8 done[4] = {255, 0, 0, 0};
    TerrainNormalizeWeights(done);
    CHECK(done[0] == 255);
}

TEST_CASE("terrain material: the layer weights sum to one and follow the slope rule", "[renderer][terrain]")
{
    TerrainMaterial m;
    m.layer_count = 3;
    const f32 splat[4] = {0.0f, 1.0f, 0.0f, 1.0f};  // channel 3 is a layer the material does not use
    f32 w[4];
    TerrainLayerWeights(m, splat, 1.0f, w);
    CHECK(w[0] == 0.0f);
    CHECK(w[1] == 1.0f);
    CHECK(w[3] == 0.0f);
    const f32 none[4] = {0, 0, 0, 0};
    TerrainLayerWeights(m, none, 1.0f, w);
    CHECK(w[0] == 1.0f);
    m.slope_layer = 2;
    m.slope_start = 0.2f;
    m.slope_end = 0.4f;
    TerrainLayerWeights(m, splat, 0.9f, w);  // 1 - 0.9 = 0.1 is below the range: the splat alone
    CHECK(w[1] == 1.0f);
    CHECK(w[2] == 0.0f);
    TerrainLayerWeights(m, splat, 0.5f, w);  // above the range: the slope layer alone
    CHECK(w[2] == 1.0f);
    CHECK(w[1] == 0.0f);
    TerrainLayerWeights(m, splat, 0.7f, w);  // inside: a mix that sums to one
    CHECK(w[1] > 0.0f);
    CHECK(w[2] > 0.0f);
    CHECK(Abs(w[0] + w[1] + w[2] + w[3] - 1.0f) < 1e-6f);
}

TEST_CASE("terrain material: validation names the first broken rule", "[renderer][terrain]")
{
    TerrainMaterial m;
    CHECK(TerrainMaterialInvalid(m) == nullptr);
    m.layer_count = 0;
    CHECK(TerrainMaterialInvalid(m) != nullptr);
    m.layer_count = 5;
    CHECK(TerrainMaterialInvalid(m) != nullptr);
    m = TerrainMaterial();
    m.layers[0].tiling = 0.0f;
    CHECK(TerrainMaterialInvalid(m) != nullptr);
    m = TerrainMaterial();
    m.layers[0].roughness = 1.5f;
    CHECK(TerrainMaterialInvalid(m) != nullptr);
    m = TerrainMaterial();
    m.slope_layer = 1;  // only one layer in use
    CHECK(TerrainMaterialInvalid(m) != nullptr);
    m = TerrainMaterial();
    m.slope_start = 0.5f;
    m.slope_end = 0.5f;
    CHECK(TerrainMaterialInvalid(m) != nullptr);
    m = TerrainMaterial();
    m.fade_start = 300.0f;
    m.fade_end = 100.0f;
    CHECK(TerrainMaterialInvalid(m) != nullptr);
}
