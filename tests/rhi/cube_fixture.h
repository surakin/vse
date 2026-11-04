#pragma once

#include <cmath>
#include <cstring>
#include <memory>
#include <vector>

#include <catch2/catch_test_macros.hpp>

#include "cube_scene.h"
#include "gpu_fixture.h"
#include "image_compare.h"
#include "render_target.h"
#include "rhi/rhi.h"
#include "rhi/uniform_ring.h"
#include "rhi/vulkan/vulkan_device.h"

// A device with a color target and a depth buffer, and a way to render cubes into them, for the cube tests.
namespace cube_test {

using namespace engine_placeholder;
using namespace engine_placeholder::rhi;
using namespace engine_placeholder::rhi::vulkan;
using gpu_test::Image;

struct Draw {
    demo::CubeRenderer* cube;
    Mat4 model;
};

// A device with a 128x128 color target and a depth buffer, and a way to render cubes into them.
struct CubeScene {
    u32 size;
    gpu_test::GpuFixture fx;
    VulkanDevice* device = nullptr;
    std::unique_ptr<gpu_test::RenderTarget> target;
    TextureHandle depth;
    std::unique_ptr<UniformRing> ring;
    Mat4 view_projection;

    explicit CubeScene(const Vec3& eye = Vec3(0, 0, 3), u32 target_size = 128) : size(target_size) {
        fx.RequireValidation();
        DeviceCreateInfo info;
        info.validation = ValidationSetting::On;
        info.sync_validation = true;
        info.pipeline_cache = false;
        device = VulkanDevice::Create(info);
        if (device == nullptr) {
            SKIP("no suitable Vulkan device");
        }
        target = std::make_unique<gpu_test::RenderTarget>(device, size, size);
        TextureDesc td;
        td.width = size;
        td.height = size;
        td.format = device->Caps().depth_format;
        td.usage = TextureUsage::DepthAttachment;
        depth = device->CreateTexture(td);
        ring = std::make_unique<UniformRing>(device, 4096);
        SetCamera(eye);
    }
    ~CubeScene() {
        if (device == nullptr) {
            return;
        }
        device->WaitIdle();
        ring.reset();
        if (depth.IsValid()) device->DestroyTexture(depth);
        target.reset();
        DestroyDevice(device);
    }

    void SetCamera(const Vec3& eye) {
        view_projection = demo::PerspectiveVulkan(0.7853982f, 1.0f, 0.1f, 10.0f) * demo::LookAt(eye, Vec3(0, 0, 0), Vec3(0, 1, 0));
    }

    Format DepthFormat() const { return device->Caps().depth_format; }

    // Renders the draws in order over a gray background; returns the image.
    Image Render(const std::vector<Draw>& draws) {
        REQUIRE(target->Valid());
        REQUIRE(depth.IsValid());
        REQUIRE(ring->Valid());
        device->BeginFrame();
        ring->BeginFrame();
        CommandList* list = device->CreateCommandList();
        list->Begin();
        // Each renderer is prepared once per list, however many cubes it draws.
        for (usize i = 0; i < draws.size(); ++i) {
            bool seen = false;
            for (usize j = 0; j < i; ++j) seen = seen || draws[j].cube == draws[i].cube;
            if (!seen) draws[i].cube->Prepare(list);
        }
        list->Barrier(target->Texture(), ResourceState::Undefined, ResourceState::ColorAttachment);
        list->Barrier(depth, ResourceState::Undefined, ResourceState::DepthAttachment);
        RenderingDesc pass;
        pass.color_count = 1;
        pass.color[0].texture = target->Texture();
        pass.color[0].clear_color[0] = pass.color[0].clear_color[1] = pass.color[0].clear_color[2] = 0.2f;
        pass.depth.texture = depth;
        pass.depth.clear_depth = 1.0f;
        pass.depth.store = StoreOp::DontCare;
        list->BeginRendering(pass);
        for (const Draw& d : draws) {
            REQUIRE(d.cube->Draw(list, ring.get(), view_projection * d.model, size, size));
        }
        list->EndRendering();
        list->End();
        ring->Flush();
        CommandList* lists[] = {list};
        const bool submitted = device->Submit(lists, 1) != 0;
        device->EndFrame();
        REQUIRE(submitted);
        for (const Draw& d : draws) d.cube->Submitted();
        device->WaitIdle();
        Image image;
        REQUIRE(target->Readback(&image));
        return image;
    }

    // The pixel the view-projection puts the model-space point `p` (through `model`) on.
    void Project(const Mat4& model, const Vec3& p, u32* x, u32* y) const {
        const Vec4 clip = view_projection * (model * Vec4(p.x, p.y, p.z, 1.0f));
        REQUIRE(clip.w > 0.0f); // in front of the camera
        const f32 px = (clip.x / clip.w + 1.0f) * 0.5f * size;
        const f32 py = (clip.y / clip.w + 1.0f) * 0.5f * size;
        REQUIRE((px >= 0.0f && px < size && py >= 0.0f && py < size)); // on screen
        *x = static_cast<u32>(px);
        *y = static_cast<u32>(py);
    }
};

inline bool PixelIs(const Image& image, u32 x, u32 y, int r, int g, int b) {
    const unsigned char* p = image.Pixel(x, y);
    return std::abs(p[0] - r) <= 2 && std::abs(p[1] - g) <= 2 && std::abs(p[2] - b) <= 2;
}



} // namespace cube_test
