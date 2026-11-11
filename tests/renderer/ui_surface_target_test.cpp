// Phase 15 Day 202: a UI surface drawn into a texture that another pass samples. The surface paints only when it must,
// keeps its picture between paints, shows the colours the theme says in sRGB and UNORM, can be transparent, and is
// painted again after a resize or a device reset.

#include <catch2/catch_test_macros.hpp>

#include "backend_fixture.h"
#include "image_compare.h"
#include "renderer/device_resources.h"
#include "renderer/render_graph.h"
#include "renderer/transient_pool.h"
#include "renderer/ui_renderer.h"
#include "renderer/ui_surface_target.h"
#include "rhi/device_host.h"
#include "ui/draw_list.h"
#include "ui/draw_list_canvas.h"
#include "lang/scalar.h"

using namespace engine_placeholder;

namespace
{

constexpr u32 kSfW = 96;
constexpr u32 kSfH = 64;
constexpr u32 kSfRed = 0xFF0000FFu;
constexpr u32 kSfGreen = 0xFF00FF00u;
constexpr u32 kSfBlue = 0xFFFF0000u;
constexpr u32 kSfBrown = 0xFF336699u;  // r 0x99, g 0x66, b 0x33

struct SfClear
{
    renderer::GraphResource target;
    f32 color[4] = {0.5f, 0.5f, 0.5f, 1};
};

void SfClearPass(const renderer::PassExecuteContext& ctx, void* user)
{
    const SfClear* c = static_cast<const SfClear*>(user);
    rhi::RenderingDesc rd;
    rd.color_count = 1;
    rd.color[0].texture = ctx.graph->ImportedTexture(c->target);
    for (int i = 0; i < 4; ++i)
    {
        rd.color[0].clear_color[i] = c->color[i];
    }
    ctx.list->BeginRendering(rd);
    ctx.list->EndRendering();
}

ui::Rect SfRect(f32 x, f32 y, f32 w, f32 h)
{
    ui::Rect r;
    r.x = x;
    r.y = y;
    r.w = w;
    r.h = h;
    return r;
}

// A surface's draw list with one filled rectangle.
struct SfList
{
    ui::DrawList list;
    void Fill(u32 w, u32 h, ui::Rect rect, u32 color)
    {
        list.Clear();
        ui::DrawListCanvas canvas(&list, nullptr);
        canvas.Begin(1.0f, w, h);
        canvas.DrawRect(rect, color);
        list.SortLayers();
    }
};

// What a window frame does with the surface target: paint it or not, then draw it as an image over a grey panel.
struct SfWorld
{
    rhi::Device* device = nullptr;
    renderer::TransientPool* pool = nullptr;
    renderer::UiRenderer* window_ui = nullptr;  // the window's renderer
    rhi::TextureHandle panel;
    rhi::Format panel_format = rhi::Format::RGBA8_UNorm;
    u64 frame = 0;

    struct Item
    {
        renderer::UiSurfaceTarget* target = nullptr;
        renderer::UiRenderer* surface_ui = nullptr;
        const ui::DrawList* surface_list = nullptr;
        bool paint = true;
        ui::ImageId image = 5;
        ui::Rect where;
    };

