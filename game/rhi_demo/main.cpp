// rhi_demo: opens a window, creates a Vulkan device and swapchain, and clears the swapchain image to a color
// that animates with time. Closes when the window does; logs the average frame time once a second.
//
// Environment: ENGINE_WINDOW_BACKEND picks the display backend (see the platform layer); RHI_DEMO_FRAMES=N stops
// after N frames (for automation); the ENGINE_RHI_VALIDATION* variables control the validation layer.

#include <cmath>
#include <cstdlib>
#include <ctime>

#include "os/input.h"
#include "os/linux/platform_window_host.h"
#include "rhi/rhi.h"
#include "rhi/rhi_log.h"
#include "rhi/vulkan/vulkan_device.h"

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

} // namespace

int main() {
    LogRhi.SetVerbosity(LogVerbosity::Info); // the frame-time line is Info
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

    const char* limit_text = std::getenv("RHI_DEMO_FRAMES");
    const long frame_limit = limit_text != nullptr ? std::atol(limit_text) : 0;

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
        CommandList* list = device->CreateCommandList();
        list->Begin();
        list->Barrier(image, ResourceState::Undefined, ResourceState::ColorAttachment);
        RenderingDesc pass;
        pass.color_count = 1;
        pass.color[0].texture = image;
        pass.color[0].load = LoadOp::Clear;
        ColorAt(NowSeconds() - start, pass.color[0].clear_color);
        list->BeginRendering(pass);
        list->EndRendering();
        list->Barrier(image, ResourceState::ColorAttachment, ResourceState::Present);
        list->End();
        CommandList* lists[] = {list};
        device->Submit(lists, 1);
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
    device->DestroySwapchain(swapchain);
    DestroyDevice(device);
    return exit_code;
}
