// Phase 15 Day 190: the UI pass. A ui::DrawList becomes one render-graph pass on a real device (Vulkan): rectangles, rounded rects with borders, images, clips,
// layers and text land where the list says; the pass follows the target's format; a device reset and a missing image are survived.
#include <cmath>
#include <cstdlib>
#include <cstring>

#include <catch2/catch_test_macros.hpp>

#include "backend_fixture.h"
#include "font_support.h"
#include "image_compare.h"
#include "renderer/device_resources.h"
#include "renderer/render_graph.h"
#include "renderer/text_renderer.h"
#include "renderer/transient_pool.h"
#include "renderer/ui_renderer.h"
#include "reset_rig.h"
#include "rhi/device_host.h"
#include "rhi/null/null_device.h"
#include "text/glyph_atlas.h"
#include "text/layout.h"
#include "ui/draw_list.h"
#include "ui/draw_list_canvas.h"

using namespace engine_placeholder;
using text_test::Opened;

namespace {

struct UiBackground {
    renderer::GraphResource target;
    f32 clear[4] = {0, 0, 0, 1};
};

void UiBackgroundPass(const renderer::PassExecuteContext& ctx, void* user) {
    const UiBackground* b = static_cast<const UiBackground*>(user);
    rhi::RenderingDesc rd;
    rd.color_count = 1;
    rd.color[0].texture = ctx.graph->ImportedTexture(b->target);
    for (int i = 0; i < 4; ++i) rd.color[0].clear_color[i] = b->clear[i];
    ctx.list->BeginRendering(rd);
    ctx.list->EndRendering();
}

// A frame: clear the target, then the UI pass (and optionally a text-renderer pass) on top.
struct UiFrame {
    rhi::Device* device = nullptr;
    renderer::TransientPool* pool = nullptr;
    u64 number = 0;
    u32 width = 0;
    u32 height = 0;
    rhi::TextureHandle texture;
    rhi::Format format = rhi::Format::RGBA8_UNorm;
    bool owns_texture = false;

    bool MakeTarget(rhi::Device* d, u32 w, u32 h, rhi::Format f) {
        device = d;
        width = w;
        height = h;
        format = f;
        rhi::TextureDesc td;
        td.width = w;
        td.height = h;
        td.format = f;
        td.usage = rhi::TextureUsage::ColorAttachment | rhi::TextureUsage::CopySrc;
        texture = d->CreateTexture(td);
        owns_texture = true;
        return texture.IsValid();
    }
    void Destroy() {
        if (owns_texture && texture.IsValid()) device->DestroyTexture(texture);
        texture = {};
    }

    // Returns the number of passes the graph held.
    u32 Run(renderer::UiRenderer* ui, const ui::DrawList* list, const f32 (&clear)[4], gpu_test::Image* image) {
        ArenaAllocator arena(4u << 20);
        renderer::RenderGraph graph(&arena, renderer::RenderGraphDesc{});
        pool->BeginFrame(number++);
        UiBackground bg;
        for (int i = 0; i < 4; ++i) bg.clear[i] = clear[i];
        bg.target = graph.ImportTexture("target", texture, rhi::ResourceState::Undefined, rhi::ResourceState::ColorAttachment, false);
        graph.AddPass("background", &UiBackgroundPass, &bg).Write(bg.target, renderer::GraphUsage::ColorAttachment);
        if (ui != nullptr) ui->AddToGraph(&graph, list, bg.target, format, width, height);
        const u32 passes = graph.PassCount();
        device->BeginFrame();
        rhi::CommandList* cl = device->CreateCommandList();
        cl->Begin();
        REQUIRE(graph.Execute(pool, cl));
        cl->End();
        rhi::CommandList* lists[] = {cl};
        REQUIRE(device->Submit(lists, 1) != 0);
        device->EndFrame();
        device->WaitIdle();
        pool->EndFrame();
        if (image != nullptr) {
            image->width = width;
            image->height = height;
            image->rgba.assign(static_cast<size_t>(width) * height * 4, 0);
            REQUIRE(device->ReadbackTexture(texture, 0, 0, image->rgba.data(), image->rgba.size(), rhi::ResourceState::ColorAttachment));
        }
        return passes;
    }
};

const f32 kBlack[4] = {0, 0, 0, 1};

bool IsPixel(const gpu_test::Image& image, u32 x, u32 y, int r, int g, int b, int tolerance = 1) {
    const u8* p = image.Pixel(x, y);
    return std::abs(p[0] - r) <= tolerance && std::abs(p[1] - g) <= tolerance && std::abs(p[2] - b) <= tolerance;
}

constexpr u32 kRed = 0xFF0000FFu;   // red in the lowest byte
constexpr u32 kGreen = 0xFF00FF00u;
constexpr u32 kBlue = 0xFFFF0000u;
constexpr u32 kWhite = 0xFFFFFFFFu;

struct Scene {
    ui::DrawList list;
    ui::DrawListCanvas canvas;
    explicit Scene(text::GlyphAtlas* atlas = nullptr, f32 scale = 1.0f, u32 w = 64, u32 h = 48) : canvas(&list, atlas) { canvas.Begin(scale, w, h); }
};

} // namespace

