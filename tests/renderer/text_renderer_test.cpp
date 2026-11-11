// Phase 14 Day 180: the GPU copy of the glyph atlas and the text pipeline.
//
// Without a GPU (the null device): the atlas textures follow the atlas (new pages, changed pages, nothing when nothing
// changed) and are made again from it after a release. On a real device: the quad of a glyph shows the atlas bitmap
// texel for texel (an asymmetric "F", so a flipped or shifted quad fails), premultiplied blending over a colored
// background, the sRGB round trip, and a device reset that rebuilds the atlas pages, the pipeline and the sampler so
// the same frame renders the same pixels.
#include "algorithm.h"
#include "lang/scalar.h"
#include "cstr/cstr.h"

#include <catch2/catch_test_macros.hpp>

#include "backend_fixture.h"
#include "font_support.h"
#include "image_compare.h"
#include "render_target.h"
#include "renderer/device_resources.h"
#include "renderer/glyph_atlas_texture.h"
#include "renderer/text_renderer.h"
#include "rhi/device_host.h"
#include "rhi/null/null_device.h"
#include "text/glyph_atlas.h"
#include "text/layout.h"

using namespace engine_placeholder;
using text_test::Opened;

namespace
{

text::TextLayout LayText(Opened& o, const char* text, u32 size)
{
    text::TextStyle style;
    style.font = &o.font;
    style.pixel_size = size;
    text::TextLayout layout;
    REQUIRE(text::Layout(StringView(text, CStrLength(text)), style, text::LayoutOptions{}, &layout).Ok());
    return layout;
}

text::TextStyle StyleOf(Opened& o, u32 size, u32 color)
{
    text::TextStyle style;
    style.font = &o.font;
    style.pixel_size = size;
    style.color = color;
    return style;
}

// An offscreen target of any color format, with readback.
struct Target
{
    rhi::Device* device = nullptr;
    rhi::TextureHandle texture;
    u32 width = 0;
    u32 height = 0;
    bool Make(rhi::Device* d, u32 w, u32 h, rhi::Format format)
    {
        rhi::TextureDesc td;
        td.width = w;
        td.height = h;
        td.format = format;
        td.usage = rhi::TextureUsage::ColorAttachment | rhi::TextureUsage::CopySrc;
        td.debug_name = "text target";
        texture = d->CreateTexture(td);
        device = d;
        width = w;
        height = h;
        return texture.IsValid();
    }
    void Destroy()
    {
        if (device != nullptr && texture.IsValid())
        {
            device->DestroyTexture(texture);
        }
        texture = {};
    }
    // Rendering leaves the target in ColorAttachment.
    gpu_test::Image Read() const
    {
        gpu_test::Image image;
        image.width = width;
        image.height = height;
        image.rgba.assign(static_cast<usize>(width) * height * 4, 0);
        REQUIRE(device->ReadbackTexture(texture, 0, 0, image.rgba.data(), image.rgba.size(),
                                        rhi::ResourceState::ColorAttachment));
        return image;
    }
};

// One frame: clear the target, run `add` (which calls TextRenderer::Add), draw, wait, and read the target back.
template <typename AddFn>
gpu_test::Image RenderFrame(rhi::Device* device, renderer::TextRenderer* text, const Target& target,
                            const f32 (&clear)[4], AddFn add)
{
    device->BeginFrame();
    text->BeginFrame();
    add();
    rhi::CommandList* list = device->CreateCommandList();
    list->Begin();
    list->Barrier(target.texture, rhi::ResourceState::Undefined, rhi::ResourceState::ColorAttachment);
    rhi::RenderingDesc pass;
    pass.color_count = 1;
    pass.color[0].texture = target.texture;
    for (int i = 0; i < 4; ++i)
    {
        pass.color[0].clear_color[i] = clear[i];
    }
    list->BeginRendering(pass);
    REQUIRE(text->Record(list, target.width, target.height));
    list->EndRendering();
    list->End();
    rhi::CommandList* lists[] = {list};
    const bool submitted = device->Submit(lists, 1) != 0;
    device->EndFrame();
    REQUIRE(submitted);
    device->WaitIdle();
    return target.Read();
}

int Diff(int a, int b)
{
    return Abs(a - b);
}

// Where the quad of `glyph` lands for a layout drawn at (x, y): the same arithmetic the renderer uses, spelled out for
// the test.
struct Quad
{
    int left = 0, top = 0;
    text::GlyphEntry entry;
};
Quad QuadOf(text::GlyphAtlas& atlas, Opened& o, u32 size, const text::PositionedGlyph& g, f32 x, f32 y)
{
    REQUIRE(o.font.SetPixelSize(size).Ok());
    Quad q;
    REQUIRE(atlas.GetOrAdd(o.font, g.glyph, &q.entry).Ok());
    q.left = static_cast<int>(Floor(x + g.x + static_cast<f32>(q.entry.left) + 0.5f));
    q.top = static_cast<int>(Floor(y + g.y - static_cast<f32>(q.entry.top) + 0.5f));
    return q;
}

u8 Coverage(const text::GlyphAtlas& atlas, const Quad& q, u32 dx, u32 dy)
{
    return atlas.PagePixels(q.entry.page)[(q.entry.rect.y + dy) * atlas.PageSize() + q.entry.rect.x + dx];
}

}  // namespace

