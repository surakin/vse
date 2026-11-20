// script_demo: a game written in Lua that uses every core (Phase 27 Day 475). A crowd of bodies wander, flock and flee,
// a player walks a square (the arrows or W, A, S, D take it over), pickups chime when it walks in, and a planner
// searches a field with `task.parallel_for`. In a window with an overlay (the script time, the speedup over one thread,
// the partitions' load and, on request, the hottest functions), offscreen with --screenshot, or headless with
// --headless, which prints the state hash: the same whatever --threads says. The script is the build's minted blob;
// edit its copy in the dev directory and the running game reloads it. See README.md.

#include "asset/cpu_loaders.h"
#include "log/output.h"
#include "asset/resource_manager.h"
#include "audio/audio_settings.h"
#include "audio/audio_system.h"
#include "containers/array.h"
#include "containers/unique_ptr.h"
#include "containers/vector.h"
#include "cstr/cstr.h"
#include "cstr/format.h"
#include "cstr/number.h"
#include "debug/program_setup.h"
#include "demo_view.h"
#include "demo_world.h"
#include "io/file_system.h"
#include "jobs/scheduler.h"
#include "os/command_line.h"
#include "os/console.h"
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
#include "script/script_settings.h"
#include "settings_main.h"
#include "string/string.h"
#include "textures.h"

using namespace vse;
// Using-declarations, not `using namespace rhi`: this file is merged with the world's in a unity build, where a
// namespace directive would make names such as `Span` ambiguous there.
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
using rhi::Device;
using rhi::DeviceCreateInfo;
using rhi::DeviceHost;
using rhi::DevicePtr;
using rhi::Format;
using rhi::LogRhi;
using rhi::ResourceState;
using rhi::RhiLogV;
using rhi::Swapchain;
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
constexpr f32 kStep = 1.0f / 60.0f;

struct Options
{
    long threads = 0;
    long partitions = 16;
    long entities = 2000;
    long frames = 0;  // 0: the mode's default (window: until closed; headless 600; screenshot 240)
    long report = 100;
    bool headless = false;
    bool built_in = false;  // the built-in copy of the script instead of the minted blob
    const char* screenshot = nullptr;
    const char* backend = "auto";
    const char* audio = "platform";
    String<HeapAllocator> dev_directory{HeapAllocator::Default()};
};

demo::DemoSettings* g_demo_settings = nullptr;

DeviceCreateInfo DeviceInfo()
{
    DeviceCreateInfo info;
    g_demo_settings->FillDeviceCreateInfo(&info);
    return info;
}

void RegisterProgramSettings(::vse::settings::Registry* registry)
{
    script::RegisterScriptSettings(registry);
    audio::RegisterAudioSettings(registry);
}

// The build's minted script, mounted at "/".
struct MintedScripts
{
    asset::TypeRegistry types;
    UniquePtr<asset::ResourceManager> manager;

    bool Open()
    {
        asset::ManagerConfig config;
        config.buffer_pool_bytes = u64{4} << 20;
        manager = MakeUnique<asset::ResourceManager>(types, config);
        asset::RegisterCpuLoaders(manager.Get());
        io::Path dir;
        if (!io::Path::Parse(StringView(SCRIPT_DEMO_MINTED_DIR), io::NativePathStyle(), &dir) ||
            !manager->MountPath(StringView("/"), dir, 0).Ok())
        {
            out::PrintError("script_demo: the minted script directory %s cannot be mounted (build the target "
                            "script_demo_minted, or run with --built-in)\n",
                            SCRIPT_DEMO_MINTED_DIR);
            return false;
        }
        return true;
    }
};

script_demo::DemoOptions WorldOptions(const Options& options, MintedScripts* minted)
{
    script_demo::DemoOptions demo;
    demo.entities = static_cast<u32>(options.entities);
    demo.partitions = static_cast<u32>(options.partitions);
    demo.minted = options.built_in ? nullptr : minted->manager.Get();
    return demo;
}

