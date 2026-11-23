// The terrain draw on a real device: a ramp patch against the CPU model of the displaced surface, no holes between
// tiles of different levels, a box half buried in the terrain, release and recreate, and the draw count of a very large
// map.
#include "lang/scalar.h"
#include "log/output.h"

#include <catch2/catch_test_macros.hpp>

#include "asset/gpu_loaders.h"
#include "asset/height_map.h"
#include "containers/shared_bytes.h"
#include "containers/span.h"
#include "gbuffer_rig.h"
#include "renderer/lighting.h"
#include "renderer/terrain_pass.h"

using namespace vse;
using namespace vse::renderer;
using namespace gbuffer_rig;

namespace
{

// A height map of `n` by `n` samples whose sample value is `f(x, z)`, as an open view.
struct TgMap
{
    Vector<byte> blob;
    asset::HeightMapView view;
    GpuTexturePtr texture;
    TerrainTiles tiles;
    f32 range_max = 20.0f;
};

u32 TgRamp(u32 x, u32 z)
{
    return (x + z) * 250;
}

u32 TgFlat(u32, u32)
{
    return 0;
}

u32 TgRough(u32 x, u32 z)
{
    u32 v = x * 73856093u ^ z * 19349663u;
    v ^= v >> 13;
    v *= 0x5bd1e995u;
    v ^= v >> 15;
    return (v & 0x3fffu) + ((x / 16 + z / 16) % 3) * 9000u;
}

bool TgMake(rhi::Device* device, TgMap* map, u32 n, f32 cell_size, f32 range_max, u32 (*f)(u32, u32))
{
    Vector<byte> samples;
    samples.Resize(static_cast<usize>(n) * n * 2);
    for (u32 z = 0; z < n; ++z)
    {
        for (u32 x = 0; x < n; ++x)
        {
            const u32 s = f(x, z);
            samples[(static_cast<usize>(z) * n + x) * 2] = static_cast<byte>(s & 0xFF);
            samples[(static_cast<usize>(z) * n + x) * 2 + 1] = static_cast<byte>(s >> 8);
        }
    }
    asset::TypeRegistry types;
    asset::HeightMapHeader header;
    header.width = n;
    header.height = n;
    header.range_min = 0.0f;
    header.range_max = range_max;
    header.cell_size = cell_size;
    map->range_max = range_max;
    if (!asset::BuildHeightMapBlob(types, asset::ResourceId::FromValue(9), header,
                                   ByteSpan(samples.Data(), samples.Size()), &map->blob)
             .Ok())
    {
        return false;
    }
    if (!map->view.Open(CopyShared(map->blob), types).Ok())
    {
        return false;
    }
    TerrainTilesDesc desc;
    map->view.MakeTilesDesc(&desc);
    return map->tiles.Build(desc) && asset::MakeHeightTexture(device, map->view, nullptr, &map->texture);
}

TerrainDraw TgDraw(const TgMap& map)
{
    TerrainDraw d;
    d.height = map.texture;
    d.samples_x = map.view.Header().width;
    d.samples_z = map.view.Header().height;
    d.cell_size = map.view.Header().cell_size;
    d.range_min = map.view.Header().range_min;
    d.range_max = map.view.Header().range_max;
    return d;
}

struct TgFrame
{
    Layers layers;
    u32 drawn = 0;
    bool terrain_added = false;
};

// Renders `scene` and the terrain into the G-buffer and reads every layer back.
TgFrame TgRender(Rig& rig, TerrainPass& pass, const RenderScene& scene, const Camera& camera, const TerrainDraw& draw,
                 const TerrainTiles& tiles, const TerrainSelectSettings& select)
{
    TgFrame out;
    GBufferPass gbuffer(rig.device, rig.sys, rig.frame);
    gbuffer.SetExtraTargetUsage(rhi::TextureUsage::CopySrc);
    const u64 f = rig.frame_index++;
    rig.device->BeginFrame();
    rig.sys->BeginFrame(f);
    rig.sys->FlushMaterials();
    rig.frame->BeginFrame(f);
    rig.frame->WriteFrameData(MakeFrameData(camera, kW, kH, 0, 0, f));
    out.layers.ok = gbuffer.Prepare(scene, Frustum::FromViewProjection(camera.ViewProjection()));
    Vector<TerrainTile> chosen;
    const Frustum frustum = Frustum::FromViewProjection(camera.ViewProjection());
    tiles.Select(camera.position, &frustum, select, &chosen);
    pass.Prepare(draw, vse::Span<const TerrainTile>(chosen.Data(), chosen.Size()), f);
    out.drawn = pass.DrawnTiles();
    rig.frame->Flush();

    rhi::BufferHandle bufs[5];
    const u64 sizes[5] = {kW * kH * 4, kW * kH * 4, kW * kH * 4, kW * kH * 4, kW * kH * 8};
    for (int i = 0; i < 5; ++i)
    {
        rhi::BufferDesc bd;
        bd.size = sizes[i];
        bd.location = rhi::MemoryLocation::GpuOnly;
        bd.usage = rhi::BufferUsage::CopyDst | rhi::BufferUsage::CopySrc;
        bufs[i] = rig.device->CreateBuffer(bd);
        REQUIRE(bufs[i].IsValid());
    }
    ArenaAllocator arena(1 << 20);
    {
        RenderGraph graph(&arena, RenderGraphDesc{});
        rig.pool->BeginFrame(f);
        Rig::CopyJob job;
        job.targets = gbuffer.AddToGraph(&graph, kW, kH);
        out.terrain_added = pass.AddToGraph(&graph, job.targets);
        PassBuilder copy = graph.AddPass("readback", &Rig::CopyPass, &job, PassFlags::SideEffects);
        const GraphResource tex[5] = {job.targets.albedo, job.targets.normal, job.targets.material, job.targets.depth,
                                      job.targets.emissive};
        for (int i = 0; i < 5; ++i)
        {
            job.buffers[i] = graph.ImportBuffer("host", bufs[i], rhi::ResourceState::Undefined,
                                                rhi::ResourceState::CopySrc, false);
            copy.Read(tex[i], GraphUsage::CopySrc).Write(job.buffers[i], GraphUsage::CopyDst);
        }
        REQUIRE(graph.Compile());
        rhi::CommandList* list = rig.device->CreateCommandList();
        list->Begin();
        if (!graph.Execute(rig.pool, list))
        {
            FAIL(graph.ExecuteError());
        }
        list->End();
        rhi::CommandList* lists[] = {list};
        REQUIRE(rig.device->Submit(lists, 1) != 0);
    }
    rig.device->EndFrame();
    rig.device->WaitIdle();
    out.layers.albedo.Resize(kW * kH * 4);
    out.layers.normal.Resize(kW * kH * 2);
    out.layers.material.Resize(kW * kH * 4);
    out.layers.depth.Resize(kW * kH);
    out.layers.emissive.Resize(kW * kH * 4);
    REQUIRE(rig.device->ReadbackBuffer(bufs[0], 0, out.layers.albedo.Data(), sizes[0]));
    REQUIRE(rig.device->ReadbackBuffer(bufs[1], 0, out.layers.normal.Data(), sizes[1]));
    REQUIRE(rig.device->ReadbackBuffer(bufs[2], 0, out.layers.material.Data(), sizes[2]));
    REQUIRE(rig.device->ReadbackBuffer(bufs[3], 0, out.layers.depth.Data(), sizes[3]));
    REQUIRE(rig.device->ReadbackBuffer(bufs[4], 0, out.layers.emissive.Data(), sizes[4]));
    for (int i = 0; i < 5; ++i)
    {
        rig.device->DestroyBuffer(bufs[i]);
    }
    return out;
}

// The height of the ramp map (TgRamp) at a point, the CPU model of the displaced surface (level 0, no morph).
f32 TgRampHeight(f32 x, f32 z, f32 cell_size, f32 range_max)
{
    return (x + z) / cell_size * 250.0f / 65535.0f * range_max;
}

Camera TgCamera(const Vec3& from, const Vec3& to, const Vec3& up, f32 far_plane)
{
    Camera cam = MakeLookAtCamera(from, to, up);
    cam.aspect = 1.0f;
    cam.near_plane = 1.0f;
    cam.far_plane = far_plane;
    return cam;
}

}  // namespace

