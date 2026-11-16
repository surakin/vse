// Phase 24 Day 358: spikes for the C API's scenes. Throwaway-grade measurements kept as tests so the numbers can be
// reproduced; the spec (docs/superpowers/specs/2026-11-16-c-api-scenes-design.md) quotes them.
//
// Spike 1: a renderer on the device and swapchain a surface has (Day 352 builds the same pair from a host window),
// fed by an ECS world through scene::RenderSync, presenting to the window and rendering offscreen on the same device.
#include <catch2/catch_test_macros.hpp>

#include "build_defs.h"
#include "cstr/mem_ops.h"
#include "demo_frame.h"
#include "display_session.h"
#include "ecs/schedule.h"
#include "ecs/transform.h"
#include "os/console.h"
#include "os/os.h"
#include "platform/input.h"
#include "platform/window_host.h"
#include "renderer/render_scene.h"
#include "scene/render_sync.h"

using namespace engine_placeholder;

namespace
{

constexpr u32 kWidth = 320;
constexpr u32 kHeight = 200;

// The platform's window kind: X11 on Linux, Win32 on Windows (Day 369), with Vulkan as Day 358 measured it.
constexpr os::WindowBackend kWindowBackend = ENGINE_PLATFORM_WINDOWS ? os::WindowBackend::Win32
                                                                     : os::WindowBackend::X11;

bool HaveDisplay()
{
#if ENGINE_PLATFORM_WINDOWS
    return true;  // the desktop is there (a locked session is skipped by SKIP_IF_NO_DISPLAY_SESSION)
#else
    const char* display = os::GetEnvironment("DISPLAY");
    return display != nullptr && display[0] != '\0';
#endif
}

}  // namespace

