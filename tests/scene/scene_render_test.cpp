// Phase 17 Day 233: an entity-built scene renders the same pixels as the RenderScene built by hand (per GPU backend).
#include <catch2/catch_test_macros.hpp>

#include "backend_fixture.h"
#include "cstr/mem_ops.h"
#include "demo_frame.h"
#include "ecs/schedule.h"
#include "ecs/transform.h"
#include "os/console.h"
#include "os/os.h"
#include "containers/array.h"
#include "renderer/render_scene.h"
#include "renderer/texture.h"
#include "scene/render_sync.h"

using namespace engine_placeholder;
using scene::LightSource;
using scene::MakeRenderSyncSystem;
using scene::MeshRenderer;
using scene::SceneLink;
using scene::SceneSyncReport;

namespace
{

constexpr u32 kSize = 96;

struct Placement
{
    f32 x, y, z;
    u32 material;  // index into the two materials
    bool sphere;
};

const Placement kPlacements[] = {
    {-2.0f, 0.0f, 0.0f, 0, false}, {0.0f, 0.0f, -1.0f, 1, true},  {2.0f, 0.5f, 0.0f, 0, true},
    {0.0f, -1.5f, 0.5f, 1, false}, {-1.0f, 1.2f, -0.5f, 1, true},
};

bool Render(renderer_demo::DemoFrame& frame, rhi::Device* device, const renderer::RenderScene& scene,
            const renderer::Camera& camera, Vector<u8>* pixels)
{
    rhi::TextureDesc td;
    td.width = kSize;
    td.height = kSize;
    td.format = rhi::Format::RGBA8_SRGB;
    td.usage = rhi::TextureUsage::ColorAttachment | rhi::TextureUsage::CopySrc;
    const rhi::TextureHandle target = device->CreateTexture(td);
    if (!target.IsValid())
    {
        return false;
    }
    renderer::FrameTarget ft;
    ft.texture = target;
    ft.width = ft.height = kSize;
    ft.format = rhi::Format::RGBA8_SRGB;
    ft.final_state = rhi::ResourceState::CopySrc;
    renderer::FrameStatus status = renderer::FrameStatus::Failed;
    for (int i = 0; i < 3; ++i)
    {
        status = frame.Render(&scene, &camera, &ft);
    }
    device->WaitIdle();
    pixels->Resize(kSize * kSize * 4);
    const bool ok = status == renderer::FrameStatus::Ok &&
                    device->ReadbackTexture(target, 0, 0, pixels->Data(), pixels->Size(), rhi::ResourceState::CopySrc);
    device->DestroyTexture(target);
    return ok;
}

}  // namespace

