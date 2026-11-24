// The instanced draw on a real device: a grid of 10,000 instances against a CPU model of what is visible, the set's own
// transform, a frame instance budget that is too small, a view that sees nothing, and the image across a device reset
// of the mesh, the materials and the frame resources.
#include "lang/scalar.h"
#include "log/output.h"

#include <catch2/catch_test_macros.hpp>

#include "gbuffer_rig.h"
#include "renderer/device_resources.h"
#include "renderer/instance_set.h"

using namespace vse;
using namespace vse::renderer;
using namespace gbuffer_rig;

namespace
{

constexpr u32 kGrid = 100;  // 100 x 100 = 10,000 instances, one metre apart

struct TintRule
{
    static Vec4 At(u32 i, u32 j)
    {
        return Vec4(static_cast<f32>(i % 4) / 3.0f, static_cast<f32>(j % 4) / 3.0f, 0.5f, 1.0f);
    }
};

InstanceSetPtr MakeGrid()
{
    InstanceSetDesc d;
    d.has_data = true;
    d.key_cell_size = 1.0;
    InstanceSetPtr set = InstanceSet::Create(d);
    REQUIRE(set.Get() != nullptr);
    for (u32 i = 0; i < kGrid; ++i)
    {
        for (u32 j = 0; j < kGrid; ++j)
        {
            InstanceTransform t;
            t.position = Vec3d(static_cast<f64>(i) - 50.0, 0.0, static_cast<f64>(j) - 50.0);
            InstanceExtra e;
            e.tint = TintRule::At(i, j);
            REQUIRE(set->Add(t, &e, nullptr) == InstanceStatus::Ok);
        }
    }
    return set;
}

// The byte the albedo target holds for a tinted white surface.
int ExpectedByte(f32 tint)
{
    return static_cast<int>(LinearToSrgb(SrgbToLinear(tint)) * 255.0f + 0.5f);
}

struct Scene
{
    InstanceSetPtr set;
    MeshData data;
    GpuMesh mesh;
    MaterialId material;
};

bool MakeScene(Rig& rig, Scene* s)
{
    s->set = MakeGrid();
    if (MakePlaneMesh(0.6f, 0.6f, 1, 1, &s->data) != nullptr || !s->mesh.Create(rig.device, s->data, "quad"))
    {
        return false;
    }
    rig.Prepare(&s->mesh);
    GBufferOptions options;
    options.instanced = true;
    s->material = CreateGBufferMaterial(rig.sys, rig.templates, options, "instanced");
    return s->material.IsValid();
}

InstancedObject MakeObject(const Scene& s)
{
    InstancedObject o;
    o.set = s.set;
    o.mesh = &s.mesh;
    o.material = s.material.index;
    return o;
}

}  // namespace

