// Phase 14 Day 181: the text pass in a render graph and the draw API.
//
// On a real device (Vulkan): "Hello, world!" is drawn over a background by one graph pass and only inside its bounds; a second call overlays the first in
// call order; a frame without text declares no pass; the pipeline follows a change of the target's format; text above the atlas maximum is scaled from the
// maximum raster and is as wide as its layout says. Without a GPU: nothing is allocated per glyph in a steady frame.
#include <cmath>
#include <cstdlib>
#include <cstring>

#include <catch2/catch_test_macros.hpp>

#include "backend_fixture.h"
#include "font_support.h"
#include "image_compare.h"
#include "renderer/render_graph.h"
#include "renderer/text_renderer.h"
#include "renderer/transient_pool.h"
#include "rhi/null/null_device.h"
#include "text/glyph_atlas.h"
#include "text/layout.h"

bool RenderTestCountsAllocations();
unsigned long RenderTestHeapAllocations();

using namespace engine_placeholder;
using text_test::Opened;

namespace {

text::TextStyle PassStyle(Opened& o, u32 size, u32 color) {
    text::TextStyle style;
    style.font = &o.font;
    style.pixel_size = size;
    style.color = color;
    return style;
}

struct Background {
    renderer::GraphResource target;
    f32 clear[4] = {0, 0, 0, 1};
};

void BackgroundPass(const renderer::PassExecuteContext& ctx, void* user) {
    const Background* b = static_cast<const Background*>(user);
    rhi::RenderingDesc rd;
    rd.color_count = 1;
    rd.color[0].texture = ctx.graph->ImportedTexture(b->target);
    for (int i = 0; i < 4; ++i) rd.color[0].clear_color[i] = b->clear[i];
    ctx.list->BeginRendering(rd);
    ctx.list->EndRendering();
}

struct Frame {
    rhi::Device* device = nullptr;
    renderer::TransientPool* pool = nullptr;
    u64 number = 0;
    u32 width = 0;
    u32 height = 0;
    rhi::TextureHandle texture;
    rhi::Format format = rhi::Format::RGBA8_UNorm;

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
        return texture.IsValid();
    }
    void Destroy() {
        if (texture.IsValid()) device->DestroyTexture(texture);
        texture = {};
    }

