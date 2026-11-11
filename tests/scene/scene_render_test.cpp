// Phase 17 Day 233: an entity-built scene renders the same pixels as the RenderScene built by hand (per GPU backend).
#include <catch2/catch_test_macros.hpp>

#include "backend_fixture.h"
#include "cstr/mem_ops.h"
#include "demo_frame.h"
#include "ecs/schedule.h"
#include "ecs/transform.h"
#include "os/console.h"
#include "os/os.h"
#include "renderer/render_scene.h"
#include "scene/render_sync.h"

using namespace engine_placeholder;
using scene::ActiveCamera;
using scene::CameraOutput;
using scene::Hidden;
using scene::LightSource;
using scene::MakeCameraSystem;
using scene::MakeRenderSyncSystem;
using scene::MeshAsset;
using scene::MeshRenderer;
using scene::RenderHandle;
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
            auto cube = MakeShared<MeshAsset>();
            auto sphere = MakeShared<MeshAsset>();
            renderer::MeshData data;
            REQUIRE(renderer::MakeCubeMesh(1.0f, &data) == nullptr);
            REQUIRE(cube->mesh.Create(device.Get(), data, "cube"));
            renderer::MeshData round;
            REQUIRE(renderer::MakeSphereMesh(0.7f, 16, 8, &round) == nullptr);
            REQUIRE(sphere->mesh.Create(device.Get(), round, "sphere"));

            // The barriers that move the buffers out of their upload state, in one submission (as the demo scene does).
            {
                device->BeginFrame();
                rhi::CommandList* list = device->CreateCommandList();
                REQUIRE(list != nullptr);
                list->Begin();
                cube->mesh.RecordPrepare(list);
                sphere->mesh.RecordPrepare(list);
                list->End();
                rhi::CommandList* lists[] = {list};
                REQUIRE(device->Submit(lists, 1) != 0);
                device->EndFrame();
                device->WaitIdle();
                cube->mesh.MarkReady();
                sphere->mesh.MarkReady();
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
                od.mesh = p.sphere ? &sphere->mesh : &cube->mesh;
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