TEST_CASE("A grid of 10,000 instances draws what the CPU model says is visible, with each instance's tint",
          "[rhi][gpu][instancing]")
{
    FOR_EACH_GPU_BACKEND(backend)
    {
        gpu_test::BackendValidation validation(backend);
        rhi::DevicePtr device = MakeDevice(backend);
        {
            Rig rig;
            REQUIRE(rig.Create(device.Get()));
            if (!rig.templates.instanced.IsValid())
            {
                WARN("no instanced template on this backend (no bytecode); skipped");
                device->WaitIdle();
                continue;
            }
            Scene s;
            REQUIRE(MakeScene(rig, &s));
            RenderScene scene(RenderSceneDesc{8, 4});
            const InstancedId id = scene.AddInstanced(MakeObject(s));
            REQUIRE(id.IsValid());
            const Camera cam = TopDownCamera();
            const Layers L = rig.Render(scene, cam);
            INFO(L.error.CStr());
            REQUIRE(L.ok);
            CHECK(L.stats.instanced_sets == 1);
            CHECK(L.stats.draws == 1);
            CHECK(L.stats.instances_dropped == 0);
            CHECK(L.stats.instance_chunks_skipped > 0);  // most of the grid is outside the view

            // The model: an instance is visible when the sphere of its quad meets the frustum.
            const Frustum frustum = Frustum::FromViewProjection(cam.ViewProjection());
            const f32 radius = s.mesh.GetBounds().sphere.radius;
            u32 expected = 0;
            u32 checked = 0;
            for (u32 i = 0; i < kGrid; ++i)
            {
                for (u32 j = 0; j < kGrid; ++j)
                {
                    const Vec3 c(static_cast<f32>(i) - 50.0f, 0.0f, static_cast<f32>(j) - 50.0f);
                    if (frustum.TestSphere(Sphere{c, radius}) != Containment::Outside)
                    {
                        ++expected;
                    }
                    const Pixel p = Project(cam, c);
                    if (p.x >= 2 && p.x < kW - 2 && p.y >= 2 && p.y < kH - 2)
                    {
                        ++checked;
                        CHECK(L.Depth(p.x, p.y) < 1.0f);
                        CHECK(Abs(L.Albedo(p.x, p.y, 0) - ExpectedByte(TintRule::At(i, j).x)) <= 2);
                        CHECK(Abs(L.Albedo(p.x, p.y, 1) - ExpectedByte(TintRule::At(i, j).y)) <= 2);
                        CHECK(Abs(L.Albedo(p.x, p.y, 2) - ExpectedByte(0.5f)) <= 2);
                    }
                }
            }
            CHECK(checked > 20);
            CHECK(L.stats.instances_visible == expected);
            CHECK(expected < 10000);

            // Hidden: nothing is drawn.
            InstancedObject hidden = MakeObject(s);
            hidden.flags = ObjectFlags::kHidden;
            scene.SetInstanced(id, hidden);
            const Layers H = rig.Render(scene, cam);
            CHECK(H.stats.draws == 0);
            CHECK(H.stats.instances_visible == 0);
            for (u32 i = 0; i < kW * kH; ++i)
            {
                CHECK(H.depth[i] == 1.0f);
            }
            device->WaitIdle();
        }
        device->WaitIdle();
        device.Reset();
    }
}

TEST_CASE("The set's own transform moves, turns and scales every instance", "[rhi][gpu][instancing]")
{
    FOR_EACH_GPU_BACKEND(backend)
    {
        gpu_test::BackendValidation validation(backend);
        rhi::DevicePtr device = MakeDevice(backend);
        {
            Rig rig;
            REQUIRE(rig.Create(device.Get()));
            if (!rig.templates.instanced.IsValid())
            {
                device->WaitIdle();
                continue;
            }
            Scene s;
            REQUIRE(MakeScene(rig, &s));
            InstancedObject o = MakeObject(s);
            o.position = Vec3(1.0f, 0.0f, -2.0f);
            o.rotation = Quaternion::FromAxisAngle(Vec3(0, 1, 0), 0.5f * 3.14159265f);
            o.scale = 0.5f;
            RenderScene scene(RenderSceneDesc{8, 4});
            REQUIRE(scene.AddInstanced(o).IsValid());
            const Camera cam = TopDownCamera();
            const Layers L = rig.Render(scene, cam);
            REQUIRE(L.ok);
            u32 checked = 0;
            for (u32 i = 0; i < kGrid; ++i)
            {
                for (u32 j = 0; j < kGrid; ++j)
                {
                    const Vec3 local(static_cast<f32>(i) - 50.0f, 0.0f, static_cast<f32>(j) - 50.0f);
                    const Vec3 world = o.position + o.rotation.RotateVector(local * o.scale);
                    const Pixel p = Project(cam, world);
                    // Half-size instances are 0.3 m across: only the pixel whose center is inside is certain.
                    if (Abs(world.x) < 3.5f && Abs(world.z) < 3.5f)
                    {
                        ++checked;
                        CHECK(L.Depth(p.x, p.y) < 1.0f);
                        CHECK(Abs(L.Albedo(p.x, p.y, 0) - ExpectedByte(TintRule::At(i, j).x)) <= 2);
                    }
                }
            }
            CHECK(checked > 10);
            device->WaitIdle();
        }
        device->WaitIdle();
        device.Reset();
    }
}

