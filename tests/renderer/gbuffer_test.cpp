// Phase 5 Day 79: the G-buffer pass without a GPU. The octahedral normal and sRGB conversions have CPU twins that the
// real-device tests compare against the shaders; the pass is checked for its sorting, instancing and named errors on
// the null device.
#include "lang/scalar.h"
#include <catch2/catch_test_macros.hpp>

#include "renderer/camera.h"
#include "renderer/frame_data.h"
#include "renderer/gbuffer.h"
#include "renderer/instance_set.h"
#include "renderer/material.h"
#include "renderer/mesh.h"
#include "renderer/render_scene.h"
#include "rhi/null/null_device.h"
#include "cstr/mem_ops.h"
#include "containers/array.h"
#include "containers/vector.h"
#include "test_text.h"

using namespace vse;
using namespace vse::renderer;

namespace
{

// A null device, a material system with the G-buffer templates, and frame resources.
struct Rig
{
    rhi::NullDevicePtr device = rhi::CreateNullDevice();
    UniquePtr<MaterialSystem> sys_owner;
    UniquePtr<FrameResources> frame_owner;
    MaterialSystem* sys = nullptr;  // views of the owners
    FrameResources* frame = nullptr;
    GBufferTemplates templates;
    Rig()
    {
        sys_owner = MakeUnique<MaterialSystem>(device.Get(), MaterialSystemDesc{});
        frame_owner = MakeUnique<FrameResources>(device.Get(), 32);
        sys = sys_owner.Get();
        frame = frame_owner.Get();
        templates = CreateGBufferTemplates(sys);
    }
    ~Rig()
    {
        frame_owner.Reset();
        sys_owner.Reset();
        device.Reset();
    }
    void MakeReady(GpuMesh* mesh)
    {
        device->BeginFrame();
        rhi::CommandList* list = device->CreateCommandList();
        list->Begin();
        mesh->RecordPrepare(list);
        list->End();
        rhi::CommandList* lists[] = {list};
        device->Submit(lists, 1);
        device->EndFrame();
        mesh->MarkReady();
    }
    void BeginFrame(u64 f)
    {
        device->BeginFrame();
        sys->BeginFrame(f);
        sys->FlushMaterials();
        frame->BeginFrame(f);
    }
};

Frustum WideFrustum()
{
    Camera cam = MakeLookAtCamera(Vec3(0, 10, 0), Vec3(0, 0, 0), Vec3(0, 0, -1));
    cam.aspect = 1.0f;
    return Frustum::FromViewProjection(cam.ViewProjection());
}

}  // namespace

TEST_CASE("Octahedral encoding round-trips unit vectors and stays in [-1, 1]", "[renderer][gbuffer]")
{
    CHECK(OctEncode(Vec3(0, 0, 1)).x == 0.0f);
    CHECK(OctEncode(Vec3(0, 0, 1)).y == 0.0f);
    CHECK(OctEncode(Vec3(1, 0, 0)).x == 1.0f);
    CHECK(OctEncode(Vec3(0, 0, -1)).x == 1.0f);  // the lower pole folds to a corner
    CHECK(OctEncode(Vec3(0, 0, -1)).y == 1.0f);
    u32 count = 0;
    for (int i = -10; i <= 10; ++i)
    {
        for (int j = -10; j <= 10; ++j)
        {
            for (int k = -10; k <= 10; ++k)
            {
                if (i == 0 && j == 0 && k == 0)
                {
                    continue;
                }
                const f32 len = Sqrt(static_cast<f32>(i * i + j * j + k * k));
                const Vec3 n(i / len, j / len, k / len);
                const Vec2 e = OctEncode(n);
                REQUIRE(Abs(e.x) <= 1.0f);
                REQUIRE(Abs(e.y) <= 1.0f);
                const Vec3 d = OctDecode(e);
                const f32 err = Abs(d.x - n.x) + Abs(d.y - n.y) + Abs(d.z - n.z);
                REQUIRE(err < 1e-4f);
                ++count;
            }
        }
    }
    CHECK(count == 9260);
}