    // Runs one frame: the background clear, then the text pass if `text` has glyphs. Returns the passes the graph held.
    u32 Run(renderer::TextRenderer* text, const f32 (&clear)[4], gpu_test::Image* image) {
        ArenaAllocator arena(4u << 20);
        renderer::RenderGraph graph(&arena, renderer::RenderGraphDesc{});
        pool->BeginFrame(number++);
        Background bg;
        for (int i = 0; i < 4; ++i) bg.clear[i] = clear[i];
        bg.target = graph.ImportTexture("target", texture, rhi::ResourceState::Undefined, rhi::ResourceState::ColorAttachment, false);
        graph.AddPass("background", &BackgroundPass, &bg).Write(bg.target, renderer::GraphUsage::ColorAttachment);
        text->AddToGraph(&graph, bg.target, format, width, height);
        const u32 passes = graph.PassCount();
        device->BeginFrame();
        rhi::CommandList* list = device->CreateCommandList();
        list->Begin();
        REQUIRE(graph.Execute(pool, list));
        list->End();
        rhi::CommandList* lists[] = {list};
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

int Dist(int a, int b) { return std::abs(a - b); }

} // namespace

TEST_CASE("Hello, world! is drawn by one graph pass, inside its bounds, over the background", "[renderer][text][gpu][pass]") {
    FOR_EACH_GPU_BACKEND(backend) {
        if (backend != rhi::Backend::Vulkan) continue; // the text shaders have SPIR-V only for now (KNOWN-ISSUES.md)
        gpu_test::BackendValidation validation(backend);
        rhi::DevicePtr device = gpu_test::CreateTestDevice(backend);
        Opened o;
        text::GlyphAtlas atlas;
        renderer::TextRenderer text;
        REQUIRE(text.Create(device.Get(), &atlas, renderer::TextRendererDesc{}, nullptr));
        renderer::TransientPool pool(device.Get(), renderer::TransientPoolDesc{});
        Frame frame;
        REQUIRE(frame.MakeTarget(device.Get(), 160, 48, rhi::Format::RGBA8_UNorm));
        frame.pool = &pool;
        const f32 clear[4] = {0, 0, 1, 1}; // blue

        // The background alone: a frame with no text has no text pass.
        text.BeginFrame();
        gpu_test::Image plain;
        const u32 passes_without = frame.Run(&text, clear, &plain);
        CHECK(passes_without == 1);
        CHECK(text.Stats().passes == 0);

        // Hello, world! at 16 px in white at (10, 12).
        text::TextLayout probe;
        REQUIRE(text::Layout(StringView("Hello, world!"), PassStyle(o, 16, 0xFFFFFFFFu), text::LayoutOptions{}, &probe).Ok());
        text.BeginFrame();
        REQUIRE(text.Draw(StringView("Hello, world!"), PassStyle(o, 16, 0xFFFFFFFFu), text::LayoutOptions{}, 10, 12));
        gpu_test::Image image;
        const u32 passes_with = frame.Run(&text, clear, &image);
        CHECK(passes_with == 2);
        CHECK(text.Stats().passes == 1);
        CHECK(text.Stats().glyphs_drawn == 12); // thirteen characters, one a space

        // Outside the text's bounds (the layout box plus a margin for bearings) every pixel is still the background; inside, the text is there.
        const int x0 = 10 - 4, x1 = 10 + static_cast<int>(std::ceil(probe.width)) + 4;
        const int y0 = 12 - 4, y1 = 12 + static_cast<int>(std::ceil(probe.height)) + 4;
        u32 inked = 0;
        u32 disturbed_outside = 0;
        for (u32 y = 0; y < frame.height; ++y) {
            for (u32 x = 0; x < frame.width; ++x) {
                const u8* p = image.Pixel(x, y);
                const bool inside = static_cast<int>(x) >= x0 && static_cast<int>(x) < x1 && static_cast<int>(y) >= y0 && static_cast<int>(y) < y1;
                const bool background = p[0] == 0 && p[1] == 0 && p[2] == 255 && p[3] == 255;
                if (!inside && !background) ++disturbed_outside;
                if (inside && !background) ++inked;
            }
        }
        CHECK(disturbed_outside == 0);
        CHECK(inked > 100);

        // A second call overlays the first in call order: green over white at the same place leaves green where the glyph is fully covered.
        text.BeginFrame();
        REQUIRE(text.Draw(StringView("Hello"), PassStyle(o, 24, 0xFFFFFFFFu), text::LayoutOptions{}, 10, 10));
        REQUIRE(text.Draw(StringView("Hello"), PassStyle(o, 24, 0xFF00FF00u), text::LayoutOptions{}, 10, 10));
        gpu_test::Image overlaid;
        frame.Run(&text, clear, &overlaid);
        u32 green = 0, white_left = 0;
        for (u32 y = 0; y < frame.height; ++y) {
            for (u32 x = 0; x < frame.width; ++x) {
                const u8* p = overlaid.Pixel(x, y);
                if (p[0] == 0 && p[1] == 255 && p[2] == 0) ++green; // fully covered: the second text's color exactly
                if (p[0] > 200 && p[1] > 200 && p[2] > 200) ++white_left; // nothing of the first text's white survives under the second
            }
        }
        CHECK(green > 50);
        CHECK(white_left == 0);
        device->WaitIdle();
        frame.Destroy();
    }
}

TEST_CASE("The pipeline follows a change of the target's format", "[renderer][text][gpu][pass]") {
    FOR_EACH_GPU_BACKEND(backend) {
        if (backend != rhi::Backend::Vulkan) continue;
        gpu_test::BackendValidation validation(backend);
        rhi::DevicePtr device = gpu_test::CreateTestDevice(backend);
        Opened o;
        text::GlyphAtlas atlas;
        renderer::TextRenderer text;
        renderer::TextRendererDesc desc;
        desc.color_format = rhi::Format::RGBA8_UNorm; // made for a linear target ...
        REQUIRE(text.Create(device.Get(), &atlas, desc, nullptr));
        renderer::TransientPool pool(device.Get(), renderer::TransientPoolDesc{});
        Frame frame;
        frame.pool = &pool;
        REQUIRE(frame.MakeTarget(device.Get(), 64, 48, rhi::Format::RGBA8_SRGB)); // ... drawn into an sRGB one
        const f32 clear[4] = {0, 0, 0, 1};
        text.BeginFrame();
        REQUIRE(text.Draw(StringView("H"), PassStyle(o, 40, 0xFF808080u), text::LayoutOptions{}, 8, 2));
        gpu_test::Image image;
        CHECK(frame.Run(&text, clear, &image) == 2);
        // Fully covered pixels hold the gray that was written (decoded by the shader, encoded by the target), not a brighter one.
        u32 full = 0, wrong = 0;
        for (u32 y = 0; y < frame.height; ++y) {
            for (u32 x = 0; x < frame.width; ++x) {
                const u8* p = image.Pixel(x, y);
                if (p[0] >= 126 && p[0] <= 130) ++full;
                else if (p[0] > 130) ++wrong;
            }
        }
        CHECK(full > 20);
        CHECK(wrong == 0);
        // And back: a linear target again.
        frame.Destroy();
        REQUIRE(frame.MakeTarget(device.Get(), 64, 48, rhi::Format::RGBA8_UNorm));
        text.BeginFrame();
        REQUIRE(text.Draw(StringView("H"), PassStyle(o, 40, 0xFF808080u), text::LayoutOptions{}, 8, 2));
        CHECK(frame.Run(&text, clear, &image) == 2);
        u32 linear_full = 0;
        for (u32 y = 0; y < frame.height; ++y) {
            for (u32 x = 0; x < frame.width; ++x) {
                if (Dist(image.Pixel(x, y)[0], 128) <= 1) ++linear_full;
            }
        }
        CHECK(linear_full > 20);
        device->WaitIdle();
        frame.Destroy();
    }
}

TEST_CASE("Text above the atlas maximum is scaled from the maximum raster and is as wide as its layout says", "[renderer][text][gpu][pass]") {
    FOR_EACH_GPU_BACKEND(backend) {
        if (backend != rhi::Backend::Vulkan) continue;
        gpu_test::BackendValidation validation(backend);
        rhi::DevicePtr device = gpu_test::CreateTestDevice(backend);
        Opened o;
        text::GlyphAtlas atlas;
        renderer::TextRenderer text;
        REQUIRE(text.Create(device.Get(), &atlas, renderer::TextRendererDesc{}, nullptr));
        renderer::TransientPool pool(device.Get(), renderer::TransientPoolDesc{});
        Frame frame;
        frame.pool = &pool;
        REQUIRE(frame.MakeTarget(device.Get(), 640, 320, rhi::Format::RGBA8_UNorm));
        const f32 clear[4] = {0, 0, 0, 1};
        const u32 sizes[3] = {64, text::kMaxAtlasPixelSize, 2 * text::kMaxAtlasPixelSize};
        for (u32 size : sizes) {
            const u64 scaled_before = text.Stats().scaled_large_glyphs;
            text.BeginFrame();
            REQUIRE(text.Draw(StringView("HH"), PassStyle(o, size, 0xFFFFFFFFu), text::LayoutOptions{}, 20, 10));
            gpu_test::Image image;
            frame.Run(&text, clear, &image);
            const u64 scaled = text.Stats().scaled_large_glyphs - scaled_before;
            CHECK(scaled == (size > text::kMaxAtlasPixelSize ? 2u : 0u));
            // The ink spans the second H's right stem edge: from the first H's left edge to the layout's right edge less the right bearing.
            text::TextLayout layout;
            REQUIRE(text::Layout(StringView("HH"), PassStyle(o, size, 0xFFFFFFFFu), text::LayoutOptions{}, &layout).Ok());
            int ink_left = 100000, ink_right = -1;
            for (u32 y = 0; y < frame.height; ++y) {
                for (u32 x = 0; x < frame.width; ++x) {
                    if (image.Pixel(x, y)[0] > 127) {
                        ink_left = std::min(ink_left, static_cast<int>(x));
                        ink_right = std::max(ink_right, static_cast<int>(x));
                    }
                }
            }
            REQUIRE(ink_right > ink_left);
            const f32 layout_width = layout.width;
            const f32 ink_width = static_cast<f32>(ink_right - ink_left + 1);
            INFO("size " << size << ": ink " << ink_width << " px, layout " << layout_width << " px");
            CHECK(ink_width <= layout_width + 2.0f);
            CHECK(ink_width >= layout_width * 0.85f); // the bearings take a few percent of an H's width
        }
        // The atlas holds no glyph larger than the maximum, and no rasterization above it was asked for.
        REQUIRE(o.font.SetPixelSize(text::kMaxAtlasPixelSize).Ok());
        text::GlyphEntry entry;
        REQUIRE(atlas.GetOrAdd(o.font, o.font.FindGlyph('H'), &entry).Ok());
        CHECK(entry.rect.w <= text::kMaxAtlasPixelSize);
        CHECK(entry.rect.h <= text::kMaxAtlasPixelSize);
        CHECK(atlas.Stats().too_large == 0);
        device->WaitIdle();
        frame.Destroy();
    }
}

TEST_CASE("A steady frame of text allocates nothing per glyph", "[renderer][text][pass]") {
    if (!RenderTestCountsAllocations()) {
        SKIP("this flavor's sanitizer runtime owns operator new, so allocations cannot be counted");
    }
    // The counter must see the engine's containers, or this test proves nothing.
    const unsigned long control_before = RenderTestHeapAllocations();
    {
        Vector<u32> control;
        for (u32 i = 0; i < 1000; ++i) control.Push(i);
    }
    if (RenderTestHeapAllocations() == control_before) {
        SKIP("the counter does not see the engine's allocations");
    }
    Opened o;
    text::GlyphAtlas atlas;
    rhi::NullDevicePtr device = rhi::CreateNullDevice();
    renderer::TextRenderer text;
    REQUIRE(text.Create(device.Get(), &atlas, renderer::TextRendererDesc{}, nullptr));
    // Layout keeps a few per-call temporaries (the grapheme walk), so the claim is "nothing per glyph": the allocations of a frame are the same for a short
    // text and a text five times as long, and the cost of a frame is steady once the atlas and the batches have grown.
    const StringView shortText("Hello, world!");
    const StringView longText("Hello, world! The quick brown fox jumps over the lazy dog. Pack my box with five dozen liquor jugs.");
    auto per_frame = [&](StringView words) {
        for (int i = 0; i < 3; ++i) { // warm up: the atlas learns the glyphs, the batches and the layout reach their sizes
            text.BeginFrame();
            REQUIRE(text.Draw(words, PassStyle(o, 16, 0xFFFFFFFFu), text::LayoutOptions{}, 4, 4));
        }
        const unsigned long before = RenderTestHeapAllocations();
        for (int i = 0; i < 100; ++i) {
            text.BeginFrame();
            REQUIRE(text.Draw(words, PassStyle(o, 16, 0xFFFFFFFFu), text::LayoutOptions{}, 4, 4));
        }
        return (RenderTestHeapAllocations() - before) / 100;
    };
    const unsigned long short_cost = per_frame(shortText);
    const unsigned long long_cost = per_frame(longText);
    INFO("allocations per frame: " << short_cost << " for 13 characters, " << long_cost << " for " << longText.Length());
    CHECK(long_cost == short_cost);
    CHECK(short_cost <= 8);
}
