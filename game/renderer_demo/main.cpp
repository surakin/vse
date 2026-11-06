// renderer_demo: the Phase 4 showcase. A few hundred lit objects in eight materials, hundreds of point lights and a sun,
// drawn by the deferred renderer (G-buffer, light volumes, ACES tonemap) into a Wayland or X11 window, or offscreen.
// Tab switches between the orbit and fly cameras. See README.md for the options and controls.

#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <ctime>

#include <unistd.h>

#include "textures.h"
#include "io/directory.h"
#include "io/file.h"
#include "io/file_system.h"
#include "debug/program_setup.h"
#include "containers/vector.h"
#include "demo_frame.h"
#include "demo_scene.h"
#include "os/input.h"
#include "os/linux/platform_window_host.h"
#include "renderer/camera.h"
#include "renderer/device_resources.h"
#include "renderer/render_settings.h"
#include "renderer/renderer.h"
#include "rhi/adapter.h"
#include "rhi/device_host.h"
#include "rhi/rhi.h"
#include "rhi/rhi_log.h"

using namespace engine_placeholder;
using namespace engine_placeholder::rhi;
using namespace engine_placeholder::renderer;
using namespace renderer_demo;

namespace {

constexpr int kExitOk = 0;
constexpr int kExitFailure = 1; // the device, window, swapchain or a frame failed, or memory grew
constexpr int kExitUsage = 2;
constexpr int kExitDeviceLost = 3; // the GPU device was lost; the demo stopped cleanly

// Memory must not grow by more than this between frame 100 and the last frame of a run of at least kMemoryCheckFrames frames.
constexpr long kMemoryCheckStart = 100;
constexpr long kMemoryCheckFrames = 200;
constexpr long kMemoryGrowthLimitKiB = 8 * 1024;

constexpr u32 kShotWidth = 640;
constexpr u32 kShotHeight = 360;

struct Options {
    long frames = 0;                  // 0: until the window closes
    const char* screenshot = nullptr; // render offscreen to this PPM and exit
    const char* backend = nullptr;    // wayland, x11 or null for automatic
    bool no_vsync = false;
    bool validation = false;
    long objects = 540;
    long lights = 300;
    int gbuffer_layer = -1;
    long inject_device_loss = 0;
    long inject_oom = 0;
    const char* gpu = nullptr;            // --gpu: an adapter id, an index or part of a name
    bool list_gpus = false;               // --list-gpus
    bool prewarm = false;                 // --prewarm: rebuild every pipeline inside a reset instead of on first use
    const char* script = nullptr;         // --script: the live paths without a keyboard
};

double NowSeconds() {
    timespec ts{};
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return static_cast<double>(ts.tv_sec) + static_cast<double>(ts.tv_nsec) * 1e-9;
}

long ResidentKiB() {
    io::Path statm_path("/proc/self/statm", io::PathStyle::Posix);
    Vector<u8> bytes;
    if (io::ReadFile(statm_path, &bytes, 4096).Failed() || bytes.Size() == 0) return 0;
    char text[128] = {};
    std::memcpy(text, bytes.Data(), bytes.Size() < sizeof(text) - 1 ? bytes.Size() : sizeof(text) - 1);
    long size = 0, resident = 0;
    const int read = std::sscanf(text, "%ld %ld", &size, &resident);
    return read == 2 ? resident * (sysconf(_SC_PAGESIZE) / 1024) : 0;
}

long OpenDescriptors() {
    long count = 0;
    io::Directory fds;
    if (io::Directory::Open(io::Path("/proc/self/fd", io::PathStyle::Posix), &fds).Ok()) {
        io::DirectoryEntry entry;
        bool found = false;
        while (fds.Next(&entry, &found).Ok() && found) ++count;
    }
    return count - 1; // the descriptor of the directory itself
}

f32 Clamp(f32 v, f32 lo, f32 hi) { return v < lo ? lo : (v > hi ? hi : v); }

// Orbit around the middle of the scene, or fly with WASD: one object, switched with Tab.
class CameraController {
public:
    explicit CameraController(const DemoScene& scene) : radius_(scene.Radius()) {
        distance_ = radius_ * 1.1f;
        fly_position_ = Vec3(0, 3, radius_ * 0.8f);
    }