TEST_CASE("The terrain pass draws a ramp patch where the CPU model puts it", "[rhi][gpu][terrain]")
{
    FOR_EACH_GPU_BACKEND(backend)
    {
        gpu_test::BackendValidation validation(backend);
        rhi::DevicePtr device = MakeDevice(backend);
        {
            Rig rig;
            REQUIRE(rig.Create(device.Get()));
            TgMap map;
            REQUIRE(TgMake(device.Get(), &map, 129, 1.0f, 20.0f, &TgRamp));
            TerrainPass pass(device.Get(), rig.sys, rig.frame);
            REQUIRE(pass.Valid());
            RenderScene scene(RenderSceneDesc{});
            const Camera cam = TgCamera(Vec3(64, 60, 64), Vec3(64, 0, 64), Vec3(0, 0, -1), 200.0f);
            const TgFrame frame = TgRender(rig, pass, scene, cam, TgDraw(map), map.tiles, TerrainSelectSettings{});
            REQUIRE(frame.layers.ok);
            CHECK(frame.terrain_added);
            CHECK(frame.drawn > 0);
            CHECK(frame.drawn <= 16);
            const FrameData fd = MakeFrameData(cam, kW, kH, 0, 0, 0);
            u32 checked = 0;
            for (u32 y = 8; y < kH - 8; y += 5)
            {
                for (u32 x = 8; x < kW - 8; x += 5)
                {
                    const f32 depth = frame.layers.Depth(x, y);
                    INFO("pixel " << x << " " << y << " depth " << depth);
                    REQUIRE(depth < 1.0f);
                    const Vec3 p = ReconstructWorldPosition(x, y, depth, kW, kH, fd.inverse_view_projection);
                    CHECK(Abs(p.y - TgRampHeight(p.x, p.z, 1.0f, 20.0f)) < 0.08f);
                    // the flat grey the pass writes: albedo 0.5 linear is 188 in sRGB
                    CHECK(Near(frame.layers.Albedo(x, y, 0), 188, 3));
                    ++checked;
                }
            }
            CHECK(checked >= 100);
            device->WaitIdle();
        }
        device->WaitIdle();
        device.Reset();
    }
}

