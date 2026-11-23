#pragma once

// A deferred demo frame on a device the host can replace, for the Phase 8 acceptance tests: every device-owning part
// registers with one registry, the offscreen target is a registered owner too, and the renderer replaces the device
// through the host.

#include <catch2/catch_test_macros.hpp>

#include "backend_fixture.h"
#include "image_compare.h"

#include "demo_frame.h"
#include "demo_scene.h"
#include "renderer/device_resources.h"
#include "renderer/renderer.h"
#include "rhi/device_host.h"

namespace reset_rig
{

using namespace vse;

// The offscreen render target as a registered owner: it is made again on the new device.
struct Target final : renderer::Recreatable
{
    rhi::Device* device = nullptr;
    rhi::TextureHandle handle;
    u32 width = 96;
    u32 height = 64;
    bool Make(rhi::Device* d)
    {
        rhi::TextureDesc td;
        td.width = width;
        td.height = height;
        td.format = rhi::Format::RGBA8_SRGB;
        td.usage = rhi::TextureUsage::ColorAttachment | rhi::TextureUsage::CopySrc;
        handle = d->CreateTexture(td);
        device = d;
        return handle.IsValid();
    }
    void Release() override
    {
        if (device != nullptr && handle.IsValid())
        {
            device->DestroyTexture(handle);
        }
        handle = {};
        device = nullptr;
    }
    bool Recreate(rhi::Device* d) override { return Make(d); }
    bool Live() const override { return handle.IsValid(); }
    const char* RecreatableName() const override { return "offscreen target"; }
};

struct Rig
{
    renderer::DeviceResources registry;  // destroyed last
    Target target;
    rhi::DeviceHost host;
    renderer_demo::DemoFrame frame;
    renderer_demo::DemoScene scene;
    renderer::Camera camera;
    rhi::DeviceCreateInfo info;
    renderer::RenderSettings initial;
    renderer::ScopedRegistration target_registration;
    Vec3d origin_offset = Vec3d(0, 0, 0);  // set before Create to stand the scene far from the origin
    bool screen = false;              // set before Create to add the scene's standing screen (SceneOptions::screen)
    const char* font_path = nullptr;  // set before Create to give the frame the text overlay (DemoFrame::SetOverlay)

    bool Create(rhi::Backend backend, u32 objects = 64, u32 lights = 8, bool prewarm = false,
                jobs::Scheduler* scheduler = nullptr, const char* asset_dir = nullptr, bool validation = false)
    {
        info.backend = backend;
        info.validation = validation ? rhi::ValidationSetting::On : rhi::ValidationSetting::Off;
        info.sync_validation = validation;
        info.pipeline_cache = false;
        initial.SetEnum(renderer::settings::kBackend, gpu_test::BackendSettingIndex(backend));
        initial.SetEnum(renderer::settings::kValidation, validation ? 2 : 1);
        initial.SetBool(renderer::settings::kSyncValidation, validation);
        host.AddObserver(&registry, 0);
        if (!host.Create(info, rhi::SwapchainDesc{}))
        {
            return false;
        }
        if (!target.Make(host.GetDevice()))
        {
            return false;
        }
        target_registration.Attach(&registry, &target, renderer::tiers::kTextures);
        renderer_demo::FrameSetup fs;
        fs.device = host.GetDevice();
        fs.max_objects = objects + 16;
        fs.max_lights = lights + 2;
        fs.resources = &registry;
        fs.prewarm_pipelines = prewarm;
        fs.scheduler = scheduler;
        fs.host = &host;
        fs.create_info = info;
        fs.initial_settings = &initial;
        fs.font_path = font_path;
        if (!frame.Create(fs))
        {
            return false;
        }
        renderer_demo::SceneOptions so;
        so.objects = objects;
        so.lights = lights;
        so.asset_dir = asset_dir;
        so.resources = &registry;
        so.screen = screen;
        so.origin_offset = origin_offset;
        if (!scene.Create(host.GetDevice(), frame.Materials(), frame.Templates(), so))
        {
            return false;
        }
        scene.Animate(0.5f);
        frame.SetTime(0.5f, 0.016f);
        camera = scene.OverviewCamera(static_cast<f32>(target.width) / static_cast<f32>(target.height));
        FitCameraDepthRange(scene.Scene(), &camera, 0.1f);
        frame.GetRenderer()->SetResetCallbacks(
            nullptr,
            [](void* user, bool)
            {
                auto* self = static_cast<Rig*>(user);
                rhi::Device* d = self->frame.GetRenderer()->GetDevice();
                if (d != nullptr)
                {
                    self->frame.Rebind(d, self->frame.GetRenderer()->GetSwapchain());
                    self->scene.Rebind(d);
                }
            },
            this);
        return true;
    }
    renderer::Renderer& R() { return *frame.GetRenderer(); }
    renderer::FrameStatus Render()
    {
        renderer::FrameTarget ft;
        ft.texture = target.handle;
        ft.width = target.width;
        ft.height = target.height;
        ft.format = rhi::Format::RGBA8_SRGB;
        ft.final_state = rhi::ResourceState::CopySrc;
        return frame.Render(&scene.Scene(), &camera, &ft);
    }
    // Reads the target back (level 0 as RGBA8) into `image`.
    bool Read(gpu_test::Image* image)
    {
        host.GetDevice()->WaitIdle();
        image->width = target.width;
        image->height = target.height;
        image->rgba.assign(static_cast<usize>(target.width) * target.height * 4, 0);
        return host.GetDevice()->ReadbackTexture(target.handle, 0, 0, image->rgba.data(), image->rgba.size(),
                                                 rhi::ResourceState::CopySrc);
    }
    // Applies a device reset by setting sync validation to `sync_validation`; the value must differ from the one in
    // force, because a reset only happens for a real change.
    renderer::ApplyResult ResetDevice(bool sync_validation)
    {
        renderer::RenderSettings wanted = R().Settings();
        wanted.SetBool(renderer::settings::kSyncValidation, sync_validation);
        return R().ApplySettings(wanted);
    }
    ~Rig() { TearDown(); }  // a failed REQUIRE unwinds through here: tear down in the right order, not in member order
    // Safe to call twice.
    void TearDown()
    {
        if (host.GetDevice() != nullptr)
        {
            host.GetDevice()->WaitIdle();
        }
        scene.Destroy();
        frame.Destroy();
        target_registration.Reset();
        target.Release();
        host.Destroy();
    }
};

// Checks that two snapshots of the device's live objects are equal in every kind and in device memory.
inline void ExpectSameCounts(const rhi::ResourceCounts& now, const rhi::ResourceCounts& first)
{
    CHECK(now.buffers == first.buffers);
    CHECK(now.textures == first.textures);
    CHECK(now.samplers == first.samplers);
    CHECK(now.shaders == first.shaders);
    CHECK(now.bind_group_layouts == first.bind_group_layouts);
    CHECK(now.bind_groups == first.bind_groups);
    CHECK(now.pipelines == first.pipelines);
    CHECK(now.swapchains == first.swapchains);
    CHECK(now.device_memory_bytes == first.device_memory_bytes);
}

}  // namespace reset_rig
