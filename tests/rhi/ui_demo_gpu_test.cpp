// Phase 15 Day 204: the ui_demo's pictures. Six golden images of the demo (1x and 2x, light and dark, a menu open, the About box, the in-world screen after a ray-cast click), and the demo's
// picture through every kind of change the renderer can make while the UI is on screen: a dynamic change, a pipeline rebuild, a device reset, a forced reset and a lost device.
// Only Vulkan writes the goldens (ENGINE_RHI_UPDATE_GOLDEN=1); Direct3D 12 waits for the UI shaders' DXIL (KNOWN-ISSUES.md).
#include <catch2/catch_test_macros.hpp>

#include <cstdio>

#include "backend_fixture.h"
#include "cube_fixture.h"
#include "image_compare.h"
#include "reset_rig.h"
#include "ui/surface.h"
#include "ui_demo_app.h"
#include "ui_demo_gpu.h"
#include "ui_script.h"

using namespace engine_placeholder;

namespace {

constexpr u32 kPixelsW = 640; // the picture's size in pixels; the 2x case needs a bigger one to hold the left panel
constexpr u32 kPixelsH = 400;

class NoHost final : public ui_demo::ScriptHost {
public:
    void Resize(u32, u32) override {}
    void Screenshot(StringView) override {}
};

// The demo on the reset rig: the scene with its screen, the surface at `scale`, the app and the GPU parts.
struct UiRig {
    explicit UiRig(f32 scale_in, u32 width = kPixelsW, u32 height = kPixelsH) : scale(scale_in), pixels_w(width), pixels_h(height) {}
    ~UiRig() { TearDown(); }

    bool Create(rhi::Backend backend) {
        rig.screen = true;
        rig.target.width = pixels_w;
        rig.target.height = pixels_h;
        if (!rig.Create(backend, 64, 8, false, nullptr, ENGINE_RENDERER_DEMO_ASSET_DIR, /*validation=*/true)) return false;
        surface = MakeUnique<ui::Surface>(static_cast<f32>(pixels_w) / scale, static_cast<f32>(pixels_h) / scale, scale);
        gpu = MakeUnique<ui_demo::UiDemoGpu>();
        if (!gpu->Create(rig.host.GetDevice(), &rig.registry, &rig.frame, &rig.scene, ENGINE_TEXT_FONT_FILE, rhi::Format::RGBA8_SRGB)) return false;
        app = MakeUnique<ui_demo::UiDemoApp>(surface.Get());
        gpu->Attach(app.Get(), surface.Get());
        return true;
    }
    void TearDown() {
        if (rig.host.GetDevice() != nullptr) rig.host.GetDevice()->WaitIdle();
        app.Reset();
        if (gpu != nullptr) gpu->Destroy();
        gpu.Reset();
        surface.Reset();
        rig.TearDown();
    }
    // Runs `frames` frames, the script's words on the way.
    renderer::FrameStatus Frames(long frames, const char* words = nullptr) {
        ui_demo::UiScript script;
        if (words != nullptr) script.Parse(StringView(words));
        NoHost host;
        renderer::FrameStatus status = renderer::FrameStatus::Ok;
        for (long i = 0; i < frames; ++i) {
            if (!script.Done()) REQUIRE(script.Run(app.Get(), surface.Get(), &host, i));
            status = One();
        }
        return status;
    }
    renderer::FrameStatus One() {
        gpu->Update(0.016f);
        rig.camera = gpu->MakeCamera();
        return rig.Render();
    }
    bool Shoot(gpu_test::Image* image, long frames = 3) {
        if (Frames(frames) != renderer::FrameStatus::Ok) return false;
        return rig.Read(image);
    }
    // Clicks the in-world screen's button through the viewport's pointer, as a person would.
    bool ClickWorldButton() {
        ui_demo::WorldScreen& world = app->World();
        const ui::Rect button = world.GetSurface().Root()->FindByName(StringView("btn_counter"))->Bounds();
        const f32 u = (button.x + button.w * 0.5f) / ui_demo::WorldScreen::kWidth;
        const f32 v = (button.y + button.h * 0.5f) / ui_demo::WorldScreen::kHeight;
        const u32 w = app->ViewportWidth();
        const u32 h = app->ViewportHeight();
        f32 best = 1e9f;
        f32 px = 0;
        f32 py = 0;
        for (u32 y = 0; y < h; y += 2) {
            for (u32 x = 0; x < w; x += 2) {
                ui::Ray3 ray;
                if (!app->Camera().MakeRay(static_cast<f32>(x), static_cast<f32>(y), w, h, app->SceneScale(), &ray)) return false;
                const ui::SurfaceHit hit = ui::RayToSurfacePoint(ray, ui_demo::WorldScreen::Quad());
                if (!hit.hit) continue;
                const f32 d = (hit.u - u) * (hit.u - u) + (hit.v - v) * (hit.v - v);
                if (d < best) {
                    best = d;
                    px = static_cast<f32>(x);
                    py = static_cast<f32>(y);
                }
            }
        }
        if (best > 0.0004f) return false;
        const ui::ViewportInputType types[] = {ui::ViewportInputType::PointerMove, ui::ViewportInputType::PointerDown, ui::ViewportInputType::PointerUp};
        for (ui::ViewportInputType type : types) {
            ui::ViewportInput input;
            input.type = type;
            input.button = os::kMouseLeft;
            input.x = px;
            input.y = py;
            app->OnInput(input);
        }
        return true;
    }