TEST_CASE("An entity-built scene renders the same pixels as the hand-built RenderScene", "[scene][render][gpu]")
{
    FOR_EACH_GPU_BACKEND(backend)
    {
        gpu_test::BackendValidation validation(backend);
        rhi::DeviceCreateInfo info;
        info.backend = backend;
        info.validation = rhi::ValidationSetting::On;
        info.pipeline_cache = false;
        rhi::DevicePtr device = rhi::CreateDevice(info);
        if (device.Get() == nullptr)
        {
            const char* required = os::GetEnvironment("ENGINE_RHI_REQUIRE_GPU");
            if (required != nullptr && required[0] == '1')
            {
                FAIL("ENGINE_RHI_REQUIRE_GPU=1 but no " << gpu_test::BackendTag(backend) << " device could be created");
            }
            SKIP("no " << gpu_test::BackendTag(backend) << " device on this machine");
        }
        {
            renderer_demo::FrameSetup setup;
            setup.device = device.Get();
            setup.max_objects = 64;
            setup.max_lights = 8;
            renderer_demo::DemoFrame frame;
            REQUIRE(frame.Create(setup));

            renderer::MaterialId materials[2];
            for (u32 m = 0; m < 2; ++m)
            {
                materials[m] = renderer::CreateGBufferMaterial(frame.Materials(), frame.Templates(),
                                                               renderer::GBufferOptions{}, m == 0 ? "red" : "blue");
                REQUIRE(frame.Materials()->SetVec4(
                    materials[m], "base_color", m == 0 ? Vec4(0.8f, 0.1f, 0.1f, 1.0f) : Vec4(0.1f, 0.2f, 0.8f, 1.0f)));
            }
            auto cube = MakeShared<renderer::GpuMesh>();
            auto sphere = MakeShared<renderer::GpuMesh>();
            renderer::MeshData data;
            REQUIRE(renderer::MakeCubeMesh(1.0f, &data) == nullptr);
            REQUIRE(cube->Create(device.Get(), data, "cube"));
            renderer::MeshData round;
            REQUIRE(renderer::MakeSphereMesh(0.7f, 16, 8, &round) == nullptr);
            REQUIRE(sphere->Create(device.Get(), round, "sphere"));

            // The barriers that move the buffers out of their upload state, in one submission (as the demo scene does).
            {
                device->BeginFrame();
                rhi::CommandList* list = device->CreateCommandList();
                REQUIRE(list != nullptr);
                list->Begin();
                cube->RecordPrepare(list);
                sphere->RecordPrepare(list);
                list->End();
                rhi::CommandList* lists[] = {list};
                REQUIRE(device->Submit(lists, 1) != 0);
                device->EndFrame();
                device->WaitIdle();
                cube->MarkReady();
                sphere->MarkReady();
            }

            renderer::Camera camera;
            camera.SetLookAt(Vec3(0, 1.5f, 6), Vec3(0, 0, 0), Vec3(0, 1, 0));
            camera.aspect = 1.0f;
            camera.near_plane = 0.5f;
            camera.far_plane = 50.0f;

            // By hand.
            renderer::RenderSceneDesc desc;
            desc.max_objects = 64;
            desc.max_lights = 8;
            renderer::RenderScene by_hand(desc);
            for (const Placement& p : kPlacements)
            {
                renderer::RenderObjectDesc od;
                od.mesh = p.sphere ? sphere.Get() : cube.Get();
                od.material = materials[p.material].index;
                od.transform = Mat4::FromBasis(Vec3(1, 0, 0), Vec3(0, 1, 0), Vec3(0, 0, 1), Vec3(p.x, p.y, p.z));
                REQUIRE(by_hand.AddObject(od).IsValid());
            }
            renderer::Light sun;
            sun.type = renderer::LightType::Directional;
            sun.direction = Vec3(0.3f, 0.8f, 0.5f).Normalized();
            sun.intensity = 2.0f;
            REQUIRE(by_hand.AddLight(sun).IsValid());
            renderer::Light ambient;
            ambient.type = renderer::LightType::Ambient;
            ambient.intensity = 0.2f;
            REQUIRE(by_hand.AddLight(ambient).IsValid());

            // From entities.
            renderer::RenderScene mirrored(desc);
            ecs::World world;
            world.SetResource(ecs::TransformReport{});
            world.SetResource(SceneLink{&mirrored});
            world.SetResource(SceneSyncReport{});
            ecs::Schedule schedule;
            schedule.Add(ecs::MakeTransformSystem("transforms"));
            schedule.Add(MakeRenderSyncSystem("render_sync").InStage(ecs::kRender));
            REQUIRE(schedule.Build().ok);
            for (const Placement& p : kPlacements)
            {
                ecs::LocalTransform local;
                local.position = Vec3(p.x, p.y, p.z);
                MeshRenderer mr;
                mr.mesh = p.sphere ? sphere : cube;
                mr.material = materials[p.material].index;
                world.Create(local, ecs::WorldTransform{}, mr);
            }
            LightSource sun_source;
            sun_source.type = renderer::LightType::Directional;
            sun_source.direction = sun.direction;
            sun_source.intensity = 2.0f;
            world.Create(ecs::LocalTransform{}, ecs::WorldTransform{}, sun_source);
            LightSource ambient_source;
            ambient_source.type = renderer::LightType::Ambient;
            ambient_source.intensity = 0.2f;
            world.Create(ecs::LocalTransform{}, ecs::WorldTransform{}, ambient_source);
            schedule.Run(world, 0.016f);
            schedule.Run(world, 0.016f);
            REQUIRE(mirrored.ObjectCount() == by_hand.ObjectCount());
            REQUIRE(mirrored.LightCount() == by_hand.LightCount());

            Vector<u8> expected;
            Vector<u8> actual;
            REQUIRE(Render(frame, device.Get(), by_hand, camera, &expected));
            REQUIRE(Render(frame, device.Get(), mirrored, camera, &actual));
            REQUIRE(expected.Size() == actual.Size());
            CHECK(MemCompare(expected.Data(), actual.Data(), expected.Size()) == 0);
            // The picture has something in it: the objects are not just the clear color.
            bool varied = false;
            for (usize i = 4; i < expected.Size() && !varied; i += 4)
            {
                varied = MemCompare(&expected[i], &expected[0], 4) != 0;
            }
            if (!varied)
            {
                os::PrintError("uniform image %u %u %u %u; visible %u drawn %u rejected %u ok %d err %s\n", expected[0],
                               expected[1], expected[2], expected[3], frame.GBufferStatistics().visible,
                               frame.GBufferStatistics().drawn_objects, frame.GBufferStatistics().rejected,
                               frame.LastGBufferOk() ? 1 : 0, frame.LastError());
            }
            CHECK(varied);
            device->WaitIdle();
            frame.Destroy();
        }
        device.Reset();
    }
}

