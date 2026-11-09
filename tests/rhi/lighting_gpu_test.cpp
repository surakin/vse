// Phase 5 Day 80: deferred lighting and tonemapping on a real device with validation and synchronization validation on.
// A G-buffer frame is lit by LightingPass and tonemapped by TonemapPass; the HDR accumulation target and the display
// target are read back and compared with the CPU twins of the shader code at probe pixels.
#include <chrono>
#include <cmath>
#include <cstdio>
#include "lang/initializer_list.h"

#include <catch2/catch_test_macros.hpp>

#include "containers/span.h"
#include "gbuffer_rig.h"
#include "renderer/lighting.h"

using namespace engine_placeholder;
using namespace engine_placeholder::renderer;
using namespace gbuffer_rig;

namespace {

struct LitFrame {
    Vector<u16> accum; // RGBA16_Float
    Vector<u8> display; // RGBA8_SRGB
    Vector<u8> debug;   // RGBA8_UNorm, the debug layer when one was asked for
    bool ok = false;
    LightSummary summary;
    u32 point_draws_planned = 0;
    u32 point_draws_recorded = 0;
    engine_test::Text error = engine_test::MakeText();

    f32 Accum(u32 x, u32 y, u32 c) const { return HalfToFloat(accum[(y * kW + x) * 4 + c]); }
    int Display(u32 x, u32 y, u32 c) const { return display[(y * kW + x) * 4 + c]; }
};

struct CopyAccum {
    GraphResource accum;
    GraphResource buffer;
};

void CopyAccumPass(const PassExecuteContext& ctx, void* user) {
    const CopyAccum* job = static_cast<const CopyAccum*>(user);
    ctx.list->CopyTextureToBuffer(ctx.graph->ResolvedTexture(job->accum), ctx.graph->ImportedBuffer(job->buffer), rhi::BufferTextureCopy{});
}

// Renders the scene through the G-buffer, lighting and tonemap passes. `debug_layer` >= 0 also shows that G-buffer layer.
LitFrame RenderLit(Rig& rig, const RenderScene& scene, const Camera& camera, f32 exposure, int debug_layer = -1) {
    LitFrame out;
    GBufferPass gbuffer(rig.device, rig.sys, rig.frame);
    LightingPass lighting(rig.device, rig.sys, rig.frame);
    TonemapPass tonemap(rig.device);
    GBufferDebugView view(rig.device);
    REQUIRE(lighting.Valid());
    REQUIRE(tonemap.Valid());
    const u64 f = rig.frame_index++;
    rig.device->BeginFrame();
    rig.sys->BeginFrame(f);
    rig.sys->FlushMaterials();
    rig.frame->BeginFrame(f);
    const FrameData frame_data = MakeFrameData(camera, kW, kH, 0, 0, f);
    rig.frame->WriteFrameData(frame_data);
    out.ok = gbuffer.Prepare(scene, Frustum::FromViewProjection(camera.ViewProjection()));
    out.error = engine_test::MakeText(StringView(gbuffer.LastError()));
    out.summary = lighting.Prepare(scene, frame_data);
    rig.frame->Flush();

    rhi::BufferDesc bd;
    bd.size = kW * kH * 8;
    bd.location = rhi::MemoryLocation::GpuOnly;
    bd.usage = rhi::BufferUsage::CopyDst | rhi::BufferUsage::CopySrc;
    const rhi::BufferHandle accum_buf = rig.device->CreateBuffer(bd);
    REQUIRE(accum_buf.IsValid());
    rhi::TextureDesc td;
    td.width = kW;
    td.height = kH;
    td.format = rhi::Format::RGBA8_SRGB;
    td.usage = rhi::TextureUsage::ColorAttachment | rhi::TextureUsage::CopySrc;
    const rhi::TextureHandle display = rig.device->CreateTexture(td);
    td.format = rhi::Format::RGBA8_UNorm;
    const rhi::TextureHandle debug = rig.device->CreateTexture(td);
    REQUIRE(display.IsValid());
    REQUIRE(debug.IsValid());

    ArenaAllocator arena(1 << 20);
    {
        RenderGraph graph(&arena, RenderGraphDesc{});
        rig.pool->BeginFrame(f);
        const GBufferTargets targets = gbuffer.AddToGraph(&graph, kW, kH);
        const GraphResource accum = lighting.AddToGraph(&graph, targets, kW, kH);
        const GraphResource display_res = graph.ImportTexture("display", display, rhi::ResourceState::Undefined, rhi::ResourceState::CopySrc, false);
        tonemap.AddToGraph(&graph, accum, display_res, rhi::Format::RGBA8_SRGB, exposure);
        CopyAccum job;
        job.accum = accum;
        job.buffer = graph.ImportBuffer("accum host", accum_buf, rhi::ResourceState::Undefined, rhi::ResourceState::CopySrc, false);
        graph.AddPass("readback accum", &CopyAccumPass, &job, PassFlags::SideEffects).Read(accum, GraphUsage::CopySrc).Write(job.buffer, GraphUsage::CopyDst);
        if (debug_layer >= 0) {
            const GraphResource debug_res = graph.ImportTexture("debug", debug, rhi::ResourceState::Undefined, rhi::ResourceState::CopySrc, false);
            view.SetInverseViewProjection(frame_data.inverse_view_projection);
            view.AddToGraph(&graph, targets, static_cast<GBufferLayer>(debug_layer), debug_res, rhi::Format::RGBA8_UNorm, camera.near_plane, camera.far_plane);
        }
        REQUIRE(graph.Compile());
        rhi::CommandList* list = rig.device->CreateCommandList();
        list->Begin();
        const bool executed = graph.Execute(rig.pool, list);
        if (!executed) FAIL(graph.ExecuteError());
        list->End();
        rhi::CommandList* lists[] = {list};
        REQUIRE(rig.device->Submit(lists, 1) != 0);
    }
    out.point_draws_planned = lighting.PointDrawsPlanned();
    out.point_draws_recorded = lighting.PointDrawsRecorded();
    rig.device->EndFrame();
    rig.device->WaitIdle();
    out.accum.Resize(kW * kH * 4);
    out.display.Resize(kW * kH * 4);
    REQUIRE(rig.device->ReadbackBuffer(accum_buf, 0, out.accum.Data(), kW * kH * 8));
    REQUIRE(rig.device->ReadbackTexture(display, 0, 0, out.display.Data(), out.display.Size(), rhi::ResourceState::CopySrc));
    if (debug_layer >= 0) {
        out.debug.Resize(kW * kH * 4);
        REQUIRE(rig.device->ReadbackTexture(debug, 0, 0, out.debug.Data(), out.debug.Size(), rhi::ResourceState::CopySrc));
    }
    rig.device->DestroyTexture(debug);
    rig.device->DestroyTexture(display);
    rig.device->DestroyBuffer(accum_buf);
    return out;
}

// The color the CPU model gives for a flat surface at a world position.
Vec3 Expected(const SurfacePoint& s, const Vec3& eye, const Vec3& world, engine_placeholder::Span<const Light> lights) {
    const Vec3 to_eye = eye - world;
    const f32 len = std::sqrt(to_eye.x * to_eye.x + to_eye.y * to_eye.y + to_eye.z * to_eye.z);
    const Vec3 v(to_eye.x / len, to_eye.y / len, to_eye.z / len);
    Vec3 ambient(0, 0, 0);
    for (const Light& l : lights) {
        if (l.type == LightType::Ambient) ambient = ambient + l.color * l.intensity;
    }
    Vec3 color = ShadeAmbient(s, ambient);
    for (const Light& l : lights) {
        if (l.type == LightType::Directional) color = color + ShadeLight(s, v, l.direction, l.color * l.intensity);
        if (l.type == LightType::Point) {
            const Vec3 to = l.position - world;
            const f32 d = std::sqrt(to.x * to.x + to.y * to.y + to.z * to.z);
            if (d > 0.0f) color = color + ShadeLight(s, v, Vec3(to.x / d, to.y / d, to.z / d), l.color * (l.intensity * PointLightAttenuation(d, l.radius)));
        }
    }
    return color;
}

Vec3 Expected(const SurfacePoint& s, const Vec3& eye, const Vec3& world, InitializerList<Light> lights) {
    Vector<Light> list;
    for (const Light& l : lights) list.Push(l);
    return Expected(s, eye, world, engine_placeholder::Span<const Light>(list));
}

bool Close(f32 got, f32 want, f32 relative = 0.02f) { return std::fabs(got - want) <= 0.004f + relative * std::fabs(want); }

int Srgb8(f32 linear) { return static_cast<int>(LinearToSrgb(linear < 0.0f ? 0.0f : (linear > 1.0f ? 1.0f : linear)) * 255.0f + 0.5f); }

} // namespace