TEST_CASE("Spike 1: a renderer on a window's device and swapchain draws an ECS world and reads it back",
          "[scene][spike][gpu][window]")
{
    SKIP_IF_NO_DISPLAY_SESSION();
    if (!HaveDisplay())
    {
        SKIP("no display");
    }
    os::InputState input;
    os::PlatformWindowHost host;
    if (!host.Open(kWindowBackend, "scene spike", kWidth, kHeight, &input))
    {
        SKIP("no window");
    }
    rhi::DeviceCreateInfo info;
    info.backend = rhi::Backend::Vulkan;
    info.window = host.Get();
    info.pipeline_cache = false;
    info.validation = rhi::ValidationSetting::On;
    info.sync_validation = true;
    rhi::DevicePtr device = rhi::CreateDevice(info);
    if (device.Get() == nullptr)
    {
        SKIP("no Vulkan device can present to this window");
    }
    rhi::Swapchain* swapchain = device->CreateSwapchain(rhi::SwapchainDesc{});
    REQUIRE(swapchain != nullptr);

    renderer_demo::FrameSetup setup;
    setup.device = device.Get();
    setup.swapchain = swapchain;
    setup.max_objects = 64;
    setup.max_lights = 8;
    renderer_demo::DemoFrame frame;
    REQUIRE(frame.Create(setup));

    const renderer::MaterialId material = renderer::CreateGBufferMaterial(frame.Materials(), frame.Templates(),
                                                                          renderer::GBufferOptions{}, "spike");
    REQUIRE(frame.Materials()->SetVec4(material, "base_color", Vec4(0.8f, 0.3f, 0.1f, 1.0f)));
    auto cube = MakeShared<renderer::GpuMesh>();
    renderer::MeshData data;
    REQUIRE(renderer::MakeCubeMesh(1.0f, &data) == nullptr);
    REQUIRE(cube->Create(device.Get(), data, "cube"));
    {
        device->BeginFrame();
        rhi::CommandList* list = device->CreateCommandList();
        list->Begin();
        cube->RecordPrepare(list);
        list->End();
        rhi::CommandList* lists[] = {list};
        REQUIRE(device->Submit(lists, 1) != 0);
        device->EndFrame();
        device->WaitIdle();
        cube->MarkReady();
    }

    renderer::RenderSceneDesc desc;
    desc.max_objects = 64;
    desc.max_lights = 8;
    renderer::RenderScene render_scene(desc);
    ecs::World world;
    world.SetResource(ecs::TransformReport{});
    world.SetResource(scene::SceneLink{&render_scene});
    world.SetResource(scene::SceneSyncReport{});
    ecs::Schedule schedule;
    schedule.Add(ecs::MakeTransformSystem("transforms"));
    schedule.Add(scene::MakeRenderSyncSystem("render_sync").InStage(ecs::kRender));
    REQUIRE(schedule.Build().ok);
    ecs::LocalTransform local;
    local.position = Vec3(0, 0, 0);
    scene::MeshRenderer mr;
    mr.mesh = cube;
    mr.material = material.index;
    world.Create(local, ecs::WorldTransform{}, mr);
    scene::LightSource sun;
    sun.type = renderer::LightType::Directional;
    sun.direction = Vec3(0.3f, 0.8f, 0.5f).Normalized();
    sun.intensity = 2.0f;
    world.Create(ecs::LocalTransform{}, ecs::WorldTransform{}, sun);
    scene::LightSource ambient;
    ambient.type = renderer::LightType::Ambient;
    ambient.intensity = 0.2f;
    world.Create(ecs::LocalTransform{}, ecs::WorldTransform{}, ambient);

    renderer::Camera camera;
    camera.SetLookAt(Vec3(0, 1.5f, 4), Vec3(0, 0, 0), Vec3(0, 1, 0));
    camera.aspect = static_cast<f32>(kWidth) / static_cast<f32>(kHeight);
    camera.near_plane = 0.5f;
    camera.far_plane = 50.0f;

    // Presented to the window, the way `RenderAll` would on every `engine_frame`.
    constexpr int kFrames = 60;
    u32 ok_frames = 0;
    const u64 start = os::MonotonicTimeNanos();
    for (int i = 0; i < kFrames; ++i)
    {
        host.Get()->PumpEvents();
        schedule.Run(world, 0.016f);
        if (frame.Render(&render_scene, &camera, nullptr) == renderer::FrameStatus::Ok)
        {
            ++ok_frames;
        }
    }
    device->WaitIdle();
    const f64 ms = static_cast<f64>(os::MonotonicTimeNanos() - start) / 1e6 / kFrames;
    os::Print("spike 1: %u of %d frames presented, %.2f ms per frame (sync + record + submit + present)\n", ok_frames,
              kFrames, ms);
    CHECK(ok_frames == static_cast<u32>(kFrames));
    CHECK(frame.LastGBufferOk());

    // Offscreen on the same device: what `surface_read_pixels` would hand back.
    rhi::TextureDesc td;
    td.width = kWidth;
    td.height = kHeight;
    td.format = rhi::Format::RGBA8_SRGB;
    td.usage = rhi::TextureUsage::ColorAttachment | rhi::TextureUsage::CopySrc;
    const rhi::TextureHandle target = device->CreateTexture(td);
    REQUIRE(target.IsValid());
    renderer::FrameTarget ft;
    ft.texture = target;
    ft.width = kWidth;
    ft.height = kHeight;
    ft.format = rhi::Format::RGBA8_SRGB;
    ft.final_state = rhi::ResourceState::CopySrc;
    // With a swapchain the renderer ignores an offscreen target and presents (RenderFrameImpl): the pixels of a
    // surface's frame cannot be read between the graph and the present, so a second frame object without a swapchain
    // stands in here (finding: the renderer needs an after-execute, before-present hook, or the surface renders
    // offscreen and blits).
    renderer_demo::FrameSetup offscreen_setup;
    offscreen_setup.device = device.Get();
    offscreen_setup.max_objects = 64;
    offscreen_setup.max_lights = 8;
    renderer_demo::DemoFrame offscreen_frame;
    REQUIRE(offscreen_frame.Create(offscreen_setup));
    const renderer::MaterialId material2 = renderer::CreateGBufferMaterial(
        offscreen_frame.Materials(), offscreen_frame.Templates(), renderer::GBufferOptions{}, "spike");
    REQUIRE(offscreen_frame.Materials()->SetVec4(material2, "base_color", Vec4(0.8f, 0.3f, 0.1f, 1.0f)));
    CHECK(material2.index == material.index);
    renderer::FrameStatus status = renderer::FrameStatus::Failed;
    for (int i = 0; i < 3; ++i)
    {
        status = offscreen_frame.Render(&render_scene, &camera, &ft);
    }
    device->WaitIdle();
    Vector<u8> pixels;
    pixels.Resize(kWidth * kHeight * 4);
    REQUIRE(status == renderer::FrameStatus::Ok);
    REQUIRE(device->ReadbackTexture(target, 0, 0, pixels.Data(), pixels.Size(), rhi::ResourceState::CopySrc));
    bool varied = false;
    for (usize i = 4; i < pixels.Size() && !varied; i += 4)
    {
        varied = MemCompare(&pixels[i], &pixels[0], 4) != 0;
    }
    os::Print("spike 1: objects %u lights %u visible %u drawn %u first pixel %u %u %u %u\n", render_scene.ObjectCount(),
              render_scene.LightCount(), offscreen_frame.GBufferStatistics().visible,
              offscreen_frame.GBufferStatistics().drawn_objects, pixels[0], pixels[1], pixels[2], pixels[3]);
    CHECK(varied);
    device->DestroyTexture(target);
    device->WaitIdle();
    offscreen_frame.Destroy();
    frame.Destroy();
    device->DestroySwapchain(swapchain);
}

