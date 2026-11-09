// Phase 5 Day 71: shader reflection. Every committed shaders/*.refl.inc must parse, say what the shader's source says,
// round-trip byte for byte, build the layouts the hand-written code used to build, and reject every kind of damage
// without reading outside its input (this binary also runs under ASan).
#include "os/console.h"
#include "cstr/cstr.h"
#include <vector>

#include <catch2/catch_test_macros.hpp>

#include "renderer/frame_data.h"
#include "renderer/shader_reflection.h"
#include "rhi/device.h"
#include "rhi/null/null_device.h"
#include "rhi/rhi.h"

#include "bindings_test.frag.refl.inc"
#include "cube.frag.refl.inc"
#include "cube.vert.refl.inc"
#include "fill.comp.refl.inc"
#include "fullscreen.vert.refl.inc"
#include "gbuffer.vert.refl.inc"
#include "gradient.comp.refl.inc"
#include "lighting.frag.refl.inc"
#include "lit.frag.refl.inc"
#include "lit.vert.refl.inc"
#include "saxpy.comp.refl.inc"
#include "textured.frag.refl.inc"
#include "textured.vert.refl.inc"
#include "triangle.frag.refl.inc"
#include "triangle.vert.refl.inc"
#include "cstr/mem_ops.h"

using namespace engine_placeholder;
using namespace engine_placeholder::renderer;
using rhi::BindingType;
using rhi::ShaderStage;
using rhi::VertexFormat;

namespace {

struct Blob {
    const char* name;
    const u8* data;
    usize size;
};

#define BLOB(base, file) Blob{file, shaders::base##Refl, shaders::base##ReflByteCount}

const Blob kAllBlobs[] = {
    BLOB(kTriangleVert, "triangle.vert"),   BLOB(kTriangleFrag, "triangle.frag"), BLOB(kFullscreenVert, "fullscreen.vert"),
    BLOB(kBindingsTestFrag, "bindings_test.frag"), BLOB(kFillComp, "fill.comp"),   BLOB(kSaxpyComp, "saxpy.comp"),
    BLOB(kGradientComp, "gradient.comp"),   BLOB(kCubeVert, "cube.vert"),         BLOB(kCubeFrag, "cube.frag"),
    BLOB(kTexturedVert, "textured.vert"),   BLOB(kTexturedFrag, "textured.frag"), BLOB(kLitVert, "lit.vert"),
    BLOB(kLitFrag, "lit.frag"),
};

ShaderReflection Load(const Blob& blob) {
    ShaderReflection r;
    const char* problem = ParseShaderReflection(blob.data, blob.size, &r);
    INFO(blob.name << ": " << (problem != nullptr ? problem : ""));
    REQUIRE(problem == nullptr);
    return r;
}

#define LOAD(base) Load(BLOB(base, #base))

// A heap copy of exactly `size` bytes, so AddressSanitizer flags any read past the end.
std::vector<u8> Copy(const u8* data, usize size) { return std::vector<u8>(data, data + size); }

} // namespace

TEST_CASE("Every committed reflection blob parses and round-trips", "[renderer][reflection]") {
    for (const Blob& blob : kAllBlobs) {
        ShaderReflection r = Load(blob);
        CHECK(CStrCompare(r.entry, "main") == 0);
        std::vector<u8> again(blob.size);
        CHECK(SerializeShaderReflection(r, again.data(), again.size()) == blob.size);
        CHECK(MemCompare(again.data(), blob.data, blob.size) == 0);
        CHECK(SerializeShaderReflection(r, nullptr, 0) == blob.size);
    }
}

