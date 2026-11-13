// Phase 5 Day 74: the material system. Templates and materials are validated against the shaders' reflection; pipelines
// are cached; bind groups are rebuilt only for the material whose texture changed. A GPU test draws two objects through
// two materials and reads the pixels back, which also proves the per-object data is indexed by first_instance.
#include "lang/new.h"
#include "os/console.h"

#include <catch2/catch_test_macros.hpp>

#include "backend_fixture.h"

#include "material_test.frag.perm.inc"
#include "material_test.vert.refl.inc"
#include "material_test.vert.dxil.inc"
#include "material_test.vert.spv.inc"
#include "lit.frag.refl.inc"
#include "lit.frag.dxil.inc"
#include "lit.frag.spv.inc"
#include "permute_test.frag.refl.inc"
#include "permute_test.frag.dxil.inc"
#include "permute_test.frag.spv.inc"
#include "renderer/camera.h"
#include "renderer/material.h"
#include "renderer/render_scene.h"
#include "rhi/null/null_device.h"
#include "lang/initializer_list.h"
#include "cstr/mem_ops.h"

using namespace engine_placeholder;
using namespace engine_placeholder::renderer;

namespace
{

// The CPU mirror of material_test.frag's MaterialParams (std140).
struct MaterialParams
{
    f32 base_color[4];
    f32 roughness;
    u32 flags;
    u32 pad[2];
    f32 emissive[3];
    f32 pad2;  // Metal's float3 is 16 bytes: nothing shares them
    Mat4 uv_transform;
};
static_assert(sizeof(MaterialParams) == 112);

PermutationModule VertexModule()
{
    PermutationModule m;
    m.words = shaders::kMaterialTestVertSpv;
    m.word_count = shaders::kMaterialTestVertSpvWordCount;
    m.reflection = shaders::kMaterialTestVertRefl;
    m.reflection_size = shaders::kMaterialTestVertReflByteCount;
    m.dxil = shaders::kMaterialTestVertDxil;
    m.dxil_size = shaders::kMaterialTestVertDxilByteCount;
    m.metallib = shaders::kMaterialTestVertMetallib;
    m.metallib_size = shaders::kMaterialTestVertMetallibByteCount;
    m.metallib_entry = shaders::kMaterialTestVertMetallibEntry;
    m.metallib_slots = &shaders::kMaterialTestVertMetallibSlots;
    return m;
}

MaterialTemplateDesc TestTemplate()
{
    MaterialTemplateDesc d;
    d.name = "material_test";
    d.vertex = VertexModule();
    d.fragment_permutations = &shaders::kMaterialTestFragPermutations;
    d.state.cull = rhi::CullMode::None;
    d.state.depth_test = false;
    d.state.depth_write = false;
    return d;
}

PermutationKey Key(InitializerList<const char*> names)
{
    PermutationKey key;
    ReflectionError error;
    REQUIRE(MakePermutationKey(shaders::kMaterialTestFragPermutations, names.begin(), static_cast<u32>(names.size()),
                               &key, &error));
    return key;
}

struct Fixture
{
    rhi::NullDevicePtr device = rhi::CreateNullDevice();
};

PipelineTarget ColorTarget(rhi::Format f = rhi::Format::RGBA8_UNorm)
{
    PipelineTarget t;
    t.color_formats[0] = f;
    t.color_count = 1;
    return t;
}

rhi::TextureHandle MakeTexture(rhi::Device* device)
{
    rhi::TextureDesc td;
    td.width = 1;
    td.height = 1;
    td.format = rhi::Format::RGBA8_UNorm;
    td.usage = rhi::TextureUsage::Sampled | rhi::TextureUsage::CopyDst;
    const rhi::TextureHandle t = device->CreateTexture(td);
    const u8 px[4] = {10, 20, 30, 255};
    device->UploadTexture(t, 0, 0, px, 0);
    return t;
}

}  // namespace

TEST_CASE("A template and a material are built from reflection", "[renderer][material]")
{
    Fixture f;
    MaterialSystem sys(f.device.Get(), MaterialSystemDesc{});
    REQUIRE(sys.Valid());
    const TemplateId t = sys.CreateTemplate(TestTemplate());
    REQUIRE(t.IsValid());
    const MaterialId m = sys.CreateMaterial(t, Key({}), "a");
    REQUIRE(m.IsValid());
    CHECK(sys.IsAlive(m));
    CHECK(sys.MaterialLayout(m).IsValid());
    CHECK(sys.FrameLayout().IsValid());
    CHECK(sys.ObjectLayout().IsValid());
    sys.DestroyMaterial(m);
    CHECK_FALSE(sys.IsAlive(m));
    sys.DestroyTemplate(t);
}

