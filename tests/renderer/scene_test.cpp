// Phase 5 Day 73: the render scene (object and light lists, culling) and the per-frame/per-object buffers, on the null
// device, plus a GPU leak test that creates and destroys many meshes.
#include "lang/new.h"
#include "os/console.h"
#include "os/os.h"
#include "containers/vector.h"

#include <catch2/catch_test_macros.hpp>

#include "backend_fixture.h"

#include "renderer/frame_data.h"
#include "renderer/render_scene.h"
#include "rhi/null/null_device.h"
#include "cstr/mem_ops.h"

using namespace engine_placeholder;
using namespace engine_placeholder::renderer;

namespace
{

struct NullFixture
{
    rhi::NullDevicePtr device = rhi::CreateNullDevice();
};

Frustum FrontFrustum()
{
    Camera cam = MakeLookAtCamera(Vec3(0, 0, 0), Vec3(0, 0, -1), Vec3(0, 1, 0));
    cam.near_plane = 0.5f;
    cam.far_plane = 100.0f;
    return Frustum::FromViewProjection(cam.ViewProjection());
}

}  // namespace

TEST_CASE("GpuMesh uploads vertices and indices and releases them", "[renderer][mesh]")
{
    NullFixture f;
    MeshData data;
    REQUIRE(MakeSphereMesh(1.0f, 12, 6, &data) == nullptr);
    {
        GpuMesh mesh;
        REQUIRE(mesh.Create(f.device.Get(), data, "sphere"));
        CHECK(mesh.Valid());
        CHECK(mesh.IndexCount() == data.IndexCount());
        CHECK(mesh.VertexCount() == data.VertexCount());
        CHECK(f.device->Statistics().buffers == 2);
        Vector<u8> back;
        back.Resize(data.VertexBytes());
        REQUIRE(f.device->PeekBuffer(mesh.VertexBuffer(), 0, back.Data(), back.Size()));
        CHECK(MemCompare(back.Data(), data.Vertices(), back.Size()) == 0);

        GpuMesh moved = static_cast<GpuMesh&&>(mesh);
        CHECK_FALSE(mesh.Valid());
        CHECK(moved.Valid());
    }
    f.device->WaitIdle();
    f.device->BeginFrame();
    f.device->EndFrame();
    CHECK(f.device->Statistics().buffers == 0);
    CHECK(f.device->ErrorCount() == 0);
}

TEST_CASE("Scene ids are generational and slots are reused", "[renderer][scene]")
{
    NullFixture f;
    MeshData data;
    REQUIRE(MakeCubeMesh(1.0f, &data) == nullptr);
    GpuMesh mesh;
    REQUIRE(mesh.Create(f.device.Get(), data, "cube"));
    RenderSceneDesc desc;
    desc.max_objects = 4;
    RenderScene scene(desc);
    RenderObjectDesc od;
    od.mesh = &mesh;
    ObjectId a = scene.AddObject(od);
    ObjectId b = scene.AddObject(od);
    REQUIRE(a.IsValid());
    REQUIRE(b.IsValid());
    CHECK(scene.ObjectCount() == 2);

    scene.RemoveObject(a);
    CHECK(scene.FindObject(a) == nullptr);  // stale id
    CHECK(scene.ObjectCount() == 1);

    ObjectId c = scene.AddObject(od);
    CHECK(c.index == a.index);
    CHECK(c.generation != a.generation);
    CHECK(scene.FindObject(a) == nullptr);
    CHECK(scene.FindObject(c) != nullptr);

    // Fills to capacity and then refuses.
    scene.AddObject(od);
    scene.AddObject(od);
    CHECK(scene.ObjectCount() == 4);
    CHECK_FALSE(scene.AddObject(od).IsValid());
}