    void Update(const os::InputState& input, f32 dt) {
        const os::KeyboardState& keys = input.keyboard;
        if (keys.IsDown(os::Key::Tab) && !tab_was_down_) {
            fly_ = !fly_;
        }
        tab_was_down_ = keys.IsDown(os::Key::Tab);
        const f32 turn = ((keys.IsDown(os::Key::Right) ? 1.0f : 0.0f) - (keys.IsDown(os::Key::Left) ? 1.0f : 0.0f)) * 1.2f * dt;
        const f32 tilt = ((keys.IsDown(os::Key::Up) ? 1.0f : 0.0f) - (keys.IsDown(os::Key::Down) ? 1.0f : 0.0f)) * 0.8f * dt;
        f32 dyaw = turn, dpitch = tilt;
        if (input.IsMouseDown(os::kMouseLeft)) {
            if (dragging_) {
                dyaw += static_cast<f32>(input.mouse_x - drag_x_) * -0.005f;
                dpitch += static_cast<f32>(input.mouse_y - drag_y_) * 0.005f;
            }
            dragging_ = true;
            drag_x_ = input.mouse_x;
            drag_y_ = input.mouse_y;
        } else {
            dragging_ = false;
        }
        if (fly_) {
            yaw_ += dyaw;
            pitch_ = Clamp(pitch_ + dpitch, -1.4f, 1.4f);
            const f32 speed = (keys.IsDown(os::Key::LeftShift) ? 30.0f : 10.0f) * dt;
            const Vec3 forward(std::sin(yaw_) * std::cos(pitch_), std::sin(pitch_), -std::cos(yaw_) * std::cos(pitch_));
            const Vec3 right(std::cos(yaw_), 0, std::sin(yaw_));
            Vec3 move(0, 0, 0);
            if (keys.IsDown(os::Key::W)) move = move + forward;
            if (keys.IsDown(os::Key::S)) move = move - forward;
            if (keys.IsDown(os::Key::D)) move = move + right;
            if (keys.IsDown(os::Key::A)) move = move - right;
            if (keys.IsDown(os::Key::Space)) move = move + Vec3(0, 1, 0);
            if (keys.IsDown(os::Key::E)) move = move + Vec3(0, 1, 0);
            if (keys.IsDown(os::Key::Q)) move = move - Vec3(0, 1, 0);
            fly_position_ = fly_position_ + move * speed;
        } else {
            orbit_yaw_ += dyaw;
            orbit_pitch_ = Clamp(orbit_pitch_ + dpitch, 0.05f, 1.45f);
            distance_ = Clamp(distance_ - static_cast<f32>(input.scroll_dy) * radius_ * 0.05f, 4.0f, radius_ * 3.0f);
        }
    }