TEST_CASE("Parameter writes land at the shader's std140 offsets", "[renderer][material]")
{
    Fixture f;
    MaterialSystem sys(f.device.Get(), MaterialSystemDesc{});
    const TemplateId t = sys.CreateTemplate(TestTemplate());
    const MaterialId m = sys.CreateMaterial(t, Key({}), "params");
    REQUIRE(m.IsValid());

    CHECK(sys.SetVec4(m, "base_color", Vec4(0.1f, 0.2f, 0.3f, 0.4f)));
    CHECK(sys.SetVec3(m, "emissive", Vec3(1, 2, 3)));
    CHECK(sys.SetFloat(m, "roughness", 0.75f));
    CHECK(sys.SetUInt(m, "flags", 7));
    // A matrix that is not its own transpose, so a layout mix-up shows.
    const Mat4 skew = Mat4::FromColumns(Vec4(1, 2, 3, 4), Vec4(5, 6, 7, 8), Vec4(9, 10, 11, 12), Vec4(13, 14, 15, 16));
    CHECK(sys.SetMat4(m, "uv_transform", skew));

    sys.BeginFrame(0);
    sys.FlushMaterials();
    const BufferRegion region = sys.ParameterRegion(m);
    REQUIRE(region.buffer.IsValid());
    CHECK(region.size == sizeof(MaterialParams));
    MaterialParams got;
    REQUIRE(f.device->PeekBuffer(region.buffer, region.offset, &got, sizeof(got)));
    CHECK(ENGINE_OFFSET_OF(MaterialParams, roughness) == 16);
    CHECK(ENGINE_OFFSET_OF(MaterialParams, flags) == 20);
    CHECK(ENGINE_OFFSET_OF(MaterialParams, emissive) == 32);
    CHECK(ENGINE_OFFSET_OF(MaterialParams, uv_transform) == 48);
    CHECK(got.base_color[0] == 0.1f);
    CHECK(got.base_color[3] == 0.4f);
    CHECK(got.emissive[0] == 1.0f);
    CHECK(got.emissive[2] == 3.0f);
    CHECK(got.roughness == 0.75f);
    CHECK(got.flags == 7);
    CHECK(MemCompare(&got.uv_transform, &skew, sizeof(Mat4)) == 0);  // column-major in memory, as the shader reads it

    // The reflected block agrees with the C++ mirror.
    ShaderReflection fs;
    const PermutationModule* module = &shaders::kMaterialTestFragModules[0];
    REQUIRE(ParseShaderReflection(module->reflection, module->reflection_size, &fs) == nullptr);
    ReflectionError error;
    CHECK(CheckBlockSize(fs, "MaterialParams", sizeof(MaterialParams), &error));
    INFO(error.message);
}

TEST_CASE("Values that do not match the reflected block are rejected", "[renderer][material]")
{
    Fixture f;
    MaterialSystem sys(f.device.Get(), MaterialSystemDesc{});
    const TemplateId t = sys.CreateTemplate(TestTemplate());
    const MaterialId m = sys.CreateMaterial(t, Key({}), "bad");
    CHECK_FALSE(sys.SetFloat(m, "base_color", 1.0f));               // a float4 is not a float
    CHECK_FALSE(sys.SetVec4(m, "uv_transform", Vec4(0, 0, 0, 0)));  // a float4x4 is not a float4
    CHECK_FALSE(sys.SetVec3(m, "base_color", Vec3(1, 1, 1)));       // nor a float3
    CHECK_FALSE(sys.SetInt(m, "flags", 1));                         // flags is a uint
    CHECK_FALSE(sys.SetVec4(m, "no_such_member", Vec4(0, 0, 0, 0)));
    CHECK_FALSE(sys.SetMat4(m, "roughness", Mat4::Identity()));
    CHECK_FALSE(sys.SetTexture(m, "no_such_texture", rhi::TextureHandle{}));
    CHECK_FALSE(sys.SetSampler(m, "albedo_tex", rhi::SamplerHandle{}));  // a texture, not a sampler
    CHECK(sys.SetTexture(m, "albedo_tex", rhi::TextureHandle{}));
    CHECK(sys.SetSampler(m, "linear_sampler", rhi::SamplerHandle{}));
}