TEST_CASE("Tiles of different levels leave no hole between them", "[rhi][gpu][terrain]")
{
    FOR_EACH_GPU_BACKEND(backend)
    {
        gpu_test::BackendValidation validation(backend);
        rhi::DevicePtr device = MakeDevice(backend);
        {
            Rig rig;
            REQUIRE(rig.Create(device.Get()));
            TgMap map;
            REQUIRE(TgMake(device.Get(), &map, 513, 1.0f, 30.0f, &TgRough));
            TerrainPass pass(device.Get(), rig.sys, rig.frame);
            REQUIRE(pass.Valid());
            RenderScene scene(RenderSceneDesc{});
            TerrainSelectSettings select;
            select.first_range = 96.0f;
            REQUIRE(TerrainSelectSettingsValid(select, 1.0f));
            const Camera cam = TgCamera(Vec3(256, 250, 480), Vec3(256, 0, 305), Vec3(0, 1, 0), 1000.0f);
            // the tiles the camera sees reach more than one level
            Vector<TerrainTile> chosen;
            const Frustum frustum = Frustum::FromViewProjection(cam.ViewProjection());
            map.tiles.Select(cam.position, &frustum, select, &chosen);
            u32 max_level = 0;
            for (const TerrainTile& t : chosen)
            {
                max_level = Max(max_level, t.level);
            }
            REQUIRE(max_level >= 1);
            const TgFrame frame = TgRender(rig, pass, scene, cam, TgDraw(map), map.tiles, select);
            REQUIRE(frame.layers.ok);
            // every pixel of the lower half looks at the map, so no pixel there may still hold the cleared depth
            u32 holes = 0;
            for (u32 y = 36; y < kH; ++y)
            {
                for (u32 x = 0; x < kW; ++x)
                {
                    if (frame.layers.Depth(x, y) >= 1.0f)
                    {
                        ++holes;
                    }
                }
            }
            CHECK(holes == 0);
            device->WaitIdle();
        }
        device->WaitIdle();
        device.Reset();
    }
}