// ---- Spike 2: the same content resident on two devices ------------------------------------------------------------
//
// A host's surface owns its own device (Day 352), so a mesh or texture of the resource host has to be made resident on
// each. The numbers: what one device pays for a mesh and a texture, and what a second device adds.

#include "containers/shared_ptr.h"
#include "renderer/mesh.h"
#include "renderer/texture.h"
#include "sync/atomic.h"
#include "sync/mutex.h"
#include "sync/thread.h"

namespace
{

f64 NowMs()
{
    return static_cast<f64>(os::MonotonicTimeNanos()) / 1e6;
}

rhi::DevicePtr MakeVulkanDevice(os::Window* window)
{
    rhi::DeviceCreateInfo info;
    info.backend = rhi::Backend::Vulkan;
    info.window = window;
    info.pipeline_cache = false;
    info.validation = rhi::ValidationSetting::On;
    info.sync_validation = true;
    return rhi::CreateDevice(info);
}

// A mesh the size of a character (a 256 x 128 sphere: 33k vertices, 196k indices) and a 1024 x 1024 RGBA texture.
bool MakeContent(rhi::Device* device, renderer::GpuMesh* mesh, renderer::GpuTexture* texture, f64* mesh_ms,
                 f64* texture_ms)
{
    renderer::MeshData data;
    if (renderer::MakeSphereMesh(1.0f, 256, 128, &data) != nullptr)
    {
        return false;
    }
    f64 t = NowMs();
    if (!mesh->Create(device, data, "spike mesh"))
    {
        return false;
    }
    device->BeginFrame();
    rhi::CommandList* list = device->CreateCommandList();
    list->Begin();
    mesh->RecordPrepare(list);
    list->End();
    rhi::CommandList* lists[] = {list};
    device->Submit(lists, 1);
    device->EndFrame();
    device->WaitIdle();
    mesh->MarkReady();
    *mesh_ms = NowMs() - t;

    renderer::TextureImage image;
    image.desc.width = 1024;
    image.desc.height = 1024;
    image.desc.format = rhi::Format::RGBA8_SRGB;
    image.desc.usage = rhi::TextureUsage::Sampled;
    image.pixels.Resize(1024u * 1024u * 4u);
    for (usize i = 0; i < image.pixels.Size(); ++i)
    {
        image.pixels[i] = static_cast<u8>(i * 31u);
    }
    t = NowMs();
    if (!texture->Create(device, static_cast<renderer::TextureImage&&>(image), nullptr, "spike texture"))
    {
        return false;
    }
    *texture_ms = NowMs() - t;
    return true;
}

}  // namespace

