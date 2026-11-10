// Phase 15 Day 201: the offscreen target of a UI viewport. The world is drawn into it (a clear pass in the UI frame's own graph, or the Phase 5 lit cube in a frame of its own), the UI pass
// shows it as an image inside a panel, a resize keeps the picture and frees the old textures, and a device reset makes it again.

#include <catch2/catch_test_macros.hpp>

#include "backend_fixture.h"
#include "image_compare.h"
#include "renderer/device_resources.h"
#include "renderer/render_graph.h"
#include "renderer/transient_pool.h"
#include "renderer/ui_renderer.h"
#include "renderer/viewport_target.h"
#include "reset_rig.h"
#include "rhi/null/null_device.h"
#include "ui/draw_list.h"
#include "ui/draw_list_canvas.h"
#include "lang/scalar.h"

using namespace engine_placeholder;

namespace {

struct VtWorld {
    renderer::GraphResource target;
    f32 color[4] = {0, 0, 0, 1};
};

void VtClearPass(const renderer::PassExecuteContext& ctx, void* user) {
    const VtWorld* w = static_cast<const VtWorld*>(user);
    rhi::RenderingDesc rd;
    rd.color_count = 1;
    rd.color[0].texture = ctx.graph->ImportedTexture(w->target);
    for (int i = 0; i < 4; ++i) rd.color[0].clear_color[i] = w->color[i];
    ctx.list->BeginRendering(rd);
    ctx.list->EndRendering();
}

constexpr u32 kVtPanelW = 128;
constexpr u32 kVtPanelH = 96;
const f32 kVtPanel[4] = {0.0f, 0.0f, 1.0f, 1.0f}; // a blue panel

// One viewport as a frame sees it: the target, the colour its stand-in world clears to, and where the UI shows it.
struct VtView {
    renderer::ViewportTarget* target = nullptr;
    f32 color[4] = {1, 0, 0, 1};
    ui::ImageId image = 3;
    ui::Rect rect;
};

// A frame in one graph: clear the panel, draw each view's world into its target, then the UI pass samples them over the panel. Reads the panel back.
void VtRunFrame(rhi::Device* device, renderer::TransientPool* pool, renderer::UiRenderer* ui_renderer, rhi::TextureHandle panel, rhi::Format format, VtView* views, usize count,
                u64 number, gpu_test::Image* out) {
    ArenaAllocator arena(4u << 20);
    renderer::RenderGraph graph(&arena, renderer::RenderGraphDesc{});
    pool->BeginFrame(number);
    VtWorld background;
    for (int i = 0; i < 4; ++i) background.color[i] = kVtPanel[i];
    background.target = graph.ImportTexture("panel", panel, rhi::ResourceState::Undefined, rhi::ResourceState::ColorAttachment, false);
    graph.AddPass("panel", &VtClearPass, &background).Write(background.target, renderer::GraphUsage::ColorAttachment);
    VtWorld worlds[4];
    renderer::GraphResource sampled[4];
    ui::DrawList list;
    ui::DrawListCanvas canvas(&list, nullptr);
    canvas.Begin(1.0f, kVtPanelW, kVtPanelH);
    for (usize i = 0; i < count && i < 4; ++i) {
        sampled[i] = views[i].target->ImportColor(&graph);
        worlds[i].target = sampled[i];
        for (int c = 0; c < 4; ++c) worlds[i].color[c] = views[i].color[c];
        graph.AddPass("world", &VtClearPass, &worlds[i]).Write(sampled[i], renderer::GraphUsage::ColorAttachment);
        ui_renderer->SetImage(views[i].image, views[i].target);
        canvas.DrawImage(views[i].rect, views[i].image, {0, 0, 1, 1}, 0xFFFFFFFFu);
    }
    list.SortLayers();
    ui_renderer->AddToGraph(&graph, &list, background.target, format, kVtPanelW, kVtPanelH, Span<const renderer::GraphResource>(sampled, count));
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
    out->width = kVtPanelW;
    out->height = kVtPanelH;
    out->rgba.assign(static_cast<usize>(kVtPanelW) * kVtPanelH * 4, 0);
    REQUIRE(device->ReadbackTexture(panel, 0, 0, out->rgba.data(), out->rgba.size(), rhi::ResourceState::ColorAttachment));
}

bool VtIs(const gpu_test::Image& image, u32 x, u32 y, int r, int g, int b, int tolerance = 2) {
    const u8* p = image.Pixel(x, y);
    return Abs(p[0] - r) <= tolerance && Abs(p[1] - g) <= tolerance && Abs(p[2] - b) <= tolerance;
}

ui::Rect VtRect(f32 x, f32 y, f32 w, f32 h) {
    ui::Rect r;
    r.x = x;
    r.y = y;
    r.w = w;
    r.h = h;
    return r;
}

rhi::TextureHandle VtPanelTexture(rhi::Device* device, rhi::Format format) {
    rhi::TextureDesc td;
    td.width = kVtPanelW;
    td.height = kVtPanelH;
    td.format = format;
    td.usage = rhi::TextureUsage::ColorAttachment | rhi::TextureUsage::CopySrc;
    return device->CreateTexture(td);
}

} // namespace