TEST_CASE("The sRGB conversion matches the standard curve and depth linearizes to the plane distances",
          "[renderer][gbuffer]")
{
    CHECK(SrgbToLinear(0.0f) == 0.0f);
    CHECK(Abs(SrgbToLinear(1.0f) - 1.0f) < 1e-6f);
    CHECK(Abs(SrgbToLinear(0.5f) - 0.21404f) < 1e-4f);
    CHECK(Abs(SrgbToLinear(0.02f) - 0.02f / 12.92f) < 1e-7f);  // the linear segment
    CHECK(Abs(LinearizeDepth(0.0f, 0.5f, 100.0f) - 0.5f) < 1e-6f);
    CHECK(Abs(LinearizeDepth(1.0f, 0.5f, 100.0f) - 100.0f) < 1e-3f);
    // Through the projection: a point at view distance z has depth (f / (f - n)) * (1 - n / z).
    Camera cam;
    cam.near_plane = 0.5f;
    cam.far_plane = 100.0f;
    const Vec4 c = cam.Projection() * Vec4(0, 0, -7.0f, 1.0f);
    CHECK(Abs(LinearizeDepth(c.z / c.w, cam.near_plane, cam.far_plane) - 7.0f) < 1e-3f);
}

TEST_CASE("The device check names the first unsupported G-buffer format", "[renderer][gbuffer]")
{
    Rig rig;
    char error[128] = "x";
    CHECK(CheckGBufferFormats(rig.device.Get(), error, sizeof(error)));
    CHECK(error[0] == '\0');
    const PipelineTarget t = GBufferPipelineTarget();
    CHECK(t.color_count == 4);
    CHECK(t.color_formats[0] == rhi::Format::RGBA8_SRGB);
    CHECK(t.color_formats[1] == rhi::Format::RG16_Float);
    CHECK(t.color_formats[2] == rhi::Format::RGBA8_UNorm);
    CHECK(t.color_formats[3] == rhi::Format::RGBA16_Float);  // the emissive target (glTF coverage Day 2)
    CHECK(t.depth_format == rhi::Format::D32_Float);
}

TEST_CASE("The device check refuses a device with too few color attachments, naming the limit", "[renderer][gbuffer]")
{
    rhi::NullDeviceOptions options;
    options.caps.max_color_attachments = 3;
    rhi::NullDevicePtr device = rhi::CreateNullDevice(options);
    char error[160] = "";
    CHECK_FALSE(CheckGBufferFormats(device.Get(), error, sizeof(error)));
    CHECK(StringView(error).Contains("3 color attachments"));
    CHECK(StringView(error).Contains("writes 4"));
    options.caps.max_color_attachments = 4;
    rhi::NullDevicePtr enough = rhi::CreateNullDevice(options);
    CHECK(CheckGBufferFormats(enough.Get(), error, sizeof(error)));
}

TEST_CASE("The camera depth range encloses the scene", "[renderer][gbuffer]")
{
    Rig rig;
    MeshData d;
    REQUIRE(MakeSphereMesh(1.0f, 16, 8, &d) == nullptr);
    GpuMesh sphere;
    REQUIRE(sphere.Create(rig.device.Get(), d, "sphere"));
    RenderScene scene(RenderSceneDesc{});
    Camera cam = MakeLookAtCamera(Vec3(0, 0, 0), Vec3(0, 0, -1), Vec3(0, 1, 0));
    cam.near_plane = 0.123f;
    cam.far_plane = 456.0f;
    FitCameraDepthRange(scene, &cam, 0.1f);
    CHECK(cam.near_plane == 0.123f);  // an empty scene changes nothing
    RenderObjectDesc od;
    od.mesh = &sphere;
    od.transform = ModelMatrix(Vec3(0, 0, -10), 1.0f, 0.0f);
    scene.AddObject(od);
    od.transform = ModelMatrix(Vec3(0, 0, -30), 2.0f, 0.0f);
    scene.AddObject(od);
    FitCameraDepthRange(scene, &cam, 0.1f);
    CHECK(Abs(cam.near_plane - 9.0f) < 0.05f);  // 10 - 1
    CHECK(Abs(cam.far_plane - 32.0f) < 0.1f);   // 30 + 2
    // The near plane never drops below the minimum, even when the camera is inside a sphere.
    cam.position = Vec3(0, 0, -10);
    FitCameraDepthRange(scene, &cam, 0.25f);
    CHECK(cam.near_plane == 0.25f);
    CHECK(cam.far_plane >= cam.near_plane * 2.0f);
}

