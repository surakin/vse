// Every minted DXIL module (Phase 13 Day 163): its container passes ShaderDesc validation, and it was minted beside the
// SPIR-V that is committed now. A shader changed and re-minted on a machine without dxc keeps its old DXIL; the FNV-1a
// hash that shader_mint recorded beside the DXIL then differs from the hash of the committed SPIR-V, and this test
// names the shader. A new shader is added to the list below; a permuted shader whose module count changes fails the
// static_asserts.

#include <catch2/catch_test_macros.hpp>

#include "triangle.vert.dxil.inc"
#include "triangle.frag.dxil.inc"
#include "fullscreen.vert.dxil.inc"
#include "bindings_test.frag.dxil.inc"
#include "fill.comp.dxil.inc"
#include "saxpy.comp.dxil.inc"
#include "gradient.comp.dxil.inc"
#include "cube.vert.dxil.inc"
#include "cube.frag.dxil.inc"
#include "textured.vert.dxil.inc"
#include "textured.frag.dxil.inc"
#include "lit.vert.dxil.inc"
#include "lit.frag.dxil.inc"
#include "permute_test.frag.perm.inc"
#include "material_test.vert.dxil.inc"
#include "material_test.frag.perm.inc"
#include "graph_sample.frag.dxil.inc"
#include "gbuffer.vert.perm.inc"
#include "gbuffer.frag.perm.inc"
#include "gbuffer_debug.frag.dxil.inc"
#include "lighting.frag.dxil.inc"
#include "point_light.vert.dxil.inc"
#include "point_light.frag.dxil.inc"
#include "tonemap.frag.dxil.inc"
#include "text.vert.dxil.inc"
#include "text.frag.dxil.inc"
#include "ui.vert.dxil.inc"
#include "ui.frag.dxil.inc"
#include "rhi/desc.h"
#include "string_view.h"

using namespace engine_placeholder;