TEST_CASE("The atlas textures follow the atlas on the null device", "[renderer][text]")
{
    Opened o;
    text::GlyphAtlas atlas(64, 8);  // small pages, so a few glyphs fill one
    rhi::NullDevicePtr device = rhi::CreateNullDevice();
    renderer::GlyphAtlasTexture pages;
    REQUIRE(pages.Create(device.Get(), &atlas, nullptr));
    CHECK(pages.PageCount() == 0);  // an empty atlas has no page yet

    REQUIRE(o.font.SetPixelSize(20).Ok());
    text::GlyphEntry entry;
    REQUIRE(atlas.GetOrAdd(o.font, o.font.FindGlyph('A'), &entry).Ok());
    REQUIRE(pages.Sync());
    CHECK(pages.PageCount() == 1);
    CHECK(pages.Page(0).IsValid());
    CHECK(pages.UploadCount() == 1);

    REQUIRE(pages.Sync());  // nothing changed: nothing is sent
    CHECK(pages.UploadCount() == 1);

    REQUIRE(atlas.GetOrAdd(o.font, o.font.FindGlyph('B'), &entry).Ok());
    REQUIRE(pages.Sync());  // the page changed: it is sent whole
    CHECK(pages.UploadCount() == 2);

    for (char c = 'C'; c <= 'Z'; ++c)
    {
        REQUIRE(atlas.GetOrAdd(o.font, o.font.FindGlyph(static_cast<text::char32>(c)), &entry).Ok());
    }
    REQUIRE(atlas.PageCount() > 1);
    REQUIRE(pages.Sync());
    CHECK(pages.PageCount() == atlas.PageCount());
    const u64 before = pages.UploadCount();

    // A release and a recreate send every page again, whether or not it changed.
    const rhi::ResourceCounts made = device->Resources();
    pages.Release();
    CHECK(device->Resources().textures == made.textures - atlas.PageCount());
    REQUIRE(pages.Recreate(device.Get()));
    CHECK(pages.PageCount() == atlas.PageCount());
    CHECK(pages.UploadCount() == before + atlas.PageCount());
    CHECK(device->Resources().textures == made.textures);
    pages.Destroy();
    CHECK(device->Resources().textures == made.textures - atlas.PageCount());
}

