// Phase 22 Day 322: GPU skinning on a real device with validation on. A flat ribbon with two joints is bent by a joint
// palette and drawn into the G-buffer; the coverage and the normals are compared with a CPU reference (the same
// weighted matrix sum, rasterized on the CPU), and the image must not change across a device reset of every owner (the
// mesh with its skin stream, the joint palette buffer, the material system).
#include "lang/scalar.h"

#include <catch2/catch_test_macros.hpp>

#include "gbuffer_rig.h"
#include "os/os.h"
#include "renderer/device_resources.h"

using namespace engine_placeholder;
using namespace engine_placeholder::renderer;

using namespace gbuffer_rig;

namespace
{

// Joint 0 owns the ribbon's left (x <= -1), joint 1 the right (x >= 1); the middle column is shared 128 / 127.
bool MakeRibbon(MeshData* out)
{
    if (MakePlaneMesh(4.0f, 1.0f, 4, 1, out) != nullptr)
    {
        return false;
    }
    Vector<u8> skin;
    skin.Resize(static_cast<usize>(out->VertexCount()) * kSkinVertexBytes);
    for (u32 i = 0; i < out->VertexCount(); ++i)
    {
        const f32 x = out->PositionAt(i)[0];
        u8* e = skin.Data() + static_cast<usize>(i) * kSkinVertexBytes;
        MemSet(e, 0, kSkinVertexBytes);
        e[1] = 1;  // joint 1 in the second slot
        e[4] = x <= -1.0f ? 255 : (x >= 1.0f ? 0 : 128);
        e[5] = x <= -1.0f ? 0 : (x >= 1.0f ? 255 : 127);
    }
    return out->SetSkin(skin.Data(), 2) == nullptr;
}

// Two 3x4 row-major matrices: joint 0 stays, joint 1 is `rows`.
void MakePalette(const f32 (&joint1)[12], f32 (&out)[24])
{
    const f32 identity[12] = {1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0};
    MemCopy(out, identity, sizeof(identity));
    MemCopy(out + 12, joint1, sizeof(identity));
}

// The CPU reference: the vertex skinned with the weighted matrix sum.
Vec3 SkinPosition(const MeshData& mesh, u32 vertex, const f32* palette, const u8* skin)
{
    const f32* p = mesh.PositionAt(vertex);
    const u8* e = skin + static_cast<usize>(vertex) * kSkinVertexBytes;
    Vec3 out(0, 0, 0);
    for (u32 k = 0; k < 4; ++k)
    {
        const f32 w = static_cast<f32>(e[4 + k]) / 255.0f;
        const f32* m = palette + 12 * e[k];
        out = out + Vec3(m[0] * p[0] + m[1] * p[1] + m[2] * p[2] + m[3], m[4] * p[0] + m[5] * p[1] + m[6] * p[2] + m[7],
                         m[8] * p[0] + m[9] * p[1] + m[10] * p[2] + m[11]) *
                        w;
    }
    return out;
}

f32 Edge(f32 ax, f32 ay, f32 bx, f32 by, f32 px, f32 py)
{
    return (bx - ax) * (py - ay) - (by - ay) * (px - ax);
}

// Coverage of the skinned mesh at pixel centers, rasterized on the CPU through the camera's projection.
void ReferenceMask(const MeshData& mesh, const Vector<u8>& skin, const f32* palette, const Camera& cam,
                   Vector<u8>* mask)
{
    mask->Resize(kW * kH);
    MemSet(mask->Data(), 0, mask->Size());
    Vec3 world[16];
    f32 sx[16];
    f32 sy[16];
    REQUIRE(mesh.VertexCount() <= 16);
    for (u32 v = 0; v < mesh.VertexCount(); ++v)
    {
        world[v] = SkinPosition(mesh, v, palette, skin.Data());
        const Vec4 c = cam.ViewProjection() * Vec4(world[v].x, world[v].y, world[v].z, 1.0f);
        sx[v] = (c.x / c.w * 0.5f + 0.5f) * kW;
        sy[v] = (c.y / c.w * 0.5f + 0.5f) * kH;
    }
    for (u32 t = 0; t + 2 < mesh.IndexCount(); t += 3)
    {
        const u32 a = mesh.IndexAt(t);
        const u32 b = mesh.IndexAt(t + 1);
        const u32 c = mesh.IndexAt(t + 2);
        for (u32 y = 0; y < kH; ++y)
        {
            for (u32 x = 0; x < kW; ++x)
            {
                const f32 px = static_cast<f32>(x) + 0.5f;
                const f32 py = static_cast<f32>(y) + 0.5f;
                const f32 e0 = Edge(sx[a], sy[a], sx[b], sy[b], px, py);
                const f32 e1 = Edge(sx[b], sy[b], sx[c], sy[c], px, py);
                const f32 e2 = Edge(sx[c], sy[c], sx[a], sy[a], px, py);
                if ((e0 >= 0 && e1 >= 0 && e2 >= 0) || (e0 <= 0 && e1 <= 0 && e2 <= 0))
                {
                    (*mask)[y * kW + x] = 1;
                }
            }
        }
    }
}

u32 Differences(const Vector<u8>& a, const Vector<u8>& b)
{
    u32 n = 0;
    for (usize i = 0; i < a.Size(); ++i)
    {
        n += a[i] != b[i] ? 1u : 0u;
    }
    return n;
}

Vector<u8> GpuMask(const Layers& layers)
{
    Vector<u8> mask;
    mask.Resize(kW * kH);
    for (u32 i = 0; i < kW * kH; ++i)
    {
        mask[i] = layers.depth[i] < 1.0f ? 1 : 0;
    }
    return mask;
}

u32 Count(const Vector<u8>& mask)
{
    u32 n = 0;
    for (usize i = 0; i < mask.Size(); ++i)
    {
        n += mask[i];
    }
    return n;
}

}  // namespace

