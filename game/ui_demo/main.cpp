// ui_demo: the Phase 15 showcase. A resizable window of the engine's own widgets (menu bar, toolbar, panels, a
// 1,000-item list, a splitter) around a viewport that shows the Phase 5 lit scene, in a window (Wayland, X11 or Win32)
// or offscreen (--screenshot). See README.md for the options and the script words.

#include "os/console.h"
#include "lang/scalar.h"

#include "cstr/number.h"
#include "cstr/cstr.h"

#include "containers/vector.h"
#include "debug/program_setup.h"
#include "demo_frame.h"
#include "demo_scene.h"
#include "io/path.h"
#include "os/os.h"
#include "platform/entry_point.h"
#include "platform/input.h"
#include "platform/window_host.h"
#include "renderer/camera.h"
#include "renderer/device_resources.h"
#include "renderer/render_settings.h"
#include "renderer/renderer.h"
#include "renderer/shared_settings.h"
#include "rhi/adapter.h"
#include "rhi/backend_registry.h"
#include "rhi/backends/backends.h"
#include "rhi/device_host.h"
#include "rhi/rhi.h"
#include "rhi/rhi_log.h"
#include "settings_main.h"
#include "textures.h"
#include "ui/surface.h"
#include "ui_demo_app.h"
#include "ui_demo_gpu.h"
#include "ui_script.h"
#include "ui_window/os_clipboard.h"
#include "ui_window/window_adapter.h"
#include "cstr/format.h"
#include "cstr/mem_ops.h"

using namespace engine_placeholder;
using namespace engine_placeholder::rhi;
using namespace engine_placeholder::renderer;

