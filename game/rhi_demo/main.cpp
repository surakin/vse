// rhi_demo: opens a window, creates a Vulkan device and swapchain, and draws a red-green-blue triangle over a
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
#include <vector>

#include "os/input.h"
#include "os/linux/platform_window_host.h"
#include "rhi/rhi.h"
#include "rhi/rhi_log.h"
#include "rhi/vulkan/vulkan_device.h"
#include "triangle.frag.spv.inc"
#include "triangle.vert.spv.inc"

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

// The triangle: shaders, pipeline and a device-local vertex buffer filled through the staging path.
class Triangle {
public:
    bool Create(Device* device, Format target_format) {
        device_ = device;
        ShaderDesc sd;
        sd.stage = ShaderStage::Vertex;
        sd.words = shaders::kTriangleVertSpv;
        sd.word_count = shaders::kTriangleVertSpvWordCount;
        vs_ = device->CreateShader(sd);
        sd.stage = ShaderStage::Fragment;
        sd.words = shaders::kTriangleFragSpv;
        sd.word_count = shaders::kTriangleFragSpvWordCount;
        fs_ = device->CreateShader(sd);

        VertexBindingDesc binding = {20, false}; // 2 floats of position, 3 of color
        VertexAttributeDesc attributes[2] = {{0, 0, VertexFormat::Float2, 0}, {1, 0, VertexFormat::Float3, 8}};
        GraphicsPipelineDesc gp;
        gp.vertex_shader = vs_;
        gp.fragment_shader = fs_;
        gp.vertex_bindings = Span<VertexBindingDesc>(&binding, 1);
        gp.vertex_attributes = Span<VertexAttributeDesc>(attributes, 2);
        gp.cull_mode = CullMode::None;
        gp.color_targets[0].format = target_format;
        gp.color_target_count = 1;
        gp.debug_name = "triangle";
        pipeline_ = device->CreateGraphicsPipeline(gp);

        const f32 vertices[3 * 5] = {
            0.0f,  -0.8f, 1.0f, 0.0f, 0.0f, // top, red
            0.8f,  0.8f,  0.0f, 1.0f, 0.0f, // bottom right, green
            -0.8f, 0.8f,  0.0f, 0.0f, 1.0f, // bottom left, blue
        };
        BufferDesc bd;
        bd.size = sizeof(vertices);
        bd.usage = BufferUsage::Vertex | BufferUsage::CopyDst;
        bd.location = MemoryLocation::GpuOnly;
        bd.debug_name = "triangle vertices";
        vb_ = device->CreateBuffer(bd);
        if (vs_.IsValid() && fs_.IsValid() && pipeline_.IsValid() && vb_.IsValid() && device->UploadBuffer(vb_, 0, vertices, sizeof(vertices)).ok) {
            return true;
        }
        Destroy(); // a partial setup cleans up after itself
        return false;
    }

    // Destroys whatever exists; safe on a triangle that was never created or only half created. Idle the GPU first.
    void Destroy() {
        if (device_ == nullptr) {
            return;
        }
        if (vb_.IsValid()) device_->DestroyBuffer(vb_);
        if (pipeline_.IsValid()) device_->DestroyPipeline(pipeline_);
        if (vs_.IsValid()) device_->DestroyShader(vs_);
        if (fs_.IsValid()) device_->DestroyShader(fs_);
        vb_ = {};
        pipeline_ = {};
        vs_ = {};
        fs_ = {};
        device_ = nullptr;
    }

    // Until a frame carrying it has been submitted, the vertex buffer is still in the upload's `CopyDst` state: record
    // the move to `VertexBuffer` in every frame's list before `BeginRendering`, and call `Submitted` once a list that
    // holds it was handed to the device.
    void PrepareFrame(CommandList* list) {
        if (!vertex_ready_) {
            list->Barrier(vb_, ResourceState::CopyDst, ResourceState::VertexBuffer);
        }
    }
    void Submitted() { vertex_ready_ = true; }
    void Draw(CommandList* list, u32 width, u32 height) {
        list->SetPipeline(pipeline_);
        list->SetViewport(Viewport{0, 0, static_cast<f32>(width), static_cast<f32>(height), 0.0f, 1.0f});
        list->SetScissor(ScissorRect{0, 0, width, height});
        list->SetVertexBuffer(0, vb_, 0);
        list->Draw(3, 1, 0, 0);
    }

private:
    Device* device_ = nullptr;
    ShaderHandle vs_;
    ShaderHandle fs_;
    PipelineHandle pipeline_;
    BufferHandle vb_;
    bool vertex_ready_ = false;
};

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
    Triangle triangle;
    int exit_code = 1;
    if (target.IsValid() && triangle.Create(device, td.format)) {
        for (long i = 0; i < (frames > 0 ? frames : 1); ++i) {
            device->BeginFrame();
            CommandList* list = device->CreateCommandList();
            list->Begin();
            triangle.PrepareFrame(list);
            list->Barrier(target, ResourceState::Undefined, ResourceState::ColorAttachment);
            RenderingDesc pass;
            pass.color_count = 1;
            pass.color[0].texture = target;
            pass.color[0].clear_color[0] = pass.color[0].clear_color[1] = pass.color[0].clear_color[2] = 0.2f;
            list->BeginRendering(pass);
            triangle.Draw(list, kSize, kSize);
            list->EndRendering();
            list->End();
            CommandList* lists[] = {list};
            if (device->Submit(lists, 1) != 0) {
                triangle.Submitted();
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
        RhiLogV(LogVerbosity::Error, "rhi_demo: could not create the target or the triangle");
    }
    device->WaitIdle();
    triangle.Destroy(); // does nothing if Create failed
    if (target.IsValid()) {
        device->DestroyTexture(target);
    }
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

    Triangle triangle;
    if (!triangle.Create(device, swapchain->ImageFormat())) {
        RhiLogV(LogVerbosity::Error, "rhi_demo: could not create the triangle");
        device->WaitIdle();
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
        CommandList* list = device->CreateCommandList();
        list->Begin();
        triangle.PrepareFrame(list);
        list->Barrier(image, ResourceState::Undefined, ResourceState::ColorAttachment);
        RenderingDesc pass;
        pass.color_count = 1;
        pass.color[0].texture = image;
        pass.color[0].load = LoadOp::Clear;
        ColorAt(NowSeconds() - start, pass.color[0].clear_color);
        list->BeginRendering(pass);
        triangle.Draw(list, swapchain->Width(), swapchain->Height());
        list->EndRendering();
        list->Barrier(image, ResourceState::ColorAttachment, ResourceState::Present);
        list->End();
        CommandList* lists[] = {list};
        if (device->Submit(lists, 1) != 0) {
            triangle.Submitted();
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
    triangle.Destroy();
    device->DestroySwapchain(swapchain);
    DestroyDevice(device);
    return exit_code;
}