TEST_CASE("Spike 2: the content of one resource is resident on two devices; the second device's cost",
          "[scene][spike][gpu][window]")
{
    SKIP_IF_NO_DISPLAY_SESSION();
    if (!HaveDisplay())
    {
        SKIP("no display");
    }
    os::InputState input;
    os::PlatformWindowHost host;
    if (!host.Open(kWindowBackend, "scene spike 2", kWidth, kHeight, &input))
    {
        SKIP("no window");
    }
    rhi::DevicePtr surface_device = MakeVulkanDevice(host.Get());
    rhi::DevicePtr resource_device = MakeVulkanDevice(nullptr);
    if (surface_device.Get() == nullptr || resource_device.Get() == nullptr)
    {
        SKIP("no Vulkan device (two are needed)");
    }
    renderer::GpuMesh mesh_a;
    renderer::GpuMesh mesh_b;
    renderer::GpuTexture texture_a;
    renderer::GpuTexture texture_b;
    f64 mesh_a_ms = 0, texture_a_ms = 0, mesh_b_ms = 0, texture_b_ms = 0;
    REQUIRE(MakeContent(surface_device.Get(), &mesh_a, &texture_a, &mesh_a_ms, &texture_a_ms));
    REQUIRE(MakeContent(resource_device.Get(), &mesh_b, &texture_b, &mesh_b_ms, &texture_b_ms));
    os::Print(
        "spike 2: mesh (33k vertices) %.2f ms on the surface device, %.2f ms on a second device; texture 1024x1024 "
        "%.2f ms and %.2f ms; two devices coexist in one process\n",
        mesh_a_ms, mesh_b_ms, texture_a_ms, texture_b_ms);
    CHECK(mesh_a.Ready());
    CHECK(mesh_b.Ready());
    surface_device->WaitIdle();
    resource_device->WaitIdle();
}

// ---- Spike 3: a texture the host updates every frame from another thread
// ---------------------------------------------
//
// A latest-wins slot (the host thread copies its pixels in under a lock; a newer update replaces an unread one), a
// staging ring (one persistently mapped upload buffer per frame in flight), one copy per rendered frame.

