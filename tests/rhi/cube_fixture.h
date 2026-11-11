#pragma once

#include "lang/scalar.h"
#include "cstr/format.h"
#include "lang/initializer_list.h"

#include <catch2/catch_test_macros.hpp>

#include "backend_fixture.h"
#include "containers/unique_ptr.h"
#include "containers/vector.h"
#include "cube_scene.h"
#include "image_compare.h"
#include "render_target.h"
#include "rhi/rhi.h"
#include "rhi/uniform_ring.h"
#include "os/console.h"

// The scene of the cube golden tests: a device on one backend, an offscreen target with depth, a uniform ring and a
// camera (Phase 13 Day 170: any backend, through the RHI interface).
namespace cube_test
{

using namespace engine_placeholder;
using namespace engine_placeholder::rhi;
using gpu_test::Image;

struct Draw
{
    demo::CubeRenderer* cube;
    Mat4 model;
    const demo::Lighting* lighting = nullptr;  // set for a lit renderer
};

struct CubeScene
{
    u32 size;
    gpu_test::BackendValidation validation;  // before the device, so its creation's messages count
    DevicePtr device;
    UniquePtr<gpu_test::RenderTarget> target;
    TextureHandle depth;
    UniquePtr<UniformRing> ring;
    Mat4 view_projection;

    explicit CubeScene(Backend backend, const Vec3& eye = Vec3(0, 0, 3), u32 target_size = 128)
        : size(target_size), validation(backend)
    {
        device = gpu_test::CreateTestDevice(backend);
        target = MakeUnique<gpu_test::RenderTarget>(device.Get(), size, size);
        TextureDesc td;
        td.width = size;
        td.height = size;
        td.format = device->Caps().depth_format;
        td.usage = TextureUsage::DepthAttachment;
        depth = device->CreateTexture(td);
        ring = MakeUnique<UniformRing>(device.Get(), 4096);
        SetCamera(eye);
    }
    ~CubeScene()
    {
        if (!device)
        {
            return;
        }
        device->WaitIdle();
        ring.Reset();
        if (depth.IsValid())
        {
            device->DestroyTexture(depth);
        }
        target.Reset();
        device.Reset();
    }

    void SetCamera(const Vec3& eye)
    {
        view_projection = demo::PerspectiveVulkan(0.7853982f, 1.0f, 0.1f, 10.0f) *
                          demo::LookAt(eye, Vec3(0, 0, 0), Vec3(0, 1, 0));
    }

    Format DepthFormat() const { return device->Caps().depth_format; }

    Image Render(InitializerList<Draw> draws)
    {
        Vector<Draw> list;
        for (const Draw& d : draws)
        {
            list.Push(d);
        }
        return Render(list);
    }

