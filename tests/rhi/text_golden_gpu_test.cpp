// Phase 14 Day 182: golden images of the text pass, and the text overlay of the demo frame through the reset and the loss.
//
// The scene is "Hello, world!" at 16 and 48 px, a wrapped paragraph and a line of kerned pairs, white on dark blue. The glyph bitmaps are rasterized on the CPU and
// the quads sit on whole pixels, so the picture is the same on any GPU within rounding; the tolerance is two levels. Only Vulkan writes the golden
// (ENGINE_RHI_UPDATE_GOLDEN=1); Direct3D 12 is checked against it once the text shaders have DXIL (KNOWN-ISSUES.md).

#include <catch2/catch_test_macros.hpp>

#include "backend_fixture.h"
#include "image_compare.h"
#include "io/path.h"
#include "render_target.h"
#include "renderer/text_renderer.h"
#include "reset_rig.h"
#include "text/font.h"
#include "text/glyph_atlas.h"
#include "text/layout.h"
#include "cstr/mem_ops.h"

using namespace engine_placeholder;
using namespace engine_placeholder::renderer;

namespace {

constexpr u32 kGoldenW = 400;
constexpr u32 kGoldenH = 230;

struct FontOpened {
    text::FontLibrary library;
    text::Font font;
    FontOpened() {
        REQUIRE(library.Initialize().Ok());
        REQUIRE(text::Font::OpenFile(library, io::Path(StringView(ENGINE_TEXT_FONT_FILE)), text::FontOptions{}, &font).Ok());
    }
};

gpu_test::Image DrawScene(rhi::Device* device, TextRenderer* text, FontOpened* f) {
    gpu_test::RenderTarget target(device, kGoldenW, kGoldenH);
    REQUIRE(target.Valid());
    auto style = [&](u32 size) {
        text::TextStyle s;
        s.font = &f->font;
        s.pixel_size = size;
        s.color = 0xFFFFFFFFu;
        return s;
    };
    device->BeginFrame();
    text->BeginFrame();
    REQUIRE(text->Draw(StringView("Hello, world!"), style(16), text::LayoutOptions{}, 8, 6));
    REQUIRE(text->Draw(StringView("Hello, world!"), style(48), text::LayoutOptions{}, 8, 26));
    text::LayoutOptions wrapped;
    wrapped.max_width = 190;
    REQUIRE(text->Draw(StringView("The quick brown fox jumps over the lazy dog and keeps running through the field."), style(14), wrapped, 8, 90));
    REQUIRE(text->Draw(StringView("AVATAR To Ty We Yo"), style(30), text::LayoutOptions{}, 8, 178));
    rhi::CommandList* list = device->CreateCommandList();
    list->Begin();
    list->Barrier(target.Texture(), rhi::ResourceState::Undefined, rhi::ResourceState::ColorAttachment);
    rhi::RenderingDesc pass;
    pass.color_count = 1;
    pass.color[0].texture = target.Texture();
    pass.color[0].clear_color[0] = 0.0f;
    pass.color[0].clear_color[1] = 0.0f;
    pass.color[0].clear_color[2] = 0.25f;
    list->BeginRendering(pass);
    REQUIRE(text->Record(list, kGoldenW, kGoldenH));
    list->EndRendering();
    list->End();
    rhi::CommandList* lists[] = {list};
    REQUIRE(device->Submit(lists, 1) != 0);
    device->EndFrame();
    device->WaitIdle();
    gpu_test::Image image;
    REQUIRE(target.Readback(&image));
    return image;
}

} // namespace

TEST_CASE("The text overlay matches its golden image", "[rhi][gpu][golden][text]") {
    FOR_EACH_GPU_BACKEND(backend) {
        gpu_test::BackendValidation validation(backend);
        rhi::DevicePtr device = gpu_test::CreateTestDevice(backend);
        FontOpened f;
        text::GlyphAtlas atlas;
        TextRenderer text;
        TextRendererDesc desc;
        desc.color_format = rhi::Format::RGBA8_UNorm;
        REQUIRE(text.Create(device.Get(), &atlas, desc, nullptr));
        const gpu_test::Image image = DrawScene(device.Get(), &text, &f);
        if (gpu_test::UpdatingGoldens()) {
            REQUIRE(gpu_test::WritePpm(gpu_test::GoldenPath("text_overlay.ppm").c_str(), image));
            WARN("ENGINE_RHI_UPDATE_GOLDEN=1: tests/rhi/golden/text_overlay.ppm was rewritten; inspect it before committing");
        } else {
            gpu_test::Image golden;
            REQUIRE(gpu_test::ReadPpm(gpu_test::GoldenPath("text_overlay.ppm").c_str(), &golden));
            gpu_test::CompareOptions co;
            co.tolerance = 2;
            co.edge_tolerance = 2;
            co.max_edge_miss = 0.0;
            co.max_interior_miss = 0.0;
            const gpu_test::CompareResult result = gpu_test::Compare(golden, image, co, "text_overlay_vulkan");
            INFO(result.message);
            CHECK(result.same);
        }
        device->WaitIdle();
    }
}