TEST_CASE("A lit plane matches the CPU lighting model, and background pixels stay black", "[rhi][gpu][lighting]") {
    FOR_EACH_GPU_BACKEND(backend) {
        gpu_test::BackendValidation validation(backend);
        rhi::DevicePtr device = MakeDevice(backend);
        {
            Rig rig;
            REQUIRE(rig.Create(device.Get()));
            MeshData d;
            REQUIRE(MakePlaneMesh(6.0f, 6.0f, 1, 1, &d) == nullptr); // smaller than the view: the corners are background
            GpuMesh plane;
            REQUIRE(plane.Create(device.Get(), d, "plane"));
            rig.Prepare(&plane);
            const MaterialId m = CreateGBufferMaterial(rig.sys, rig.templates, GBufferOptions{}, "m");
            rig.sys->SetVec4(m, "base_color", Vec4(0.6f, 0.5f, 0.4f, 1.0f));
            rig.sys->SetFloat(m, "roughness", 0.6f);
            rig.sys->SetFloat(m, "metallic", 0.0f);
            rig.sys->SetFloat(m, "occlusion", 0.75f);
            rig.sys->SetFloat(m, "emissive_scale", 0.05f);
            RenderScene scene(RenderSceneDesc{});
            RenderObjectDesc od;
            od.mesh = &plane;
            od.material = m.index;
            scene.AddObject(od);
            Vector<Light> lights;
            Light sun;
            sun.type = LightType::Directional;
            sun.direction = Vec3(0.3f, 0.8f, 0.5f);
            {
                const Vec3& v = sun.direction;
                const f32 len = std::sqrt(v.x * v.x + v.y * v.y + v.z * v.z);
                sun.direction = Vec3(v.x / len, v.y / len, v.z / len);
            }
            sun.color = Vec3(1.0f, 0.9f, 0.8f);
            sun.intensity = 2.0f;
            Light fill;
            fill.type = LightType::Directional;
            fill.direction = Vec3(-0.6f, 0.2f, -0.77f);
            {
                const Vec3& v = fill.direction;
                const f32 len = std::sqrt(v.x * v.x + v.y * v.y + v.z * v.z);
                fill.direction = Vec3(v.x / len, v.y / len, v.z / len);
            }
            fill.color = Vec3(0.3f, 0.4f, 1.0f);
            fill.intensity = 0.5f;
            Light ambient;
            ambient.type = LightType::Ambient;
            ambient.color = Vec3(0.2f, 0.25f, 0.3f);
            ambient.intensity = 0.5f;
            lights.Push(sun);
        lights.Push(fill);
        lights.Push(ambient);
            for (usize i = 0; i < lights.Size(); ++i) scene.AddLight(lights[i]);

            const Camera cam = TopDownCamera();
            const LitFrame L = RenderLit(rig, scene, cam, 1.0f);
            INFO(L.error.CStr());
            REQUIRE(L.ok);
            CHECK(L.summary.directional_count == 2);
            CHECK(std::fabs(L.summary.ambient.x - 0.1f) < 1e-6f);
            CHECK(std::fabs(L.summary.ambient.y - 0.125f) < 1e-6f);
            CHECK(std::fabs(L.summary.ambient.z - 0.15f) < 1e-6f);

            SurfacePoint surface;
            surface.albedo = Vec3(0.6f, 0.5f, 0.4f);
            surface.roughness = 0.6f;
            surface.metallic = 0.0f;
            surface.occlusion = 0.75f;
            surface.emissive_scale = 0.05f;
            surface.normal = Vec3(0, 1, 0);
            const FrameData fd = MakeFrameData(cam, kW, kH, 0, 0, 0);
            for (const Vec3& probe : {Vec3(0, 0, 0), Vec3(1.5f, 0, -1.0f), Vec3(-2.0f, 0, 2.0f)}) {
                const Pixel p = Project(cam, probe);
                const Vec3 world = ReconstructWorldPosition(p.x, p.y, p.depth, kW, kH, fd.inverse_view_projection);
                const Vec3 want = Expected(surface, cam.position, world, engine_placeholder::Span<const Light>(lights));
                INFO("probe " << probe.x << " " << probe.z << " want " << want.x << " " << want.y << " " << want.z << " got " << L.Accum(p.x, p.y, 0) << " " << L.Accum(p.x, p.y, 1) << " " << L.Accum(p.x, p.y, 2));
                CHECK(Close(L.Accum(p.x, p.y, 0), want.x));
                CHECK(Close(L.Accum(p.x, p.y, 1), want.y));
                CHECK(Close(L.Accum(p.x, p.y, 2), want.z));
                // The tonemap: the display value is the ACES curve of the HDR value, sRGB-encoded by the target format.
                for (u32 c = 0; c < 3; ++c) {
                    CHECK(Near(L.Display(p.x, p.y, c), Srgb8(TonemapAces(L.Accum(p.x, p.y, c))), 2));
                }
            }
            // The plane is 6 wide and the view 11.5: the corners are background, skipped by the lighting pass and left black.
            for (u32 c = 0; c < 4; ++c) CHECK(L.accum[(2 * kW + 2) * 4 + c] == 0);
            CHECK(L.Display(2, 2, 0) == 0);

            // Exposure scales the HDR value before the operator.
            const LitFrame H = RenderLit(rig, scene, cam, 0.5f);
            REQUIRE(H.ok);
            const Pixel c = Project(cam, Vec3(0, 0, 0));
            for (u32 ch = 0; ch < 3; ++ch) {
                CHECK(Near(H.Display(c.x, c.y, ch), Srgb8(TonemapAces(0.5f * H.Accum(c.x, c.y, ch))), 2));
            }
            device->WaitIdle();
        }
        device->WaitIdle();
        device.Reset();
    }
}