TEST_CASE("A box half buried in the terrain shows the terrain around it, and one below it is hidden",
          "[rhi][gpu][terrain]")
{
    FOR_EACH_GPU_BACKEND(backend)
    {
        gpu_test::BackendValidation validation(backend);
        rhi::DevicePtr device = MakeDevice(backend);
        {
            Rig rig;
            REQUIRE(rig.Create(device.Get()));
            TgMap map;
            REQUIRE(TgMake(device.Get(), &map, 65, 1.0f, 10.0f, &TgFlat));
            TerrainPass pass(device.Get(), rig.sys, rig.frame);
            REQUIRE(pass.Valid());
            MeshData d;
            REQUIRE(MakeCubeMesh(8.0f, &d) == nullptr);
            GpuMesh cube;
            REQUIRE(cube.Create(device.Get(), d, "cube"));
            rig.Prepare(&cube);
            const MaterialId red = CreateGBufferMaterial(rig.sys, rig.templates, GBufferOptions{}, "red");
            rig.sys->SetVec4(red, "base_color", Vec4(1.0f, 0.0f, 0.0f, 1.0f));
            RenderScene scene(RenderSceneDesc{});
            RenderObjectDesc up;  // centre on the ground: its top is 4 m up
            up.mesh = &cube;
            up.material = red.index;
            up.transform = ModelMatrix(Vec3(20, 0, 32), 1.0f, 0.0f);
            scene.AddObject(up);
            RenderObjectDesc down;  // centre 8 m below the ground: its top is 4 m below it
            down.mesh = &cube;
            down.material = red.index;
            down.transform = ModelMatrix(Vec3(44, -8, 32), 1.0f, 0.0f);
            scene.AddObject(down);
            const Camera cam = TgCamera(Vec3(32, 50, 32), Vec3(32, 0, 32), Vec3(0, 0, -1), 200.0f);
            const TgFrame frame = TgRender(rig, pass, scene, cam, TgDraw(map), map.tiles, TerrainSelectSettings{});
            REQUIRE(frame.layers.ok);
            const FrameData fd = MakeFrameData(cam, kW, kH, 0, 0, 0);
            const Pixel above = Project(cam, Vec3(20, 4, 32));
            const Pixel below = Project(cam, Vec3(44, 0, 32));
            const Pixel far = Project(cam, Vec3(32, 0, 12));
            // the box that rises out of the ground is red, 4 m up
            CHECK(frame.layers.Albedo(above.x, above.y, 0) > 200);
            CHECK(frame.layers.Albedo(above.x, above.y, 1) < 40);
            const Vec3 top = ReconstructWorldPosition(above.x, above.y, frame.layers.Depth(above.x, above.y), kW, kH,
                                                      fd.inverse_view_projection);
            CHECK(Abs(top.y - 4.0f) < 0.1f);
            // the one below it is under the terrain: grey, on the ground
            CHECK(Near(frame.layers.Albedo(below.x, below.y, 0), 188, 3));
            CHECK(Near(frame.layers.Albedo(below.x, below.y, 1), 188, 3));
            const Vec3 ground = ReconstructWorldPosition(below.x, below.y, frame.layers.Depth(below.x, below.y), kW, kH,
                                                         fd.inverse_view_projection);
            CHECK(Abs(ground.y) < 0.1f);
            CHECK(Near(frame.layers.Albedo(far.x, far.y, 0), 188, 3));
            device->WaitIdle();
        }
        device->WaitIdle();
        device.Reset();
    }
}