TEST_CASE("A skinned ribbon bent by a joint palette matches the CPU reference in coverage and normals",
          "[rhi][gpu][skinning]")
{
    FOR_EACH_GPU_BACKEND(backend)
    {
        gpu_test::BackendValidation validation(backend);
        rhi::DevicePtr device = MakeDevice(backend);
        {
            Rig rig;
            REQUIRE(rig.Create(device.Get()));
            REQUIRE(rig.templates.skinned.IsValid());
            MeshData data;
            REQUIRE(MakeRibbon(&data));
            Vector<u8> skin;
            skin.Resize(static_cast<usize>(data.VertexCount()) * kSkinVertexBytes);
            MemCopy(skin.Data(), data.SkinBytes(), skin.Size());
            GpuMesh mesh;
            REQUIRE(mesh.Create(device.Get(), data, "ribbon"));
            rig.Prepare(&mesh);
            GBufferOptions options;
            options.skinned = true;
            const MaterialId material = CreateGBufferMaterial(rig.sys, rig.templates, options, "ribbon");
            REQUIRE(material.IsValid());
            rig.sys->SetVec4(material, "base_color", Vec4(0.2f, 0.7f, 0.2f, 1.0f));
            const Camera cam = TopDownCamera();

            struct Pose
            {
                const char* name;
                f32 joint1[12];
            };
            const f32 c45 = 0.70710678f;
            const Pose poses[] = {
                {"rest", {1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0}},
                {"right half swung 90 degrees about Y", {0, 0, 1, 0, 0, 1, 0, 0, -1, 0, 0, 0}},
                {"right half rolled 45 degrees about X", {1, 0, 0, 0, 0, c45, -c45, 0, 0, c45, c45, 0}},
            };
            Vector<u8> rest_mask;
            for (const Pose& pose : poses)
            {
                INFO(pose.name);
                f32 palette[24];
                MakePalette(pose.joint1, palette);
                RenderScene scene(RenderSceneDesc{8, 4});
                RenderObjectDesc od;
                od.mesh = &mesh;
                od.material = material.index;
                od.joint_matrices = palette;
                od.joint_count = 2;
                scene.AddObject(od);
                const Layers L = rig.Render(scene, cam);
                INFO(L.error.CStr());
                REQUIRE(L.ok);
                CHECK(L.stats.drawn_objects == 1);
                CHECK(L.stats.skinned_dropped == 0);

                Vector<u8> reference;
                ReferenceMask(data, skin, palette, cam, &reference);
                const Vector<u8> gpu = GpuMask(L);
                CHECK(Count(gpu) > 80);
                CHECK(Differences(gpu, reference) <= 8);  // only pixels on an edge may differ
                if (rest_mask.Size() == 0)
                {
                    rest_mask = GpuMask(L);
                }
                else if (StringView(pose.name).StartsWith("right half swung"))
                {
                    CHECK(Differences(gpu, rest_mask) > 100);  // the bend moved the picture: not a rigid draw
                }
                if (StringView(pose.name).StartsWith("right half rolled"))
                {
                    // The right half's normal is tilted by the joint's rotation, the left half's is not.
                    const Pixel right = Project(cam, Vec3(1.5f, 0, 0));
                    const Vec3 nr = L.Normal(right.x, right.y);
                    CHECK(Abs(nr.y - c45) < 0.05f);
                    CHECK(Abs(nr.z - c45) < 0.05f);
                    const Pixel left = Project(cam, Vec3(-1.5f, 0, 0));
                    const Vec3 nl = L.Normal(left.x, left.y);
                    CHECK(Abs(nl.y - 1.0f) < 0.02f);
                }
            }
            device->WaitIdle();
        }
        device->WaitIdle();
        device.Reset();
    }
}

