// render_features_demo: a 2048 x 2048 terrain under a cube map sky, a forest of 200,000 instanced trees (an instance
// hierarchy with levels of detail and a distant stand-in) scattered on it by ray casts, instanced rocks, a character
// walking on the height field and bodies rolling on it. The overlay shows the draw counts, the cull counters and the
// frame time. See README.md.
//
//   render_features_demo [--frames N] [--sky procedural|cube] [--backend B]      a window
//   render_features_demo --screenshot out.ppm [--frames N] [--sky ...]           draws offscreen, writes the last frame
//   render_features_demo --headless [--steps N] [--threads N] [--trees N]         physics and visible-list hashes

#include "containers/array.h"
#include "containers/vector.h"
#include "cstr/format.h"
#include "cstr/number.h"
#include "debug/program_setup.h"
#include "features_gpu.h"
#include "features_world.h"
#include "io/file.h"
#include "io/path.h"
#include "jobs/scheduler.h"
#include "log/output.h"
#include "os/command_line.h"
#include "os/os.h"
#include "platform/entry_point.h"
#include "platform/input.h"
#include "platform/window_host.h"
#include "renderer/device_resources.h"
#include "renderer/render_settings.h"
#include "renderer/renderer.h"
#include "renderer/scene_frame.h"
#include "renderer/shared_settings.h"
#include "rhi/backend_registry.h"
#include "rhi/backends/backends.h"
#include "rhi/device_host.h"
#include "rhi/rhi.h"
#include "rhi/rhi_log.h"
#include "settings_main.h"
#include "string/string.h"
#include "textures.h"

using namespace vse;
using namespace render_features;
using renderer::Camera;
using renderer::DeviceResources;
using renderer::FrameStatus;
using renderer::FrameTarget;
using renderer::MakeDeviceCreateInfo;
using renderer::MakeSwapchainDesc;
using renderer::Renderer;
using renderer::RenderSettings;
using renderer::SceneFrame;
using renderer::SceneFrameSetup;
using rhi::CreateDevice;
using rhi::DeviceCreateInfo;
using rhi::DeviceHost;
using rhi::DevicePtr;
using rhi::Format;
using rhi::LogRhi;
using rhi::ResourceState;
using rhi::RhiLogV;
using rhi::TextureDesc;
using rhi::TextureHandle;
using rhi::TextureUsage;

