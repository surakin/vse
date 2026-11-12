// The Metal backend's shaders and pipelines (Phase 26 Day 319): every committed shader forms a pipeline with the
// layouts its reflection gives (Metal validation on and silent), the vertex descriptor, the depth-stencil state cache,
// compute threadgroups, refusals, and the pipeline cache (an MTLBinaryArchive) round-tripping and surviving damage.
// Modelled on dx12_pipeline_test.cpp. macOS only.
#include "metal_test_support.h"  // first: it imports AppKit before any engine name is in scope

#include "cstr/cstr.h"
#include "cstr/format.h"
#include "cstr/mem_ops.h"

#include <catch2/catch_test_macros.hpp>

#include "bindings_test.frag.dxil.inc"
#include "bindings_test.frag.refl.inc"
#include "cube.frag.dxil.inc"
#include "cube.frag.refl.inc"
#include "cube.vert.dxil.inc"
#include "cube.vert.refl.inc"
#include "fill.comp.dxil.inc"
#include "fill.comp.refl.inc"
#include "fullscreen.vert.dxil.inc"
#include "fullscreen.vert.refl.inc"
#include "gbuffer.frag.perm.inc"
#include "gbuffer.vert.perm.inc"
#include "gbuffer_debug.frag.dxil.inc"
#include "gbuffer_debug.frag.refl.inc"
#include "gradient.comp.dxil.inc"
#include "gradient.comp.refl.inc"
#include "graph_sample.frag.dxil.inc"
#include "graph_sample.frag.refl.inc"
#include "lighting.frag.dxil.inc"
#include "lighting.frag.refl.inc"
#include "lit.frag.dxil.inc"
#include "lit.frag.refl.inc"
#include "lit.vert.dxil.inc"
#include "lit.vert.refl.inc"
#include "material_test.frag.perm.inc"
#include "material_test.vert.dxil.inc"
#include "material_test.vert.refl.inc"
#include "permute_test.frag.perm.inc"
#include "point_light.frag.dxil.inc"
#include "point_light.frag.refl.inc"
#include "point_light.vert.dxil.inc"
#include "point_light.vert.refl.inc"
#include "saxpy.comp.dxil.inc"
#include "saxpy.comp.refl.inc"
#include "temp_dir.h"
#include "textured.frag.dxil.inc"
#include "textured.frag.refl.inc"
#include "textured.vert.dxil.inc"
#include "textured.vert.refl.inc"
#include "tonemap.frag.dxil.inc"
#include "tonemap.frag.refl.inc"
#include "triangle.frag.dxil.inc"
#include "triangle.frag.refl.inc"
#include "triangle.vert.dxil.inc"
#include "triangle.vert.refl.inc"
#include "containers/vector.h"
#include "io/file.h"
#include "renderer/gbuffer.h"
#include "renderer/material.h"
#include "renderer/shader_reflection.h"
#include "rhi/metal/metal_device.h"
#include "rhi/metal/metal_pipeline_cache.h"
#include "rhi/rhi.h"
#include "rhi/rhi_log.h"

using namespace engine_placeholder;
using namespace engine_placeholder::rhi;
using namespace engine_placeholder::rhi::metal;
using namespace engine_placeholder::rhi::metal::test;

