// Phase 5 Day 70: the committed Slang shaders must present the interface the RHI and the C++ structs expect: entry name
// `main`, the right stage, compute local sizes, descriptor (set, binding) pairs, interface locations, std140 member
// offsets, and no capability beyond what every Vulkan device guarantees. Runs without the shader compiler.
#include <catch2/catch_test_macros.hpp>

#include "rhi/enums.h"
#include "rhi/spirv.h"
#include "test_text.h"
#include "types.h"

#include "bindings_test.frag.spv.inc"
#include "fill.comp.spv.inc"
#include "fullscreen.vert.spv.inc"
#include "gradient.comp.spv.inc"
#include "saxpy.comp.spv.inc"
#include "triangle.frag.spv.inc"
#include "triangle.vert.spv.inc"
#include "cube.vert.spv.inc"
#include "cube.frag.spv.inc"
#include "textured.vert.spv.inc"
#include "textured.frag.spv.inc"
#include "lit.vert.spv.inc"
#include "lit.frag.spv.inc"

using namespace engine_placeholder;
using namespace engine_placeholder::rhi;

namespace
{

// SPIR-V opcodes and enumerants this test reads.
constexpr u32 kOpEntryPoint = 15;
constexpr u32 kOpExecutionMode = 16;
constexpr u32 kOpDecorate = 71;
constexpr u32 kOpMemberDecorate = 72;
constexpr u32 kOpCapability = 17;
constexpr u32 kOpTypeImage = 25;
constexpr u32 kOpVariable = 59;
constexpr u32 kDecorationNonWritable = 24;
constexpr u32 kDecorationNonReadable = 25;
constexpr u32 kDecorationOffset = 35;
constexpr u32 kCapabilityMatrix = 0;
constexpr u32 kCapabilityShader = 1;
constexpr u32 kCapabilityImageQuery = 50;
constexpr u32 kLocalSize = 17;
constexpr u32 kDecorationLocation = 30;
constexpr u32 kDecorationBinding = 33;
constexpr u32 kDecorationDescriptorSet = 34;

// A (set, binding) or (id, number) pair, ordered by first then second.
struct Pair
{
    u32 first;
    u32 second;
    bool operator==(const Pair& o) const { return first == o.first && second == o.second; }
    bool operator<(const Pair& o) const { return first < o.first || (first == o.first && second < o.second); }
};

template <typename T> using List = engine_test::ValueList<T>;

struct Interface
{
    List<u32> locations = {};  // sorted
    List<Pair> bindings = {};  // (set, binding), sorted
    u32 local_size[3] = {0, 0, 0};
    u32 entry_points = 0;
    List<u32> capabilities;      // sorted
    List<u32> offsets;           // every member Offset decoration (uniform and push-constant layouts), sorted
    List<u32> variable_classes;  // storage class of every OpVariable, sorted
    List<u32> image_formats;     // format operand of every OpTypeImage, sorted
    u32 non_readable = 0;
    u32 non_writable = 0;
};

Interface Reflect(const u32* words, usize count)
{
    Interface out;
    List<Pair> sets;   // (id, set)
    List<Pair> binds;  // (id, binding)
    for (usize i = 5; i < count;)
    {
        u32 op = words[i] & 0xffffu;
        u32 len = words[i] >> 16;
        if (op == kOpEntryPoint)
        {
            ++out.entry_points;
        }
        else if (op == kOpCapability)
        {
            out.capabilities.Push(words[i + 1]);
        }
        else if (op == kOpTypeImage)
        {
            out.image_formats.Push(words[i + 8]);
        }
        else if (op == kOpVariable)
        {
            out.variable_classes.Push(words[i + 3]);
        }
        else if (op == kOpMemberDecorate && words[i + 3] == kDecorationOffset)
        {
            out.offsets.Push(words[i + 4]);
        }
        else if (op == kOpExecutionMode && words[i + 2] == kLocalSize)
        {
            out.local_size[0] = words[i + 3];
            out.local_size[1] = words[i + 4];
            out.local_size[2] = words[i + 5];
        }
        else if (op == kOpDecorate)
        {
            if (words[i + 2] == kDecorationLocation)
            {
                out.locations.Push(words[i + 3]);
            }
            else if (words[i + 2] == kDecorationNonReadable)
            {
                ++out.non_readable;
            }
            else if (words[i + 2] == kDecorationNonWritable)
            {
                ++out.non_writable;
            }
            else if (words[i + 2] == kDecorationDescriptorSet)
            {
                sets.Push(Pair{words[i + 1], words[i + 3]});
            }
            else if (words[i + 2] == kDecorationBinding)
            {
                binds.Push(Pair{words[i + 1], words[i + 3]});
            }
        }
        i += len;
    }
    for (usize b = 0; b < binds.Size(); ++b)
    {
        for (usize s = 0; s < sets.Size(); ++s)
        {
            if (sets[s].first == binds[b].first)
            {
                out.bindings.Push(Pair{sets[s].second, binds[b].second});
            }
        }
    }
    out.locations.Sort();
    out.bindings.Sort();
    out.capabilities.Sort();
    out.offsets.Sort();
    out.variable_classes.Sort();
    out.image_formats.Sort();
    return out;
}

using namespace engine_placeholder::shaders;

struct Expect
{
    List<Pair> bindings = {};  // (set, binding)
    List<u32> locations = {};  // sorted
    List<u32> offsets = {};    // std140 / std430 member offsets, sorted
    u32 local_size[3] = {0, 0, 0};
    u32 non_readable = 0;
    u32 non_writable = 0;
    List<u32> image_formats = {};  // sorted; 0 = Unknown (sampled images), 4 = Rgba8
    u32 uniform_blocks = 0;
    u32 storage_buffers = 0;
    u32 push_constant_blocks = 0;
};

void Check(const u32* words, usize count, ShaderStage stage, const Expect& want)
{
    CHECK(ValidateSpirv(words, count, stage, "main") == nullptr);
    Interface got = Reflect(words, count);
    CHECK(got.entry_points == 1);
    CHECK(got.bindings == want.bindings);
    CHECK(got.locations == want.locations);
    CHECK(got.offsets == want.offsets);
    CHECK(got.local_size[0] == want.local_size[0]);
    CHECK(got.local_size[1] == want.local_size[1]);
    CHECK(got.local_size[2] == want.local_size[2]);
    CHECK(got.non_readable == want.non_readable);
    CHECK(got.non_writable == want.non_writable);
    CHECK(got.image_formats == want.image_formats);
    CHECK(got.variable_classes.Count(2u) == want.uniform_blocks);
    CHECK(got.variable_classes.Count(12u) == want.storage_buffers);
    CHECK(got.variable_classes.Count(9u) == want.push_constant_blocks);
    // No capability beyond what every Vulkan device guarantees: in particular not DrawParameters (SV_VertexID).
    for (usize c = 0; c < got.capabilities.Size(); ++c)
    {
        const u32 capability = got.capabilities[c];
        CHECK((capability == kCapabilityMatrix || capability == kCapabilityShader ||
               capability == kCapabilityImageQuery));
    }
}

}  // namespace