TEST_CASE("viewport target: sizes, resize, release and recreate on the null device", "[renderer][viewport]") {
    rhi::NullDevicePtr device = rhi::CreateNullDevice();
    renderer::DeviceResources registry;
    renderer::ViewportTarget target;
    renderer::ViewportTargetDesc desc;
    desc.with_depth = true;
    CHECK_FALSE(target.Create(device.Get(), desc, 0, 10, &registry, "vt"));
    REQUIRE(target.Create(device.Get(), desc, 64, 32, &registry, "vt"));
    CHECK(registry.Count() == 1);
    CHECK(target.Width() == 64);
    CHECK(target.Height() == 32);
    CHECK(target.Handle().IsValid());
    CHECK(target.DepthHandle().IsValid());
    const usize textures = device->Resources().textures;
    CHECK(textures == 2);
    CHECK(target.Resize(64, 32)); // the same size: nothing happens
    CHECK(target.ResizeCount() == 0);
    CHECK(target.Resize(80, 40));
    CHECK(target.ResizeCount() == 1);
    CHECK(device->Resources().textures == textures); // the old pair is gone, the new pair is there
    CHECK_FALSE(target.Resize(0, 40)); // refused, and the target keeps its size
    CHECK(target.Width() == 80);
    CHECK(target.Handle().IsValid());
    // A reset: released, resized while released, recreated at the size asked last.
    target.Release();
    CHECK_FALSE(target.Live());
    CHECK_FALSE(target.Handle().IsValid());
    CHECK(target.Resize(100, 50));
    REQUIRE(target.Recreate(device.Get()));
    CHECK(target.Live());
    CHECK(target.Width() == 100);
    CHECK(device->Resources().textures == textures);
    target.Destroy();
    CHECK(registry.Count() == 0);
    CHECK(device->Resources().textures == 0);
}

TEST_CASE("viewport target: the world drawn into it shows inside the panel rectangle only", "[renderer][viewport][gpu]") {
    FOR_EACH_GPU_BACKEND(backend) {
        gpu_test::BackendValidation validation(backend);
        rhi::DevicePtr device = gpu_test::CreateTestDevice(backend);
        renderer::UiRendererDesc ud;
        ud.color_format = rhi::Format::RGBA8_UNorm;
        renderer::UiRenderer ui_renderer;
        REQUIRE(ui_renderer.Create(device.Get(), nullptr, ud, nullptr));
        renderer::TransientPool pool(device.Get(), renderer::TransientPoolDesc{});
        renderer::ViewportTarget target;
        REQUIRE(target.Create(device.Get(), renderer::ViewportTargetDesc{}, 40, 24, nullptr, "vt"));
        const rhi::TextureHandle panel = VtPanelTexture(device.Get(), rhi::Format::RGBA8_UNorm);
        REQUIRE(panel.IsValid());
        VtView view;
        view.target = &target;
        view.rect = VtRect(10, 20, 40, 24);
        gpu_test::Image image;
        VtRunFrame(device.Get(), &pool, &ui_renderer, panel, rhi::Format::RGBA8_UNorm, &view, 1, 1, &image);
        CHECK(VtIs(image, 30, 32, 255, 0, 0));  // inside: the world's red
        CHECK(VtIs(image, 10, 20, 255, 0, 0));  // the corners of the rectangle
        CHECK(VtIs(image, 49, 43, 255, 0, 0));
        CHECK(VtIs(image, 9, 20, 0, 0, 255));   // outside: the panel
        CHECK(VtIs(image, 50, 30, 0, 0, 255));
        CHECK(VtIs(image, 30, 44, 0, 0, 255));
        // A second frame: the target is in its sampled state from the first, and the picture is the same.
        view.color[0] = 0;
        view.color[1] = 1;
        VtRunFrame(device.Get(), &pool, &ui_renderer, panel, rhi::Format::RGBA8_UNorm, &view, 1, 2, &image);
        CHECK(VtIs(image, 30, 32, 0, 255, 0));
        device->WaitIdle();
        ui_renderer.ClearImage(3);
        device->DestroyTexture(panel);
    }
}