jobs::Scheduler::Config SchedulerConfig(const Options& options)
{
    jobs::Scheduler::Config config;
    config.participants = static_cast<usize>(options.threads);
    config.pin = false;
    return config;
}

// The headless run of Day 475: the hash every N frames, the same at any thread count.
int RunHeadless(const Options& options, MintedScripts* minted)
{
    jobs::Scheduler scheduler(SchedulerConfig(options));
    const script_demo::DemoOptions demo = WorldOptions(options, minted);
    script_demo::DemoWorld world(demo);
    if (!world.Ok())
    {
        return kExitFailure;
    }
    const long frames = options.frames > 0 ? options.frames : 600;
    out::Print("script_demo: %u bodies, %u partitions, %zu threads, the %s script\n", world.EntityCount(),
               demo.partitions, static_cast<size_t>(scheduler.ParticipantCount()),
               options.built_in ? "built-in" : "minted");
    for (long f = 1; f <= frames; ++f)
    {
        world.Frame(&scheduler);
        if (f % options.report == 0 || f == frames)
        {
            out::Print("frame %ld: scripts %.2f ms, physics %.2f ms, pickups %llu, errors %llu, hash %016llx\n", f,
                       world.LastScriptSeconds() * 1000.0, world.LastPhysicsSeconds() * 1000.0,
                       static_cast<unsigned long long>(world.PickupsCollected()),
                       static_cast<unsigned long long>(world.ScriptStats().errors),
                       static_cast<unsigned long long>(world.StateHash()));
        }
    }
    return world.ScriptStats().errors == 0 ? kExitOk : kExitFailure;
}

// What the overlay shows, measured as the game runs.
class Overlay
{
public:
    explicit Overlay(usize threads) : threads_(threads) {}

    // After each frame: the script time at the thread count it ran with, and the partitions' load.
    void Measure(script_demo::DemoWorld& world, bool one_thread)
    {
        const f64 ms = world.LastScriptSeconds() * 1000.0;
        f64& average = one_thread ? one_ms_ : many_ms_;
        average = average <= 0.0 ? ms : average * 0.9 + ms * 0.1;
        const f64 physics = world.LastPhysicsSeconds() * 1000.0;
        physics_ms_ = physics_ms_ <= 0.0 ? physics : physics_ms_ * 0.9 + physics * 0.1;
        world.Runtime().LastPartitionLoad(&load_);
        ++frames_;
        if (profiling_ && frames_ % 30 == 0)
        {
            hot_.Clear();
            world.Runtime().ProfileReport(&hot_, 3);
        }
    }

    void SetProfiling(script_demo::DemoWorld& world, bool on)
    {
        profiling_ = on;
        world.Runtime().SetProfiling(on);
        hot_.Clear();
    }
    bool Profiling() const { return profiling_; }