namespace
{

struct LiveSlot
{
    Mutex mutex;
    Vector<u8> pixels;
    u64 version = 0;  // incremented by every update
};

struct SourceThreadState
{
    LiveSlot* slot = nullptr;
    u32 width = 0;
    u32 height = 0;
    Atomic<bool>* stop = nullptr;
    Atomic<u64>* updates = nullptr;
};

void SourceThread(SourceThreadState state)
{
    Vector<u8> mine;
    mine.Resize(static_cast<usize>(state.width) * state.height * 4u);
    u32 phase = 0;
    while (!state.stop->Load(MemoryOrder::Acquire))
    {
        for (usize i = 0; i < mine.Size(); i += 4)
        {
            mine[i] = static_cast<u8>(phase + i);
            mine[i + 1] = static_cast<u8>(phase * 3u);
            mine[i + 2] = 0x40;
            mine[i + 3] = 0xFF;
        }
        ++phase;
        {
            LockGuard<Mutex> lock(state.slot->mutex);
            MemCopy(state.slot->pixels.Data(), mine.Data(), mine.Size());
            ++state.slot->version;
        }
        state.updates->FetchAdd(1, MemoryOrder::Relaxed);
        ThisThread::SleepForMilliseconds(2);  // a producer faster than the display
    }
}

void LiveUploadRun(rhi::Device* device, u32 width, u32 height, int frames)
{
    constexpr u32 kRing = 3;
    const u32 pitch = width * 4u;
    REQUIRE(pitch % device->Caps().buffer_texture_copy_row_pitch_alignment == 0);
    rhi::TextureDesc td;
    td.width = width;
    td.height = height;
    td.format = rhi::Format::RGBA8_SRGB;
    td.usage = rhi::TextureUsage::Sampled | rhi::TextureUsage::CopyDst;
    const rhi::TextureHandle texture = device->CreateTexture(td);
    REQUIRE(texture.IsValid());
    rhi::BufferHandle staging[kRing];
    u8* mapped[kRing];
    for (u32 i = 0; i < kRing; ++i)
    {
        rhi::BufferDesc bd;
        bd.size = static_cast<u64>(pitch) * height;
        bd.usage = rhi::BufferUsage::CopySrc;
        bd.location = rhi::MemoryLocation::CpuToGpu;
        staging[i] = device->CreateBuffer(bd);
        REQUIRE(staging[i].IsValid());
        mapped[i] = static_cast<u8*>(device->MapBuffer(staging[i]));
        REQUIRE(mapped[i] != nullptr);
    }
    LiveSlot slot;
    slot.pixels.Resize(static_cast<usize>(pitch) * height);
    Atomic<bool> stop(false);
    Atomic<u64> updates(0);
    Thread producer(SourceThread, SourceThreadState{&slot, width, height, &stop, &updates});

    u64 seen = 0;
    u32 uploads = 0;
    f64 upload_ms_total = 0;
    f64 upload_ms_worst = 0;
    rhi::ResourceState state = rhi::ResourceState::Undefined;
    const f64 start = NowMs();
    for (int frame = 0; frame < frames; ++frame)
    {
        device->BeginFrame();
        const u32 ring = static_cast<u32>(frame) % kRing;
        bool fresh = false;
        const f64 t = NowMs();
        {
            LockGuard<Mutex> lock(slot.mutex);
            if (slot.version != seen)
            {
                seen = slot.version;
                MemCopy(mapped[ring], slot.pixels.Data(), slot.pixels.Size());
                fresh = true;
            }
        }
        rhi::CommandList* list = device->CreateCommandList();
        list->Begin();
        if (fresh)
        {
            list->Barrier(texture, state, rhi::ResourceState::CopyDst);
            rhi::BufferTextureCopy region;
            region.buffer_row_pitch = pitch;
            list->CopyBufferToTexture(staging[ring], texture, region);
            list->Barrier(texture, rhi::ResourceState::CopyDst, rhi::ResourceState::ShaderRead);
            state = rhi::ResourceState::ShaderRead;
            ++uploads;
        }
        list->End();
        rhi::CommandList* lists[] = {list};
        device->Submit(lists, 1);
        device->EndFrame();
        if (fresh)
        {
            const f64 ms = NowMs() - t;
            upload_ms_total += ms;
            if (ms > upload_ms_worst)
            {
                upload_ms_worst = ms;
            }
        }
        ThisThread::SleepForMilliseconds(16);  // a 60 Hz host
    }
    device->WaitIdle();
    const f64 total_ms = NowMs() - start;
    stop.Store(true, MemoryOrder::Release);
    producer.Join();
    os::Print("spike 3: %ux%u: %d frames in %.0f ms, %llu source updates, %u uploads (one per frame at most), "
              "%.3f ms per upload (CPU copy + record + submit), worst %.3f ms\n",
              width, height, frames, total_ms, static_cast<unsigned long long>(updates.Load(MemoryOrder::Relaxed)),
              uploads, uploads != 0 ? upload_ms_total / uploads : 0.0, upload_ms_worst);
    CHECK(uploads > 0);
    CHECK(static_cast<int>(uploads) <= frames);
    CHECK(updates.Load(MemoryOrder::Relaxed) >= uploads);  // never more than one upload per frame, however many updates
    for (u32 i = 0; i < kRing; ++i)
    {
        device->UnmapBuffer(staging[i]);
        device->DestroyBuffer(staging[i]);
    }
    device->DestroyTexture(texture);
}

}  // namespace

TEST_CASE("Spike 3: a texture updated from a second thread every frame, 1280x720 and 320x180", "[scene][spike][gpu]")
{
    rhi::DevicePtr device = MakeVulkanDevice(nullptr);
    if (device.Get() == nullptr)
    {
        SKIP("no Vulkan device");
    }
    LiveUploadRun(device.Get(), 1280, 720, 120);
    LiveUploadRun(device.Get(), 320, 180, 120);
}