TEST_CASE("G-buffer materials pick the shader modules their options name", "[renderer][gbuffer]")
{
    Rig rig;
    REQUIRE(rig.templates.plain.IsValid());
    REQUIRE(rig.templates.colored.IsValid());
    for (int bits = 0; bits < 8; ++bits)
    {
        GBufferOptions o;
        o.normal_map = (bits & 1) != 0;
        o.alpha_test = (bits & 2) != 0;
        o.vertex_color = (bits & 4) != 0;
        const MaterialId m = CreateGBufferMaterial(rig.sys, rig.templates, o, "m");
        REQUIRE(m.IsValid());
        CHECK(rig.sys->ReadsVertexInput(m, 4) == o.vertex_color);
        {
            const MaterialId back = rig.sys->MaterialAtIndex(m.index);
            CHECK((back.index == m.index && back.generation == m.generation));
        }
    }
    CHECK_FALSE(rig.sys->MaterialAtIndex(999).IsValid());
    GBufferTemplates none;
    CHECK_FALSE(CreateGBufferMaterial(rig.sys, none, GBufferOptions{}, "m").IsValid());
}

// glTF coverage Day 1: every G-buffer template has the metallic-roughness and occlusion map slots, linear, and a
// material that leaves them unset samples the white default (the scalars alone decide, as before the maps existed).
TEST_CASE("The G-buffer templates have linear metallic-roughness and occlusion slots that default to white",
          "[renderer][gbuffer]")
{
    const Array<const char*, 4> names = {"gbuffer", "gbuffer_color", "gbuffer_skinned", "gbuffer_skinned_color"};
    for (const char* name : names)
    {
        INFO(name);
        const GBufferTemplateInfo* info = FindGBufferTemplateInfo(name);
        REQUIRE(info != nullptr);
        bool mr = false;
        bool occlusion = false;
        for (u32 i = 0; i < info->slot_count; ++i)
        {
            const StringView slot(info->slots[i].name);
            mr = mr ||
                 (slot == StringView("metallic_roughness_tex") && info->slots[i].color_space == SlotColorSpace::Linear);
            occlusion = occlusion ||
                        (slot == StringView("occlusion_tex") && info->slots[i].color_space == SlotColorSpace::Linear);
        }
        CHECK(mr);
        CHECK(occlusion);
        bool strength = false;
        for (u32 i = 0; i < info->default_count; ++i)
        {
            strength = strength || (StringView(info->defaults[i].name) == StringView("occlusion_strength") &&
                                    info->defaults[i].value[0] == 1.0f);
        }
        CHECK(strength);
    }
    Rig rig;
    const MaterialId m = CreateGBufferMaterial(rig.sys, rig.templates, GBufferOptions{}, "m");
    REQUIRE(m.IsValid());
    const rhi::TextureHandle white = rig.sys->GetDefaultTexture(DefaultTexture::White);
    CHECK(rig.sys->EffectiveTexture(m, "metallic_roughness_tex") == white);
    CHECK(rig.sys->EffectiveTexture(m, "occlusion_tex") == white);
    CHECK(rig.sys->SetFloat(m, "occlusion_strength", 0.5f));
}

TEST_CASE("A colored mesh gets a pipeline from a vertex color material and is refused by one without",
          "[renderer][gbuffer]")
{
    Rig rig;
    MeshData plain, colored;
    REQUIRE(MakePlaneMesh(2.0f, 2.0f, 1, 1, &plain) == nullptr);
    const u8 white[4] = {255, 255, 255, 255};
    REQUIRE(MakeUniformColoredMesh(plain, white, &colored) == nullptr);
    GBufferOptions with;
    with.vertex_color = true;
    const MaterialId a = CreateGBufferMaterial(rig.sys, rig.templates, with, "a");
    const MaterialId b = CreateGBufferMaterial(rig.sys, rig.templates, GBufferOptions{}, "b");
    const PipelineTarget target = GBufferPipelineTarget();
    // A UNorm8x4 attribute feeds a float4 shader input.
    CHECK(rig.sys->GetPipeline(a, target, ColorVertexLayout()).pipeline.IsValid());
    // The material that expects colors has nothing to read from a standard-layout mesh.
    CHECK_FALSE(rig.sys->GetPipeline(a, target, StandardVertexLayout()).pipeline.IsValid());
    CHECK(rig.sys->GetPipeline(b, target, StandardVertexLayout()).pipeline.IsValid());
}