namespace
{

constexpr int kExitOk = 0;
constexpr int kExitFailure = 1;
constexpr int kExitUsage = 2;
constexpr int kExitDeviceLost = 3;

constexpr u32 kShotWidth = 960;
constexpr u32 kShotHeight = 540;

// The reference image is the frame reduced by 8 in each direction (box filter), so a driver's rounding differences
// vanish and the committed file stays small.
constexpr u32 kRefWidth = kShotWidth / 8;
constexpr u32 kRefHeight = kShotHeight / 8;

struct Options
{
    const char* reference = nullptr;        // --reference: compare the frame with this image
    const char* write_reference = nullptr;  // --write-reference: write the reduced frame here
    long tolerance = 40;                    // thousandths of the mean channel difference that still matches
    long frames = 0;                        // 0: until the window closes (screenshot: 240)
    long steps = 300;
    long threads = 1;
    long trees = 200000;
    long rocks = 400;
    long reset_every = 0;  // a device reset every N frames (0: never)
    bool headless = false;
    bool cube_sky = true;
    const char* screenshot = nullptr;
    const char* backend = "auto";
};

demo::DemoSettings* g_demo_settings = nullptr;

DeviceCreateInfo DeviceInfo()
{
    DeviceCreateInfo info;
    g_demo_settings->FillDeviceCreateInfo(&info);
    return info;
}

WorldOptions WorldFor(const Options& options)
{
    WorldOptions wo;
    wo.trees = static_cast<u32>(options.trees);
    wo.rocks = static_cast<u32>(options.rocks);
    wo.threads = static_cast<u32>(options.threads);
    return wo;
}

SceneFrameSetup SetupFor(rhi::Device* device, rhi::Swapchain* swapchain, const RenderSettings& settings,
                         jobs::Scheduler* scheduler)
{
    SceneFrameSetup fs;
    fs.initial_settings = &settings;
    fs.device = device;
    fs.swapchain = swapchain;
    fs.max_objects = 1024;
    fs.max_lights = 8;
    fs.scheduler = scheduler;
    fs.font_path = RENDER_FEATURES_FONT_FILE;
    return fs;
}

// The overlay: what the frame drew and what the culls did.
void OverlayText(const SceneFrame& frame, const FeaturesWorld& world, f64 frame_ms, bool cube_sky,
                 String<HeapAllocator>* greeting, String<HeapAllocator>* status)
{
    const renderer::GBufferStats& s = frame.GBufferStatistics();
    *greeting = String<HeapAllocator>(HeapAllocator::Default(), StringView("render features"));
    Array<char, 512> line;
    FormatTo(line.Data(), line.Size(),
             "%u trees, %u draws, %u instances drawn (%u tree nodes), %u chunks tested / %u skipped, %.2f ms\n",
             world.TreesPlaced(), s.draws, s.instances_visible, s.instance_tree_nodes, s.instance_chunks_tested,
             s.instance_chunks_skipped, frame_ms);
    *status = String<HeapAllocator>(HeapAllocator::Default(), StringView(line.Data()));
    FormatTo(line.Data(), line.Size(), "%s sky; C: switch the sky, F1: overlay, Escape: quit",
             cube_sky ? "cube map" : "procedural");
    status->Append(StringView(line.Data()));
}

// The frame reduced to kRefWidth x kRefHeight, as RGBA.
void Reduce(const byte* rgba, Vector<byte>* out)
{
    out->Resize(static_cast<usize>(kRefWidth) * kRefHeight * 4);
    for (u32 y = 0; y < kRefHeight; ++y)
    {
        for (u32 x = 0; x < kRefWidth; ++x)
        {
            u32 sum[3] = {0, 0, 0};
            for (u32 dy = 0; dy < 8; ++dy)
            {
                for (u32 dx = 0; dx < 8; ++dx)
                {
                    const byte* p = rgba + (static_cast<usize>(y * 8 + dy) * kShotWidth + x * 8 + dx) * 4;
                    sum[0] += static_cast<u32>(p[0]);
                    sum[1] += static_cast<u32>(p[1]);
                    sum[2] += static_cast<u32>(p[2]);
                }
            }
            byte* o = out->Data() + (static_cast<usize>(y) * kRefWidth + x) * 4;
            o[0] = static_cast<byte>(sum[0] / 64);
            o[1] = static_cast<byte>(sum[1] / 64);
            o[2] = static_cast<byte>(sum[2] / 64);
            o[3] = static_cast<byte>(255);
        }
    }
}

// Reads a binary PPM of exactly kRefWidth x kRefHeight; `rgb` receives the pixels.
bool ReadReference(const char* path_text, Vector<byte>* rgb)
{
    io::Path path;
    io::FilePtr in;
    Vector<u8> bytes;
    if (!io::Path::Parse(StringView(path_text), io::NativePathStyle(), &path) ||
        io::File::Open(path, io::OpenMode::Read, io::kOpenNone, &in).Failed() ||
        !io::ReadAll(*in, &bytes, 1 << 20).Ok())
    {
        return false;
    }
    Array<char, 32> header;
    FormatTo(header.Data(), header.Size(), "P6\n%u %u\n255\n", kRefWidth, kRefHeight);
    const usize header_size = CStrLength(header.Data());
    const usize pixels = static_cast<usize>(kRefWidth) * kRefHeight * 3;
    if (bytes.Size() != header_size + pixels || MemCompare(bytes.Data(), header.Data(), header_size) != 0)
    {
        return false;
    }
    rgb->Resize(pixels);
    MemCopy(rgb->Data(), bytes.Data() + header_size, pixels);
    return true;
}

// ---- headless
// ---------------------------------------------------------------------------------------------------------

int RunHeadless(const Options& options)
{
    jobs::Scheduler::Config sc;
    sc.participants = static_cast<u32>(options.threads);
    sc.pin = false;
    jobs::Scheduler scheduler(sc);
    FeaturesWorld world;
    if (!world.Build(WorldFor(options), &scheduler))
    {
        out::PrintError("render_features_demo: %s\n", world.Error());
        return kExitFailure;
    }
    for (long i = 0; i < options.steps; ++i)
    {
        world.Step();
    }
    bool valid = false;
    const u64 physics_hash = world.PhysicsHash(&valid);
    const VisibleSummary visible = world.Visible(world.ChaseCamera(16.0f / 9.0f), &scheduler);
    const Vec3 at = world.CharacterPosition();
    const bool on_ground = at.y > world.HeightAt(at.x, at.z) - 0.1f;
    const bool ok = valid && on_ground && visible.tree.nodes_emitted > 0;
    out::Print("render_features_demo: %ld steps, %ld threads, %u trees, state hash 0x%016llx, visible hash 0x%016llx, "
               "%u tree nodes and %u rock runs visible %s\n",
               options.steps, options.threads, world.TreesPlaced(), static_cast<unsigned long long>(physics_hash),
               static_cast<unsigned long long>(visible.hash), visible.tree.nodes_emitted, visible.rocks.runs,
               ok ? "ok" : "FAILED");
    return ok ? kExitOk : kExitFailure;
}

// ---- offscreen
// --------------------------------------------------------------------------------------------------------

// Writes the reduced frame and/or compares it with the reference; false when it differs by more than the tolerance.
bool CheckReference(const Options& options, const byte* rgba)
{
    Vector<byte> reduced;
    Reduce(rgba, &reduced);
    io::Path file;
    if (options.write_reference != nullptr &&
        (!io::Path::Parse(StringView(options.write_reference), io::NativePathStyle(), &file) ||
         !demo::WritePpm(file, kRefWidth, kRefHeight, reduced.Data()).Ok()))
    {
        RhiLogV(LogVerbosity::Error, "render_features_demo: could not write %s", options.write_reference);
        return false;
    }
    if (options.reference == nullptr)
    {
        return true;
    }
    Vector<byte> expected;
    if (!ReadReference(options.reference, &expected))
    {
        RhiLogV(LogVerbosity::Error, "render_features_demo: %s is not a %ux%u binary PPM", options.reference, kRefWidth,
                kRefHeight);
        return false;
    }
    u64 total = 0;
    for (usize i = 0; i < static_cast<usize>(kRefWidth) * kRefHeight; ++i)
    {
        for (usize c = 0; c < 3; ++c)
        {
            const int a = static_cast<int>(reduced[i * 4 + c]);
            const int b = static_cast<int>(expected[i * 3 + c]);
            total += static_cast<u64>(a > b ? a - b : b - a);
        }
    }
    const f64 mean = static_cast<f64>(total) / (static_cast<f64>(kRefWidth) * kRefHeight * 3.0 * 255.0);
    const bool match = mean * 1000.0 <= static_cast<f64>(options.tolerance);
    RhiLogV(match ? LogVerbosity::Info : LogVerbosity::Error,
            "render_features_demo: the frame %s the reference (mean difference %.4f, tolerance %.4f)",
            match ? "matches" : "differs from", mean, static_cast<f64>(options.tolerance) / 1000.0);
    return match;
}

int RunScreenshot(const Options& options, const RenderSettings& settings)
{
    jobs::Scheduler::Config sc;
    sc.participants = static_cast<u32>(options.threads);
    sc.pin = false;
    jobs::Scheduler scheduler(sc);
    FeaturesWorld world;
    if (!world.Build(WorldFor(options), &scheduler))
    {
        out::PrintError("render_features_demo: %s\n", world.Error());
        return kExitFailure;
    }
    Array<char, 64> adapter_buffer;
    DeviceCreateInfo device_info;
    MakeDeviceCreateInfo(settings, DeviceInfo(), adapter_buffer.Data(), &device_info);
    DevicePtr device = CreateDevice(device_info);
    if (device == nullptr)
    {
        RhiLogV(LogVerbosity::Error, "render_features_demo: no %s device", demo::BackendLabel(device_info.backend));
        return kExitFailure;
    }
    TextureDesc td;
    td.width = kShotWidth;
    td.height = kShotHeight;
    td.format = Format::RGBA8_SRGB;
    td.usage = TextureUsage::ColorAttachment | TextureUsage::CopySrc;
    const TextureHandle target = device->CreateTexture(td);
    int exit_code = kExitFailure;
    {
        SceneFrame frame;
        FeaturesGpu gpu;
        GpuOptions go;
        go.cube_sky = options.cube_sky;
        if (!target.IsValid() || !frame.Create(SetupFor(device.Get(), nullptr, settings, &scheduler)))
        {
            RhiLogV(LogVerbosity::Error, "render_features_demo: could not create the target or the frame");
        }
        else if (!gpu.Create(device.Get(), frame.Materials(), frame.Templates(), &world, go))
        {
            RhiLogV(LogVerbosity::Error, "render_features_demo: %s", gpu.Error());
        }
        else
        {
            const f32 aspect = static_cast<f32>(kShotWidth) / static_cast<f32>(kShotHeight);
            FrameTarget ft;
            ft.texture = target;
            ft.width = kShotWidth;
            ft.height = kShotHeight;
            ft.format = Format::RGBA8_SRGB;
            ft.initial = ResourceState::Undefined;
            ft.final_state = ResourceState::CopySrc;
            FrameStatus status = FrameStatus::Ok;
            frame.SetSky(gpu.Sky(), 1.0f);
            const long frames = options.frames > 0 ? options.frames : 240;
            for (long i = 0; i < frames && status == FrameStatus::Ok; ++i)
            {
                world.Step();
                gpu.Sync();
                String<HeapAllocator> greeting(HeapAllocator::Default());
                String<HeapAllocator> text(HeapAllocator::Default());
                OverlayText(frame, world, 0.0, options.cube_sky, &greeting, &text);
                frame.SetOverlay(true, greeting.View(), text.View());
                Camera view = world.ChaseCamera(aspect);
                frame.SetTime(static_cast<f32>(i) * kStepSeconds, kStepSeconds);
                status = frame.Render(&gpu.Scene(), &view, &ft);
                ft.initial = ResourceState::CopySrc;
            }
            Vector<byte> pixels;
            pixels.Resize(static_cast<usize>(kShotWidth) * kShotHeight * 4);
            device->WaitIdle();
            io::Path file;
            if (status == FrameStatus::DeviceLost || device->DeviceLost())
            {
                RhiLogV(LogVerbosity::Error, "render_features_demo: the GPU device was lost; stopping");
                exit_code = kExitDeviceLost;
            }
            else if (status != FrameStatus::Ok)
            {
                RhiLogV(LogVerbosity::Error, "render_features_demo: a frame failed: %s", frame.LastError());
            }
            else if (!device->ReadbackTexture(target, 0, 0, pixels.Data(), pixels.Size(), ResourceState::CopySrc))
            {
                RhiLogV(LogVerbosity::Error, "render_features_demo: could not read the image back");
            }
            else if (!io::Path::Parse(StringView(options.screenshot), io::NativePathStyle(), &file) ||
                     !demo::WritePpm(file, kShotWidth, kShotHeight, pixels.Data()).Ok())
            {
                RhiLogV(LogVerbosity::Error, "render_features_demo: could not write %s", options.screenshot);
            }
            else if (!CheckReference(options, pixels.Data()))
            {
                exit_code = kExitFailure;
            }
            else
            {
                const renderer::GBufferStats& s = frame.GBufferStatistics();
                RhiLogV(LogVerbosity::Info,
                        "render_features_demo: wrote %s (%u trees, %u draws, %u instances drawn, %u tree nodes)",
                        options.screenshot, world.TreesPlaced(), s.draws, s.instances_visible, s.instance_tree_nodes);
                exit_code = kExitOk;
            }
        }
        device->WaitIdle();
        gpu.Destroy();
        frame.Destroy();
    }
    if (target.IsValid())
    {
        device->DestroyTexture(target);
    }
    device.Reset();
    return exit_code;
}

// ---- window
// -----------------------------------------------------------------------------------------------------------

int RunWindow(const Options& options, const RenderSettings& settings)
{
    jobs::Scheduler::Config scheduler_config;
    scheduler_config.pin = false;
    jobs::Scheduler scheduler(scheduler_config);
    FeaturesWorld world;
    if (!world.Build(WorldFor(options), &scheduler))
    {
        out::PrintError("render_features_demo: %s\n", world.Error());
        return kExitFailure;
    }
    os::InputState input;
    os::PlatformWindowHost window_host;
    if (!window_host.Open(os::ParseWindowBackend(StringView(options.backend)), "render_features_demo", 1280, 720,
                          &input))
    {
        RhiLogV(LogVerbosity::Error, "render_features_demo: could not open a window");
        return kExitFailure;
    }
    os::Window* window = window_host.Get();
    DeviceCreateInfo base = DeviceInfo();
    base.window = window;
    Array<char, 64> adapter_buffer;
    DeviceCreateInfo info;
    MakeDeviceCreateInfo(settings, base, adapter_buffer.Data(), &info);
    DeviceResources registry;
    DeviceHost host;
    host.AddObserver(&registry, 0);
    if (!host.Create(info, MakeSwapchainDesc(settings)))
    {
        RhiLogV(LogVerbosity::Error, "render_features_demo: no %s device can present to this window (%s)",
                demo::BackendLabel(info.backend), host.LastError());
        return kExitFailure;
    }
    int exit_code = kExitOk;
    {
        SceneFrame frame;
        FeaturesGpu gpu;
        SceneFrameSetup fs = SetupFor(host.GetDevice(), host.GetSwapchain(), settings, &scheduler);
        fs.resources = &registry;
        fs.host = &host;
        fs.create_info = base;
        fs.window = window;
        GpuOptions go;
        go.resources = &registry;
        go.cube_sky = options.cube_sky;
        if (!frame.Create(fs))
        {
            RhiLogV(LogVerbosity::Error, "render_features_demo: could not create the frame");
            exit_code = kExitFailure;
        }
        else if (!gpu.Create(host.GetDevice(), frame.Materials(), frame.Templates(), &world, go))
        {
            RhiLogV(LogVerbosity::Error, "render_features_demo: %s", gpu.Error());
            exit_code = kExitFailure;
        }
        else
        {
            Renderer& renderer = *frame.GetRenderer();
            struct ResetParts
            {
                SceneFrame* frame;
                FeaturesGpu* gpu;
            } parts{&frame, &gpu};
            renderer.SetResetCallbacks([](void*, const char*) {},
                                       [](void* user, bool)
                                       {
                                           auto* p = static_cast<ResetParts*>(user);
                                           Renderer* r = p->frame->GetRenderer();
                                           if (r->GetDevice() != nullptr)
                                           {
                                               p->frame->Rebind(r->GetDevice(), r->GetSwapchain());
                                               p->gpu->Rebind(r->GetDevice());
                                           }
                                       },
                                       &parts);
            frame.SetOverlayScale(window->ContentScale());
            frame.SetSky(gpu.Sky(), 1.0f);
            const auto aspect_now = [&]()
            {
                rhi::Swapchain* swapchain = renderer.GetSwapchain();
                return swapchain != nullptr && swapchain->Height() > 0
                           ? static_cast<f32>(swapchain->Width()) / static_cast<f32>(swapchain->Height())
                           : 16.0f / 9.0f;
            };
            const f64 start = static_cast<f64>(os::MonotonicTimeNanos()) * 1e-9;
            f64 previous = start;
            f64 accumulator = 0.0;
            f64 frame_ms = 0.0;
            long frames = 0;
            bool overlay_on = true;
            bool cube_sky = options.cube_sky;
            bool stop = false;
            os::KeyboardState keys_before;
            const auto pressed = [&](os::Key key) { return input.keyboard.IsDown(key) && !keys_before.IsDown(key); };
            auto draw = [&]()
            {
                if (stop || (options.frames > 0 && frames >= options.frames))
                {
                    return;
                }
                const f64 now = static_cast<f64>(os::MonotonicTimeNanos()) * 1e-9;
                const f64 dt = now - previous < 0.1 ? now - previous : 0.1;
                previous = now;
                frame_ms = frame_ms * 0.9 + dt * 1000.0 * 0.1;
                if (pressed(os::Key::F1))
                {
                    overlay_on = !overlay_on;
                }
                if (pressed(os::Key::C))
                {
                    cube_sky = !cube_sky;
                    gpu.SetCubeSky(cube_sky);
                    frame.SetSky(gpu.Sky(), 1.0f);
                }
                if (pressed(os::Key::Escape))
                {
                    stop = true;
                    return;
                }
                keys_before = input.keyboard;
                // The simulation runs in fixed steps; a frame does as many as it owes (at most 4).
                accumulator += dt;
                for (int n = 0; n < 4 && accumulator >= kStepSeconds; ++n)
                {
                    world.Step();
                    accumulator -= kStepSeconds;
                }
                gpu.Sync();
                String<HeapAllocator> greeting(HeapAllocator::Default());
                String<HeapAllocator> text(HeapAllocator::Default());
                OverlayText(frame, world, frame_ms, cube_sky, &greeting, &text);
                frame.SetOverlay(overlay_on, greeting.View(), text.View());
                Camera view = world.ChaseCamera(aspect_now());
                frame.SetTime(static_cast<f32>(now - start), static_cast<f32>(dt));
                const FrameStatus status = frame.Render(&gpu.Scene(), &view, nullptr);
                if (status == FrameStatus::DeviceLost)
                {
                    if (renderer.IsBroken())
                    {
                        RhiLogV(LogVerbosity::Error, "render_features_demo: the GPU device was lost; stopping");
                        exit_code = kExitDeviceLost;
                        stop = true;
                        return;
                    }
                }
                else if (status == FrameStatus::Failed)
                {
                    RhiLogV(LogVerbosity::Error, "render_features_demo: a frame failed: %s", frame.LastError());
                    exit_code = kExitFailure;
                    stop = true;
                    return;
                }
                else if (status == FrameStatus::Skipped)
                {
                    os::SleepNanos(10 * 1000 * 1000);
                }
                ++frames;
                if (options.reset_every > 0 && frames % options.reset_every == 0)
                {
                    renderer.ForceDeviceReset();
                }
            };
            using Draw = decltype(draw);
            window->SetModalLoopCallback([](void* user) { (*static_cast<Draw*>(user))(); }, &draw);
            while (!stop && !window->ShouldClose() && (options.frames <= 0 || frames < options.frames))
            {
                input.BeginFrame();
                window->PumpEvents();
                if (stop)
                {
                    break;
                }
                draw();
            }
            window->SetModalLoopCallback(nullptr, nullptr);
            if (exit_code == kExitOk)
            {
                RhiLogV(LogVerbosity::Info, "render_features_demo: showed the scene for %ld frames (%u trees)", frames,
                        world.TreesPlaced());
            }
            if (host.GetDevice() != nullptr)
            {
                host.GetDevice()->WaitIdle();
            }
        }
        gpu.Destroy();
        frame.Destroy();
    }
    host.Destroy();
    return exit_code;
}

bool ParseCount(StringView text, long min, long max, long* out)
{
    char* end = nullptr;
    const long value = CStrToLong(text.Data(), &end, 10);
    if (end == text.Data() || *end != '\0' || value < min || value > max)
    {
        return false;
    }
    *out = value;
    return true;
}

const char kUsageFormat[] =
    "usage: %s [--frames N] [--screenshot out.ppm] [--sky procedural|cube] [--backend wayland|x11|win32|cocoa]\n"
    "          [--trees N] [--rocks N] [--threads N] [--reset-every N]\n"
    "       %s --headless [--steps N] [--threads N] [--trees N] [--rocks N]\n"
    "  --frames N       stop after N frames (--screenshot: 240)\n"
    "  --screenshot P   draw offscreen, no window, and write the last frame to P (a binary PPM)\n"
    "  --reference P    compare the frame, reduced 8 times, with the image P (--tolerance N thousandths, default 40)\n"
    "  --write-reference P  write that reduced image to P\n"
    "  --sky S          the cube map sky (default) or the procedural gradient\n"
    "  --trees N        trees to place (default 200000)\n"
    "  --rocks N        rocks to place (default 400)\n"
    "  --threads N      threads of the job scheduler (headless and offscreen; default 1)\n"
    "  --reset-every N  reset the graphics device every N frames\n"
    "  --headless       no window and no device: step the physics, print the hash of its state and of the visible\n"
    "                   instance list of the last frame (--steps N, default 300)\n"
    "  --backend B      display backend (default: automatic; same as ENGINE_WINDOW_BACKEND)\n"
    "Keys: C switches the sky, F1 hides the overlay, Escape quits.\n";

int Usage(const char* program, int code)
{
    if (code == kExitOk)
    {
        out::Print(kUsageFormat, program, program);
    }
    else
    {
        out::PrintError(kUsageFormat, program, program);
    }
    return code;
}

}  // namespace