TEST_CASE("UI pass: a frame with an empty list declares no pass", "[renderer][ui][pass]") {
    rhi::NullDevicePtr device = rhi::CreateNullDevice();
    renderer::UiRenderer ui_renderer;
    REQUIRE(ui_renderer.Create(device.Get(), nullptr, renderer::UiRendererDesc{}, nullptr));
    ArenaAllocator arena(1u << 20);
    renderer::RenderGraph graph(&arena, renderer::RenderGraphDesc{});
    const renderer::GraphResource target = graph.ImportTexture("t", rhi::TextureHandle(), rhi::ResourceState::Undefined, rhi::ResourceState::ColorAttachment, false);
    ui::DrawList empty;
    ui_renderer.AddToGraph(&graph, &empty, target, rhi::Format::RGBA8_UNorm, 64, 48);
    CHECK(graph.PassCount() == 0);
    CHECK(ui_renderer.Stats().passes == 0);
    Scene scene;
    scene.canvas.DrawRect({0, 0, 10, 10}, kRed);
    ui_renderer.AddToGraph(&graph, &scene.list, target, rhi::Format::RGBA8_UNorm, 64, 48);
    CHECK(graph.PassCount() == 1);
    CHECK(ui_renderer.Stats().passes == 1);
}

TEST_CASE("UI pass: a filled rectangle is exact inside and the target is untouched outside", "[renderer][ui][gpu][pass]") {
    FOR_EACH_GPU_BACKEND(backend) {
        if (backend != rhi::Backend::Vulkan) continue; // the UI shaders have SPIR-V only for now (KNOWN-ISSUES.md)
        gpu_test::BackendValidation validation(backend);
        rhi::DevicePtr device = gpu_test::CreateTestDevice(backend);
        renderer::UiRenderer ui_renderer;
        REQUIRE(ui_renderer.Create(device.Get(), nullptr, renderer::UiRendererDesc{}, nullptr));
        renderer::TransientPool pool(device.Get(), renderer::TransientPoolDesc{});
        UiFrame frame;
        frame.pool = &pool;
        REQUIRE(frame.MakeTarget(device.Get(), 64, 48, rhi::Format::RGBA8_UNorm));
        Scene scene;
        scene.canvas.DrawRect({10, 8, 20, 12}, kRed);
        gpu_test::Image image;
        CHECK(frame.Run(&ui_renderer, &scene.list, kBlack, &image) == 2);
        u32 inside = 0, outside_changed = 0;
        for (u32 y = 0; y < 48; ++y) {
            for (u32 x = 0; x < 64; ++x) {
                const bool in = x >= 10 && x < 30 && y >= 8 && y < 20;
                if (in && IsPixel(image, x, y, 255, 0, 0, 0)) ++inside;
                if (!in && !IsPixel(image, x, y, 0, 0, 0, 0)) ++outside_changed;
            }
        }
        CHECK(inside == 20 * 12);
        CHECK(outside_changed == 0);
        CHECK(ui_renderer.Stats().draws == 1);
        device->WaitIdle();
        frame.Destroy();
    }
}