    void Run(Item* items, usize count, gpu_test::Image* out, f32 grey = 0.5f)
    {
        ArenaAllocator arena(4u << 20);
        renderer::RenderGraph graph(&arena, renderer::RenderGraphDesc{});
        pool->BeginFrame(++frame);
        SfClear bg;
        for (int i = 0; i < 3; ++i)
        {
            bg.color[i] = grey;
        }
        bg.target = graph.ImportTexture("panel", panel, rhi::ResourceState::Undefined,
                                        rhi::ResourceState::ColorAttachment, false);
        graph.AddPass("panel", &SfClearPass, &bg).Write(bg.target, renderer::GraphUsage::ColorAttachment);
        renderer::GraphResource sampled[4];
        ui::DrawList window_list;
        ui::DrawListCanvas canvas(&window_list, nullptr);
        canvas.Begin(1.0f, kSfW, kSfH);
        for (usize i = 0; i < count; ++i)
        {
            items[i].target->BeginFrame();
            sampled[i] = items[i].paint
                             ? items[i].target->AddToGraph(&graph, items[i].surface_ui, items[i].surface_list)
                             : items[i].target->Sampled(&graph);
            window_ui->SetImage(items[i].image, items[i].target);
            canvas.DrawImage(items[i].where, items[i].image, {0, 0, 1, 1}, 0xFFFFFFFFu);
        }
        window_list.SortLayers();
        window_ui->AddToGraph(&graph, &window_list, bg.target, panel_format, kSfW, kSfH,
                              Span<const renderer::GraphResource>(sampled, count));
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
        out->width = kSfW;
        out->height = kSfH;
        out->rgba.assign(static_cast<usize>(kSfW) * kSfH * 4, 0);
        REQUIRE(device->ReadbackTexture(panel, 0, 0, out->rgba.data(), out->rgba.size(),
                                        rhi::ResourceState::ColorAttachment));
    }
};

bool SfIs(const gpu_test::Image& image, u32 x, u32 y, int r, int g, int b, int tolerance = 2)
{
    const u8* p = image.Pixel(x, y);
    return Abs(p[0] - r) <= tolerance && Abs(p[1] - g) <= tolerance && Abs(p[2] - b) <= tolerance;
}

rhi::TextureHandle SfPanel(rhi::Device* device, rhi::Format format)
{
    rhi::TextureDesc td;
    td.width = kSfW;
    td.height = kSfH;
    td.format = format;
    td.usage = rhi::TextureUsage::ColorAttachment | rhi::TextureUsage::CopySrc;
    return device->CreateTexture(td);
}

}  // namespace

TEST_CASE(
    "surface target: a surface painted into its texture shows inside the window frame, and keeps its picture without a repaint",
    "[renderer][ui][surface][gpu]")
{
    FOR_EACH_GPU_BACKEND(backend)
    {
        gpu_test::BackendValidation validation(backend);
        rhi::DevicePtr device = gpu_test::CreateTestDevice(backend);
        renderer::UiRendererDesc ud;
        ud.color_format = rhi::Format::RGBA8_UNorm;
        renderer::UiRenderer window_ui;
        renderer::UiRenderer surface_ui;
        REQUIRE(window_ui.Create(device.Get(), nullptr, ud, nullptr));
        REQUIRE(surface_ui.Create(device.Get(), nullptr, ud, nullptr));
        renderer::TransientPool pool(device.Get(), renderer::TransientPoolDesc{});
        renderer::UiSurfaceTarget target;
        renderer::UiSurfaceTargetDesc td;
        td.format = rhi::Format::RGBA8_UNorm;
        td.background = kSfGreen;
        REQUIRE(target.Create(device.Get(), td, 32, 32, nullptr, "screen"));
        CHECK(target.NeedsPaint());
        SfList surface;
        surface.Fill(32, 32, SfRect(8, 8, 16, 16), kSfRed);
        SfWorld world;
        world.device = device.Get();
        world.pool = &pool;
        world.window_ui = &window_ui;
        world.panel = SfPanel(device.Get(), rhi::Format::RGBA8_UNorm);
        SfWorld::Item item;
        item.target = &target;
        item.surface_ui = &surface_ui;
        item.surface_list = &surface.list;
        item.where = SfRect(20, 10, 32, 32);
        gpu_test::Image image;
        world.Run(&item, 1, &image);
        CHECK_FALSE(target.NeedsPaint());
        CHECK(SfIs(image, 36, 26, 255, 0, 0));      // the red rectangle of the surface
        CHECK(SfIs(image, 22, 12, 0, 255, 0));      // the surface's own background
        CHECK(SfIs(image, 10, 10, 128, 128, 128));  // the grey panel around it
        CHECK(target.Paints() == 1);
        CHECK(surface_ui.Stats().passes == 1);
        // Idle frames: no paint, no surface pass, and the same picture.
        item.paint = false;
        for (int i = 0; i < 20; ++i)
        {
            world.Run(&item, 1, &image);
        }
        CHECK(target.Paints() == 1);
        CHECK(surface_ui.Stats().passes == 1);
        CHECK(window_ui.Stats().passes == 21);
        CHECK(SfIs(image, 36, 26, 255, 0, 0));
        // The surface changes, so the app paints once.
        surface.Fill(32, 32, SfRect(8, 8, 16, 16), kSfBlue);
        item.paint = true;
        world.Run(&item, 1, &image);
        CHECK(SfIs(image, 36, 26, 0, 0, 255));
        CHECK(target.Paints() == 2);
        device->WaitIdle();
        window_ui.ClearImage(5);
        device->DestroyTexture(world.panel);
    }
}