TEST_CASE("Templates and materials that break the conventions are rejected", "[renderer][material]")
{
    Fixture f;
    MaterialSystem sys(f.device.Get(), MaterialSystemDesc{});

    SECTION("a fragment reflection given as the vertex stage")
    {
        MaterialTemplateDesc d = TestTemplate();
        d.vertex.reflection = shaders::kLitFragRefl;
        d.vertex.reflection_size = shaders::kLitFragReflByteCount;
        CHECK_FALSE(sys.CreateTemplate(d).IsValid());
    }
    SECTION("a corrupt reflection blob")
    {
        MaterialTemplateDesc d = TestTemplate();
        d.vertex.reflection_size = 5;
        CHECK_FALSE(sys.CreateTemplate(d).IsValid());
    }
    SECTION("a missing fragment")
    {
        MaterialTemplateDesc d = TestTemplate();
        d.fragment_permutations = nullptr;
        CHECK_FALSE(sys.CreateTemplate(d).IsValid());
    }
    SECTION("a permutation key that was not built")
    {
        const TemplateId t = sys.CreateTemplate(TestTemplate());
        PermutationKey unbuilt;
        unbuilt.bits = 2;
        CHECK_FALSE(sys.CreateMaterial(t, unbuilt, "x").IsValid());
    }
    SECTION("a fragment shader that puts a texture in set 0")
    {
        MaterialTemplateDesc d = TestTemplate();
        d.fragment_permutations = nullptr;
        d.fragment.words = shaders::kLitFragSpv;
        d.fragment.word_count = shaders::kLitFragSpvWordCount;
        d.fragment.reflection = shaders::kLitFragRefl;
        d.fragment.reflection_size = shaders::kLitFragReflByteCount;
        d.fragment.dxil = shaders::kLitFragDxil;
        d.fragment.dxil_size = shaders::kLitFragDxilByteCount;
        d.fragment.metallib = shaders::kLitFragMetallib;
        d.fragment.metallib_size = shaders::kLitFragMetallibByteCount;
        d.fragment.metallib_entry = shaders::kLitFragMetallibEntry;
        d.fragment.metallib_slots = &shaders::kLitFragMetallibSlots;
        const TemplateId t = sys.CreateTemplate(d);
        REQUIRE(t.IsValid());  // the vertex stage is fine; the fragment is checked per material
        CHECK_FALSE(sys.CreateMaterial(t, PermutationKey{}, "x").IsValid());
    }
    SECTION("more texture defaults than texture slots")
    {
        MaterialTemplateDesc d = TestTemplate();
        TextureDefault many[kMaxMaterialTextures + 1];
        d.texture_defaults = many;
        d.texture_default_count = kMaxMaterialTextures + 1;
        CHECK_FALSE(sys.CreateTemplate(d).IsValid());
    }
    SECTION("the template list and material list are bounded")
    {
        MaterialSystemDesc small;
        small.max_templates = 1;
        small.max_materials = 1;
        MaterialSystem tiny(f.device.Get(), small);
        const TemplateId t = tiny.CreateTemplate(TestTemplate());
        REQUIRE(t.IsValid());
        CHECK_FALSE(tiny.CreateTemplate(TestTemplate()).IsValid());
        CHECK(tiny.CreateMaterial(t, Key({}), "one").IsValid());
        CHECK_FALSE(tiny.CreateMaterial(t, Key({}), "two").IsValid());
    }
}

TEST_CASE("A fragment shader without material resources still makes a material", "[renderer][material]")
{
    Fixture f;
    MaterialSystem sys(f.device.Get(), MaterialSystemDesc{});
    MaterialTemplateDesc d = TestTemplate();
    d.fragment_permutations = nullptr;
    d.fragment.words = shaders::kPermuteTestFragSpv;
    d.fragment.word_count = shaders::kPermuteTestFragSpvWordCount;
    d.fragment.reflection = shaders::kPermuteTestFragRefl;
    d.fragment.reflection_size = shaders::kPermuteTestFragReflByteCount;
    d.fragment.dxil = shaders::kPermuteTestFragDxil;
    d.fragment.dxil_size = shaders::kPermuteTestFragDxilByteCount;
    d.fragment.metallib = shaders::kPermuteTestFragMetallib;
    d.fragment.metallib_size = shaders::kPermuteTestFragMetallibByteCount;
    d.fragment.metallib_entry = shaders::kPermuteTestFragMetallibEntry;
    d.fragment.metallib_slots = &shaders::kPermuteTestFragMetallibSlots;
    const TemplateId t = sys.CreateTemplate(d);
    REQUIRE(t.IsValid());
    const MaterialId m = sys.CreateMaterial(t, PermutationKey{}, "flat");
    REQUIRE(m.IsValid());
    sys.BeginFrame(0);
    sys.FlushMaterials();
    CHECK(sys.MaterialBindGroup(m).IsValid());
    CHECK_FALSE(sys.ParameterRegion(m).buffer.IsValid());
    CHECK_FALSE(sys.SetFloat(m, "roughness", 1.0f));  // there is no parameter block
    CHECK(f.device->ErrorCount() == 0);
}