    void Text(const script_demo::DemoWorld& world, bool one_thread, String<HeapAllocator>* greeting,
              String<HeapAllocator>* status) const
    {
        Array<char, 160> line;
        FormatTo(line.Data(), line.Size(), "script_demo: %u Lua bodies on %zu thread%s", world.EntityCount(),
                 static_cast<size_t>(one_thread ? 1 : threads_), one_thread || threads_ == 1 ? "" : "s");
        *greeting = String<HeapAllocator>(HeapAllocator::Default(), StringView(line.Data()));
        status->Clear();
        const f64 now_ms = one_thread ? one_ms_ : many_ms_;
        if (one_ms_ > 0.0 && many_ms_ > 0.0)
        {
            FormatTo(line.Data(), line.Size(),
                     "scripts %.1f ms a frame; 1 thread %.1f ms, %zu threads %.1f ms: %.1fx\n", now_ms, one_ms_,
                     static_cast<size_t>(threads_), many_ms_, one_ms_ / many_ms_);
        }
        else
        {
            FormatTo(line.Data(), line.Size(), "scripts %.1f ms a frame (T: one thread, to compare)\n", now_ms);
        }
        status->Append(StringView(line.Data()));
        u64 busiest = 0;
        u64 sum = 0;
        u32 busy = 0;
        for (const script::PartitionLoad& l : load_)
        {
            busiest = l.nanoseconds > busiest ? l.nanoseconds : busiest;
            sum += l.nanoseconds;
            busy += l.instances != 0 ? 1u : 0u;
        }
        FormatTo(line.Data(), line.Size(),
                 "last frame: %u of %zu partitions busy, the busiest %.2f ms, mean %.2f ms; physics %.1f ms\n", busy,
                 static_cast<size_t>(load_.Size()), static_cast<f64>(busiest) * 1e-6,
                 busy != 0 ? static_cast<f64>(sum) * 1e-6 / static_cast<f64>(busy) : 0.0, physics_ms_);
        status->Append(StringView(line.Data()));
        FormatTo(line.Data(), line.Size(), "pickups %llu, script reloads %llu, errors %llu\n",
                 static_cast<unsigned long long>(world.PickupsCollected()),
                 static_cast<unsigned long long>(world.FileReloads()),
                 static_cast<unsigned long long>(world.ScriptStats().errors));
        status->Append(StringView(line.Data()));
        if (profiling_)
        {
            status->Append(StringView("hot:"));
            for (const script::ProfileEntry& e : hot_)
            {
                FormatTo(line.Data(), line.Size(), " %s:%u %.2f ms", e.name.CStr(), e.line,
                         static_cast<f64>(e.nanoseconds) * 1e-6 / 30.0);
                status->Append(StringView(line.Data()));
            }
            status->Append(StringView("\n"));
        }
        status->Append(StringView("arrows/WASD move, Q/E turn, T one thread, P profile, F1 overlay"));
    }

private:
    usize threads_;
    f64 one_ms_ = 0.0;
    f64 many_ms_ = 0.0;
    f64 physics_ms_ = 0.0;
    u64 frames_ = 0;
    bool profiling_ = false;
    Vector<script::PartitionLoad> load_;
    Vector<script::ProfileEntry> hot_;
};

SceneFrameSetup SetupFor(const script_demo::DemoWorld& world, Device* device, Swapchain* swapchain)
{
    SceneFrameSetup fs;
    fs.device = device;
    fs.swapchain = swapchain;
    fs.max_objects = static_cast<u32>(world.VisualCount()) + 8;
    fs.max_lights = 4;
    fs.font_path = SCRIPT_DEMO_FONT_FILE;
    return fs;
}

