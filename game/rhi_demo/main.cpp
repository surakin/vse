// rhi_demo: opens a window, creates a Vulkan device and swapchain, and draws a spinning, depth-tested cube over a
// background color that animates with time. Closes when the window does; logs the average frame time once a second.
//
// Options: --frames N stops after N frames (RHI_DEMO_FRAMES=N does the same); --screenshot out.ppm renders offscreen
// without a window, N frames (at least one), writes the last one as a binary PPM and exits.
// Environment: ENGINE_WINDOW_BACKEND picks the display backend (see the platform layer); the ENGINE_RHI_VALIDATION*
// variables control the validation layer.

#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <memory>
#include <vector>

#include "os/input.h"
#include "os/linux/platform_window_host.h"
#include "rhi/rhi.h"
#include "rhi/rhi_log.h"
#include "rhi/vulkan/vulkan_device.h"
#include "cube_scene.h"
#include "rhi/uniform_ring.h"

using namespace engine_placeholder;
using namespace engine_placeholder::rhi;

namespace {

double NowSeconds() {
    timespec ts{};
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return static_cast<double>(ts.tv_sec) + static_cast<double>(ts.tv_nsec) * 1e-9;
}

// The clear color at time `t`: three out-of-phase sine waves.
void ColorAt(double t, f32 out[4]) {
    out[0] = static_cast<f32>(0.5 + 0.5 * std::sin(t * 1.0));
    out[1] = static_cast<f32>(0.5 + 0.5 * std::sin(t * 1.3 + 2.0));
    out[2] = static_cast<f32>(0.5 + 0.5 * std::sin(t * 0.7 + 4.0));
    out[3] = 1.0f;
}

// A depth texture that follows the size of what it is used with.
class DepthBuffer {
public:
    // Makes sure a depth texture of `width` x `height` exists, replacing a differently sized one (the old one is
    // destroyed by the device once the GPU has finished with it). Returns false if it could not be created.
    bool Ensure(Device* device, u32 width, u32 height) {
        if (texture_.IsValid() && width == width_ && height == height_) {
            return true;
        }
        Destroy(device);
        TextureDesc td;
        td.width = width;
        td.height = height;
        td.format = device->Caps().depth_format;
        td.usage = TextureUsage::DepthAttachment;
        td.debug_name = "depth";
        texture_ = device->CreateTexture(td);
        width_ = width;
        height_ = height;
        return texture_.IsValid();
    }
    void Destroy(Device* device) {
        if (texture_.IsValid()) {
            device->DestroyTexture(texture_);
            texture_ = {};
        }
    }
    TextureHandle Texture() const { return texture_; }

private:
    TextureHandle texture_;
    u32 width_ = 0;
    u32 height_ = 0;
};

// Records the whole frame into `color`: clear, one cube, and `final_state` for the color image. `yaw` spins the cube.
bool RecordFrame(CommandList* list, demo::CubeRenderer* cube, UniformRing* ring, TextureHandle color, TextureHandle depth, u32 width,
                 u32 height, const f32 background[4], f32 yaw, ResourceState final_state) {
    cube->Prepare(list);
    list->Barrier(color, ResourceState::Undefined, ResourceState::ColorAttachment);
    list->Barrier(depth, ResourceState::Undefined, ResourceState::DepthAttachment);
    RenderingDesc pass;
    pass.color_count = 1;
    pass.color[0].texture = color;
    for (int i = 0; i < 4; ++i) {
        pass.color[0].clear_color[i] = background[i];
    }
    pass.depth.texture = depth;
    pass.depth.store = StoreOp::DontCare; // read by nothing after the pass
    list->BeginRendering(pass);
    const Mat4 projection = demo::PerspectiveVulkan(0.7853982f, static_cast<f32>(width) / static_cast<f32>(height), 0.1f, 10.0f);
    const Mat4 view = demo::LookAt(Vec3(0.0f, 1.2f, 3.0f), Vec3(0, 0, 0), Vec3(0, 1, 0));
    const bool drawn = cube->Draw(list, ring, projection * view * demo::ModelMatrix(Vec3(0, 0, 0), 1.0f, yaw), width, height);
    list->EndRendering();
    if (final_state != ResourceState::ColorAttachment) {
        list->Barrier(color, ResourceState::ColorAttachment, final_state);
    }
    return drawn;
}

bool WritePpm(const char* path, u32 width, u32 height, const std::vector<unsigned char>& rgba) {
    std::FILE* f = std::fopen(path, "wb");
    if (f == nullptr) {
        return false;
    }
    std::fprintf(f, "P6\n%u %u\n255\n", width, height);
    bool ok = true;
    for (usize i = 0; i < static_cast<usize>(width) * height && ok; ++i) {
        ok = std::fwrite(&rgba[i * 4], 1, 3, f) == 3;
    }
    return std::fclose(f) == 0 && ok;
}

// Renders `frames` frames offscreen (no window) and saves the last one.
int RunScreenshot(const char* path, long frames) {
    constexpr u32 kSize = 256;
    DeviceCreateInfo info;
    vulkan::VulkanDevice* device = vulkan::VulkanDevice::Create(info);
    if (device == nullptr) {
        RhiLogV(LogVerbosity::Error, "rhi_demo: no Vulkan device");
        return 1;
    }
    TextureDesc td;
    td.width = kSize;
    td.height = kSize;
    td.format = Format::RGBA8_UNorm;
    td.usage = TextureUsage::ColorAttachment | TextureUsage::CopySrc;
    const TextureHandle target = device->CreateTexture(td);
    DepthBuffer depth;
    demo::CubeRenderer cube;
    std::unique_ptr<UniformRing> ring = std::make_unique<UniformRing>(device, 4096);
    int exit_code = 1;
    demo::CubeOptions options;
    options.color_format = td.format;
    options.depth_format = device->Caps().depth_format;
    if (target.IsValid() && depth.Ensure(device, kSize, kSize) && ring->Valid() && cube.Create(device, options)) {
        const f32 background[4] = {0.2f, 0.2f, 0.2f, 1.0f};
        for (long i = 0; i < (frames > 0 ? frames : 1); ++i) {
            device->BeginFrame();
            ring->BeginFrame();
            CommandList* list = device->CreateCommandList();
            list->Begin();
            if (!RecordFrame(list, &cube, ring.get(), target, depth.Texture(), kSize, kSize, background, 0.6f, ResourceState::ColorAttachment)) {
                RhiLogV(LogVerbosity::Error, "rhi_demo: the cube could not be drawn");
                exit_code = 1;
            }
            list->End();
            ring->Flush();
            CommandList* lists[] = {list};
            if (device->Submit(lists, 1) != 0) {
                cube.Submitted();
            }
            device->EndFrame();
        }
        std::vector<unsigned char> pixels(static_cast<usize>(kSize) * kSize * 4);
        if (!device->ReadbackTexture(target, 0, 0, pixels.data(), pixels.size(), ResourceState::ColorAttachment)) {
            RhiLogV(LogVerbosity::Error, "rhi_demo: could not read the image back");
        } else if (!WritePpm(path, kSize, kSize, pixels)) {
            RhiLogV(LogVerbosity::Error, "rhi_demo: could not write %s", path);
        } else {
            RhiLogV(LogVerbosity::Info, "rhi_demo: wrote %s", path);
            exit_code = 0;
        }
    } else {
        RhiLogV(LogVerbosity::Error, "rhi_demo: could not create the target or the cube");
    }
    device->WaitIdle();
    cube.Destroy(); // does nothing if Create failed
    depth.Destroy(device);
    if (target.IsValid()) {
        device->DestroyTexture(target);
    }
    ring.reset(); // the ring owns a buffer: it goes before the device
    DestroyDevice(device);
    return exit_code;
}

} // namespace

