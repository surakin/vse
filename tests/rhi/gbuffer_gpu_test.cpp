// Phase 5 Day 79: the G-buffer geometry pass on a real device with validation and synchronization validation on.
// A known scene (a ground plane, two cubes sharing a mesh and a material, a sphere, an object outside the frustum) is
// rendered; the albedo, normal, material and depth layers are copied to host buffers by the graph and checked against
// values computed on the CPU. Further cases cover the vertex-color, normal-map and alpha-test permutations, draw-order
// independence, and the debug view.
#include <cmath>
#include <cstring>

#include <catch2/catch_test_macros.hpp>

#include "gbuffer_rig.h"

using namespace engine_placeholder;
using namespace engine_placeholder::renderer;

using namespace gbuffer_rig;


TEST_CASE("The G-buffer holds albedo, normal, material and depth of a known scene", "[rhi][gpu][gbuffer]") {
    FOR_EACH_GPU_BACKEND(backend) {
        gpu_test::BackendValidation validation(backend);
        rhi::DevicePtr device = MakeDevice(backend);
        {
            Rig rig;
            REQUIRE(rig.Create(device.Get()));
            char error[128];
            REQUIRE(CheckGBufferFormats(device.Get(), error, sizeof(error)));

            MeshData d;
            REQUIRE(MakePlaneMesh(16.0f, 16.0f, 1, 1, &d) == nullptr);
            GpuMesh plane;
            REQUIRE(plane.Create(device.Get(), d, "plane"));
            REQUIRE(MakeCubeMesh(2.0f, &d) == nullptr);
            GpuMesh cube;
            REQUIRE(cube.Create(device.Get(), d, "cube"));
            REQUIRE(MakeSphereMesh(1.0f, 32, 16, &d) == nullptr);
            GpuMesh sphere;
            REQUIRE(sphere.Create(device.Get(), d, "sphere"));
            rig.Prepare(&plane);
            rig.Prepare(&cube);
            rig.Prepare(&sphere);

            const MaterialId green = CreateGBufferMaterial(rig.sys, rig.templates, GBufferOptions{}, "ground");
            const MaterialId red = CreateGBufferMaterial(rig.sys, rig.templates, GBufferOptions{}, "cube");
            const MaterialId white = CreateGBufferMaterial(rig.sys, rig.templates, GBufferOptions{}, "sphere");
            REQUIRE(green.IsValid());
            REQUIRE(red.IsValid());
            REQUIRE(white.IsValid());
            rig.sys->SetVec4(green, "base_color", Vec4(0.0f, 0.6f, 0.0f, 1.0f));
            rig.sys->SetFloat(green, "roughness", 0.8f);
            rig.sys->SetFloat(green, "metallic", 0.0f);
            rig.sys->SetVec4(red, "base_color", Vec4(0.8f, 0.1f, 0.1f, 1.0f));
            rig.sys->SetFloat(red, "roughness", 0.4f);
            rig.sys->SetFloat(red, "metallic", 1.0f);
            rig.sys->SetVec4(white, "base_color", Vec4(0.5f, 0.5f, 0.5f, 1.0f));
            rig.sys->SetFloat(white, "emissive_scale", 0.25f);

            Camera cam = TopDownCamera();
            RenderScene scene(RenderSceneDesc{});
            RenderObjectDesc od;
            od.mesh = &plane;
            od.material = green.index;
            od.transform = ModelMatrix(Vec3(0, 0, 0), 1.0f, 0.0f);
            scene.AddObject(od);
            od.mesh = &cube;
            od.material = red.index;
            od.transform = ModelMatrix(Vec3(-3, 1, 0), 1.0f, 0.0f);
            scene.AddObject(od);
            od.transform = ModelMatrix(Vec3(-3, 1, 4), 1.0f, 0.0f);
            scene.AddObject(od);
            od.mesh = &sphere;
            od.material = white.index;
            od.transform = ModelMatrix(Vec3(3, 1, 0), 1.0f, 0.0f);
            scene.AddObject(od);
            od.mesh = &cube;
            od.material = red.index;
            od.transform = ModelMatrix(Vec3(200, 1, 0), 1.0f, 0.0f); // far outside the frustum
            scene.AddObject(od);

            const Layers L = rig.Render(scene, cam);
            INFO(L.error.CStr());
            REQUIRE(L.ok);
            CHECK(L.stats.visible == 4);
            CHECK(L.stats.drawn_objects == 4);
            CHECK(L.stats.draws == 3); // ground, the two cubes as one instanced draw, the sphere

            // Ground away from everything: albedo (0, 0.6, 0) -> sRGB bytes, normal +Y, material (0.8, 0, 0), depth at 10 units.
            const Pixel ground = Project(cam, Vec3(0, 0, 4));
            CHECK(Near(L.Albedo(ground.x, ground.y, 0), 0, 2));
            CHECK(Near(L.Albedo(ground.x, ground.y, 1), static_cast<int>(LinearToSrgb(0.6f) * 255.0f + 0.5f), 2));
            CHECK(Near(L.Albedo(ground.x, ground.y, 3), 255, 1));
            const Vec3 gn = L.Normal(ground.x, ground.y);
            CHECK(std::fabs(gn.y - 1.0f) < 0.01f);
            CHECK(Near(L.Material(ground.x, ground.y, 0), 204, 2));
            CHECK(Near(L.Material(ground.x, ground.y, 1), 0, 1));
            CHECK(std::fabs(L.Depth(ground.x, ground.y) - ground.depth) < 1e-4f);

            // The cube's top face (y = 2): nearer than the ground under it, so it wins the depth test whatever the draw order.
            const Pixel top = Project(cam, Vec3(-3, 2, 0));
            CHECK(Near(L.Albedo(top.x, top.y, 0), static_cast<int>(LinearToSrgb(0.8f) * 255.0f + 0.5f), 2));
            CHECK(Near(L.Albedo(top.x, top.y, 1), static_cast<int>(LinearToSrgb(0.1f) * 255.0f + 0.5f), 2));
            const Vec3 tn = L.Normal(top.x, top.y);
            CHECK(std::fabs(tn.y - 1.0f) < 0.01f);
            CHECK(Near(L.Material(top.x, top.y, 0), 102, 2));
            CHECK(Near(L.Material(top.x, top.y, 1), 255, 1));
            CHECK(std::fabs(L.Depth(top.x, top.y) - top.depth) < 1e-4f);
            CHECK(L.Depth(top.x, top.y) < Project(cam, Vec3(-3, 0, 0)).depth);

            // The instanced second cube.
            const Pixel top2 = Project(cam, Vec3(-3, 2, 4));
            CHECK(Near(L.Albedo(top2.x, top2.y, 0), static_cast<int>(LinearToSrgb(0.8f) * 255.0f + 0.5f), 2));
            CHECK(std::fabs(L.Depth(top2.x, top2.y) - top2.depth) < 1e-4f);

            // The sphere: a point 0.6 off its pole has the normal (0.6, 0.8, 0); the emissive scale lands in the material layer.
            const Pixel sp = Project(cam, Vec3(3.6f, 1.0f + std::sqrt(1.0f - 0.36f), 0));
            const Vec3 sn = L.Normal(sp.x, sp.y);
            CHECK(std::fabs(sn.x - 0.6f) < 0.08f);
            CHECK(std::fabs(sn.y - 0.8f) < 0.08f);
            CHECK(std::fabs(sn.z) < 0.08f);
            CHECK(Near(L.Material(sp.x, sp.y, 2), 64, 2));

            // Nothing was drawn where no object is: this corner lies on the ground, so use the cleared depth of a point outside
            // the ground instead (the ground is 16 wide, the view 11.5 wide at that distance, so every pixel is covered).
            for (u32 i = 0; i < kW * kH; ++i) CHECK(L.depth[i] < 1.0f);
            // The object outside the frustum left no trace: the pixels of its footprint do not exist, and it was not drawn.
            CHECK(L.stats.rejected == 0);

            device->WaitIdle();
        }
        device->WaitIdle();
        device.Reset();
    }
}

