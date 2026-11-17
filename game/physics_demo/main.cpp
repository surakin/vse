// physics_demo: a flat floor with random boxes, spheres and capsules lying on it, and more of them spawning in the air
// and falling onto it. Every shape is an ECS entity (mesh, rigid body, collider, transform); the schedule steps the
// physics, propagates the transforms and mirrors the entities into the deferred renderer's scene. In a window, or
// offscreen with --screenshot. See README.md.

#include "containers/vector.h"
#include "cstr/cstr.h"
#include "cstr/format.h"
#include "cstr/number.h"
#include "debug/program_setup.h"
#include "io/file.h"
#include "io/file_system.h"
#include "lang/scalar.h"
#include "os/command_line.h"
#include "os/console.h"
#include "os/os.h"
#include "physics_scene.h"
#include "platform/entry_point.h"
#include "platform/input.h"
#include "platform/window_host.h"
#include "renderer/device_resources.h"
#include "renderer/render_settings.h"
#include "renderer/renderer.h"
#include "renderer/scene_frame.h"
#include "renderer/shared_settings.h"
#include "rhi/adapter.h"
#include "rhi/backend_registry.h"
#include "rhi/backends/backends.h"
#include "rhi/device_host.h"
#include "rhi/rhi.h"
#include "rhi/rhi_log.h"
#include "settings_main.h"
#include "textures.h"

using namespace engine_placeholder;
using namespace engine_placeholder::rhi;
using namespace engine_placeholder::renderer;
using namespace physics_demo;