TEST_CASE("The committed shaders present the expected interface", "[rhi][spirv][shaders]")
{
    SECTION("fullscreen.vert")
    {
        Check(kFullscreenVertSpv, kFullscreenVertSpvWordCount, ShaderStage::Vertex, {.locations = {0}});
    }
    SECTION("triangle.vert")
    {
        Check(kTriangleVertSpv, kTriangleVertSpvWordCount, ShaderStage::Vertex, {.locations = {0, 0, 1}});
    }
    SECTION("triangle.frag")
    {
        Check(kTriangleFragSpv, kTriangleFragSpvWordCount, ShaderStage::Fragment, {.locations = {0, 0}});
    }
    SECTION("bindings_test.frag")
    {
        // Params { vec4 scale } at offset 0 and Push { vec4 bias } at offset 0.
        Check(kBindingsTestFragSpv, kBindingsTestFragSpvWordCount, ShaderStage::Fragment,
              {.bindings = {{0, 0}, {0, 1}, {0, 2}},
               .locations = {0, 0},
               .offsets = {0, 0},
               .image_formats = {0},
               .uniform_blocks = 1,
               .push_constant_blocks = 1});
    }
    SECTION("fill.comp")
    {
        Check(kFillCompSpv, kFillCompSpvWordCount, ShaderStage::Compute,
              {.bindings = {{0, 0}},
               .offsets = {0, 0},
               .local_size = {64, 1, 1},
               .storage_buffers = 1,
               .push_constant_blocks = 1});
    }
    SECTION("saxpy.comp")
    {
        Check(kSaxpyCompSpv, kSaxpyCompSpvWordCount, ShaderStage::Compute,
              {.bindings = {{0, 0}, {0, 1}},
               .offsets = {0, 0, 0},
               .local_size = {64, 1, 1},
               .non_writable = 1,
               .storage_buffers = 2,
               .push_constant_blocks = 1});
    }
    SECTION("gradient.comp")
    {
        Check(kGradientCompSpv, kGradientCompSpvWordCount, ShaderStage::Compute,
              {.bindings = {{0, 0}}, .local_size = {8, 8, 1}, .non_readable = 1, .image_formats = {4}});
    }
    SECTION("cube.vert and textured.vert")
    {
        // Params { mat4 mvp } at offset 0 (the C++ side copies a Mat4 into it).
        Expect want{.bindings = {{0, 0}}, .locations = {0, 0, 1, 1, 2}, .offsets = {0}, .uniform_blocks = 1};
        Check(kCubeVertSpv, kCubeVertSpvWordCount, ShaderStage::Vertex, want);
        Check(kTexturedVertSpv, kTexturedVertSpvWordCount, ShaderStage::Vertex, want);
    }
    SECTION("cube.frag")
    {
        Check(kCubeFragSpv, kCubeFragSpvWordCount, ShaderStage::Fragment, {.locations = {0, 0}});
    }
    SECTION("textured.frag")
    {
        Check(kTexturedFragSpv, kTexturedFragSpvWordCount, ShaderStage::Fragment,
              {.bindings = {{0, 1}, {0, 2}}, .locations = {0, 0, 1}, .image_formats = {0}});
    }
    SECTION("lit.vert and lit.frag")
    {
        // Params: 2 x mat4 + 3 x vec4 = 176 bytes, every member at its std140 offset (matches LitUniforms).
        List<u32> params = {0, 64, 128, 144, 160};
        Check(kLitVertSpv, kLitVertSpvWordCount, ShaderStage::Vertex,
              {.bindings = {{0, 0}}, .locations = {0, 0, 1, 1, 2}, .offsets = params, .uniform_blocks = 1});
        Check(kLitFragSpv, kLitFragSpvWordCount, ShaderStage::Fragment,
              {.bindings = {{0, 0}, {0, 1}, {0, 2}},
               .locations = {0, 0, 1},
               .offsets = params,
               .image_formats = {0},
               .uniform_blocks = 1});
    }
}