TEST_CASE("The G-buffer does not depend on the order objects were added", "[rhi][gpu][gbuffer]") {
    FOR_EACH_GPU_BACKEND(backend) {
        gpu_test::BackendValidation validation(backend);
        rhi::DevicePtr device = MakeDevice(backend);
        {
            Rig rig;
            REQUIRE(rig.Create(device.Get()));
            MeshData d;
            REQUIRE(MakeCubeMesh(2.0f, &d) == nullptr);
            GpuMesh cube;
            REQUIRE(cube.Create(device.Get(), d, "cube"));
            REQUIRE(MakeSphereMesh(1.0f, 24, 12, &d) == nullptr);
            GpuMesh sphere;
            REQUIRE(sphere.Create(device.Get(), d, "sphere"));
            REQUIRE(MakePlaneMesh(16.0f, 16.0f, 1, 1, &d) == nullptr);
            GpuMesh plane;
            REQUIRE(plane.Create(device.Get(), d, "plane"));
            rig.Prepare(&cube);
            rig.Prepare(&sphere);
            rig.Prepare(&plane);
            MaterialId mats[3];
            for (int i = 0; i < 3; ++i) {
                mats[i] = CreateGBufferMaterial(rig.sys, rig.templates, GBufferOptions{}, "m");
                rig.sys->SetVec4(mats[i], "base_color", Vec4(0.2f + 0.3f * i, 0.5f, 0.9f - 0.3f * i, 1.0f));
            }
            struct Item {
                const GpuMesh* mesh;
                int material;
                Vec3 position;
            };
            const Item items[] = {{&plane, 0, Vec3(0, 0, 0)}, {&cube, 1, Vec3(-3, 1, 0)}, {&sphere, 2, Vec3(3, 1, 0)}, {&cube, 1, Vec3(-3, 1, 4)},
                                  {&sphere, 2, Vec3(3, 1, -4)}, {&cube, 2, Vec3(0, 1, 3)}};
            const Camera cam = TopDownCamera();
            Layers results[2];
            for (int pass = 0; pass < 2; ++pass) {
                RenderScene scene(RenderSceneDesc{});
                for (int i = 0; i < 6; ++i) {
                    const Item& it = items[pass == 0 ? i : 5 - i];
                    RenderObjectDesc od;
                    od.mesh = it.mesh;
                    od.material = mats[it.material].index;
                    od.transform = ModelMatrix(it.position, 1.0f, 0.0f);
                    scene.AddObject(od);
                }
                results[pass] = rig.Render(scene, cam);
                REQUIRE(results[pass].ok);
            }
            // Four (mesh, material) groups: plane/0, cube/1 (two instances), sphere/2 (two instances), cube/2, however they were added.
            CHECK(results[0].stats.draws == 4);
            CHECK(results[1].stats.draws == 4);
            CHECK(Same(results[0].albedo, results[1].albedo));
            CHECK(Same(results[0].normal, results[1].normal));
            CHECK(Same(results[0].material, results[1].material));
            CHECK(Same(results[0].depth, results[1].depth));
            device->WaitIdle();
        }
        device->WaitIdle();
        device.Reset();
    }
}