namespace
{

constexpr int kExitOk = 0;
constexpr int kExitFailure = 1;
constexpr int kExitUsage = 2;
constexpr int kExitDeviceLost = 3;

constexpr f32 kShotWidth = 960.0f;  // logical units of the offscreen surface
constexpr f32 kShotHeight = 600.0f;
constexpr u32 kSceneObjects = 64;
constexpr u32 kSceneLights = 8;

struct Options
{
    long frames = 0;                   // 0: until the window closes
    const char* screenshot = nullptr;  // render offscreen to this PPM and exit
    const char* backend = "auto";
    bool no_vsync = false;
    bool validation = false;
    const char* gpu = nullptr;
    const char* script = nullptr;
    f32 scale = 0.0f;  // 0: the window's scale (1 for --screenshot)
};

double NowSeconds()
{
    return static_cast<double>(os::MonotonicTimeNanos()) * 1e-9;
}

demo::DemoSettings* g_demo_settings = nullptr;

bool WritePpmFile(const char* path, u32 width, u32 height, const Vector<byte>& rgba)
{
    io::Path file;
    return io::Path::Parse(StringView(path), io::NativePathStyle(), &file) &&
           demo::WritePpm(file, width, height, rgba.Data()).Ok();
}

void LogApply(const char* what, const ApplyResult& r)
{
    if (r.queued)
    {
        RhiLogV(LogVerbosity::Info, "ui_demo: %s: queued for the next frame", what);
    }
    else if (r.ok)
    {
        RhiLogV(LogVerbosity::Info, "ui_demo: %s: %s done in %.2f ms", what, CostClassName(r.performed), r.total_ms);
    }
    else
    {
        RhiLogV(LogVerbosity::Error, "ui_demo: %s: %s FAILED (%s)%s", what, CostClassName(r.performed), r.reason,
                r.rolled_back ? ", rolled back" : "");
    }
}

bool NextAdapterId(const Device& device, char* out, usize capacity)
{
    const char* current_id = device.Adapter().id;
    AdapterList list;
    if (!device.ListAdapters(&list) || list.count == 0)
    {
        return false;
    }
    u32 at = 0;
    for (u32 i = 0; i < list.count; ++i)
    {
        if (CStrCompare(list.items[i].id, current_id) == 0)
        {
            at = i;
        }
    }
    for (u32 step = 1; step <= list.count; ++step)
    {
        const AdapterInfo& a = list.items[(at + step) % list.count];
        if (a.suitable)
        {
            FormatTo(out, capacity, "%s", a.id);
            return true;
        }
    }
    return false;
}

// Everything one run owns that the two modes share: the surface and the tree, the frame and the scene, the GPU parts
// and the script.
class Run final : public ui_demo::ScriptHost
{
public:
    Run(const Options& options, Renderer* (*renderer_of)(void*), void* renderer_user)
        : options_(options), renderer_of_(renderer_of), renderer_user_(renderer_user)
    {
    }
    void Resize(u32 width, u32 height) override
    {
        wanted_w_ = width;
        wanted_h_ = height;
    }
    void Screenshot(StringView path) override
    {
        shot_.Resize(path.Length() + 1);
        MemCopy(shot_.Data(), path.Data(), path.Length());
        shot_[path.Length()] = '\0';
    }
    bool Other(StringView word) override
    {
        Renderer* renderer = renderer_of_(renderer_user_);
        if (renderer == nullptr)
        {
            return false;
        }
        char text[256];
        const usize n = word.Length() < sizeof(text) - 1 ? word.Length() : sizeof(text) - 1;
        MemCopy(text, word.Data(), n);
        text[n] = '\0';
        if (CStrCompareN(text, "set:", 4) == 0)
        {
            RenderSettings wanted = renderer->Settings();
            const char* eq = CStrFindChar(text + 4, '=');
            if (eq == nullptr)
            {
                return false;
            }
            char key[64];
            if (static_cast<usize>(eq - (text + 4)) >= sizeof(key))
            {
                return false;
            }
            MemCopy(key, text + 4, static_cast<usize>(eq - (text + 4)));
            key[eq - (text + 4)] = '\0';
            const SettingId id = wanted.Registry().FindByName(key);
            if (!id.IsValid())
            {
                return false;
            }
            const SetStatus status = wanted.SetFromString(id, eq + 1);
            if (status != SetStatus::Ok && status != SetStatus::Clamped)
            {
                return false;
            }
            ApplyWanted(text, wanted, renderer);
            return true;
        }
        if (CStrCompare(text, "reset") == 0)
        {
            LogApply("reset", renderer->ForceDeviceReset());
            return true;
        }
        if (CStrCompare(text, "loss") == 0)
        {
            if (renderer->GetDevice() != nullptr)
            {
                renderer->GetDevice()->InjectDeviceLoss();
            }
            return true;
        }
        if (CStrCompare(text, "gpu:next") == 0)
        {
            char id[64];
            if (renderer->GetDevice() == nullptr || !NextAdapterId(*renderer->GetDevice(), id, sizeof(id)))
            {
                return true;
            }
            RenderSettings wanted = renderer->Settings();
            wanted.SetText(renderer::settings::kAdapter, id);
            ApplyWanted(text, wanted, renderer);
            return true;
        }
        return false;
    }
    // Through the settings system, as the renderer demo does.
    // Returns whether the change was accepted and applied: a change the settings system refuses leaves the renderer's
    // last-apply record as it was, which says nothing about this one.
    static bool ApplyWanted(const char* what, const RenderSettings& wanted, Renderer* renderer)
    {
        Vector<engine_placeholder::settings::ChangeRequest> requests;
        renderer::RenderSettingsToChanges(wanted, g_demo_settings->GetRegistry(), &requests);
        g_demo_settings->Get().SetCapabilityCheck(&renderer::RendererCapability, renderer);
        const engine_placeholder::settings::SetResult set = g_demo_settings->Get().Apply(
            requests.Data(), requests.Size(), &renderer::ApplySharedChange, renderer);
        if (!set.Ok())
        {
            RhiLogV(LogVerbosity::Warn, "ui_demo: %s: the change was refused (error %d)", what,
                    static_cast<int>(set.error));
            return false;
        }
        if (set.changed == 0)
        {
            LogApply(what, renderer->ApplySettings(wanted));
        }
        else
        {
            LogApply(what, renderer->LastApply());
        }
        return renderer->LastApply().ok;
    }