TEST_CASE("UI pass: a rounded rect with a border has smooth corners, a ring and an anti-aliased edge", "[renderer][ui][gpu][pass]") {
    FOR_EACH_GPU_BACKEND(backend) {
        if (backend != rhi::Backend::Vulkan) continue;
        gpu_test::BackendValidation validation(backend);
        rhi::DevicePtr device = gpu_test::CreateTestDevice(backend);
        renderer::UiRenderer ui_renderer;
        REQUIRE(ui_renderer.Create(device.Get(), nullptr, renderer::UiRendererDesc{}, nullptr));
        renderer::TransientPool pool(device.Get(), renderer::TransientPoolDesc{});
        UiFrame frame;
        frame.pool = &pool;
        REQUIRE(frame.MakeTarget(device.Get(), 64, 48, rhi::Format::RGBA8_UNorm));
        Scene scene;
        scene.canvas.DrawRoundedRect({8, 8, 48, 32}, ui::CornerRadii(10), kGreen);
        scene.canvas.DrawBorder({8, 8, 48, 32}, ui::CornerRadii(10), 2, kWhite);
        gpu_test::Image image;
        frame.Run(&ui_renderer, &scene.list, kBlack, &image);
        CHECK(IsPixel(image, 32, 24, 0, 255, 0, 1));      // the middle: the fill
        CHECK(IsPixel(image, 8, 8, 0, 0, 0, 0));          // the sharp corner of the box is outside the rounded shape
        CHECK(IsPixel(image, 32, 8, 255, 255, 255, 40));  // the top edge row is in the border ring
        CHECK(IsPixel(image, 32, 9, 255, 255, 255, 40));  // two pixels thick
        CHECK(IsPixel(image, 32, 10, 0, 255, 0, 1));      // the fill again just inside the ring
        // The curve is anti-aliased: along the corner arc some pixel is neither the background nor a pure ring/fill colour.
        u32 blended = 0;
        for (u32 y = 8; y < 18; ++y) {
            for (u32 x = 8; x < 18; ++x) {
                const u8* p = image.Pixel(x, y);
                const bool pure = (p[0] == 0 && p[1] == 0 && p[2] == 0) || (p[0] == 255 && p[1] == 255 && p[2] == 255) || (p[0] == 0 && p[1] == 255 && p[2] == 0);
                if (!pure) ++blended;
            }
        }
        CHECK(blended > 4);
        device->WaitIdle();
        frame.Destroy();
    }
}

TEST_CASE("UI pass: a clip hides everything outside it, and layers draw in order", "[renderer][ui][gpu][pass]") {
    FOR_EACH_GPU_BACKEND(backend) {
        if (backend != rhi::Backend::Vulkan) continue;
        gpu_test::BackendValidation validation(backend);
        rhi::DevicePtr device = gpu_test::CreateTestDevice(backend);
        renderer::UiRenderer ui_renderer;
        REQUIRE(ui_renderer.Create(device.Get(), nullptr, renderer::UiRendererDesc{}, nullptr));
        renderer::TransientPool pool(device.Get(), renderer::TransientPoolDesc{});
        UiFrame frame;
        frame.pool = &pool;
        REQUIRE(frame.MakeTarget(device.Get(), 64, 48, rhi::Format::RGBA8_UNorm));
        Scene scene;
        scene.canvas.PushClip({0, 0, 20, 20});
        scene.canvas.DrawRect({0, 0, 64, 48}, kRed); // the whole target, cut to the clip
        scene.canvas.PopClip();
        scene.canvas.BeginLayer(2);
        scene.canvas.DrawRect({30, 10, 20, 20}, kBlue); // the popup, visited first ...
        scene.canvas.EndLayer();
        scene.canvas.DrawRect({25, 5, 20, 20}, kGreen); // ... but a base-layer draw that comes after it must lie under it
        scene.list.SortLayers();
        gpu_test::Image image;
        frame.Run(&ui_renderer, &scene.list, kBlack, &image);
        CHECK(IsPixel(image, 10, 10, 255, 0, 0, 0));
        CHECK(IsPixel(image, 22, 10, 0, 0, 0, 0)); // outside the clip and outside every other rectangle: untouched
        CHECK(IsPixel(image, 5, 30, 0, 0, 0, 0));
        CHECK(IsPixel(image, 35, 15, 0, 0, 255, 0));  // overlap of green and blue: blue wins (layer 2)
        CHECK(IsPixel(image, 27, 7, 0, 255, 0, 0));   // green only
        device->WaitIdle();
        frame.Destroy();
    }
}

