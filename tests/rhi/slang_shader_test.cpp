// Phase 4 Day 69: every shader ported to Slang must present the same interface to the RHI as the GLSL original
// (entry name, stage, compute local size, descriptor bindings and stage-interface locations), and pass ValidateSpirv.
// Both outputs are committed, so this runs without either compiler.
#include <algorithm>
#include <vector>

#include <catch2/catch_test_macros.hpp>

#include "rhi/enums.h"
#include "rhi/spirv.h"
#include "types.h"

namespace engine_placeholder::glsl_ref {
#include "shaders/bindings_test.frag.spv.inc"
#include "shaders/fill.comp.spv.inc"
#include "shaders/fullscreen.vert.spv.inc"
#include "shaders/gradient.comp.spv.inc"
#include "shaders/saxpy.comp.spv.inc"
#include "shaders/triangle.frag.spv.inc"
#include "shaders/triangle.vert.spv.inc"
} // namespace engine_placeholder::glsl_ref
namespace engine_placeholder::slang_out {
#include "shaders/slang/bindings_test.frag.spv.inc"
#include "shaders/slang/fill.comp.spv.inc"
#include "shaders/slang/fullscreen.vert.spv.inc"
#include "shaders/slang/gradient.comp.spv.inc"
#include "shaders/slang/saxpy.comp.spv.inc"
#include "shaders/slang/triangle.frag.spv.inc"
#include "shaders/slang/triangle.vert.spv.inc"
} // namespace engine_placeholder::slang_out

using namespace engine_placeholder;
using namespace engine_placeholder::rhi;

namespace {

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

struct Interface {
    std::vector<u32> locations;                 // sorted
    std::vector<std::pair<u32, u32>> bindings;  // (set, binding), sorted
    u32 local_size[3] = {0, 0, 0};
    u32 entry_points = 0;
    std::vector<u32> capabilities;      // sorted
    std::vector<u32> offsets;           // every member Offset decoration (uniform and push-constant layouts), sorted
    std::vector<u32> variable_classes;  // storage class of every OpVariable, sorted
    std::vector<u32> image_formats;     // format operand of every OpTypeImage, sorted
    u32 non_readable = 0;
    u32 non_writable = 0;
};

Interface Reflect(const u32* words, usize count) {
    Interface out;
    std::vector<std::pair<u32, u32>> sets;     // (id, set)
    std::vector<std::pair<u32, u32>> binds;    // (id, binding)
    for (usize i = 5; i < count;) {
        u32 op = words[i] & 0xffffu;
        u32 len = words[i] >> 16;
        if (op == kOpEntryPoint) {
            ++out.entry_points;
        } else if (op == kOpCapability) {
            out.capabilities.push_back(words[i + 1]);
        } else if (op == kOpTypeImage) {
            out.image_formats.push_back(words[i + 8]);
        } else if (op == kOpVariable) {
            out.variable_classes.push_back(words[i + 3]);
        } else if (op == kOpMemberDecorate && words[i + 3] == kDecorationOffset) {
            out.offsets.push_back(words[i + 4]);
        } else if (op == kOpExecutionMode && words[i + 2] == kLocalSize) {
            out.local_size[0] = words[i + 3];
            out.local_size[1] = words[i + 4];
            out.local_size[2] = words[i + 5];
        } else if (op == kOpDecorate) {
            if (words[i + 2] == kDecorationLocation) {
                out.locations.push_back(words[i + 3]);
            } else if (words[i + 2] == kDecorationNonReadable) {
                ++out.non_readable;
            } else if (words[i + 2] == kDecorationNonWritable) {
                ++out.non_writable;
            } else if (words[i + 2] == kDecorationDescriptorSet) {
                sets.emplace_back(words[i + 1], words[i + 3]);
            } else if (words[i + 2] == kDecorationBinding) {
                binds.emplace_back(words[i + 1], words[i + 3]);
            }
        }
        i += len;
    }
    for (auto& [id, binding] : binds) {
        for (auto& [sid, set] : sets) {
            if (sid == id) {
                out.bindings.emplace_back(set, binding);
            }
        }
    }
    std::sort(out.locations.begin(), out.locations.end());
    std::sort(out.bindings.begin(), out.bindings.end());
    std::sort(out.capabilities.begin(), out.capabilities.end());
    std::sort(out.offsets.begin(), out.offsets.end());
    std::sort(out.variable_classes.begin(), out.variable_classes.end());
    std::sort(out.image_formats.begin(), out.image_formats.end());
    return out;
}

void SameInterface(const u32* glsl, usize glsl_count, const u32* slang, usize slang_count, ShaderStage stage) {
    CHECK(ValidateSpirv(glsl, glsl_count, stage, "main") == nullptr);
    CHECK(ValidateSpirv(slang, slang_count, stage, "main") == nullptr);
    Interface a = Reflect(glsl, glsl_count);
    Interface b = Reflect(slang, slang_count);
    CHECK(b.entry_points == 1);
    CHECK(a.entry_points == b.entry_points);
    CHECK(a.locations == b.locations);
    CHECK(a.bindings == b.bindings);
    CHECK(a.non_readable == b.non_readable);
    CHECK(a.non_writable == b.non_writable);
    CHECK(a.offsets == b.offsets);
    CHECK(a.variable_classes.size() == b.variable_classes.size());
    // Slang may declare a few more Input/Output variables (it splits struct outputs); push-constant (9), uniform (2),
    // storage-buffer (12) and uniform-constant (0) variables must match exactly.
    for (u32 storage_class : {0u, 2u, 9u, 12u}) {
        CHECK(std::count(a.variable_classes.begin(), a.variable_classes.end(), storage_class) ==
              std::count(b.variable_classes.begin(), b.variable_classes.end(), storage_class));
    }
    // No capability beyond what every Vulkan device guarantees: in particular not DrawParameters (SV_VertexID).
    for (u32 capability : b.capabilities) {
        CHECK((capability == kCapabilityMatrix || capability == kCapabilityShader || capability == kCapabilityImageQuery));
    }
    CHECK(a.image_formats.size() == b.image_formats.size());
    CHECK(a.local_size[0] == b.local_size[0]);
    CHECK(a.local_size[1] == b.local_size[1]);
    CHECK(a.local_size[2] == b.local_size[2]);
    if (stage == ShaderStage::Compute) {
        CHECK(b.local_size[0] != 0);
    }
}

} // namespace

