// Every minted Metal library (Phase 26 Day 315): it passes ShaderDesc validation (MTLB, an executable library, the size
// its header records), it names the function it exports (main_0, Slang's rename of main), and it was minted beside the
// SPIR-V that is committed now. A shader changed and re-minted on a machine without a Mac keeps its old library; the
// FNV-1a hash shader_mint recorded beside it then differs from the hash of the committed SPIR-V, and this test names
// the shader. A new shader is added to the list below; a permuted shader whose module count changes fails the
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

struct MintedLibrary
{
    const char* name;
    rhi::ShaderStage stage;
    const u32* words;
    usize word_count;
    const u8* metallib;
    usize metallib_size;
    const char* entry;
    u64 recorded_spirv_fnv;
};

u64 CommittedSpirvFnv(const u32* words, usize count)
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
static_assert(sizeof(shaders::kGbufferVertModules) / sizeof(shaders::kGbufferVertModules[0]) == 2,
              "gbuffer.vert has another module count: update this list");
static_assert(sizeof(shaders::kGbufferFragModules) / sizeof(shaders::kGbufferFragModules[0]) == 8,
              "gbuffer.frag has another module count: update this list");

const MintedLibrary kLibraries[] = {
    {"triangle.vert", rhi::ShaderStage::Vertex, shaders::kTriangleVertSpv, shaders::kTriangleVertSpvWordCount,
     shaders::kTriangleVertMetallib, shaders::kTriangleVertMetallibByteCount, shaders::kTriangleVertMetallibEntry,
     shaders::kTriangleVertMetallibSpirvFnv},
    {"triangle.frag", rhi::ShaderStage::Fragment, shaders::kTriangleFragSpv, shaders::kTriangleFragSpvWordCount,
     shaders::kTriangleFragMetallib, shaders::kTriangleFragMetallibByteCount, shaders::kTriangleFragMetallibEntry,
     shaders::kTriangleFragMetallibSpirvFnv},
    {"fullscreen.vert", rhi::ShaderStage::Vertex, shaders::kFullscreenVertSpv, shaders::kFullscreenVertSpvWordCount,
     shaders::kFullscreenVertMetallib, shaders::kFullscreenVertMetallibByteCount, shaders::kFullscreenVertMetallibEntry,
     shaders::kFullscreenVertMetallibSpirvFnv},
    {"bindings_test.frag", rhi::ShaderStage::Fragment, shaders::kBindingsTestFragSpv,
     shaders::kBindingsTestFragSpvWordCount, shaders::kBindingsTestFragMetallib,
     shaders::kBindingsTestFragMetallibByteCount, shaders::kBindingsTestFragMetallibEntry,
     shaders::kBindingsTestFragMetallibSpirvFnv},
    {"fill.comp", rhi::ShaderStage::Compute, shaders::kFillCompSpv, shaders::kFillCompSpvWordCount,
     shaders::kFillCompMetallib, shaders::kFillCompMetallibByteCount, shaders::kFillCompMetallibEntry,
     shaders::kFillCompMetallibSpirvFnv},
    {"saxpy.comp", rhi::ShaderStage::Compute, shaders::kSaxpyCompSpv, shaders::kSaxpyCompSpvWordCount,
     shaders::kSaxpyCompMetallib, shaders::kSaxpyCompMetallibByteCount, shaders::kSaxpyCompMetallibEntry,
     shaders::kSaxpyCompMetallibSpirvFnv},
    {"gradient.comp", rhi::ShaderStage::Compute, shaders::kGradientCompSpv, shaders::kGradientCompSpvWordCount,
     shaders::kGradientCompMetallib, shaders::kGradientCompMetallibByteCount, shaders::kGradientCompMetallibEntry,
     shaders::kGradientCompMetallibSpirvFnv},
    {"cube.vert", rhi::ShaderStage::Vertex, shaders::kCubeVertSpv, shaders::kCubeVertSpvWordCount,
     shaders::kCubeVertMetallib, shaders::kCubeVertMetallibByteCount, shaders::kCubeVertMetallibEntry,
     shaders::kCubeVertMetallibSpirvFnv},
    {"cube.frag", rhi::ShaderStage::Fragment, shaders::kCubeFragSpv, shaders::kCubeFragSpvWordCount,
     shaders::kCubeFragMetallib, shaders::kCubeFragMetallibByteCount, shaders::kCubeFragMetallibEntry,
     shaders::kCubeFragMetallibSpirvFnv},
    {"textured.vert", rhi::ShaderStage::Vertex, shaders::kTexturedVertSpv, shaders::kTexturedVertSpvWordCount,
     shaders::kTexturedVertMetallib, shaders::kTexturedVertMetallibByteCount, shaders::kTexturedVertMetallibEntry,
     shaders::kTexturedVertMetallibSpirvFnv},
    {"textured.frag", rhi::ShaderStage::Fragment, shaders::kTexturedFragSpv, shaders::kTexturedFragSpvWordCount,
     shaders::kTexturedFragMetallib, shaders::kTexturedFragMetallibByteCount, shaders::kTexturedFragMetallibEntry,
     shaders::kTexturedFragMetallibSpirvFnv},
    {"lit.vert", rhi::ShaderStage::Vertex, shaders::kLitVertSpv, shaders::kLitVertSpvWordCount,
     shaders::kLitVertMetallib, shaders::kLitVertMetallibByteCount, shaders::kLitVertMetallibEntry,
     shaders::kLitVertMetallibSpirvFnv},
    {"lit.frag", rhi::ShaderStage::Fragment, shaders::kLitFragSpv, shaders::kLitFragSpvWordCount,
     shaders::kLitFragMetallib, shaders::kLitFragMetallibByteCount, shaders::kLitFragMetallibEntry,
     shaders::kLitFragMetallibSpirvFnv},
    {"permute_test.frag", rhi::ShaderStage::Fragment, shaders::kPermuteTestFragSpv,
     shaders::kPermuteTestFragSpvWordCount, shaders::kPermuteTestFragMetallib,
     shaders::kPermuteTestFragMetallibByteCount, shaders::kPermuteTestFragMetallibEntry,
     shaders::kPermuteTestFragMetallibSpirvFnv},
    {"permute_test.frag module 1", rhi::ShaderStage::Fragment, shaders::kPermuteTestFragModules[1].words,
     shaders::kPermuteTestFragModules[1].word_count, shaders::kPermuteTestFragModules[1].metallib,
     shaders::kPermuteTestFragModules[1].metallib_size, shaders::kPermuteTestFragModules[1].metallib_entry,
     shaders::kPermuteTestFragPerm1MetallibSpirvFnv},
    {"permute_test.frag module 2", rhi::ShaderStage::Fragment, shaders::kPermuteTestFragModules[2].words,
     shaders::kPermuteTestFragModules[2].word_count, shaders::kPermuteTestFragModules[2].metallib,
     shaders::kPermuteTestFragModules[2].metallib_size, shaders::kPermuteTestFragModules[2].metallib_entry,
     shaders::kPermuteTestFragPerm2MetallibSpirvFnv},
    {"permute_test.frag module 3", rhi::ShaderStage::Fragment, shaders::kPermuteTestFragModules[3].words,
     shaders::kPermuteTestFragModules[3].word_count, shaders::kPermuteTestFragModules[3].metallib,
     shaders::kPermuteTestFragModules[3].metallib_size, shaders::kPermuteTestFragModules[3].metallib_entry,
     shaders::kPermuteTestFragPerm3MetallibSpirvFnv},
    {"permute_test.frag module 4", rhi::ShaderStage::Fragment, shaders::kPermuteTestFragModules[4].words,
     shaders::kPermuteTestFragModules[4].word_count, shaders::kPermuteTestFragModules[4].metallib,
     shaders::kPermuteTestFragModules[4].metallib_size, shaders::kPermuteTestFragModules[4].metallib_entry,
     shaders::kPermuteTestFragPerm4MetallibSpirvFnv},
    {"permute_test.frag module 5", rhi::ShaderStage::Fragment, shaders::kPermuteTestFragModules[5].words,
     shaders::kPermuteTestFragModules[5].word_count, shaders::kPermuteTestFragModules[5].metallib,
     shaders::kPermuteTestFragModules[5].metallib_size, shaders::kPermuteTestFragModules[5].metallib_entry,
     shaders::kPermuteTestFragPerm5MetallibSpirvFnv},
    {"material_test.vert", rhi::ShaderStage::Vertex, shaders::kMaterialTestVertSpv,
     shaders::kMaterialTestVertSpvWordCount, shaders::kMaterialTestVertMetallib,
     shaders::kMaterialTestVertMetallibByteCount, shaders::kMaterialTestVertMetallibEntry,
     shaders::kMaterialTestVertMetallibSpirvFnv},
    {"material_test.frag", rhi::ShaderStage::Fragment, shaders::kMaterialTestFragSpv,
     shaders::kMaterialTestFragSpvWordCount, shaders::kMaterialTestFragMetallib,
     shaders::kMaterialTestFragMetallibByteCount, shaders::kMaterialTestFragMetallibEntry,
     shaders::kMaterialTestFragMetallibSpirvFnv},
    {"material_test.frag module 1", rhi::ShaderStage::Fragment, shaders::kMaterialTestFragModules[1].words,
     shaders::kMaterialTestFragModules[1].word_count, shaders::kMaterialTestFragModules[1].metallib,
     shaders::kMaterialTestFragModules[1].metallib_size, shaders::kMaterialTestFragModules[1].metallib_entry,
     shaders::kMaterialTestFragPerm1MetallibSpirvFnv},
    {"graph_sample.frag", rhi::ShaderStage::Fragment, shaders::kGraphSampleFragSpv,
     shaders::kGraphSampleFragSpvWordCount, shaders::kGraphSampleFragMetallib,
     shaders::kGraphSampleFragMetallibByteCount, shaders::kGraphSampleFragMetallibEntry,
     shaders::kGraphSampleFragMetallibSpirvFnv},
    {"gbuffer.vert", rhi::ShaderStage::Vertex, shaders::kGbufferVertSpv, shaders::kGbufferVertSpvWordCount,
     shaders::kGbufferVertMetallib, shaders::kGbufferVertMetallibByteCount, shaders::kGbufferVertMetallibEntry,
     shaders::kGbufferVertMetallibSpirvFnv},
    {"gbuffer.vert module 1", rhi::ShaderStage::Vertex, shaders::kGbufferVertModules[1].words,
     shaders::kGbufferVertModules[1].word_count, shaders::kGbufferVertModules[1].metallib,
     shaders::kGbufferVertModules[1].metallib_size, shaders::kGbufferVertModules[1].metallib_entry,
     shaders::kGbufferVertPerm1MetallibSpirvFnv},
    {"gbuffer.frag", rhi::ShaderStage::Fragment, shaders::kGbufferFragSpv, shaders::kGbufferFragSpvWordCount,
     shaders::kGbufferFragMetallib, shaders::kGbufferFragMetallibByteCount, shaders::kGbufferFragMetallibEntry,
     shaders::kGbufferFragMetallibSpirvFnv},
    {"gbuffer.frag module 1", rhi::ShaderStage::Fragment, shaders::kGbufferFragModules[1].words,
     shaders::kGbufferFragModules[1].word_count, shaders::kGbufferFragModules[1].metallib,
     shaders::kGbufferFragModules[1].metallib_size, shaders::kGbufferFragModules[1].metallib_entry,
     shaders::kGbufferFragPerm1MetallibSpirvFnv},
    {"gbuffer.frag module 2", rhi::ShaderStage::Fragment, shaders::kGbufferFragModules[2].words,
     shaders::kGbufferFragModules[2].word_count, shaders::kGbufferFragModules[2].metallib,
     shaders::kGbufferFragModules[2].metallib_size, shaders::kGbufferFragModules[2].metallib_entry,
     shaders::kGbufferFragPerm2MetallibSpirvFnv},
    {"gbuffer.frag module 3", rhi::ShaderStage::Fragment, shaders::kGbufferFragModules[3].words,
     shaders::kGbufferFragModules[3].word_count, shaders::kGbufferFragModules[3].metallib,
     shaders::kGbufferFragModules[3].metallib_size, shaders::kGbufferFragModules[3].metallib_entry,
     shaders::kGbufferFragPerm3MetallibSpirvFnv},
    {"gbuffer.frag module 4", rhi::ShaderStage::Fragment, shaders::kGbufferFragModules[4].words,
     shaders::kGbufferFragModules[4].word_count, shaders::kGbufferFragModules[4].metallib,
     shaders::kGbufferFragModules[4].metallib_size, shaders::kGbufferFragModules[4].metallib_entry,
     shaders::kGbufferFragPerm4MetallibSpirvFnv},
    {"gbuffer.frag module 5", rhi::ShaderStage::Fragment, shaders::kGbufferFragModules[5].words,
     shaders::kGbufferFragModules[5].word_count, shaders::kGbufferFragModules[5].metallib,
     shaders::kGbufferFragModules[5].metallib_size, shaders::kGbufferFragModules[5].metallib_entry,
     shaders::kGbufferFragPerm5MetallibSpirvFnv},
    {"gbuffer.frag module 6", rhi::ShaderStage::Fragment, shaders::kGbufferFragModules[6].words,
     shaders::kGbufferFragModules[6].word_count, shaders::kGbufferFragModules[6].metallib,
     shaders::kGbufferFragModules[6].metallib_size, shaders::kGbufferFragModules[6].metallib_entry,
     shaders::kGbufferFragPerm6MetallibSpirvFnv},
    {"gbuffer.frag module 7", rhi::ShaderStage::Fragment, shaders::kGbufferFragModules[7].words,
     shaders::kGbufferFragModules[7].word_count, shaders::kGbufferFragModules[7].metallib,
     shaders::kGbufferFragModules[7].metallib_size, shaders::kGbufferFragModules[7].metallib_entry,
     shaders::kGbufferFragPerm7MetallibSpirvFnv},
    {"gbuffer_debug.frag", rhi::ShaderStage::Fragment, shaders::kGbufferDebugFragSpv,
     shaders::kGbufferDebugFragSpvWordCount, shaders::kGbufferDebugFragMetallib,
     shaders::kGbufferDebugFragMetallibByteCount, shaders::kGbufferDebugFragMetallibEntry,
     shaders::kGbufferDebugFragMetallibSpirvFnv},
    {"lighting.frag", rhi::ShaderStage::Fragment, shaders::kLightingFragSpv, shaders::kLightingFragSpvWordCount,
     shaders::kLightingFragMetallib, shaders::kLightingFragMetallibByteCount, shaders::kLightingFragMetallibEntry,
     shaders::kLightingFragMetallibSpirvFnv},
    {"point_light.vert", rhi::ShaderStage::Vertex, shaders::kPointLightVertSpv, shaders::kPointLightVertSpvWordCount,
     shaders::kPointLightVertMetallib, shaders::kPointLightVertMetallibByteCount, shaders::kPointLightVertMetallibEntry,
     shaders::kPointLightVertMetallibSpirvFnv},
    {"point_light.frag", rhi::ShaderStage::Fragment, shaders::kPointLightFragSpv, shaders::kPointLightFragSpvWordCount,
     shaders::kPointLightFragMetallib, shaders::kPointLightFragMetallibByteCount, shaders::kPointLightFragMetallibEntry,
     shaders::kPointLightFragMetallibSpirvFnv},
    {"tonemap.frag", rhi::ShaderStage::Fragment, shaders::kTonemapFragSpv, shaders::kTonemapFragSpvWordCount,
     shaders::kTonemapFragMetallib, shaders::kTonemapFragMetallibByteCount, shaders::kTonemapFragMetallibEntry,
     shaders::kTonemapFragMetallibSpirvFnv},
    {"text.vert", rhi::ShaderStage::Vertex, shaders::kTextVertSpv, shaders::kTextVertSpvWordCount,
     shaders::kTextVertMetallib, shaders::kTextVertMetallibByteCount, shaders::kTextVertMetallibEntry,
     shaders::kTextVertMetallibSpirvFnv},
    {"text.frag", rhi::ShaderStage::Fragment, shaders::kTextFragSpv, shaders::kTextFragSpvWordCount,
     shaders::kTextFragMetallib, shaders::kTextFragMetallibByteCount, shaders::kTextFragMetallibEntry,
     shaders::kTextFragMetallibSpirvFnv},
    {"ui.vert", rhi::ShaderStage::Vertex, shaders::kUiVertSpv, shaders::kUiVertSpvWordCount, shaders::kUiVertMetallib,
     shaders::kUiVertMetallibByteCount, shaders::kUiVertMetallibEntry, shaders::kUiVertMetallibSpirvFnv},
    {"ui.frag", rhi::ShaderStage::Fragment, shaders::kUiFragSpv, shaders::kUiFragSpvWordCount, shaders::kUiFragMetallib,
     shaders::kUiFragMetallibByteCount, shaders::kUiFragMetallibEntry, shaders::kUiFragMetallibSpirvFnv},
};

}  // namespace

