// renderer_demo: the Phase 4 showcase. A few hundred lit objects in eight materials, hundreds of point lights and a sun,
// drawn by the deferred renderer (G-buffer, light volumes, ACES tonemap) into a Wayland or X11 window, or offscreen.
// Tab switches between the orbit and fly cameras. See README.md for the options and controls.

#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <ctime>

#include <unistd.h>

#include "containers/vector.h"
#include "demo_frame.h"
#include "demo_scene.h"
#include "os/input.h"
#include "os/linux/platform_window_host.h"
#include "renderer/camera.h"
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
};

double NowSeconds() {
    timespec ts{};
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return static_cast<double>(ts.tv_sec) + static_cast<double>(ts.tv_nsec) * 1e-9;
}

long ResidentKiB() {
    std::FILE* f = std::fopen("/proc/self/statm", "r");
    if (f == nullptr) return 0;
    long size = 0, resident = 0;
    const int read = std::fscanf(f, "%ld %ld", &size, &resident);
    std::fclose(f);
    return read == 2 ? resident * (sysconf(_SC_PAGESIZE) / 1024) : 0;
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
    std::FILE* f = std::fopen(path, "wb");
    if (f == nullptr) return false;
    std::fprintf(f, "P6\n%u %u\n255\n", width, height);
    bool ok = true;
    for (usize i = 0; i < static_cast<usize>(width) * height && ok; ++i) {
        ok = std::fwrite(&rgba[i * 4], 1, 3, f) == 3;
    }
    return std::fclose(f) == 0 && ok;
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

// Renders `frames` frames offscreen (no window) with a fixed camera and time, and saves the last one.
int RunScreenshot(const Options& options) {
    DevicePtr device = CreateDevice(DeviceInfo(options));
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

int RunWindow(const Options& options) {
    os::InputState input;
    os::PlatformWindowHost host;
    if (!host.OpenFromEnvironment("renderer_demo", 1280, 720, &input)) {
        RhiLogV(LogVerbosity::Error, "renderer_demo: could not open a window");
        return kExitFailure;
    }
    os::Window* window = host.Get();
    DeviceCreateInfo info = DeviceInfo(options);
    info.window = window;
    DevicePtr device = CreateDevice(info);
    if (device == nullptr) {
        RhiLogV(LogVerbosity::Error, "renderer_demo: no Vulkan device can present to this window");
        return kExitFailure;
    }
    SwapchainDesc sc;
    if (options.no_vsync) sc.present_mode = PresentMode::Immediate;
    Swapchain* swapchain = device->CreateSwapchain(sc);
    if (swapchain == nullptr) {
        RhiLogV(LogVerbosity::Error, "renderer_demo: could not create a swapchain");
        device.Reset();
        return kExitFailure;
    }
    int exit_code = kExitOk;
    {
        DemoFrame frame;
        DemoScene scene;
        const SceneOptions so = SceneOptionsFor(options);
        const FrameSetup fs = SetupFor(options, so, device.Get(), swapchain);
        if (!frame.Create(fs) || !scene.Create(device.Get(), frame.Materials(), frame.Templates(), so)) {
            RhiLogV(LogVerbosity::Error, "renderer_demo: could not create the scene");
            device->WaitIdle();
            exit_code = kExitFailure;
        } else {
            CameraController controller(scene);
            const double start = NowSeconds();
            double window_start = start;
            double previous = start;
            u64 window_frames = 0;
            long frames = 0;
            long rss_at_start = 0;
            while (!window->ShouldClose() && (options.frames <= 0 || frames < options.frames)) {
                input.BeginFrame();
                window->PumpEvents();
                const double now_time = NowSeconds();
                const f32 dt = static_cast<f32>(now_time - previous < 0.1 ? now_time - previous : 0.1);
                previous = now_time;
                controller.Update(input, dt);
                const f32 seconds = static_cast<f32>(now_time - start);
                scene.Animate(seconds);
                Camera camera = controller.Get(static_cast<f32>(swapchain->Width()) / static_cast<f32>(swapchain->Height() > 0 ? swapchain->Height() : 1));
                FitCameraDepthRange(scene.Scene(), &camera, 0.1f);
                frame.SetTime(seconds, dt);
                const FrameStatus status = frame.Render(&scene.Scene(), &camera, nullptr);
                if (status == FrameStatus::DeviceLost) {
                    RhiLogV(LogVerbosity::Error, "renderer_demo: the GPU device was lost; stopping");
                    exit_code = kExitDeviceLost;
                    break;
                }
                if (status == FrameStatus::Failed) {
                    RhiLogV(LogVerbosity::Error, "renderer_demo: a frame failed: %s", frame.LastError());
                    exit_code = kExitFailure;
                    break;
                }
                if (status == FrameStatus::Skipped) { // minimized: wait without spinning; it still counts toward --frames
                    ++frames;
                    timespec nap{0, 10 * 1000 * 1000};
                    nanosleep(&nap, nullptr);
                    continue;
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
                RhiLogV(LogVerbosity::Info, "renderer_demo: resident memory %ld KiB at frame %ld, %ld KiB at frame %ld", rss_at_start, kMemoryCheckStart, rss_at_end, frames);
                if (rss_at_end - rss_at_start > kMemoryGrowthLimitKiB) {
                    RhiLogV(LogVerbosity::Error, "renderer_demo: memory grew by %ld KiB over the run", rss_at_end - rss_at_start);
                    exit_code = kExitFailure;
                }
            }
            device->WaitIdle();
        }
    }
    device->WaitIdle();
    device->DestroySwapchain(swapchain);
    device.Reset();
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
                 "       [--inject-device-loss FRAME] [--inject-oom N]\n"
                 "  --backend B       display backend (default: automatic; same as ENGINE_WINDOW_BACKEND)\n"
                 "  --frames N        stop after N frames (also RENDERER_DEMO_FRAMES=N)\n"
                 "  --screenshot P    render offscreen, no window, and write the last frame to P (a binary PPM, 640x360)\n"
                 "  --objects N       total objects including 49 ground tiles, 64 to 20000 (default 540)\n"
                 "  --lights N        point lights, 0 to 16000 (default 300)\n"
                 "  --gbuffer L       show a G-buffer layer instead of the lit picture\n"
                 "  --no-vsync        present without waiting for the display\n"
                 "  --validation      enable the validation layer with synchronization validation\n"
                 "  --inject-device-loss F  fault injection: lose the GPU device at frame F (exit code 3)\n"
                 "  --inject-oom N    fault injection: fail device-memory allocation number N and every one after it\n",
                 program);
    return code;
}

} // namespace

int main(int argc, char** argv) {
    LogRhi.SetVerbosity(LogVerbosity::Info); // the frame-time line is Info
    Options options;
    const char* frames_env = std::getenv("RENDERER_DEMO_FRAMES");
    options.frames = frames_env != nullptr ? std::atol(frames_env) : 0;
    for (int i = 1; i < argc; ++i) {
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
    return options.screenshot != nullptr ? RunScreenshot(options) : RunWindow(options);
}