TEST_CASE("The atlas textures are rebuilt by the registry when the device is replaced", "[renderer][text]")
{
    Opened o;
    text::GlyphAtlas atlas(128, 4);
    renderer::DeviceResources registry;
    rhi::DeviceHost host;
    host.AddObserver(&registry, 0);
    rhi::DeviceCreateInfo info;
    info.backend = rhi::Backend::Null;
    REQUIRE(host.Create(info, rhi::SwapchainDesc{}));
    renderer::GlyphAtlasTexture pages;
    REQUIRE(o.font.SetPixelSize(18).Ok());
    text::GlyphEntry entry;
    REQUIRE(atlas.GetOrAdd(o.font, o.font.FindGlyph('g'), &entry).Ok());
    REQUIRE(pages.Create(host.GetDevice(), &atlas, &registry));
    CHECK(registry.Count() == 1);
    const rhi::TextureHandle first = pages.Page(0);
    REQUIRE(host.Recreate(info, rhi::SwapchainDesc{}));
    CHECK(pages.Live());
    CHECK(pages.PageCount() == 1);
    CHECK(pages.Page(0).IsValid());
    (void)first;  // the handle may or may not be equal on a new device; what matters is that the page is live and was
                  // sent again
    CHECK(pages.UploadCount() == 2);
    pages.Destroy();
    CHECK(registry.Count() == 0);
    host.Destroy();
}

TEST_CASE("The text renderer queues glyphs, skips spaces and counts what the budget drops", "[renderer][text]")
{
    Opened o;
    text::GlyphAtlas atlas;
    rhi::NullDevicePtr device = rhi::CreateNullDevice();
    renderer::TextRenderer text;
    renderer::TextRendererDesc desc;
    desc.max_glyphs = 5;
    REQUIRE(text.Create(device.Get(), &atlas, desc, nullptr));
    text::TextLayout layout = LayText(o, "ab cd", 16);
    text.BeginFrame();
    CHECK(text.Add(layout, StyleOf(o, 16, 0xFFFFFFFFu), 0, 0));
    CHECK(text.QueuedGlyphs() == 4);  // the space has no pixels
    CHECK(text.DroppedGlyphs() == 0);
    text::TextLayout more = LayText(o, "xyz", 16);
    CHECK_FALSE(text.Add(more, StyleOf(o, 16, 0xFFFFFFFFu), 0, 20));  // one fits, two do not
    CHECK(text.QueuedGlyphs() == 5);
    CHECK(text.DroppedGlyphs() == 2);
    text.BeginFrame();
    CHECK(text.QueuedGlyphs() == 0);
    CHECK(text.DroppedGlyphs() == 2);  // the total is for the object's life

    // Record runs inside a rendering pass on the null device (which checks the calls' order); it must not fail.
    {
        Target target;
        REQUIRE(target.Make(device.Get(), 64, 32, rhi::Format::RGBA8_UNorm));
        CHECK(text.Add(layout, StyleOf(o, 16, 0xFFFFFFFFu), 0, 0));
        device->BeginFrame();
        rhi::CommandList* list = device->CreateCommandList();
        list->Begin();
        list->Barrier(target.texture, rhi::ResourceState::Undefined, rhi::ResourceState::ColorAttachment);
        rhi::RenderingDesc pass;
        pass.color_count = 1;
        pass.color[0].texture = target.texture;
        list->BeginRendering(pass);
        CHECK(text.Record(list, 64, 32));
        list->EndRendering();
        list->End();
        rhi::CommandList* lists[] = {list};
        device->Submit(lists, 1);
        device->EndFrame();
        device->WaitIdle();
        target.Destroy();
    }
    text.Destroy();
    CHECK(device->Resources().pipelines == 0);
    CHECK(device->Resources().buffers == 0);
}