TEST_CASE("Vertex colors blend across a mesh, authored in sRGB and landing in the albedo layer", "[rhi][gpu][gbuffer]") {
    FOR_EACH_GPU_BACKEND(backend) {
        gpu_test::BackendValidation validation(backend);
        rhi::DevicePtr device = MakeDevice(backend);
        {
            Rig rig;
            REQUIRE(rig.Create(device.Get()));
            MeshData plain;
            REQUIRE(MakePlaneMesh(16.0f, 16.0f, 1, 1, &plain) == nullptr);
            // Left vertices red, right vertices blue (x < 0 is the left edge).
            Vector<u8> colors;
            colors.Resize(plain.VertexCount() * 4);
            for (u32 i = 0; i < plain.VertexCount(); ++i) {
                const bool left = plain.PositionAt(i)[0] < 0.0f;
                colors[i * 4 + 0] = left ? 255 : 0;
                colors[i * 4 + 1] = 0;
                colors[i * 4 + 2] = left ? 0 : 255;
                colors[i * 4 + 3] = 255;
            }
            MeshData colored;
            REQUIRE(MakeColoredMesh(plain, colors.Data(), plain.VertexCount(), &colored) == nullptr);
            GpuMesh mesh;
            REQUIRE(mesh.Create(device.Get(), colored, "colored plane"));
            GpuMesh bare;
            REQUIRE(bare.Create(device.Get(), plain, "plane"));
            rig.Prepare(&mesh);
            rig.Prepare(&bare);
            GBufferOptions opt;
            opt.vertex_color = true;
            const MaterialId with = CreateGBufferMaterial(rig.sys, rig.templates, opt, "with colors");
            const MaterialId without = CreateGBufferMaterial(rig.sys, rig.templates, GBufferOptions{}, "without");
            REQUIRE(with.IsValid());
            REQUIRE(without.IsValid());
            rig.sys->SetVec4(without, "base_color", Vec4(0.25f, 0.5f, 0.75f, 1.0f));

            const Camera cam = TopDownCamera();
            RenderScene scene(RenderSceneDesc{});
            RenderObjectDesc od;
            od.mesh = &mesh;
            od.material = with.index;
            scene.AddObject(od);
            const Layers L = rig.Render(scene, cam);
            INFO(L.error.CStr());
            REQUIRE(L.ok);
            // Plane x in [-8, 8] on the ground at 10 units: the blend is linear across the screen. t = (x + 8) / 16.
            for (f32 x : {-4.0f, -2.0f, 0.0f, 2.0f, 4.0f}) {
                const Pixel p = Project(cam, Vec3(x, 0, 0));
                const f32 t = (x + 8.0f) / 16.0f;
                INFO("x = " << x);
                CHECK(Near(L.Albedo(p.x, p.y, 0), static_cast<int>(255.0f * (1.0f - t) + 0.5f), 6));
                CHECK(Near(L.Albedo(p.x, p.y, 1), 0, 2));
                CHECK(Near(L.Albedo(p.x, p.y, 2), static_cast<int>(255.0f * t + 0.5f), 6));
            }

            // A mesh without colors renders with the base color only.
            RenderScene scene2(RenderSceneDesc{});
            od.mesh = &bare;
            od.material = without.index;
            scene2.AddObject(od);
            const Layers B = rig.Render(scene2, cam);
            REQUIRE(B.ok);
            const Pixel c = Project(cam, Vec3(0, 0, 0));
            CHECK(Near(B.Albedo(c.x, c.y, 0), static_cast<int>(LinearToSrgb(0.25f) * 255.0f + 0.5f), 2));
            CHECK(Near(B.Albedo(c.x, c.y, 2), static_cast<int>(LinearToSrgb(0.75f) * 255.0f + 0.5f), 2));

            // A colored mesh through a material that ignores colors, and the reverse, are named errors; the object is left out.
            RenderScene bad(RenderSceneDesc{});
            od.mesh = &mesh;
            od.material = without.index;
            bad.AddObject(od);
            const Layers E = rig.Render(bad, cam);
            CHECK_FALSE(E.ok);
            CHECK(E.stats.rejected == 1);
            CHECK(E.error.View().Contains(StringView("vertex colors")));
            CHECK(E.stats.drawn_objects == 0);
            device->WaitIdle();
        }
        device->WaitIdle();
        device.Reset();
    }
}