TEST_CASE("Reflection says what the shader sources say", "[renderer][reflection]") {
    SECTION("lit.vert: the 176-byte block, one binding, three vertex inputs") {
        ShaderReflection r = LOAD(kLitVert);
        CHECK(r.stage == ShaderStage::Vertex);
        REQUIRE(r.binding_count == 1);
        CHECK(r.bindings[0].set == 0);
        CHECK(r.bindings[0].binding == 0);
        CHECK(r.bindings[0].type == BindingType::UniformBuffer);
        CHECK(r.bindings[0].stages == ShaderStage::Vertex);
        REQUIRE(r.bindings[0].block != kNoBlock);
        const ReflectedBlock& block = r.blocks[r.bindings[0].block];
        CHECK(CStrCompare(block.name, "Params") == 0);
        CHECK(block.size == 176);
        REQUIRE(block.member_count == 5);
        const char* names[5] = {"mvp", "normal_matrix", "to_light", "light_color", "ambient"};
        const u32 offsets[5] = {0, 64, 128, 144, 160};
        for (u32 i = 0; i < 5; ++i) {
            CHECK(CStrCompare(block.members[i].name, names[i]) == 0);
            CHECK(block.members[i].offset == offsets[i]);
            CHECK(block.members[i].scalar == ScalarType::Float32);
        }
        CHECK(block.members[0].kind == MemberKind::Matrix);
        CHECK(block.members[0].rows == 4);
        CHECK(block.members[0].columns == 4);
        CHECK(block.members[0].size == 64);
        CHECK(block.members[2].kind == MemberKind::Vector);
        CHECK(block.members[2].rows == 4);
        CHECK_FALSE(r.has_push_constants);
        REQUIRE(r.vertex_input_count == 3);
        CHECK(r.vertex_inputs[0].location == 0);
        CHECK(r.vertex_inputs[0].format == VertexFormat::Float3);
        CHECK(r.vertex_inputs[2].location == 2);
        CHECK(r.vertex_inputs[2].format == VertexFormat::Float2);
    }
    SECTION("lit.frag: the same block plus a texture and a sampler") {
        ShaderReflection r = LOAD(kLitFrag);
        REQUIRE(r.binding_count == 3);
        CHECK(r.bindings[0].type == BindingType::UniformBuffer);
        CHECK(r.bindings[1].type == BindingType::SampledTexture);
        CHECK(r.bindings[1].binding == 1);
        CHECK(r.bindings[2].type == BindingType::Sampler);
        CHECK(r.bindings[2].binding == 2);
        CHECK(r.blocks[r.bindings[0].block].size == 176);
        CHECK(r.vertex_input_count == 0);
    }
    SECTION("bindings_test.frag: all binding kinds and a push-constant block") {
        ShaderReflection r = LOAD(kBindingsTestFrag);
        REQUIRE(r.binding_count == 3);
        CHECK(r.has_push_constants);
        const ReflectedBlock& push = r.blocks[r.push_constant_block];
        CHECK(CStrCompare(push.name, "Push") == 0);
        CHECK(push.size == 16);
        REQUIRE(push.member_count == 1);
        CHECK(CStrCompare(push.members[0].name, "bias") == 0);
    }
    SECTION("compute shaders: local size, storage buffers, storage image") {
        ShaderReflection fill = LOAD(kFillComp);
        CHECK(fill.stage == ShaderStage::Compute);
        CHECK(fill.local_size[0] == 64);
        CHECK(fill.local_size[1] == 1);
        CHECK(fill.local_size[2] == 1);
        REQUIRE(fill.binding_count == 1);
        CHECK(fill.bindings[0].type == BindingType::StorageBuffer);
        CHECK(fill.bindings[0].stride == 4); // RWStructuredBuffer<uint> (Phase 13 Day 166)
        CHECK(fill.has_push_constants);
        CHECK(fill.blocks[fill.push_constant_block].size == 4);

        ShaderReflection saxpy = LOAD(kSaxpyComp);
        REQUIRE(saxpy.binding_count == 2);
        CHECK(saxpy.bindings[0].type == BindingType::ReadOnlyStorageBuffer); // x_buf is a StructuredBuffer (Phase 13 Day 163)
        CHECK(saxpy.bindings[1].type == BindingType::StorageBuffer);
        CHECK(saxpy.bindings[0].stride == 4);
        CHECK(saxpy.bindings[1].stride == 4);

        ShaderReflection gradient = LOAD(kGradientComp);
        CHECK(gradient.local_size[0] == 8);
        CHECK(gradient.local_size[1] == 8);
        REQUIRE(gradient.binding_count == 1);
        CHECK(gradient.bindings[0].type == BindingType::StorageTexture);
        CHECK_FALSE(gradient.has_push_constants);
    }
    SECTION("triangle.vert: position and color inputs; fullscreen.vert has none") {
        ShaderReflection tri = LOAD(kTriangleVert);
        REQUIRE(tri.vertex_input_count == 2);
        CHECK(tri.vertex_inputs[0].format == VertexFormat::Float2);
        CHECK(tri.vertex_inputs[1].format == VertexFormat::Float3);
        CHECK(LOAD(kFullscreenVert).vertex_input_count == 0);
    }
}