// Unlit materials (maintenance, 2026-11-19; glTF's KHR_materials_unlit): a material with renderer::kMaterialFlagUnlit
// in its `flags` shows its base colour whatever the lights are, while a lit one beside it follows them.
TEST_CASE("An unlit material keeps its colour under other lights; a lit one does not", "[scene][render][gpu]")
{
    FOR_EACH_GPU_BACKEND(backend)
    {
        gpu_test::BackendValidation validation(backend);
        rhi::DeviceCreateInfo info;
        info.backend = backend;
        info.validation = rhi::ValidationSetting::On;
        info.pipeline_cache = false;
        rhi::DevicePtr device = rhi::CreateDevice(info);
        if (device.Get() == nullptr)
        {
            const char* required = os::GetEnvironment("ENGINE_RHI_REQUIRE_GPU");
            if (required != nullptr && required[0] == '1')
            {
                FAIL("ENGINE_RHI_REQUIRE_GPU=1 but no " << gpu_test::BackendTag(backend) << " device could be created");
            }
            SKIP("no " << gpu_test::BackendTag(backend) << " device on this machine");
        }
        {
            renderer_demo::FrameSetup setup;
            setup.device = device.Get();
            setup.max_objects = 8;
            setup.max_lights = 8;
            renderer_demo::DemoFrame frame;
            REQUIRE(frame.Create(setup));
            const renderer::MaterialId lit = renderer::CreateGBufferMaterial(frame.Materials(), frame.Templates(),
                                                                             renderer::GBufferOptions{}, "lit");
            const renderer::MaterialId unlit = renderer::CreateGBufferMaterial(frame.Materials(), frame.Templates(),
                                                                               renderer::GBufferOptions{}, "unlit");
            REQUIRE(frame.Materials()->SetVec4(lit, "base_color", Vec4(0.8f, 0.2f, 0.2f, 1.0f)));
            REQUIRE(frame.Materials()->SetVec4(unlit, "base_color", Vec4(0.8f, 0.2f, 0.2f, 1.0f)));
            REQUIRE(frame.Materials()->SetUInt(unlit, "flags", renderer::kMaterialFlagUnlit));

            auto cube = MakeShared<renderer::GpuMesh>();
            renderer::MeshData data;
            REQUIRE(renderer::MakeCubeMesh(1.0f, &data) == nullptr);
            REQUIRE(cube->Create(device.Get(), data, "cube"));
            {
                device->BeginFrame();
                rhi::CommandList* list = device->CreateCommandList();
                REQUIRE(list != nullptr);
                list->Begin();
                cube->RecordPrepare(list);
                list->End();
                rhi::CommandList* lists[] = {list};
                REQUIRE(device->Submit(lists, 1) != 0);
                device->EndFrame();
                device->WaitIdle();
                cube->MarkReady();
            }

            renderer::Camera camera;
            camera.SetLookAt(Vec3(0, 1.0f, 4.0f), Vec3(0, 0, 0), Vec3(0, 1, 0));
            camera.aspect = 1.0f;
            camera.near_plane = 0.5f;
            camera.far_plane = 50.0f;
            renderer::RenderSceneDesc desc;
            desc.max_objects = 8;
            desc.max_lights = 8;
            renderer::RenderScene scene(desc);
            renderer::RenderObjectDesc od;
            od.mesh = cube.Get();
            od.material = unlit.index;
            od.transform = Mat4::FromBasis(Vec3(1, 0, 0), Vec3(0, 1, 0), Vec3(0, 0, 1), Vec3(-0.9f, 0, 0));
            scene.AddObject(od);
            od.material = lit.index;
            od.transform = Mat4::FromBasis(Vec3(1, 0, 0), Vec3(0, 1, 0), Vec3(0, 0, 1), Vec3(0.9f, 0, 0));
            scene.AddObject(od);
            renderer::Light sun;
            sun.type = renderer::LightType::Directional;
            sun.direction = Vec3(0.3f, 0.8f, 0.6f);
            renderer::Light point;
            point.type = renderer::LightType::Point;
            point.position = Vec3(0.0f, 1.0f, 2.0f);
            point.radius = 6.0f;
            renderer::Light ambient;
            ambient.type = renderer::LightType::Ambient;
            ambient.intensity = 0.3f;
            const renderer::LightId sun_id = scene.AddLight(sun);
            const renderer::LightId point_id = scene.AddLight(point);
            const renderer::LightId ambient_id = scene.AddLight(ambient);

            const auto render_with = [&](const Vec3& color, Vector<u8>* pixels)
            {
                sun.color = color;
                point.color = color;
                ambient.color = color;
                scene.SetLight(sun_id, sun);
                scene.SetLight(point_id, point);
                scene.SetLight(ambient_id, ambient);
                return Render(frame, device.Get(), scene, camera, pixels);
            };
            Vector<u8> blue_lit;
            Vector<u8> green_lit;
            REQUIRE(render_with(Vec3(0.2f, 0.3f, 1.0f), &blue_lit));
            REQUIRE(render_with(Vec3(0.2f, 1.0f, 0.3f), &green_lit));
            // The middle row: the unlit cube on the left third, the lit one on the right third.
            const u32 row = kSize / 2;
            const auto at = [&](const Vector<u8>& px, u32 x, u32 c)
            { return static_cast<int>(px[(row * kSize + x) * 4 + c]); };
            const u32 left = kSize / 4;
            const u32 right = kSize - kSize / 4;
            INFO("unlit " << at(blue_lit, left, 0) << "," << at(blue_lit, left, 1) << "," << at(blue_lit, left, 2)
                          << " / " << at(green_lit, left, 0) << "," << at(green_lit, left, 1) << ","
                          << at(green_lit, left, 2));
            INFO("lit " << at(blue_lit, right, 0) << "," << at(blue_lit, right, 1) << "," << at(blue_lit, right, 2)
                        << " / " << at(green_lit, right, 0) << "," << at(green_lit, right, 1) << ","
                        << at(green_lit, right, 2));
            for (u32 c = 0; c < 3; ++c)
            {
                CHECK(at(blue_lit, left, c) == at(green_lit, left, c));  // the lights do not reach it
            }
            CHECK(at(blue_lit, left, 0) > at(blue_lit, left, 1));  // and it shows its own red base colour
            CHECK(at(blue_lit, left, 0) > at(blue_lit, left, 2));
            CHECK(at(blue_lit, right, 2) > at(green_lit, right, 2));  // the lit cube follows the light's colour
            CHECK(at(green_lit, right, 1) > at(blue_lit, right, 1));
            device->WaitIdle();
        }
        device->WaitIdle();
        device.Reset();
    }
}

namespace
{

// The largest difference of one channel between two pictures of the same size.
int LargestDifference(const Vector<u8>& a, const Vector<u8>& b)
{
    int largest = a.Size() == b.Size() ? 0 : 255;
    for (usize i = 0; i < a.Size() && i < b.Size(); ++i)
    {
        const int d = static_cast<int>(a[i]) - static_cast<int>(b[i]);
        const int magnitude = d < 0 ? -d : d;
        largest = magnitude > largest ? magnitude : largest;
    }
    return largest;
}

// A 1x1 linear texture of one texel, for a map slot.
renderer::GpuTexturePtr MakeTexel(rhi::Device* device, u8 r, u8 g, u8 b, const char* name)
{
    renderer::GpuTexturePtr texture = MakeShared<renderer::GpuTexture>();
    renderer::TextureImage image;
    image.desc.width = image.desc.height = 1;
    image.desc.format = rhi::Format::RGBA8_UNorm;
    image.desc.usage = rhi::TextureUsage::Sampled;
    image.pixels.Resize(4);
    image.pixels[0] = r;
    image.pixels[1] = g;
    image.pixels[2] = b;
    image.pixels[3] = 255;
    if (!texture->Create(device, static_cast<renderer::TextureImage&&>(image), nullptr, name))
    {
        return renderer::GpuTexturePtr{};
    }
    return texture;
}

}  // namespace