namespace metal_pipeline_test
{

DevicePtr MakePipelineDevice(const char* cache_dir, bool cache = true)
{
    DeviceCreateInfo info = MetalInfo("metal-pipeline-test");
    info.pipeline_cache = cache;
    info.pipeline_cache_dir = cache_dir;
    return CreateDevice(info);
}

// One minted module: its Metal library (with its entry and slot table) and its reflection.
struct Module
{
    const char* name;
    const u8* metallib;
    usize metallib_size;
    const char* entry;
    const ShaderSlotTable* slots;
    const u8* refl;
    usize refl_size;
};

#define MODULE(base, name)                                                                                             \
    Module                                                                                                             \
    {                                                                                                                  \
        name, shaders::base##Metallib, shaders::base##MetallibByteCount, shaders::base##MetallibEntry,                 \
            &shaders::base##MetallibSlots, shaders::base##Refl, shaders::base##ReflByteCount                           \
    }
#define PERM(base, i, name)                                                                                            \
    Module                                                                                                             \
    {                                                                                                                  \
        name, shaders::base##Modules[i].metallib, shaders::base##Modules[i].metallib_size,                             \
            shaders::base##Modules[i].metallib_entry, shaders::base##Modules[i].metallib_slots,                        \
            shaders::base##Modules[i].reflection, shaders::base##Modules[i].reflection_size                            \
    }

const Module kFullscreen = MODULE(kFullscreenVert, "fullscreen.vert");
const Module kGbufferVert[2] = {PERM(kGbufferVert, 0, "gbuffer.vert"),
                                PERM(kGbufferVert, 1, "gbuffer.vert VERTEX_COLOR")};

// Every fragment module with the vertex module it runs after (Metal links the stages' interfaces).
struct Pair
{
    Module vs;
    Module fs;
};

Vector<Pair> AllPairs()
{
    Vector<Pair> pairs;
    pairs.Push({MODULE(kTriangleVert, "triangle.vert"), MODULE(kTriangleFrag, "triangle.frag")});
    pairs.Push({kFullscreen, MODULE(kBindingsTestFrag, "bindings_test.frag")});
    pairs.Push({MODULE(kCubeVert, "cube.vert"), MODULE(kCubeFrag, "cube.frag")});
    pairs.Push({MODULE(kTexturedVert, "textured.vert"), MODULE(kTexturedFrag, "textured.frag")});
    pairs.Push({MODULE(kLitVert, "lit.vert"), MODULE(kLitFrag, "lit.frag")});
    static_assert(sizeof(shaders::kPermuteTestFragModules) / sizeof(shaders::kPermuteTestFragModules[0]) == 6,
                  "update the permute_test list");
    for (u32 i = 0; i < 6; ++i)
    {
        pairs.Push({kFullscreen, PERM(kPermuteTestFrag, i, "permute_test.frag")});
    }
    static_assert(sizeof(shaders::kMaterialTestFragModules) / sizeof(shaders::kMaterialTestFragModules[0]) == 2,
                  "update the material_test list");
    for (u32 i = 0; i < 2; ++i)
    {
        pairs.Push({MODULE(kMaterialTestVert, "material_test.vert"), PERM(kMaterialTestFrag, i, "material_test.frag")});
    }
    pairs.Push({kFullscreen, MODULE(kGraphSampleFrag, "graph_sample.frag")});
    // gbuffer.frag's options are NORMAL_MAP, ALPHA_TEST, VERTEX_COLOR (bits 0-2); gbuffer.vert's only VERTEX_COLOR.
    static_assert(sizeof(shaders::kGbufferFragModules) / sizeof(shaders::kGbufferFragModules[0]) == 8,
                  "update the gbuffer list");
    for (u32 i = 0; i < 8; ++i)
    {
        pairs.Push({kGbufferVert[(i >> 2) & 1], PERM(kGbufferFrag, i, "gbuffer.frag")});
    }
    pairs.Push({kFullscreen, MODULE(kGbufferDebugFrag, "gbuffer_debug.frag")});
    pairs.Push({kFullscreen, MODULE(kLightingFrag, "lighting.frag")});
    pairs.Push({MODULE(kPointLightVert, "point_light.vert"), MODULE(kPointLightFrag, "point_light.frag")});
    pairs.Push({kFullscreen, MODULE(kTonemapFrag, "tonemap.frag")});
    return pairs;
}

const Module kComputeModules[] = {MODULE(kFillComp, "fill.comp"), MODULE(kSaxpyComp, "saxpy.comp"),
                                  MODULE(kGradientComp, "gradient.comp")};

renderer::ShaderReflection Reflect(const Module& m)
{
    renderer::ShaderReflection r;
    const char* problem = renderer::ParseShaderReflection(m.refl, m.refl_size, &r);
    INFO(m.name << ": " << (problem != nullptr ? problem : ""));
    REQUIRE(problem == nullptr);
    return r;
}

ShaderHandle MakeShader(Device& device, const Module& m, ShaderStage stage)
{
    ShaderDesc sd;
    sd.stage = stage;
    sd.SetCode(MetalLibBlob(m.metallib, m.metallib_size, m.entry, m.slots));
    sd.debug_name = m.name;
    return device.CreateShader(sd);
}

// The layouts and push range of shaders, built from their reflection as the renderer builds them.
struct Layouts
{
    BindGroupLayoutHandle handles[kMaxBindGroups];
    u32 count = 0;
    u32 push_bytes = 0;
    ShaderStage push_stages = ShaderStage::None;