// ---- Spike 4: text in the world
// ---------------------------------------------------------------------------------------
//
// The text renderer draws 2D, in pixels, over a frame's target (DemoFrame's overlay is its client). A label at a world
// point is that path with the point projected through the camera: constant pixel size, no depth test. The numbers: what
// a frame costs with 0, ~40, ~400 and ~4000 glyphs on the overlay (a proxy for 1, 10 and 100 name tags).
TEST_CASE("Spike 4: the cost of glyphs drawn over a frame (labels projected from world points)", "[scene][spike][gpu]")
{
    rhi::DevicePtr device = MakeVulkanDevice(nullptr);
    if (device.Get() == nullptr)
    {
        SKIP("no Vulkan device");
    }
    renderer_demo::FrameSetup setup;
    setup.device = device.Get();
    setup.max_objects = 64;
    setup.max_lights = 8;
    setup.font_path = SPIKE_FONT_FILE;
    renderer_demo::DemoFrame frame;
    REQUIRE(frame.Create(setup));
    renderer::RenderSceneDesc desc;
    desc.max_objects = 64;
    desc.max_lights = 8;
    renderer::RenderScene scene(desc);
    renderer::Camera camera;
    camera.SetLookAt(Vec3(0, 1.5f, 4), Vec3(0, 0, 0), Vec3(0, 1, 0));
    camera.aspect = 16.0f / 9.0f;
    rhi::TextureDesc td;
    td.width = 1280;
    td.height = 720;
    td.format = rhi::Format::RGBA8_SRGB;
    td.usage = rhi::TextureUsage::ColorAttachment | rhi::TextureUsage::CopySrc;
    const rhi::TextureHandle target = device->CreateTexture(td);
    renderer::FrameTarget ft;
    ft.texture = target;
    ft.width = 1280;
    ft.height = 720;
    ft.format = rhi::Format::RGBA8_SRGB;
    ft.final_state = rhi::ResourceState::CopySrc;
    const u32 glyph_counts[] = {0, 40, 400, 4000};
    f64 base_ms = 0;
    for (const u32 glyphs : glyph_counts)
    {
        char text[4100];
        for (u32 i = 0; i < glyphs; ++i)
        {
            text[i] = static_cast<char>('a' + i % 26);
        }
        text[glyphs] = 0;
        if (!frame.SetOverlay(glyphs != 0, StringView("x", 1), StringView(text, glyphs)))
        {
            SKIP("the overlay font did not open");
        }
        for (int i = 0; i < 5; ++i)
        {
            REQUIRE(frame.Render(&scene, &camera, &ft) == renderer::FrameStatus::Ok);
        }
        device->WaitIdle();
        const f64 start = NowMs();
        constexpr int kFrames = 60;
        for (int i = 0; i < kFrames; ++i)
        {
            REQUIRE(frame.Render(&scene, &camera, &ft) == renderer::FrameStatus::Ok);
        }
        device->WaitIdle();
        const f64 ms = (NowMs() - start) / kFrames;
        if (glyphs == 0)
        {
            base_ms = ms;
        }
        os::Print("spike 4: %4u glyphs over a 1280x720 frame: %.3f ms per frame (%+.3f ms against none)\n", glyphs, ms,
                  ms - base_ms);
    }
    device->WaitIdle();
    device->DestroyTexture(target);
    frame.Destroy();
}