TEST_CASE("A frame instance budget that is too small draws what fits and counts the rest", "[rhi][gpu][instancing]")
{
    FOR_EACH_GPU_BACKEND(backend)
    {
        gpu_test::BackendValidation validation(backend);
        rhi::DevicePtr device = MakeDevice(backend);
        {
            Rig rig;
            REQUIRE(rig.Create(device.Get()));
            if (!rig.templates.instanced.IsValid())
            {
                device->WaitIdle();
                continue;
            }
            rig.frame_owner = MakeUnique<FrameResources>(device.Get(), 64, 8, 256, 40);
            rig.frame = rig.frame_owner.Get();
            REQUIRE(rig.frame->Valid());
            Scene s;
            REQUIRE(MakeScene(rig, &s));
            RenderScene scene(RenderSceneDesc{8, 4});
            REQUIRE(scene.AddInstanced(MakeObject(s)).IsValid());
            const Layers L = rig.Render(scene, TopDownCamera());
            CHECK(L.stats.instances_visible == 40);
            CHECK(L.stats.instances_dropped > 0);
            CHECK(L.stats.draws == 1);
            device->WaitIdle();
        }
        device->WaitIdle();
        device.Reset();
    }
}

TEST_CASE("The instanced image survives a reset of the mesh, the materials and the frame resources",
          "[rhi][gpu][instancing]")
{
    FOR_EACH_GPU_BACKEND(backend)
    {
        gpu_test::BackendValidation validation(backend);
        rhi::DevicePtr device = MakeDevice(backend);
        {
            DeviceResources registry;
            Rig rig;
            REQUIRE(rig.Create(device.Get()));
            rig.pool_owner.Reset();
            rig.frame_owner.Reset();
            rig.sys_owner.Reset();
            MaterialSystemDesc md;
            md.resources = &registry;
            rig.sys_owner = MakeUnique<MaterialSystem>(device.Get(), md);
            rig.sys = rig.sys_owner.Get();
            rig.frame_owner = MakeUnique<FrameResources>(device.Get(), 64, 8, 256);
            rig.frame = rig.frame_owner.Get();
            REQUIRE(rig.frame->RegisterWith(&registry));
            rig.pool_owner = MakeUnique<TransientPool>(device.Get(), TransientPoolDesc{});
            rig.pool = rig.pool_owner.Get();
            rig.templates = CreateGBufferTemplates(rig.sys);
            if (!rig.templates.instanced.IsValid())
            {
                device->WaitIdle();
                continue;
            }
            Scene s;
            s.set = MakeGrid();
            REQUIRE(MakePlaneMesh(0.6f, 0.6f, 1, 1, &s.data) == nullptr);
            REQUIRE(s.mesh.Create(device.Get(), s.data, "quad"));
            REQUIRE(s.mesh.MakeRecreatable(&registry, s.data, "quad"));
            rig.Prepare(&s.mesh);
            GBufferOptions options;
            options.instanced = true;
            s.material = CreateGBufferMaterial(rig.sys, rig.templates, options, "instanced");
            REQUIRE(s.material.IsValid());
            RenderScene scene(RenderSceneDesc{8, 4});
            REQUIRE(scene.AddInstanced(MakeObject(s)).IsValid());
            const Camera cam = TopDownCamera();
            const Layers before = rig.Render(scene, cam);
            REQUIRE(before.ok);
            REQUIRE(before.stats.instances_visible > 20);

            device->WaitIdle();
            registry.OnDeviceReleasing(1);
            REQUIRE(registry.OnDeviceCreated(device.Get(), 2));
            const Layers after = rig.Render(scene, cam);
            INFO(after.error.CStr());
            REQUIRE(after.ok);
            CHECK(after.stats.instances_visible == before.stats.instances_visible);
            for (u32 i = 0; i < kW * kH * 4; ++i)
            {
                CHECK(after.albedo[i] == before.albedo[i]);
            }
            device->WaitIdle();
        }
        device->WaitIdle();
        device.Reset();
    }
}
