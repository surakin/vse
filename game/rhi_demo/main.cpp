// rhi_demo: opens a window, creates a device (Vulkan or Direct3D 12: the `renderer.backend` setting) and swapchain, and
// draws a textured, depth-tested cube lit by a light that orbits it. Arrow keys or dragging with the left mouse button
// orbit the camera, the wheel zooms. Closes when the window does; logs the average frame time once a second. See
// README.md for the options.

#include "lang/scalar.h"

#include "os/console.h"
#include "cstr/cstr.h"

#include "io/file.h"
#include "io/file_system.h"
#include "debug/program_setup.h"
#include "cube_scene.h"
#include "os/os.h"
#include "platform/entry_point.h"
#include "platform/input.h"
#include "platform/window_host.h"
#include "rhi/backend_registry.h"
#include "rhi/backends/backends.h"
#include "rhi/rhi.h"
#include "rhi/rhi_log.h"
#include "rhi/uniform_ring.h"
#include "settings_main.h"
#include "textures.h"
#include "cstr/format.h"
#include "cstr/number.h"

using namespace engine_placeholder;
using namespace engine_placeholder::rhi;

namespace
{

constexpr int kExitOk = 0;
constexpr int kExitFailure = 1;  // the device, window, swapchain or a draw failed, or memory grew
constexpr int kExitUsage = 2;
constexpr int kExitDeviceLost = 3;  // the GPU device was lost; the demo stopped cleanly

// If memory grows by more than this between frame 100 and the last frame of a run of at least kMemoryCheckFrames
// frames, the run fails: it points at a per-frame leak.
constexpr long kMemoryCheckStart = 100;
constexpr long kMemoryCheckFrames = 200;
constexpr long kMemoryGrowthLimitKiB = 8 * 1024;

struct Options
{
    long frames = 0;                   // 0: until the window closes
    const char* screenshot = nullptr;  // render offscreen to this PPM and exit
    const char*
        backend = "auto";  // the display backend: auto, wayland, x11 or win32 (the setting platform.window_backend)
    bool no_vsync = false;
    bool validation = false;
    long inject_device_loss = 0;  // lose the device at this frame (fault injection), 0 = never
    long inject_oom = 0;          // fail device-memory allocations from the Nth on (fault injection), 0 = never
    const char* gpu = nullptr;    // --gpu: an adapter id, an index or part of a name
    bool list_gpus = false;       // --list-gpus
};

double NowSeconds()
{
    return static_cast<double>(os::MonotonicTimeNanos()) * 1e-9;
}

// The clear color at time `t`: three out-of-phase sine waves, darkened so the cube stands out.
void BackgroundAt(double t, f32 out[4])
{
    out[0] = static_cast<f32>(0.5 + 0.5 * Sin(t * 1.0)) * 0.3f;
    out[1] = static_cast<f32>(0.5 + 0.5 * Sin(t * 1.3 + 2.0)) * 0.3f;
    out[2] = static_cast<f32>(0.5 + 0.5 * Sin(t * 0.7 + 4.0)) * 0.3f;
    out[3] = 1.0f;
}

// An orbit camera around the origin.
struct OrbitCamera
{
    f32 yaw = 0.6f;
    f32 pitch = 0.35f;
    f32 distance = 3.2f;

    void Rotate(f32 dyaw, f32 dpitch)
    {
        yaw += dyaw;
        pitch += dpitch;
        pitch = pitch < -1.4f ? -1.4f : (pitch > 1.4f ? 1.4f : pitch);  // stay off the poles
    }
    void Zoom(f32 delta)
    {
        distance += delta;
        distance = distance < 1.6f ? 1.6f : (distance > 8.0f ? 8.0f : distance);
    }
    Mat4 View() const
    {
        const Vec3 eye(distance * Cos(pitch) * Sin(yaw), distance * Sin(pitch), distance * Cos(pitch) * Cos(yaw));
        return demo::LookAt(eye, Vec3(0, 0, 0), Vec3(0, 1, 0));
    }
};

// The light circles the cube once every 8 seconds, above the equator.
demo::Lighting LightAt(double t)
{
    demo::Lighting lighting;
    lighting.to_light = Vec3(static_cast<f32>(Cos(t * 0.8)), 0.6f, static_cast<f32>(Sin(t * 0.8)));
    lighting.intensity = 0.9f;
    lighting.ambient = Vec3(0.12f, 0.12f, 0.14f);
    return lighting;
}

// The cube's texture and sampler: the checkerboard from assets/checker.rtex (every mip level stored in the file), or
// a procedural one with GPU-generated mips if the file is missing or unusable.
struct CubeTexture
{
    TextureHandle texture;
    SamplerHandle sampler;