    Camera Get(f32 aspect) const {
        Camera cam;
        if (fly_) {
            const Vec3 forward(std::sin(yaw_) * std::cos(pitch_), std::sin(pitch_), -std::cos(yaw_) * std::cos(pitch_));
            cam = MakeLookAtCamera(fly_position_, fly_position_ + forward, Vec3(0, 1, 0));
        } else {
            const Vec3 eye(distance_ * std::cos(orbit_pitch_) * std::sin(orbit_yaw_), distance_ * std::sin(orbit_pitch_), distance_ * std::cos(orbit_pitch_) * std::cos(orbit_yaw_));
            cam = MakeLookAtCamera(eye, Vec3(0, 0.5f, 0), Vec3(0, 1, 0));
        }
        cam.aspect = aspect;
        cam.fov_y = 0.9f;
        cam.near_plane = 0.1f;
        cam.far_plane = radius_ * 6.0f + 60.0f;
        return cam;
    }

private:
    f32 radius_;
    bool fly_ = false;
    bool tab_was_down_ = false;
    f32 orbit_yaw_ = 0.55f;
    f32 orbit_pitch_ = 0.5f;
    f32 distance_ = 20.0f;
    f32 yaw_ = 0.0f;
    f32 pitch_ = -0.2f;
    Vec3 fly_position_ = Vec3(0, 3, 20);
    bool dragging_ = false;
    f64 drag_x_ = 0.0;
    f64 drag_y_ = 0.0;
};

bool WritePpm(const char* path, u32 width, u32 height, const Vector<unsigned char>& rgba) {
    io::Path file;
    return io::Path::Parse(StringView(path), io::NativePathStyle(), &file) && demo::WritePpm(file, width, height, rgba.Data()).Ok();
}

DeviceCreateInfo DeviceInfo(const Options& options) {
    DeviceCreateInfo info;
    if (options.validation) {
        info.validation = ValidationSetting::On;
        info.sync_validation = true;
    }
    info.faults.lose_device_at_frame = static_cast<u32>(options.inject_device_loss);
    info.faults.fail_allocations_from = static_cast<u32>(options.inject_oom);
    return info;
}

SceneOptions SceneOptionsFor(const Options& options) {
    SceneOptions so;
    so.objects = static_cast<u32>(options.objects);
    so.lights = static_cast<u32>(options.lights);
    so.asset_dir = RENDERER_DEMO_ASSET_DIR;
    return so;
}

FrameSetup SetupFor(const Options& options, const SceneOptions& so, Device* device, Swapchain* swapchain) {
    FrameSetup fs;
    fs.device = device;
    fs.swapchain = swapchain;
    fs.max_objects = so.objects + 16;
    fs.max_lights = so.lights + 2;
    fs.gbuffer_layer = options.gbuffer_layer;
    return fs;
}

// ---- settings ----------------------------------------------------------------------------------------------------------

// The rendering settings the flags describe (the demo has no settings file or generic setting option yet; see KNOWN-ISSUES.md).
void BuildSettings(const Options& options, RenderSettings* settings) {
    if (options.no_vsync) settings->SetEnum(settings::kPresentMode, 2);
    if (options.validation) {
        settings->SetEnum(settings::kValidation, 2);
        settings->SetBool(settings::kSyncValidation, true);
    }
    if (options.gpu != nullptr) settings->SetText(settings::kAdapter, options.gpu);
}

int ListGpus() {
    DeviceCreateInfo info; // Vulkan, headless: the list does not depend on a window
    AdapterList list;
    if (!EnumerateAdapters(info, &list)) {
        std::fprintf(stderr, "renderer_demo: cannot list the GPUs (no Vulkan loader or instance)\n");
        return kExitFailure;
    }
    static const char* const kKinds[] = {"other", "software", "virtual", "integrated", "discrete"};
    std::printf("%u GPU(s):\n", list.count);
    for (u32 i = 0; i < list.count; ++i) {
        const AdapterInfo& a = list.items[i];
        std::printf("  %u  %s  %s  (%s, %llu MiB, driver %s)%s%s\n", i, a.id, a.name, kKinds[static_cast<usize>(a.kind)],
                    static_cast<unsigned long long>(a.dedicated_memory_bytes >> 20), a.driver, a.suitable ? "" : "  unsuitable: ",
                    a.suitable ? "" : (a.unsuitable_reason != nullptr ? a.unsuitable_reason : "?"));
    }
    return kExitOk;
}

// What the keys and the script do to the settings. Each returns the settings to apply.
const char* const kPresentNames[] = {"fifo", "mailbox", "immediate"};

const char* EnumName(const RenderSettings& s, SettingId id) {
    const SettingDesc* d = s.Registry().Find(id);
    return d != nullptr && d->type == SettingType::Enum ? d->enum_names[s.GetEnum(id)] : "?";
}

void CycleEnum(RenderSettings* s, SettingId id) {
    const SettingDesc* d = s->Registry().Find(id);
    s->SetEnum(id, (s->GetEnum(id) + 1) % d->enum_count);
}

// Picks the next suitable adapter after the one in use, as an id for the `adapter` setting.
bool NextAdapterId(const char* current_id, char* out, usize capacity) {
    DeviceCreateInfo info;
    AdapterList list;
    if (!EnumerateAdapters(info, &list) || list.count == 0) return false;
    u32 at = 0;
    for (u32 i = 0; i < list.count; ++i) {
        if (std::strcmp(list.items[i].id, current_id) == 0) at = i;
    }
    for (u32 step = 1; step <= list.count; ++step) {
        const AdapterInfo& a = list.items[(at + step) % list.count];
        if (a.suitable) {
            std::snprintf(out, capacity, "%s", a.id);
            return true;
        }
    }
    return false;
}

void LogApply(const char* what, const ApplyResult& r) {
    if (r.queued) {
        RhiLogV(LogVerbosity::Info, "renderer_demo: %s: queued for the next frame", what);
    } else if (!r.change.Any() && r.ok && r.performed == CostClass::None) {
        RhiLogV(LogVerbosity::Info, "renderer_demo: %s: nothing changed", what);
    } else if (r.ok) {
        RhiLogV(LogVerbosity::Info, "renderer_demo: %s: %s done in %.2f ms (release %.2f, device %.2f, swapchain %.2f, rebuild %.2f)", what,
                CostClassName(r.performed), r.total_ms, r.release_ms, r.device_ms, r.swapchain_ms, r.rebuild_ms);
        if (r.performed == CostClass::DeviceReset) {
            RhiLogV(LogVerbosity::Info, "renderer_demo: %s: reset split: owners release %.2f, destroy old device %.2f, create device %.2f, swapchain %.2f, owners rebuild %.2f ms",
                    what, r.reset.release_ms, r.reset.destroy_ms, r.reset.create_device_ms, r.reset.create_swapchain_ms, r.reset.rebuild_ms);
        }
    } else {
        RhiLogV(LogVerbosity::Error, "renderer_demo: %s: %s FAILED (%s)%s%s", what, CostClassName(r.performed), r.reason,
                r.rolled_back ? ", rolled back" : "", r.device_lost ? ", device lost" : "");
    }
}

// Renders `frames` frames offscreen (no window) with a fixed camera and time, and saves the last one.
int RunScreenshot(const Options& options, const RenderSettings& settings) {
    char adapter_buffer[64];
    DeviceCreateInfo device_info;
    MakeDeviceCreateInfo(settings, DeviceInfo(options), adapter_buffer, &device_info);
    DevicePtr device = CreateDevice(device_info);
    if (device == nullptr) {
        RhiLogV(LogVerbosity::Error, "renderer_demo: no Vulkan device");
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
        DemoFrame frame;
        DemoScene scene;
        const SceneOptions so = SceneOptionsFor(options);
        const FrameSetup fs = SetupFor(options, so, device.Get(), nullptr);
        if (target.IsValid() && frame.Create(fs) && scene.Create(device.Get(), frame.Materials(), frame.Templates(), so)) {
            scene.Animate(0.5f);
            Camera camera = scene.OverviewCamera(static_cast<f32>(kShotWidth) / static_cast<f32>(kShotHeight));
            FitCameraDepthRange(scene.Scene(), &camera, 0.1f);
            FrameTarget ft;
            ft.texture = target;
            ft.width = kShotWidth;
            ft.height = kShotHeight;
            ft.format = Format::RGBA8_SRGB;
            ft.initial = ResourceState::Undefined;
            ft.final_state = ResourceState::CopySrc;
            FrameStatus status = FrameStatus::Ok;
            for (long i = 0; i < (options.frames > 0 ? options.frames : 1) && status == FrameStatus::Ok; ++i) {
                frame.SetTime(0.5f, 0.016f);
                status = frame.Render(&scene.Scene(), &camera, &ft);
                ft.initial = ResourceState::CopySrc; // the next frame starts where this one ended
            }
            Vector<unsigned char> pixels;
            pixels.Resize(static_cast<usize>(kShotWidth) * kShotHeight * 4);
            device->WaitIdle();
            if (status == FrameStatus::DeviceLost || device->DeviceLost()) {
                RhiLogV(LogVerbosity::Error, "renderer_demo: the GPU device was lost; stopping");
                exit_code = kExitDeviceLost;
            } else if (status != FrameStatus::Ok) {
                RhiLogV(LogVerbosity::Error, "renderer_demo: a frame failed: %s", frame.LastError());
            } else if (!frame.LastGBufferOk()) {
                RhiLogV(LogVerbosity::Error, "renderer_demo: some objects could not be drawn");
            } else if (!device->ReadbackTexture(target, 0, 0, pixels.Data(), pixels.Size(), ResourceState::CopySrc)) {
                RhiLogV(LogVerbosity::Error, "renderer_demo: could not read the image back");
            } else if (!WritePpm(options.screenshot, kShotWidth, kShotHeight, pixels)) {
                RhiLogV(LogVerbosity::Error, "renderer_demo: could not write %s", options.screenshot);
            } else {
                RhiLogV(LogVerbosity::Info, "renderer_demo: wrote %s (%u objects, %u point lights, %u light volume draws)", options.screenshot,
                        scene.ObjectCount(), frame.Lights().point_count, frame.PointDraws());
                exit_code = kExitOk;
            }
        } else {
            RhiLogV(LogVerbosity::Error, "renderer_demo: could not create the target or the scene");
        }
        device->WaitIdle();
    }
    if (target.IsValid()) device->DestroyTexture(target);
    device.Reset();
    return exit_code;
}

// Splits the script into words and runs them one at a time: `frame:N` runs until N frames were drawn, `set:key=value` applies a
// setting, `reset` forces a device reset, `gpu:next` moves to the next adapter, `loss` loses the device, `quit` stops.
struct Script {
    char text[4096] = {};
    char* words[512] = {};
    int count = 0;
    int next = 0;
    long wait_until = 0;
    void Parse(const char* source) {
        std::snprintf(text, sizeof(text), "%s", source);
        for (char* p = text; *p != '\0' && count < 512;) {
            while (*p == ' ') ++p;
            if (*p == '\0') break;
            words[count++] = p;
            while (*p != ' ' && *p != '\0') ++p;
            if (*p == ' ') *p++ = '\0';
        }
    }
};

int RunWindow(const Options& options, RenderSettings settings) {
    os::InputState input;
    os::PlatformWindowHost window_host;
    if (!window_host.OpenFromEnvironment("renderer_demo", 1280, 720, &input)) {
        RhiLogV(LogVerbosity::Error, "renderer_demo: could not open a window");
        return kExitFailure;
    }
    os::Window* window = window_host.Get();
    DeviceCreateInfo base = DeviceInfo(options);
    base.window = window;
    char adapter_buffer[64];
    DeviceCreateInfo info;
    MakeDeviceCreateInfo(settings, base, adapter_buffer, &info);
    DeviceResources registry; // destroyed after the host and everything that registered with it
    DeviceHost host;
    host.AddObserver(&registry, 0);
    if (!host.Create(info, MakeSwapchainDesc(settings))) {
        RhiLogV(LogVerbosity::Error, "renderer_demo: no Vulkan device can present to this window (%s)", host.LastError());
        return kExitFailure;
    }
    int exit_code = kExitOk;
    {
        DemoFrame frame;
        DemoScene scene;
        const SceneOptions so_base = SceneOptionsFor(options);
        SceneOptions so = so_base;
        so.resources = &registry;
        FrameSetup fs = SetupFor(options, so, host.GetDevice(), host.GetSwapchain());
        fs.resources = &registry;
        fs.host = &host;
        fs.create_info = base;
        fs.initial_settings = &settings;
        fs.window = window;
        fs.prewarm_pipelines = options.prewarm;
        if (options.inject_device_loss != 0) fs.recovery.max_attempts = 0; // the injected loss is the terminal kind the tests expect
        if (!frame.Create(fs) || !scene.Create(host.GetDevice(), frame.Materials(), frame.Templates(), so)) {
            RhiLogV(LogVerbosity::Error, "renderer_demo: could not create the scene");
            host.GetDevice()->WaitIdle();
            exit_code = kExitFailure;
        } else {
            Renderer& renderer = *frame.GetRenderer();
            struct ResetParts {
                DemoFrame* frame;
                DemoScene* scene;
            } parts{&frame, &scene};
            renderer.SetResetCallbacks(
                [](void*, const char* stage) { RhiLogV(LogVerbosity::Info, "renderer_demo: changing settings (%s): the window keeps its last picture", stage); },
                [](void* user, bool ok) {
                    auto* p = static_cast<ResetParts*>(user);
                    Renderer* r = p->frame->GetRenderer();
                    if (r->GetDevice() != nullptr) {
                        p->frame->Rebind(r->GetDevice(), r->GetSwapchain());
                        p->scene->Rebind(r->GetDevice());
                    }
                    if (!ok) RhiLogV(LogVerbosity::Warn, "renderer_demo: the change did not work; the previous settings are back");
                },
                &parts);

            Script script;
            if (options.script != nullptr) script.Parse(options.script);
            RhiLogV(LogVerbosity::Info,
                    "renderer_demo keys: F1 vsync, F2 validation, F3 next GPU, F4 render scale, F5 fullscreen, F6 swapchain format, F7 shader quality, F9 force reset, F10 lose the device");
            bool previous_keys[11] = {};
            bool measure_next_frame = false;
            double measure_start = 0.0;

            CameraController controller(scene);
            const double start = NowSeconds();
            double window_start = start;
            double previous = start;
            u64 window_frames = 0;
            long frames = 0;
            long rss_at_start = 0;
            bool quit = false;
            auto apply = [&](const char* what, const RenderSettings& wanted) {
                const double t = NowSeconds();
                const ApplyResult r = renderer.ApplySettings(wanted);
                LogApply(what, r);
                if (r.ok && r.performed >= CostClass::Swapchain) {
                    measure_next_frame = true;
                    measure_start = t;
                }
            };
            while (!quit && !window->ShouldClose() && (options.frames <= 0 || frames < options.frames)) {
                input.BeginFrame();
                window->PumpEvents();
                // Script words that are due.
                while (script.next < script.count && frames >= script.wait_until) {
                    const char* word = script.words[script.next++];
                    if (std::strncmp(word, "frame:", 6) == 0) {
                        script.wait_until = std::atol(word + 6);
                    } else if (std::strncmp(word, "set:", 4) == 0) {
                        RenderSettings wanted = renderer.Settings();
                        const char* eq = std::strchr(word + 4, '=');
                        char key[64];
                        bool applied = false;
                        if (eq != nullptr && static_cast<usize>(eq - (word + 4)) < sizeof(key)) {
                            std::memcpy(key, word + 4, static_cast<usize>(eq - (word + 4)));
                            key[eq - (word + 4)] = '\0';
                            const SettingId id = wanted.Registry().FindByName(key);
                            if (id.IsValid()) {
                                const SetStatus status = wanted.SetFromString(id, eq + 1);
                                if (status == SetStatus::Ok || status == SetStatus::Clamped) {
                                    apply(word, wanted);
                                    applied = true;
                                }
                            }
                        }
                        if (!applied) RhiLogV(LogVerbosity::Error, "renderer_demo: script: bad setting '%s' (use set:key=value)", word + 4);
                    } else if (std::strcmp(word, "reset") == 0) {
                        const double t = NowSeconds();
                        const ApplyResult r = renderer.ForceDeviceReset();
                        LogApply("reset", r);
                        if (r.ok) {
                            measure_next_frame = true;
                            measure_start = t;
                        }
                    } else if (std::strcmp(word, "gpu:next") == 0) {
                        char id[64];
                        if (renderer.GetDevice() == nullptr) {
                            RhiLogV(LogVerbosity::Warn, "renderer_demo: script: gpu:next skipped (no device right now)");
                        } else if (NextAdapterId(renderer.GetDevice()->Adapter().id, id, sizeof(id))) {
                            RenderSettings wanted = renderer.Settings();
                            wanted.SetText(settings::kAdapter, id);
                            apply("gpu:next", wanted);
                        }
                    } else if (std::strcmp(word, "loss") == 0) {
                        if (renderer.GetDevice() != nullptr) renderer.GetDevice()->InjectDeviceLoss();
                        else RhiLogV(LogVerbosity::Warn, "renderer_demo: script: loss skipped (no device right now)");
                    } else if (std::strncmp(word, "crash:", 6) == 0) {
                        // A crash in the middle of a run that already reset the device: the report must show the epoch and the reset count.
                        const char* kind = word + 6;
                        engine_placeholder::debug::RunCrashTest(std::strcmp(kind, "segv") == 0     ? engine_placeholder::debug::CrashTestKind::Segv
                                                                : std::strcmp(kind, "assert") == 0 ? engine_placeholder::debug::CrashTestKind::Assert
                                                                                                   : engine_placeholder::debug::CrashTestKind::Abort);
                    } else if (std::strcmp(word, "quit") == 0) {
                        quit = true;
                    } else {
                        RhiLogV(LogVerbosity::Error, "renderer_demo: script: unknown word '%s'", word);
                    }
                }
                // The live keys: one action per press.
                const os::Key function_keys[] = {os::Key::F1, os::Key::F2, os::Key::F3, os::Key::F4, os::Key::F5, os::Key::F6, os::Key::F7, os::Key::F8, os::Key::F9, os::Key::F10};
                if (options.script == nullptr) {
                    for (int k = 0; k < 10; ++k) {
                        const bool down = input.keyboard.IsDown(function_keys[k]);
                        if (down && !previous_keys[k]) {
                            RenderSettings wanted = renderer.Settings();
                            char id[64];
                            switch (k) {
                            case 0: wanted.SetEnum(settings::kPresentMode, wanted.GetEnum(settings::kPresentMode) == 0 ? 2 : 0); apply("F1 vsync", wanted); break;
                            case 1: wanted.SetEnum(settings::kValidation, wanted.GetEnum(settings::kValidation) == 2 ? 1 : 2); wanted.SetBool(settings::kSyncValidation, wanted.GetEnum(settings::kValidation) == 2); apply("F2 validation", wanted); break;
                            case 2:
                                if (renderer.GetDevice() != nullptr && NextAdapterId(renderer.GetDevice()->Adapter().id, id, sizeof(id))) { wanted.SetText(settings::kAdapter, id); apply("F3 next GPU", wanted); }
                                break;
                            case 3: wanted.SetFloat(settings::kRenderScale, wanted.GetFloat(settings::kRenderScale) >= 2.0f ? 0.5f : wanted.GetFloat(settings::kRenderScale) * 2.0f); apply("F4 render scale", wanted); break;
                            case 4: wanted.SetBool(settings::kFullscreen, !wanted.GetBool(settings::kFullscreen)); apply("F5 fullscreen", wanted); break;
                            case 5: CycleEnum(&wanted, settings::kSwapchainFormat); apply("F6 swapchain format", wanted); break;
                            case 6: CycleEnum(&wanted, settings::kShaderQuality); apply("F7 shader quality", wanted); break;
                            case 8: {
                                const double t = NowSeconds();
                                const ApplyResult r = renderer.ForceDeviceReset();
                                LogApply("F9 force reset", r);
                                if (r.ok) { measure_next_frame = true; measure_start = t; }
                                break;
                            }
                            case 9:
                                if (renderer.GetDevice() != nullptr) {
                                    renderer.GetDevice()->InjectDeviceLoss();
                                    RhiLogV(LogVerbosity::Info, "renderer_demo: F10: the device was told it is lost");
                                }
                                break;
                            default: break;
                            }
                        }
                        previous_keys[k] = down;
                    }
                }
                const double now_time = NowSeconds();
                const f32 dt = static_cast<f32>(now_time - previous < 0.1 ? now_time - previous : 0.1);
                previous = now_time;
                controller.Update(input, dt);
                const f32 seconds = static_cast<f32>(now_time - start);
                scene.Animate(seconds);
                rhi::Swapchain* swapchain = renderer.GetSwapchain();
                const f32 aspect = swapchain != nullptr && swapchain->Height() > 0 ? static_cast<f32>(swapchain->Width()) / static_cast<f32>(swapchain->Height()) : 1.0f;
                Camera camera = controller.Get(aspect);
                FitCameraDepthRange(scene.Scene(), &camera, 0.1f);
                frame.SetTime(seconds, dt);
                const FrameStatus status = frame.Render(&scene.Scene(), &camera, nullptr);
                if (status == FrameStatus::DeviceLost) {
                    if (renderer.IsBroken() || options.inject_device_loss != 0) {
                        RhiLogV(LogVerbosity::Error, "renderer_demo: the GPU device was lost; stopping");
                        exit_code = kExitDeviceLost;
                        break;
                    }
                    RhiLogV(LogVerbosity::Warn, "renderer_demo: the GPU device was lost; recovering");
                    ++frames;
                    continue;
                }
                if (status == FrameStatus::Failed) {
                    RhiLogV(LogVerbosity::Error, "renderer_demo: a frame failed: %s", frame.LastError());
                    exit_code = kExitFailure;
                    break;
                }
                if (status == FrameStatus::Skipped) { // minimized or recovering: wait without spinning; it still counts toward --frames
                    ++frames;
                    timespec nap{0, 10 * 1000 * 1000};
                    nanosleep(&nap, nullptr);
                    continue;
                }
                if (measure_next_frame) {
                    RhiLogV(LogVerbosity::Info, "renderer_demo: first good frame %.2f ms after the change was asked for", (NowSeconds() - measure_start) * 1000.0);
                    measure_next_frame = false;
                }
                ++frames;
                ++window_frames;
                if (rss_at_start == 0 && frames >= kMemoryCheckStart) rss_at_start = ResidentKiB();
                const double now = NowSeconds();
                if (now - window_start >= 1.0) {
                    RhiLogV(LogVerbosity::Info, "renderer_demo: %llu frames, %.3f ms/frame, %u objects drawn in %u draws, %u point lights (%u culled, %u dropped), %u volume draws",
                            static_cast<unsigned long long>(window_frames), (now - window_start) * 1000.0 / static_cast<double>(window_frames),
                            frame.GBufferStatistics().drawn_objects, frame.GBufferStatistics().draws, frame.Lights().point_count, frame.Lights().point_culled,
                            frame.Lights().dropped, frame.PointDraws());
                    window_start = now;
                    window_frames = 0;
                }
            }
            if (exit_code == kExitOk && options.frames >= kMemoryCheckFrames && rss_at_start == 0) {
                RhiLogV(LogVerbosity::Warn, "renderer_demo: the memory check did not run (fewer than %ld frames were drawn)", kMemoryCheckStart);
            }
            if (exit_code == kExitOk && options.frames >= kMemoryCheckFrames && rss_at_start > 0) {
                const long rss_at_end = ResidentKiB();
                RhiLogV(LogVerbosity::Info, "renderer_demo: resident memory %ld KiB at frame %ld, %ld KiB at frame %ld; %ld open descriptors; %llu resets", rss_at_start, kMemoryCheckStart, rss_at_end, frames,
                        OpenDescriptors(), static_cast<unsigned long long>(renderer.Stats().resets));
                if (rss_at_end - rss_at_start > kMemoryGrowthLimitKiB && options.script == nullptr) { // a script resets devices on purpose: report only
                    RhiLogV(LogVerbosity::Error, "renderer_demo: memory grew by %ld KiB over the run", rss_at_end - rss_at_start);
                    exit_code = kExitFailure;
                }
            }
            if (host.GetDevice() != nullptr) host.GetDevice()->WaitIdle();
            scene.Destroy();
            frame.Destroy();
        }
    }
    host.Destroy();
    return exit_code;
}

bool ParseCount(const char* text, long* out, long max = 0x7fffffffL) {
    char* end = nullptr;
    const long value = std::strtol(text, &end, 10);
    if (end == text || *end != '\0' || value < 0 || value > max) return false;
    *out = value;
    return true;
}

// Maps a layer name to a GBufferLayer value, or -1.
int ParseLayer(const char* name) {
    static const char* const kNames[] = {"albedo", "normal", "material", "depth", "worldpos"};
    for (int i = 0; i < 5; ++i) {
        if (std::strcmp(name, kNames[i]) == 0) return i;
    }
    return -1;
}

int Usage(const char* program, int code) {
    std::fprintf(code == kExitOk ? stdout : stderr,
                 "usage: %s [--backend wayland|x11] [--frames N] [--screenshot out.ppm] [--no-vsync] [--validation]\n"
                 "       [--objects N] [--lights N] [--gbuffer albedo|normal|material|depth|worldpos]\n"
                 "       [--inject-device-loss FRAME] [--inject-oom N] [--gpu ID|INDEX|NAME] [--list-gpus] [--prewarm]\n"
                 "       [--script WORDS]\n"
                 "  --backend B       display backend (default: automatic; same as ENGINE_WINDOW_BACKEND)\n"
                 "  --frames N        stop after N frames (also RENDERER_DEMO_FRAMES=N)\n"
                 "  --screenshot P    render offscreen, no window, and write the last frame to P (a binary PPM, 640x360)\n"
                 "  --objects N       total objects including 49 ground tiles, 64 to 20000 (default 540)\n"
                 "  --lights N        point lights, 0 to 16000 (default 300)\n"
                 "  --gbuffer L       show a G-buffer layer instead of the lit picture\n"
                 "  --no-vsync        present without waiting for the display\n"
                 "  --validation      enable the validation layer with synchronization validation\n"
                 "  --gpu G           the GPU to use: an id from --list-gpus, an index, or part of the name\n"
                 "  --list-gpus       print the GPUs and exit\n"
                 "  --prewarm         after a device reset rebuild every pipeline at once instead of when it is first used\n"
                 "  --script W        run steps without a keyboard: frame:N set:key=value reset gpu:next loss crash:segv|abort|assert quit (space separated)\n"
                 "  --inject-device-loss F  fault injection: lose the GPU device at frame F (exit code 3)\n"
                 "  --inject-oom N    fault injection: fail device-memory allocation number N and every one after it\n",
                 program);
    return code;
}

} // namespace

