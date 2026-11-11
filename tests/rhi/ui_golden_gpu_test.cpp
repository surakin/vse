// Phase 15 Day 190: the golden image of the UI pass. The scene is a shadow, a rounded button with a ring, a disc and a
// word, drawn from a ui::DrawList through the render graph. Only Vulkan writes the golden (ENGINE_RHI_UPDATE_GOLDEN=1);
// Direct3D 12 is checked against it (DXIL since Phase 15 Day 205).
#include <catch2/catch_test_macros.hpp>

#include "backend_fixture.h"
#include "image_compare.h"
#include "io/path.h"
#include "mem/arena_allocator.h"
#include "renderer/render_graph.h"
#include "renderer/transient_pool.h"
#include "renderer/ui_renderer.h"
#include "text/font.h"
#include "text/glyph_atlas.h"
#include "text/layout.h"
#include "ui/draw_list.h"
#include "ui/draw_list_canvas.h"

using namespace engine_placeholder;

namespace
{

constexpr u32 kW = 160;
constexpr u32 kH = 100;

void ClearPass(const renderer::PassExecuteContext& ctx, void* user)
{
    rhi::RenderingDesc rd;
    rd.color_count = 1;
    rd.color[0].texture = ctx.graph->ImportedTexture(*static_cast<renderer::GraphResource*>(user));
    ctx.list->BeginRendering(rd);
    ctx.list->EndRendering();
}

}  // namespace

TEST_CASE("The UI pass matches its golden image", "[rhi][gpu][golden][ui]")
{
    FOR_EACH_GPU_BACKEND(backend)
    {
        gpu_test::BackendValidation validation(backend);
        rhi::DevicePtr device = gpu_test::CreateTestDevice(backend);
        text::FontLibrary library;
        text::Font font;
        REQUIRE(library.Initialize().Ok());
        REQUIRE(text::Font::OpenFile(library, io::Path(StringView(ENGINE_TEXT_FONT_FILE)), text::FontOptions{}, &font)
                    .Ok());
        text::GlyphAtlas atlas;
        renderer::GlyphAtlasTexture pages;
        REQUIRE(pages.Create(device.Get(), &atlas, nullptr));
        renderer::UiRenderer ui_renderer;
        REQUIRE(ui_renderer.Create(device.Get(), &pages, renderer::UiRendererDesc{}, nullptr));

        text::TextStyle style;
        style.font = &font;
        style.pixel_size = 16;
        text::TextLayout layout;
        REQUIRE(text::Layout(StringView("Button"), style, text::LayoutOptions{}, &layout).Ok());
        ui::DrawList list;
        ui::DrawListCanvas canvas(&list, &atlas);
        canvas.Begin(1.0f, kW, kH);
        canvas.DrawRect({0, 0, 160, 100}, 0xFF402020u);
        canvas.DrawShadow({20, 20, 120, 60}, 8, 12, 0x80000000u);
        canvas.DrawRoundedRect({20, 20, 120, 60}, ui::CornerRadii(8), 0xFFB07030u);
        canvas.DrawBorder({20, 20, 120, 60}, ui::CornerRadii(8), 2, 0xFFFFFFFFu);
        canvas.DrawRoundedRect({30, 30, 40, 40}, ui::CornerRadii(20), 0xFF30C030u);
        canvas.DrawText(layout, style, {80, 44}, 0xFFFFFFFFu);
        list.SortLayers();

        rhi::TextureDesc td;
        td.width = kW;
        td.height = kH;
        td.format = rhi::Format::RGBA8_UNorm;
        td.usage = rhi::TextureUsage::ColorAttachment | rhi::TextureUsage::CopySrc;
        rhi::TextureHandle texture = device->CreateTexture(td);
        REQUIRE(texture.IsValid());
        renderer::TransientPool pool(device.Get(), renderer::TransientPoolDesc{});
        pool.BeginFrame(0);
        ArenaAllocator arena(4u << 20);
        renderer::RenderGraph graph(&arena, renderer::RenderGraphDesc{});
        renderer::GraphResource target = graph.ImportTexture("target", texture, rhi::ResourceState::Undefined,
                                                             rhi::ResourceState::ColorAttachment, false);
        graph.AddPass("clear", &ClearPass, &target).Write(target, renderer::GraphUsage::ColorAttachment);
        ui_renderer.AddToGraph(&graph, &list, target, td.format, kW, kH);
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
        gpu_test::Image image;
        image.width = kW;
        image.height = kH;
        image.rgba.Assign(static_cast<engine_placeholder::usize>(kW) * kH * 4, 0);
        REQUIRE(device->ReadbackTexture(texture, 0, 0, image.rgba.Data(), image.rgba.Size(),
                                        rhi::ResourceState::ColorAttachment));

        if (gpu_test::UpdatingGoldens())
        {
            REQUIRE(gpu_test::WritePpm(gpu_test::GoldenPath("ui_scene.ppm").CStr(), image));
            WARN(
                "ENGINE_RHI_UPDATE_GOLDEN=1: tests/rhi/golden/ui_scene.ppm was rewritten; inspect it before committing");
        }
        else
        {
            gpu_test::Image golden;
            REQUIRE(gpu_test::ReadPpm(gpu_test::GoldenPath("ui_scene.ppm").CStr(), &golden));
            gpu_test::CompareOptions co;
            co.tolerance = 2;
            co.edge_tolerance = 4;
            co.max_edge_miss = 0.0;
            co.max_interior_miss = 0.0;
            const gpu_test::CompareResult result = gpu_test::Compare(golden, image, co, "ui_scene_vulkan");
            INFO(result.message.CStr());
            CHECK(result.same);
        }
        device->WaitIdle();
        device->DestroyTexture(texture);
    }
}