TEST_CASE("Prepare sorts, instances and counts what it draws", "[renderer][gbuffer]")
{
    Rig rig;
    MeshData d;
    REQUIRE(MakeCubeMesh(1.0f, &d) == nullptr);
    GpuMesh cube;
    REQUIRE(cube.Create(rig.device.Get(), d, "cube"));
    REQUIRE(MakeSphereMesh(1.0f, 12, 6, &d) == nullptr);
    GpuMesh sphere;
    REQUIRE(sphere.Create(rig.device.Get(), d, "sphere"));
    rig.MakeReady(&cube);
    rig.MakeReady(&sphere);
    const MaterialId m0 = CreateGBufferMaterial(rig.sys, rig.templates, GBufferOptions{}, "m0");
    const MaterialId m1 = CreateGBufferMaterial(rig.sys, rig.templates, GBufferOptions{}, "m1");

    struct Item
    {
        const GpuMesh* mesh;
        MaterialId material;
    };
    const Item items[] = {{&cube, m0},   {&sphere, m1}, {&cube, m1},  {&cube, m0},
                          {&sphere, m1}, {&cube, m0},   {&sphere, m1}};
    GBufferStats stats[2];
    for (int order = 0; order < 2; ++order)
    {
        RenderScene scene(RenderSceneDesc{});
        for (int i = 0; i < 7; ++i)
        {
            const Item& it = items[order == 0 ? i : 6 - i];
            RenderObjectDesc od;
            od.mesh = it.mesh;
            od.material = it.material.index;
            od.transform = ModelMatrix(Vec3(static_cast<f32>(i) - 3.0f, 0, 0), 1.0f, 0.0f);
            scene.AddObject(od);
        }
        GBufferPass pass(rig.device.Get(), rig.sys, rig.frame);
        rig.BeginFrame(static_cast<u64>(order));
        CHECK(pass.Prepare(scene, WideFrustum()));
        rig.frame->Flush();
        rig.device->EndFrame();
        stats[order] = pass.Stats();
        CHECK(StringView(pass.LastError()).Empty());
    }
    // Groups: cube/m0 x3, cube/m1 x1, sphere/m1 x3 -> 3 draws, whatever the order.
    for (const GBufferStats& s : stats)
    {
        CHECK(s.visible == 7);
        CHECK(s.drawn_objects == 7);
        CHECK(s.draws == 3);
        CHECK(s.rejected == 0);
        CHECK(s.pipeline_binds == 1);  // one template and key: one pipeline for all materials
        CHECK(s.material_binds == 2);  // m0, then m1 for both its meshes
    }
}