TEST_CASE("A structured buffer's reflected stride is the C++ element's size, and only storage buffers have one", "[renderer][reflection]") {
    // Direct3D 12 makes structured views with it (Phase 13 Day 166).
    const ShaderReflection gbuffer = LOAD(kGbufferVert);
    const ShaderReflection lighting = LOAD(kLightingFrag);
    u32 found = 0;
    for (const ShaderReflection* r : {&gbuffer, &lighting}) {
        for (u32 i = 0; i < r->binding_count; ++i) {
            const ReflectedBinding& b = r->bindings[i];
            INFO(b.name);
            if (CStrCompare(b.name, "objects") == 0) {
                CHECK(b.stride == sizeof(ObjectData));
                ++found;
            } else if (CStrCompare(b.name, "lights") == 0) {
                CHECK(b.stride == sizeof(LightData));
                ++found;
            } else {
                CHECK(b.stride == 0);
            }
        }
    }
    CHECK(found == 2);
    for (const Blob& blob : kAllBlobs) {
        const ShaderReflection r = Load(blob);
        for (u32 i = 0; i < r.binding_count; ++i) {
            const bool storage = r.bindings[i].type == BindingType::StorageBuffer || r.bindings[i].type == BindingType::ReadOnlyStorageBuffer;
            INFO(blob.name << " " << r.bindings[i].name);
            CHECK((r.bindings[i].stride != 0) == storage);
        }
    }
}

TEST_CASE("A C++ struct whose size disagrees with the reflected block fails the size check", "[renderer][reflection]") {
    ShaderReflection lit = LOAD(kLitVert);
    ReflectionError error;
    CHECK(CheckBlockSize(lit, "Params", 176, &error));
    CHECK_FALSE(CheckBlockSize(lit, "Params", 160, &error));
    CHECK(CStrFind(error.message, "176") != nullptr);
    CHECK(CStrFind(error.message, "160") != nullptr);
    CHECK_FALSE(CheckBlockSize(lit, "Missing", 176, &error));
    CHECK(CStrFind(error.message, "Missing") != nullptr);
    // The constants the generated include files carry agree with the parsed blocks.
    CHECK(shaders::kLitVertParamsSize == lit.blocks[0].size);
    CHECK(shaders::kLitVertParamsAmbientOffset == lit.blocks[0].members[4].offset);
}