namespace
{

struct MintedModule
{
    const char* name;
    rhi::ShaderStage stage;
    const u32* words;
    usize word_count;
    const u8* dxil;
    usize dxil_size;
    u64 recorded_spirv_fnv;
};

u64 SpirvFnv(const u32* words, usize count)
{
    u64 hash = 0xcbf29ce484222325ull;
    for (usize i = 0; i < count; ++i)
    {
        for (u32 b = 0; b < 4; ++b)
        {
            hash ^= static_cast<u64>((words[i] >> (8 * b)) & 0xffu);
            hash *= 0x100000001b3ull;
        }
    }
    return hash;
}

static_assert(sizeof(shaders::kPermuteTestFragModules) / sizeof(shaders::kPermuteTestFragModules[0]) == 6,
              "permute_test.frag has another module count: update this list");
static_assert(sizeof(shaders::kMaterialTestFragModules) / sizeof(shaders::kMaterialTestFragModules[0]) == 2,
              "material_test.frag has another module count: update this list");
static_assert(sizeof(shaders::kGbufferVertModules) / sizeof(shaders::kGbufferVertModules[0]) == 4,
              "gbuffer.vert has another module count: update this list");
static_assert(sizeof(shaders::kGbufferFragModules) / sizeof(shaders::kGbufferFragModules[0]) == 8,
              "gbuffer.frag has another module count: update this list");

const MintedModule kModules[] = {
    {"triangle.vert", rhi::ShaderStage::Vertex, shaders::kTriangleVertSpv, shaders::kTriangleVertSpvWordCount,
     shaders::kTriangleVertDxil, shaders::kTriangleVertDxilByteCount, shaders::kTriangleVertDxilSpirvFnv},
    {"triangle.frag", rhi::ShaderStage::Fragment, shaders::kTriangleFragSpv, shaders::kTriangleFragSpvWordCount,
     shaders::kTriangleFragDxil, shaders::kTriangleFragDxilByteCount, shaders::kTriangleFragDxilSpirvFnv},
    {"fullscreen.vert", rhi::ShaderStage::Vertex, shaders::kFullscreenVertSpv, shaders::kFullscreenVertSpvWordCount,
     shaders::kFullscreenVertDxil, shaders::kFullscreenVertDxilByteCount, shaders::kFullscreenVertDxilSpirvFnv},
    {"bindings_test.frag", rhi::ShaderStage::Fragment, shaders::kBindingsTestFragSpv,
     shaders::kBindingsTestFragSpvWordCount, shaders::kBindingsTestFragDxil, shaders::kBindingsTestFragDxilByteCount,
     shaders::kBindingsTestFragDxilSpirvFnv},
    {"fill.comp", rhi::ShaderStage::Compute, shaders::kFillCompSpv, shaders::kFillCompSpvWordCount,
     shaders::kFillCompDxil, shaders::kFillCompDxilByteCount, shaders::kFillCompDxilSpirvFnv},
    {"saxpy.comp", rhi::ShaderStage::Compute, shaders::kSaxpyCompSpv, shaders::kSaxpyCompSpvWordCount,
     shaders::kSaxpyCompDxil, shaders::kSaxpyCompDxilByteCount, shaders::kSaxpyCompDxilSpirvFnv},
    {"gradient.comp", rhi::ShaderStage::Compute, shaders::kGradientCompSpv, shaders::kGradientCompSpvWordCount,
     shaders::kGradientCompDxil, shaders::kGradientCompDxilByteCount, shaders::kGradientCompDxilSpirvFnv},
    {"cube.vert", rhi::ShaderStage::Vertex, shaders::kCubeVertSpv, shaders::kCubeVertSpvWordCount,
     shaders::kCubeVertDxil, shaders::kCubeVertDxilByteCount, shaders::kCubeVertDxilSpirvFnv},
    {"cube.frag", rhi::ShaderStage::Fragment, shaders::kCubeFragSpv, shaders::kCubeFragSpvWordCount,
     shaders::kCubeFragDxil, shaders::kCubeFragDxilByteCount, shaders::kCubeFragDxilSpirvFnv},
    {"textured.vert", rhi::ShaderStage::Vertex, shaders::kTexturedVertSpv, shaders::kTexturedVertSpvWordCount,
     shaders::kTexturedVertDxil, shaders::kTexturedVertDxilByteCount, shaders::kTexturedVertDxilSpirvFnv},
    {"textured.frag", rhi::ShaderStage::Fragment, shaders::kTexturedFragSpv, shaders::kTexturedFragSpvWordCount,
     shaders::kTexturedFragDxil, shaders::kTexturedFragDxilByteCount, shaders::kTexturedFragDxilSpirvFnv},
    {"lit.vert", rhi::ShaderStage::Vertex, shaders::kLitVertSpv, shaders::kLitVertSpvWordCount, shaders::kLitVertDxil,
     shaders::kLitVertDxilByteCount, shaders::kLitVertDxilSpirvFnv},
    {"lit.frag", rhi::ShaderStage::Fragment, shaders::kLitFragSpv, shaders::kLitFragSpvWordCount, shaders::kLitFragDxil,
     shaders::kLitFragDxilByteCount, shaders::kLitFragDxilSpirvFnv},
    {"permute_test.frag", rhi::ShaderStage::Fragment, shaders::kPermuteTestFragSpv,
     shaders::kPermuteTestFragSpvWordCount, shaders::kPermuteTestFragDxil, shaders::kPermuteTestFragDxilByteCount,
     shaders::kPermuteTestFragDxilSpirvFnv},
    {"permute_test.frag module 1", rhi::ShaderStage::Fragment, shaders::kPermuteTestFragModules[1].words,
     shaders::kPermuteTestFragModules[1].word_count, shaders::kPermuteTestFragModules[1].dxil,
     shaders::kPermuteTestFragModules[1].dxil_size, shaders::kPermuteTestFragPerm1DxilSpirvFnv},
    {"permute_test.frag module 2", rhi::ShaderStage::Fragment, shaders::kPermuteTestFragModules[2].words,
     shaders::kPermuteTestFragModules[2].word_count, shaders::kPermuteTestFragModules[2].dxil,
     shaders::kPermuteTestFragModules[2].dxil_size, shaders::kPermuteTestFragPerm2DxilSpirvFnv},
    {"permute_test.frag module 3", rhi::ShaderStage::Fragment, shaders::kPermuteTestFragModules[3].words,
     shaders::kPermuteTestFragModules[3].word_count, shaders::kPermuteTestFragModules[3].dxil,
     shaders::kPermuteTestFragModules[3].dxil_size, shaders::kPermuteTestFragPerm3DxilSpirvFnv},
    {"permute_test.frag module 4", rhi::ShaderStage::Fragment, shaders::kPermuteTestFragModules[4].words,
     shaders::kPermuteTestFragModules[4].word_count, shaders::kPermuteTestFragModules[4].dxil,
     shaders::kPermuteTestFragModules[4].dxil_size, shaders::kPermuteTestFragPerm4DxilSpirvFnv},
    {"permute_test.frag module 5", rhi::ShaderStage::Fragment, shaders::kPermuteTestFragModules[5].words,
     shaders::kPermuteTestFragModules[5].word_count, shaders::kPermuteTestFragModules[5].dxil,
     shaders::kPermuteTestFragModules[5].dxil_size, shaders::kPermuteTestFragPerm5DxilSpirvFnv},
    {"material_test.vert", rhi::ShaderStage::Vertex, shaders::kMaterialTestVertSpv,
     shaders::kMaterialTestVertSpvWordCount, shaders::kMaterialTestVertDxil, shaders::kMaterialTestVertDxilByteCount,
     shaders::kMaterialTestVertDxilSpirvFnv},
    {"material_test.frag", rhi::ShaderStage::Fragment, shaders::kMaterialTestFragSpv,
     shaders::kMaterialTestFragSpvWordCount, shaders::kMaterialTestFragDxil, shaders::kMaterialTestFragDxilByteCount,
     shaders::kMaterialTestFragDxilSpirvFnv},
    {"material_test.frag module 1", rhi::ShaderStage::Fragment, shaders::kMaterialTestFragModules[1].words,
     shaders::kMaterialTestFragModules[1].word_count, shaders::kMaterialTestFragModules[1].dxil,
     shaders::kMaterialTestFragModules[1].dxil_size, shaders::kMaterialTestFragPerm1DxilSpirvFnv},
    {"graph_sample.frag", rhi::ShaderStage::Fragment, shaders::kGraphSampleFragSpv,
     shaders::kGraphSampleFragSpvWordCount, shaders::kGraphSampleFragDxil, shaders::kGraphSampleFragDxilByteCount,
     shaders::kGraphSampleFragDxilSpirvFnv},
    {"gbuffer.vert", rhi::ShaderStage::Vertex, shaders::kGbufferVertSpv, shaders::kGbufferVertSpvWordCount,
     shaders::kGbufferVertDxil, shaders::kGbufferVertDxilByteCount, shaders::kGbufferVertDxilSpirvFnv},
    {"gbuffer.vert module 1", rhi::ShaderStage::Vertex, shaders::kGbufferVertModules[1].words,
     shaders::kGbufferVertModules[1].word_count, shaders::kGbufferVertModules[1].dxil,
     shaders::kGbufferVertModules[1].dxil_size, shaders::kGbufferVertPerm1DxilSpirvFnv},
    {"gbuffer.vert module 2", rhi::ShaderStage::Vertex, shaders::kGbufferVertModules[2].words,
     shaders::kGbufferVertModules[2].word_count, shaders::kGbufferVertModules[2].dxil,
     shaders::kGbufferVertModules[2].dxil_size, shaders::kGbufferVertPerm2DxilSpirvFnv},
    {"gbuffer.vert module 3", rhi::ShaderStage::Vertex, shaders::kGbufferVertModules[3].words,
     shaders::kGbufferVertModules[3].word_count, shaders::kGbufferVertModules[3].dxil,
     shaders::kGbufferVertModules[3].dxil_size, shaders::kGbufferVertPerm3DxilSpirvFnv},
    {"gbuffer.frag", rhi::ShaderStage::Fragment, shaders::kGbufferFragSpv, shaders::kGbufferFragSpvWordCount,
     shaders::kGbufferFragDxil, shaders::kGbufferFragDxilByteCount, shaders::kGbufferFragDxilSpirvFnv},
    {"gbuffer.frag module 1", rhi::ShaderStage::Fragment, shaders::kGbufferFragModules[1].words,
     shaders::kGbufferFragModules[1].word_count, shaders::kGbufferFragModules[1].dxil,
     shaders::kGbufferFragModules[1].dxil_size, shaders::kGbufferFragPerm1DxilSpirvFnv},
    {"gbuffer.frag module 2", rhi::ShaderStage::Fragment, shaders::kGbufferFragModules[2].words,
     shaders::kGbufferFragModules[2].word_count, shaders::kGbufferFragModules[2].dxil,
     shaders::kGbufferFragModules[2].dxil_size, shaders::kGbufferFragPerm2DxilSpirvFnv},
    {"gbuffer.frag module 3", rhi::ShaderStage::Fragment, shaders::kGbufferFragModules[3].words,
     shaders::kGbufferFragModules[3].word_count, shaders::kGbufferFragModules[3].dxil,
     shaders::kGbufferFragModules[3].dxil_size, shaders::kGbufferFragPerm3DxilSpirvFnv},
    {"gbuffer.frag module 4", rhi::ShaderStage::Fragment, shaders::kGbufferFragModules[4].words,
     shaders::kGbufferFragModules[4].word_count, shaders::kGbufferFragModules[4].dxil,
     shaders::kGbufferFragModules[4].dxil_size, shaders::kGbufferFragPerm4DxilSpirvFnv},
    {"gbuffer.frag module 5", rhi::ShaderStage::Fragment, shaders::kGbufferFragModules[5].words,
     shaders::kGbufferFragModules[5].word_count, shaders::kGbufferFragModules[5].dxil,
     shaders::kGbufferFragModules[5].dxil_size, shaders::kGbufferFragPerm5DxilSpirvFnv},
    {"gbuffer.frag module 6", rhi::ShaderStage::Fragment, shaders::kGbufferFragModules[6].words,
     shaders::kGbufferFragModules[6].word_count, shaders::kGbufferFragModules[6].dxil,
     shaders::kGbufferFragModules[6].dxil_size, shaders::kGbufferFragPerm6DxilSpirvFnv},
    {"gbuffer.frag module 7", rhi::ShaderStage::Fragment, shaders::kGbufferFragModules[7].words,
     shaders::kGbufferFragModules[7].word_count, shaders::kGbufferFragModules[7].dxil,
     shaders::kGbufferFragModules[7].dxil_size, shaders::kGbufferFragPerm7DxilSpirvFnv},
    {"gbuffer_debug.frag", rhi::ShaderStage::Fragment, shaders::kGbufferDebugFragSpv,
     shaders::kGbufferDebugFragSpvWordCount, shaders::kGbufferDebugFragDxil, shaders::kGbufferDebugFragDxilByteCount,
     shaders::kGbufferDebugFragDxilSpirvFnv},
    {"lighting.frag", rhi::ShaderStage::Fragment, shaders::kLightingFragSpv, shaders::kLightingFragSpvWordCount,
     shaders::kLightingFragDxil, shaders::kLightingFragDxilByteCount, shaders::kLightingFragDxilSpirvFnv},
    {"point_light.vert", rhi::ShaderStage::Vertex, shaders::kPointLightVertSpv, shaders::kPointLightVertSpvWordCount,
     shaders::kPointLightVertDxil, shaders::kPointLightVertDxilByteCount, shaders::kPointLightVertDxilSpirvFnv},
    {"point_light.frag", rhi::ShaderStage::Fragment, shaders::kPointLightFragSpv, shaders::kPointLightFragSpvWordCount,
     shaders::kPointLightFragDxil, shaders::kPointLightFragDxilByteCount, shaders::kPointLightFragDxilSpirvFnv},
    {"tonemap.frag", rhi::ShaderStage::Fragment, shaders::kTonemapFragSpv, shaders::kTonemapFragSpvWordCount,
     shaders::kTonemapFragDxil, shaders::kTonemapFragDxilByteCount, shaders::kTonemapFragDxilSpirvFnv},
    {"text.vert", rhi::ShaderStage::Vertex, shaders::kTextVertSpv, shaders::kTextVertSpvWordCount,
     shaders::kTextVertDxil, shaders::kTextVertDxilByteCount, shaders::kTextVertDxilSpirvFnv},
    {"text.frag", rhi::ShaderStage::Fragment, shaders::kTextFragSpv, shaders::kTextFragSpvWordCount,
     shaders::kTextFragDxil, shaders::kTextFragDxilByteCount, shaders::kTextFragDxilSpirvFnv},
    {"ui.vert", rhi::ShaderStage::Vertex, shaders::kUiVertSpv, shaders::kUiVertSpvWordCount, shaders::kUiVertDxil,
     shaders::kUiVertDxilByteCount, shaders::kUiVertDxilSpirvFnv},
    {"ui.frag", rhi::ShaderStage::Fragment, shaders::kUiFragSpv, shaders::kUiFragSpvWordCount, shaders::kUiFragDxil,
     shaders::kUiFragDxilByteCount, shaders::kUiFragDxilSpirvFnv},
};

}  // namespace