TEST_CASE("surface target: an sRGB surface shows the theme's bytes and a transparent one lets the panel through",
          "[renderer][ui][surface][gpu]")
{
    FOR_EACH_GPU_BACKEND(backend)
    {
        gpu_test::BackendValidation validation(backend);
        rhi::DevicePtr device = gpu_test::CreateTestDevice(backend);
        renderer::UiRendererDesc srgb;
        srgb.color_format = rhi::Format::RGBA8_SRGB;
        renderer::UiRenderer window_ui;
        renderer::UiRenderer surface_ui;
        renderer::UiRenderer clear_ui;
        REQUIRE(window_ui.Create(device.Get(), nullptr, srgb, nullptr));
        REQUIRE(surface_ui.Create(device.Get(), nullptr, srgb, nullptr));
        renderer::UiRendererDesc unorm;
        REQUIRE(clear_ui.Create(device.Get(), nullptr, unorm, nullptr));
        renderer::TransientPool pool(device.Get(), renderer::TransientPoolDesc{});
        renderer::UiSurfaceTarget opaque;
        renderer::UiSurfaceTargetDesc od;
        od.format = rhi::Format::RGBA8_SRGB;
        od.background = kSfBrown;
        REQUIRE(opaque.Create(device.Get(), od, 32, 32, nullptr, "opaque"));
        renderer::UiSurfaceTarget overlay;
        renderer::UiSurfaceTargetDesc ld;
        ld.format = rhi::Format::RGBA8_UNorm;
        ld.background = 0x00FFFFFFu;  // transparent white: the image shader reads the texture as straight alpha, so it
                                      // must add nothing to what is under it
        REQUIRE(overlay.Create(device.Get(), ld, 24, 24, nullptr, "overlay"));
        SfList brown_surface;
        brown_surface.Fill(32, 32, SfRect(0, 0, 0, 0), kSfRed);  // an empty rectangle: only the background shows
        SfList overlay_surface;
        overlay_surface.Fill(24, 24, SfRect(4, 4, 8, 8), kSfRed);
        SfWorld world;
        world.device = device.Get();
        world.pool = &pool;
        world.window_ui = &window_ui;
        world.panel_format = rhi::Format::RGBA8_SRGB;
        world.panel = SfPanel(device.Get(), rhi::Format::RGBA8_SRGB);
        SfWorld::Item items[2];
        items[0].target = &opaque;
        items[0].surface_ui = &surface_ui;
        items[0].surface_list = &brown_surface.list;
        items[0].image = 5;
        items[0].where = SfRect(4, 4, 32, 32);
        items[1].target = &overlay;
        items[1].surface_ui = &clear_ui;
        items[1].surface_list = &overlay_surface.list;
        items[1].image = 6;
        items[1].where = SfRect(48, 4, 24, 24);
        gpu_test::Image image;
        world.Run(items, 2, &image, 0.5f);
        CHECK(SfIs(image, 20, 20, 0x99, 0x66, 0x33, 3));  // the sRGB surface shows the bytes of the theme colour
        CHECK(SfIs(image, 56, 12, 255, 0, 0, 3));         // the overlay's red rectangle
        // The transparent part of the overlay lets the panel (grey 0.5 linear written to an sRGB panel) through
        // unchanged.
        gpu_test::Image only_panel;
        items[1].where = SfRect(48, 4, 0, 0);
        world.Run(items, 2, &only_panel, 0.5f);
        CHECK(SfIs(image, 70, 26, only_panel.Pixel(70, 26)[0], only_panel.Pixel(70, 26)[1], only_panel.Pixel(70, 26)[2],
                   1));
        device->WaitIdle();
        window_ui.ClearImage(5);
        window_ui.ClearImage(6);
        device->DestroyTexture(world.panel);
    }
}