TEST_CASE("The normal map and alpha test permutations change the layers they should", "[rhi][gpu][gbuffer]") {
    FOR_EACH_GPU_BACKEND(backend) {
        gpu_test::BackendValidation validation(backend);
        rhi::DevicePtr device = MakeDevice(backend);
        {
            Rig rig;
            REQUIRE(rig.Create(device.Get()));
            MeshData d;
            REQUIRE(MakePlaneMesh(16.0f, 16.0f, 1, 1, &d) == nullptr);
            GpuMesh plane;
            REQUIRE(plane.Create(device.Get(), d, "plane"));
            rig.Prepare(&plane);

            // A tangent-space normal (0.6, 0, 0.8) as a 1x1 texture: (0.8, 0.5, 0.9).
            rhi::TextureDesc td;
            td.width = 1;
            td.height = 1;
            td.format = rhi::Format::RGBA8_UNorm;
            td.usage = rhi::TextureUsage::Sampled | rhi::TextureUsage::CopyDst;
            const rhi::TextureHandle normal_tex = device->CreateTexture(td);
            const u8 texel[4] = {204, 128, 230, 255};
            device->UploadTexture(normal_tex, 0, 0, texel, 0);

            GBufferOptions nm;
            nm.normal_map = true;
            const MaterialId mapped = CreateGBufferMaterial(rig.sys, rig.templates, nm, "mapped");
            REQUIRE(mapped.IsValid());
            REQUIRE(rig.sys->SetTexture(mapped, "normal_tex", normal_tex));
            const Camera cam = TopDownCamera();
            RenderScene scene(RenderSceneDesc{});
            RenderObjectDesc od;
            od.mesh = &plane;
            od.material = mapped.index;
            scene.AddObject(od);
            const Layers L = rig.Render(scene, cam);
            REQUIRE(L.ok);
            const Pixel c = Project(cam, Vec3(0, 0, 0));
            const Vec3 n = L.Normal(c.x, c.y);
            INFO("normal " << n.x << " " << n.y << " " << n.z);
            // The plane's tangent is +X with w = 1 and its normal +Y, so the bitangent is -Z: (0.6, 0.8, 0).
            CHECK(std::fabs(n.x - 0.6f) < 0.03f);
            CHECK(std::fabs(n.y - 0.8f) < 0.03f);
            CHECK(std::fabs(n.z) < 0.03f);

            // Alpha test: alpha 0.3 under a cutoff of 0.5 leaves the pixel cleared; alpha 0.8 draws it.
            GBufferOptions at;
            at.alpha_test = true;
            const MaterialId cut = CreateGBufferMaterial(rig.sys, rig.templates, at, "cut");
            REQUIRE(cut.IsValid());
            rig.sys->SetVec4(cut, "base_color", Vec4(1, 1, 1, 0.3f));
            RenderScene scene2(RenderSceneDesc{});
            od.material = cut.index;
            scene2.AddObject(od);
            const Layers C = rig.Render(scene2, cam);
            REQUIRE(C.ok);
            CHECK(C.Depth(c.x, c.y) == 1.0f);
            CHECK(C.Albedo(c.x, c.y, 3) == 0);
            rig.sys->SetVec4(cut, "base_color", Vec4(1, 1, 1, 0.8f));
            const Layers K = rig.Render(scene2, cam);
            REQUIRE(K.ok);
            CHECK(K.Depth(c.x, c.y) < 1.0f);
            CHECK(K.Albedo(c.x, c.y, 3) == 255);

            device->WaitIdle();
            device->DestroyTexture(normal_tex);
        }
        device->WaitIdle();
        device.Reset();
    }
}