TEST_CASE("Prepare names every object it has to leave out", "[renderer][gbuffer]")
{
    Rig rig;
    MeshData d, colored;
    REQUIRE(MakeCubeMesh(1.0f, &d) == nullptr);
    const u8 grey[4] = {128, 128, 128, 255};
    REQUIRE(MakeUniformColoredMesh(d, grey, &colored) == nullptr);
    GpuMesh cube, colored_cube, unprepared;
    REQUIRE(cube.Create(rig.device.Get(), d, "cube"));
    REQUIRE(colored_cube.Create(rig.device.Get(), colored, "colored cube"));
    REQUIRE(unprepared.Create(rig.device.Get(), d, "unprepared"));
    rig.MakeReady(&cube);
    rig.MakeReady(&colored_cube);
    const MaterialId plain = CreateGBufferMaterial(rig.sys, rig.templates, GBufferOptions{}, "plain");
    GBufferOptions vc;
    vc.vertex_color = true;
    const MaterialId color = CreateGBufferMaterial(rig.sys, rig.templates, vc, "color");

    auto run = [&](const GpuMesh* mesh, u32 material, engine_test::Text* message, GBufferStats* stats)
    {
        RenderScene scene(RenderSceneDesc{});
        RenderObjectDesc od;
        od.mesh = mesh;
        od.material = material;
        scene.AddObject(od);
        GBufferPass pass(rig.device.Get(), rig.sys, rig.frame);
        static u64 frame = 100;
        rig.BeginFrame(frame++);
        const bool ok = pass.Prepare(scene, WideFrustum());
        rig.frame->Flush();
        rig.device->EndFrame();
        *message = engine_test::MakeText(pass.LastError());
        *stats = pass.Stats();
        return ok;
    };
    engine_test::Text msg = engine_test::MakeText();
    GBufferStats stats;
    CHECK(run(&cube, plain.index, &msg, &stats));
    CHECK(run(&colored_cube, color.index, &msg, &stats));

    CHECK_FALSE(run(&colored_cube, plain.index, &msg, &stats));
    CHECK(msg.View().Contains("vertex colors"));
    CHECK(msg.View().Contains("slot 0"));
    CHECK(stats.rejected == 1);
    CHECK(stats.drawn_objects == 0);

    CHECK_FALSE(run(&cube, color.index, &msg, &stats));
    CHECK(msg.View().Contains("without vertex colors"));

    CHECK_FALSE(run(&unprepared, plain.index, &msg, &stats));
    CHECK(msg.View().Contains("not prepared"));

    CHECK_FALSE(run(&cube, 77, &msg, &stats));
    CHECK(msg.View().Contains("material 77"));
}

TEST_CASE("The frame and object bind groups are created once per region", "[renderer][gbuffer]")
{
    Rig rig;
    rig.BeginFrame(0);
    const rhi::BindGroupHandle a = rig.sys->FrameBindGroup(rig.frame->FrameRegion());
    const rhi::BindGroupHandle b = rig.sys->ObjectBindGroup(rig.frame->ObjectRegion());
    CHECK(a.IsValid());
    CHECK(b.IsValid());
    CHECK(rig.sys->FrameBindGroup(rig.frame->FrameRegion()) == a);
    CHECK(rig.sys->ObjectBindGroup(rig.frame->ObjectRegion()) == b);
    rig.device->EndFrame();
    // The next frame uses another region of the same buffers, which gets its own group, and the first one is reused
    // after the ring wraps.
    rig.BeginFrame(1);
    const rhi::BindGroupHandle a1 = rig.sys->FrameBindGroup(rig.frame->FrameRegion());
    CHECK(a1.IsValid());
    CHECK_FALSE(a1 == a);
    rig.device->EndFrame();
    rig.BeginFrame(2);
    rig.device->EndFrame();
    rig.BeginFrame(3);
    CHECK(rig.sys->FrameBindGroup(rig.frame->FrameRegion()) == a);
    rig.device->EndFrame();
}

TEST_CASE("The bind group cache evicts its oldest entry instead of filling up", "[renderer][gbuffer]")
{
    Rig rig;
    rig.BeginFrame(0);
    const BufferRegion base = rig.frame->FrameRegion();
    // Recreated frame resources leave regions of dead buffers behind; model that with 20 distinct regions.
    rhi::BindGroupHandle last;
    for (u64 i = 0; i < 20; ++i)
    {
        BufferRegion r = base;
        r.size = base.size;
        r.offset = base.offset;
        r.buffer = rig.frame->ObjectRegion().buffer;
        r.offset = i * 256;  // distinct (buffer, offset) pairs that stay inside the object buffer
        r.size = sizeof(ObjectData);
        last = rig.sys->ObjectBindGroup(r);
        REQUIRE(last.IsValid());
    }
    CHECK(rig.sys->ObjectBindGroup(rig.frame->ObjectRegion()).IsValid());
    rig.device->EndFrame();
}

TEST_CASE("Mesh sort ids are never zero for a live mesh and a moved-from mesh has none", "[renderer][gbuffer]")
{
    Rig rig;
    MeshData d;
    REQUIRE(MakeCubeMesh(1.0f, &d) == nullptr);
    GpuMesh a, b;
    CHECK(a.SortId() == 0);
    REQUIRE(a.Create(rig.device.Get(), d, "a"));
    REQUIRE(b.Create(rig.device.Get(), d, "b"));
    CHECK(a.SortId() != 0);
    CHECK(a.SortId() != b.SortId());
    CHECK(a.SortId() < (1u << 24));
    const u32 id = a.SortId();
    GpuMesh moved = static_cast<GpuMesh&&>(a);
    CHECK(moved.SortId() == id);
    CHECK(a.SortId() == 0);
}