TEST_CASE("Every minted Metal library is an executable library minted beside the committed SPIR-V, naming its entry",
          "[renderer][shaders][metal]")
{
    for (const MintedLibrary& m : kLibraries)
    {
        INFO(m.name);
        CHECK(CommittedSpirvFnv(m.words, m.word_count) ==
              m.recorded_spirv_fnv);  // else: re-mint on a Mac (shaders/README.md)
        CHECK(StringView(m.entry) == "main_0");
        rhi::ShaderDesc d;
        d.stage = m.stage;
        d.SetCode(rhi::MetalLibBlob(m.metallib, m.metallib_size, m.entry));
        CHECK(d.Validate() == nullptr);
        CHECK(StringView(d.entry) == "main_0");
    }
}

TEST_CASE("A minted blob set holds the Metal library with its entry", "[renderer][shaders][metal]")
{
    const rhi::ShaderBlob metal = shaders::kLightingFragShader.For(rhi::ShaderFormat::MetalLib);
    CHECK(metal.size == shaders::kLightingFragMetallibByteCount);
    CHECK(StringView(metal.entry) == "main_0");
    CHECK(shaders::kGbufferFragModules[0].Blob(rhi::ShaderFormat::MetalLib).bytes == shaders::kGbufferFragMetallib);
    CHECK(StringView(shaders::kGbufferFragModules[0].Blob(rhi::ShaderFormat::MetalLib).entry) == "main_0");
}