    bool Create(Device* device)
    {
        const io::Path asset = io::Path(StringView(RHI_DEMO_ASSET_DIR)).Join("checker.rtex");
        demo::RtexImage image;
        if (demo::ReadRtex(asset, &image).Ok())
        {
            texture = demo::CreateTextureFromRtex(device, image, "checker.rtex");
        }
        if (!texture.IsValid())
        {
            RhiLogV(LogVerbosity::Warn, "rhi_demo: %.*s could not be loaded; using a procedural checkerboard",
                    static_cast<int>(asset.View().Length()), asset.View().Data());
            texture = demo::CreateSampledTexture(device,
                                                 demo::Checkerboard(256, 32, {235, 200, 90, 255}, {40, 60, 120, 255}),
                                                 Format::RGBA8_SRGB, true, "checker");
        }
        SamplerDesc sd;  // trilinear, repeat
        sd.max_anisotropy = 16.0f;
        sd.debug_name = "cube sampler";
        sampler = device->CreateSampler(sd);
        return texture.IsValid() && sampler.IsValid();
    }
    void Destroy(Device* device)
    {
        if (sampler.IsValid())
        {
            device->DestroySampler(sampler);
        }
        if (texture.IsValid())
        {
            device->DestroyTexture(texture);
        }
        sampler = {};
        texture = {};
    }
};

// A depth texture that follows the size of what it is used with.
class DepthBuffer
{
public:
    // Makes sure a depth texture of `width` x `height` exists, replacing a differently sized one (the old one is
    // destroyed by the device once the GPU has finished with it). Returns false if it could not be created.
    bool Ensure(Device* device, u32 width, u32 height)
    {
        if (texture_.IsValid() && width == width_ && height == height_)
        {
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
    void Destroy(Device* device)
    {
        if (texture_.IsValid())
        {
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

// Everything the demo creates besides the device and the swapchain.
struct Scene
{
    demo::CubeRenderer cube;
    CubeTexture texture;
    DepthBuffer depth;
    UniquePtr<UniformRing> ring;

    bool Create(Device* device, Format color_format)
    {
        demo::CubeOptions options;
        options.color_format = color_format;
        options.depth_format = device->Caps().depth_format;
        options.lit = true;
        ring = MakeUnique<UniformRing>(device, 4096);
        if (!ring->Valid() || !texture.Create(device) || !cube.Create(device, options))
        {
            return false;
        }
        cube.SetTexture(texture.texture, texture.sampler);
        return true;
    }
    // Idle the GPU first.
    void Destroy(Device* device)
    {
        cube.Destroy();
        texture.Destroy(device);
        depth.Destroy(device);
        ring.Reset();  // the ring owns a buffer: it goes before the device
    }
};

// Records the whole frame into `color`: clear, the lit cube, and `final_state` for the color image.
bool RecordFrame(CommandList* list, Scene* scene, TextureHandle color, u32 width, u32 height, const f32 background[4],
                 const OrbitCamera& camera, const demo::Lighting& lighting, ResourceState final_state)
{
    scene->cube.Prepare(list);
    list->Barrier(color, ResourceState::Undefined, ResourceState::ColorAttachment);
    list->Barrier(scene->depth.Texture(), ResourceState::Undefined, ResourceState::DepthAttachment);
    RenderingDesc pass;
    pass.color_count = 1;
    pass.color[0].texture = color;
    for (int i = 0; i < 4; ++i)
    {
        pass.color[0].clear_color[i] = background[i];
    }
    pass.depth.texture = scene->depth.Texture();
    pass.depth.store = StoreOp::DontCare;  // read by nothing after the pass
    list->BeginRendering(pass);
    const Mat4 projection = demo::PerspectiveVulkan(0.7853982f, static_cast<f32>(width) / static_cast<f32>(height),
                                                    0.1f, 20.0f);
    const bool drawn = scene->cube.DrawLit(list, scene->ring.Get(), projection * camera.View(), Mat4::Identity(),
                                           lighting, width, height);
    list->EndRendering();
    if (final_state != ResourceState::ColorAttachment)
    {
        list->Barrier(color, ResourceState::ColorAttachment, final_state);
    }
    return drawn;
}

bool WritePpm(const char* path, u32 width, u32 height, const Vector<byte>& rgba)
{
    io::Path file;
    return io::Path::Parse(StringView(path), io::NativePathStyle(), &file) &&
           demo::WritePpm(file, width, height, rgba.Data()).Ok();
}

demo::DemoSettings* g_demo_settings = nullptr;  // the settings of this run

DeviceCreateInfo DeviceInfo(const Options& options)
{
    DeviceCreateInfo info;
    g_demo_settings->FillDeviceCreateInfo(&info);  // the backend, the validation layer, the GPU, the pipeline cache and
                                                   // the frames in flight come from the settings
    info.faults.lose_device_at_frame = static_cast<u32>(options.inject_device_loss);
    info.faults.fail_allocations_from = static_cast<u32>(options.inject_oom);
    return info;
}

// Prints the GPUs a device of the chosen backend can be made on, one per line, and returns the exit code.
int ListGpus()
{
    DeviceCreateInfo info;
    g_demo_settings->FillDeviceCreateInfo(&info);  // the backend
    char why[256] = {};
    const BackendDescriptor* backend = ResolveBackend(info.backend, why, sizeof(why));
    if (backend == nullptr)
    {
        os::PrintError("rhi_demo: cannot list the GPUs: %s\n", why);
        return kExitFailure;
    }
    AdapterList list;
    if (!EnumerateAdapters(info, &list))
    {
        os::PrintError("rhi_demo: cannot list the %s GPUs\n", backend->name);
        return kExitFailure;
    }
    os::Print("backend %s\n", backend->name);
    static const char* const kKinds[] = {"other", "software", "virtual", "integrated", "discrete"};
    os::Print("%u GPU(s):\n", list.count);
    for (u32 i = 0; i < list.count; ++i)
    {
        const AdapterInfo& a = list.items[i];
        os::Print("  %u  %s  %s  (%s, %llu MiB, driver %s)%s%s\n", i, a.id, a.name, kKinds[static_cast<usize>(a.kind)],
                  static_cast<unsigned long long>(a.dedicated_memory_bytes >> 20), a.driver,
                  a.suitable ? "" : "  unsuitable: ",
                  a.suitable ? "" : (a.unsuitable_reason != nullptr ? a.unsuitable_reason : "?"));
    }
    return kExitOk;
}

// Renders `frames` frames offscreen (no window) and saves the last one, with a fixed camera and light.
int RunScreenshot(const Options& options)
{
    constexpr u32 kSize = 256;
    const DeviceCreateInfo info = DeviceInfo(options);
    DevicePtr device = CreateDevice(info);
    if (device == nullptr)
    {
        RhiLogV(LogVerbosity::Error, "rhi_demo: no %s device", demo::BackendLabel(info.backend));
        return kExitFailure;
    }
    TextureDesc td;
    td.width = kSize;
    td.height = kSize;
    td.format = Format::RGBA8_UNorm;
    td.usage = TextureUsage::ColorAttachment | TextureUsage::CopySrc;
    const TextureHandle target = device->CreateTexture(td);
    Scene scene;
    int exit_code = kExitFailure;
    if (target.IsValid() && scene.depth.Ensure(device.Get(), kSize, kSize) && scene.Create(device.Get(), td.format))
    {
        const f32 background[4] = {0.05f, 0.05f, 0.05f, 1.0f};
        const OrbitCamera camera;
        const demo::Lighting lighting = LightAt(0.5);
        bool drawn = true;
        bool lost = false;
        for (long i = 0; i < (options.frames > 0 ? options.frames : 1); ++i)
        {
            device->BeginFrame();
            if (device->DeviceLost())
            {
                RhiLogV(LogVerbosity::Error, "rhi_demo: the GPU device was lost; stopping");
                device->EndFrame();
                lost = true;
                break;
            }
            scene.ring->BeginFrame();
            CommandList* list = device->CreateCommandList();
            list->Begin();
            drawn = RecordFrame(list, &scene, target, kSize, kSize, background, camera, lighting,
                                ResourceState::ColorAttachment) &&
                    drawn;
            list->End();
            scene.ring->Flush();
            CommandList* lists[] = {list};
            if (device->Submit(lists, 1) != 0)
            {
                scene.cube.Submitted();
            }
            device->EndFrame();
        }
        Vector<byte> pixels;
        pixels.Resize(static_cast<usize>(kSize) * kSize * 4);
        if (!lost && device->DeviceLost())
        {  // lost during the last frame's submit: no further BeginFrame to notice
            RhiLogV(LogVerbosity::Error, "rhi_demo: the GPU device was lost; stopping");
            lost = true;
        }
        if (lost)
        {
            exit_code = kExitDeviceLost;
        }
        else if (!drawn)
        {
            RhiLogV(LogVerbosity::Error, "rhi_demo: the cube could not be drawn");
        }
        else if (!device->ReadbackTexture(target, 0, 0, pixels.Data(), pixels.Size(), ResourceState::ColorAttachment))
        {
            RhiLogV(LogVerbosity::Error, "rhi_demo: could not read the image back");
        }
        else if (!WritePpm(options.screenshot, kSize, kSize, pixels))
        {
            RhiLogV(LogVerbosity::Error, "rhi_demo: could not write %s", options.screenshot);
        }
        else
        {
            RhiLogV(LogVerbosity::Info, "rhi_demo: wrote %s", options.screenshot);
            exit_code = kExitOk;
        }
    }
    else
    {
        RhiLogV(LogVerbosity::Error, "rhi_demo: could not create the target or the cube");
    }
    device->WaitIdle();
    scene.Destroy(device.Get());
    if (target.IsValid())
    {
        device->DestroyTexture(target);
    }
    device.Reset();
    return exit_code;
}

int RunWindow(const Options& options)
{
    os::InputState input;
    os::PlatformWindowHost host;
    if (!host.Open(os::ParseWindowBackend(StringView(options.backend)), "rhi_demo", 960, 540, &input))
    {
        RhiLogV(LogVerbosity::Error, "rhi_demo: could not open a window");
        return kExitFailure;
    }
    os::Window* window = host.Get();

    DeviceCreateInfo info = DeviceInfo(options);
    info.window = window;
    DevicePtr device = CreateDevice(info);
    if (device == nullptr)
    {
        RhiLogV(LogVerbosity::Error, "rhi_demo: no %s device can present to this window",
                demo::BackendLabel(info.backend));
        return kExitFailure;
    }
    SwapchainDesc swapchain_desc;
    if (options.no_vsync)
    {
        swapchain_desc.present_mode = PresentMode::Immediate;
    }
    Swapchain* swapchain = device->CreateSwapchain(swapchain_desc);
    if (swapchain == nullptr)
    {
        RhiLogV(LogVerbosity::Error, "rhi_demo: could not create a swapchain");
        device.Reset();
        return kExitFailure;
    }
    Scene scene;
    if (!scene.Create(device.Get(), swapchain->ImageFormat()))
    {
        RhiLogV(LogVerbosity::Error, "rhi_demo: could not create the cube");
        device->WaitIdle();
        scene.Destroy(device.Get());
        device->DestroySwapchain(swapchain);
        device.Reset();
        return kExitFailure;
    }

    OrbitCamera camera;
    const double start = NowSeconds();
    double window_start = start;
    double previous = start;
    double drag_x = 0.0;
    double drag_y = 0.0;
    bool dragging = false;
    u64 window_frames = 0;
    long frames = 0;
    long rss_at_start = 0;
    int exit_code = kExitOk;
    bool stop = false;  // set by a frame that failed; the run ends after it
    // One frame, from BeginFrame to Present, with the camera as it is. The loop below calls it, and so does the window
    // while Windows holds the thread in its own loop (the user dragging the window's frame), so the picture follows the
    // size while the frame is dragged.
    auto draw = [&]()
    {
        if (stop || (options.frames > 0 && frames >= options.frames))
        {
            return;  // (the window's loop may call again after the run ended)
        }
        const double now_time = NowSeconds();
        device->BeginFrame();
        if (device->DeviceLost())
        {
            RhiLogV(LogVerbosity::Error, "rhi_demo: the GPU device was lost; stopping");
            device->EndFrame();
            exit_code = kExitDeviceLost;
            stop = true;
            return;
        }
        const SwapchainStatus acquired = swapchain->Acquire();
        if (acquired == SwapchainStatus::Lost)
        {
            RhiLogV(LogVerbosity::Error, "rhi_demo: the swapchain was lost");
            device->EndFrame();
            exit_code = kExitFailure;
            stop = true;
            return;
        }
        if (acquired == SwapchainStatus::Minimized)
        {
            device->EndFrame();
            ++frames;  // an unpresentable surface still counts toward --frames, so automation terminates
            os::SleepNanos(10 * 1000 * 1000);
            return;
        }
        if (!scene.depth.Ensure(device.Get(), swapchain->Width(), swapchain->Height()))
        {  // follows a resize
            RhiLogV(LogVerbosity::Error, "rhi_demo: could not create the depth buffer");
            device->EndFrame();
            exit_code = kExitFailure;
            stop = true;
            return;
        }
        scene.ring->BeginFrame();
        CommandList* list = device->CreateCommandList();
        list->Begin();
        f32 background[4];
        BackgroundAt(now_time - start, background);
        if (!RecordFrame(list, &scene, swapchain->CurrentTexture(), swapchain->Width(), swapchain->Height(), background,
                         camera, LightAt(now_time - start), ResourceState::Present))
        {
            RhiLogV(LogVerbosity::Error, "rhi_demo: the cube could not be drawn");
            exit_code = kExitFailure;
        }
        list->End();
        scene.ring->Flush();
        CommandList* lists[] = {list};
        if (device->Submit(lists, 1) != 0)
        {
            scene.cube.Submitted();
        }
        device->EndFrame();
        if (swapchain->Present() == SwapchainStatus::Lost)
        {
            RhiLogV(LogVerbosity::Error, "rhi_demo: the swapchain was lost while presenting");
            exit_code = kExitFailure;
            stop = true;
            return;
        }
        ++frames;
        ++window_frames;
        if (rss_at_start == 0 && frames >= kMemoryCheckStart)
        {
            rss_at_start = static_cast<long>(os::ResidentMemoryKiB());
        }
        const double now = NowSeconds();
        if (now - window_start >= 1.0)
        {
            RhiLogV(LogVerbosity::Info, "rhi_demo: %llu frames, %.3f ms/frame",
                    static_cast<unsigned long long>(window_frames),
                    (now - window_start) * 1000.0 / static_cast<double>(window_frames));
            window_start = now;
            window_frames = 0;
        }
    };
    using Draw = decltype(draw);
    window->SetModalLoopCallback(
        [](void* user)
        {
            Draw& frame = *static_cast<Draw*>(user);
            frame();
        },
        &draw);
    while (!stop && !window->ShouldClose() && (options.frames <= 0 || frames < options.frames))
    {
        input.BeginFrame();
        window->PumpEvents();  // may draw frames itself (above), and so end the run
        if (stop)
        {
            break;
        }
        const double now_time = NowSeconds();
        const f32 dt = static_cast<f32>(now_time - previous < 0.1 ? now_time - previous
                                                                  : 0.1);  // a stall must not fling the camera
        previous = now_time;

        // The camera: arrow keys, left-button drag and the wheel.
        const os::KeyboardState& keys = input.keyboard;
        camera.Rotate(
            ((keys.IsDown(os::Key::Right) ? 1.0f : 0.0f) - (keys.IsDown(os::Key::Left) ? 1.0f : 0.0f)) * 1.5f * dt,
            ((keys.IsDown(os::Key::Up) ? 1.0f : 0.0f) - (keys.IsDown(os::Key::Down) ? 1.0f : 0.0f)) * 1.0f * dt);
        if (input.IsMouseDown(os::kMouseLeft))
        {
            if (dragging)
            {
                camera.Rotate(static_cast<f32>(input.mouse_x - drag_x) * -0.01f,
                              static_cast<f32>(input.mouse_y - drag_y) * 0.01f);
            }
            dragging = true;
            drag_x = input.mouse_x;
            drag_y = input.mouse_y;
        }
        else
        {
            dragging = false;
        }
        camera.Zoom(static_cast<f32>(input.scroll_dy) * 0.3f);

        if (window->IsMinimized())
        {
            // Nothing to draw to; wait for the window to come back without spinning. It still counts toward --frames,
            // so a bounded run on a minimized window terminates.
            ++frames;
            os::SleepNanos(10 * 1000 * 1000);
            continue;
        }
        draw();
    }
    window->SetModalLoopCallback(nullptr, nullptr);  // `draw` ends with this scope

    // A run of a few hundred frames or more must not grow in memory (a leak per frame would).
    if (exit_code == kExitOk && options.frames >= kMemoryCheckFrames && rss_at_start == 0)
    {
        RhiLogV(LogVerbosity::Warn, "rhi_demo: the memory check did not run (fewer than %ld frames were drawn)",
                kMemoryCheckStart);
    }
    if (exit_code == kExitOk && options.frames >= kMemoryCheckFrames && rss_at_start > 0)
    {
        const long rss_at_end = static_cast<long>(os::ResidentMemoryKiB());
        RhiLogV(LogVerbosity::Info, "rhi_demo: resident memory %ld KiB at frame %ld, %ld KiB at frame %ld",
                rss_at_start, kMemoryCheckStart, rss_at_end, frames);
        if (rss_at_end - rss_at_start > kMemoryGrowthLimitKiB)
        {
            RhiLogV(LogVerbosity::Error, "rhi_demo: memory grew by %ld KiB over the run", rss_at_end - rss_at_start);
            exit_code = kExitFailure;
        }
    }

    // Shutdown order: idle the GPU, then the scene, the swapchain and the device.
    device->WaitIdle();
    scene.Destroy(device.Get());
    device->DestroySwapchain(swapchain);
    device.Reset();
    return exit_code;
}

// Parses a whole non-negative number that fits in u32 (so a mistyped fault is a usage error, not "inject nothing").
bool ParseCount(const char* text, long* out)
{
    char* end = nullptr;
    const long value = CStrToLong(text, &end, 10);
    if (end == text || *end != '\0' || value < 0 || value > 0x7fffffffL)
    {
        return false;
    }
    *out = value;
    return true;
}

const char kUsageFormat[] =
    "usage: %s [--backend wayland|x11|win32|cocoa] [--frames N] [--screenshot out.ppm] [--no-vsync] [--validation]\n"
    "       [--inject-device-loss FRAME] [--inject-oom N] [--gpu ID|INDEX|NAME] [--list-gpus]\n"
    "  --backend B       display backend (default: automatic; same as ENGINE_WINDOW_BACKEND)\n"
    "  --frames N        stop after N frames (also RHI_DEMO_FRAMES=N)\n"
    "  --screenshot P    render offscreen, no window, and write the last frame to P (a binary PPM)\n"
    "  --no-vsync        present without waiting for the display\n"
    "  --gpu G           the GPU to use: an id from --list-gpus, an index, or part of the name\n"
    "  --list-gpus       print the GPUs and exit\n"
    "  --validation      enable the validation layer with synchronization validation\n"
    "  --inject-device-loss F  fault injection: lose the GPU device at frame F (exit code 3)\n"
    "  --inject-oom N    fault injection: fail device-memory allocation number N and every one after it\n";

int Usage(const char* program, int code)
{
    if (code == kExitOk)
    {
        os::Print(kUsageFormat, program);
    }
    else
    {
        os::PrintError(kUsageFormat, program);
    }
    return code;
}

}  // namespace

// The program's entry point (platform/entry_point.h): ENGINE_WINDOWED_PROGRAM at the end of the file makes it `main` on
// Linux and `wWinMain` (the Windows subsystem, no console window of its own) on Windows.
int EngineMain(int argc, char** argv)
{
    ::engine_placeholder::rhi::RegisterCompiledBackends();  // before any device: the backends this build has
    const engine_placeholder::debug::ProgramDebugSetup debug_setup = engine_placeholder::debug::StartProgramDebugging(
        argc, argv);
    engine_placeholder::debug::RunCrashTest(
        debug_setup.crash_test);              // --crash-test=segv|abort|assert|ensure, before any window or device
    LogRhi.SetVerbosity(LogVerbosity::Info);  // the frame-time line is Info
    demo::DemoSettings demo_settings;
    char usage_text[2048];
    FormatTo(usage_text, sizeof(usage_text), kUsageFormat, argv[0]);
    if (!demo_settings.Begin("rhi_demo", usage_text, argc, argv))
    {
        return kExitUsage;
    }
    g_demo_settings = &demo_settings;
    argc = demo_settings.RestCount();  // what the settings system did not take: --set, --settings, --gpu, --backend,
                                       // ... are handled there
    argv = demo_settings.RestArgs();
    Options options;
    const char* frames_env = os::GetEnvironment("RHI_DEMO_FRAMES");
    options.frames = frames_env != nullptr ? CStrToLong(frames_env) : 0;
    for (int i = 1; i < argc; ++i)
    {
        if (engine_placeholder::debug::IsProgramDebugArgument(argv[i]))
        {
            continue;  // --crash-handler=... and --crash-test=... are handled by StartProgramDebugging
        }
        const bool has_value = i + 1 < argc;
        if (CStrCompare(argv[i], "--frames") == 0 && has_value)
        {
            options.frames = CStrToLong(argv[++i]);
        }
        else if (CStrCompare(argv[i], "--screenshot") == 0 && has_value)
        {
            options.screenshot = argv[++i];
        }
        else if (CStrCompare(argv[i], "--backend") == 0 && has_value)
        {
            options.backend = argv[++i];
        }
        else if (CStrCompare(argv[i], "--gpu") == 0 && has_value)
        {
            options.gpu = argv[++i];
        }
        else if (CStrCompare(argv[i], "--list-gpus") == 0)
        {
            options.list_gpus = true;
        }
        else if (CStrCompare(argv[i], "--no-vsync") == 0)
        {
            options.no_vsync = true;
        }
        else if (CStrCompare(argv[i], "--validation") == 0)
        {
            options.validation = true;
        }
        else if (CStrCompare(argv[i], "--inject-device-loss") == 0 && has_value)
        {
            if (!ParseCount(argv[++i], &options.inject_device_loss))
            {
                return Usage(argv[0], kExitUsage);
            }
        }
        else if (CStrCompare(argv[i], "--inject-oom") == 0 && has_value)
        {
            if (!ParseCount(argv[++i], &options.inject_oom))
            {
                return Usage(argv[0], kExitUsage);
            }
        }
        else if (CStrCompare(argv[i], "--help") == 0 || CStrCompare(argv[i], "-h") == 0)
        {
            return Usage(argv[0], kExitOk);
        }
        else
        {
            return Usage(argv[0], kExitUsage);
        }
    }
    // The flags of this program that are settings: put them into the settings, so --list-settings and --save-settings
    // show them.
    if (options.no_vsync)
    {
        demo_settings.SetLegacy("renderer.present_mode", "immediate", "--no-vsync");
    }
    if (options.validation)
    {
        demo_settings.SetLegacy("renderer.validation", "on", "--validation");
        demo_settings.SetLegacy("renderer.sync_validation", "true", "--validation");
    }
    {
        const int code = demo_settings.Finish();
        if (code >= 0)
        {
            return code;
        }
    }
    // And what the settings say (a file, --set, the environment) is what the program does.
    static demo::DemoSettings::Effective effective;
    effective = demo_settings.ReadEffective();
    if (effective.gpu[0] != '\0')
    {
        options.gpu = effective.gpu;
    }
    options.no_vsync = effective.immediate;
    options.validation = effective.validation;
    options.backend = effective.window_backend;  // --backend, ENGINE_WINDOW_BACKEND or a file, through the settings
    if (options.list_gpus)
    {
        return ListGpus();
    }
    return options.screenshot != nullptr ? RunScreenshot(options) : RunWindow(options);
}

ENGINE_WINDOWED_PROGRAM(EngineMain)