// glTF coverage Day 1: the metallic-roughness and occlusion maps give exactly what their products as scalars give
// (roughness times green, metallic times blue, occlusion times 1 + strength * (red - 1)), under a directional, a point
// and an ambient light, and a white map changes nothing.
TEST_CASE("A metallic-roughness or occlusion map draws as its products set as scalars do", "[scene][render][gpu]")
{
    FOR_EACH_GPU_BACKEND(backend)
    {
        gpu_test::BackendValidation validation(backend);
        rhi::DeviceCreateInfo info;
        info.backend = backend;
        info.validation = rhi::ValidationSetting::On;
        info.pipeline_cache = false;
        rhi::DevicePtr device = rhi::CreateDevice(info);
        if (device.Get() == nullptr)
        {
            const char* required = os::GetEnvironment("ENGINE_RHI_REQUIRE_GPU");
            if (required != nullptr && required[0] == '1')
            {
                FAIL("ENGINE_RHI_REQUIRE_GPU=1 but no " << gpu_test::BackendTag(backend) << " device could be created");
            }
            SKIP("no " << gpu_test::BackendTag(backend) << " device on this machine");
        }
        {
            renderer_demo::FrameSetup setup;
            setup.device = device.Get();
            setup.max_objects = 8;
            setup.max_lights = 8;
            renderer_demo::DemoFrame frame;
            REQUIRE(frame.Create(setup));
            renderer::MaterialSystem* materials = frame.Materials();
            const auto make = [&](const char* name, f32 roughness, f32 metallic, f32 occlusion)
            {
                const renderer::MaterialId id = renderer::CreateGBufferMaterial(materials, frame.Templates(),
                                                                                renderer::GBufferOptions{}, name);
                CHECK(materials->SetVec4(id, "base_color", Vec4(0.7f, 0.5f, 0.3f, 1.0f)));
                CHECK(materials->SetFloat(id, "roughness", roughness));
                CHECK(materials->SetFloat(id, "metallic", metallic));
                CHECK(materials->SetFloat(id, "occlusion", occlusion));
                return id;
            };
            // The texels are exact in 8 bits, so each scalar material is given the very value the map holds.
            const f32 g = 153.0f / 255.0f;
            const f32 b = 128.0f / 255.0f;
            const f32 occ = 64.0f / 255.0f;
            const renderer::MaterialId mr_scalars = make("mr scalars", g, b, 1.0f);
            const renderer::MaterialId mr_map = make("mr map", 1.0f, 1.0f, 1.0f);
            const renderer::MaterialId mr_unmapped = make("mr without its map", 1.0f, 1.0f, 1.0f);
            const renderer::MaterialId occ_scalar = make("occlusion scalar", 0.5f, 0.0f, 1.0f + 0.5f * (occ - 1.0f));
            const renderer::MaterialId occ_map = make("occlusion map", 0.5f, 0.0f, 1.0f);
            const renderer::MaterialId white_maps = make("white maps", 0.5f, 0.0f, 1.0f);
            const renderer::MaterialId no_maps = make("no maps", 0.5f, 0.0f, 1.0f);
            const renderer::GpuTexturePtr mr_texel = MakeTexel(device.Get(), 0, 153, 128, "mr texel");
            const renderer::GpuTexturePtr occ_texel = MakeTexel(device.Get(), 64, 0, 0, "occlusion texel");
            const renderer::GpuTexturePtr white = MakeTexel(device.Get(), 255, 255, 255, "white texel");
            REQUIRE(mr_texel.Get() != nullptr);
            REQUIRE(occ_texel.Get() != nullptr);
            REQUIRE(white.Get() != nullptr);
            REQUIRE(materials->SetTexture(mr_map, "metallic_roughness_tex", mr_texel));
            REQUIRE(materials->SetTexture(occ_map, "occlusion_tex", occ_texel));
            REQUIRE(materials->SetFloat(occ_map, "occlusion_strength", 0.5f));
            REQUIRE(materials->SetTexture(white_maps, "metallic_roughness_tex", white));
            REQUIRE(materials->SetTexture(white_maps, "occlusion_tex", white));

            auto cube = MakeShared<renderer::GpuMesh>();
            renderer::MeshData data;
            REQUIRE(renderer::MakeCubeMesh(1.0f, &data) == nullptr);
            REQUIRE(cube->Create(device.Get(), data, "cube"));
            {
                device->BeginFrame();
                rhi::CommandList* list = device->CreateCommandList();
                REQUIRE(list != nullptr);
                list->Begin();
                cube->RecordPrepare(list);
                list->End();
                rhi::CommandList* lists[] = {list};
                REQUIRE(device->Submit(lists, 1) != 0);
                device->EndFrame();
                device->WaitIdle();
                cube->MarkReady();
            }
            renderer::Camera camera;
            camera.SetLookAt(Vec3(0, 1.5f, 2.5f), Vec3(0, 0, 0), Vec3(0, 1, 0));
            camera.aspect = 1.0f;
            camera.near_plane = 0.5f;
            camera.far_plane = 50.0f;
            const auto draw = [&](renderer::MaterialId material, Vector<u8>* pixels)
            {
                renderer::RenderSceneDesc desc;
                desc.max_objects = 8;
                desc.max_lights = 8;
                renderer::RenderScene scene(desc);
                renderer::RenderObjectDesc od;
                od.mesh = cube.Get();
                od.material = material.index;
                od.transform = Mat4::FromBasis(Vec3(1, 0, 0), Vec3(0, 1, 0), Vec3(0, 0, 1), Vec3(0, 0, 0));
                scene.AddObject(od);
                renderer::Light sun;
                sun.type = renderer::LightType::Directional;
                sun.direction = Vec3(0.3f, 0.8f, 0.6f);
                renderer::Light point;
                point.type = renderer::LightType::Point;
                point.position = Vec3(0.8f, 1.0f, 1.2f);
                point.radius = 6.0f;
                renderer::Light ambient;
                ambient.type = renderer::LightType::Ambient;
                ambient.intensity = 0.6f;
                scene.AddLight(sun);
                scene.AddLight(point);
                scene.AddLight(ambient);
                return Render(frame, device.Get(), scene, camera, pixels);
            };
            Vector<u8> scalars;
            Vector<u8> mapped;
            Vector<u8> plain;
            REQUIRE(draw(mr_scalars, &scalars));
            REQUIRE(draw(mr_map, &mapped));
            REQUIRE(draw(mr_unmapped, &plain));
            CHECK(LargestDifference(scalars, mapped) <= 1);  // the map's products
            CHECK(LargestDifference(plain, mapped) > 8);     // and the map did change the picture
            REQUIRE(draw(occ_scalar, &scalars));
            REQUIRE(draw(occ_map, &mapped));
            REQUIRE(draw(no_maps, &plain));
            CHECK(LargestDifference(scalars, mapped) <= 1);
            CHECK(LargestDifference(plain, mapped) > 8);
            REQUIRE(draw(white_maps, &mapped));
            CHECK(LargestDifference(plain, mapped) == 0);  // a white map is the default: nothing changes
            // glTF coverage Day 2 (a Day 1 bug): a material that never sets occlusion_strength applies its occlusion
            // map fully, as glTF's default strength 1 says (the defaults left it 0, which ignored the map).
            const renderer::MaterialId occ_default = make("occlusion map, strength unset", 0.5f, 0.0f, 1.0f);
            const renderer::MaterialId occ_full = make("occlusion scalar, full strength", 0.5f, 0.0f, occ);
            REQUIRE(materials->SetTexture(occ_default, "occlusion_tex", occ_texel));
            REQUIRE(draw(occ_full, &scalars));
            REQUIRE(draw(occ_default, &mapped));
            CHECK(LargestDifference(scalars, mapped) <= 1);
            device->WaitIdle();
            const Array<renderer::MaterialId, 4> mapped_materials = {mr_map, occ_map, white_maps, occ_default};
            for (const renderer::MaterialId& id : mapped_materials)
            {
                materials->SetTexture(id, "metallic_roughness_tex", renderer::GpuTextureConstPtr{});
                materials->SetTexture(id, "occlusion_tex", renderer::GpuTextureConstPtr{});
            }
        }
        device->WaitIdle();
        device.Reset();
    }
}