TEST_CASE("A glyph's quad shows the atlas bitmap texel for texel", "[renderer][text][gpu]")
{
    FOR_EACH_GPU_BACKEND(backend)
    {
        gpu_test::BackendValidation validation(backend);
        rhi::DevicePtr device = gpu_test::CreateTestDevice(backend);
        Opened o;
        text::GlyphAtlas atlas;
        renderer::TextRenderer text;
        renderer::TextRendererDesc desc;
        desc.color_format = rhi::Format::RGBA8_UNorm;
        REQUIRE(text.Create(device.Get(), &atlas, desc, nullptr));

        Target target;
        REQUIRE(target.Make(device.Get(), 80, 64, rhi::Format::RGBA8_UNorm));

        const u32 size = 40;
        text::TextLayout layout = LayText(o, "F", size);
        const f32 x = 11.0f;
        const f32 y = 5.0f;
        const f32 clear[4] = {0, 0, 0, 1};
        const gpu_test::Image image = RenderFrame(device.Get(), &text, target, clear, [&]
                                                  { REQUIRE(text.Add(layout, StyleOf(o, size, 0xFFFFFFFFu), x, y)); });

        const Quad q = QuadOf(atlas, o, size, layout.glyphs[0], x, y);
        REQUIRE(q.entry.has_pixels);
        REQUIRE(q.left >= 0);
        REQUIRE(q.top >= 0);
        REQUIRE(q.left + static_cast<int>(q.entry.rect.w) <= 80);
        REQUIRE(q.top + static_cast<int>(q.entry.rect.h) <= 64);
        int worst = 0;
        u32 inked = 0;
        for (u32 py = 0; py < 64; ++py)
        {
            for (u32 px = 0; px < 80; ++px)
            {
                const u8* p = image.Pixel(px, py);
                const int dx = static_cast<int>(px) - q.left;
                const int dy = static_cast<int>(py) - q.top;
                const bool inside = dx >= 0 && dy >= 0 && dx < static_cast<int>(q.entry.rect.w) &&
                                    dy < static_cast<int>(q.entry.rect.h);
                const int expected = inside ? Coverage(atlas, q, static_cast<u32>(dx), static_cast<u32>(dy)) : 0;
                if (expected != 0)
                {
                    ++inked;
                }
                worst = Max(Max(worst, Diff(p[0], expected)), Max(Diff(p[1], expected), Diff(p[2], expected)));
                CHECK(p[3] == 255);
            }
        }
        INFO("backend " << gpu_test::BackendTag(backend));
        CHECK(worst <= 2);
        CHECK(inked > 100);

        // The letter F is asymmetric: its stem is on the left, the top bar is the widest part and the lower right is
        // empty. A quad flipped in either direction fails one of these (the bitmap comparison above already pins every
        // pixel).
        const u32 w = q.entry.rect.w, h = q.entry.rect.h;
        auto ink = [&](u32 row, u32 x0, u32 x1)
        {
            u32 sum = 0;
            for (u32 dx = x0; dx < x1; ++dx)
            {
                sum += Coverage(atlas, q, dx, row);
            }
            return sum;
        };
        CHECK(ink(h - 3, 0, w / 3) > 0);   // the stem, near the bottom
        CHECK(ink(h - 3, w / 2, w) == 0);  // nothing to its right
        CHECK(ink(1, w / 2, w) > 0);       // the top bar reaches right
        auto on_screen = [&](u32 row, u32 x0, u32 x1)
        {
            u32 sum = 0;
            for (u32 dx = x0; dx < x1; ++dx)
            {
                sum += image.Pixel(static_cast<u32>(q.left) + dx, static_cast<u32>(q.top) + row)[0];
            }
            return sum;
        };
        CHECK(on_screen(h - 3, 0, w / 3) > 0);
        CHECK(on_screen(h - 3, w / 2, w) == 0);
        CHECK(on_screen(1, w / 2, w) > 0);

        device->WaitIdle();
        target.Destroy();
    }
}