int RunScreenshot(const Options& options, const RenderSettings& settings, MintedScripts* minted)
{
    jobs::Scheduler scheduler(SchedulerConfig(options));
    script_demo::DemoWorld world(WorldOptions(options, minted));
    if (!world.Ok())
    {
        return kExitFailure;
    }
    Array<char, 64> adapter_buffer;
    DeviceCreateInfo device_info;
    MakeDeviceCreateInfo(settings, DeviceInfo(), adapter_buffer.Data(), &device_info);
    DevicePtr device = CreateDevice(device_info);
    if (device == nullptr)
    {
        RhiLogV(LogVerbosity::Error, "script_demo: no %s device", demo::BackendLabel(device_info.backend));
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
        script_demo::DemoView view;
        Overlay overlay(scheduler.ParticipantCount());
        if (target.IsValid() && frame.Create(SetupFor(world, device.Get(), nullptr)) &&
            view.Create(device.Get(), frame.Materials(), frame.Templates(), nullptr, world))
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
                world.Frame(&scheduler);
                world.PollReload();
                overlay.Measure(world, false);
                view.Sync(world);
                String<HeapAllocator> greeting(HeapAllocator::Default());
                String<HeapAllocator> text(HeapAllocator::Default());
                overlay.Text(world, false, &greeting, &text);
                frame.SetOverlay(true, greeting.View(), text.View());
                const f32 seconds = static_cast<f32>(i) * kStep;
                Camera camera = view.CameraAt(0.6f + 0.1f * seconds,
                                              static_cast<f32>(kShotWidth) / static_cast<f32>(kShotHeight));
                frame.SetTime(seconds, kStep);
                status = frame.Render(&view.Scene(), &camera, &ft);
                ft.initial = ResourceState::CopySrc;
            }
            Vector<byte> pixels;
            pixels.Resize(static_cast<usize>(kShotWidth) * kShotHeight * 4);
            device->WaitIdle();
            io::Path file;
            if (status == FrameStatus::DeviceLost || device->DeviceLost())
            {
                RhiLogV(LogVerbosity::Error, "script_demo: the GPU device was lost; stopping");
                exit_code = kExitDeviceLost;
            }
            else if (status != FrameStatus::Ok)
            {
                RhiLogV(LogVerbosity::Error, "script_demo: a frame failed: %s", frame.LastError());
            }
            else if (world.ScriptStats().errors != 0)
            {
                RhiLogV(LogVerbosity::Error, "script_demo: the scripts reported %llu errors",
                        static_cast<unsigned long long>(world.ScriptStats().errors));
            }
            else if (!device->ReadbackTexture(target, 0, 0, pixels.Data(), pixels.Size(), ResourceState::CopySrc))
            {
                RhiLogV(LogVerbosity::Error, "script_demo: could not read the image back");
            }
            else if (!io::Path::Parse(StringView(options.screenshot), io::NativePathStyle(), &file) ||
                     !demo::WritePpm(file, kShotWidth, kShotHeight, pixels.Data()).Ok())
            {
                RhiLogV(LogVerbosity::Error, "script_demo: could not write %s", options.screenshot);
            }
            else
            {
                RhiLogV(LogVerbosity::Info,
                        "script_demo: wrote %s (%u bodies, %u objects drawn, %llu frames, %llu script reloads, hash "
                        "%016llx)",
                        options.screenshot, world.EntityCount(), view.ObjectCount(),
                        static_cast<unsigned long long>(world.Frames()),
                        static_cast<unsigned long long>(world.FileReloads()),
                        static_cast<unsigned long long>(world.StateHash()));
                exit_code = kExitOk;
            }
        }
        else
        {
            RhiLogV(LogVerbosity::Error, "script_demo: could not create the target or the view");
        }
        device->WaitIdle();
        view.Destroy();
        frame.Destroy();
    }
    if (target.IsValid())
    {
        device->DestroyTexture(target);
    }
    device.Reset();
    return exit_code;
}

