#pragma once

// The GPU side of the ui_demo (Phase 15, Day 204): the font, the glyph pages, the UI renderer and the viewport's render
// target, and the two hooks that put the 3D scene in the viewport's texture and draw the window over the frame's
// target. The widget tree (ui_demo_app.h) names none of it; main.cpp and the GPU tests use this.

#include "demo_frame.h"
#include "demo_scene.h"
#include "renderer/device_resources.h"
#include "renderer/glyph_atlas_texture.h"
#include "renderer/ui_renderer.h"
#include "renderer/ui_surface_target.h"
#include "renderer/viewport_target.h"
#include "text/font.h"
#include "text/glyph_atlas.h"
#include "types.h"
#include "ui/draw_list.h"
#include "ui/surface.h"
#include "ui_demo_app.h"

namespace ui_demo
{

using namespace vse;

/// The image id the viewport's picture has in the UI renderer.
constexpr ui::ImageId kViewportImage = 1;

/// Everything the demo draws with.
///
/// - Thread-safety: owner-thread-only.
/// - Ownership: owns the font, the atlas, the GPU objects and the draw list; borrows the device, the registry, the
/// frame, the scene and the app.
class UiDemoGpu final : public ui::FontProvider
{
public:
    UiDemoGpu() = default;
    ~UiDemoGpu() override { Destroy(); }
    UiDemoGpu(const UiDemoGpu&) = delete;
    UiDemoGpu& operator=(const UiDemoGpu&) = delete;

    /// Creates the font, the pages, the renderer and the viewport target, and installs the compose hooks on `frame`.
    /// - Param device: the device; borrowed.
    /// - Param resources: the registry that makes everything survive a reset; borrowed.
    /// - Param frame: the renderer demo's frame; borrowed.
    /// - Param scene: the scene drawn in the viewport; borrowed.
    /// - Param font_file: a TrueType file.
    /// - Param ui_format: the format of the frame's target (the swapchain's or the offscreen texture's).
    /// - Returns: false, logging why, if a part could not be made.
    bool Create(rhi::Device* device, renderer::DeviceResources* resources, renderer_demo::DemoFrame* frame,
                renderer_demo::DemoScene* scene, const char* font_file, rhi::Format ui_format);
    /// Releases everything; idle the device first.
    void Destroy();
    /// Points the object at the app whose viewport it follows (the app registers its image). Call once after both
    /// exist.
    void Attach(UiDemoApp* app, ui::Surface* surface);
    /// Runs the surface's frame, resizes the viewport's texture to the size the viewport asked for and keeps the draw
    /// list.
    /// - Returns: whether the draw list was rebuilt.
    bool Update(f32 dt);
    /// The camera for this frame, from the app's orbit camera.
    renderer::Camera MakeCamera() const;

    text::Font* Resolve(u32) override { return font_ok_ ? &font_ : nullptr; }
    text::GlyphAtlas& Atlas() { return atlas_; }
    renderer::UiRenderer& Ui() { return ui_renderer_; }
    renderer::ViewportTarget& Viewport() { return viewport_; }
    /// The texture of the in-world screen and the renderer that paints it.
    renderer::UiSurfaceTarget& WorldTarget() { return world_target_; }
    const ui::DrawList& List() const { return list_; }

private:
    static bool SceneTarget(const renderer::FrameContext& context, void* user, renderer::GraphResource* target,
                            u32* width, u32* height, rhi::Format* format);
    static void ClearPass(const renderer::PassExecuteContext& context, void* user);
    static void AfterScene(const renderer::FrameContext& context, void* user);

    rhi::Device* device_ = nullptr;
    renderer_demo::DemoFrame* frame_ = nullptr;
    renderer_demo::DemoScene* scene_ = nullptr;
    UiDemoApp* app_ = nullptr;
    ui::Surface* surface_ = nullptr;
    text::FontLibrary library_;
    text::Font font_;
    bool font_ok_ = false;
    text::GlyphAtlas atlas_;
    renderer::GlyphAtlasTexture pages_;
    renderer::UiRenderer ui_renderer_;
    renderer::ViewportTarget viewport_;
    renderer::GraphResource viewport_resource_;
    renderer::GraphResource frame_target_;
    ui::DrawList list_;
    renderer::UiRenderer world_ui_;
    renderer::UiSurfaceTarget world_target_;
    ui::DrawList world_list_;
    bool world_dirty_ = true;
    bool created_ = false;
};

}  // namespace ui_demo