// glTF coverage Day 2: emission is a colour of its own, in the G-buffer's emissive target, added once after the lights:
// a white emissive map changes nothing and a black one removes the glow, a strength above 1 is not clamped, the glow
// does not depend on the base colour, and an unlit material shows its base colour whatever its emissive colour is.
TEST_CASE("An emissive colour glows on its own, times its map and strength, and not on an unlit material",
          "[scene][render][gpu]")
{
    FOR_EACH_GPU_BACKEND(backend)
    {
        gpu_test::BackendValidation validation(backend);
        rhi::DeviceCreateInfo info;
        info.backend = backend;
        info.validation = rhi::ValidationSetting::On;
        info.pipeline_cache = false;
        rhi::DevicePtr device = rhi::CreateDevice(info);
        if (device.Get() == nullptr)
        {
            const char* required = os::GetEnvironment("ENGINE_RHI_REQUIRE_GPU");
            if (required != nullptr && required[0] == '1')
            {
                FAIL("ENGINE_RHI_REQUIRE_GPU=1 but no " << gpu_test::BackendTag(backend) << " device could be created");
            }
            SKIP("no " << gpu_test::BackendTag(backend) << " device on this machine");
        }
        {
            renderer_demo::FrameSetup setup;
            setup.device = device.Get();
            setup.max_objects = 8;
            setup.max_lights = 8;
            renderer_demo::DemoFrame frame;
            REQUIRE(frame.Create(setup));
            renderer::MaterialSystem* materials = frame.Materials();
            const auto make = [&](const char* name, const Vec4& base, const Vec4& emissive)
            {
                const renderer::MaterialId id = renderer::CreateGBufferMaterial(materials, frame.Templates(),
                                                                                renderer::GBufferOptions{}, name);
                CHECK(materials->SetVec4(id, "base_color", base));
                CHECK(materials->SetVec4(id, "emissive_color", emissive));
                return id;
            };
            const Vec4 grey(0.2f, 0.2f, 0.2f, 1.0f);
            const Vec4 glow(0.8f, 0.4f, 0.2f, 1.0f);
            const renderer::MaterialId dark = make("no glow", grey, Vec4(0, 0, 0, 1));
            const renderer::MaterialId lit = make("glow", grey, glow);
            const renderer::MaterialId white_map = make("glow, white map", grey, glow);
            const renderer::MaterialId black_map = make("glow, black map", grey, glow);
            const renderer::MaterialId bright = make("glow, strength 4", grey, Vec4(0.8f, 0.4f, 0.2f, 4.0f));
            const renderer::MaterialId other_base = make("glow on another base", Vec4(0.6f, 0.1f, 0.5f, 1), glow);
            const renderer::MaterialId unlit = make("unlit", grey, Vec4(0, 0, 0, 1));
            const renderer::MaterialId unlit_glow = make("unlit with a glow", grey, glow);
            REQUIRE(materials->SetUInt(unlit, "flags", renderer::kMaterialFlagUnlit));
            REQUIRE(materials->SetUInt(unlit_glow, "flags", renderer::kMaterialFlagUnlit));
            const renderer::GpuTexturePtr white = MakeTexel(device.Get(), 255, 255, 255, "white texel");
            const renderer::GpuTexturePtr black = MakeTexel(device.Get(), 0, 0, 0, "black texel");
            REQUIRE(white.Get() != nullptr);
            REQUIRE(black.Get() != nullptr);
            REQUIRE(materials->SetTexture(white_map, "emissive_tex", white));
            REQUIRE(materials->SetTexture(black_map, "emissive_tex", black));

            auto cube = MakeShared<renderer::GpuMesh>();
            renderer::MeshData data;
            REQUIRE(renderer::MakeCubeMesh(1.0f, &data) == nullptr);
            REQUIRE(cube->Create(device.Get(), data, "cube"));
            {
                device->BeginFrame();
                rhi::CommandList* list = device->CreateCommandList();
                REQUIRE(list != nullptr);
                list->Begin();
                cube->RecordPrepare(list);
                list->End();
                rhi::CommandList* lists[] = {list};
                REQUIRE(device->Submit(lists, 1) != 0);
                device->EndFrame();
                device->WaitIdle();
                cube->MarkReady();
            }
            renderer::Camera camera;
            camera.SetLookAt(Vec3(0, 1.5f, 2.5f), Vec3(0, 0, 0), Vec3(0, 1, 0));
            camera.aspect = 1.0f;
            camera.near_plane = 0.5f;
            camera.far_plane = 50.0f;
            // No light at all: what shows is the glow alone (and the unlit base colour).
            const auto draw = [&](renderer::MaterialId material, Vector<u8>* pixels)
            {
                renderer::RenderSceneDesc desc;
                desc.max_objects = 8;
                desc.max_lights = 8;
                renderer::RenderScene scene(desc);
                renderer::RenderObjectDesc od;
                od.mesh = cube.Get();
                od.material = material.index;
                od.transform = Mat4::FromBasis(Vec3(1, 0, 0), Vec3(0, 1, 0), Vec3(0, 0, 1), Vec3(0, 0, 0));
                scene.AddObject(od);
                return Render(frame, device.Get(), scene, camera, pixels);
            };
            Vector<u8> none;
            Vector<u8> glowing;
            Vector<u8> other;
            REQUIRE(draw(dark, &none));
            REQUIRE(draw(lit, &glowing));
            const u32 centre = (kSize / 2 * kSize + kSize / 2) * 4;
            INFO("dark " << int(none[centre]) << " glowing " << int(glowing[centre]) << "," << int(glowing[centre + 1])
                         << "," << int(glowing[centre + 2]));
            CHECK(int(none[centre]) == 0);                           // no light, no glow: black
            CHECK(int(glowing[centre]) > int(glowing[centre + 1]));  // the glow's own colour, red over green over blue
            CHECK(int(glowing[centre + 1]) > int(glowing[centre + 2]));
            REQUIRE(draw(white_map, &other));
            CHECK(LargestDifference(glowing, other) == 0);  // a white map is the default
            REQUIRE(draw(black_map, &other));
            CHECK(LargestDifference(none, other) == 0);  // a black map takes the glow away
            REQUIRE(draw(other_base, &other));
            CHECK(LargestDifference(glowing, other) == 0);  // the glow does not depend on the base colour
            REQUIRE(draw(bright, &other));
            CHECK(int(other[centre]) > int(glowing[centre]));  // a strength of 4 is not clamped to 1
            Vector<u8> flat;
            REQUIRE(draw(unlit, &flat));
            REQUIRE(draw(unlit_glow, &other));
            CHECK(LargestDifference(flat, other) == 0);  // unlit: its base colour, no glow
            device->WaitIdle();
            materials->SetTexture(white_map, "emissive_tex", renderer::GpuTextureConstPtr{});
            materials->SetTexture(black_map, "emissive_tex", renderer::GpuTextureConstPtr{});
        }
        device->WaitIdle();
        device.Reset();
    }
}