TEST_CASE("The skinned image survives a reset of the mesh, the palette buffer and the materials",
          "[rhi][gpu][skinning]")
{
    FOR_EACH_GPU_BACKEND(backend)
    {
        gpu_test::BackendValidation validation(backend);
        rhi::DevicePtr device = MakeDevice(backend);
        {
            DeviceResources registry;  // outlives every registration
            Rig rig;
            REQUIRE(rig.Create(device.Get()));
            // Owners that register: the material system, the frame resources and the mesh.
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
            REQUIRE(rig.templates.skinned.IsValid());

            MeshData data;
            REQUIRE(MakeRibbon(&data));
            GpuMesh mesh;
            REQUIRE(mesh.Create(device.Get(), data, "ribbon"));
            REQUIRE(mesh.MakeRecreatable(&registry, data, "ribbon"));
            rig.Prepare(&mesh);
            GBufferOptions options;
            options.skinned = true;
            const MaterialId material = CreateGBufferMaterial(rig.sys, rig.templates, options, "ribbon");
            REQUIRE(material.IsValid());
            const Camera cam = TopDownCamera();
            const f32 swing[12] = {0, 0, 1, 0, 0, 1, 0, 0, -1, 0, 0, 0};
            f32 palette[24];
            MakePalette(swing, palette);
            RenderScene scene(RenderSceneDesc{8, 4});
            RenderObjectDesc od;
            od.mesh = &mesh;
            od.material = material.index;
            od.joint_matrices = palette;
            od.joint_count = 2;
            scene.AddObject(od);

            const Layers before = rig.Render(scene, cam);
            REQUIRE(before.ok);
            const Vector<u8> before_mask = GpuMask(before);
            REQUIRE(Count(before_mask) > 80);

            device->WaitIdle();
            registry.OnDeviceReleasing(1);
            REQUIRE_FALSE(mesh.Valid());
            REQUIRE(registry.OnDeviceCreated(device.Get(), 2));
            REQUIRE(mesh.Valid());
            CHECK(mesh.SkinBuffer().IsValid());

            const Layers after = rig.Render(scene, cam);
            INFO(after.error.CStr());
            REQUIRE(after.ok);
            CHECK(Differences(GpuMask(after), before_mask) == 0);
            for (u32 i = 0; i < kW * kH; ++i)
            {
                CHECK(after.depth[i] == before.depth[i]);
            }
            device->WaitIdle();
        }
        device->WaitIdle();
        device.Reset();
    }
}