TEST_CASE("Sweeping the light direction follows max(dot(N, L), 0) and the GGX lobe, metals have no diffuse", "[rhi][gpu][lighting]") {
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
            const MaterialId rough = CreateGBufferMaterial(rig.sys, rig.templates, GBufferOptions{}, "rough");
            rig.sys->SetVec4(rough, "base_color", Vec4(0.5f, 0.5f, 0.5f, 1.0f));
            rig.sys->SetFloat(rough, "roughness", 1.0f);
            const MaterialId metal = CreateGBufferMaterial(rig.sys, rig.templates, GBufferOptions{}, "metal");
            rig.sys->SetVec4(metal, "base_color", Vec4(1.0f, 0.8f, 0.2f, 1.0f));
            rig.sys->SetFloat(metal, "roughness", 0.3f);
            rig.sys->SetFloat(metal, "metallic", 1.0f);
            const Camera cam = TopDownCamera();
            const Pixel c = Project(cam, Vec3(0, 0, 0));
            const FrameData fd = MakeFrameData(cam, kW, kH, 0, 0, 0);
            const Vec3 world = ReconstructWorldPosition(c.x, c.y, c.depth, kW, kH, fd.inverse_view_projection);

            for (int which = 0; which < 2; ++which) {
                RenderScene scene(RenderSceneDesc{});
                RenderObjectDesc od;
                od.mesh = &plane;
                od.material = (which == 0 ? rough : metal).index;
                scene.AddObject(od);
                Light sun;
                sun.type = LightType::Directional;
                sun.intensity = 1.5f;
                const LightId id = scene.AddLight(sun);
                SurfacePoint surface;
                surface.albedo = which == 0 ? Vec3(0.5f, 0.5f, 0.5f) : Vec3(1.0f, 0.8f, 0.2f);
                surface.roughness = which == 0 ? 1.0f : 0.3f;
                surface.metallic = which == 0 ? 0.0f : 1.0f;
                f32 previous = -1.0f;
                for (int degrees : {0, 30, 60, 85, 95, 120}) {
                    const f32 theta = static_cast<f32>(degrees) * 3.14159265f / 180.0f;
                    sun.direction = Vec3(std::sin(theta), std::cos(theta), 0.0f);
                    scene.SetLight(id, sun);
                    // The scene may hold a direction that is not a unit vector; the light buffer holds it normalized, so the
                    // image must equal the unit-vector model below.
                    Light stored = sun;
                    stored.direction = Vec3(sun.direction.x * 2.5f, sun.direction.y * 2.5f, 0.0f);
                    scene.SetLight(id, stored);
                    const LitFrame L = RenderLit(rig, scene, cam, 1.0f);
                    REQUIRE(L.ok);
                    const Vec3 want = Expected(surface, cam.position, world, {sun});
                    INFO((which == 0 ? "rough " : "metal ") << degrees << " degrees, want " << want.x << " got " << L.Accum(c.x, c.y, 0));
                    // The GGX peak (a smooth metal seen along its mirror direction) changes by several percent for a tenth of a
                    // pixel of position error, so that one probe gets a wider tolerance.
                    const f32 tolerance = (which == 1 && degrees == 0) ? 0.06f : 0.02f;
                    CHECK(Close(L.Accum(c.x, c.y, 0), want.x, tolerance));
                    CHECK(Close(L.Accum(c.x, c.y, 1), want.y, tolerance));
                    CHECK(Close(L.Accum(c.x, c.y, 2), want.z, tolerance));
                    if (degrees >= 95) CHECK(L.Accum(c.x, c.y, 0) == 0.0f); // facing away: nothing, with no ambient
                    if (which == 0) {
                        // Rough dielectric: the Lambert term dominates. diffuse = albedo * radiance * N.L, specular adds a little.
                        const f32 lambert = 0.5f * 1.5f * std::fmax(std::cos(theta), 0.0f);
                        CHECK(L.Accum(c.x, c.y, 0) >= lambert - 0.004f);
                        CHECK(L.Accum(c.x, c.y, 0) <= lambert * 1.3f + 0.004f);
                        if (degrees <= 85) {
                            CHECK(L.Accum(c.x, c.y, 0) < previous + 1e-6f + (previous < 0.0f ? 10.0f : 0.0f)); // dimmer as the light tilts away
                            previous = L.Accum(c.x, c.y, 0);
                        }
                    } else if (degrees == 0) {
                        // A metal has no diffuse: red/green/blue follow the albedo (F0), not the albedo times N.L alone.
                        CHECK(L.Accum(c.x, c.y, 0) > L.Accum(c.x, c.y, 2) * 3.0f);
                    }
                }
            }
            device->WaitIdle();
        }
        device->WaitIdle();
        device.Reset();
    }
}