    void Destroy(Device& device)
    {
        for (u32 i = 0; i < count; ++i)
        {
            device.DestroyBindGroupLayout(handles[i]);
        }
        count = 0;
    }
};

Layouts BuildLayouts(Device& device, const renderer::ShaderReflection* const* shaders, u32 shader_count)
{
    Layouts out;
    u32 sets = 0;
    for (u32 s = 0; s < shader_count; ++s)
    {
        for (u32 b = 0; b < shaders[s]->binding_count; ++b)
        {
            sets = shaders[s]->bindings[b].set + 1 > sets ? shaders[s]->bindings[b].set + 1 : sets;
        }
        if (shaders[s]->has_push_constants)
        {
            const u32 size = shaders[s]->blocks[shaders[s]->push_constant_block].size;
            out.push_bytes = size > out.push_bytes ? size : out.push_bytes;
            out.push_stages = out.push_stages | shaders[s]->stage;
        }
    }
    REQUIRE(sets <= kMaxBindGroups);
    for (u32 set = 0; set < sets; ++set)
    {
        renderer::BindGroupLayoutData data;
        renderer::ReflectionError error;
        if (!renderer::BuildBindGroupLayout(shaders, shader_count, set, &data, &error))
        {
            FAIL(error.message);
        }
        out.handles[set] = device.CreateBindGroupLayout(data.Desc());
        REQUIRE(out.handles[set].IsValid());
        out.count = set + 1;
    }
    out.push_bytes = (out.push_bytes + 3) / 4 * 4;
    return out;
}

// Makes every committed shader's pipeline; returns how many.
u32 MakeEveryPipeline(Device& device, Vector<PipelineHandle>* pipelines, Vector<ShaderHandle>* shaders_out,
                      Vector<Layouts>* layouts_out)
{
    const Vector<Pair> pairs = AllPairs();
    for (usize p = 0; p < pairs.Size(); ++p)
    {
        const Pair& pair = pairs[p];
        INFO(pair.vs.name << " + " << pair.fs.name << " (pair " << p << ")");
        const renderer::ShaderReflection vr = Reflect(pair.vs);
        const renderer::ShaderReflection fr = Reflect(pair.fs);
        const renderer::ShaderReflection* both[2] = {&vr, &fr};
        Layouts layouts = BuildLayouts(device, both, 2);
        const ShaderHandle vs = MakeShader(device, pair.vs, ShaderStage::Vertex);
        const ShaderHandle fs = MakeShader(device, pair.fs, ShaderStage::Fragment);
        REQUIRE((vs.IsValid() && fs.IsValid()));
        renderer::VertexLayoutData vl;
        renderer::ReflectionError error;
        REQUIRE(renderer::BuildVertexLayout(vr, 0, &vl, &error));
        VertexBindingDesc bindings[2] = {{vl.stride != 0 ? vl.stride : 16, false}, {}};
        VertexAttributeDesc attributes[kMaxVertexAttributes];
        for (u32 i = 0; i < vl.attribute_count; ++i)
        {
            attributes[i] = vl.attributes[i];
        }
        u32 binding_count = 1, attribute_count = vl.attribute_count;
        REQUIRE(renderer::MaterialSystem::AddObjectIndexAttribute(device, bindings, &binding_count, 2, attributes,
                                                                  &attribute_count, kMaxVertexAttributes,
                                                                  renderer::MaterialSystem::ObjectIndexSlot()));
        GraphicsPipelineDesc gp;
        gp.vertex_shader = vs;
        gp.fragment_shader = fs;
        gp.vertex_bindings = rhi::Span<VertexBindingDesc>(bindings, binding_count);
        gp.vertex_attributes = rhi::Span<VertexAttributeDesc>(attributes, attribute_count);
        // The G-buffer's three targets for gbuffer.frag, else one.
        if (CStrCompareN(pair.fs.name, "gbuffer.frag", 12) == 0)
        {
            gp.color_targets[0].format = renderer::kGBufferAlbedoFormat;
            gp.color_targets[1].format = renderer::kGBufferNormalFormat;
            gp.color_targets[2].format = renderer::kGBufferMaterialFormat;
            gp.color_target_count = 3;
        }
        else
        {
            gp.color_targets[0].format = Format::RGBA16_Float;
            gp.color_target_count = 1;
        }
        gp.depth_format = Format::D32_Float;
        gp.depth_test = true;
        gp.depth_write = true;
        for (u32 i = 0; i < layouts.count; ++i)
        {
            gp.bind_group_layouts[i] = layouts.handles[i];
        }
        gp.bind_group_layout_count = layouts.count;
        gp.push_constant_bytes = layouts.push_bytes;
        gp.push_constant_stages = layouts.push_stages;
        gp.debug_name = pair.fs.name;
        const PipelineHandle pipeline = device.CreateGraphicsPipeline(gp);
        CHECK(pipeline.IsValid());
        pipelines->Push(pipeline);
        shaders_out->Push(vs);
        shaders_out->Push(fs);
        layouts_out->Push(layouts);
    }
    // Each vertex module alone too: a depth-only pipeline.
    const Module vertex_only[] = {kFullscreen, kGbufferVert[0], kGbufferVert[1],
                                  MODULE(kPointLightVert, "point_light.vert")};
    for (const Module& m : vertex_only)
    {
        INFO(m.name << " (depth only)");
        const renderer::ShaderReflection vr = Reflect(m);
        const renderer::ShaderReflection* one[1] = {&vr};
        Layouts layouts = BuildLayouts(device, one, 1);
        const ShaderHandle vs = MakeShader(device, m, ShaderStage::Vertex);
        renderer::VertexLayoutData vl;
        renderer::ReflectionError error;
        REQUIRE(renderer::BuildVertexLayout(vr, 0, &vl, &error));
        VertexBindingDesc bindings[2] = {{vl.stride != 0 ? vl.stride : 16, false}, {}};
        VertexAttributeDesc attributes[kMaxVertexAttributes];
        for (u32 i = 0; i < vl.attribute_count; ++i)
        {
            attributes[i] = vl.attributes[i];
        }
        u32 binding_count = 1, attribute_count = vl.attribute_count;
        REQUIRE(renderer::MaterialSystem::AddObjectIndexAttribute(device, bindings, &binding_count, 2, attributes,
                                                                  &attribute_count, kMaxVertexAttributes, 1));
        GraphicsPipelineDesc gp;
        gp.vertex_shader = vs;
        gp.vertex_bindings = rhi::Span<VertexBindingDesc>(bindings, binding_count);
        gp.vertex_attributes = rhi::Span<VertexAttributeDesc>(attributes, attribute_count);
        gp.depth_format = Format::D32_Float;
        gp.depth_test = true;
        gp.depth_write = true;
        for (u32 i = 0; i < layouts.count; ++i)
        {
            gp.bind_group_layouts[i] = layouts.handles[i];
        }
        gp.bind_group_layout_count = layouts.count;
        gp.push_constant_bytes = layouts.push_bytes;
        gp.push_constant_stages = layouts.push_stages;
        const PipelineHandle pipeline = device.CreateGraphicsPipeline(gp);
        CHECK(pipeline.IsValid());
        pipelines->Push(pipeline);
        shaders_out->Push(vs);
        layouts_out->Push(layouts);
    }
    for (const Module& m : kComputeModules)
    {
        INFO(m.name);
        const renderer::ShaderReflection cr = Reflect(m);
        const renderer::ShaderReflection* one[1] = {&cr};
        Layouts layouts = BuildLayouts(device, one, 1);
        const ShaderHandle cs = MakeShader(device, m, ShaderStage::Compute);
        ComputePipelineDesc cp;
        cp.shader = cs;
        for (u32 i = 0; i < layouts.count; ++i)
        {
            cp.bind_group_layouts[i] = layouts.handles[i];
        }
        cp.bind_group_layout_count = layouts.count;
        cp.push_constant_bytes = layouts.push_bytes;
        cp.debug_name = m.name;
        const PipelineHandle pipeline = device.CreateComputePipeline(cp);
        CHECK(pipeline.IsValid());
        pipelines->Push(pipeline);
        shaders_out->Push(cs);
        layouts_out->Push(layouts);
    }
    return static_cast<u32>(pipelines->Size());
}

struct Everything
{
    Vector<PipelineHandle> pipelines;
    Vector<ShaderHandle> shaders;
    Vector<Layouts> layouts;
    u32 Make(Device& device) { return MakeEveryPipeline(device, &pipelines, &shaders, &layouts); }
    void Destroy(Device& device)
    {
        for (usize i = 0; i < pipelines.Size(); ++i)
        {
            if (pipelines[i].IsValid())
            {
                device.DestroyPipeline(pipelines[i]);
            }
        }
        for (usize i = 0; i < shaders.Size(); ++i)
        {
            device.DestroyShader(shaders[i]);
        }
        for (usize i = 0; i < layouts.Size(); ++i)
        {
            layouts[i].Destroy(device);
        }
        pipelines.Clear();
        shaders.Clear();
        layouts.Clear();
        device.Quiesce();
    }
};

bool ReadAll(const char* path, Vector<byte>* out)
{
    io::Path p;
    return io::Path::Parse(StringView(path), io::NativePathStyle(), &p) && io::ReadFile(p, out).Ok();
}

bool WriteAll(const char* path, const byte* data, usize size)
{
    io::Path p;
    return io::Path::Parse(StringView(path), io::NativePathStyle(), &p) && io::WriteFile(p, data, size).Ok();
}

}  // namespace metal_pipeline_test