int main(int argc, char** argv) {
    LogRhi.SetVerbosity(LogVerbosity::Info); // the frame-time line is Info
    const char* limit_text = std::getenv("RHI_DEMO_FRAMES");
    long frame_limit = limit_text != nullptr ? std::atol(limit_text) : 0;
    const char* screenshot = nullptr;
    for (int i = 1; i < argc; ++i) {
        if (std::strcmp(argv[i], "--frames") == 0 && i + 1 < argc) {
            frame_limit = std::atol(argv[++i]);
        } else if (std::strcmp(argv[i], "--screenshot") == 0 && i + 1 < argc) {
            screenshot = argv[++i];
        } else {
            std::fprintf(stderr, "usage: rhi_demo [--frames N] [--screenshot out.ppm]\n");
            return 2;
        }
    }
    if (screenshot != nullptr) {
        return RunScreenshot(screenshot, frame_limit);
    }
    os::InputState input;
    os::PlatformWindowHost host;
    if (!host.OpenFromEnvironment("rhi_demo", 960, 540, &input)) {
        RhiLogV(LogVerbosity::Error, "rhi_demo: could not open a window");
        return 1;
    }
    os::Window* window = host.Get();

    DeviceCreateInfo info;
    info.window = window;
    vulkan::VulkanDevice* device = vulkan::VulkanDevice::Create(info);
    if (device == nullptr) {
        RhiLogV(LogVerbosity::Error, "rhi_demo: no Vulkan device can present to this window");
        return 1;
    }
    Swapchain* swapchain = device->CreateSwapchain(SwapchainDesc{});
    if (swapchain == nullptr) {
        RhiLogV(LogVerbosity::Error, "rhi_demo: could not create a swapchain");
        DestroyDevice(device);
        return 1;
    }

    demo::CubeRenderer cube;
    demo::CubeOptions options;
    options.color_format = swapchain->ImageFormat();
    options.depth_format = device->Caps().depth_format;
    std::unique_ptr<UniformRing> ring = std::make_unique<UniformRing>(device, 4096);
    DepthBuffer depth;
    if (!ring->Valid() || !cube.Create(device, options)) {
        RhiLogV(LogVerbosity::Error, "rhi_demo: could not create the cube");
        device->WaitIdle();
        ring.reset();
        device->DestroySwapchain(swapchain);
        DestroyDevice(device);
        return 1;
    }

    const double start = NowSeconds();
    double window_start = start;
    u64 window_frames = 0;
    long frames = 0;
    int exit_code = 0;
    while (!window->ShouldClose() && (frame_limit <= 0 || frames < frame_limit)) {
        window->PumpEvents();
        if (window->IsMinimized()) {
            // Nothing to draw to; wait for the window to come back without spinning.
            timespec nap{0, 10 * 1000 * 1000};
            nanosleep(&nap, nullptr);
            continue;
        }
        device->BeginFrame();
        const SwapchainStatus acquired = swapchain->Acquire();
        if (acquired == SwapchainStatus::Lost) {
            RhiLogV(LogVerbosity::Error, "rhi_demo: the swapchain was lost");
            device->EndFrame();
            exit_code = 1;
            break;
        }
        if (acquired == SwapchainStatus::Minimized) {
            device->EndFrame();
            ++frames; // an unpresentable surface still counts toward RHI_DEMO_FRAMES, so automation terminates
            timespec nap{0, 10 * 1000 * 1000};
            nanosleep(&nap, nullptr);
            continue;
        }
        const TextureHandle image = swapchain->CurrentTexture();
        if (!depth.Ensure(device, swapchain->Width(), swapchain->Height())) { // follows a resize
            RhiLogV(LogVerbosity::Error, "rhi_demo: could not create the depth buffer");
            device->EndFrame();
            exit_code = 1;
            break;
        }
        ring->BeginFrame();
        CommandList* list = device->CreateCommandList();
        list->Begin();
        f32 background[4];
        ColorAt(NowSeconds() - start, background);
        for (f32& c : background) {
            c *= 0.4f; // darker, so the cube stands out
        }
        background[3] = 1.0f;
        if (!RecordFrame(list, &cube, ring.get(), image, depth.Texture(), swapchain->Width(), swapchain->Height(), background,
                         static_cast<f32>(NowSeconds() - start), ResourceState::Present)) {
            RhiLogV(LogVerbosity::Error, "rhi_demo: the cube could not be drawn");
            exit_code = 1;
        }
        list->End();
        ring->Flush();
        CommandList* lists[] = {list};
        if (device->Submit(lists, 1) != 0) {
            cube.Submitted();
        }
        device->EndFrame();
        if (swapchain->Present() == SwapchainStatus::Lost) {
            RhiLogV(LogVerbosity::Error, "rhi_demo: the swapchain was lost while presenting");
            exit_code = 1;
            break;
        }
        ++frames;
        ++window_frames;
        const double now = NowSeconds();
        if (now - window_start >= 1.0) {
            RhiLogV(LogVerbosity::Info, "rhi_demo: %llu frames, %.3f ms/frame", static_cast<unsigned long long>(window_frames),
                    (now - window_start) * 1000.0 / static_cast<double>(window_frames));
            window_start = now;
            window_frames = 0;
        }
    }

    // Shutdown order: idle the GPU, then the swapchain, then the device.
    device->WaitIdle();
    cube.Destroy();
    depth.Destroy(device);
    ring.reset();
    device->DestroySwapchain(swapchain);
    DestroyDevice(device);
    return exit_code;
}