TEST_CASE("Unset texture slots show a default chosen by name or by the template", "[renderer][material]")
{
    Fixture f;
    MaterialSystem sys(f.device.Get(), MaterialSystemDesc{});
    const TemplateId plain = sys.CreateTemplate(TestTemplate());
    const MaterialId a = sys.CreateMaterial(plain, Key({}), "a");
    CHECK(sys.EffectiveTexture(a, "albedo_tex") == sys.GetDefaultTexture(DefaultTexture::White));
    CHECK(sys.EffectiveTexture(a, "normal_tex") == sys.GetDefaultTexture(DefaultTexture::FlatNormal));
    CHECK_FALSE(sys.EffectiveTexture(a, "no_such").IsValid());

    // A template can say what a slot defaults to, whatever its name.
    MaterialTemplateDesc d = TestTemplate();
    d.name = "explicit";
    const TextureDefault overrides[1] = {{"albedo_tex", DefaultTexture::Black}};
    d.texture_defaults = overrides;
    d.texture_default_count = 1;
    const TemplateId custom = sys.CreateTemplate(d);
    const MaterialId b = sys.CreateMaterial(custom, Key({}), "b");
    CHECK(sys.EffectiveTexture(b, "albedo_tex") == sys.GetDefaultTexture(DefaultTexture::Black));
    CHECK(sys.EffectiveTexture(b, "normal_tex") == sys.GetDefaultTexture(DefaultTexture::FlatNormal));

    const rhi::TextureHandle mine = MakeTexture(f.device.Get());
    sys.SetTexture(b, "albedo_tex", mine);
    CHECK(sys.EffectiveTexture(b, "albedo_tex") == mine);
    sys.SetTexture(b, "albedo_tex", rhi::TextureHandle{});
    CHECK(sys.EffectiveTexture(b, "albedo_tex") == sys.GetDefaultTexture(DefaultTexture::Black));
    f.device->DestroyTexture(mine);
}

TEST_CASE("The pipeline cache keys on topology and primitive restart", "[renderer][material][topology]")
{
    Fixture f;
    MaterialSystem sys(f.device.Get(), MaterialSystemDesc{});
    const TemplateId t = sys.CreateTemplate(TestTemplate());
    const MaterialId a = sys.CreateMaterial(t, Key({}), "a");
    REQUIRE(a.IsValid());
    const VertexLayoutData layout = StandardVertexLayout();
    const rhi::PipelineHandle list = sys.GetPipeline(a, ColorTarget(), layout).pipeline;
    REQUIRE(list.IsValid());
    const PipelineLookup strip = sys.GetPipeline(a, ColorTarget(), layout, rhi::PrimitiveTopology::TriangleStrip,
                                                 false);
    const PipelineLookup restart = sys.GetPipeline(a, ColorTarget(), layout, rhi::PrimitiveTopology::TriangleStrip,
                                                   true);
    REQUIRE(strip.pipeline.IsValid());
    REQUIRE(restart.pipeline.IsValid());
    CHECK(strip.pipeline != list);
    CHECK(restart.pipeline != strip.pipeline);
    CHECK(sys.Stats().pipelines == 3);
    // The same request again is a hit.
    CHECK(sys.GetPipeline(a, ColorTarget(), layout, rhi::PrimitiveTopology::TriangleStrip, true).pipeline ==
          restart.pipeline);
    CHECK(sys.Stats().pipelines == 3);
    // Restart only makes sense for a strip.
    f.device->ResetErrors();
    CHECK_FALSE(
        sys.GetPipeline(a, ColorTarget(), layout, rhi::PrimitiveTopology::TriangleList, true).pipeline.IsValid());
    CHECK(sys.Warmup(a, ColorTarget(), layout, rhi::PrimitiveTopology::LineList, false));
    CHECK(sys.Stats().pipelines == 4);
}