using namespace metal_pipeline_test;

TEST_CASE("The Metal pipeline cache file's header refuses another format, adapter or macOS, and damage", "[rhi][metal]")
{
    const u8 archive[5] = {1, 2, 3, 4, 5};
    const u64 identity = MetalArchiveIdentity("00000000000abcde", "Version 15.8.1 (Build 24H32)");
    CHECK(identity != MetalArchiveIdentity("00000000000abcde", "Version 15.8.2 (Build 24H40)"));
    CHECK(identity != MetalArchiveIdentity("00000000000abcdf", "Version 15.8.1 (Build 24H32)"));
    u8 file[kMetalArchiveHeaderBytes + 5];
    WriteMetalArchiveHeader(file, identity, archive, 5);
    MemCopy(file + kMetalArchiveHeaderBytes, archive, 5);
    const u8* got = nullptr;
    usize size = 0;
    const char* why = nullptr;
    REQUIRE(CheckMetalArchiveFile(file, sizeof(file), identity, &got, &size, &why));
    CHECK(size == 5);
    CHECK(MemCompare(got, archive, 5) == 0);
    CHECK_FALSE(CheckMetalArchiveFile(file, sizeof(file), identity + 1, &got, &size, &why));
    CHECK(CStrFind(why, "another adapter") != nullptr);
    CHECK_FALSE(CheckMetalArchiveFile(file, sizeof(file) - 1, identity, &got, &size, &why));  // cut short
    CHECK(CStrFind(why, "damaged") != nullptr);
    file[kMetalArchiveHeaderBytes + 2] ^= 0x5a;
    CHECK_FALSE(CheckMetalArchiveFile(file, sizeof(file), identity, &got, &size, &why));
    CHECK(CStrFind(why, "damaged") != nullptr);
    file[0] = 'X';
    CHECK_FALSE(CheckMetalArchiveFile(file, sizeof(file), identity, &got, &size, &why));
    CHECK(CStrFind(why, "not a Metal pipeline cache") != nullptr);
    CHECK_FALSE(CheckMetalArchiveFile(file, 3, identity, &got, &size, &why));
}