TEST_CASE("Only a UNorm8x4 attribute at location 4 counts as a vertex color", "[renderer][gbuffer]")
{
    Rig rig;
    // The standard attributes plus a second uv set (Float4) at location 4: not a color, so a plain material takes it.
    struct Wide
    {
        StandardVertex base;
        f32 extra[4];
    };
    VertexLayoutData layout = StandardVertexLayout();
    layout.attributes[layout.attribute_count].location = 4;
    layout.attributes[layout.attribute_count].format = rhi::VertexFormat::Float4;
    layout.attributes[layout.attribute_count].offset = sizeof(StandardVertex);
    ++layout.attribute_count;
    layout.stride = sizeof(Wide);
    MeshData plane;
    REQUIRE(MakePlaneMesh(2.0f, 2.0f, 1, 1, &plane) == nullptr);
    Vector<Wide> vertices;
    vertices.Resize(plane.VertexCount());
    const StandardVertex* in = static_cast<const StandardVertex*>(plane.Vertices());
    for (u32 i = 0; i < plane.VertexCount(); ++i)
    {
        vertices[i].base = in[i];
        MemSet(vertices[i].extra, 0, sizeof(vertices[i].extra));
    }
    Vector<u32> indices;
    indices.Resize(plane.IndexCount());
    for (u32 i = 0; i < plane.IndexCount(); ++i)
    {
        indices[i] = plane.IndexAt(i);
    }
    MeshData wide;
    REQUIRE(wide.Set(layout, 0, vertices.Data(), plane.VertexCount(), rhi::IndexType::UInt32, indices.Data(),
                     plane.IndexCount()) == nullptr);
    GpuMesh mesh;
    REQUIRE(mesh.Create(rig.device.Get(), wide, "wide"));
    rig.MakeReady(&mesh);
    const MaterialId m = CreateGBufferMaterial(rig.sys, rig.templates, GBufferOptions{}, "m");
    RenderScene scene(RenderSceneDesc{});
    RenderObjectDesc od;
    od.mesh = &mesh;
    od.material = m.index;
    scene.AddObject(od);
    GBufferPass pass(rig.device.Get(), rig.sys, rig.frame);
    rig.BeginFrame(0);
    CHECK(pass.Prepare(scene, WideFrustum()));
    rig.frame->Flush();
    rig.device->EndFrame();
    CHECK(pass.Stats().drawn_objects == 1);
}

// Phase 24 Day 374: one mesh drawn as several index ranges, each with its own material.
TEST_CASE("A mesh's index ranges draw as separate draws and instance by range", "[renderer][gbuffer][ranges]")
{
    Rig rig;
    MeshData d;
    REQUIRE(MakeCubeMesh(1.0f, &d) == nullptr);
    const u32 total = d.IndexCount();
    REQUIRE(total >= 12);
    const MeshRange ranges[] = {{0, 6}, {6, 6}, {12, total - 12}};
    REQUIRE(d.SetRanges(ranges, 3) == nullptr);
    GpuMesh cube;
    REQUIRE(cube.Create(rig.device.Get(), d, "cube"));
    REQUIRE(cube.RangeCount() == 3);
    CHECK(cube.Range(1).first_index == 6);
    CHECK(cube.Range(2).index_count == total - 12);
    rig.MakeReady(&cube);
    const MaterialId m0 = CreateGBufferMaterial(rig.sys, rig.templates, GBufferOptions{}, "m0");
    const MaterialId m1 = CreateGBufferMaterial(rig.sys, rig.templates, GBufferOptions{}, "m1");

    RenderScene scene(RenderSceneDesc{});
    // Two cubes, each as three objects (one per range); ranges 0 and 2 share m0, range 1 uses m1.
    for (u32 cube_number = 0; cube_number < 2; ++cube_number)
    {
        for (u32 r = 0; r < 3; ++r)
        {
            RenderObjectDesc od;
            od.mesh = &cube;
            od.material = (r == 1 ? m1 : m0).index;
            od.first_index = ranges[r].first_index;
            od.index_count = ranges[r].index_count;
            od.transform = ModelMatrix(Vec3(static_cast<f32>(cube_number) * 2.0f, 0, 0), 1.0f, 0.0f);
            REQUIRE(scene.AddObject(od).IsValid());
        }
    }
    GBufferPass pass(rig.device.Get(), rig.sys, rig.frame);
    rig.BeginFrame(0);
    CHECK(pass.Prepare(scene, WideFrustum()));
    rig.frame->Flush();
    rig.device->EndFrame();
    CHECK(pass.Stats().drawn_objects == 6);
    CHECK(pass.Stats().draws == 3);  // range 0, range 1 and range 2, each instanced twice (the two cubes)
    CHECK(StringView(pass.LastError()).Empty());
}