TEST_CASE("The reconstructed world position matches the geometry, including the projection's Y flip", "[rhi][gpu][lighting]") {
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
            REQUIRE(MakeCubeMesh(2.0f, &d) == nullptr);
            GpuMesh cube;
            REQUIRE(cube.Create(device.Get(), d, "cube"));
            rig.Prepare(&plane);
            rig.Prepare(&cube);
            const MaterialId m = CreateGBufferMaterial(rig.sys, rig.templates, GBufferOptions{}, "m");
            RenderScene scene(RenderSceneDesc{});
            RenderObjectDesc od;
            od.mesh = &plane;
            od.material = m.index;
            scene.AddObject(od);
            od.mesh = &cube;
            od.transform = ModelMatrix(Vec3(-3, 1, 0), 1.0f, 0.0f);
            scene.AddObject(od);
            // An off-center camera so that neither the X nor the Z axis of the picture is symmetric.
            Camera cam = MakeLookAtCamera(Vec3(2, 10, 3), Vec3(0, 0, 0), Vec3(0, 0, -1));
            cam.aspect = 1.0f;
            cam.near_plane = 1.0f;
            cam.far_plane = 30.0f;
            const LitFrame L = RenderLit(rig, scene, cam, 1.0f, static_cast<int>(GBufferLayer::WorldPosition));
            REQUIRE(L.ok);
            auto decode = [&](const Pixel& p, int c) { return (static_cast<f32>(L.debug[(p.y * kW + p.x) * 4 + c]) / 255.0f - 0.5f) / 0.05f; };
            for (const Vec3& truth : {Vec3(3, 0, 3), Vec3(-4, 0, -3), Vec3(1, 0, -2), Vec3(-3, 2, 0), Vec3(-3.5f, 2, 0.5f)}) {
                const Pixel p = Project(cam, truth);
                INFO("truth " << truth.x << " " << truth.y << " " << truth.z << " got " << decode(p, 0) << " " << decode(p, 1) << " " << decode(p, 2));
                CHECK(std::fabs(decode(p, 0) - truth.x) < 0.3f);
                CHECK(std::fabs(decode(p, 1) - truth.y) < 0.2f);
                CHECK(std::fabs(decode(p, 2) - truth.z) < 0.3f);
            }
            device->WaitIdle();
        }
        device->WaitIdle();
        device.Reset();
    }
}