    Image Render(const Vector<Draw>& draws)
    {
        REQUIRE(target->Valid());
        REQUIRE(depth.IsValid());
        REQUIRE(ring->Valid());
        device->BeginFrame();
        ring->BeginFrame();
        CommandList* list = device->CreateCommandList();
        list->Begin();
        for (usize i = 0; i < draws.Size(); ++i)
        {
            bool seen = false;
            for (usize j = 0; j < i; ++j)
            {
                seen = seen || draws[j].cube == draws[i].cube;
            }
            if (!seen)
            {
                draws[i].cube->Prepare(list);
            }
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
        for (usize i = 0; i < draws.Size(); ++i)
        {
            const Draw& d = draws[i];
            if (d.lighting != nullptr)
            {
                REQUIRE(d.cube->DrawLit(list, ring.Get(), view_projection, d.model, *d.lighting, size, size));
            }
            else
            {
                REQUIRE(d.cube->Draw(list, ring.Get(), view_projection * d.model, size, size));
            }
        }
        list->EndRendering();
        list->End();
        ring->Flush();
        CommandList* lists[] = {list};
        const bool submitted = device->Submit(lists, 1) != 0;
        device->EndFrame();
        REQUIRE(submitted);
        for (usize i = 0; i < draws.Size(); ++i)
        {
            draws[i].cube->Submitted();
        }
        device->WaitIdle();
        Image image;
        REQUIRE(target->Readback(&image));
        return image;
    }

    // The pixel the view-projection puts the model-space point `p` (through `model`) on.
    void Project(const Mat4& model, const Vec3& p, u32* x, u32* y) const
    {
        const Vec4 clip = view_projection * (model * Vec4(p.x, p.y, p.z, 1.0f));
        REQUIRE(clip.w > 0.0f);  // in front of the camera
        const f32 px = (clip.x / clip.w + 1.0f) * 0.5f * size;
        const f32 py = (clip.y / clip.w + 1.0f) * 0.5f * size;
        REQUIRE((px >= 0.0f && px < size && py >= 0.0f && py < size));  // on screen
        *x = static_cast<u32>(px);
        *y = static_cast<u32>(py);
    }
};

inline bool PixelIs(const Image& image, u32 x, u32 y, int r, int g, int b)
{
    const u8* p = image.Pixel(x, y);
    return Abs(p[0] - r) <= 2 && Abs(p[1] - g) <= 2 && Abs(p[2] - b) <= 2;
}

// Compares a rendered image with a committed golden (or rewrites the golden under ENGINE_RHI_UPDATE_GOLDEN=1), naming
// the backend. Returns the largest per-channel difference from the golden (0 when updating).
inline int CheckGolden(const Image& image, const char* golden_file, const char* name, Backend backend,
                       const gpu_test::CompareOptions& options = gpu_test::CompareOptions{})
{
    if (gpu_test::UpdatingGoldens())
    {
        // Only Vulkan writes goldens: the images were blessed through it, and Direct3D 12 is checked against them
        // (decision 11).
        if (backend == Backend::Vulkan)
        {
            REQUIRE(gpu_test::WritePpm(gpu_test::GoldenPath(golden_file).CStr(), image));
            WARN("ENGINE_RHI_UPDATE_GOLDEN=1: tests/rhi/golden/" << golden_file
                                                                 << " was rewritten; inspect it before committing");
        }
        return 0;
    }
    Image golden;
    REQUIRE(gpu_test::ReadPpm(gpu_test::GoldenPath(golden_file).CStr(), &golden));
    char diff_name[96];
    FormatTo(diff_name, sizeof(diff_name), "%s_%s", name, gpu_test::BackendTag(backend));
    const gpu_test::CompareResult result = gpu_test::Compare(golden, image, options, diff_name);
    INFO(gpu_test::BackendTag(backend) << ": " << result.message.CStr());
    CHECK(result.same);
    // ENGINE_RHI_GOLDEN_REPORT=1: say how far the image is from the golden and from the other backend's image of the
    // same test (both run in one process), which separates a driver's differences from a backend's.
    const char* report = os::GetEnvironment("ENGINE_RHI_GOLDEN_REPORT");
    if (report != nullptr && report[0] == '1')
    {
        char own[96], other[96];
        FormatTo(own, sizeof(own), "golden_%s_%s.ppm", name, gpu_test::BackendTag(backend));
        FormatTo(other, sizeof(other), "golden_%s_%s.ppm", name, backend == Backend::Vulkan ? "dx12" : "vulkan");
        gpu_test::WritePpm(gpu_test::TempPath(own).CStr(), image);
        int between = -1;
        Image theirs;
        if (gpu_test::ReadPpm(gpu_test::TempPath(other).CStr(), &theirs) && theirs.width == image.width &&
            theirs.height == image.height)
        {
            between = 0;
            for (usize i = 0; i < image.rgba.Size(); ++i)
            {
                const int d = Abs(static_cast<int>(image.rgba[i]) - static_cast<int>(theirs.rgba[i]));
                between = d > between ? d : between;
            }
        }
        if (between >= 0)
        {
            WARN(name << " on " << gpu_test::BackendTag(backend) << ": largest difference " << result.max_difference
                      << " from the golden (" << result.interior_misses << " interior pixels over the tolerance), "
                      << between << " from the other backend");
        }
        else
        {
            WARN(name << " on " << gpu_test::BackendTag(backend) << ": largest difference " << result.max_difference
                      << " from the golden (" << result.interior_misses << " interior pixels over the tolerance)");
        }
    }
    return result.max_difference;
}

}  // namespace cube_test