TEST_CASE("Materials of one template and key share a pipeline", "[renderer][material]")
{
    Fixture f;
    MaterialSystem sys(f.device.Get(), MaterialSystemDesc{});
    const TemplateId t = sys.CreateTemplate(TestTemplate());
    const MaterialId a = sys.CreateMaterial(t, Key({}), "a");
    const MaterialId b = sys.CreateMaterial(t, Key({}), "b");
    const MaterialId tinted = sys.CreateMaterial(t, Key({"TINT"}), "tinted");
    REQUIRE(a.IsValid());
    REQUIRE(b.IsValid());
    REQUIRE(tinted.IsValid());
    const VertexLayoutData layout = StandardVertexLayout();

    const PipelineLookup pa = sys.GetPipeline(a, ColorTarget(), layout);
    REQUIRE(pa.pipeline.IsValid());
    CHECK(sys.Stats().pipeline_misses == 1);
    const PipelineLookup pb = sys.GetPipeline(b, ColorTarget(), layout);
    CHECK(pb.pipeline == pa.pipeline);
    CHECK(pb.index == pa.index);
    CHECK(sys.Stats().pipeline_hits == 1);
    CHECK(sys.Stats().pipelines == 1);

    CHECK(sys.GetPipeline(tinted, ColorTarget(), layout).pipeline != pa.pipeline);  // another permutation
    CHECK(sys.GetPipeline(a, ColorTarget(rhi::Format::BGRA8_UNorm), layout).pipeline != pa.pipeline);  // another target
    PipelineTarget with_depth = ColorTarget();
    with_depth.depth_format = rhi::Format::D32_Float;
    CHECK(sys.GetPipeline(a, with_depth, layout).pipeline != pa.pipeline);
    CHECK(sys.Stats().pipelines == 4);

    // Warmup is a lookup that creates; asking again is a hit.
    const u32 misses = sys.Stats().pipeline_misses;
    CHECK(sys.Warmup(b, ColorTarget(), layout));
    CHECK(sys.Stats().pipeline_misses == misses);
    CHECK(f.device->ErrorCount() == 0);
}

TEST_CASE("A pipeline is refused for a mesh layout the vertex shader cannot read", "[renderer][material]")
{
    Fixture f;
    MaterialSystem sys(f.device.Get(), MaterialSystemDesc{});
    const TemplateId t = sys.CreateTemplate(TestTemplate());
    const MaterialId m = sys.CreateMaterial(t, Key({}), "m");
    VertexLayoutData missing = StandardVertexLayout();
    missing.attribute_count = 3;  // no uv
    CHECK_FALSE(sys.GetPipeline(m, ColorTarget(), missing).pipeline.IsValid());
    VertexLayoutData wrong = StandardVertexLayout();
    wrong.attributes[3].format = rhi::VertexFormat::Float3;
    CHECK_FALSE(sys.GetPipeline(m, ColorTarget(), wrong).pipeline.IsValid());
    PipelineTarget none;
    none.color_count = 0;
    CHECK_FALSE(sys.GetPipeline(m, none, StandardVertexLayout()).pipeline.IsValid());
    PipelineTarget undefined;
    CHECK_FALSE(sys.GetPipeline(m, undefined, StandardVertexLayout()).pipeline.IsValid());
}

TEST_CASE("Changing a texture rebuilds only that material's bind groups", "[renderer][material]")
{
    Fixture f;
    MaterialSystem sys(f.device.Get(), MaterialSystemDesc{});
    const TemplateId t = sys.CreateTemplate(TestTemplate());
    const MaterialId a = sys.CreateMaterial(t, Key({}), "a");
    const MaterialId b = sys.CreateMaterial(t, Key({}), "b");
    const rhi::TextureHandle tex = MakeTexture(f.device.Get());

    sys.BeginFrame(0);
    sys.FlushMaterials();
    const rhi::BindGroupHandle a0 = sys.MaterialBindGroup(a);
    const rhi::BindGroupHandle b0 = sys.MaterialBindGroup(b);
    CHECK(a0 != b0);

    CHECK(sys.SetTexture(a, "albedo_tex", tex));
    sys.BeginFrame(1);
    sys.FlushMaterials();
    CHECK(sys.BindGroupRebuilds(a) == 1);
    CHECK(sys.BindGroupRebuilds(b) == 0);
    CHECK(sys.Stats().bind_group_rebuilds == 1);
    sys.BeginFrame(0);
    CHECK(sys.MaterialBindGroup(a) != a0);
    CHECK(sys.MaterialBindGroup(b) == b0);

    // Setting the same texture again is not a change; a parameter write never rebuilds a group.
    CHECK(sys.SetTexture(a, "albedo_tex", tex));
    CHECK(sys.SetFloat(a, "roughness", 0.5f));
    sys.FlushMaterials();
    CHECK(sys.BindGroupRebuilds(a) == 1);

    // Going back to the default is a change.
    CHECK(sys.SetTexture(a, "albedo_tex", rhi::TextureHandle{}));
    sys.FlushMaterials();
    CHECK(sys.BindGroupRebuilds(a) == 2);

    sys.DestroyMaterial(a);
    sys.DestroyMaterial(b);
    f.device->DestroyTexture(tex);
}