int main(int argc, char** argv) {
    const engine_placeholder::debug::ProgramDebugSetup debug_setup = engine_placeholder::debug::StartProgramDebugging(argc, argv);
    engine_placeholder::debug::RunCrashTest(debug_setup.crash_test); // --crash-test=segv|abort|assert|ensure, before any window or device
    LogRhi.SetVerbosity(LogVerbosity::Info); // the frame-time line is Info
    Options options;
    const char* frames_env = std::getenv("RENDERER_DEMO_FRAMES");
    options.frames = frames_env != nullptr ? std::atol(frames_env) : 0;
    for (int i = 1; i < argc; ++i) {
        if (engine_placeholder::debug::IsProgramDebugArgument(argv[i])) {
            continue; // --crash-handler=... and --crash-test=... are handled by StartProgramDebugging
        }
        const bool has_value = i + 1 < argc;
        if (std::strcmp(argv[i], "--frames") == 0 && has_value) {
            if (!ParseCount(argv[++i], &options.frames)) return Usage(argv[0], kExitUsage);
        } else if (std::strcmp(argv[i], "--screenshot") == 0 && has_value) {
            options.screenshot = argv[++i];
        } else if (std::strcmp(argv[i], "--backend") == 0 && has_value) {
            options.backend = argv[++i];
        } else if (std::strcmp(argv[i], "--no-vsync") == 0) {
            options.no_vsync = true;
        } else if (std::strcmp(argv[i], "--validation") == 0) {
            options.validation = true;
        } else if (std::strcmp(argv[i], "--objects") == 0 && has_value) {
            if (!ParseCount(argv[++i], &options.objects, 20000) || options.objects < 64) return Usage(argv[0], kExitUsage);
        } else if (std::strcmp(argv[i], "--lights") == 0 && has_value) {
            if (!ParseCount(argv[++i], &options.lights, 16000)) return Usage(argv[0], kExitUsage);
        } else if (std::strcmp(argv[i], "--gbuffer") == 0 && has_value) {
            options.gbuffer_layer = ParseLayer(argv[++i]);
            if (options.gbuffer_layer < 0) return Usage(argv[0], kExitUsage);
        } else if (std::strcmp(argv[i], "--inject-device-loss") == 0 && has_value) {
            if (!ParseCount(argv[++i], &options.inject_device_loss)) return Usage(argv[0], kExitUsage);
        } else if (std::strcmp(argv[i], "--inject-oom") == 0 && has_value) {
            if (!ParseCount(argv[++i], &options.inject_oom)) return Usage(argv[0], kExitUsage);
        } else if (std::strcmp(argv[i], "--gpu") == 0 && has_value) {
            options.gpu = argv[++i];
        } else if (std::strcmp(argv[i], "--prewarm") == 0) {
            options.prewarm = true;
        } else if (std::strcmp(argv[i], "--list-gpus") == 0) {
            options.list_gpus = true;
        } else if (std::strcmp(argv[i], "--script") == 0 && has_value) {
            options.script = argv[++i];
        } else if (std::strcmp(argv[i], "--help") == 0 || std::strcmp(argv[i], "-h") == 0) {
            return Usage(argv[0], kExitOk);
        } else {
            return Usage(argv[0], kExitUsage);
        }
    }
    if (options.backend != nullptr) {
        if (std::strcmp(options.backend, "wayland") != 0 && std::strcmp(options.backend, "x11") != 0) {
            std::fprintf(stderr, "renderer_demo: unknown backend '%s'\n", options.backend);
            return kExitUsage;
        }
        setenv("ENGINE_WINDOW_BACKEND", options.backend, 1);
    }
    if (options.list_gpus) return ListGpus();
    RenderSettings settings;
    settings.SetEnum(settings::kBackend, 1); // the demo draws with Vulkan
    BuildSettings(options, &settings);
    if (options.screenshot != nullptr) {
        return RunScreenshot(options, settings);
    }
    return RunWindow(options, settings);
}