namespace
{

// Moves a freshly created mesh's buffers out of their upload state, in one submission (as the demo scene does).
void PrepareMesh(rhi::Device* device, renderer::GpuMesh* mesh)
{
    device->BeginFrame();
    rhi::CommandList* list = device->CreateCommandList();
    REQUIRE(list != nullptr);
    list->Begin();
    mesh->RecordPrepare(list);
    list->End();
    rhi::CommandList* lists[] = {list};
    REQUIRE(device->Submit(lists, 1) != 0);
    device->EndFrame();
    device->WaitIdle();
    mesh->MarkReady();
}

// The number of pixels whose colour differs by more than `tolerance` in some channel.
u32 PixelsThatDiffer(const Vector<u8>& a, const Vector<u8>& b, int tolerance)
{
    u32 count = 0;
    for (usize p = 0; p + 3 < a.Size() && p + 3 < b.Size(); p += 4)
    {
        for (usize c = 0; c < 3; ++c)
        {
            const int d = static_cast<int>(a[p + c]) - static_cast<int>(b[p + c]);
            if (d > tolerance || -d > tolerance)
            {
                ++count;
                break;
            }
        }
    }
    return count;
}

}  // namespace

// glTF coverage Day 14 (the Mac): morph targets drawn on the GPU (the morph pre-pass of Day 7, shaders/morph.comp). A
// cube with eight targets (the most applied at once), each moving every vertex 0.075 along x: at weight 0 it draws
// exactly as the plain cube, with every weight 1 as the plain cube moved by 0.6 (up to the rasterization of the edges),
// and with every weight 0.5 halfway. Eight targets fill the push block to its last word (it was 160 bytes, past the 128
// bound, until Day 14).
TEST_CASE("A morph target moves the mesh on the GPU by its weight", "[scene][render][gpu]")
{
    FOR_EACH_GPU_BACKEND(backend)
    {
        gpu_test::BackendValidation validation(backend);
        rhi::DevicePtr device = gpu_test::CreateTestDevice(backend);  // with Vulkan's synchronization validation
        {
            renderer_demo::FrameSetup setup;
            setup.device = device.Get();
            setup.max_objects = 8;
            setup.max_lights = 8;
            renderer_demo::DemoFrame frame;
            REQUIRE(frame.Create(setup));
            renderer::MaterialSystem* materials = frame.Materials();
            // A glow and no light: the picture is the cube's silhouette in one colour.
            const renderer::MaterialId glow = renderer::CreateGBufferMaterial(materials, frame.Templates(),
                                                                              renderer::GBufferOptions{}, "glow");
            REQUIRE(materials->SetVec4(glow, "base_color", Vec4(0.2f, 0.2f, 0.2f, 1.0f)));
            REQUIRE(materials->SetVec4(glow, "emissive_color", Vec4(0.8f, 0.4f, 0.2f, 1.0f)));

            renderer::MeshData data;
            REQUIRE(renderer::MakeCubeMesh(1.0f, &data) == nullptr);
            auto plain = MakeShared<renderer::GpuMesh>();
            REQUIRE(plain->Create(device.Get(), data, "cube"));
            PrepareMesh(device.Get(), plain.Get());

            // The same cube with eight morph targets: each a delta of (0.075, 0, 0) on every vertex, after the cube's
            // bytes.
            renderer::MeshGeometry geometry = data.CopyGeometry();
            REQUIRE(geometry.bytes.Get() != nullptr);
            const ByteSpan region = geometry.bytes->Bytes();
            const usize delta_offset = (region.Size() + 15) / 16 * 16;
            Vector<byte> bytes;
            constexpr u32 kTargets = renderer::kMaxActiveMorphTargets;
            const u32 deltas = geometry.vertex_count * kTargets;
            bytes.Resize(delta_offset + static_cast<usize>(deltas) * renderer::kMorphDeltaBytes);
            MemSet(bytes.Data(), 0, bytes.Size());
            MemCopy(bytes.Data(), region.Data(), region.Size());
            for (u32 d = 0; d < deltas; ++d)
            {
                byte* delta = bytes.Data() + delta_offset + static_cast<usize>(d) * renderer::kMorphDeltaBytes;
                const u32 v = d % geometry.vertex_count;  // each target's run is sorted by vertex
                const f32 dx = 0.075f;
                MemCopy(delta, &v, 4);
                MemCopy(delta + 4, &dx, 4);  // position x; the rest of the position, the normal and the tangent stay 0
            }
            geometry.bytes = MakeShared<OwnedBytes>(static_cast<Vector<byte>&&>(bytes));
            geometry.morph_delta_offset = delta_offset;
            geometry.morph_delta_count = deltas;
            geometry.morph_target_count = kTargets;
            for (u32 t = 0; t < kTargets; ++t)
            {
                geometry.morph_targets[t].first_delta = t * geometry.vertex_count;
                geometry.morph_targets[t].delta_count = geometry.vertex_count;
            }
            geometry.bounds.box.max.x += 0.6f;  // the box and the sphere cover weights up to 1
            geometry.bounds.sphere.center.x += 0.3f;
            geometry.bounds.sphere.radius += 0.3f;
            REQUIRE(geometry.Validate() == nullptr);
            auto morphed = MakeShared<renderer::GpuMesh>();
            REQUIRE(morphed->CreateFromGeometry(device.Get(), geometry, nullptr, "morphed cube"));
            REQUIRE(morphed->MorphTargetCount() == kTargets);
            PrepareMesh(device.Get(), morphed.Get());

            renderer::Camera camera;
            camera.SetLookAt(Vec3(0.3f, 1.5f, 3.0f), Vec3(0.3f, 0, 0), Vec3(0, 1, 0));
            camera.aspect = 1.0f;
            camera.near_plane = 0.5f;
            camera.far_plane = 50.0f;
            const auto draw = [&](renderer::GpuMesh* mesh, f32 x, const f32* weight, Vector<u8>* pixels)
            {
                renderer::RenderSceneDesc desc;
                desc.max_objects = 8;
                desc.max_lights = 8;
                renderer::RenderScene scene(desc);
                renderer::RenderObjectDesc od;
                od.mesh = mesh;
                od.material = glow.index;
                od.transform = Mat4::FromBasis(Vec3(1, 0, 0), Vec3(0, 1, 0), Vec3(0, 0, 1), Vec3(x, 0, 0));
                od.morph_weights = weight;
                od.morph_weight_count = weight != nullptr ? kTargets : 0;
                scene.AddObject(od);
                return Render(frame, device.Get(), scene, camera, pixels);
            };
            const f32 zero[kTargets] = {};
            const f32 half[kTargets] = {0.5f, 0.5f, 0.5f, 0.5f, 0.5f, 0.5f, 0.5f, 0.5f};
            const f32 one[kTargets] = {1, 1, 1, 1, 1, 1, 1, 1};
            Vector<u8> base;
            Vector<u8> moved;
            Vector<u8> halfway;
            Vector<u8> picture;
            REQUIRE(draw(plain.Get(), 0.0f, nullptr, &base));
            REQUIRE(draw(plain.Get(), 0.6f, nullptr, &moved));
            REQUIRE(draw(plain.Get(), 0.3f, nullptr, &halfway));
            const u32 pixels = kSize * kSize;
            INFO("the plain cube moved by 0.6 differs from the plain cube in " << PixelsThatDiffer(base, moved, 8)
                                                                               << " pixels");
            REQUIRE(PixelsThatDiffer(base, moved, 8) > pixels / 20);  // the move is visible

            REQUIRE(draw(morphed.Get(), 0.0f, zero, &picture));
            CHECK(LargestDifference(base, picture) == 0);  // weight 0: the base shape
            REQUIRE(draw(morphed.Get(), 0.0f, nullptr, &picture));
            CHECK(LargestDifference(base, picture) == 0);  // no weights: the base shape
            REQUIRE(draw(morphed.Get(), 0.0f, one, &picture));
            INFO("weight 1 against the moved cube: " << PixelsThatDiffer(moved, picture, 8) << " pixels differ");
            CHECK(PixelsThatDiffer(moved, picture, 8) <= pixels / 200);
            REQUIRE(draw(morphed.Get(), 0.0f, half, &picture));
            INFO("weight 0.5 against the cube moved by 0.3: " << PixelsThatDiffer(halfway, picture, 8)
                                                              << " pixels differ");
            CHECK(PixelsThatDiffer(halfway, picture, 8) <= pixels / 200);
            device->WaitIdle();
        }
        device->WaitIdle();
        device.Reset();
    }
}