TEST_CASE("Parameter blocks are copied once per changed frame region", "[renderer][material]")
{
    Fixture f;
    MaterialSystem sys(f.device.Get(), MaterialSystemDesc{});
    const TemplateId t = sys.CreateTemplate(TestTemplate());
    const MaterialId m = sys.CreateMaterial(t, Key({}), "m");
    u32 flushes = 0;
    // A change must reach all three frame regions, then stop costing anything.
    for (u64 frame = 0; frame < 3; ++frame)
    {
        sys.BeginFrame(frame);
        sys.FlushMaterials();
        flushes = sys.Stats().parameter_flushes;
    }
    CHECK(flushes == 3);
    sys.BeginFrame(3);
    sys.FlushMaterials();
    CHECK(sys.Stats().parameter_flushes == 3);
    CHECK(sys.SetFloat(m, "roughness", 0.5f));
    CHECK(sys.SetFloat(m, "roughness", 0.5f));  // unchanged: no new work
    for (u64 frame = 4; frame < 7; ++frame)
    {
        sys.BeginFrame(frame);
        sys.FlushMaterials();
    }
    CHECK(sys.Stats().parameter_flushes == 6);
}

TEST_CASE("The draw sort key orders by pipeline, then material, then mesh", "[renderer][material]")
{
    CHECK(MakeDrawSortKey(0, 5, 9) < MakeDrawSortKey(1, 0, 0));
    CHECK(MakeDrawSortKey(1, 2, 9) < MakeDrawSortKey(1, 3, 0));
    CHECK(MakeDrawSortKey(1, 3, 1) < MakeDrawSortKey(1, 3, 2));
    CHECK(MakeDrawSortKey(65535, 0xffffff, 0xffffff) > MakeDrawSortKey(65534, 0xffffff, 0xffffff));
}

TEST_CASE("Default textures exist and differ", "[renderer][material]")
{
    Fixture f;
    MaterialSystem sys(f.device.Get(), MaterialSystemDesc{});
    const rhi::TextureHandle white = sys.GetDefaultTexture(DefaultTexture::White);
    const rhi::TextureHandle black = sys.GetDefaultTexture(DefaultTexture::Black);
    const rhi::TextureHandle flat = sys.GetDefaultTexture(DefaultTexture::FlatNormal);
    CHECK(white.IsValid());
    CHECK(white != black);
    CHECK(black != flat);
}

TEST_CASE("Everything the system created is released in order", "[renderer][material]")
{
    Fixture f;
    {
        MaterialSystem sys(f.device.Get(), MaterialSystemDesc{});
        const TemplateId t = sys.CreateTemplate(TestTemplate());
        const MaterialId a = sys.CreateMaterial(t, Key({}), "a");
        const MaterialId b = sys.CreateMaterial(t, Key({"TINT"}), "b");
        sys.Warmup(a, ColorTarget(), StandardVertexLayout());
        sys.Warmup(b, ColorTarget(), StandardVertexLayout());
        sys.DestroyMaterial(a);
        // b and the template are left for the destructor.
    }
    f.device->WaitIdle();
    f.device->BeginFrame();
    f.device->EndFrame();
    const rhi::NullDeviceStats s = f.device->Statistics();
    CHECK(s.buffers == 0);
    CHECK(s.textures == 0);
    CHECK(s.samplers == 0);
    CHECK(s.shaders == 0);
    CHECK(s.bind_group_layouts == 0);
    CHECK(s.bind_groups == 0);
    CHECK(s.pipelines == 0);
    CHECK(f.device->ErrorCount() == 0);
}