TEST_CASE("Index ranges are checked by the mesh data", "[renderer][gbuffer][ranges]")
{
    MeshData d;
    CHECK(d.SetRanges(nullptr, 0) != nullptr);  // empty mesh
    REQUIRE(MakeCubeMesh(1.0f, &d) == nullptr);
    const u32 total = d.IndexCount();
    const MeshRange past[] = {{0, total + 3}};
    CHECK(d.SetRanges(past, 1) != nullptr);
    const MeshRange empty[] = {{0, 0}};
    CHECK(d.SetRanges(empty, 1) != nullptr);
    const MeshRange odd[] = {{1, 3}};
    CHECK(d.SetRanges(odd, 1) != nullptr);
    CHECK(d.RangeCount() == 0);
    const MeshRange good[] = {{0, 3}, {3, total - 3}};
    CHECK(d.SetRanges(good, 2) == nullptr);
    CHECK(d.RangeCount() == 2);
    CHECK(d.CopyGeometry().range_count == 2);
    CHECK(d.SetRanges(nullptr, 0) == nullptr);
    CHECK(d.RangeCount() == 0);
}

TEST_CASE("Instanced objects draw as one draw per set and name what they cannot draw",
          "[renderer][gbuffer][instancing]")
{
    Rig rig;
    REQUIRE(rig.templates.instanced.IsValid());
    MeshData data;
    REQUIRE(MakeSphereMesh(1.0f, 8, 4, &data) == nullptr);
    GpuMesh mesh;
    REQUIRE(mesh.Create(rig.device.Get(), data, "sphere"));
    rig.MakeReady(&mesh);
    InstanceSetDesc sd;
    sd.has_data = true;
    InstanceSetPtr set = InstanceSet::Create(sd);
    REQUIRE(set.Get() != nullptr);
    for (u32 i = 0; i < 50; ++i)
    {
        InstanceTransform t;
        t.position = Vec3d(static_cast<f64>(i) * 0.2 - 5.0, 0.0, 0.0);
        REQUIRE(set->Add(t, nullptr, nullptr) == InstanceStatus::Ok);
    }
    GBufferOptions io;
    io.instanced = true;
    const MaterialId instanced = CreateGBufferMaterial(rig.sys, rig.templates, io, "instanced");
    const MaterialId plain = CreateGBufferMaterial(rig.sys, rig.templates, GBufferOptions{}, "plain");
    REQUIRE(instanced.IsValid());
    REQUIRE(plain.IsValid());
    CHECK(rig.sys->IsInstanced(instanced));
    CHECK_FALSE(rig.sys->IsInstanced(plain));

    GBufferOptions bad;
    bad.instanced = true;
    bad.skinned = true;
    CHECK_FALSE(CreateGBufferMaterial(rig.sys, rig.templates, bad, "bad").IsValid());

    InstancedObject o;
    o.set = set;
    o.mesh = &mesh;
    o.material = instanced.index;
    {
        RenderScene scene(RenderSceneDesc{4, 4, 1, 4});
        scene.AddInstanced(o);
        rig.BeginFrame(1);
        GBufferPass pass(rig.device.Get(), rig.sys, rig.frame);
        CHECK(pass.Prepare(scene, WideFrustum()));
        const GBufferStats s = pass.Stats();
        CHECK(s.instanced_sets == 1);
        CHECK(s.draws == 1);
        CHECK(s.instances_visible == 50);
        CHECK(s.instances_dropped == 0);
        CHECK(rig.frame->InstancesWritten() == 50);
        rig.frame->Flush();
        rig.device->EndFrame();
    }
    {
        // A material that is not instanced is named in the error and nothing is drawn.
        InstancedObject wrong = o;
        wrong.material = plain.index;
        RenderScene scene(RenderSceneDesc{4, 4, 1, 4});
        scene.AddInstanced(wrong);
        rig.BeginFrame(2);
        GBufferPass pass(rig.device.Get(), rig.sys, rig.frame);
        CHECK_FALSE(pass.Prepare(scene, WideFrustum()));
        CHECK(StringView(pass.LastError()).Contains("GBufferOptions::instanced"));
        CHECK(pass.Stats().draws == 0);
        rig.frame->Flush();
        rig.device->EndFrame();
    }
}

