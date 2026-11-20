// model_viewer: shows a glTF or GLB file in a window. The model is imported in memory, its default scene spawned and
// framed; a skinned model plays its first clip. Left-drag orbits, the wheel zooms, right- or middle-drag strafes, the
// keys strafe too (W/A/S/D, Q/E), F frames the model again, Space pauses the clip, N plays the next one, F1 hides the
// overlay. `--screenshot out.ppm` draws offscreen instead. See README.md.

#include "asset_import/gltf_import.h"
#include "log/output.h"
#include "containers/array.h"
#include "containers/vector.h"
#include "cstr/format.h"
#include "cstr/number.h"
#include "debug/program_setup.h"
#include "io/file.h"
#include "io/file_system.h"
#include "jobs/scheduler.h"
#include "orbit_camera.h"
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
#include "settings_main.h"
#include "string/string.h"
#include "textures.h"
#include "viewer_scene.h"

using namespace vse;
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
constexpr f32 kStep = 1.0f / 60.0f;
constexpr f32 kOrbitPerPixel = 0.008f;  // radians of orbit per pixel of mouse drag

struct Options
{
    String<HeapAllocator> file{HeapAllocator::Default()};
    long frames = 0;  // 0: until the window closes (screenshot: 120)
    long clip = 0;
    bool animate = true;
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

bool ReadFile(const io::Path& path, u64 max_bytes, Vector<u8>* out)
{
    io::FilePtr in;
    if (io::File::Open(path, io::OpenMode::Read, io::kOpenNone, &in).Failed())
    {
        return false;
    }
    return io::ReadAll(*in, out, max_bytes).Ok();
}

// Reads the files a `.gltf` names (buffers, images) from its folder.
class DirectorySource final : public asset::GltfSource
{
public:
    explicit DirectorySource(const io::Path& directory) : directory_(directory) {}
    asset::Status Read(StringView name, u64 max_bytes, Vector<byte>* out) override
    {
        io::Path relative;
        io::Path full;
        Vector<u8> bytes;
        if (!io::Path::Parse(name, io::NativePathStyle(), &relative) || !directory_.TryJoin(relative, &full) ||
            !ReadFile(full, max_bytes, &bytes))
        {
            return asset::Status::Make(asset::ErrorCode::NotFound, "the file cannot be read");
        }
        *out = static_cast<Vector<byte>&&>(bytes);
        return asset::Status::Success();
    }

private:
    io::Path directory_;
};

// The model file and the folder it names its other files from, read before any device exists.
struct ModelFile
{
    Vector<u8> bytes;
    io::Path path;
    UniquePtr<DirectorySource> source;
    String<HeapAllocator> name{HeapAllocator::Default()};  // the file name, for the overlay
};

bool OpenModel(const Options& options, ModelFile* model)
{
    if (!io::Path::Parse(options.file.View(), io::NativePathStyle(), &model->path))
    {
        out::PrintError("model_viewer: %s is not a path\n", options.file.CStr());
        return false;
    }
    if (!ReadFile(model->path, asset::ImportLimits().max_file_bytes, &model->bytes))
    {
        out::PrintError("model_viewer: %s cannot be read\n", options.file.CStr());
        return false;
    }
    model->source = MakeUnique<DirectorySource>(model->path.Parent());
    model->name = String<HeapAllocator>(HeapAllocator::Default(), model->path.FileName());
    return true;
}

model_viewer::ViewerOptions SceneOptions(const Options& options, ModelFile* model, DeviceResources* resources)
{
    model_viewer::ViewerOptions so;
    so.file = ByteSpan(reinterpret_cast<const byte*>(model->bytes.Data()), model->bytes.Size());
    so.source = model->source.Get();
    so.clip = static_cast<u32>(options.clip);
    so.animate = options.animate;
    so.resources = resources;
    return so;
}

SceneFrameSetup SetupFor(rhi::Device* device, rhi::Swapchain* swapchain, const RenderSettings& settings)
{
    SceneFrameSetup fs;
    fs.initial_settings = &settings;  // the device was made from them; also sizes the joint palette
    fs.device = device;
    fs.swapchain = swapchain;
    fs.max_objects = 65536;
    fs.max_lights = 8;
    fs.font_path = MODEL_VIEWER_FONT_FILE;
    return fs;
}

void OverlayText(const ModelFile& model, const model_viewer::ViewerScene& scene, String<HeapAllocator>* greeting,
                 String<HeapAllocator>* status)
{
    *greeting = String<HeapAllocator>(HeapAllocator::Default(), model.name.View());
    Array<char, 256> line;
    FormatTo(line.Data(), line.Size(), "%u meshes, %u materials, %u objects drawn, %u triangles\n",
             scene.Report().meshes, scene.Report().materials, scene.ObjectCount(), scene.Triangles());
    *status = String<HeapAllocator>(HeapAllocator::Default(), StringView(line.Data()));
    if (scene.CurrentClip() < scene.Clips().Size())
    {
        FormatTo(line.Data(), line.Size(), "clip %zu of %zu: %s%s\n", static_cast<size_t>(scene.CurrentClip() + 1),
                 static_cast<size_t>(scene.Clips().Size()), scene.Clips()[scene.CurrentClip()].name.CStr(),
                 scene.Paused() ? " (paused)" : "");
    }
    else
    {
        FormatTo(line.Data(), line.Size(), "%s\n", scene.Clips().Empty() ? "no animation" : "animation off");
    }
    status->Append(StringView(line.Data()));
    status->Append(StringView("drag: orbit, wheel: zoom, right/middle drag or W/A/S/D Q/E: strafe, F: frame, "
                              "Space: pause, N: next clip, F1: overlay"));
}

int RunScreenshot(const Options& options, const RenderSettings& settings, ModelFile* model)
{
    Array<char, 64> adapter_buffer;
    DeviceCreateInfo device_info;
    MakeDeviceCreateInfo(settings, DeviceInfo(), adapter_buffer.Data(), &device_info);
    DevicePtr device = CreateDevice(device_info);
    if (device == nullptr)
    {
        RhiLogV(LogVerbosity::Error, "model_viewer: no %s device", demo::BackendLabel(device_info.backend));
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
        model_viewer::ViewerScene scene;
        if (!target.IsValid() || !frame.Create(SetupFor(device.Get(), nullptr, settings)))
        {
            RhiLogV(LogVerbosity::Error, "model_viewer: could not create the target or the frame");
        }
        else if (!scene.Create(device.Get(), frame.Materials(), frame.Templates(),
                               SceneOptions(options, model, nullptr)))
        {
            out::PrintError("model_viewer: %s: %s\n", options.file.CStr(), scene.Error());
        }
        else
        {
            const f32 aspect = static_cast<f32>(kShotWidth) / static_cast<f32>(kShotHeight);
            model_viewer::OrbitCamera camera;
            scene.Update(0.0f, nullptr);
            camera.Frame(scene.Bounds(), aspect);
            FrameTarget ft;
            ft.texture = target;
            ft.width = kShotWidth;
            ft.height = kShotHeight;
            ft.format = Format::RGBA8_SRGB;
            ft.initial = ResourceState::Undefined;
            ft.final_state = ResourceState::CopySrc;
            FrameStatus status = FrameStatus::Ok;
            const long frames = options.frames > 0 ? options.frames : 120;
            for (long i = 0; i < frames && status == FrameStatus::Ok; ++i)
            {
                scene.Update(kStep, nullptr);
                camera.Orbit(0.004f, 0.0f);  // a slow turn, so the frames differ
                String<HeapAllocator> greeting(HeapAllocator::Default());
                String<HeapAllocator> text(HeapAllocator::Default());
                OverlayText(*model, scene, &greeting, &text);
                frame.SetOverlay(true, greeting.View(), text.View());
                Camera view = camera.View(aspect);
                frame.SetTime(static_cast<f32>(i) * kStep, kStep);
                status = frame.Render(&scene.Scene(), &view, &ft);
                ft.initial = ResourceState::CopySrc;
            }
            Vector<byte> pixels;
            pixels.Resize(static_cast<usize>(kShotWidth) * kShotHeight * 4);
            device->WaitIdle();
            io::Path file;
            if (status == FrameStatus::DeviceLost || device->DeviceLost())
            {
                RhiLogV(LogVerbosity::Error, "model_viewer: the GPU device was lost; stopping");
                exit_code = kExitDeviceLost;
            }
            else if (status != FrameStatus::Ok)
            {
                RhiLogV(LogVerbosity::Error, "model_viewer: a frame failed: %s", frame.LastError());
            }
            else if (!device->ReadbackTexture(target, 0, 0, pixels.Data(), pixels.Size(), ResourceState::CopySrc))
            {
                RhiLogV(LogVerbosity::Error, "model_viewer: could not read the image back");
            }
            else if (!io::Path::Parse(StringView(options.screenshot), io::NativePathStyle(), &file) ||
                     !demo::WritePpm(file, kShotWidth, kShotHeight, pixels.Data()).Ok())
            {
                RhiLogV(LogVerbosity::Error, "model_viewer: could not write %s", options.screenshot);
            }
            else
            {
                RhiLogV(LogVerbosity::Info,
                        "model_viewer: wrote %s (%u meshes, %u objects drawn, %u triangles, %zu clips, %u skinned)",
                        options.screenshot, scene.Report().meshes, scene.ObjectCount(), scene.Triangles(),
                        static_cast<size_t>(scene.Clips().Size()), scene.SkinnedEntities());
                exit_code = kExitOk;
            }
        }
        device->WaitIdle();
        scene.Destroy();
        frame.Destroy();
    }
    if (target.IsValid())
    {
        device->DestroyTexture(target);
    }
    device.Reset();
    return exit_code;
}

int RunWindow(const Options& options, const RenderSettings& settings, ModelFile* model)
{
    jobs::Scheduler::Config scheduler_config;
    scheduler_config.pin = false;
    jobs::Scheduler scheduler(scheduler_config);
    os::InputState input;
    os::PlatformWindowHost window_host;
    Array<char, 300> title;
    FormatTo(title.Data(), title.Size(), "model_viewer: %s", model->name.CStr());
    if (!window_host.Open(os::ParseWindowBackend(StringView(options.backend)), title.Data(), 1280, 720, &input))
    {
        RhiLogV(LogVerbosity::Error, "model_viewer: could not open a window");
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
        RhiLogV(LogVerbosity::Error, "model_viewer: no %s device can present to this window (%s)",
                demo::BackendLabel(info.backend), host.LastError());
        return kExitFailure;
    }
    int exit_code = kExitOk;
    {
        SceneFrame frame;
        model_viewer::ViewerScene scene;
        SceneFrameSetup fs = SetupFor(host.GetDevice(), host.GetSwapchain(), settings);
        fs.resources = &registry;
        fs.host = &host;
        fs.create_info = base;
        fs.window = window;
        if (!frame.Create(fs))
        {
            RhiLogV(LogVerbosity::Error, "model_viewer: could not create the frame");
            exit_code = kExitFailure;
        }
        else if (!scene.Create(host.GetDevice(), frame.Materials(), frame.Templates(),
                               SceneOptions(options, model, &registry)))
        {
            out::PrintError("model_viewer: %s: %s\n", options.file.CStr(), scene.Error());
            exit_code = kExitFailure;
        }
        else
        {
            Renderer& renderer = *frame.GetRenderer();
            struct ResetParts
            {
                SceneFrame* frame;
                model_viewer::ViewerScene* scene;
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
            frame.SetOverlayScale(window->ContentScale());
            const auto aspect_now = [&]()
            {
                rhi::Swapchain* swapchain = renderer.GetSwapchain();
                return swapchain != nullptr && swapchain->Height() > 0
                           ? static_cast<f32>(swapchain->Width()) / static_cast<f32>(swapchain->Height())
                           : 16.0f / 9.0f;
            };
            model_viewer::OrbitCamera camera;
            scene.Update(0.0f, &scheduler);
            camera.Frame(scene.Bounds(), aspect_now());
            RhiLogV(LogVerbosity::Info, "model_viewer: %s: %u meshes, %u objects, %u triangles, %zu clips",
                    model->name.CStr(), scene.Report().meshes, scene.ObjectCount(), scene.Triangles(),
                    static_cast<size_t>(scene.Clips().Size()));
            const f64 start = static_cast<f64>(os::MonotonicTimeNanos()) * 1e-9;
            f64 previous = start;
            long frames = 0;
            bool overlay_on = true;
            bool stop = false;
            os::KeyboardState keys_before;
            f64 mouse_x = input.mouse_x;
            f64 mouse_y = input.mouse_y;
            const auto pressed = [&](os::Key key) { return input.keyboard.IsDown(key) && !keys_before.IsDown(key); };
            const auto axis = [&](os::Key minus, os::Key plus)
            { return (input.keyboard.IsDown(plus) ? 1.0f : 0.0f) - (input.keyboard.IsDown(minus) ? 1.0f : 0.0f); };
            auto draw = [&]()
            {
                if (stop || (options.frames > 0 && frames >= options.frames))
                {
                    return;
                }
                const f64 now = static_cast<f64>(os::MonotonicTimeNanos()) * 1e-9;
                const f32 dt = static_cast<f32>(now - previous < 0.1 ? now - previous : 0.1);
                previous = now;
                // The mouse: drag to orbit or pan, the wheel to zoom.
                const f32 dx = static_cast<f32>(input.mouse_x - mouse_x);
                const f32 dy = static_cast<f32>(input.mouse_y - mouse_y);
                mouse_x = input.mouse_x;
                mouse_y = input.mouse_y;
                rhi::Swapchain* swapchain = renderer.GetSwapchain();
                const f32 height = swapchain != nullptr ? static_cast<f32>(swapchain->Height()) : 720.0f;
                if (input.IsMouseDown(os::kMouseLeft))
                {
                    camera.Orbit(dx * kOrbitPerPixel, dy * kOrbitPerPixel);
                }
                else if (input.IsMouseDown(os::kMouseRight) || input.IsMouseDown(os::kMouseMiddle))
                {
                    camera.Pan(dx, dy, height);
                }
                if (input.scroll_dy != 0.0)
                {
                    // Positive is scrolling down (toward the user): that zooms out. 10 units a notch on every platform.
                    camera.Zoom(static_cast<f32>(-input.scroll_dy / 10.0));
                }
                // The keys.
                camera.Strafe(axis(os::Key::A, os::Key::D), axis(os::Key::Q, os::Key::E), axis(os::Key::S, os::Key::W),
                              dt);
                if (pressed(os::Key::F))
                {
                    camera.Frame(scene.Bounds(), aspect_now());
                }
                if (pressed(os::Key::Space))
                {
                    scene.SetPaused(!scene.Paused());
                }
                if (pressed(os::Key::N))
                {
                    scene.NextClip();
                }
                if (pressed(os::Key::F1))
                {
                    overlay_on = !overlay_on;
                }
                if (pressed(os::Key::Escape))
                {
                    stop = true;
                    return;
                }
                keys_before = input.keyboard;
                scene.Update(dt, &scheduler);
                String<HeapAllocator> greeting(HeapAllocator::Default());
                String<HeapAllocator> text(HeapAllocator::Default());
                OverlayText(*model, scene, &greeting, &text);
                frame.SetOverlay(overlay_on, greeting.View(), text.View());
                Camera view = camera.View(aspect_now());
                frame.SetTime(static_cast<f32>(now - start), dt);
                const FrameStatus status = frame.Render(&scene.Scene(), &view, nullptr);
                if (status == FrameStatus::DeviceLost)
                {
                    if (renderer.IsBroken())
                    {
                        RhiLogV(LogVerbosity::Error, "model_viewer: the GPU device was lost; stopping");
                        exit_code = kExitDeviceLost;
                        stop = true;
                        return;
                    }
                }
                else if (status == FrameStatus::Failed)
                {
                    RhiLogV(LogVerbosity::Error, "model_viewer: a frame failed: %s", frame.LastError());
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
                RhiLogV(LogVerbosity::Info, "model_viewer: showed %s for %ld frames", model->name.CStr(), frames);
            }
            if (host.GetDevice() != nullptr)
            {
                host.GetDevice()->WaitIdle();
            }
        }
        scene.Destroy();
        frame.Destroy();
    }
    host.Destroy();
    return exit_code;
}

bool ParseCount(StringView text, long max, long* out)
{
    char* end = nullptr;
    const long value = CStrToLong(text.Data(), &end, 10);
    if (end == text.Data() || *end != '\0' || value < 0 || value > max)
    {
        return false;
    }
    *out = value;
    return true;
}

const char kUsageFormat[] =
    "usage: %s FILE.glb|FILE.gltf [--frames N] [--screenshot out.ppm] [--clip N] [--no-animation]\n"
    "          [--backend wayland|x11|win32|cocoa]\n"
    "  FILE            the model: a .glb, or a .gltf with its buffers and images beside it\n"
    "  --frames N      stop after N frames (--screenshot: 120)\n"
    "  --screenshot P  draw offscreen, no window, and write the last frame to P (a binary PPM)\n"
    "  --clip N        play the file's clip N first (0: the first)\n"
    "  --no-animation  show a skinned model in its rest pose\n"
    "  --backend B     display backend (default: automatic; same as ENGINE_WINDOW_BACKEND)\n"
    "Mouse: left-drag orbits, the wheel zooms, right- or middle-drag strafes.\n"
    "Keys: W/A/S/D and Q/E strafe, F frames the model, Space pauses the clip, N plays the next, F1 hides the overlay,\n"
    "Escape quits.\n";

int Usage(const char* program, int code)
{
    if (code == kExitOk)
    {
        out::Print(kUsageFormat, program);
    }
    else
    {
        out::PrintError(kUsageFormat, program);
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
    FormatTo(usage_text.Data(), usage_text.Size(), kUsageFormat, native.ProgramName().Data());
    if (!demo_settings.Begin("model_viewer", usage_text.Data(), argc, argv))
    {
        return kExitUsage;
    }
    g_demo_settings = &demo_settings;
    static const Array<os::CommandLineOption, 9> kCliOptions = {os::CommandLineOption{"frames", true},
                                                                os::CommandLineOption{"screenshot", true},
                                                                os::CommandLineOption{"clip", true},
                                                                os::CommandLineOption{"no-animation", false},
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
    if (!line.Ok() || line.ArgCount() != 1)
    {
        out::PrintError("%s: %s\n", program,
                        !line.Ok() ? line.Error().Data()
                                   : (line.ArgCount() == 0 ? "no model file given" : "only one model file at a time"));
        return Usage(program, kExitUsage);
    }
    Options options;
    options.file = String<HeapAllocator>(HeapAllocator::Default(), line.Arg(0));
    StringView value;
    if ((line.TryGetOption("frames", value) && !ParseCount(value, 10000000, &options.frames)) ||
        (line.TryGetOption("clip", value) && !ParseCount(value, 100000, &options.clip)))
    {
        out::PrintError("%s: a number is out of range\n", program);
        return Usage(program, kExitUsage);
    }
    options.animate = !line.HasFlag("no-animation");
    if (line.TryGetOption("screenshot", value))
    {
        options.screenshot = value.Data();
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
    ModelFile model;
    if (!OpenModel(options, &model))
    {
        return kExitFailure;
    }
    const RenderSettings settings = renderer::RenderSettingsFromSnapshot(demo_settings.GetRegistry(),
                                                                         *demo_settings.Get().Snapshot());
    if (options.screenshot != nullptr)
    {
        return RunScreenshot(options, settings, &model);
    }
    return RunWindow(options, settings, &model);
}

ENGINE_WINDOWED_PROGRAM(EngineMain)