// Hidden benchmark for Day 338: 1,000 and 10,000 skinned instances of a 5,000-vertex, 40-joint mesh. Prints the CPU
// time of `GBufferPass::Prepare` (cull, palette copy, object data, sort) and of a whole frame with readback (CPU
// recording plus the GPU time, the backend reports no separate figure here).
TEST_CASE("Skinned instances: CPU prepare and frame time (recorded)", "[.][render-bench][skinning]")
{
    FOR_EACH_GPU_BACKEND(backend)
    {
        rhi::DevicePtr device = MakeDevice(backend);
        {
            Rig rig;
            REQUIRE(rig.Create(device.Get()));
            MeshData data;
            REQUIRE(MakeSphereMesh(1.0f, 69, 69, &data) == nullptr);  // 70 x 70 = 4,900 vertices
            Vector<u8> skin;
            skin.Resize(static_cast<usize>(data.VertexCount()) * kSkinVertexBytes);
            for (u32 i = 0; i < data.VertexCount(); ++i)
            {
                u8* e = skin.Data() + static_cast<usize>(i) * kSkinVertexBytes;
                const u32 j = (i / 2) % 39;
                e[0] = static_cast<u8>(j);
                e[1] = static_cast<u8>(j + 1);
                e[2] = e[3] = 0;
                e[4] = 128;
                e[5] = 127;
                e[6] = e[7] = 0;
            }
            REQUIRE(data.SetSkin(skin.Data(), 40) == nullptr);
            GpuMesh mesh;
            REQUIRE(mesh.Create(device.Get(), data, "bench"));
            rig.Prepare(&mesh);
            GBufferOptions options;
            options.skinned = true;
            const MaterialId material = CreateGBufferMaterial(rig.sys, rig.templates, options, "bench");
            REQUIRE(material.IsValid());
            Vector<f32> palette;
            palette.Resize(40 * 12);
            for (u32 j = 0; j < 40; ++j)
            {
                const f32 m[12] = {1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0};
                MemCopy(palette.Data() + j * 12, m, sizeof(m));
            }
            const Camera cam = TopDownCamera();
            for (const u32 count : {1000u, 10000u})
            {
                rig.frame_owner = MakeUnique<FrameResources>(device.Get(), count, 8, count * 40);
                rig.frame = rig.frame_owner.Get();
                RenderScene scene(RenderSceneDesc{count, 4});
                for (u32 i = 0; i < count; ++i)
                {
                    RenderObjectDesc od;
                    od.mesh = &mesh;
                    od.material = material.index;
                    od.transform = ModelMatrix(
                        Vec3(static_cast<f32>(i % 100) * 0.1f - 5.0f, 0, static_cast<f32>(i / 100) * 0.05f - 2.5f),
                        0.02f, 0.0f);
                    od.joint_matrices = palette.Data();
                    od.joint_count = 40;
                    REQUIRE(scene.AddObject(od).IsValid());
                }
                const u64 t0 = os::MonotonicTimeNanos();
                const Layers L = rig.Render(scene, cam);
                const u64 t1 = os::MonotonicTimeNanos();
                REQUIRE(L.ok);
                os::Print(
                    "skinned bench (%s): %u instances x 4,900 vertices, 40 joints: whole frame with readback %.2f ms, "
                    "%u drawn, %u draws\n",
                    device->BackendName(), count, static_cast<f64>(t1 - t0) / 1e6, L.stats.drawn_objects,
                    L.stats.draws);
            }
            device->WaitIdle();
        }
        device->WaitIdle();
        device.Reset();
    }
}