TEST_CASE("Every committed shader forms a Metal pipeline with the layouts its reflection gives", "[rhi][metal][gpu]")
{
    DevicePtr device = MakePipelineDevice(nullptr, false);
    REQUIRE(device);
    Everything all;
    const u32 count = all.Make(*device);
    CHECK(count >= 30);
    const ResourceCounts c = device->Resources();
    CHECK(c.pipelines == count);
    all.Destroy(*device);
    device->WaitIdle();
    CHECK(device->Resources().pipelines == 0);
    CHECK(device->Resources().shaders == 0);
}

TEST_CASE("The Metal vertex descriptor puts each binding at the top of the buffer table", "[rhi][metal][gpu]")
{
    VertexBindingDesc bindings[2] = {{32, false}, {4, true}};
    VertexAttributeDesc attributes[3];
    attributes[0].location = 0;
    attributes[0].format = VertexFormat::Float3;
    attributes[1].location = 1;
    attributes[1].format = VertexFormat::Float2;
    attributes[1].offset = 12;
    attributes[2].location = 15;
    attributes[2].binding = 1;
    attributes[2].format = VertexFormat::UInt1;
    GraphicsPipelineDesc gp;
    gp.vertex_bindings = rhi::Span<VertexBindingDesc>(bindings, 2);
    gp.vertex_attributes = rhi::Span<VertexAttributeDesc>(attributes, 3);
    @autoreleasepool
    {
        MTLVertexDescriptor* v = MakeMetalVertexDescriptor(gp);
        CHECK(v.attributes[0].format == MTLVertexFormatFloat3);
        CHECK(v.attributes[0].bufferIndex == 30);
        CHECK(v.attributes[1].format == MTLVertexFormatFloat2);
        CHECK(v.attributes[1].offset == 12);
        CHECK(v.attributes[15].format == MTLVertexFormatUInt);
        CHECK(v.attributes[15].bufferIndex == 29);
        CHECK(v.layouts[30].stride == 32);
        CHECK(v.layouts[30].stepFunction == MTLVertexStepFunctionPerVertex);
        CHECK(v.layouts[29].stride == 4);
        CHECK(v.layouts[29].stepFunction == MTLVertexStepFunctionPerInstance);
    }
    CHECK(MetalVertexBufferSlot(0) == 30);
    CHECK(MetalVertexBufferSlot(7) == 23);
}