TEST_CASE("UI pass: sRGB and UNORM targets show the colour that was written", "[renderer][ui][gpu][pass]") {
    FOR_EACH_GPU_BACKEND(backend) {
        if (backend != rhi::Backend::Vulkan) continue;
        gpu_test::BackendValidation validation(backend);
        rhi::DevicePtr device = gpu_test::CreateTestDevice(backend);
        renderer::UiRenderer ui_renderer;
        renderer::UiRendererDesc desc;
        desc.color_format = rhi::Format::RGBA8_UNorm; // made for a linear target ...
        REQUIRE(ui_renderer.Create(device.Get(), nullptr, desc, nullptr));
        renderer::TransientPool pool(device.Get(), renderer::TransientPoolDesc{});
        UiFrame frame;
        frame.pool = &pool;
        REQUIRE(frame.MakeTarget(device.Get(), 32, 32, rhi::Format::RGBA8_SRGB)); // ... drawn into an sRGB one: the pipeline follows
        Scene scene(nullptr, 1.0f, 32, 32);
        scene.canvas.DrawRect({0, 0, 32, 32}, 0xFF808080u);
        gpu_test::Image image;
        CHECK(frame.Run(&ui_renderer, &scene.list, kBlack, &image) == 2);
        CHECK(IsPixel(image, 16, 16, 128, 128, 128, 1));
        frame.Destroy();
        REQUIRE(frame.MakeTarget(device.Get(), 32, 32, rhi::Format::RGBA8_UNorm)); // and back
        CHECK(frame.Run(&ui_renderer, &scene.list, kBlack, &image) == 2);
        CHECK(IsPixel(image, 16, 16, 128, 128, 128, 1));
        device->WaitIdle();
        frame.Destroy();
    }
}

TEST_CASE("UI pass: an application image is drawn through its owner, tinted, and skipped when the owner has no texture", "[renderer][ui][gpu][pass]") {
    FOR_EACH_GPU_BACKEND(backend) {
        if (backend != rhi::Backend::Vulkan) continue;
        gpu_test::BackendValidation validation(backend);
        rhi::DevicePtr device = gpu_test::CreateTestDevice(backend);
        renderer::UiRenderer ui_renderer;
        REQUIRE(ui_renderer.Create(device.Get(), nullptr, renderer::UiRendererDesc{}, nullptr));
        renderer::TextureImage image_pixels;
        image_pixels.desc.width = 2;
        image_pixels.desc.height = 2;
        image_pixels.desc.format = rhi::Format::RGBA8_UNorm;
        image_pixels.desc.usage = rhi::TextureUsage::Sampled;
        image_pixels.pixels.Resize(16);
        for (usize i = 0; i < 4; ++i) {
            image_pixels.pixels[i * 4 + 0] = 200; // a flat (200, 100, 50)
            image_pixels.pixels[i * 4 + 1] = 100;
            image_pixels.pixels[i * 4 + 2] = 50;
            image_pixels.pixels[i * 4 + 3] = 255;
        }
        renderer::GpuTexture texture;
        REQUIRE(texture.Create(device.Get(), static_cast<renderer::TextureImage&&>(image_pixels), nullptr, "ui test image"));
        renderer::GpuTextureImage source(&texture);
        ui_renderer.SetImage(5, &source);
        renderer::TransientPool pool(device.Get(), renderer::TransientPoolDesc{});
        UiFrame frame;
        frame.pool = &pool;
        REQUIRE(frame.MakeTarget(device.Get(), 64, 48, rhi::Format::RGBA8_UNorm));
        Scene scene;
        scene.canvas.DrawImage({4, 4, 16, 16}, 5, {0, 0, 1, 1}, kWhite);
        scene.canvas.DrawImage({30, 4, 16, 16}, 5, {0, 0, 1, 1}, 0xFF808080u); // tinted to half
        scene.canvas.DrawImage({4, 28, 16, 16}, 9, {0, 0, 1, 1}, kWhite);       // an image nobody registered
        gpu_test::Image image;
        frame.Run(&ui_renderer, &scene.list, kBlack, &image);
        CHECK(IsPixel(image, 12, 12, 200, 100, 50, 1));
        CHECK(IsPixel(image, 38, 12, 100, 50, 25, 2));
        CHECK(IsPixel(image, 12, 36, 0, 0, 0, 0));
        CHECK(ui_renderer.Stats().dropped_commands >= 1);
        // An owner whose texture is gone draws nothing and does not crash.
        ui_renderer.ClearImage(5);
        frame.Run(&ui_renderer, &scene.list, kBlack, &image);
        CHECK(IsPixel(image, 12, 12, 0, 0, 0, 0));
        device->WaitIdle();
        frame.Destroy();
    }
}