TEST_CASE("Layouts are built from reflection", "[renderer][reflection]") {
    ShaderReflection vs = LOAD(kLitVert);
    ShaderReflection fs = LOAD(kLitFrag);
    ReflectionError error;

    SECTION("a vertex and a fragment shader merge into one layout with the stage masks OR-ed") {
        const ShaderReflection* pair[2] = {&vs, &fs};
        BindGroupLayoutData layout;
        REQUIRE(BuildBindGroupLayout(pair, 2, 0, &layout, &error));
        REQUIRE(layout.entry_count == 3);
        CHECK(layout.entries[0].binding == 0);
        CHECK(layout.entries[0].type == BindingType::UniformBuffer);
        CHECK(layout.entries[0].stages == (ShaderStage::Vertex | ShaderStage::Fragment));
        CHECK(layout.entries[1].binding == 1);
        CHECK(layout.entries[1].type == BindingType::SampledTexture);
        CHECK(layout.entries[1].stages == ShaderStage::Fragment);
        CHECK(layout.entries[2].binding == 2);
        CHECK(layout.entries[2].type == BindingType::Sampler);
        CHECK(layout.Desc().entries.Size() == 3);
        CHECK(layout.Desc().Validate() == nullptr);
    }
    SECTION("the order of the shaders does not matter") {
        const ShaderReflection* pair[2] = {&fs, &vs};
        BindGroupLayoutData layout;
        REQUIRE(BuildBindGroupLayout(pair, 2, 0, &layout, &error));
        REQUIRE(layout.entry_count == 3);
        CHECK(layout.entries[0].binding == 0);
        CHECK(layout.entries[0].stages == (ShaderStage::Vertex | ShaderStage::Fragment));
    }
    SECTION("a set the shaders do not use is empty") {
        BindGroupLayoutData layout;
        REQUIRE(BuildBindGroupLayoutFor(vs, 1, &layout, &error));
        CHECK(layout.entry_count == 0);
    }
    SECTION("conflicting types for one binding are reported with both names") {
        ShaderReflection other = fs;
        other.bindings[0].type = BindingType::StorageBuffer;
        other.bindings[0].block = kNoBlock;
        other.bindings[0].stride = 16; // a storage buffer has one (Phase 13 Day 166)
        MemCopy(other.bindings[0].name, "gizmo", 6);
        const ShaderReflection* pair[2] = {&vs, &other};
        BindGroupLayoutData layout;
        CHECK_FALSE(BuildBindGroupLayout(pair, 2, 0, &layout, &error));
        CHECK(CStrFind(error.message, "params") != nullptr);
        CHECK(CStrFind(error.message, "gizmo") != nullptr);
        CHECK(CStrFind(error.message, "binding 0") != nullptr);
    }
    SECTION("storage buffers of one binding with different strides conflict, and the message names both") {
        // Review finding (Day 166): the conflict message left the stride out.
        ShaderReflection a = fs;
        a.bindings[0].type = BindingType::ReadOnlyStorageBuffer;
        a.bindings[0].block = kNoBlock;
        a.bindings[0].stride = 16;
        ShaderReflection b = vs;
        b.bindings[0].type = BindingType::ReadOnlyStorageBuffer;
        b.bindings[0].block = kNoBlock;
        b.bindings[0].stride = 32;
        const ShaderReflection* pair[2] = {&b, &a};
        BindGroupLayoutData layout;
        CHECK_FALSE(BuildBindGroupLayout(pair, 2, 0, &layout, &error));
        CHECK(CStrFind(error.message, "stride 32") != nullptr);
        CHECK(CStrFind(error.message, "stride 16") != nullptr);
    }
    SECTION("conflicting array sizes are reported too") {
        ShaderReflection other = fs;
        other.bindings[1].count = 4;
        const ShaderReflection* pair[2] = {&fs, &other};
        BindGroupLayoutData layout;
        CHECK_FALSE(BuildBindGroupLayout(pair, 2, 0, &layout, &error));
        CHECK(CStrFind(error.message, "count 4") != nullptr);
    }
    SECTION("no shaders is an error") {
        BindGroupLayoutData layout;
        CHECK_FALSE(BuildBindGroupLayout(nullptr, 0, 0, &layout, &error));
    }
}

TEST_CASE("The cube vertex layout is built from the vertex shader", "[renderer][reflection]") {
    ReflectionError error;
    VertexLayoutData layout;
    ShaderReflection cube = LOAD(kCubeVert);
    REQUIRE(BuildVertexLayout(cube, 0, &layout, &error));
    REQUIRE(layout.attribute_count == 3);
    CHECK(layout.stride == 32); // sizeof(CubeVertex) in game/common/cube_scene.h
    CHECK(layout.attributes[0].location == 0);
    CHECK(layout.attributes[0].format == VertexFormat::Float3);
    CHECK(layout.attributes[0].offset == 0);
    CHECK(layout.attributes[1].location == 1);
    CHECK(layout.attributes[1].offset == 12);
    CHECK(layout.attributes[2].location == 2);
    CHECK(layout.attributes[2].format == VertexFormat::Float2);
    CHECK(layout.attributes[2].offset == 24);
    CHECK(layout.attributes[2].binding == 0);

    ShaderReflection fragment = LOAD(kLitFrag);
    CHECK_FALSE(BuildVertexLayout(fragment, 0, &layout, &error));
    CHECK(CStrFind(error.message, "fragment") != nullptr);

    ShaderReflection tri = LOAD(kTriangleVert);
    REQUIRE(BuildVertexLayout(tri, 1, &layout, &error));
    CHECK(layout.stride == 20);
    CHECK(layout.attributes[1].offset == 8);
    CHECK(layout.attributes[1].binding == 1);
}