TEST_CASE("Scene lights are added, changed and removed", "[renderer][scene]")
{
    RenderSceneDesc desc;
    desc.max_lights = 2;
    RenderScene scene(desc);
    Light l;
    l.type = LightType::Point;
    l.radius = 5.0f;
    LightId a = scene.AddLight(l);
    LightId b = scene.AddLight(l);
    CHECK_FALSE(scene.AddLight(l).IsValid());
    l.radius = 9.0f;
    scene.SetLight(a, l);
    CHECK(scene.FindLight(a)->radius == 9.0f);
    scene.RemoveLight(a);
    CHECK(scene.FindLight(a) == nullptr);
    CHECK(scene.FindLight(b) != nullptr);
    CHECK(scene.LightCount() == 1);
    CHECK(scene.AddLight(l).IsValid());
}

TEST_CASE("Culling keeps visible objects and drops hidden, removed and off-screen ones", "[renderer][scene]")
{
    NullFixture f;
    MeshData data;
    REQUIRE(MakeCubeMesh(1.0f, &data) == nullptr);
    GpuMesh mesh;
    REQUIRE(mesh.Create(f.device.Get(), data, "cube"));

    RenderScene scene(RenderSceneDesc{});
    auto add = [&](Vec3 p)
    {
        RenderObjectDesc d;
        d.mesh = &mesh;
        d.transform = ModelMatrix(p, 1.0f, 0.0f);
        return scene.AddObject(d);
    };
    ObjectId visible = add(Vec3(0, 0, -10));
    ObjectId behind = add(Vec3(0, 0, 10));
    ObjectId far_left = add(Vec3(-500, 0, -10));
    ObjectId hidden = add(Vec3(1, 0, -10));
    ObjectId removed = add(Vec3(2, 0, -10));
    ObjectId moved = add(Vec3(0, 0, 10));
    scene.SetFlags(hidden, ObjectFlags::kHidden);
    scene.RemoveObject(removed);
    scene.SetTransform(moved, ModelMatrix(Vec3(0, 0, -20), 1.0f, 0.0f));  // brought into view

    Vector<u32> out;
    out.Resize(scene.SlotCount());
    u32 n = scene.Cull(FrontFrustum(), out.Data(), static_cast<u32>(out.Size()));
    auto has = [&](ObjectId id)
    {
        for (u32 i = 0; i < n; ++i)
        {
            if (out[i] == id.index)
            {
                return true;
            }
        }
        return false;
    };
    CHECK(n == 2);
    CHECK(has(visible));
    CHECK(has(moved));
    CHECK_FALSE(has(behind));
    CHECK_FALSE(has(far_left));
    CHECK_FALSE(has(hidden));
    CHECK_FALSE(has(removed));
}

TEST_CASE("Frame data layout matches the shader's std140 view", "[renderer][framedata]")
{
    Camera cam = MakeLookAtCamera(Vec3(1, 2, 3), Vec3(0, 0, 0), Vec3(0, 1, 0));
    FrameData fd = MakeFrameData(cam, 1280, 720, 2.5f, 0.016f, 7);
    CHECK(fd.camera_position.x == 1.0f);
    CHECK(fd.camera_position.z == 3.0f);
    CHECK(fd.viewport.x == 1280.0f);
    CHECK(fd.viewport.y == 720.0f);
    CHECK(fd.time.x == 2.5f);
    CHECK(fd.time.y == 0.016f);
    CHECK(fd.time.z == 7.0f);
    CHECK(ENGINE_OFFSET_OF(FrameData, camera_position) == 256);
}