TEST_CASE("Equal Metal depth-stencil descriptions share one state, and compute pipelines carry their threadgroup",
          "[rhi][metal][gpu]")
{
    DevicePtr device = MakePipelineDevice(nullptr, false);
    REQUIRE(device);
    MetalDevice& metal = AsMetal(*device);
    id<MTLDepthStencilState> a = metal.DepthStencilState(true, true, CompareOp::Less);
    CHECK(a != nil);
    CHECK(metal.DepthStencilState(true, true, CompareOp::Less) == a);
    CHECK(metal.DepthStencilState(true, false, CompareOp::Less) != a);
    CHECK(metal.DepthStencilState(true, true, CompareOp::Greater) != a);
    const Module fill = MODULE(kFillComp, "fill.comp");
    const renderer::ShaderReflection cr = Reflect(fill);
    const renderer::ShaderReflection* one[1] = {&cr};
    Layouts layouts = BuildLayouts(*device, one, 1);
    const ShaderHandle cs = MakeShader(*device, fill, ShaderStage::Compute);
    REQUIRE(cs.IsValid());
    ComputePipelineDesc cp;
    cp.shader = cs;
    for (u32 i = 0; i < layouts.count; ++i)
    {
        cp.bind_group_layouts[i] = layouts.handles[i];
    }
    cp.bind_group_layout_count = layouts.count;
    cp.push_constant_bytes = layouts.push_bytes;
    const PipelineHandle pipeline = device->CreateComputePipeline(cp);
    REQUIRE(pipeline.IsValid());
    MetalPipelineInfo info;
    REQUIRE(metal.LookupPipeline(pipeline, &info));
    CHECK(info.compute);
    CHECK(info.threadgroup.width == cr.local_size[0]);
    CHECK(info.threadgroup.height == cr.local_size[1]);
    CHECK(info.threadgroup.depth == cr.local_size[2]);
    CHECK(info.kernel.refs.Size() == cr.binding_count);
    CHECK(info.kernel.push_constant_slot != kNoShaderSlot);
    device->DestroyPipeline(pipeline);
    device->DestroyShader(cs);
    layouts.Destroy(*device);
}

TEST_CASE("Metal refuses SPIR-V, wrong stages and a layout that lacks a shader's binding", "[rhi][metal][gpu]")
{
    DevicePtr device = MakePipelineDevice(nullptr, false);
    REQUIRE(device);
    const bool was = LogRhi.Enabled();
    LogRhi.SetEnabled(false);
    ShaderDesc spirv;
    spirv.stage = ShaderStage::Vertex;
    spirv.SetCode(shaders::kCubeVertShader.For(ShaderFormat::Spirv));
    CHECK_FALSE(device->CreateShader(spirv).IsValid());
    const Module cube_vs = MODULE(kCubeVert, "cube.vert");
    CHECK_FALSE(MakeShader(*device, cube_vs, ShaderStage::Fragment).IsValid());  // a vertex function
    const ShaderHandle vs = MakeShader(*device, cube_vs, ShaderStage::Vertex);
    const ShaderHandle fs = MakeShader(*device, MODULE(kCubeFrag, "cube.frag"), ShaderStage::Fragment);
    REQUIRE((vs.IsValid() && fs.IsValid()));
    VertexBindingDesc binding{24, false};
    VertexAttributeDesc attribute;
    GraphicsPipelineDesc gp;
    gp.vertex_shader = vs;
    gp.fragment_shader = fs;
    gp.vertex_bindings = rhi::Span<VertexBindingDesc>(&binding, 1);
    gp.vertex_attributes = rhi::Span<VertexAttributeDesc>(&attribute, 1);
    gp.color_targets[0].format = Format::RGBA8_UNorm;
    gp.color_target_count = 1;
    CHECK_FALSE(device->CreateGraphicsPipeline(gp).IsValid());  // cube.vert binds set 0, and there is no layout
    // An attribute location past Metal's 31 is refused with a log line, not a range exception inside Metal.
    const renderer::ShaderReflection vr = Reflect(cube_vs);
    const renderer::ShaderReflection* one[1] = {&vr};
    Layouts layouts = BuildLayouts(*device, one, 1);
    for (u32 i = 0; i < layouts.count; ++i)
    {
        gp.bind_group_layouts[i] = layouts.handles[i];
    }
    gp.bind_group_layout_count = layouts.count;
    attribute.location = 40;
    CHECK_FALSE(device->CreateGraphicsPipeline(gp).IsValid());
    attribute.location = 0;
    attribute.binding = 3;  // no such vertex buffer
    CHECK_FALSE(device->CreateGraphicsPipeline(gp).IsValid());
    layouts.Destroy(*device);
    LogRhi.SetEnabled(was);
    device->DestroyShader(vs);
    device->DestroyShader(fs);
}