// ---- Spike 5: golden images across devices
// ----------------------------------------------------------------------------
//
// Only one GPU is installed here (no lavapipe: `/usr/share/vulkan/icd.d` holds the NVIDIA driver only), so the
// cross-GPU number cannot be taken; what can be measured is device-to-device and run-to-run agreement on this GPU,
// which is the floor of the tolerance. The rule the phase's goldens use is in the spec.
namespace
{

bool RenderLitCube(Vector<u8>* pixels)
{
    rhi::DevicePtr device = MakeVulkanDevice(nullptr);
    if (device.Get() == nullptr)
    {
        return false;
    }
    renderer_demo::FrameSetup setup;
    setup.device = device.Get();
    setup.max_objects = 16;
    setup.max_lights = 4;
    renderer_demo::DemoFrame frame;
    if (!frame.Create(setup))
    {
        return false;
    }
    const renderer::MaterialId material = renderer::CreateGBufferMaterial(frame.Materials(), frame.Templates(),
                                                                          renderer::GBufferOptions{}, "golden");
    frame.Materials()->SetVec4(material, "base_color", Vec4(0.8f, 0.3f, 0.1f, 1.0f));
    auto cube = MakeShared<renderer::GpuMesh>();
    renderer::MeshData data;
    if (renderer::MakeSphereMesh(0.9f, 32, 16, &data) != nullptr || !cube->Create(device.Get(), data, "sphere"))
    {
        return false;
    }
    device->BeginFrame();
    rhi::CommandList* list = device->CreateCommandList();
    list->Begin();
    cube->RecordPrepare(list);
    list->End();
    rhi::CommandList* lists[] = {list};
    device->Submit(lists, 1);
    device->EndFrame();
    device->WaitIdle();
    cube->MarkReady();
    renderer::RenderSceneDesc desc;
    desc.max_objects = 16;
    desc.max_lights = 4;
    renderer::RenderScene scene(desc);
    renderer::RenderObjectDesc od;
    od.mesh = cube.Get();
    od.material = material.index;
    od.transform = Mat4::FromBasis(Vec3(1, 0, 0), Vec3(0, 1, 0), Vec3(0, 0, 1), Vec3(0, 0, 0));
    scene.AddObject(od);
    renderer::Light sun;
    sun.type = renderer::LightType::Directional;
    sun.direction = Vec3(0.3f, 0.8f, 0.5f).Normalized();
    sun.intensity = 2.0f;
    scene.AddLight(sun);
    renderer::Light ambient;
    ambient.type = renderer::LightType::Ambient;
    ambient.intensity = 0.2f;
    scene.AddLight(ambient);
    renderer::Camera camera;
    camera.SetLookAt(Vec3(0, 1.0f, 3), Vec3(0, 0, 0), Vec3(0, 1, 0));
    camera.aspect = 1.0f;
    camera.near_plane = 0.5f;
    camera.far_plane = 50.0f;
    rhi::TextureDesc td;
    td.width = 128;
    td.height = 128;
    td.format = rhi::Format::RGBA8_SRGB;
    td.usage = rhi::TextureUsage::ColorAttachment | rhi::TextureUsage::CopySrc;
    const rhi::TextureHandle target = device->CreateTexture(td);
    renderer::FrameTarget ft;
    ft.texture = target;
    ft.width = ft.height = 128;
    ft.format = rhi::Format::RGBA8_SRGB;
    ft.final_state = rhi::ResourceState::CopySrc;
    renderer::FrameStatus status = renderer::FrameStatus::Failed;
    for (int i = 0; i < 3; ++i)
    {
        status = frame.Render(&scene, &camera, &ft);
    }
    device->WaitIdle();
    pixels->Resize(128u * 128u * 4u);
    const bool ok = status == renderer::FrameStatus::Ok &&
                    device->ReadbackTexture(target, 0, 0, pixels->Data(), pixels->Size(), rhi::ResourceState::CopySrc);
    device->DestroyTexture(target);
    device->WaitIdle();
    frame.Destroy();
    return ok;
}

}  // namespace

TEST_CASE("Spike 5: a lit frame rendered on two device instances agrees (the floor of a golden's tolerance)",
          "[scene][spike][gpu]")
{
    Vector<u8> a;
    Vector<u8> b;
    if (!RenderLitCube(&a))
    {
        SKIP("no Vulkan device");
    }
    REQUIRE(RenderLitCube(&b));
    REQUIRE(a.Size() == b.Size());
    u32 max_diff = 0;
    u32 differing = 0;
    for (usize i = 0; i < a.Size(); ++i)
    {
        const u32 d = a[i] > b[i] ? a[i] - b[i] : b[i] - a[i];
        if (d != 0)
        {
            ++differing;
        }
        if (d > max_diff)
        {
            max_diff = d;
        }
    }
    os::Print(
        "spike 5: two device instances, 128x128 lit sphere: %u of %zu channel values differ, largest difference %u\n",
        differing, a.Size(), max_diff);
    CHECK(max_diff <= 1);
}