TEST_CASE("viewport target: a resize keeps the picture, frees the old textures and the next frame fills the new one", "[renderer][viewport][gpu]") {
    FOR_EACH_GPU_BACKEND(backend) {
        gpu_test::BackendValidation validation(backend);
        rhi::DevicePtr device = gpu_test::CreateTestDevice(backend);
        renderer::UiRenderer ui_renderer;
        REQUIRE(ui_renderer.Create(device.Get(), nullptr, renderer::UiRendererDesc{}, nullptr));
        renderer::TransientPool pool(device.Get(), renderer::TransientPoolDesc{});
        renderer::ViewportTarget target;
        REQUIRE(target.Create(device.Get(), renderer::ViewportTargetDesc{}, 40, 24, nullptr, "vt"));
        const rhi::TextureHandle panel = VtPanelTexture(device.Get(), rhi::Format::RGBA8_UNorm);
        VtView view;
        view.target = &target;
        view.rect = VtRect(8, 8, 40, 24);
        gpu_test::Image image;
        VtRunFrame(device.Get(), &pool, &ui_renderer, panel, rhi::Format::RGBA8_UNorm, &view, 1, 1, &image);
        const usize textures = device->Resources().textures;
        REQUIRE(target.Resize(80, 48));
        CHECK(device->Resources().textures == textures); // the old texture is destroyed, the new one is live
        CHECK(target.ResizeCount() == 1);
        view.color[0] = 1;
        view.color[1] = 1; // yellow: the new world
        view.rect = VtRect(8, 8, 80, 48);
        VtRunFrame(device.Get(), &pool, &ui_renderer, panel, rhi::Format::RGBA8_UNorm, &view, 1, 2, &image);
        CHECK(VtIs(image, 50, 30, 255, 255, 0));
        CHECK(VtIs(image, 87, 55, 255, 255, 0));
        CHECK(VtIs(image, 88, 56, 0, 0, 255));
        device->WaitIdle();
        ui_renderer.ClearImage(3);
        device->DestroyTexture(panel);
    }
}

TEST_CASE("viewport target: two viewports draw different worlds in one UI pass", "[renderer][viewport][gpu]") {
    FOR_EACH_GPU_BACKEND(backend) {
        gpu_test::BackendValidation validation(backend);
        rhi::DevicePtr device = gpu_test::CreateTestDevice(backend);
        renderer::UiRenderer ui_renderer;
        REQUIRE(ui_renderer.Create(device.Get(), nullptr, renderer::UiRendererDesc{}, nullptr));
        renderer::TransientPool pool(device.Get(), renderer::TransientPoolDesc{});
        renderer::ViewportTarget left;
        renderer::ViewportTarget right;
        REQUIRE(left.Create(device.Get(), renderer::ViewportTargetDesc{}, 24, 24, nullptr, "left"));
        REQUIRE(right.Create(device.Get(), renderer::ViewportTargetDesc{}, 24, 24, nullptr, "right"));
        const rhi::TextureHandle panel = VtPanelTexture(device.Get(), rhi::Format::RGBA8_UNorm);
        VtView views[2];
        views[0].target = &left;
        views[0].image = 3;
        views[0].rect = VtRect(4, 4, 24, 24);
        views[1].target = &right;
        views[1].image = 4;
        views[1].color[0] = 0;
        views[1].color[1] = 1;
        views[1].rect = VtRect(40, 4, 24, 24);
        gpu_test::Image image;
        VtRunFrame(device.Get(), &pool, &ui_renderer, panel, rhi::Format::RGBA8_UNorm, views, 2, 1, &image);
        CHECK(VtIs(image, 16, 16, 255, 0, 0));
        CHECK(VtIs(image, 52, 16, 0, 255, 0));
        CHECK(VtIs(image, 34, 16, 0, 0, 255));
        CHECK(ui_renderer.Stats().passes == 1);
        device->WaitIdle();
        ui_renderer.ClearImage(3);
        ui_renderer.ClearImage(4);
        device->DestroyTexture(panel);
    }
}