TEST_CASE("A cube lit through the deferred path is close to the Phase 4 forward Lambert result", "[rhi][gpu][lighting]") {
    FOR_EACH_GPU_BACKEND(backend) {
        gpu_test::BackendValidation validation(backend);
        rhi::DevicePtr device = MakeDevice(backend);
        {
            Rig rig;
            REQUIRE(rig.Create(device.Get()));
            MeshData d;
            REQUIRE(MakeCubeMesh(1.0f, &d) == nullptr);
            GpuMesh cube;
            REQUIRE(cube.Create(device.Get(), d, "cube"));
            rig.Prepare(&cube);
            // The Phase 4 test "Visible faces are lit by the Lambert ratio of their normals to the light": white albedo, light
            // (0.3, 0.9, 0.5) at 0.8, ambient 0.1, eye (1.8, 1.5, 3.0). The forward result is 0.1 + 0.8 * max(N.L, 0); the
            // deferred model adds a GGX lobe, small for roughness 1, so the tolerance is 0.035.
            const MaterialId m = CreateGBufferMaterial(rig.sys, rig.templates, GBufferOptions{}, "white");
            rig.sys->SetFloat(m, "roughness", 1.0f);
            RenderScene scene(RenderSceneDesc{});
            RenderObjectDesc od;
            od.mesh = &cube;
            od.material = m.index;
            scene.AddObject(od);
            Light sun;
            sun.type = LightType::Directional;
            const Vec3 raw(0.3f, 0.9f, 0.5f);
            const f32 len = std::sqrt(raw.x * raw.x + raw.y * raw.y + raw.z * raw.z);
            sun.direction = Vec3(raw.x / len, raw.y / len, raw.z / len);
            sun.intensity = 0.8f;
            scene.AddLight(sun);
            Light amb;
            amb.type = LightType::Ambient;
            amb.intensity = 0.1f;
            scene.AddLight(amb);
            Camera cam = MakeLookAtCamera(Vec3(1.8f, 1.5f, 3.0f), Vec3(0, 0, 0), Vec3(0, 1, 0));
            cam.aspect = 1.0f;
            cam.near_plane = 0.5f;
            cam.far_plane = 20.0f;
            const LitFrame L = RenderLit(rig, scene, cam, 1.0f);
            REQUIRE(L.ok);
            struct Face {
                Vec3 center;
                Vec3 normal;
            };
            for (const Face& f : {Face{Vec3(0, 0, 0.5f), Vec3(0, 0, 1)}, Face{Vec3(0.5f, 0, 0), Vec3(1, 0, 0)}, Face{Vec3(0, 0.5f, 0), Vec3(0, 1, 0)}}) {
                const f32 lambert = f.normal.x * sun.direction.x + f.normal.y * sun.direction.y + f.normal.z * sun.direction.z;
                const f32 forward = 0.1f + 0.8f * (lambert > 0.0f ? lambert : 0.0f);
                const Pixel p = Project(cam, f.center);
                INFO("face normal " << f.normal.x << " " << f.normal.y << " " << f.normal.z << " forward " << forward << " deferred " << L.Accum(p.x, p.y, 0));
                for (u32 c = 0; c < 3; ++c) CHECK(std::fabs(L.Accum(p.x, p.y, c) - forward) < 0.035f);
                CHECK(L.Accum(p.x, p.y, 0) >= forward - 0.002f); // the GGX lobe only adds
            }
            device->WaitIdle();
        }
        device->WaitIdle();
        device.Reset();
    }
}

namespace {

// A flat plane at y = 0 seen from above, lit by whatever lights the test adds.
struct PlaneRig {
    Rig rig;
    GpuMesh plane;
    RenderScene scene{RenderSceneDesc{4, 2048}};
    SurfacePoint surface;
    bool Create(rhi::Device* device) {
        if (!rig.Create(device)) return false;
        MeshData d;
        if (MakePlaneMesh(12.0f, 12.0f, 1, 1, &d) != nullptr) return false; // covers the whole view: no background
        if (!plane.Create(device, d, "plane")) return false;
        rig.Prepare(&plane);
        const MaterialId m = CreateGBufferMaterial(rig.sys, rig.templates, GBufferOptions{}, "m");
        rig.sys->SetVec4(m, "base_color", Vec4(0.6f, 0.5f, 0.4f, 1.0f));
        rig.sys->SetFloat(m, "roughness", 0.6f);
        rig.sys->SetFloat(m, "metallic", 0.0f);
        RenderObjectDesc od;
        od.mesh = &plane;
        od.material = m.index;
        scene.AddObject(od);
        surface.albedo = Vec3(0.6f, 0.5f, 0.4f);
        surface.roughness = 0.6f;
        surface.normal = Vec3(0, 1, 0);
        return true;
    }
    // World position of the plane point under a probe.
    Vec3 WorldAt(const Camera& cam, const Vec3& probe, Pixel* pixel) const {
        const Pixel p = Project(cam, probe);
        const FrameData fd = MakeFrameData(cam, kW, kH, 0, 0, 0);
        *pixel = p;
        return ReconstructWorldPosition(p.x, p.y, p.depth, kW, kH, fd.inverse_view_projection);
    }
};

Light Point(const Vec3& position, f32 radius, f32 intensity, const Vec3& color = Vec3(1, 1, 1)) {
    Light l;
    l.type = LightType::Point;
    l.position = position;
    l.radius = radius;
    l.intensity = intensity;
    l.color = color;
    return l;
}

} // namespace