TEST_CASE("Text blends with premultiplied alpha over a colored background", "[renderer][text][gpu]")
{
    FOR_EACH_GPU_BACKEND(backend)
    {
        gpu_test::BackendValidation validation(backend);
        rhi::DevicePtr device = gpu_test::CreateTestDevice(backend);
        Opened o;
        text::GlyphAtlas atlas;
        renderer::TextRenderer text;
        REQUIRE(text.Create(device.Get(), &atlas, renderer::TextRendererDesc{}, nullptr));
        Target target;
        REQUIRE(target.Make(device.Get(), 64, 64, rhi::Format::RGBA8_UNorm));
        const u32 size = 48;
        text::TextLayout layout = LayText(o, "H", size);
        const f32 clear[4] = {0, 0, 1, 1};  // blue
        // Red, fully opaque: red in the lowest byte.
        const gpu_test::Image opaque = RenderFrame(device.Get(), &text, target, clear,
                                                   [&] { text.Add(layout, StyleOf(o, size, 0xFF0000FFu), 8, 4); });
        const Quad q = QuadOf(atlas, o, size, layout.glyphs[0], 8, 4);
        int worst = 0;
        for (u32 dy = 0; dy < q.entry.rect.h; ++dy)
        {
            for (u32 dx = 0; dx < q.entry.rect.w; ++dx)
            {
                const u8* p = opaque.Pixel(static_cast<u32>(q.left) + dx, static_cast<u32>(q.top) + dy);
                const int cov = Coverage(atlas, q, dx, dy);
                worst = Max(Max(worst, Diff(p[0], cov)), Max(Diff(p[1], 0), Diff(p[2], 255 - cov)));
            }
        }
        CHECK(worst <= 2);

        // Half transparent red: coverage times 0.5.
        const gpu_test::Image half = RenderFrame(device.Get(), &text, target, clear,
                                                 [&] { text.Add(layout, StyleOf(o, size, 0x800000FFu), 8, 4); });
        worst = 0;
        for (u32 dy = 0; dy < q.entry.rect.h; ++dy)
        {
            for (u32 dx = 0; dx < q.entry.rect.w; ++dx)
            {
                const u8* p = half.Pixel(static_cast<u32>(q.left) + dx, static_cast<u32>(q.top) + dy);
                const f32 a = static_cast<f32>(Coverage(atlas, q, dx, dy)) / 255.0f * (128.0f / 255.0f);
                worst = Max(Max(worst, Diff(p[0], static_cast<int>(LRound(255.0f * a)))),
                            Diff(p[2], static_cast<int>(LRound(255.0f * (1.0f - a)))));
            }
        }
        CHECK(worst <= 3);
        device->WaitIdle();
        target.Destroy();
    }
}

TEST_CASE("An sRGB target shows the color the application wrote", "[renderer][text][gpu]")
{
    FOR_EACH_GPU_BACKEND(backend)
    {
        gpu_test::BackendValidation validation(backend);
        rhi::DevicePtr device = gpu_test::CreateTestDevice(backend);
        Opened o;
        text::GlyphAtlas atlas;
        renderer::TextRenderer text;
        renderer::TextRendererDesc desc;
        desc.color_format = rhi::Format::RGBA8_SRGB;
        REQUIRE(text.Create(device.Get(), &atlas, desc, nullptr));
        Target target;
        REQUIRE(target.Make(device.Get(), 64, 64, rhi::Format::RGBA8_SRGB));
        const u32 size = 48;
        text::TextLayout layout = LayText(o, "H", size);
        const f32 clear[4] = {0, 0, 0, 1};
        // A mid gray, written as sRGB bytes. In the middle of a full-coverage pixel the stored byte must be the same
        // gray again (decode in the shader, encode on write), where a missing decode would store a much brighter value.
        const gpu_test::Image image = RenderFrame(device.Get(), &text, target, clear,
                                                  [&] { text.Add(layout, StyleOf(o, size, 0xFF808080u), 8, 4); });
        const Quad q = QuadOf(atlas, o, size, layout.glyphs[0], 8, 4);
        u32 full = 0;
        for (u32 dy = 0; dy < q.entry.rect.h; ++dy)
        {
            for (u32 dx = 0; dx < q.entry.rect.w; ++dx)
            {
                if (Coverage(atlas, q, dx, dy) != 255)
                {
                    continue;
                }
                const u8* p = image.Pixel(static_cast<u32>(q.left) + dx, static_cast<u32>(q.top) + dy);
                ++full;
                CHECK(Diff(p[0], 128) <= 2);
                CHECK(Diff(p[1], 128) <= 2);
                CHECK(Diff(p[2], 128) <= 2);
            }
        }
        CHECK(full > 50);
        device->WaitIdle();
        target.Destroy();
    }
}