TEST_CASE("Every minted DXIL module is a valid container minted beside the committed SPIR-V",
          "[renderer][shaders][dxil]")
{
    for (const MintedModule& m : kModules)
    {
        INFO(m.name);
        CHECK(SpirvFnv(m.words, m.word_count) ==
              m.recorded_spirv_fnv);  // else: re-mint on a machine with dxc (shaders/README.md)
        rhi::ShaderDesc d;
        d.stage = m.stage;
        d.SetCode(rhi::DxilBlob(m.dxil, m.dxil_size));
        CHECK(d.Validate() == nullptr);
        bool signed_container = false;
        for (usize i = 4; i < 20 && m.dxil_size >= 20; ++i)
        {
            signed_container = signed_container || m.dxil[i] != 0;
        }
        CHECK(signed_container);  // the validator wrote its digest
    }
}

TEST_CASE("A minted blob set names its shader and holds both formats", "[renderer][shaders][dxil]")
{
    const rhi::ShaderBlobSet& set = shaders::kLightingFragShader;
    CHECK(StringView(set.name) == "lighting.frag");
    CHECK(set.For(rhi::ShaderFormat::Spirv).size == shaders::kLightingFragSpvWordCount * sizeof(u32));
    CHECK(set.For(rhi::ShaderFormat::Dxil).size == shaders::kLightingFragDxilByteCount);
    CHECK(shaders::kGbufferFragModules[0].Blob(rhi::ShaderFormat::Dxil).bytes == shaders::kGbufferFragDxil);
}