TEST_CASE("An instanced tree draws one draw per run and names a material that is not instanced",
          "[renderer][gbuffer][instancing]")
{
    Rig rig;
    MeshData data;
    REQUIRE(MakeSphereMesh(1.0f, 8, 4, &data) == nullptr);
    GpuMesh mesh;
    REQUIRE(mesh.Create(rig.device.Get(), data, "sphere"));
    rig.MakeReady(&mesh);

    Vector<TreeMeshInfo> infos;
    TreeMeshInfo info;
    info.bounds = Aabb::FromMinMax(Vec3(-1, -1, -1), Vec3(1, 1, 1));
    infos.Push(info);
    InstanceTreeBuilder builder(Span<const TreeMeshInfo>(infos.Data(), infos.Size()));
    TreeNodeDesc root_desc;
    root_desc.mesh = 0;
    TreeNodeId root;
    REQUIRE(builder.AddRoot(root_desc, &root) == TreeStatus::Ok);
    for (u32 i = 0; i < 9; ++i)
    {
        TreeNodeDesc child;
        child.mesh = 0;
        child.position = Vec3d(static_cast<f64>(i) - 4.0, 0.0, 0.0);
        child.scale = Vec3(0.3f, 0.3f, 0.3f);
        REQUIRE(builder.AddChild(root, child, nullptr) == TreeStatus::Ok);
    }
    InstanceTreePtr tree = builder.Finish(nullptr);
    REQUIRE(tree.Get() != nullptr);

    GBufferOptions io;
    io.instanced = true;
    const MaterialId instanced = CreateGBufferMaterial(rig.sys, rig.templates, io, "instanced");
    const MaterialId plain = CreateGBufferMaterial(rig.sys, rig.templates, GBufferOptions{}, "plain");
    REQUIRE(instanced.IsValid());

    InstancedObject o;
    o.tree = tree;
    o.tree_meshes[0] = &mesh;
    o.tree_materials[0] = instanced.index;
    {
        RenderScene scene(RenderSceneDesc{4, 4, 1, 4});
        scene.AddInstanced(o);
        rig.BeginFrame(1);
        GBufferPass pass(rig.device.Get(), rig.sys, rig.frame);
        CHECK(pass.Prepare(scene, WideFrustum()));
        const GBufferStats s = pass.Stats();
        CHECK(s.instance_trees == 1);
        CHECK(s.instance_tree_runs == 1);
        CHECK(s.instance_tree_nodes == 10);
        CHECK(s.draws == 1);
        CHECK(rig.frame->InstancesWritten() == 10);
        rig.frame->Flush();
        rig.device->EndFrame();
    }
    {
        InstancedObject wrong = o;
        wrong.tree_materials[0] = plain.index;
        RenderScene scene(RenderSceneDesc{4, 4, 1, 4});
        scene.AddInstanced(wrong);
        rig.BeginFrame(2);
        GBufferPass pass(rig.device.Get(), rig.sys, rig.frame);
        CHECK_FALSE(pass.Prepare(scene, WideFrustum()));
        CHECK(StringView(pass.LastError()).Contains("GBufferOptions::instanced"));
        CHECK(pass.Stats().draws == 0);
        rig.frame->Flush();
        rig.device->EndFrame();
    }
}