TEST_CASE("The debug view shows the albedo, normal, material and depth layers", "[rhi][gpu][gbuffer]") {
    FOR_EACH_GPU_BACKEND(backend) {
        gpu_test::BackendValidation validation(backend);
        rhi::DevicePtr device = MakeDevice(backend);
        {
            Rig rig;
            REQUIRE(rig.Create(device.Get()));
            GBufferDebugView view(device.Get());
            REQUIRE(view.Valid());
            MeshData d;
            REQUIRE(MakePlaneMesh(16.0f, 16.0f, 1, 1, &d) == nullptr);
            GpuMesh plane;
            REQUIRE(plane.Create(device.Get(), d, "plane"));
            rig.Prepare(&plane);
            const MaterialId m = CreateGBufferMaterial(rig.sys, rig.templates, GBufferOptions{}, "m");
            rig.sys->SetVec4(m, "base_color", Vec4(0.5f, 0.25f, 1.0f, 1.0f));
            rig.sys->SetFloat(m, "roughness", 0.2f);
            rig.sys->SetFloat(m, "metallic", 0.6f);
            Camera cam = TopDownCamera();
            RenderScene scene(RenderSceneDesc{});
            RenderObjectDesc od;
            od.mesh = &plane;
            od.material = m.index;
            scene.AddObject(od);
            FitCameraDepthRange(scene, &cam, 0.1f);
            const Pixel c = Project(cam, Vec3(0, 0, 0));
            Vector<u8> out;
            rig.Render(scene, cam, &view, GBufferLayer::Albedo, &out);
            // The target is RGBA8_UNorm (no sRGB encoding), so the shown value is the linear albedo.
            CHECK(Near(out[(c.y * kW + c.x) * 4 + 0], 128, 3));
            CHECK(Near(out[(c.y * kW + c.x) * 4 + 1], 64, 3));
            CHECK(Near(out[(c.y * kW + c.x) * 4 + 2], 255, 3));
            rig.Render(scene, cam, &view, GBufferLayer::Normal, &out);
            CHECK(Near(out[(c.y * kW + c.x) * 4 + 0], 128, 3)); // (0, 1, 0) * 0.5 + 0.5
            CHECK(Near(out[(c.y * kW + c.x) * 4 + 1], 255, 3));
            CHECK(Near(out[(c.y * kW + c.x) * 4 + 2], 128, 3));
            rig.Render(scene, cam, &view, GBufferLayer::Material, &out);
            CHECK(Near(out[(c.y * kW + c.x) * 4 + 0], 51, 3));
            CHECK(Near(out[(c.y * kW + c.x) * 4 + 1], 153, 3));
            rig.Render(scene, cam, &view, GBufferLayer::Depth, &out);
            // The plane is 10 units from the camera; the fitted far plane is the distance of its farthest sphere point.
            const f32 expected = 10.0f / cam.far_plane;
            CHECK(Near(out[(c.y * kW + c.x) * 4 + 0], static_cast<int>(expected * 255.0f + 0.5f), 5));
            device->WaitIdle();
        }
        device->WaitIdle();
        device.Reset();
    }
}