TEST_CASE("A device reset rebuilds the atlas pages, the pipeline and the sampler", "[renderer][text][gpu][reset]")
{
    FOR_EACH_GPU_BACKEND(backend)
    {
        gpu_test::BackendValidation validation(backend);
        Opened o;
        text::GlyphAtlas atlas;
        renderer::DeviceResources registry;  // destroyed last
        rhi::DeviceHost host;
        rhi::DeviceCreateInfo info;
        info.backend = backend;
        info.validation = rhi::ValidationSetting::On;
        info.sync_validation = backend == rhi::Backend::Vulkan;
        info.pipeline_cache = false;
        host.AddObserver(&registry, 0);
        if (!host.Create(info, rhi::SwapchainDesc{}))
        {
            SKIP("no " << gpu_test::BackendTag(backend) << " device with validation on this machine");
        }
        renderer::TextRenderer text;
        REQUIRE(text.Create(host.GetDevice(), &atlas, renderer::TextRendererDesc{}, &registry));
        Target target;  // not registered: made again by hand after the reset
        REQUIRE(target.Make(host.GetDevice(), 96, 48, rhi::Format::RGBA8_UNorm));

        const u32 size = 24;
        text::TextLayout layout = LayText(o, "Reset FAQ", size);
        const f32 clear[4] = {0.1f, 0.1f, 0.1f, 1};
        auto draw = [&] { REQUIRE(text.Add(layout, StyleOf(o, size, 0xFFE0E0E0u), 4, 4)); };
        const gpu_test::Image before = RenderFrame(host.GetDevice(), &text, target, clear, draw);
        const rhi::ResourceCounts counts = host.GetDevice()->Resources();

        // Replace the device: the target belongs to the old one and go with it; the text renderer is rebuilt by the
        // registry.
        host.GetDevice()->WaitIdle();
        target.Destroy();
        REQUIRE(host.Recreate(info, rhi::SwapchainDesc{}));
        REQUIRE(text.Live());
        REQUIRE(target.Make(host.GetDevice(), 96, 48, rhi::Format::RGBA8_UNorm));
        const gpu_test::Image after = RenderFrame(host.GetDevice(), &text, target, clear, draw);
        const rhi::ResourceCounts now = host.GetDevice()->Resources();

        CHECK(before.rgba == after.rgba);  // the same frame, pixel for pixel

        // A lost device recovers through the same path: inject the loss, replace the device, draw again.
        host.GetDevice()->WaitIdle();
        host.GetDevice()->InjectDeviceLoss();
        target.Destroy();
        REQUIRE(host.Recreate(info, rhi::SwapchainDesc{}));
        REQUIRE(target.Make(host.GetDevice(), 96, 48, rhi::Format::RGBA8_UNorm));
        const gpu_test::Image recovered = RenderFrame(host.GetDevice(), &text, target, clear, draw);
        CHECK(before.rgba == recovered.rgba);
        CHECK(now.buffers == counts.buffers);
        CHECK(now.textures == counts.textures);
        CHECK(now.samplers == counts.samplers);
        CHECK(now.shaders == counts.shaders);
        CHECK(now.bind_group_layouts == counts.bind_group_layouts);
        CHECK(now.pipelines == counts.pipelines);

        host.GetDevice()->WaitIdle();
        target.Destroy();
        text.Destroy();
        host.Destroy();
    }
}