TEST_CASE("UI pass: text through the atlas matches the text renderer's pixels", "[renderer][ui][text][gpu][pass]") {
    FOR_EACH_GPU_BACKEND(backend) {
        if (backend != rhi::Backend::Vulkan) continue;
        gpu_test::BackendValidation validation(backend);
        rhi::DevicePtr device = gpu_test::CreateTestDevice(backend);
        Opened o;
        text::TextStyle style;
        style.font = &o.font;
        style.pixel_size = 16;
        style.color = kWhite;
        text::TextLayout layout;
        REQUIRE(text::Layout(StringView("Hello, ui!"), style, text::LayoutOptions{}, &layout).Ok());
        renderer::TransientPool pool(device.Get(), renderer::TransientPoolDesc{});
        UiFrame frame;
        frame.pool = &pool;
        REQUIRE(frame.MakeTarget(device.Get(), 128, 32, rhi::Format::RGBA8_UNorm));

        // The reference: the text renderer.
        text::GlyphAtlas text_atlas;
        renderer::TextRenderer text_renderer;
        REQUIRE(text_renderer.Create(device.Get(), &text_atlas, renderer::TextRendererDesc{}, nullptr));
        text_renderer.BeginFrame();
        REQUIRE(text_renderer.Add(layout, style, 6, 6));
        gpu_test::Image reference;
        {
            ArenaAllocator arena(4u << 20);
            renderer::RenderGraph graph(&arena, renderer::RenderGraphDesc{});
            pool.BeginFrame(frame.number++);
            UiBackground bg;
            bg.target = graph.ImportTexture("target", frame.texture, rhi::ResourceState::Undefined, rhi::ResourceState::ColorAttachment, false);
            graph.AddPass("background", &UiBackgroundPass, &bg).Write(bg.target, renderer::GraphUsage::ColorAttachment);
            text_renderer.AddToGraph(&graph, bg.target, frame.format, frame.width, frame.height);
            device->BeginFrame();
            rhi::CommandList* cl = device->CreateCommandList();
            cl->Begin();
            REQUIRE(graph.Execute(&pool, cl));
            cl->End();
            rhi::CommandList* lists[] = {cl};
            REQUIRE(device->Submit(lists, 1) != 0);
            device->EndFrame();
            device->WaitIdle();
            pool.EndFrame();
            reference.width = frame.width;
            reference.height = frame.height;
            reference.rgba.assign(static_cast<size_t>(frame.width) * frame.height * 4, 0);
            REQUIRE(device->ReadbackTexture(frame.texture, 0, 0, reference.rgba.data(), reference.rgba.size(), rhi::ResourceState::ColorAttachment));
        }

        // The UI renderer with its own atlas and pages.
        text::GlyphAtlas ui_atlas;
        renderer::GlyphAtlasTexture pages;
        REQUIRE(pages.Create(device.Get(), &ui_atlas, nullptr));
        renderer::UiRenderer ui_renderer;
        REQUIRE(ui_renderer.Create(device.Get(), &pages, renderer::UiRendererDesc{}, nullptr));
        Scene scene(&ui_atlas, 1.0f, 128, 32);
        scene.canvas.DrawText(layout, style, {6, 6}, kWhite);
        REQUIRE(scene.list.stats.glyphs_dropped == 0);
        gpu_test::Image image;
        CHECK(frame.Run(&ui_renderer, &scene.list, kBlack, &image) == 2);
        u32 inked = 0, different = 0;
        for (u32 y = 0; y < frame.height; ++y) {
            for (u32 x = 0; x < frame.width; ++x) {
                const u8* a = reference.Pixel(x, y);
                const u8* b = image.Pixel(x, y);
                if (a[0] > 0) ++inked;
                if (std::abs(a[0] - b[0]) > 1 || std::abs(a[1] - b[1]) > 1 || std::abs(a[2] - b[2]) > 1) ++different;
            }
        }
        CHECK(inked > 100);
        CHECK(different == 0);
        device->WaitIdle();
        frame.Destroy();
    }
}