int EngineMain(int argc, char** argv)
{
    ::vse::rhi::RegisterCompiledBackends();
    const debug::ProgramDebugSetup debug_setup = debug::StartProgramDebugging(argc, argv);
    debug::RunCrashTest(debug_setup.crash_test);
    LogRhi.SetVerbosity(LogVerbosity::Info);
    demo::DemoSettings demo_settings;
    Array<char, 3072> usage_text;
    const os::CommandLine<HeapAllocator> native(HeapAllocator::Default(), argc, argv);
    FormatTo(usage_text.Data(), usage_text.Size(), kUsageFormat, native.ProgramName().Data(),
             native.ProgramName().Data());
    if (!demo_settings.Begin("render_features_demo", usage_text.Data(), argc, argv))
    {
        return kExitUsage;
    }
    g_demo_settings = &demo_settings;
    static const Array<os::CommandLineOption, 17> kCliOptions = {os::CommandLineOption{"reference", true},
                                                                 os::CommandLineOption{"write-reference", true},
                                                                 os::CommandLineOption{"tolerance", true},
                                                                 os::CommandLineOption{"frames", true},
                                                                 os::CommandLineOption{"screenshot", true},
                                                                 os::CommandLineOption{"sky", true},
                                                                 os::CommandLineOption{"trees", true},
                                                                 os::CommandLineOption{"rocks", true},
                                                                 os::CommandLineOption{"threads", true},
                                                                 os::CommandLineOption{"steps", true},
                                                                 os::CommandLineOption{"reset-every", true},
                                                                 os::CommandLineOption{"headless", false},
                                                                 os::CommandLineOption{"backend", true},
                                                                 os::CommandLineOption{"help", false},
                                                                 os::CommandLineOption{"-h", false},
                                                                 debug::kCrashHandlerOption,
                                                                 debug::kCrashTestOption};
    const os::CommandLine<HeapAllocator> line(
        HeapAllocator::Default(), demo_settings.RestCount(), demo_settings.RestArgs(),
        Span<const os::CommandLineOption>(kCliOptions.Data(), kCliOptions.Size()));
    const char* program = line.ProgramName().Data();
    if (line.HasFlag("help") || line.HasFlag("-h"))
    {
        return Usage(program, kExitOk);
    }
    if (!line.Ok() || line.ArgCount() != 0)
    {
        out::PrintError("%s: %s\n", program, !line.Ok() ? line.Error().Data() : "no arguments are taken");
        return Usage(program, kExitUsage);
    }
    Options options;
    StringView value;
    if ((line.TryGetOption("frames", value) && !ParseCount(value, 0, 10000000, &options.frames)) ||
        (line.TryGetOption("steps", value) && !ParseCount(value, 1, 1000000, &options.steps)) ||
        (line.TryGetOption("threads", value) && !ParseCount(value, 1, 64, &options.threads)) ||
        (line.TryGetOption("trees", value) && !ParseCount(value, 0, 1000000, &options.trees)) ||
        (line.TryGetOption("rocks", value) && !ParseCount(value, 0, 100000, &options.rocks)) ||
        (line.TryGetOption("tolerance", value) && !ParseCount(value, 0, 1000, &options.tolerance)) ||
        (line.TryGetOption("reset-every", value) && !ParseCount(value, 1, 10000000, &options.reset_every)))
    {
        out::PrintError("%s: a number is out of range\n", program);
        return Usage(program, kExitUsage);
    }
    if (line.TryGetOption("screenshot", value))
    {
        options.screenshot = value.Data();
    }
    options.headless = line.HasFlag("headless");
    if (options.reset_every > 0 && (options.headless || options.screenshot != nullptr))
    {
        out::PrintError("%s: --reset-every needs a window\n", program);
        return Usage(program, kExitUsage);
    }
    if (line.TryGetOption("sky", value))
    {
        if (value == StringView("cube"))
        {
            options.cube_sky = true;
        }
        else if (value == StringView("procedural"))
        {
            options.cube_sky = false;
        }
        else
        {
            out::PrintError("%s: --sky is procedural or cube\n", program);
            return Usage(program, kExitUsage);
        }
    }
    if (line.TryGetOption("reference", value))
    {
        options.reference = value.Data();
    }
    if (line.TryGetOption("write-reference", value))
    {
        options.write_reference = value.Data();
    }
    if (line.TryGetOption("backend", value))
    {
        options.backend = value.Data();
    }
    {
        const int code = demo_settings.Finish();
        if (code >= 0)
        {
            return code;
        }
    }
    if (options.headless)
    {
        return RunHeadless(options);
    }
    static demo::DemoSettings::Effective effective;
    effective = demo_settings.ReadEffective();
    options.backend = effective.window_backend;
    const RenderSettings settings = renderer::RenderSettingsFromSnapshot(demo_settings.GetRegistry(),
                                                                         *demo_settings.Get().Snapshot());
    if (options.screenshot != nullptr)
    {
        return RunScreenshot(options, settings);
    }
    return RunWindow(options, settings);
}

ENGINE_WINDOWED_PROGRAM(EngineMain)
