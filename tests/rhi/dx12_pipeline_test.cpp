// The Direct3D 12 backend's shaders, pipelines and pipeline cache (Phase 13 Day 167): the cache file's rules, every
// committed shader as a pipeline whose layouts come from its reflection, the refusals, the cache's round trip and its
// damaged files, and cold against warm creation. Windows only.
#include "cstr/format.h"
#include "cstr/cstr.h"

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
#include "rhi/dx12/dx12_debug.h"
#include "rhi/dx12/dx12_device.h"
#include "rhi/dx12/dx12_pipeline_cache.h"
#include "os/os.h"
#include "rhi/rhi.h"
#include "cstr/mem_ops.h"

using namespace vse;
using namespace vse::rhi;
using namespace vse::rhi::dx12;

namespace dx12_pipeline_test
{

struct PipelineSilence
{
    PipelineSilence() { ResetDebugMessageCounts(); }
    ~PipelineSilence()
    {
        const DebugMessageCounts c = GetDebugMessageCounts();
        CHECK(c.errors == 0);
        CHECK(c.warnings == 0);
    }
};

Dx12DevicePtr MakePipelineDevice(const char* cache_dir, bool cache = true)
{
    DeviceCreateInfo info;
    info.backend = Backend::Dx12;
    info.application_name = "dx12-pipeline-test";
    info.validation = ValidationSetting::On;
    info.pipeline_cache = cache;
    info.pipeline_cache_dir = cache_dir;
    return Dx12Device::Create(info);
}

// One minted module: its DXIL and its reflection.
struct Module
{
    const char* name;
    const u8* dxil;
    usize dxil_size;
    const u8* refl;
    usize refl_size;
};

#define MODULE(base, name)                                                                                             \
    Module                                                                                                             \
    {                                                                                                                  \
        name, shaders::base##Dxil, shaders::base##DxilByteCount, shaders::base##Refl, shaders::base##ReflByteCount     \
    }
#define PERM(base, i, name)                                                                                            \
    Module                                                                                                             \
    {                                                                                                                  \
        name, shaders::base##Modules[i].dxil, shaders::base##Modules[i].dxil_size,                                     \
            shaders::base##Modules[i].reflection, shaders::base##Modules[i].reflection_size                            \
    }

const Module kFullscreen = MODULE(kFullscreenVert, "fullscreen.vert");
const Module kGbufferVert[2] = {PERM(kGbufferVert, 0, "gbuffer.vert"),
                                PERM(kGbufferVert, 1, "gbuffer.vert VERTEX_COLOR")};

// Every fragment module with the vertex module it runs after (D3D12 checks that the stages' signatures link).
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

ShaderHandle MakeShader(Dx12Device& device, const Module& m, ShaderStage stage)
{
    ShaderDesc sd;
    sd.stage = stage;
    sd.SetCode(DxilBlob(m.dxil, m.dxil_size));
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

    void Destroy(Dx12Device& device)
    {
        for (u32 i = 0; i < count; ++i)
        {
            device.DestroyBindGroupLayout(handles[i]);
        }
        count = 0;
    }
};

Layouts BuildLayouts(Dx12Device& device, const renderer::ShaderReflection* const* shaders, u32 shader_count)
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
u32 MakeEveryPipeline(Dx12Device& device, Vector<PipelineHandle>* pipelines, Vector<ShaderHandle>* shaders_out,
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
        // The G-buffer's four targets for gbuffer.frag (D3D12 warns about a target with more channels than the shader
        // writes), else one.
        if (CStrCompareN(pair.fs.name, "gbuffer.frag", 12) == 0)
        {
            gp.color_targets[0].format = renderer::kGBufferAlbedoFormat;
            gp.color_targets[1].format = renderer::kGBufferNormalFormat;
            gp.color_targets[2].format = renderer::kGBufferMaterialFormat;
            gp.color_targets[3].format = renderer::kGBufferEmissiveFormat;
            gp.color_target_count = 4;
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
    u32 Make(Dx12Device& device) { return MakeEveryPipeline(device, &pipelines, &shaders, &layouts); }
    void Destroy(Dx12Device& device)
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

}  // namespace dx12_pipeline_test

using namespace dx12_pipeline_test;

TEST_CASE(
    "The DX12 pipeline cache file is named per adapter and driver, and a bad file is refused before D3D12 reads it",
    "[rhi][dx12]")
{
    char path[256];
    REQUIRE(BuildPipelineLibraryPath("C:/cache", "dx12-1002-1114-50071d05-c2", "32.0.21030.2001", path, sizeof(path)));
    CHECK(CStrCompare(path, "C:/cache/pipelines_dx12-1002-1114-50071d05-c2_32_0_21030_2001.bin") == 0);
    REQUIRE(BuildPipelineLibraryPath("C:/cache/", "a", "", path, sizeof(path)));
    CHECK(CStrCompare(path, "C:/cache/pipelines_a_nodriver.bin") == 0);
    char tiny[8];
    CHECK_FALSE(BuildPipelineLibraryPath("C:/cache", "a", "1", tiny, sizeof(tiny)));
    CHECK(tiny[0] == '\0');

    wchar_t name[18];
    PipelineLibraryName(0x0123456789abcdefull, name);
    CHECK(WStrCompare(name, L"p0123456789abcdef") == 0);

    const u64 identity = PipelineLibraryIdentity("dx12-1002-1114-50071d05-c2", "32.0.21030.2001");
    CHECK(identity != PipelineLibraryIdentity("dx12-1002-1114-50071d05-c2", "32.0.21030.2002"));
    CHECK(PipelineLibraryIdentity("ab", "c") != PipelineLibraryIdentity("a", "bc"));
    const char payload[] = "a serialized pipeline library";
    Vector<byte> file;
    file.Resize(kPipelineLibraryHeaderBytes + sizeof(payload));
    MemCopy(file.Data() + kPipelineLibraryHeaderBytes, payload, sizeof(payload));
    WritePipelineLibraryHeader(file.Data(), identity, 0, payload, sizeof(payload));
    PipelineLibraryParts parts;
    CHECK(CheckPipelineLibraryFile(file.Data(), file.Size(), identity, &parts) == nullptr);
    CHECK(parts.key_count == 0);
    CHECK(parts.library_size == sizeof(payload));
    CHECK(CheckPipelineLibraryFile(file.Data(), file.Size(), identity + 1) != nullptr);  // another adapter or driver
    CHECK(CheckPipelineLibraryFile(file.Data(), file.Size() - 1, identity) != nullptr);  // truncated
    CHECK(CheckPipelineLibraryFile(file.Data(), 12, identity) != nullptr);               // shorter than the header
    file[kPipelineLibraryHeaderBytes + 3] ^= 0x40;
    CHECK(CheckPipelineLibraryFile(file.Data(), file.Size(), identity) != nullptr);  // damaged
    file[kPipelineLibraryHeaderBytes + 3] ^= 0x40;
    file[0] = 'X';
    CHECK(CheckPipelineLibraryFile(file.Data(), file.Size(), identity) != nullptr);  // not ours

    // Keys, then the library; a key count that leaves no library is refused.
    Vector<byte> keyed;
    keyed.Resize(kPipelineLibraryHeaderBytes + 16 + sizeof(payload));
    const u64 keys[2] = {0x1111222233334444ull, 0x5555666677778888ull};
    MemCopy(keyed.Data() + kPipelineLibraryHeaderBytes, keys, 16);  // little-endian on every target
    MemCopy(keyed.Data() + kPipelineLibraryHeaderBytes + 16, payload, sizeof(payload));
    WritePipelineLibraryHeader(keyed.Data(), identity, 2, keyed.Data() + kPipelineLibraryHeaderBytes,
                               16 + sizeof(payload));
    REQUIRE(CheckPipelineLibraryFile(keyed.Data(), keyed.Size(), identity, &parts) == nullptr);
    CHECK(parts.key_count == 2);
    CHECK(PipelineLibraryKey(parts, 1) == keys[1]);
    CHECK(parts.library_size == sizeof(payload));
    WritePipelineLibraryHeader(keyed.Data(), identity, 9, keyed.Data() + kPipelineLibraryHeaderBytes,
                               16 + sizeof(payload));
    CHECK(CheckPipelineLibraryFile(keyed.Data(), keyed.Size(), identity) != nullptr);

    Vector<u64> sorted;
    CHECK(InsertSortedKey(&sorted, 30));
    CHECK(InsertSortedKey(&sorted, 10));
    CHECK(InsertSortedKey(&sorted, 20));
    CHECK_FALSE(InsertSortedKey(&sorted, 20));
    REQUIRE(sorted.Size() == 3);
    CHECK((sorted[0] == 10 && sorted[1] == 20 && sorted[2] == 30));
    CHECK(HasSortedKey(sorted, 20));
    CHECK_FALSE(HasSortedKey(sorted, 25));
}

TEST_CASE("Every committed shader forms a DX12 pipeline with the layouts its reflection gives, the debug layer silent",
          "[rhi][dx12][gpu]")
{
    io_test::TempDir dir;
    Dx12DevicePtr device = MakePipelineDevice(dir.Native().CStr());
    REQUIRE(device);
    PipelineSilence silence;
    Everything all;
    const u32 made = all.Make(*device);
    CHECK(made == 33);  // 26 pairs, 4 depth-only, 3 compute
    CHECK(device->Resources().pipelines == made);
    Dx12Device::PipelineInfo info;
    REQUIRE(device->LookupPipeline(all.pipelines[0], &info));
    CHECK(info.state != nullptr);
    CHECK(info.root.signature != nullptr);
    CHECK(info.color_count == 1);
    CHECK(info.depth_format == Format::D32_Float);
    CHECK(info.topology == D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    CHECK_FALSE(info.compute);
    REQUIRE(device->LookupPipeline(all.pipelines[all.pipelines.Size() - 1], &info));
    CHECK(info.compute);
    all.Destroy(*device);
    CHECK(device->Resources().pipelines == 0);
    CHECK(device->Resources().shaders == 0);
}

TEST_CASE("DX12 refuses SPIR-V, wrong stages, unrenderable formats and destroyed layouts cleanly", "[rhi][dx12][gpu]")
{
    Dx12DevicePtr device = MakePipelineDevice(nullptr, false);
    REQUIRE(device);
    PipelineSilence silence;
    ShaderDesc spirv;
    spirv.stage = ShaderStage::Vertex;
    spirv.SetCode(SpirvBlob(shaders::kTriangleVertSpv, shaders::kTriangleVertSpvWordCount));
    spirv.debug_name = "triangle.vert";
    CHECK_FALSE(device->CreateShader(spirv).IsValid());

    const ShaderHandle vs = MakeShader(*device, MODULE(kTriangleVert, "triangle.vert"), ShaderStage::Vertex);
    const ShaderHandle fs = MakeShader(*device, MODULE(kTriangleFrag, "triangle.frag"), ShaderStage::Fragment);
    REQUIRE((vs.IsValid() && fs.IsValid()));
    renderer::VertexLayoutData vl;
    renderer::ReflectionError error;
    REQUIRE(renderer::BuildVertexLayout(Reflect(MODULE(kTriangleVert, "triangle.vert")), 0, &vl, &error));
    VertexBindingDesc binding = {vl.stride, false};
    GraphicsPipelineDesc gp;
    gp.vertex_shader = vs;
    gp.fragment_shader = fs;
    gp.vertex_bindings = rhi::Span<VertexBindingDesc>(&binding, 1);
    gp.vertex_attributes = rhi::Span<VertexAttributeDesc>(vl.attributes, vl.attribute_count);
    gp.color_targets[0].format = Format::RGBA8_UNorm;
    gp.color_target_count = 1;
    const PipelineHandle good = device->CreateGraphicsPipeline(gp);
    CHECK(good.IsValid());

    GraphicsPipelineDesc swapped = gp;
    swapped.vertex_shader = fs;
    swapped.fragment_shader = vs;
    CHECK_FALSE(device->CreateGraphicsPipeline(swapped).IsValid());
    GraphicsPipelineDesc compressed = gp;
    compressed.color_targets[0].format = Format::BC1_UNorm;
    CHECK_FALSE(device->CreateGraphicsPipeline(compressed).IsValid());
    BindGroupLayoutDesc empty;
    const BindGroupLayoutHandle gone = device->CreateBindGroupLayout(empty);
    REQUIRE(gone.IsValid());
    device->DestroyBindGroupLayout(gone);
    GraphicsPipelineDesc stale = gp;
    stale.bind_group_layouts[0] = gone;
    stale.bind_group_layout_count = 1;
    CHECK_FALSE(device->CreateGraphicsPipeline(stale).IsValid());
    ComputePipelineDesc not_compute;
    not_compute.shader = vs;
    CHECK_FALSE(device->CreateComputePipeline(not_compute).IsValid());

    // A pipeline outlives its shaders, and a strip with restart has a second state for 16-bit indices.
    GraphicsPipelineDesc strip = gp;
    strip.topology = PrimitiveTopology::TriangleStrip;
    strip.primitive_restart = true;
    const PipelineHandle restart = device->CreateGraphicsPipeline(strip);
    REQUIRE(restart.IsValid());
    device->DestroyShader(vs);
    device->DestroyShader(fs);
    Dx12Device::PipelineInfo info;
    REQUIRE(device->LookupPipeline(restart, &info));
    CHECK(info.state_cut16 != nullptr);
    CHECK(info.topology == D3D_PRIMITIVE_TOPOLOGY_TRIANGLESTRIP);
    REQUIRE(device->LookupPipeline(good, &info));
    CHECK(info.state_cut16 == nullptr);
    CHECK(info.vertex_binding_count == 1);
    CHECK(info.vertex_strides[0] == vl.stride);
    device->DestroyPipeline(good);
    device->DestroyPipeline(restart);
    device->Quiesce();
    CHECK(device->Resources().UserObjects() == 0);
}

TEST_CASE("The DX12 pipeline cache round-trips, and a truncated, damaged or foreign file is discarded",
          "[rhi][dx12][gpu]")
{
    io_test::TempDir dir;
    const engine_test::Text native = dir.Native();
    Vector<byte> saved;
    char path[512] = {};
    u32 count = 0;
    {
        Dx12DevicePtr device = MakePipelineDevice(native.CStr());
        REQUIRE(device);
        REQUIRE(device->PipelineCache().enabled);
        FormatTo(path, sizeof(path), "%s", device->PipelineCachePath());
        Everything all;
        count = all.Make(*device);
        const Dx12Device::PipelineCacheStats cold = device->PipelineCache();
        CHECK(cold.hits == 0);
        CHECK(cold.misses == count);
        CHECK(cold.loaded_bytes == 0);
        all.Destroy(*device);
        CHECK(device->SavePipelineCache());
        CHECK_FALSE(device->SavePipelineCache());  // nothing new since
    }
    REQUIRE(ReadAll(path, &saved));
    CHECK(saved.Size() > kPipelineLibraryHeaderBytes);
    {
        Dx12DevicePtr device = MakePipelineDevice(native.CStr());
        REQUIRE(device);
        PipelineSilence silence;
        CHECK(device->PipelineCache().loaded_bytes == saved.Size());
        Everything all;
        all.Make(*device);
        const Dx12Device::PipelineCacheStats warm = device->PipelineCache();
        CHECK(warm.hits == count);
        CHECK(warm.misses == 0);
        CHECK_FALSE(warm.rejected);
        all.Destroy(*device);
        CHECK_FALSE(device->SavePipelineCache());  // every pipeline came from the file: nothing to write
    }
    struct Damage
    {
        const char* what;
        usize size;     // bytes kept
        usize flip_at;  // a byte flipped, or ~0
    };
    const Damage damages[] = {{"truncated", saved.Size() / 2, ~usize{0}},
                              {"damaged", saved.Size(), saved.Size() - 7},
                              {"another adapter or driver", saved.Size(), 17}};
    for (const Damage& d : damages)
    {
        INFO(d.what);
        Vector<byte> bad;
        bad.Resize(d.size);
        MemCopy(bad.Data(), saved.Data(), d.size);
        if (d.flip_at != ~usize{0})
        {
            bad[d.flip_at] ^= 0x5a;
        }
        REQUIRE(WriteAll(path, bad.Data(), bad.Size()));
        Dx12DevicePtr device = MakePipelineDevice(native.CStr());
        REQUIRE(device);
        PipelineSilence silence;
        const Dx12Device::PipelineCacheStats s = device->PipelineCache();
        CHECK(s.rejected);
        CHECK(s.enabled);
        CHECK(s.loaded_bytes == 0);
        Everything all;
        CHECK(all.Make(*device) == count);  // compiled again, and fine
        all.Destroy(*device);
    }
}

TEST_CASE(
    "Creating every committed pipeline on DX12: cold, warm from the library, and from cached blobs (measured, not asserted)",
    "[rhi][dx12][gpu][bench]")
{
    io_test::TempDir dir;
    const engine_test::Text native = dir.Native();
    auto time_all = [&](f64* ms, Dx12Device::PipelineCacheStats* stats)
    {
        Dx12DevicePtr device = MakePipelineDevice(native.CStr());
        REQUIRE(device);
        Everything all;
        const u64 start = os::MonotonicTimeNanos();
        all.Make(*device);
        *ms = (static_cast<f64>(os::MonotonicTimeNanos() - start) / 1e6);
        *stats = device->PipelineCache();
        all.Destroy(*device);
    };
    f64 cold = 0, warm = 0;
    Dx12Device::PipelineCacheStats cold_stats, warm_stats;
    time_all(&cold, &cold_stats);
    time_all(&warm, &warm_stats);
    // The other candidate: each state's own cached blob (GetCachedBlob, then CachedPSO), timed on one pipeline made
    // many times.
    Dx12DevicePtr device = MakePipelineDevice(nullptr, false);
    REQUIRE(device);
    const Module vsm = kGbufferVert[0],
                 fsm = PERM(kGbufferFrag, 3, "gbuffer.frag NORMAL_MAP ALPHA_TEST");  // no vertex color on either
    const renderer::ShaderReflection vr = Reflect(vsm), fr = Reflect(fsm);
    const renderer::ShaderReflection* both[2] = {&vr, &fr};
    Layouts layouts = BuildLayouts(*device, both, 2);
    Dx12Device::RootLayout root;
    REQUIRE(device->RootSignatureFor(layouts.handles, layouts.count, layouts.push_bytes, layouts.push_stages, false,
                                     &root));
    renderer::VertexLayoutData vl;
    renderer::ReflectionError error;
    REQUIRE(renderer::BuildVertexLayout(vr, 0, &vl, &error));
    D3D12_INPUT_ELEMENT_DESC elements[kMaxVertexAttributes + 1] = {};
    for (u32 i = 0; i < vl.attribute_count; ++i)
    {
        elements[i] = {"TEXCOORD",
                       vl.attributes[i].location,
                       vl.attributes[i].format == VertexFormat::Float2   ? DXGI_FORMAT_R32G32_FLOAT
                       : vl.attributes[i].format == VertexFormat::Float4 ? DXGI_FORMAT_R32G32B32A32_FLOAT
                                                                         : DXGI_FORMAT_R32G32B32_FLOAT,
                       0,
                       vl.attributes[i].offset,
                       D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA,
                       0};
    }
    elements[vl.attribute_count] = {
        "TEXCOORD", 15, DXGI_FORMAT_R32_UINT, 1, 0, D3D12_INPUT_CLASSIFICATION_PER_INSTANCE_DATA, 1};
    D3D12_GRAPHICS_PIPELINE_STATE_DESC d = {};
    d.pRootSignature = root.signature;
    d.VS = {vsm.dxil, vsm.dxil_size};
    d.PS = {fsm.dxil, fsm.dxil_size};
    d.InputLayout = {elements, vl.attribute_count + 1};
    d.RasterizerState.FillMode = D3D12_FILL_MODE_SOLID;
    d.RasterizerState.CullMode = D3D12_CULL_MODE_BACK;
    d.RasterizerState.DepthClipEnable = TRUE;
    const DXGI_FORMAT gbuffer[4] = {DXGI_FORMAT_R8G8B8A8_UNORM_SRGB, DXGI_FORMAT_R16G16_FLOAT,
                                    DXGI_FORMAT_R8G8B8A8_UNORM, DXGI_FORMAT_R16G16B16A16_FLOAT};
    for (u32 i = 0; i < 4; ++i)
    {
        d.BlendState.RenderTarget[i].RenderTargetWriteMask = D3D12_COLOR_WRITE_ENABLE_ALL;
        d.RTVFormats[i] = gbuffer[i];
    }
    d.NumRenderTargets = 4;
    d.DepthStencilState.DepthEnable = TRUE;
    d.DepthStencilState.DepthWriteMask = D3D12_DEPTH_WRITE_MASK_ALL;
    d.DepthStencilState.DepthFunc = D3D12_COMPARISON_FUNC_LESS;
    d.DSVFormat = DXGI_FORMAT_D32_FLOAT;
    d.SampleMask = UINT_MAX;
    d.SampleDesc.Count = 1;
    d.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
    const int kRuns = 20;
    ComRef<ID3D12PipelineState> first;
    u64 t0 = os::MonotonicTimeNanos();
    for (int i = 0; i < kRuns; ++i)
    {
        ComRef<ID3D12PipelineState> s;
        REQUIRE(SUCCEEDED(device->D3D()->CreateGraphicsPipelineState(&d, __uuidof(ID3D12PipelineState), s.PutVoid())));
        if (i == 0)
        {
            first = static_cast<ComRef<ID3D12PipelineState>&&>(s);
        }
    }
    const f64 plain_ms = (static_cast<f64>(os::MonotonicTimeNanos() - t0) / 1e6) / kRuns;
    ComRef<ID3DBlob> blob;
    REQUIRE(SUCCEEDED(first->GetCachedBlob(blob.Put())));
    d.CachedPSO = {blob->GetBufferPointer(), blob->GetBufferSize()};
    t0 = os::MonotonicTimeNanos();
    for (int i = 0; i < kRuns; ++i)
    {
        ComRef<ID3D12PipelineState> s;
        REQUIRE(SUCCEEDED(device->D3D()->CreateGraphicsPipelineState(&d, __uuidof(ID3D12PipelineState), s.PutVoid())));
    }
    const f64 blob_ms = (static_cast<f64>(os::MonotonicTimeNanos() - t0) / 1e6) / kRuns;
    layouts.Destroy(*device);
    WARN("all " << cold_stats.misses << " pipelines: cold " << cold << " ms, warm from the library " << warm << " ms ("
                << warm_stats.hits << " hits); one G-buffer pipeline: " << plain_ms << " ms plain, " << blob_ms
                << " ms from its cached blob (" << blob->GetBufferSize() << " bytes)");
}