TEST_CASE("UI pass: commands past the vertex budget are dropped and counted", "[renderer][ui][gpu][pass]") {
    FOR_EACH_GPU_BACKEND(backend) {
        if (backend != rhi::Backend::Vulkan) continue;
        gpu_test::BackendValidation validation(backend);
        rhi::DevicePtr device = gpu_test::CreateTestDevice(backend);
        renderer::UiRenderer ui_renderer;
        renderer::UiRendererDesc desc;
        desc.max_vertices = 8; // two rectangles
        desc.max_indices = 12;
        REQUIRE(ui_renderer.Create(device.Get(), nullptr, desc, nullptr));
        renderer::TransientPool pool(device.Get(), renderer::TransientPoolDesc{});
        UiFrame frame;
        frame.pool = &pool;
        REQUIRE(frame.MakeTarget(device.Get(), 64, 48, rhi::Format::RGBA8_UNorm));
        Scene scene;
        scene.canvas.DrawRect({0, 0, 10, 10}, kRed);
        scene.canvas.DrawRect({20, 0, 10, 10}, kRed);
        scene.canvas.PushClip({0, 20, 64, 28});
        scene.canvas.DrawRect({0, 20, 10, 10}, kRed); // a third command, past the budget
        scene.canvas.PopClip();
        gpu_test::Image image;
        frame.Run(&ui_renderer, &scene.list, kBlack, &image);
        CHECK(IsPixel(image, 5, 5, 255, 0, 0, 0));
        CHECK(IsPixel(image, 25, 5, 255, 0, 0, 0));
        CHECK(IsPixel(image, 5, 25, 0, 0, 0, 0));
        CHECK(ui_renderer.Stats().dropped_commands == 1);
        device->WaitIdle();
        frame.Destroy();
    }
}

TEST_CASE("UI pass: the scene looks the same after a device reset and every object is rebuilt", "[renderer][ui][gpu][recreatable]") {
    FOR_EACH_GPU_BACKEND(backend) {
        if (backend != rhi::Backend::Vulkan) continue;
        gpu_test::BackendValidation validation(backend);
        renderer::DeviceResources registry; // destroyed last
        reset_rig::Target target;
        rhi::DeviceHost host;
        host.AddObserver(&registry, 0);
        rhi::DeviceCreateInfo info;
        info.backend = backend;
        info.validation = rhi::ValidationSetting::Off;
        info.pipeline_cache = false;
        if (!host.Create(info, rhi::SwapchainDesc{})) SKIP("no device");
        REQUIRE(target.Make(host.GetDevice()));
        renderer::ScopedRegistration target_registration;
        target_registration.Attach(&registry, &target, renderer::tiers::kTextures);
        Opened o;
        text::GlyphAtlas atlas;
        renderer::GlyphAtlasTexture pages;
        REQUIRE(pages.Create(host.GetDevice(), &atlas, &registry));
        renderer::UiRenderer ui_renderer;
        renderer::UiRendererDesc desc;
        desc.color_format = rhi::Format::RGBA8_SRGB;
        REQUIRE(ui_renderer.Create(host.GetDevice(), &pages, desc, &registry));
        text::TextStyle style;
        style.font = &o.font;
        style.pixel_size = 14;
        text::TextLayout layout;
        REQUIRE(text::Layout(StringView("reset"), style, text::LayoutOptions{}, &layout).Ok());
        Scene scene(&atlas, 1.0f, target.width, target.height);
        scene.canvas.DrawRoundedRect({4, 4, 60, 30}, ui::CornerRadii(6), kBlue);
        scene.canvas.DrawBorder({4, 4, 60, 30}, ui::CornerRadii(6), 1, kWhite);
        scene.canvas.DrawText(layout, style, {10, 10}, kWhite);
        scene.list.SortLayers();

        auto render = [&](gpu_test::Image* image) {
            rhi::Device* device = host.GetDevice();
            renderer::TransientPool pool(device, renderer::TransientPoolDesc{});
            UiFrame frame;
            frame.pool = &pool;
            frame.device = device;
            frame.texture = target.handle;
            frame.width = target.width;
            frame.height = target.height;
            frame.format = rhi::Format::RGBA8_SRGB;
            REQUIRE(frame.Run(&ui_renderer, &scene.list, kBlack, image) == 2);
            pool.Release();
        };
        gpu_test::Image before, after;
        render(&before);
        const rhi::ResourceCounts counts_before = host.GetDevice()->Resources();
        REQUIRE(host.Recreate(info, rhi::SwapchainDesc{}));
        REQUIRE(ui_renderer.Live());
        render(&after);
        const rhi::ResourceCounts counts_after = host.GetDevice()->Resources();
        CHECK(counts_after.buffers == counts_before.buffers);
        CHECK(counts_after.textures == counts_before.textures);
        CHECK(counts_after.pipelines == counts_before.pipelines);
        REQUIRE(before.Valid());
        CHECK(before.rgba == after.rgba);
        host.GetDevice()->WaitIdle();
        ui_renderer.Destroy();
        pages.Destroy();
        target_registration.Reset();
        target.Release();
        host.Destroy();
    }
}