#define SAME_INTERFACE(name, stage) \
    SameInterface(glsl_ref::engine_placeholder::shaders::name, glsl_ref::engine_placeholder::shaders::name##WordCount, \
                  slang_out::engine_placeholder::shaders::name, slang_out::engine_placeholder::shaders::name##WordCount, stage)

TEST_CASE("Slang ports keep the GLSL shaders' interface", "[rhi][spirv][slang]") {
    SECTION("fullscreen.vert") { SAME_INTERFACE(kFullscreenVertSpv, ShaderStage::Vertex); }
    SECTION("triangle.vert") { SAME_INTERFACE(kTriangleVertSpv, ShaderStage::Vertex); }
    SECTION("triangle.frag") { SAME_INTERFACE(kTriangleFragSpv, ShaderStage::Fragment); }
    SECTION("bindings_test.frag") { SAME_INTERFACE(kBindingsTestFragSpv, ShaderStage::Fragment); }
    SECTION("fill.comp") { SAME_INTERFACE(kFillCompSpv, ShaderStage::Compute); }
    SECTION("saxpy.comp") { SAME_INTERFACE(kSaxpyCompSpv, ShaderStage::Compute); }
    SECTION("gradient.comp") { SAME_INTERFACE(kGradientCompSpv, ShaderStage::Compute); }
}

// Guards the comparison above against passing because the reflection found nothing on both sides.
TEST_CASE("Slang ports expose the expected bindings and local sizes", "[rhi][spirv][slang]") {
    namespace s = engine_placeholder::slang_out::engine_placeholder::shaders;
    Interface bindings = Reflect(s::kBindingsTestFragSpv, s::kBindingsTestFragSpvWordCount);
    CHECK(bindings.bindings == std::vector<std::pair<u32, u32>>{{0, 0}, {0, 1}, {0, 2}});
    CHECK(Reflect(s::kSaxpyCompSpv, s::kSaxpyCompSpvWordCount).bindings.size() == 2);
    CHECK(Reflect(s::kGradientCompSpv, s::kGradientCompSpvWordCount).bindings.size() == 1);
    Interface fill = Reflect(s::kFillCompSpv, s::kFillCompSpvWordCount);
    CHECK(fill.local_size[0] == 64);
    CHECK(fill.local_size[1] == 1);
    Interface gradient = Reflect(s::kGradientCompSpv, s::kGradientCompSpvWordCount);
    CHECK(gradient.local_size[0] == 8);
    CHECK(gradient.local_size[1] == 8);
    Interface triangle = Reflect(s::kTriangleVertSpv, s::kTriangleVertSpvWordCount);
    CHECK(triangle.locations == std::vector<u32>{0, 0, 1});
}