TEST_CASE("viewport target: the lit cube drawn into it shows in the panel and returns after a device reset", "[renderer][viewport][gpu][recreatable]") {
    FOR_EACH_GPU_BACKEND(backend) {
        gpu_test::BackendValidation validation(backend);
        reset_rig::Rig rig;
        if (!rig.Create(backend)) SKIP("no device");
        reset_rig::Target panel;
        panel.width = kVtPanelW;
        panel.height = kVtPanelH;
        REQUIRE(panel.Make(rig.host.GetDevice()));
        renderer::ScopedRegistration panel_registration;
        panel_registration.Attach(&rig.registry, &panel, renderer::tiers::kTextures);
        renderer::ViewportTarget target;
        REQUIRE(target.Create(rig.host.GetDevice(), renderer::ViewportTargetDesc{rhi::Format::RGBA8_SRGB, false, rhi::Format::D32_Float}, rig.target.width, rig.target.height,
                              &rig.registry, "cube view"));
        renderer::UiRendererDesc ud;
        ud.color_format = rhi::Format::RGBA8_SRGB;
        renderer::UiRenderer ui_renderer;
        REQUIRE(ui_renderer.Create(rig.host.GetDevice(), nullptr, ud, &rig.registry));
        ui_renderer.SetImage(3, &target);

        auto draw_world = [&]() {
            renderer::FrameTarget ft = target.AsFrameTarget();
            REQUIRE(rig.frame.Render(&rig.scene.Scene(), &rig.camera, &ft) == renderer::FrameStatus::Ok);
        };
        auto show = [&](gpu_test::Image* out) {
            rhi::Device* device = rig.host.GetDevice();
            renderer::TransientPool pool(device, renderer::TransientPoolDesc{});
            ArenaAllocator arena(4u << 20);
            renderer::RenderGraph graph(&arena, renderer::RenderGraphDesc{});
            pool.BeginFrame(1);
            VtWorld background;
            for (int i = 0; i < 4; ++i) background.color[i] = kVtPanel[i];
            background.target = graph.ImportTexture("panel", panel.handle, rhi::ResourceState::Undefined, rhi::ResourceState::ColorAttachment, false);
            graph.AddPass("panel", &VtClearPass, &background).Write(background.target, renderer::GraphUsage::ColorAttachment);
            ui::DrawList list;
            ui::DrawListCanvas canvas(&list, nullptr);
            canvas.Begin(1.0f, kVtPanelW, kVtPanelH);
            canvas.DrawImage(VtRect(16, 16, static_cast<f32>(rig.target.width), static_cast<f32>(rig.target.height)), 3, {0, 0, 1, 1}, 0xFFFFFFFFu);
            list.SortLayers();
            ui_renderer.AddToGraph(&graph, &list, background.target, rhi::Format::RGBA8_SRGB, kVtPanelW, kVtPanelH);
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
            pool.Release();
            out->width = kVtPanelW;
            out->height = kVtPanelH;
            out->rgba.assign(static_cast<usize>(kVtPanelW) * kVtPanelH * 4, 0);
            REQUIRE(device->ReadbackTexture(panel.handle, 0, 0, out->rgba.data(), out->rgba.size(), rhi::ResourceState::ColorAttachment));
        };
        // The same world drawn straight into the rig's own target is the reference for the picture inside the rectangle.
        REQUIRE(rig.Render() == renderer::FrameStatus::Ok);
        gpu_test::Image reference;
        REQUIRE(rig.Read(&reference));

        auto check = [&](const gpu_test::Image& shown, const char* when) {
            INFO(when);
            u32 differing = 0;
            u32 lit = 0;
            for (u32 y = 0; y < rig.target.height; ++y) {
                for (u32 x = 0; x < rig.target.width; ++x) {
                    const u8* a = reference.Pixel(x, y);
                    const u8* b = shown.Pixel(x + 16, y + 16);
                    if (Abs(a[0] - b[0]) > 3 || Abs(a[1] - b[1]) > 3 || Abs(a[2] - b[2]) > 3) ++differing;
                    if (a[0] + a[1] + a[2] > 30) ++lit;
                }
            }
            CHECK(lit > 100);       // the reference does show objects
            CHECK(differing == 0);  // and the panel shows the same picture inside the rectangle
            CHECK(VtIs(shown, 4, 4, 0, 0, 255));      // the panel colour outside
            CHECK(VtIs(shown, 120, 90, 0, 0, 255));
        };
        gpu_test::Image shown;
        draw_world();
        show(&shown);
        check(shown, "first frame");

        const rhi::ResourceCounts before = rig.host.GetDevice()->Resources();
        REQUIRE(rig.ResetDevice(true).ok);
        CHECK(rig.host.LeakedObjects() == 0); // the old device held nothing the registry did not release
        REQUIRE(target.Live());
        CHECK(target.Handle().IsValid());
        ui_renderer.SetImage(3, &target);
        // After the reset the picture is empty until the world is drawn again; then it is back.
        draw_world();
        show(&shown);
        check(shown, "after the reset");
        const rhi::ResourceCounts after = rig.host.GetDevice()->Resources();
        CHECK(after.textures == before.textures);
        rig.host.GetDevice()->WaitIdle();
        ui_renderer.Destroy();
        target.Destroy();
        panel_registration.Reset();
        panel.Release();
    }
}