TEST_CASE("A point light on a plane matches the analytic falloff, and is exactly zero outside its radius", "[rhi][gpu][lighting][point]") {
    FOR_EACH_GPU_BACKEND(backend) {
        gpu_test::BackendValidation validation(backend);
        rhi::DevicePtr device = MakeDevice(backend);
        {
            PlaneRig pr;
            REQUIRE(pr.Create(device.Get()));
            const Camera cam = TopDownCamera();
            const Light light = Point(Vec3(1.0f, 2.0f, -0.5f), 4.0f, 6.0f, Vec3(1.0f, 0.8f, 0.6f));
            pr.scene.AddLight(light);
            const LitFrame L = RenderLit(pr.rig, pr.scene, cam, 1.0f);
            INFO(L.error.CStr());
            REQUIRE(L.ok);
            CHECK(L.summary.point_count == 1);
            CHECK(L.point_draws_recorded == 1);
            int inside = 0, outside = 0;
            for (const Vec3& probe : {Vec3(1.0f, 0, -0.5f), Vec3(2.0f, 0, 0.0f), Vec3(0.0f, 0, 1.0f), Vec3(-1.5f, 0, -1.5f), Vec3(3.0f, 0, 1.5f), Vec3(-4.0f, 0, 3.0f),
                                      Vec3(4.5f, 0, -4.0f), Vec3(-5.0f, 0, -5.0f)}) {
                Pixel p;
                const Vec3 world = pr.WorldAt(cam, probe, &p);
                const Vec3 want = Expected(pr.surface, cam.position, world, {light});
                const Vec3 to = light.position - world;
                const f32 dist = std::sqrt(to.x * to.x + to.y * to.y + to.z * to.z);
                INFO("probe " << probe.x << " " << probe.z << " distance " << dist << " want " << want.x << " got " << L.Accum(p.x, p.y, 0));
                for (u32 c = 0; c < 3; ++c) CHECK(Close(L.Accum(p.x, p.y, c), c == 0 ? want.x : (c == 1 ? want.y : want.z)));
                if (dist < light.radius - 0.2f) {
                    CHECK(L.Accum(p.x, p.y, 0) > 0.001f);
                    ++inside;
                } else if (dist > light.radius + 0.2f) {
                    // Outside the radius: exactly nothing, not just small.
                    CHECK(L.Accum(p.x, p.y, 0) == 0.0f);
                    CHECK(L.Accum(p.x, p.y, 1) == 0.0f);
                    CHECK(L.Accum(p.x, p.y, 2) == 0.0f);
                    ++outside;
                }
            }
            CHECK(inside >= 4);
            CHECK(outside >= 2);
            device->WaitIdle();
        }
        device->WaitIdle();
        device.Reset();
    }
}

TEST_CASE("A camera inside a light volume is still lit", "[rhi][gpu][lighting][point]") {
    FOR_EACH_GPU_BACKEND(backend) {
        gpu_test::BackendValidation validation(backend);
        rhi::DevicePtr device = MakeDevice(backend);
        {
            PlaneRig pr;
            REQUIRE(pr.Create(device.Get()));
            const Camera cam = TopDownCamera(); // at (0, 10, 0)
            const Light light = Point(Vec3(0.5f, 10.5f, 0.0f), 16.0f, 40.0f); // the camera and the whole view are inside the sphere
            pr.scene.AddLight(light);
            const LitFrame L = RenderLit(pr.rig, pr.scene, cam, 1.0f);
            REQUIRE(L.ok);
            CHECK(L.point_draws_recorded == 1);
            for (const Vec3& probe : {Vec3(0, 0, 0), Vec3(3.0f, 0, 2.0f), Vec3(-4.0f, 0, -3.0f), Vec3(5.0f, 0, 5.0f)}) {
                Pixel p;
                const Vec3 world = pr.WorldAt(cam, probe, &p);
                const Vec3 want = Expected(pr.surface, cam.position, world, {light});
                INFO("probe " << probe.x << " " << probe.z << " want " << want.x << " got " << L.Accum(p.x, p.y, 0));
                REQUIRE(want.x > 0.01f);
                CHECK(Close(L.Accum(p.x, p.y, 0), want.x));
            }
            // Every pixel of the view is lit: the volume's back faces covered the whole target.
            int dark = 0;
            for (u32 y = 0; y < kH; ++y) {
                for (u32 x = 0; x < kW; ++x) {
                    if (L.Accum(x, y, 0) <= 0.0f) ++dark;
                }
            }
            CHECK(dark == 0);
            device->WaitIdle();
        }
        device->WaitIdle();
        device.Reset();
    }
}