    f32 scale;
    u32 pixels_w;
    u32 pixels_h;
    reset_rig::Rig rig;
    UniquePtr<ui::Surface> surface;
    UniquePtr<ui_demo::UiDemoGpu> gpu;
    UniquePtr<ui_demo::UiDemoApp> app;
};

gpu_test::CompareOptions DemoCompare() {
    gpu_test::CompareOptions co;
    co.tolerance = 6; // GGX highlights and the lights: a few levels of rounding difference
    co.edge_tolerance = 90;
    co.max_edge_miss = 0.3;
    co.max_interior_miss = 0.002;
    return co;
}

struct GoldenCase {
    const char* file;
    const char* name;
    f32 scale;
    const char* words;
    bool click_world;
    u32 width;
    u32 height;
};

const GoldenCase kCases[] = {
    {"ui_demo_1x.ppm", "ui_demo_1x", 1.0f, nullptr, false, kPixelsW, kPixelsH},
    {"ui_demo_2x.ppm", "ui_demo_2x", 2.0f, nullptr, false, 960, 800},
    {"ui_demo_dark.ppm", "ui_demo_dark", 1.0f, "theme:dark", false, kPixelsW, kPixelsH},
    {"ui_demo_menu.ppm", "ui_demo_menu", 1.0f, "openmenu:View", false, kPixelsW, kPixelsH},
    {"ui_demo_modal.ppm", "ui_demo_modal", 1.0f, "menu:Help/About", false, kPixelsW, kPixelsH},
    {"ui_demo_world.ppm", "ui_demo_world", 1.0f, nullptr, true, kPixelsW, kPixelsH},
};

} // namespace

TEST_CASE("The ui_demo matches its golden images", "[rhi][gpu][golden][ui_demo]") {
    FOR_EACH_GPU_BACKEND(backend) {
        if (backend != rhi::Backend::Vulkan) continue; // no DXIL for the UI shaders yet
        for (const GoldenCase& c : kCases) {
            gpu_test::BackendValidation validation(backend);
            UiRig ui(c.scale, c.width, c.height);
            if (!ui.Create(backend)) {
                ui.TearDown();
                SKIP("no suitable " << gpu_test::BackendTag(backend) << " device");
            }
            INFO(c.name);
            ui.Frames(2, c.words);
            if (c.click_world) {
                ui.app->Camera().Set(0.3f, 0.2f, 2.5f);
                ui.Frames(2);
                REQUIRE(ui.ClickWorldButton());
                ui.Frames(2); // the surface handles the posted events in its frame
                CHECK(ui.app->World().Clicks() == 1);
            }
            gpu_test::Image image;
            REQUIRE(ui.Shoot(&image));
            cube_test::CheckGolden(image, c.file, c.name, backend, DemoCompare());
            ui.TearDown();
        }
    }
}