TEST_CASE("The terrain pass and its height texture draw the same after release and recreate", "[rhi][gpu][terrain]")
{
    FOR_EACH_GPU_BACKEND(backend)
    {
        gpu_test::BackendValidation validation(backend);
        rhi::DevicePtr device = MakeDevice(backend);
        {
            Rig rig;
            REQUIRE(rig.Create(device.Get()));
            TgMap map;
            REQUIRE(TgMake(device.Get(), &map, 129, 1.0f, 20.0f, &TgRamp));
            TerrainPass pass(device.Get(), rig.sys, rig.frame);
            REQUIRE(pass.Valid());
            RenderScene scene(RenderSceneDesc{});
            const Camera cam = TgCamera(Vec3(64, 60, 64), Vec3(64, 0, 64), Vec3(0, 0, -1), 200.0f);
            const TgFrame before = TgRender(rig, pass, scene, cam, TgDraw(map), map.tiles, TerrainSelectSettings{});
            REQUIRE(before.layers.ok);
            REQUIRE(before.terrain_added);
            pass.Release();
            map.texture->Release();
            CHECK_FALSE(pass.Valid());
            CHECK_FALSE(map.texture->Valid());
            REQUIRE(pass.Recreate(device.Get()));
            REQUIRE(map.texture->Recreate(device.Get()));
            REQUIRE(pass.Valid());
            REQUIRE(map.texture->Valid());
            const TgFrame after = TgRender(rig, pass, scene, cam, TgDraw(map), map.tiles, TerrainSelectSettings{});
            REQUIRE(after.layers.ok);
            CHECK(after.terrain_added);
            CHECK(Same(before.layers.depth, after.layers.depth));
            CHECK(Same(before.layers.albedo, after.layers.albedo));
            device->WaitIdle();
        }
        device->WaitIdle();
        device.Reset();
    }
}

TEST_CASE("A 4096 by 4096 map is a few hundred tiles in one draw", "[rhi][gpu][terrain]")
{
    FOR_EACH_GPU_BACKEND(backend)
    {
        gpu_test::BackendValidation validation(backend);
        rhi::DevicePtr device = MakeDevice(backend);
        {
            Rig rig;
            REQUIRE(rig.Create(device.Get()));
            // the quadtree of a 4096 by 4096 map (flat), drawn from a small height texture: the draw is the tiles
            TgMap small;
            REQUIRE(TgMake(device.Get(), &small, 129, 1.0f, 20.0f, &TgRamp));
            TerrainTilesDesc desc;
            desc.samples_x = desc.samples_z = 4097;
            desc.cell_size = 1.0f;
            desc.range_min = 0.0f;
            desc.range_max = 20.0f;
            desc.blocks_x = desc.blocks_z = 512;
            desc.block_min.Resize(512 * 512);
            desc.block_max.Resize(512 * 512);
            TerrainTiles big;
            REQUIRE(big.Build(desc));
            TerrainPass pass(device.Get(), rig.sys, rig.frame);
            REQUIRE(pass.Valid());
            RenderScene scene(RenderSceneDesc{});
            const Camera cam = TgCamera(Vec3(2048, 400, 2048), Vec3(2048, 0, 1700), Vec3(0, 1, 0), 6000.0f);
            const TgFrame frame = TgRender(rig, pass, scene, cam, TgDraw(small), big, TerrainSelectSettings{});
            REQUIRE(frame.layers.ok);
            CHECK(frame.terrain_added);
            CHECK(frame.drawn > 0);
            CHECK(frame.drawn <= 400);
            device->WaitIdle();
        }
        device->WaitIdle();
        device.Reset();
    }
}

TEST_CASE("A tile clipped by the map edge draws nothing past the edge", "[rhi][gpu][terrain]")
{
    FOR_EACH_GPU_BACKEND(backend)
    {
        gpu_test::BackendValidation validation(backend);
        rhi::DevicePtr device = MakeDevice(backend);
        {
            Rig rig;
            REQUIRE(rig.Create(device.Get()));
            TgMap map;
            REQUIRE(TgMake(device.Get(), &map, 100, 1.0f, 10.0f, &TgFlat));  // 99 cells: the last tile ends at 99 m
            TerrainPass pass(device.Get(), rig.sys, rig.frame);
            REQUIRE(pass.Valid());
            RenderScene scene(RenderSceneDesc{});
            const Camera cam = TgCamera(Vec3(64, 120, 64), Vec3(64, 0, 64), Vec3(0, 0, -1), 300.0f);
            const TgFrame frame = TgRender(rig, pass, scene, cam, TgDraw(map), map.tiles, TerrainSelectSettings{});
            REQUIRE(frame.layers.ok);
            const Pixel inside = Project(cam, Vec3(90, 0, 64));
            const Pixel outside = Project(cam, Vec3(108, 0, 64));
            CHECK(frame.layers.Depth(inside.x, inside.y) < 1.0f);
            CHECK(frame.layers.Depth(outside.x, outside.y) >= 1.0f);
            device->WaitIdle();
        }
        device->WaitIdle();
        device.Reset();
    }
}