TEST_CASE("surface target: a resize and a device reset leave it unpainted until the app paints again",
          "[renderer][ui][surface][gpu][recreatable]")
{
    FOR_EACH_GPU_BACKEND(backend)
    {
        gpu_test::BackendValidation validation(backend);
        renderer::DeviceResources registry;  // destroyed last
        rhi::DeviceHost host;
        host.AddObserver(&registry, 0);
        rhi::DeviceCreateInfo info;
        info.backend = backend;
        info.validation = rhi::ValidationSetting::Off;
        info.pipeline_cache = false;
        if (!host.Create(info, rhi::SwapchainDesc{}))
        {
            SKIP("no device");
        }
        renderer::UiRendererDesc ud;
        renderer::UiRenderer window_ui;
        renderer::UiRenderer surface_ui;
        REQUIRE(window_ui.Create(host.GetDevice(), nullptr, ud, &registry));
        REQUIRE(surface_ui.Create(host.GetDevice(), nullptr, ud, &registry));
        renderer::UiSurfaceTarget target;
        renderer::UiSurfaceTargetDesc td;
        td.format = rhi::Format::RGBA8_UNorm;
        td.background = kSfGreen;
        REQUIRE(target.Create(host.GetDevice(), td, 32, 32, &registry, "screen"));
        SfList surface;
        surface.Fill(32, 32, SfRect(8, 8, 16, 16), kSfRed);
        rhi::TextureHandle panel = SfPanel(host.GetDevice(), rhi::Format::RGBA8_UNorm);
        struct PanelOwner final : renderer::Recreatable
        {
            rhi::Device* device = nullptr;
            rhi::TextureHandle handle;
            void Release() override
            {
                if (device != nullptr && handle.IsValid())
                {
                    device->DestroyTexture(handle);
                }
                handle = {};
                device = nullptr;
            }
            bool Recreate(rhi::Device* d) override
            {
                device = d;
                handle = SfPanel(d, rhi::Format::RGBA8_UNorm);
                return handle.IsValid();
            }
            bool Live() const override { return handle.IsValid(); }
            const char* RecreatableName() const override { return "sf panel"; }
        } panel_owner;
        panel_owner.device = host.GetDevice();
        panel_owner.handle = panel;
        renderer::ScopedRegistration panel_registration;
        panel_registration.Attach(&registry, &panel_owner, renderer::tiers::kTextures);
        auto frame = [&](bool paint, gpu_test::Image* out)
        {
            rhi::Device* device = host.GetDevice();
            renderer::TransientPool pool(device, renderer::TransientPoolDesc{});
            SfWorld world;
            world.device = device;
            world.pool = &pool;
            world.window_ui = &window_ui;
            world.panel = panel_owner.handle;
            SfWorld::Item item;
            item.target = &target;
            item.surface_ui = &surface_ui;
            item.surface_list = &surface.list;
            item.paint = paint;
            item.where = SfRect(20, 10, static_cast<f32>(target.Width()), static_cast<f32>(target.Height()));
            world.Run(&item, 1, out);
            pool.Release();
        };
        gpu_test::Image image;
        frame(true, &image);
        CHECK(SfIs(image, 36, 26, 255, 0, 0));
        // A resize: the picture is gone and the target says so.
        REQUIRE(target.Resize(48, 48));
        CHECK(target.NeedsPaint());
        surface.Fill(48, 48, SfRect(8, 8, 32, 32), kSfBlue);
        frame(true, &image);
        CHECK_FALSE(target.NeedsPaint());
        CHECK(SfIs(image, 40, 30, 0, 0, 255));
        // A device reset: released, made again, empty, and the target asks for a paint.
        REQUIRE(host.Recreate(info, rhi::SwapchainDesc{}));
        CHECK(host.LeakedObjects() == 0);
        REQUIRE(target.Handle().IsValid());
        CHECK(target.NeedsPaint());
        frame(true, &image);
        CHECK_FALSE(target.NeedsPaint());
        CHECK(SfIs(image, 40, 30, 0, 0, 255));
        CHECK(SfIs(image, 22, 12, 0, 255, 0));
        host.GetDevice()->WaitIdle();
        window_ui.Destroy();
        surface_ui.Destroy();
        target.Destroy();
        panel_registration.Reset();
        panel_owner.Release();
    }
}