    const Options& options_;
    Renderer* (*renderer_of_)(void*);
    void* renderer_user_;
    u32 wanted_w_ = 0;
    u32 wanted_h_ = 0;
    Vector<char> shot_;
};

// The rendering settings dialog's side: a change goes through the settings system to the renderer, and the cost class
// is read from the registry.
class SettingsGlue final : public ui_demo::RenderSettingsHost
{
public:
    explicit SettingsGlue(Renderer* renderer) : renderer_(renderer) {}
    bool Change(StringView key, StringView value) override
    {
        char k[48];
        char v[48];
        FormatTo(k, sizeof(k), "%.*s", static_cast<int>(key.Length()), key.Data());
        FormatTo(v, sizeof(v), "%.*s", static_cast<int>(value.Length()), value.Data());
        RenderSettings wanted = renderer_->Settings();
        const SettingId id = wanted.Registry().FindByName(k);
        if (!id.IsValid())
        {
            return false;
        }
        const SetStatus status = wanted.SetFromString(id, v);
        if (status != SetStatus::Ok && status != SetStatus::Clamped)
        {
            return false;
        }
        return Run::ApplyWanted(k, wanted, renderer_);
    }
    StringView CostOf(StringView key) override
    {
        char k[48];
        FormatTo(k, sizeof(k), "%.*s", static_cast<int>(key.Length()), key.Data());
        RenderSettings current = renderer_->Settings();
        const SettingId id = current.Registry().FindByName(k);
        const SettingDesc* desc = id.IsValid() ? current.Registry().Find(id) : nullptr;
        return desc != nullptr ? StringView(CostClassName(desc->cost)) : StringView("unknown");
    }

private:
    Renderer* renderer_;
};

Renderer* RendererOf(void* user)
{
    return static_cast<renderer_demo::DemoFrame*>(user)->GetRenderer();
}

renderer_demo::FrameSetup SetupFor(Device* device, Swapchain* swapchain)
{
    renderer_demo::FrameSetup fs;
    fs.device = device;
    fs.swapchain = swapchain;
    fs.max_objects = kSceneObjects + 17;
    fs.max_lights = kSceneLights + 2;
    return fs;
}

renderer_demo::SceneOptions SceneOptionsFor()
{
    renderer_demo::SceneOptions so;
    so.objects = kSceneObjects;
    so.lights = kSceneLights;
    so.asset_dir = RENDERER_DEMO_ASSET_DIR;
    so.screen = true;
    so.screen_center = Vec3(ui_demo::kWorldScreenCenter[0], ui_demo::kWorldScreenCenter[1],
                            ui_demo::kWorldScreenCenter[2]);
    so.screen_width = ui_demo::kWorldScreenWidth;
    so.screen_height = ui_demo::kWorldScreenHeight;
    return so;
}

// Posts a script word's effects for the frame: the shared loop body of both modes. Returns false when the script
// failed.
bool RunScript(ui_demo::UiScript* script, ui_demo::UiDemoApp* app, ui::Surface* surface, Run* run, long frame)
{
    if (script->Done())
    {
        return true;
    }
    if (!script->Run(app, surface, run, frame))
    {
        RhiLogV(LogVerbosity::Error, "ui_demo: %s", script->LastError().Data());
        return false;
    }
    return true;
}

// Renders `frames` frames offscreen with the script, writing every `shot:` and the last picture to
// `options.screenshot`.
int RunScreenshot(const Options& options, const RenderSettings& settings)
{
    char adapter_buffer[64];
    DeviceCreateInfo base;
    g_demo_settings->FillDeviceCreateInfo(&base);
    DeviceCreateInfo device_info;
    MakeDeviceCreateInfo(settings, base, adapter_buffer, &device_info);
    DevicePtr device = CreateDevice(device_info);
    if (device == nullptr)
    {
        RhiLogV(LogVerbosity::Error, "ui_demo: no %s device", demo::BackendLabel(device_info.backend));
        return kExitFailure;
    }
    DeviceResources registry;
    const f32 shot_scale = options.scale > 0.0f ? options.scale : 1.0f;
    u32 pixel_w = static_cast<u32>(LRound(kShotWidth * shot_scale));
    u32 pixel_h = static_cast<u32>(LRound(kShotHeight * shot_scale));
    TextureHandle target;
    auto make_target = [&]
    {
        if (target.IsValid())
        {
            device->WaitIdle();
            device->DestroyTexture(target);
        }
        TextureDesc td;
        td.width = pixel_w;
        td.height = pixel_h;
        td.format = Format::RGBA8_SRGB;
        td.usage = TextureUsage::ColorAttachment | TextureUsage::CopySrc;
        target = device->CreateTexture(td);
        return target.IsValid();
    };
    int exit_code = kExitFailure;
    if (make_target())
    {
        renderer_demo::DemoFrame frame;
        renderer_demo::DemoScene scene;
        ui_demo::UiDemoGpu gpu;
        ui::Surface surface(kShotWidth, kShotHeight, shot_scale);
        renderer_demo::FrameSetup fs = SetupFor(device.Get(), nullptr);
        fs.resources = &registry;
        renderer_demo::SceneOptions so = SceneOptionsFor();
        so.resources = &registry;
        if (frame.Create(fs) && scene.Create(device.Get(), frame.Materials(), frame.Templates(), so) &&
            gpu.Create(device.Get(), &registry, &frame, &scene, UI_DEMO_FONT_FILE, Format::RGBA8_SRGB))
        {
            ui_demo::UiDemoApp app(&surface);
            gpu.Attach(&app, &surface);
            Run run(options, &RendererOf, &frame);
            ui_demo::UiScript script;
            if (options.script != nullptr)
            {
                script.Parse(StringView(options.script));
            }
            FrameTarget ft;
            ft.texture = target;
            ft.width = pixel_w;
            ft.height = pixel_h;
            ft.format = Format::RGBA8_SRGB;
            ft.initial = ResourceState::Undefined;
            ft.final_state = ResourceState::CopySrc;
            FrameStatus status = FrameStatus::Ok;
            bool ok = true;
            bool wrote_last = false;
            const long total = options.frames > 0 ? options.frames : 3;
            auto write_picture = [&](const char* path)
            {
                Vector<byte> pixels;
                pixels.Resize(static_cast<usize>(pixel_w) * pixel_h * 4);
                device->WaitIdle();
                if (!device->ReadbackTexture(target, 0, 0, pixels.Data(), pixels.Size(), ResourceState::CopySrc) ||
                    !WritePpmFile(path, pixel_w, pixel_h, pixels))
                {
                    RhiLogV(LogVerbosity::Error, "ui_demo: could not write %s", path);
                    return false;
                }
                RhiLogV(LogVerbosity::Info, "ui_demo: wrote %s (%ux%u)", path, pixel_w, pixel_h);
                return true;
            };
            for (long i = 0; i < total && ok && status == FrameStatus::Ok && !app.QuitRequested(); ++i)
            {
                ok = RunScript(&script, &app, &surface, &run, i);
                if (run.wanted_w_ != 0)
                {  // resize:WxH
                    surface.Resize(static_cast<f32>(run.wanted_w_), static_cast<f32>(run.wanted_h_), shot_scale);
                    pixel_w = static_cast<u32>(LRound(static_cast<f32>(run.wanted_w_) * shot_scale));
                    pixel_h = static_cast<u32>(LRound(static_cast<f32>(run.wanted_h_) * shot_scale));
                    run.wanted_w_ = run.wanted_h_ = 0;
                    ok = ok && make_target();
                    ft.texture = target;
                    ft.width = pixel_w;
                    ft.height = pixel_h;
                    ft.initial = ResourceState::Undefined;
                }
                gpu.Update(0.016f);
                Camera camera = gpu.MakeCamera();
                frame.SetTime(0.5f, 0.016f);
                status = frame.Render(&scene.Scene(), &camera, &ft);
                ft.initial = ResourceState::CopySrc;
                if (ok && run.shot_.Size() != 0)
                {
                    ok = write_picture(run.shot_.Data());
                    if (options.screenshot != nullptr && CStrCompare(run.shot_.Data(), options.screenshot) == 0)
                    {
                        wrote_last = true;
                    }
                    run.shot_.Clear();
                }
            }
            device->WaitIdle();
            if (options.script != nullptr && !script.Done() && !app.QuitRequested() && ok)
            {
                RhiLogV(
                    LogVerbosity::Warn,
                    "ui_demo: the script has words left after %ld frames (raise --frames or lower its frame: words)",
                    total);
            }
            if (status == FrameStatus::DeviceLost || device->DeviceLost())
            {
                RhiLogV(LogVerbosity::Error, "ui_demo: the GPU device was lost; stopping");
                exit_code = kExitDeviceLost;
            }
            else if (status != FrameStatus::Ok || !ok)
            {
                RhiLogV(LogVerbosity::Error, "ui_demo: a frame failed: %s", frame.LastError());
            }
            else if (options.screenshot != nullptr && !wrote_last && !write_picture(options.screenshot))
            {
                exit_code = kExitFailure;
            }
            else
            {
                exit_code = kExitOk;
            }
            device->WaitIdle();
            gpu.Destroy();
            scene.Destroy();
            frame.Destroy();
        }
        else
        {
            RhiLogV(LogVerbosity::Error, "ui_demo: could not create the frame, the scene or the UI renderer");
            device->WaitIdle();
        }
    }
    if (target.IsValid())
    {
        device->DestroyTexture(target);
    }
    device.Reset();
    return exit_code;
}

int RunWindow(const Options& options, RenderSettings settings)
{
    os::InputState input;
    os::PlatformWindowHost window_host;
    constexpr u32 kStartWidth = 1280;
    constexpr u32 kStartHeight = 720;
    if (!window_host.Open(os::ParseWindowBackend(StringView(options.backend)), "ui_demo", kStartWidth, kStartHeight,
                          &input))
    {
        RhiLogV(LogVerbosity::Error, "ui_demo: could not open a window");
        return kExitFailure;
    }
    os::Window* window = window_host.Get();
    DeviceCreateInfo base;
    g_demo_settings->FillDeviceCreateInfo(&base);
    base.window = window;
    char adapter_buffer[64];
    DeviceCreateInfo info;
    MakeDeviceCreateInfo(settings, base, adapter_buffer, &info);
    DeviceResources registry;
    DeviceHost host;
    host.AddObserver(&registry, 0);
    if (!host.Create(info, MakeSwapchainDesc(settings)))
    {
        RhiLogV(LogVerbosity::Error, "ui_demo: no %s device can present to this window (%s)",
                demo::BackendLabel(info.backend), host.LastError());
        return kExitFailure;
    }
    int exit_code = kExitOk;
    {
        renderer_demo::DemoFrame frame;
        renderer_demo::DemoScene scene;
        ui_demo::UiDemoGpu gpu;
        const f32 scale = options.scale > 0.0f ? options.scale : window->ContentScale();
        ui::Surface surface(static_cast<f32>(window->Width()) / scale, static_cast<f32>(window->Height()) / scale,
                            scale);
        renderer_demo::FrameSetup fs = SetupFor(host.GetDevice(), host.GetSwapchain());
        fs.resources = &registry;
        fs.host = &host;
        fs.create_info = base;
        fs.initial_settings = &settings;
        fs.window = window;
        renderer_demo::SceneOptions so = SceneOptionsFor();
        so.resources = &registry;
        if (!frame.Create(fs) || !scene.Create(host.GetDevice(), frame.Materials(), frame.Templates(), so) ||
            !gpu.Create(host.GetDevice(), &registry, &frame, &scene, UI_DEMO_FONT_FILE,
                        host.GetSwapchain()->ImageFormat()))
        {
            RhiLogV(LogVerbosity::Error, "ui_demo: could not create the frame, the scene or the UI renderer");
            host.GetDevice()->WaitIdle();
            exit_code = kExitFailure;
        }
        else
        {
            Renderer& renderer = *frame.GetRenderer();
            struct ResetParts
            {
                renderer_demo::DemoFrame* frame;
                renderer_demo::DemoScene* scene;
            } parts{&frame, &scene};
            renderer.SetResetCallbacks(
                [](void*, const char*) {},
                [](void* user, bool ok)
                {
                    auto* p = static_cast<ResetParts*>(user);
                    Renderer* r = p->frame->GetRenderer();
                    if (r->GetDevice() != nullptr)
                    {
                        p->frame->Rebind(r->GetDevice(), r->GetSwapchain());
                        p->scene->Rebind(r->GetDevice());
                    }
                    if (!ok)
                    {
                        RhiLogV(LogVerbosity::Warn, "ui_demo: the change did not work; the previous settings are back");
                    }
                },
                &parts);
            ui_demo::UiDemoApp app(&surface);
            gpu.Attach(&app, &surface);
            SettingsGlue settings_glue(&renderer);
            app.SetRenderSettingsHost(&settings_glue);
            ui::WindowAdapter adapter;
            adapter.SetFollowWindowScale(options.scale == 0.0f);
            ui::OsClipboard clipboard(window);
            ui::WindowPointerLock pointer_lock(window);
            surface.SetClipboard(&clipboard);
            adapter.SetClipboard(&clipboard);
            surface.SetPointerLockHost(&pointer_lock);
            adapter.EnableDrops(*window);
            Run run(options, &RendererOf, &frame);
            ui_demo::UiScript script;
            if (options.script != nullptr)
            {
                script.Parse(StringView(options.script));
            }
            const double start = NowSeconds();
            double previous = start;
            long frames = 0;
            bool stop = false;
            auto draw = [&]()
            {
                if (stop || (options.frames > 0 && frames >= options.frames))
                {
                    return;
                }
                const double now = NowSeconds();
                const f32 dt = static_cast<f32>(now - previous < 0.1 ? now - previous : 0.1);
                previous = now;
                adapter.PumpInto(*window, surface);
                if (!RunScript(&script, &app, &surface, &run, frames))
                {
                    exit_code = kExitFailure;
                    stop = true;
                    return;
                }
                if (run.wanted_w_ != 0)
                {  // a window cannot be resized from here on every display backend
                    RhiLogV(LogVerbosity::Warn, "ui_demo: resize:%ux%u is for --screenshot; the window keeps its size",
                            run.wanted_w_, run.wanted_h_);
                    run.wanted_w_ = run.wanted_h_ = 0;
                }
                gpu.Update(dt);
                adapter.UpdateCursor(*window, surface);
                const Camera camera = gpu.MakeCamera();
                frame.SetTime(static_cast<f32>(now - start), dt);
                Camera mutable_camera = camera;
                const FrameStatus status = frame.Render(&scene.Scene(), &mutable_camera, nullptr);
                if (status == FrameStatus::DeviceLost)
                {
                    if (renderer.IsBroken())
                    {
                        RhiLogV(LogVerbosity::Error, "ui_demo: the GPU device was lost; stopping");
                        exit_code = kExitDeviceLost;
                        stop = true;
                        return;
                    }
                    RhiLogV(LogVerbosity::Warn, "ui_demo: the GPU device was lost; recovering");
                }
                else if (status == FrameStatus::Failed)
                {
                    RhiLogV(LogVerbosity::Error, "ui_demo: a frame failed: %s", frame.LastError());
                    exit_code = kExitFailure;
                    stop = true;
                    return;
                }
                else if (status == FrameStatus::Skipped)
                {
                    os::SleepNanos(10 * 1000 * 1000);
                }
                ++frames;
            };
            using Draw = decltype(draw);
            window->SetModalLoopCallback([](void* user) { (*static_cast<Draw*>(user))(); }, &draw);
            while (!stop && !app.QuitRequested() && !window->ShouldClose() &&
                   (options.frames <= 0 || frames < options.frames) &&
                   !(script.Done() && options.script != nullptr && options.frames <= 0))
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
            gpu.Destroy();
            scene.Destroy();
            frame.Destroy();
        }
    }
    host.Destroy();
    return exit_code;
}

const char kUsageFormat[] =
    "usage: %s [--backend wayland|x11|win32|cocoa] [--frames N] [--screenshot out.ppm] [--script WORDS] [--scale 1|1.25|2] [--no-vsync] [--validation] [--gpu ID|INDEX|NAME]\n"
    "  --backend B       display backend (default: automatic; same as ENGINE_WINDOW_BACKEND)\n"
    "  --frames N        stop after N frames\n"
    "  --screenshot P    render offscreen, no window, and write the last frame to P (a binary PPM, 960x600 logical units)\n"
    "  --script W        run steps without a person: frame:N click:name type:text key:Name resize:WxH theme:dark|light menu:File/About drag:name:dx,dy\n"
    "                    drop:text:T drop:file:P paste:T shot:path set:key=value reset loss gpu:next quit (space separated; see README.md)\n"
    "  --scale S         the pixels per logical unit: 1, 1.25 or 2 (default: the display's, 1 with --screenshot)\n"
    "  --no-vsync        present without waiting for the display\n"
    "  --validation      enable the validation layer with synchronization validation\n"
    "  --gpu G           the GPU to use: an id, an index, or part of the name\n";

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

bool ParseCount(const char* text, long* out)
{
    char* end = nullptr;
    const long v = CStrToLong(text, &end, 10);
    if (end == text || *end != '\0' || v < 0 || v > 0x7fffffffL)
    {
        return false;
    }
    *out = v;
    return true;
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
    char usage_text[4096];
    const os::CommandLine<HeapAllocator> native(HeapAllocator::Default(), argc, argv);
    FormatTo(usage_text, sizeof(usage_text), kUsageFormat, native.ProgramName().Data());
    if (!demo_settings.Begin("ui_demo", usage_text, argc, argv))
    {
        return kExitUsage;
    }
    g_demo_settings = &demo_settings;
    // What the settings system did not take: --set, --settings, --gpu, --backend, ... are handled there.
    static const os::CommandLineOption kCliOptions[] = {{"frames", true},
                                                        {"screenshot", true},
                                                        {"backend", true},
                                                        {"no-vsync", false},
                                                        {"validation", false},
                                                        {"gpu", true},
                                                        {"script", true},
                                                        {"scale", true},
                                                        {"help", false},
                                                        {"-h", false},
                                                        engine_placeholder::debug::kCrashHandlerOption,
                                                        engine_placeholder::debug::kCrashTestOption};
    const os::CommandLine<HeapAllocator> line(HeapAllocator::Default(), demo_settings.RestCount(),
                                              demo_settings.RestArgs(),
                                              engine_placeholder::Span<const os::CommandLineOption>(kCliOptions, 12));
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
    if (line.TryGetOption("backend", value))
    {
        options.backend = value.Data();
    }
    options.no_vsync = line.HasFlag("no-vsync");
    options.validation = line.HasFlag("validation");
    if (line.TryGetOption("gpu", value))
    {
        options.gpu = value.Data();
    }
    if (line.TryGetOption("script", value))
    {
        options.script = value.Data();
    }
    if (line.TryGetOption("scale", value))
    {
        options.scale = static_cast<f32>(CStrToDouble(value.Data()));
        if (options.scale != 1.0f && options.scale != 1.25f && options.scale != 2.0f)
        {
            return Usage(program, kExitUsage);
        }
    }
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
    static demo::DemoSettings::Effective effective;
    effective = demo_settings.ReadEffective();
    if (effective.gpu[0] != '\0')
    {
        options.gpu = effective.gpu;
    }
    options.backend = effective.window_backend;
    RenderSettings settings = renderer::RenderSettingsFromSnapshot(demo_settings.GetRegistry(),
                                                                   *demo_settings.Get().Snapshot());
    if (options.gpu != nullptr)
    {
        settings.SetText(renderer::settings::kAdapter, options.gpu);
    }
    if (options.screenshot != nullptr)
    {
        return RunScreenshot(options, settings);
    }
    return RunWindow(options, settings);
}

ENGINE_WINDOWED_PROGRAM(EngineMain)