TEST_CASE("A light much larger than the view distance still lights everything inside it", "[rhi][gpu][lighting][point]") {
    FOR_EACH_GPU_BACKEND(backend) {
        gpu_test::BackendValidation validation(backend);
        rhi::DevicePtr device = MakeDevice(backend);
        {
            PlaneRig pr;
            REQUIRE(pr.Create(device.Get()));
            const Camera cam = TopDownCamera(); // far plane at 30
            // The far side of the volume is 55+ units below the light, beyond the far plane: without clamping the clip depth, the
            // rasterizer would clip it away and the plane would stay dark.
            const Light light = Point(Vec3(0.0f, 10.5f, 0.0f), 50.0f, 800.0f);
            pr.scene.AddLight(light);
            const LitFrame L = RenderLit(pr.rig, pr.scene, cam, 1.0f);
            REQUIRE(L.ok);
            for (const Vec3& probe : {Vec3(0, 0, 0), Vec3(3.0f, 0, 2.0f), Vec3(-5.0f, 0, -4.0f)}) {
                Pixel p;
                const Vec3 world = pr.WorldAt(cam, probe, &p);
                const Vec3 want = Expected(pr.surface, cam.position, world, {light});
                INFO("probe " << probe.x << " " << probe.z << " want " << want.x << " got " << L.Accum(p.x, p.y, 0));
                REQUIRE(want.x > 0.1f);
                CHECK(Close(L.Accum(p.x, p.y, 0), want.x));
            }
            device->WaitIdle();
        }
        device->WaitIdle();
        device.Reset();
    }
}

TEST_CASE("Overlapping point lights sum linearly, and a culled light issues no draw", "[rhi][gpu][lighting][point]") {
    FOR_EACH_GPU_BACKEND(backend) {
        gpu_test::BackendValidation validation(backend);
        rhi::DevicePtr device = MakeDevice(backend);
        {
            PlaneRig pr;
            REQUIRE(pr.Create(device.Get()));
            const Camera cam = TopDownCamera();
            Vector<Light> lights;
            for (int i = 0; i < 12; ++i) {
                const f32 a = static_cast<f32>(i) * 0.9f;
                lights.Push(Point(Vec3(2.0f * std::cos(a), 1.0f + 0.2f * static_cast<f32>(i % 3), 2.0f * std::sin(a)), 5.0f, 1.5f,
                                       Vec3(0.5f + 0.5f * std::cos(a), 0.7f, 0.5f + 0.5f * std::sin(a))));
            }
            for (usize i = 0; i < lights.Size(); ++i) pr.scene.AddLight(lights[i]);
            // Lights the view cannot see: behind the camera, far to the side and beyond the far plane. They must cost no draw.
            pr.scene.AddLight(Point(Vec3(0, 40, 0), 3.0f, 5.0f));
            pr.scene.AddLight(Point(Vec3(200, 0, 0), 3.0f, 5.0f));
            pr.scene.AddLight(Point(Vec3(0, -100, 0), 3.0f, 5.0f));
            const LitFrame L = RenderLit(pr.rig, pr.scene, cam, 1.0f);
            REQUIRE(L.ok);
            CHECK(L.summary.point_count == 12);
            CHECK(L.summary.point_culled == 3);
            CHECK(L.point_draws_planned == 12);
            CHECK(L.point_draws_recorded == 12);
            for (const Vec3& probe : {Vec3(0, 0, 0), Vec3(2.0f, 0, 1.0f), Vec3(-2.0f, 0, -1.5f), Vec3(1.0f, 0, -2.5f), Vec3(-3.0f, 0, 2.0f)}) {
                Pixel p;
                const Vec3 world = pr.WorldAt(cam, probe, &p);
                const Vec3 want = Expected(pr.surface, cam.position, world, engine_placeholder::Span<const Light>(lights));
                INFO("probe " << probe.x << " " << probe.z << " want " << want.x << " got " << L.Accum(p.x, p.y, 0));
                REQUIRE(want.x > 0.03f);
                for (u32 c = 0; c < 3; ++c) CHECK(Close(L.Accum(p.x, p.y, c), c == 0 ? want.x : (c == 1 ? want.y : want.z)));
            }
            // All lights culled: no draw at all, and the target is black.
            RenderScene none(RenderSceneDesc{4, 8});
            none.AddLight(Point(Vec3(0, 40, 0), 3.0f, 5.0f));
            const LitFrame N = RenderLit(pr.rig, none, cam, 1.0f);
            REQUIRE(N.ok);
            CHECK(N.point_draws_planned == 0);
            CHECK(N.point_draws_recorded == 0);
            CHECK(N.Accum(32, 32, 0) == 0.0f);
            device->WaitIdle();
        }
        device->WaitIdle();
        device.Reset();
    }
}