TEST_CASE("The demo frame's text overlay survives a device reset, a pipeline rebuild and a lost device", "[rhi][gpu][reset][text]") {
    FOR_EACH_GPU_BACKEND(backend) {
        gpu_test::BackendValidation validation(backend);
        reset_rig::Rig rig;
        rig.font_path = ENGINE_TEXT_FONT_FILE;
        rig.target.width = 320;
        rig.target.height = 180;
        if (!rig.Create(backend, 200, 60, false, nullptr, ENGINE_RENDERER_DEMO_ASSET_DIR, /*validation=*/true)) {
            rig.TearDown();
            SKIP("no suitable " << gpu_test::BackendTag(backend) << " device");
        }
        REQUIRE(rig.frame.OverlayAvailable());
        REQUIRE(rig.frame.SetOverlay(true, StringView("Hello, world!"), StringView("status line")));
        auto shoot = [&](gpu_test::Image* image) {
            REQUIRE(rig.Render() == FrameStatus::Ok);
            REQUIRE(rig.Read(image));
        };
        // The overlay region: the top 80 rows hold the greeting and the status line.
        auto top_differs = [](const gpu_test::Image& a, const gpu_test::Image& b) {
            return MemCompare(a.rgba.data(), b.rgba.data(), static_cast<usize>(a.width) * 80 * 4) != 0;
        };
        gpu_test::Image before;
        shoot(&before);
        const u64 drawn = rig.frame.OverlayStats().glyphs_drawn;
        REQUIRE(drawn > 0);

        // Without the overlay the same region differs: the text is really in the picture.
        REQUIRE(rig.frame.SetOverlay(false, StringView("Hello, world!"), StringView("status line")));
        gpu_test::Image bare;
        shoot(&bare);
        CHECK(top_differs(bare, before));
        REQUIRE(rig.frame.SetOverlay(true, StringView("Hello, world!"), StringView("status line")));

        // A device reset: the same picture, byte for byte.
        const ApplyResult reset = rig.ResetDevice(false);
        REQUIRE(reset.ok);
        CHECK(reset.performed == CostClass::DeviceReset);
        gpu_test::Image after_reset;
        shoot(&after_reset);
        CHECK(before.rgba == after_reset.rgba);

        // A forced reset through the renderer, as the demo's F9 does.
        REQUIRE(rig.R().ForceDeviceReset().ok);
        gpu_test::Image after_forced;
        shoot(&after_forced);
        CHECK(before.rgba == after_forced.rgba);

        // A pipeline rebuild: the text overlay keeps drawing (the picture below it may change with the shader quality, so only the overlay's pixels are compared
        // against the picture without any overlay, which must differ in the same region).
        RenderSettings wanted = rig.R().Settings();
        wanted.SetEnum(settings::kShaderQuality, 0);
        const ApplyResult rebuilt = rig.R().ApplySettings(wanted);
        REQUIRE(rebuilt.ok);
        CHECK(rebuilt.performed == CostClass::PipelineRebuild);
        gpu_test::Image after_rebuild;
        shoot(&after_rebuild);
        CHECK(rig.frame.OverlayStats().glyphs_drawn > drawn);
        wanted.SetEnum(settings::kShaderQuality, rig.initial.GetEnum(settings::kShaderQuality));
        REQUIRE(rig.R().ApplySettings(wanted).ok);

        // A lost device: the frame reports it, the renderer recovers, and the next frame has the text again, identical to the first.
        rig.host.GetDevice()->InjectDeviceLoss();
        CHECK(rig.Render() == FrameStatus::DeviceLost);
        gpu_test::Image recovered;
        shoot(&recovered);
        CHECK(before.rgba == recovered.rgba);
        rig.TearDown();
    }
}