int RunWindow(const Options& options, const RenderSettings& settings, MintedScripts* minted)
{
    jobs::Scheduler scheduler(SchedulerConfig(options));
    // The sound: the platform's device, or the null one when it cannot open or --audio null asks.
    audio::AudioSystem sound;
    audio::AudioSystemConfig sound_config;
    sound_config.settings = audio::AudioSettingsFromRegistry(g_demo_settings->GetRegistry());
    sound_config.kind = CStrCompare(options.audio, "null") == 0 ? audio::DeviceKind::Null : audio::DeviceKind::Platform;
    sound.Init(sound_config, os::MonotonicTimeNanos());
    script_demo::DemoOptions demo = WorldOptions(options, minted);
    demo.input = true;
    demo.mixer = &sound.GetMixer();
    demo.sample_rate = sound.GetMixer().Config().sample_rate;
    script_demo::DemoWorld world(demo);
    if (!world.Ok())
    {
        sound.Shutdown();
        return kExitFailure;
    }
    if (options.dev_directory.Length() != 0)
    {
        if (world.WatchDirectory(options.dev_directory.View()))
        {
            RhiLogV(LogVerbosity::Info, "script_demo: edit %s/%s.lua and the game reloads it",
                    options.dev_directory.CStr(), script_demo::kDemoScriptKey);
        }
        else
        {
            RhiLogV(LogVerbosity::Warn, "script_demo: %s cannot be watched; no hot reload",
                    options.dev_directory.CStr());
        }
    }

    os::InputState input;
    os::PlatformWindowHost window_host;
    if (!window_host.Open(os::ParseWindowBackend(StringView(options.backend)), "script_demo", 1280, 720, &input))
    {
        RhiLogV(LogVerbosity::Error, "script_demo: could not open a window");
        sound.Shutdown();
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
        RhiLogV(LogVerbosity::Error, "script_demo: no %s device can present to this window (%s)",
                demo::BackendLabel(info.backend), host.LastError());
        sound.Shutdown();
        return kExitFailure;
    }
    int exit_code = kExitOk;
    {
        SceneFrame frame;
        script_demo::DemoView view;
        SceneFrameSetup fs = SetupFor(world, host.GetDevice(), host.GetSwapchain());
        fs.resources = &registry;
        fs.host = &host;
        fs.create_info = base;
        fs.initial_settings = &settings;
        fs.window = window;
        if (!frame.Create(fs) || !view.Create(host.GetDevice(), frame.Materials(), frame.Templates(), &registry, world))
        {
            RhiLogV(LogVerbosity::Error, "script_demo: could not create the view");
            host.GetDevice()->WaitIdle();
            exit_code = kExitFailure;
        }
        else
        {
            Renderer& renderer = *frame.GetRenderer();
            struct ResetParts
            {
                SceneFrame* frame;
                script_demo::DemoView* view;
            } parts{&frame, &view};
            renderer.SetResetCallbacks([](void*, const char*) {},
                                       [](void* user, bool)
                                       {
                                           auto* p = static_cast<ResetParts*>(user);
                                           Renderer* r = p->frame->GetRenderer();
                                           if (r->GetDevice() != nullptr)
                                           {
                                               p->frame->Rebind(r->GetDevice(), r->GetSwapchain());
                                               p->view->Rebind(r->GetDevice());
                                           }
                                       },
                                       &parts);
            frame.SetOverlayScale(window->ContentScale());
            RhiLogV(
                LogVerbosity::Info,
                "script_demo keys: arrows or W/A/S/D move the player, Q/E turn the camera, T runs the scripts on one "
                "thread (again: on all), P shows the hottest functions, F1 the overlay");
            Overlay overlay(scheduler.ParticipantCount());
            const f64 start = static_cast<f64>(os::MonotonicTimeNanos()) * 1e-9;
            f64 previous = start;
            long frames = 0;
            f32 angle = 0.6f;
            bool one_thread = false;
            bool overlay_on = true;
            bool stop = false;
            os::KeyboardState before;
            const auto pressed = [&](os::Key key) { return input.keyboard.IsDown(key) && !before.IsDown(key); };
            auto draw = [&]()
            {
                if (stop || (options.frames > 0 && frames >= options.frames))
                {
                    return;
                }
                const f64 now = static_cast<f64>(os::MonotonicTimeNanos()) * 1e-9;
                const f32 dt = static_cast<f32>(now - previous < 0.1 ? now - previous : 0.1);
                previous = now;
                if (pressed(os::Key::T))
                {
                    one_thread = !one_thread;
                }
                if (pressed(os::Key::P))
                {
                    overlay.SetProfiling(world, !overlay.Profiling());
                }
                if (pressed(os::Key::F1))
                {
                    overlay_on = !overlay_on;
                }
                angle += input.keyboard.IsDown(os::Key::E) ? dt : 0.0f;
                angle -= input.keyboard.IsDown(os::Key::Q) ? dt : 0.0f;
                before = input.keyboard;
                world.Frame(one_thread ? nullptr : &scheduler, &input);
                world.PollReload();
                sound.Update(os::MonotonicTimeNanos());
                overlay.Measure(world, one_thread);
                view.Sync(world);
                String<HeapAllocator> greeting(HeapAllocator::Default());
                String<HeapAllocator> text(HeapAllocator::Default());
                overlay.Text(world, one_thread, &greeting, &text);
                frame.SetOverlay(overlay_on, greeting.View(), text.View());
                rhi::Swapchain* swapchain = renderer.GetSwapchain();
                const f32 aspect = swapchain != nullptr && swapchain->Height() > 0
                                       ? static_cast<f32>(swapchain->Width()) / static_cast<f32>(swapchain->Height())
                                       : 1.0f;
                Camera camera = view.CameraAt(angle, aspect);
                frame.SetTime(static_cast<f32>(now - start), dt);
                const FrameStatus status = frame.Render(&view.Scene(), &camera, nullptr);
                if (status == FrameStatus::DeviceLost)
                {
                    if (renderer.IsBroken())
                    {
                        RhiLogV(LogVerbosity::Error, "script_demo: the GPU device was lost; stopping");
                        exit_code = kExitDeviceLost;
                        stop = true;
                        return;
                    }
                }
                else if (status == FrameStatus::Failed)
                {
                    RhiLogV(LogVerbosity::Error, "script_demo: a frame failed: %s", frame.LastError());
                    exit_code = kExitFailure;
                    stop = true;
                    return;
                }
                else if (status == FrameStatus::Skipped)
                {
                    os::SleepNanos(10 * 1000 * 1000);
                }
                ++frames;
                if (frames % 300 == 0)
                {
                    RhiLogV(LogVerbosity::Info, "script_demo: frame %ld, scripts %.2f ms, pickups %llu, hash %016llx",
                            frames, world.LastScriptSeconds() * 1000.0,
                            static_cast<unsigned long long>(world.PickupsCollected()),
                            static_cast<unsigned long long>(world.StateHash()));
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
            if (exit_code == kExitOk && world.ScriptStats().errors != 0)
            {
                RhiLogV(LogVerbosity::Error, "script_demo: the scripts reported %llu errors",
                        static_cast<unsigned long long>(world.ScriptStats().errors));
                exit_code = kExitFailure;
            }
            if (exit_code == kExitOk)
            {
                RhiLogV(LogVerbosity::Info, "script_demo: ran %ld frames in a window, %llu pickups, hash %016llx",
                        frames, static_cast<unsigned long long>(world.PickupsCollected()),
                        static_cast<unsigned long long>(world.StateHash()));
            }
            if (host.GetDevice() != nullptr)
            {
                host.GetDevice()->WaitIdle();
            }
            view.Destroy();
            frame.Destroy();
        }
    }
    host.Destroy();
    sound.Shutdown();
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
    "usage: %s [--threads N] [--partitions P] [--entities N] [--frames N] [--headless [--report N]]\n"
    "          [--screenshot out.ppm] [--backend wayland|x11|win32|cocoa] [--audio platform|null] [--built-in]\n"
    "  --threads N      worker threads (default: the machine's); T in the window runs the scripts on one\n"
    "  --partitions P   script partitions (default 16)\n"
    "  --entities N     bodies with a behavior (default 2000)\n"
    "  --frames N       stop after N frames (window: until closed; --headless 600; --screenshot 240)\n"
    "  --headless       no window: print the state hash every --report frames (default 100)\n"
    "  --screenshot P   run N frames offscreen and write the last picture to P (a binary PPM)\n"
    "  --backend B      display backend (default: automatic; same as ENGINE_WINDOW_BACKEND)\n"
    "  --audio A        the sound device: platform (default; the null device when it cannot open) or null\n"
    "  --built-in       run the built-in copy of the script instead of the build's minted blob\n"
    "Hot reload: --script-dev-directory DIR (the setting script.dev_directory; default: the build's dev directory)\n"
    "is watched for %s.lua.\n"
    "Keys: arrows or W/A/S/D move the player, Q/E turn the camera, T one thread, P profile, F1 overlay.\n";

int Usage(const char* program, int code)
{
    if (code == kExitOk)
    {
        out::Print(kUsageFormat, program, script_demo::kDemoScriptKey);
    }
    else
    {
        out::PrintError(kUsageFormat, program, script_demo::kDemoScriptKey);
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
    Array<char, 2048> usage_text;
    const os::CommandLine<HeapAllocator> native(HeapAllocator::Default(), argc, argv);
    FormatTo(usage_text.Data(), usage_text.Size(), kUsageFormat, native.ProgramName().Data(),
             script_demo::kDemoScriptKey);
    if (!demo_settings.Begin("script_demo", usage_text.Data(), argc, argv, &RegisterProgramSettings))
    {
        return kExitUsage;
    }
    g_demo_settings = &demo_settings;
    static const Array<os::CommandLineOption, 14> kCliOptions = {os::CommandLineOption{"threads", true},
                                                                 os::CommandLineOption{"partitions", true},
                                                                 os::CommandLineOption{"entities", true},
                                                                 os::CommandLineOption{"frames", true},
                                                                 os::CommandLineOption{"report", true},
                                                                 os::CommandLineOption{"headless", false},
                                                                 os::CommandLineOption{"screenshot", true},
                                                                 os::CommandLineOption{"backend", true},
                                                                 os::CommandLineOption{"audio", true},
                                                                 os::CommandLineOption{"built-in", false},
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
        out::PrintError("%s: %s\n", program, line.Ok() ? "unexpected argument" : line.Error().Data());
        return Usage(program, kExitUsage);
    }
    Options options;
    options.threads = static_cast<long>(jobs::DefaultParticipantCount());
    StringView value;
    if ((line.TryGetOption("threads", value) && !ParseCount(value, 1, 256, &options.threads)) ||
        (line.TryGetOption("partitions", value) && !ParseCount(value, 1, 1024, &options.partitions)) ||
        (line.TryGetOption("entities", value) && !ParseCount(value, 1, 200000, &options.entities)) ||
        (line.TryGetOption("frames", value) && !ParseCount(value, 1, 10000000, &options.frames)) ||
        (line.TryGetOption("report", value) && !ParseCount(value, 1, 10000000, &options.report)))
    {
        out::PrintError("%s: a number is out of range\n", program);
        return Usage(program, kExitUsage);
    }
    options.headless = line.HasFlag("headless");
    options.built_in = line.HasFlag("built-in");
    if (line.TryGetOption("screenshot", value))
    {
        options.screenshot = value.Data();
    }
    if (line.TryGetOption("audio", value))
    {
        if (value != StringView("platform") && value != StringView("null"))
        {
            out::PrintError("%s: --audio is platform or null\n", program);
            return Usage(program, kExitUsage);
        }
        options.audio = value.Data();
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
    static demo::DemoSettings::Effective effective;
    effective = demo_settings.ReadEffective();
    options.backend = effective.window_backend;
    options.dev_directory = script::ScriptDevDirectory(demo_settings.GetRegistry());
    if (options.dev_directory.Length() == 0)
    {
        options.dev_directory = String<HeapAllocator>(HeapAllocator::Default(), StringView(SCRIPT_DEMO_DEV_DIR));
    }
    MintedScripts minted;
    if (!options.built_in && !minted.Open())
    {
        return kExitFailure;
    }
    if (options.headless)
    {
        return RunHeadless(options, &minted);
    }
    const RenderSettings settings = renderer::RenderSettingsFromSnapshot(demo_settings.GetRegistry(),
                                                                         *demo_settings.Get().Snapshot());
    if (options.screenshot != nullptr)
    {
        return RunScreenshot(options, settings, &minted);
    }
    return RunWindow(options, settings, &minted);
}

ENGINE_WINDOWED_PROGRAM(EngineMain)