// glTF coverage Day 14 (the Mac): a spot light on the GPU (Day 11: the point-light pass with the cone factor). A spot
// light 1 m in front of a cube's face, shining at it with a narrow cone, lights the middle of the face as a point light
// at the same place does (inside the inner cone the factor is 1) and leaves the face's edge, outside the outer cone,
// black, where the point light still reaches.
TEST_CASE("A spot light lights inside its cone as a point light does, and nothing outside it", "[scene][render][gpu]")
{
    FOR_EACH_GPU_BACKEND(backend)
    {
        gpu_test::BackendValidation validation(backend);
        rhi::DevicePtr device = gpu_test::CreateTestDevice(backend);  // with Vulkan's synchronization validation
        {
            renderer_demo::FrameSetup setup;
            setup.device = device.Get();
            setup.max_objects = 8;
            setup.max_lights = 8;
            renderer_demo::DemoFrame frame;
            REQUIRE(frame.Create(setup));
            renderer::MaterialSystem* materials = frame.Materials();
            const renderer::MaterialId white = renderer::CreateGBufferMaterial(materials, frame.Templates(),
                                                                               renderer::GBufferOptions{}, "white");
            REQUIRE(materials->SetVec4(white, "base_color", Vec4(0.9f, 0.9f, 0.9f, 1.0f)));
            renderer::MeshData data;
            REQUIRE(renderer::MakeCubeMesh(1.0f, &data) == nullptr);
            auto cube = MakeShared<renderer::GpuMesh>();
            REQUIRE(cube->Create(device.Get(), data, "cube"));
            PrepareMesh(device.Get(), cube.Get());

            // Straight at the face at z = 0.5, which fills the middle of the picture.
            renderer::Camera camera;
            camera.SetLookAt(Vec3(0, 0, 2.5f), Vec3(0, 0, 0), Vec3(0, 1, 0));
            camera.aspect = 1.0f;
            camera.near_plane = 0.5f;
            camera.far_plane = 50.0f;
            const auto draw = [&](bool lit, renderer::LightType type, Vector<u8>* pixels)
            {
                renderer::RenderSceneDesc desc;
                desc.max_objects = 8;
                desc.max_lights = 8;
                renderer::RenderScene scene(desc);
                renderer::RenderObjectDesc od;
                od.mesh = cube.Get();
                od.material = white.index;
                od.transform = Mat4::FromBasis(Vec3(1, 0, 0), Vec3(0, 1, 0), Vec3(0, 0, 1), Vec3(0, 0, 0));
                scene.AddObject(od);
                renderer::Light light;
                light.type = type;
                light.position = Vec3(0, 0, 1.5f);
                light.direction = Vec3(0, 0, 1);  // toward the light: it shines along -z, at the face
                light.radius = 6.0f;
                light.intensity = 2.0f;
                light.inner_cone = 0.10f;  // about 0.1 m around the face's middle at full strength
                light.outer_cone = 0.15f;  // nothing beyond about 0.15 m
                if (lit)
                {
                    scene.AddLight(light);
                }
                return Render(frame, device.Get(), scene, camera, pixels);
            };
            Vector<u8> dark;
            Vector<u8> point;
            Vector<u8> spot;
            REQUIRE(draw(false, renderer::LightType::Point, &dark));
            REQUIRE(draw(true, renderer::LightType::Point, &point));
            REQUIRE(draw(true, renderer::LightType::Spot, &spot));
            // The face's extent along the middle row: where the point light adds to the picture without a light (so the
            // test does not depend on the background or on an ambient term).
            const u32 row = kSize / 2;
            const auto red = [&](const Vector<u8>& p, u32 x) { return static_cast<int>(p[(row * kSize + x) * 4]); };
            u32 left = kSize;
            u32 right = 0;
            for (u32 x = 0; x < kSize; ++x)
            {
                if (red(point, x) > red(dark, x))
                {
                    left = x < left ? x : left;
                    right = x > right ? x : right;
                }
            }
            REQUIRE(left < right);
            const u32 middle = (left + right) / 2;
            const u32 edge = left + (right - left) / 10;  // 0.4 m from the middle: about 22 degrees off the axis
            INFO("face " << left << ".." << right << ": point " << red(point, middle) << " / " << red(point, edge)
                         << ", spot " << red(spot, middle) << " / " << red(spot, edge));
            CHECK(red(point, edge) > red(dark, edge));                // the point light reaches the edge
            CHECK(red(spot, edge) == red(dark, edge));                // the spot light adds nothing there
            CHECK(red(spot, middle) > red(dark, middle));             // it lights the middle
            CHECK(Abs(red(spot, middle) - red(point, middle)) <= 1);  // as the point light does, inside the inner cone
            device->WaitIdle();
        }
        device->WaitIdle();
        device.Reset();
    }
}