TEST_CASE("The Metal pipeline cache round-trips, and a damaged or foreign file is discarded", "[rhi][metal][gpu]")
{
    io_test::TempDir dir;
    const engine_test::Text native = dir.Native();
    Vector<byte> saved;
    char path[512] = {};
    u32 count = 0;
    {
        DevicePtr device = MakePipelineDevice(native.CStr());
        REQUIRE(device);
        MetalDevice& metal = AsMetal(*device);
        REQUIRE(metal.PipelineCache().GetStats().enabled);
        CHECK_FALSE(metal.PipelineCache().GetStats().loaded);
        FormatTo(path, sizeof(path), "%s", metal.PipelineCache().Path());
        Everything all;
        count = all.Make(*device);  // Destroy quiesces, which writes the file
        CHECK(metal.PipelineCacheMisses() == count);
        CHECK(metal.PipelineCacheHits() == 0);
        all.Destroy(*device);
        CHECK(metal.PipelineCache().GetStats().saved_bytes > kMetalArchiveHeaderBytes);
    }
    REQUIRE(ReadAll(path, &saved));
    {
        DevicePtr device = MakePipelineDevice(native.CStr());
        REQUIRE(device);
        MetalDevice& metal = AsMetal(*device);
        CHECK(metal.PipelineCache().GetStats().loaded);
        Everything all;
        CHECK(all.Make(*device) == count);
        CHECK(metal.PipelineCacheHits() == count);  // every pipeline from the archive
        CHECK(metal.PipelineCacheMisses() == 0);
        all.Destroy(*device);
    }
    {
        // Two devices on one adapter and cache: each has its own copy of the archive, so the first one's end does not
        // take the second one's away.
        DevicePtr first = MakePipelineDevice(native.CStr());
        DevicePtr second = MakePipelineDevice(native.CStr());
        REQUIRE((first && second));
        CHECK(CStrCompare(AsMetal(*first).PipelineCache().Path(), AsMetal(*second).PipelineCache().Path()) == 0);
        first.Reset();
        Everything all;
        CHECK(all.Make(*second) == count);
        CHECK(AsMetal(*second).PipelineCacheHits() == count);
        all.Destroy(*second);
    }
    const usize flips[] = {saved.Size() - 7, 17};  // the archive's bytes, the identity
    for (usize at : flips)
    {
        INFO("flipped byte " << at);
        Vector<byte> bad;
        bad.Resize(saved.Size());
        MemCopy(bad.Data(), saved.Data(), saved.Size());
        bad[at] ^= 0x5a;
        REQUIRE(WriteAll(path, bad.Data(), bad.Size()));
        const bool was = LogRhi.Enabled();
        LogRhi.SetEnabled(false);
        DevicePtr device = MakePipelineDevice(native.CStr());
        LogRhi.SetEnabled(was);
        REQUIRE(device);
        MetalDevice& metal = AsMetal(*device);
        CHECK(metal.PipelineCache().GetStats().discarded);
        CHECK_FALSE(metal.PipelineCache().GetStats().loaded);
        Everything all;
        CHECK(all.Make(*device) == count);  // compiled again, and fine
        all.Destroy(*device);
    }
}