namespace {

struct ScaleResult {
    double ms_per_frame = 0.0;
    u32 draws = 0;
    u32 culled = 0;
    u32 dropped = 0;
};

// Renders `frames` frames of the G-buffer and lighting at 1920x1080 with `count` point lights spread over the plane and returns
// the mean wall time per frame (submit to idle) and the draw counts.
ScaleResult MeasureLights(Rig& rig, const RenderScene& scene, const Camera& camera, u32 frames) {
    constexpr u32 w = 1920, h = 1080;
    ScaleResult result;
    GBufferPass gbuffer(rig.device, rig.sys, rig.frame);
    LightingPass lighting(rig.device, rig.sys, rig.frame);
    TonemapPass tonemap(rig.device);
    rhi::TextureDesc td;
    td.width = w;
    td.height = h;
    td.format = rhi::Format::RGBA8_SRGB;
    td.usage = rhi::TextureUsage::ColorAttachment | rhi::TextureUsage::CopySrc;
    const rhi::TextureHandle display = rig.device->CreateTexture(td);
    REQUIRE(display.IsValid());
    double total = 0.0;
    // One warm-up frame, then `frames` timed ones.
    for (u32 i = 0; i <= frames; ++i) {
        const auto start = std::chrono::steady_clock::now();
        const u64 f = rig.frame_index++;
        rig.device->BeginFrame();
        rig.sys->BeginFrame(f);
        rig.sys->FlushMaterials();
        rig.frame->BeginFrame(f);
        const FrameData fd = MakeFrameData(camera, w, h, 0, 0, f);
        rig.frame->WriteFrameData(fd);
        REQUIRE(gbuffer.Prepare(scene, Frustum::FromViewProjection(camera.ViewProjection())));
        const LightSummary summary = lighting.Prepare(scene, fd);
        rig.frame->Flush();
        ArenaAllocator arena(1 << 20);
        {
            RenderGraph graph(&arena, RenderGraphDesc{});
            rig.pool->BeginFrame(f);
            const GBufferTargets targets = gbuffer.AddToGraph(&graph, w, h);
            const GraphResource accum = lighting.AddToGraph(&graph, targets, w, h);
            const GraphResource out = graph.ImportTexture("display", display, rhi::ResourceState::Undefined, rhi::ResourceState::CopySrc, false);
            tonemap.AddToGraph(&graph, accum, out, rhi::Format::RGBA8_SRGB, 1.0f);
            REQUIRE(graph.Compile());
            rhi::CommandList* list = rig.device->CreateCommandList();
            list->Begin();
            REQUIRE(graph.Execute(rig.pool, list));
            list->End();
            rhi::CommandList* lists[] = {list};
            REQUIRE(rig.device->Submit(lists, 1) != 0);
        }
        rig.device->EndFrame();
        rig.device->WaitIdle();
        const double ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - start).count();
        if (i > 0) total += ms;
        result.draws = lighting.PointDrawsRecorded();
        result.culled = summary.point_culled;
        result.dropped = summary.dropped;
    }
    rig.device->DestroyTexture(display);
    result.ms_per_frame = total / static_cast<double>(frames);
    return result;
}

} // namespace

TEST_CASE("Point light scale: 1, 100 and 1000 lights at 1080p (measured, not asserted)", "[rhi][gpu][lighting][point][measure]") {
    FOR_EACH_GPU_BACKEND(backend) {
        gpu_test::BackendValidation validation(backend);
        rhi::DevicePtr device = MakeDevice(backend);
        {
            Rig rig;
            REQUIRE(rig.Create(device.Get()));
            rig.ReplaceFrame(64, 2048);
            REQUIRE(rig.frame->Valid());
            MeshData d;
            REQUIRE(MakePlaneMesh(24.0f, 24.0f, 1, 1, &d) == nullptr);
            GpuMesh plane;
            REQUIRE(plane.Create(device.Get(), d, "plane"));
            rig.Prepare(&plane);
            const MaterialId m = CreateGBufferMaterial(rig.sys, rig.templates, GBufferOptions{}, "m");
            Camera cam = MakeLookAtCamera(Vec3(0, 20, 0), Vec3(0, 0, 0), Vec3(0, 0, -1));
            cam.aspect = 1920.0f / 1080.0f;
            cam.near_plane = 1.0f;
            cam.far_plane = 40.0f;
            u32 seed = 12345;
            auto rnd = [&seed]() {
                seed = seed * 1664525u + 1013904223u;
                return static_cast<f32>(seed >> 8) / static_cast<f32>(1u << 24);
            };
            for (u32 count : {1u, 100u, 1000u}) {
                RenderScene scene(RenderSceneDesc{4, 2048});
                RenderObjectDesc od;
                od.mesh = &plane;
                od.material = m.index;
                scene.AddObject(od);
                for (u32 i = 0; i < count; ++i) {
                    scene.AddLight(Point(Vec3((rnd() - 0.5f) * 20.0f, 0.5f + rnd() * 2.0f, (rnd() - 0.5f) * 11.0f), 2.5f, 1.0f, Vec3(rnd(), rnd(), rnd())));
                }
                const ScaleResult r = MeasureLights(rig, scene, cam, 10);
                CHECK(r.draws == count); // none culled: all lights are over the visible plane
                CHECK(r.dropped == 0);
                std::printf("MEASURE point lights %4u at 1920x1080: %8.3f ms/frame (wall, submit to idle), %u light volume draws, %u culled\n", count, r.ms_per_frame, r.draws,
                            r.culled);
            }
            device->WaitIdle();
        }
        device->WaitIdle();
        device.Reset();
    }
}