TEST_CASE("Two objects drawn through two materials show their own transform and parameters",
          "[renderer][material][gpu]")
{
    FOR_EACH_GPU_BACKEND(backend)
    {
        gpu_test::BackendValidation validation(backend);
        rhi::DeviceCreateInfo info;
        info.backend = backend;
        info.validation = rhi::ValidationSetting::On;
        info.sync_validation = true;
        info.pipeline_cache = false;
        rhi::DevicePtr device = rhi::CreateDevice(info);
        if (device == nullptr)
        {
            const char* required = os::GetEnvironment("ENGINE_RHI_REQUIRE_GPU");
            if (required != nullptr && required[0] == '1')
            {
                FAIL("ENGINE_RHI_REQUIRE_GPU=1 but no " << gpu_test::BackendTag(backend) << " device could be created");
            }
            SKIP("no " << gpu_test::BackendTag(backend) << " device on this machine");
        }
        {
            MaterialSystem sys(device.Get(), MaterialSystemDesc{});
            REQUIRE(sys.Valid());
            const TemplateId tmpl = sys.CreateTemplate(TestTemplate());
            REQUIRE(tmpl.IsValid());
            // A: red, half rough. B: the TINT permutation, emissive green, and a user texture in the albedo slot left
            // unset (so white), a normal slot left unset (so a flat normal: alpha stays base_color.a).
            const MaterialId a = sys.CreateMaterial(tmpl, Key({}), "red");
            const MaterialId b = sys.CreateMaterial(tmpl, Key({"TINT"}), "green");
            REQUIRE(a.IsValid());
            REQUIRE(b.IsValid());
            sys.SetVec4(a, "base_color", Vec4(1, 0, 0, 1));
            sys.SetFloat(a, "roughness", 0.5f);
            sys.SetMat4(a, "uv_transform", Mat4::Identity());
            sys.SetVec4(b, "base_color", Vec4(0, 0, 1, 1));
            sys.SetVec3(b, "emissive", Vec3(0, 0.4f, 0));
            sys.SetMat4(b, "uv_transform", Mat4::Identity());
            // C shows its transformed uv (flags bit 1): t = (0.5 u + 0.25, 0.8 v), a scale plus a translation, which a
            // transposed matrix would get wrong. At the plane's center uv = (0.5, 0.5), so t = (0.5, 0.4).
            const MaterialId c = sys.CreateMaterial(tmpl, Key({}), "uv");
            REQUIRE(c.IsValid());
            sys.SetUInt(c, "flags", 2);
            sys.SetMat4(
                c, "uv_transform",
                Mat4::FromColumns(Vec4(0.5f, 0, 0, 0), Vec4(0, 0.8f, 0, 0), Vec4(0, 0, 1, 0), Vec4(0.25f, 0, 0, 1)));

            MeshData data;
            REQUIRE(MakePlaneMesh(2.0f, 2.0f, 1, 1, &data) == nullptr);
            GpuMesh mesh;
            REQUIRE(mesh.Create(device.Get(), data, "plane"));
            RenderScene scene(RenderSceneDesc{});
            RenderObjectDesc od;
            od.mesh = &mesh;
            od.transform = ModelMatrix(Vec3(-3.0f, 0, 0), 1.0f, 0.0f);
            const ObjectId left = scene.AddObject(od);
            od.transform = ModelMatrix(Vec3(3.0f, 0, 0), 1.0f, 0.0f);
            const ObjectId right = scene.AddObject(od);
            od.transform = ModelMatrix(Vec3(0.0f, 0, 0), 1.0f, 0.0f);
            const ObjectId middle = scene.AddObject(od);
            // Draw order is the instance index: left = 0, right = 1, middle = 2.
            const u32 slots[3] = {left.index, right.index, middle.index};

            Camera camera = MakeLookAtCamera(Vec3(0, 5, 0), Vec3(0, 0, 0), Vec3(0, 0, -1));
            camera.aspect = 2.0f;
            FrameResources frame(device.Get(), 16);
            REQUIRE(frame.Valid());

            rhi::TextureDesc rt;
            rt.width = 64;
            rt.height = 32;
            rt.format = rhi::Format::RGBA8_UNorm;
            rt.usage = rhi::TextureUsage::ColorAttachment | rhi::TextureUsage::CopySrc;
            const rhi::TextureHandle target = device->CreateTexture(rt);
            REQUIRE(target.IsValid());

            device->BeginFrame();
            sys.BeginFrame(0);
            sys.FlushMaterials();
            frame.BeginFrame(0);
            frame.WriteFrameData(MakeFrameData(camera, 64, 32, 0, 0, 0));
            frame.WriteObjects(scene, slots, 3);
            frame.Flush();

            const PipelineLookup pa = sys.GetPipeline(a, ColorTarget(), mesh.Layout());
            const PipelineLookup pb = sys.GetPipeline(b, ColorTarget(), mesh.Layout());
            const PipelineLookup pc = sys.GetPipeline(c, ColorTarget(), mesh.Layout());
            REQUIRE(pa.pipeline.IsValid());
            REQUIRE(pb.pipeline.IsValid());
            CHECK(pc.pipeline == pa.pipeline);  // same template and key as A

            const BufferRegion fr = frame.FrameRegion();
            const BufferRegion orr = frame.ObjectRegion();
            rhi::BindGroupEntry fe;
            fe.binding = 0;
            fe.buffer = fr.buffer;
            fe.offset = fr.offset;
            fe.size = fr.size;
            rhi::BindGroupDesc fd;
            fd.layout = sys.FrameLayout();
            fd.entries = rhi::Span<rhi::BindGroupEntry>(&fe, 1);
            const rhi::BindGroupHandle frame_group = device->CreateBindGroup(fd);
            rhi::BindGroupEntry oe;
            oe.binding = 0;
            oe.buffer = orr.buffer;
            oe.offset = orr.offset;
            oe.size = orr.size;
            rhi::BindGroupDesc od2;
            od2.layout = sys.ObjectLayout();
            od2.entries = rhi::Span<rhi::BindGroupEntry>(&oe, 1);
            const rhi::BindGroupHandle object_group = device->CreateBindGroup(od2);
            REQUIRE(frame_group.IsValid());
            REQUIRE(object_group.IsValid());

            rhi::CommandList* list = device->CreateCommandList();
            list->Begin();
            mesh.RecordPrepare(list);
            list->Barrier(target, rhi::ResourceState::Undefined, rhi::ResourceState::ColorAttachment);
            rhi::RenderingDesc pass;
            pass.color_count = 1;
            pass.color[0].texture = target;
            list->BeginRendering(pass);
            list->SetViewport(rhi::Viewport{0, 0, 64, 32, 0, 1});
            list->SetScissor(rhi::ScissorRect{0, 0, 64, 32});
            list->SetBindGroup(kFrameSet, frame_group);
            list->SetBindGroup(kObjectSet, object_group);
            if (sys.ObjectIndexStream().IsValid())
            {  // Direct3D 12 reads the object index from the stream, not the instance index
                list->SetVertexBuffer(MaterialSystem::ObjectIndexSlot(), sys.ObjectIndexStream(), 0);
            }
            // Object i is drawn with first_instance = i; the material is the only per-draw bind.
            list->SetPipeline(pa.pipeline);
            list->SetBindGroup(kMaterialSet, sys.MaterialBindGroup(a));
            mesh.Draw(list, 1, 0);
            list->SetPipeline(pb.pipeline);
            list->SetBindGroup(kMaterialSet, sys.MaterialBindGroup(b));
            mesh.Draw(list, 1, 1);
            list->SetPipeline(pc.pipeline);
            list->SetBindGroup(kMaterialSet, sys.MaterialBindGroup(c));
            mesh.Draw(list, 1, 2);
            list->EndRendering();
            list->End();
            rhi::CommandList* lists[] = {list};
            REQUIRE(device->Submit(lists, 1) != 0);
            mesh.MarkReady();
            device->EndFrame();

            Vector<u8> px;
            px.Resize(64 * 32 * 4);
            REQUIRE(device->ReadbackTexture(target, 0, 0, px.Data(), px.Size(), rhi::ResourceState::ColorAttachment));
            auto at = [&](int x, int y, int c) { return static_cast<int>(px[(y * 64 + x) * 4 + c]); };
            auto near = [](int got, int want) { return got - want <= 3 && want - got <= 3; };
            // Left object, material A: red * (1 - 0.5 * 0.5) = 0.75.
            INFO("left pixel " << at(15, 16, 0) << " " << at(15, 16, 1) << " " << at(15, 16, 2));
            CHECK(near(at(15, 16, 0), 191));
            CHECK(near(at(15, 16, 1), 0));
            CHECK(near(at(15, 16, 2), 0));
            CHECK(near(at(15, 16, 3), 255));
            // Right object, material B with TINT: (0, 0.4, 1) * (1, 0.5, 0.5) = (0, 0.2, 0.5).
            CHECK(near(at(49, 16, 0), 0));
            CHECK(near(at(49, 16, 1), 51));
            CHECK(near(at(49, 16, 2), 128));
            CHECK(near(at(49, 16, 3), 255));
            // Middle object, material C: (t, 0) = (0.5, 0.4, 0); a transposed matrix would give (0.25, 0.4, 0). The
            // pixel is near the plane's center, so uv is close to (0.5, 0.5).
            auto close = [](int got, int want) { return got - want <= 10 && want - got <= 10; };
            INFO("middle pixel " << at(32, 16, 0) << " " << at(32, 16, 1) << " " << at(32, 16, 2));
            CHECK(close(at(32, 16, 0),
                        128));  // the pixel is a little off the exact center, so allow the uv to differ by ~0.04
            CHECK(close(at(32, 16, 1), 102));
            CHECK(near(at(32, 16, 2), 0));
            // Nothing between the objects or outside.
            CHECK(near(at(24, 16, 0) + at(24, 16, 1) + at(24, 16, 2), 0));
            CHECK(near(at(2, 2, 0) + at(2, 2, 1) + at(2, 2, 2), 0));

            device->WaitIdle();
            device->DestroyBindGroup(frame_group);
            device->DestroyBindGroup(object_group);
            device->DestroyTexture(target);
        }
        device->WaitIdle();
        device.Reset();
    }
}