TEST_CASE("Built layouts are accepted by the null device", "[renderer][reflection][null]") {
    rhi::NullDevicePtr device = rhi::CreateNullDevice();
    ShaderReflection vs = LOAD(kLitVert);
    ShaderReflection fs = LOAD(kLitFrag);
    const ShaderReflection* pair[2] = {&vs, &fs};
    BindGroupLayoutData layout;
    ReflectionError error;
    REQUIRE(BuildBindGroupLayout(pair, 2, 0, &layout, &error));
    rhi::BindGroupLayoutHandle handle = device->CreateBindGroupLayout(layout.Desc("lit"));
    CHECK(handle.IsValid());
    device->DestroyBindGroupLayout(handle);
    device->WaitIdle();
    device.Reset();
}

TEST_CASE("Built layouts are accepted by a real device", "[renderer][reflection][gpu]") {
    rhi::DeviceCreateInfo info;
    info.validation = rhi::ValidationSetting::On;
    info.sync_validation = true;
    info.pipeline_cache = false;
    rhi::DevicePtr device = rhi::CreateDevice(info);
    if (device == nullptr) {
        const char* required = os::GetEnvironment("ENGINE_RHI_REQUIRE_GPU");
        if (required != nullptr && required[0] == '1') {
            FAIL("ENGINE_RHI_REQUIRE_GPU=1 but no Vulkan device could be created");
        }
        SKIP("no Vulkan device on this machine");
    }
    ShaderReflection vs = LOAD(kLitVert);
    ShaderReflection fs = LOAD(kLitFrag);
    const ShaderReflection* pair[2] = {&vs, &fs};
    BindGroupLayoutData layout;
    ReflectionError error;
    REQUIRE(BuildBindGroupLayout(pair, 2, 0, &layout, &error));
    rhi::BindGroupLayoutHandle handle = device->CreateBindGroupLayout(layout.Desc("lit"));
    CHECK(handle.IsValid());
    device->DestroyBindGroupLayout(handle);
    device->WaitIdle();
    device.Reset();
}

TEST_CASE("Malformed blobs are rejected with a message", "[renderer][reflection]") {
    const Blob& lit = kAllBlobs[11];
    REQUIRE(CStrCompare(lit.name, "lit.vert") == 0);
    ShaderReflection scratch;

    SECTION("bad magic") {
        std::vector<u8> bytes = Copy(lit.data, lit.size);
        bytes[0] ^= 0xff;
        const char* problem = ParseShaderReflection(bytes.data(), bytes.size(), &scratch);
        REQUIRE(problem != nullptr);
        CHECK(CStrFind(problem, "magic") != nullptr);
    }
    SECTION("bad version") {
        std::vector<u8> bytes = Copy(lit.data, lit.size);
        bytes[4] = static_cast<u8>(kReflectionVersion + 1);
        const char* problem = ParseShaderReflection(bytes.data(), bytes.size(), &scratch);
        REQUIRE(problem != nullptr);
        CHECK(CStrFind(problem, "version") != nullptr);
    }
    SECTION("unknown stage") {
        std::vector<u8> bytes = Copy(lit.data, lit.size);
        bytes[8] = 0x40;
        const char* problem = ParseShaderReflection(bytes.data(), bytes.size(), &scratch);
        REQUIRE(problem != nullptr);
        CHECK(CStrFind(problem, "stage") != nullptr);
    }
    SECTION("unknown binding type") {
        const Blob& bt = kAllBlobs[3];
        REQUIRE(CStrCompare(bt.name, "bindings_test.frag") == 0);
        ShaderReflection r = Load(bt);
        // magic, version, stage (12 bytes), entry (1 + len), local size (12), binding count (4), then the first binding:
        // name (1 + len), set (4), binding (4), type.
        const usize type_offset = 12 + 1 + CStrLength(r.entry) + 12 + 4 + 1 + CStrLength(r.bindings[0].name) + 4 + 4;
        std::vector<u8> bytes = Copy(bt.data, bt.size);
        REQUIRE(bytes[type_offset] == static_cast<u8>(r.bindings[0].type));
        bytes[type_offset] = static_cast<u8>(BindingType::Count);
        const char* problem = ParseShaderReflection(bytes.data(), bytes.size(), &scratch);
        REQUIRE(problem != nullptr);
        CHECK(CStrFind(problem, "binding type") != nullptr);
    }
    SECTION("trailing bytes") {
        std::vector<u8> bytes = Copy(lit.data, lit.size);
        bytes.push_back(0);
        const char* problem = ParseShaderReflection(bytes.data(), bytes.size(), &scratch);
        REQUIRE(problem != nullptr);
        CHECK(CStrFind(problem, "trailing") != nullptr);
    }
    SECTION("empty and null input") {
        CHECK(ParseShaderReflection(nullptr, 0, &scratch) != nullptr);
        u8 one = 0;
        CHECK(ParseShaderReflection(&one, 0, &scratch) != nullptr);
        CHECK(ParseShaderReflection(nullptr, 5, &scratch) != nullptr);
    }
}