TEST_CASE("FrameResources writes per-object data into the current frame's region", "[renderer][framedata]")
{
    NullFixture f;
    MeshData data;
    REQUIRE(MakeCubeMesh(1.0f, &data) == nullptr);
    GpuMesh mesh;
    REQUIRE(mesh.Create(f.device.Get(), data, "cube"));
    RenderScene scene(RenderSceneDesc{});
    RenderObjectDesc d;
    d.mesh = &mesh;
    d.transform = ModelMatrix(Vec3(4, 0, 0), 2.0f, 0.0f);
    ObjectId id = scene.AddObject(d);
    d.transform = ModelMatrix(Vec3(0, 5, 0), 1.0f, 0.0f);
    ObjectId id2 = scene.AddObject(d);
    u32 slots[2] = {id2.index, id.index};

    FrameResources res(f.device.Get(), 16);
    REQUIRE(res.Valid());
    const u64 align = f.device->Caps().min_uniform_buffer_offset_alignment;

    res.BeginFrame(0);
    res.WriteFrameData(MakeFrameData(Camera(), 640, 480, 0, 0, 0));
    CHECK(res.WriteObjects(scene, slots, 2) == 2);
    res.Flush();
    BufferRegion r0 = res.ObjectRegion();
    CHECK(r0.offset % align == 0);
    ObjectData got[2];
    REQUIRE(f.device->PeekBuffer(r0.buffer, r0.offset, got, sizeof(got)));
    CHECK(got[0].world.col[3].y == 5.0f);  // order follows the slot list
    CHECK(got[1].world.col[3].x == 4.0f);

    res.BeginFrame(1);
    BufferRegion r1 = res.ObjectRegion();
    CHECK(r1.offset != r0.offset);  // a different region, so the GPU can still read frame 0's
    CHECK(r1.offset % align == 0);
    CHECK(res.FrameRegion().offset % align == 0);

    // Exactly the buffer's capacity fits.
    Vector<u32> many;
    many.Resize(16);
    for (usize i = 0; i < many.Size(); ++i)
    {
        many[i] = id.index;
    }
    CHECK(res.WriteObjects(scene, many.Data(), 16) == 16);
    CHECK(f.device->ErrorCount() == 0);
}

TEST_CASE("Culling and packing 100k objects (timing only)", "[renderer][scene][perf]")
{
    RenderSceneDesc desc;
    desc.max_objects = 100000;
    RenderScene scene(desc);
    MeshData data;
    REQUIRE(MakeCubeMesh(1.0f, &data) == nullptr);
    // Bounds come from the mesh; a null device is enough for a GpuMesh that is never drawn.
    NullFixture f;
    GpuMesh mesh;
    REQUIRE(mesh.Create(f.device.Get(), data, "cube"));
    for (u32 i = 0; i < 100000; ++i)
    {
        RenderObjectDesc d;
        d.mesh = &mesh;
        d.transform = ModelMatrix(Vec3(static_cast<f32>(i % 100) * 3 - 150, 0, -static_cast<f32>(i / 100) * 3), 1.0f,
                                  0.0f);
        REQUIRE(scene.AddObject(d).IsValid());
    }
    Vector<u32> out;
    out.Resize(scene.SlotCount());
    Frustum fr = FrontFrustum();
    const u64 t0 = os::MonotonicTimeNanos();
    u32 n = scene.Cull(fr, out.Data(), static_cast<u32>(out.Size()));
    const u64 t1 = os::MonotonicTimeNanos();
    os::Print("cull of 100000 objects: %u visible, %.3f ms\n", n, static_cast<double>(t1 - t0) / 1e6);
    CHECK(n > 0);
    CHECK(n < 100000);
}

TEST_CASE("Creating and destroying 1000 meshes on a GPU leaks nothing", "[renderer][mesh][gpu]")
{
    FOR_EACH_GPU_BACKEND(backend)
    {
        rhi::DeviceCreateInfo info;
        info.backend = backend;
        info.validation = rhi::ValidationSetting::On;
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
        MeshData data;
        REQUIRE(MakeSphereMesh(1.0f, 16, 8, &data) == nullptr);
        for (int i = 0; i < 1000; ++i)
        {
            GpuMesh mesh;
            REQUIRE(mesh.Create(device.Get(), data, "leak-test"));
            if (i % 100 == 99)
            {
                device->BeginFrame();
                device->EndFrame();
            }
        }
        device->WaitIdle();
        device->BeginFrame();
        device->EndFrame();
        device->WaitIdle();
        device.Reset();
    }
}