namespace
{

constexpr int kExitOk = 0;
constexpr int kExitFailure = 1;
constexpr int kExitUsage = 2;
constexpr int kExitDeviceLost = 3;

constexpr u32 kShotWidth = 960;
constexpr u32 kShotHeight = 540;

using Frame = SceneFrame;

struct Options
{
    long frames = 0;                   // 0: until the window closes
    const char* screenshot = nullptr;  // render offscreen to this PPM and exit
    const char* backend = "auto";
    long bodies = 160;  // the most shapes alive
    long seed = 1;
    bool paused = false;
};

double NowSeconds()
{
    return static_cast<double>(os::MonotonicTimeNanos()) * 1e-9;
}

demo::DemoSettings* g_demo_settings = nullptr;

DeviceCreateInfo DeviceInfo()
{
    DeviceCreateInfo info;
    g_demo_settings->FillDeviceCreateInfo(&info);
    return info;
}

SceneOptions SceneOptionsFor(const Options& options)
{
    SceneOptions so;
    so.max_bodies = static_cast<u32>(options.bodies);
    so.seed = static_cast<u32>(options.seed);
    return so;
}

SceneFrameSetup SetupFor(const SceneOptions& so, Device* device, Swapchain* swapchain)
{
    SceneFrameSetup fs;
    fs.device = device;
    fs.swapchain = swapchain;
    fs.max_objects = so.max_bodies + 16;
    fs.max_lights = 8;
    fs.font_path = PHYSICS_DEMO_FONT_FILE;
    return fs;
}

bool WritePpm(const char* path, u32 width, u32 height, const Vector<byte>& rgba)
{
    io::Path file;
    return io::Path::Parse(StringView(path), io::NativePathStyle(), &file) &&
           demo::WritePpm(file, width, height, rgba.Data()).Ok();
}

// The simulated time of a screenshot is `frames` steps of 1/60 s; the camera turns slowly.
constexpr f32 kStep = 1.0f / 60.0f;

int RunScreenshot(const Options& options, const RenderSettings& settings)
{
    char adapter_buffer[64];
    DeviceCreateInfo device_info;
    MakeDeviceCreateInfo(settings, DeviceInfo(), adapter_buffer, &device_info);
    DevicePtr device = CreateDevice(device_info);
    if (device == nullptr)
    {
        RhiLogV(LogVerbosity::Error, "physics_demo: no %s device", demo::BackendLabel(device_info.backend));
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
        Frame frame;
        PhysicsScene scene;
        const SceneOptions so = SceneOptionsFor(options);
        const SceneFrameSetup fs = SetupFor(so, device.Get(), nullptr);
        if (target.IsValid() && frame.Create(fs) &&
            scene.Create(device.Get(), frame.Materials(), frame.Templates(), so))
        {
            FrameTarget ft;
            ft.texture = target;
            ft.width = kShotWidth;
            ft.height = kShotHeight;
            ft.format = Format::RGBA8_SRGB;
            ft.initial = ResourceState::Undefined;
            ft.final_state = ResourceState::CopySrc;
            FrameStatus status = FrameStatus::Ok;
            const long frames = options.frames > 0 ? options.frames : 240;
            for (long i = 0; i < frames && status == FrameStatus::Ok; ++i)
            {
                const f32 seconds = static_cast<f32>(i) * kStep;
                scene.SetCameraAngle(0.6f + 0.15f * seconds);
                scene.Step(kStep);
                Camera camera = scene.Camera(static_cast<f32>(kShotWidth) / static_cast<f32>(kShotHeight));
                frame.SetTime(seconds, kStep);
                status = frame.Render(&scene.Scene(), &camera, &ft);
                ft.initial = ResourceState::CopySrc;
            }
            Vector<byte> pixels;
            pixels.Resize(static_cast<usize>(kShotWidth) * kShotHeight * 4);
            device->WaitIdle();
            if (status == FrameStatus::DeviceLost || device->DeviceLost())
            {
                RhiLogV(LogVerbosity::Error, "physics_demo: the GPU device was lost; stopping");
                exit_code = kExitDeviceLost;
            }
            else if (status != FrameStatus::Ok)
            {
                RhiLogV(LogVerbosity::Error, "physics_demo: a frame failed: %s", frame.LastError());
            }
            else if (scene.LowestBodyY() < -1.0f)
            {
                RhiLogV(LogVerbosity::Error, "physics_demo: a shape fell through the floor (y = %.2f)",
                        static_cast<double>(scene.LowestBodyY()));
            }
            else if (!device->ReadbackTexture(target, 0, 0, pixels.Data(), pixels.Size(), ResourceState::CopySrc))
            {
                RhiLogV(LogVerbosity::Error, "physics_demo: could not read the image back");
            }
            else if (!WritePpm(options.screenshot, kShotWidth, kShotHeight, pixels))
            {
                RhiLogV(LogVerbosity::Error, "physics_demo: could not write %s", options.screenshot);
            }
            else
            {
                RhiLogV(LogVerbosity::Info, "physics_demo: wrote %s (%u shapes, %u objects drawn, %llu physics steps)",
                        options.screenshot, scene.BodyCount(), scene.ObjectCount(),
                        static_cast<unsigned long long>(scene.PhysicsSteps()));
                exit_code = kExitOk;
            }
        }
        else
        {
            RhiLogV(LogVerbosity::Error, "physics_demo: could not create the target or the scene");
        }
        device->WaitIdle();
    }
    if (target.IsValid())
    {
        device->DestroyTexture(target);
    }
    device.Reset();
    return exit_code;
}

int RunWindow(const Options& options, const RenderSettings& settings)
{
    os::InputState input;
    os::PlatformWindowHost window_host;
    if (!window_host.Open(os::ParseWindowBackend(StringView(options.backend)), "physics_demo", 1280, 720, &input))
    {
        RhiLogV(LogVerbosity::Error, "physics_demo: could not open a window");
        return kExitFailure;
    }
    os::Window* window = window_host.Get();
    DeviceCreateInfo base = DeviceInfo();
    base.window = window;
    char adapter_buffer[64];
    DeviceCreateInfo info;
    MakeDeviceCreateInfo(settings, base, adapter_buffer, &info);
    DeviceResources registry;
    DeviceHost host;
    host.AddObserver(&registry, 0);
    if (!host.Create(info, MakeSwapchainDesc(settings)))
    {
        RhiLogV(LogVerbosity::Error, "physics_demo: no %s device can present to this window (%s)",
                demo::BackendLabel(info.backend), host.LastError());
        return kExitFailure;
    }
    int exit_code = kExitOk;
    {
        Frame frame;
        PhysicsScene scene;
        SceneOptions so = SceneOptionsFor(options);
        so.resources = &registry;
        SceneFrameSetup fs = SetupFor(so, host.GetDevice(), host.GetSwapchain());
        fs.resources = &registry;
        fs.host = &host;
        fs.create_info = base;
        fs.initial_settings = &settings;
        fs.window = window;
        if (!frame.Create(fs) || !scene.Create(host.GetDevice(), frame.Materials(), frame.Templates(), so))
        {
            RhiLogV(LogVerbosity::Error, "physics_demo: could not create the scene");
            host.GetDevice()->WaitIdle();
            exit_code = kExitFailure;
        }
        else
        {
            Renderer& renderer = *frame.GetRenderer();
            struct ResetParts
            {
                Frame* frame;
                PhysicsScene* scene;
            } parts{&frame, &scene};
            renderer.SetResetCallbacks([](void*, const char*) {},
                                       [](void* user, bool)
                                       {
                                           auto* p = static_cast<ResetParts*>(user);
                                           Renderer* r = p->frame->GetRenderer();
                                           if (r->GetDevice() != nullptr)
                                           {
                                               p->frame->Rebind(r->GetDevice(), r->GetSwapchain());
                                               p->scene->Rebind(r->GetDevice());
                                           }
                                       },
                                       &parts);
            RhiLogV(LogVerbosity::Info, "physics_demo keys: Space pause, Left/Right turn the camera");
            const double start = NowSeconds();
            double previous = start;
            double window_start = start;
            u64 window_frames = 0;
            long frames = 0;
            f32 angle = 0.6f;
            bool paused = options.paused;
            bool stop = false;
            bool previous_space = false;
            auto draw = [&]()
            {
                if (stop || (options.frames > 0 && frames >= options.frames))
                {
                    return;
                }
                const double now_time = NowSeconds();
                const f32 dt = static_cast<f32>(now_time - previous < 0.1 ? now_time - previous : 0.1);
                previous = now_time;
                const bool space = input.keyboard.IsDown(os::Key::Space);
                if (space && !previous_space)
                {
                    paused = !paused;
                }
                previous_space = space;
                if (input.keyboard.IsDown(os::Key::Left))
                {
                    angle -= dt;
                }
                if (input.keyboard.IsDown(os::Key::Right))
                {
                    angle += dt;
                }
                if (!paused && !input.keyboard.IsDown(os::Key::Left) && !input.keyboard.IsDown(os::Key::Right))
                {
                    angle += 0.08f * dt;  // the camera drifts around the floor
                }
                scene.SetCameraAngle(angle);
                scene.Step(paused ? 0.0f : dt);
                rhi::Swapchain* swapchain = renderer.GetSwapchain();
                const f32 aspect = swapchain != nullptr && swapchain->Height() > 0
                                       ? static_cast<f32>(swapchain->Width()) / static_cast<f32>(swapchain->Height())
                                       : 1.0f;
                Camera camera = scene.Camera(aspect);
                frame.SetTime(static_cast<f32>(now_time - start), dt);
                const FrameStatus status = frame.Render(&scene.Scene(), &camera, nullptr);
                if (status == FrameStatus::DeviceLost)
                {
                    if (renderer.IsBroken())
                    {
                        RhiLogV(LogVerbosity::Error, "physics_demo: the GPU device was lost; stopping");
                        exit_code = kExitDeviceLost;
                        stop = true;
                        return;
                    }
                    ++frames;
                    return;
                }
                if (status == FrameStatus::Failed)
                {
                    RhiLogV(LogVerbosity::Error, "physics_demo: a frame failed: %s", frame.LastError());
                    exit_code = kExitFailure;
                    stop = true;
                    return;
                }
                if (status == FrameStatus::Skipped)
                {
                    ++frames;
                    os::SleepNanos(10 * 1000 * 1000);
                    return;
                }
                ++frames;
                ++window_frames;
                const double now = NowSeconds();
                if (now - window_start >= 1.0)
                {
                    RhiLogV(LogVerbosity::Info, "physics_demo: %llu frames, %.2f ms/frame, %u shapes, %llu steps",
                            static_cast<unsigned long long>(window_frames),
                            (now - window_start) * 1000.0 / static_cast<double>(window_frames), scene.BodyCount(),
                            static_cast<unsigned long long>(scene.PhysicsSteps()));
                    window_start = now;
                    window_frames = 0;
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
            if (host.GetDevice() != nullptr)
            {
                host.GetDevice()->WaitIdle();
            }
            scene.Destroy();
            frame.Destroy();
        }
    }
    host.Destroy();
    return exit_code;
}

bool ParseCount(const char* text, long* out, long max = 0x7fffffffL)
{
    char* end = nullptr;
    const long value = CStrToLong(text, &end, 10);
    if (end == text || *end != '\0' || value < 0 || value > max)
    {
        return false;
    }
    *out = value;
    return true;
}

const char kUsageFormat[] =
    "usage: %s [--backend wayland|x11|win32|cocoa] [--frames N] [--screenshot out.ppm] [--shapes N] [--seed N] [--paused]\n"
    "  --backend B     display backend (default: automatic; same as ENGINE_WINDOW_BACKEND)\n"
    "  --frames N      stop after N frames\n"
    "  --screenshot P  simulate N frames (default 240) offscreen, no window, and write the last to P (a binary PPM)\n"
    "  --shapes N      the most shapes alive, 1 to 2000 (default 160); the oldest is replaced by a new one\n"
    "  --seed N        seeds the shapes, sizes, colors and places (default 1)\n"
    "  --paused        start with the simulation paused (Space toggles it)\n"
    "Keys: Space pause, Left/Right turn the camera.\n";

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

int EngineMain(int argc, char** argv)
{
    ::engine_placeholder::rhi::RegisterCompiledBackends();
    const engine_placeholder::debug::ProgramDebugSetup debug_setup = engine_placeholder::debug::StartProgramDebugging(
        argc, argv);
    engine_placeholder::debug::RunCrashTest(debug_setup.crash_test);
    LogRhi.SetVerbosity(LogVerbosity::Info);
    demo::DemoSettings demo_settings;
    char usage_text[2048];
    const os::CommandLine<HeapAllocator> native(HeapAllocator::Default(), argc, argv);
    FormatTo(usage_text, sizeof(usage_text), kUsageFormat, native.ProgramName().Data());
    if (!demo_settings.Begin("physics_demo", usage_text, argc, argv))
    {
        return kExitUsage;
    }
    g_demo_settings = &demo_settings;
    static const os::CommandLineOption kCliOptions[] = {{"frames", true},
                                                        {"screenshot", true},
                                                        {"backend", true},
                                                        {"shapes", true},
                                                        {"seed", true},
                                                        {"paused", false},
                                                        {"help", false},
                                                        {"-h", false},
                                                        engine_placeholder::debug::kCrashHandlerOption,
                                                        engine_placeholder::debug::kCrashTestOption};
    const os::CommandLine<HeapAllocator> line(HeapAllocator::Default(), demo_settings.RestCount(),
                                              demo_settings.RestArgs(),
                                              engine_placeholder::Span<const os::CommandLineOption>(kCliOptions, 10));
    const char* program = line.ProgramName().Data();
    Options options;
    if (line.HasFlag("help") || line.HasFlag("-h"))
    {
        return Usage(program, kExitOk);
    }
    if (!line.Ok() || line.ArgCount() != 0)
    {
        os::PrintError("%s: %s\n", program, line.Ok() ? "unexpected argument" : line.Error().Data());
        return Usage(program, kExitUsage);
    }
    StringView value;
    if (line.TryGetOption("frames", value) && !ParseCount(value.Data(), &options.frames))
    {
        return Usage(program, kExitUsage);
    }
    if (line.TryGetOption("screenshot", value))
    {
        options.screenshot = value.Data();
    }
    if (line.TryGetOption("shapes", value) && (!ParseCount(value.Data(), &options.bodies, 2000) || options.bodies < 1))
    {
        return Usage(program, kExitUsage);
    }
    if (line.TryGetOption("seed", value) && !ParseCount(value.Data(), &options.seed))
    {
        return Usage(program, kExitUsage);
    }
    options.paused = line.HasFlag("paused");
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
    static demo::DemoSettings::Effective effective;
    effective = demo_settings.ReadEffective();
    options.backend = effective.window_backend;
    RenderSettings settings = renderer::RenderSettingsFromSnapshot(demo_settings.GetRegistry(),
                                                                   *demo_settings.Get().Snapshot());
    if (options.screenshot != nullptr)
    {
        return RunScreenshot(options, settings);
    }
    return RunWindow(options, settings);
}

ENGINE_WINDOWED_PROGRAM(EngineMain)