TEST_CASE("Every truncation of every blob is rejected without reading out of bounds", "[renderer][reflection][asan]") {
    for (const Blob& blob : kAllBlobs) {
        for (usize length = 0; length < blob.size; ++length) {
            std::vector<u8> bytes = Copy(blob.data, length); // exact-size heap allocation: ASan sees any overread
            ShaderReflection scratch;
            INFO(blob.name << " truncated to " << length);
            CHECK(ParseShaderReflection(bytes.data(), bytes.size(), &scratch) != nullptr);
        }
    }
}

TEST_CASE("Random damage never crashes the parser and never yields an inconsistent reflection", "[renderer][reflection][asan]") {
    u32 state = 0x12345678u;
    auto next = [&]() {
        state = state * 1664525u + 1013904223u;
        return state >> 8;
    };
    u32 accepted = 0;
    u32 rejected = 0;
    for (const Blob& blob : kAllBlobs) {
        for (u32 trial = 0; trial < 400; ++trial) {
            std::vector<u8> bytes = Copy(blob.data, blob.size);
            const u32 flips = 1 + next() % 3;
            for (u32 f = 0; f < flips; ++f) {
                bytes[next() % bytes.size()] = static_cast<u8>(next());
            }
            ShaderReflection r;
            const char* problem = ParseShaderReflection(bytes.data(), bytes.size(), &r);
            if (problem == nullptr) {
                ++accepted;
                CHECK(ValidateShaderReflection(r) == nullptr);
                // A reflection the parser accepts serializes back to the same bytes.
                std::vector<u8> again(bytes.size());
                CHECK(SerializeShaderReflection(r, again.data(), again.size()) == bytes.size());
                CHECK(again == bytes);
            } else {
                ++rejected;
            }
        }
    }
    CHECK(rejected > accepted); // most damage is caught; some (a changed offset, say) is still a valid reflection
}

TEST_CASE("Serialization refuses an invalid or oversized reflection", "[renderer][reflection]") {
    ShaderReflection r = LOAD(kLitVert);
    std::vector<u8> tiny(8);
    CHECK(SerializeShaderReflection(r, tiny.data(), tiny.size()) == 0);
    r.bindings[0].type = BindingType::Count;
    CHECK(ValidateShaderReflection(r) != nullptr);
    CHECK(SerializeShaderReflection(r, nullptr, 0) == 0);
    r = LOAD(kLitVert);
    r.block_count = kMaxReflectedBlocks + 1;
    CHECK(ValidateShaderReflection(r) != nullptr);
    r = LOAD(kLitVert);
    r.entry[kMaxReflectionName - 1] = 'x';
    MemSet(r.entry, 'x', kMaxReflectionName);
    CHECK(ValidateShaderReflection(r) != nullptr);
}

TEST_CASE("Names must be non-empty identifiers", "[renderer][reflection]") {
    ShaderReflection r = LOAD(kLitVert);
    r.blocks[0].members[0].name[0] = '\0';
    CHECK(ValidateShaderReflection(r) != nullptr);
    r = LOAD(kLitVert);
    r.bindings[0].name[0] = '!';
    CHECK(ValidateShaderReflection(r) != nullptr);
}

TEST_CASE("Duplicate block and member names are rejected", "[renderer][reflection]") {
    ShaderReflection r = LOAD(kLitVert);
    MemCopy(r.blocks[0].members[1].name, r.blocks[0].members[0].name, kMaxReflectionName);
    CHECK(ValidateShaderReflection(r) != nullptr);
    CHECK(SerializeShaderReflection(r, nullptr, 0) == 0);

    r = LOAD(kLitFrag);
    r.block_count = 2;
    r.blocks[1] = r.blocks[0];
    CHECK(ValidateShaderReflection(r) != nullptr);
}