TEST_CASE("The ui_demo's picture survives every kind of change and a lost device", "[rhi][gpu][reset][ui_demo]") {
    FOR_EACH_GPU_BACKEND(backend) {
        if (backend != rhi::Backend::Vulkan) continue;
        gpu_test::BackendValidation validation(backend);
        UiRig ui(1.0f);
        if (!ui.Create(backend)) {
            ui.TearDown();
            SKIP("no suitable " << gpu_test::BackendTag(backend) << " device");
        }
        gpu_test::Image baseline;
        REQUIRE(ui.Shoot(&baseline));
        cube_test::CheckGolden(baseline, "ui_demo_1x.ppm", "ui_demo_reset_baseline", backend, DemoCompare());
        const u64 viewport_resizes = ui.gpu->Viewport().ResizeCount();
        const u64 paints = ui.gpu->WorldTarget().Paints();
        auto same_as_baseline = [&](const char* what) {
            INFO(what);
            gpu_test::Image image;
            REQUIRE(ui.Shoot(&image));
            CHECK(ui.gpu->Viewport().Live());
            CHECK(ui.gpu->WorldTarget().Handle().IsValid());
            usize different = 0;
            usize first = 0;
            for (usize i = 0; i < image.rgba.size() && i < baseline.rgba.size(); ++i) {
                if (image.rgba[i] != baseline.rgba[i] && different++ == 0) first = i / 4;
            }
            INFO("first different pixel (" << first % image.width << ", " << first / image.width << ") is " << static_cast<int>(image.rgba[first * 4]) << "," << static_cast<int>(image.rgba[first * 4 + 1]) << "," << static_cast<int>(image.rgba[first * 4 + 2]) << " and was "
                 << static_cast<int>(baseline.rgba[first * 4]) << "," << static_cast<int>(baseline.rgba[first * 4 + 1]) << "," << static_cast<int>(baseline.rgba[first * 4 + 2]));
            CHECK(image.rgba.size() == baseline.rgba.size());
            CHECK(different == 0);
        };

        // A dynamic change.
        renderer::RenderSettings wanted = ui.rig.R().Settings();
        wanted.SetFloat(renderer::settings::kRenderScale, 0.5f);
        renderer::ApplyResult r = ui.rig.R().ApplySettings(wanted);
        REQUIRE(r.ok);
        CHECK(r.performed == renderer::CostClass::Dynamic);
        same_as_baseline("dynamic");
        wanted.SetFloat(renderer::settings::kRenderScale, 1.0f);
        REQUIRE(ui.rig.R().ApplySettings(wanted).ok);

        // A pipeline rebuild (the shader quality changes the lit picture, so it is compared once it is back).
        wanted.SetEnum(renderer::settings::kShaderQuality, 0);
        r = ui.rig.R().ApplySettings(wanted);
        REQUIRE(r.ok);
        CHECK(r.performed == renderer::CostClass::PipelineRebuild);
        gpu_test::Image low;
        REQUIRE(ui.Shoot(&low));
        wanted.SetEnum(renderer::settings::kShaderQuality, ui.rig.initial.GetEnum(renderer::settings::kShaderQuality));
        REQUIRE(ui.rig.R().ApplySettings(wanted).ok);
        same_as_baseline("pipeline rebuild");

        // A device reset, with the counts of live device objects the same afterwards.
        const rhi::ResourceCounts before = ui.rig.host.GetDevice()->Resources();
        r = ui.rig.ResetDevice(false); // the rig starts with sync validation on, so false is the change
        REQUIRE(r.ok);
        CHECK(r.performed == renderer::CostClass::DeviceReset);
        same_as_baseline("device reset");
        r = ui.rig.ResetDevice(true);
        REQUIRE(r.ok);
        same_as_baseline("device reset back");
        reset_rig::ExpectSameCounts(ui.rig.host.GetDevice()->Resources(), before);

        // A forced reset, as the demo's reset word does.
        REQUIRE(ui.rig.R().ForceDeviceReset().ok);
        same_as_baseline("forced reset");

        // A lost device: the frame reports it, the renderer recovers, the next picture is the same.
        ui.rig.host.GetDevice()->InjectDeviceLoss();
        CHECK(ui.One() == renderer::FrameStatus::DeviceLost);
        same_as_baseline("lost device");

        // The viewport's target was not recreated by the size, only by the resets; the world screen was painted again after each (it holds no picture after a reset).
        CHECK(ui.gpu->Viewport().ResizeCount() == viewport_resizes);
        CHECK(ui.gpu->WorldTarget().Paints() > paints);
        ui.TearDown();
    }
}
