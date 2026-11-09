#include "ui_demo_gpu.h"

#include <cmath>

#include "io/path.h"
#include "renderer/camera.h"
#include "renderer/gbuffer.h"
#include "rhi/rhi_log.h"

namespace ui_demo {

namespace {

constexpr u32 kInitialViewport = 64;

} // namespace

bool UiDemoGpu::Create(rhi::Device* device, renderer::DeviceResources* resources, renderer_demo::DemoFrame* frame, renderer_demo::DemoScene* scene, const char* font_file, rhi::Format ui_format) {
    device_ = device;
    frame_ = frame;
    scene_ = scene;
    if (!library_.Initialize().Ok() || !text::Font::OpenFile(library_, io::Path(StringView(font_file)), text::FontOptions{}, &font_).Ok()) {
        rhi::RhiLogV(LogVerbosity::Error, "ui_demo: cannot open the font %s", font_file);
        return false;
    }
    font_ok_ = true;
    renderer::UiRendererDesc ud;
    ud.color_format = ui_format;
    renderer::ViewportTargetDesc vd;
    vd.color_format = rhi::Format::RGBA8_SRGB;
    if (!pages_.Create(device, &atlas_, resources) || !ui_renderer_.Create(device, &pages_, ud, resources) || !viewport_.Create(device, vd, kInitialViewport, kInitialViewport, resources, "ui_demo viewport")) {
        rhi::RhiLogV(LogVerbosity::Error, "ui_demo: cannot create the UI renderer or the viewport target");
        return false;
    }
    ui_renderer_.SetImage(kViewportImage, &viewport_);
    renderer::UiRendererDesc wd;
    wd.color_format = rhi::Format::RGBA8_SRGB;
    renderer::UiSurfaceTargetDesc wt;
    wt.format = rhi::Format::RGBA8_SRGB;
    if (!world_ui_.Create(device, &pages_, wd, resources) ||
        !world_target_.Create(device, wt, static_cast<u32>(WorldScreen::kWidth * WorldScreen::kScale), static_cast<u32>(WorldScreen::kHeight * WorldScreen::kScale), resources, "ui_demo world screen")) {
        rhi::RhiLogV(LogVerbosity::Error, "ui_demo: cannot create the in-world screen");
        return false;
    }
    renderer_demo::FrameCompose compose;
    compose.scene_target = &UiDemoGpu::SceneTarget;
    compose.after_scene = &UiDemoGpu::AfterScene;
    compose.user = this;
    frame->SetCompose(compose);
    created_ = true;
    return true;
}

void UiDemoGpu::Destroy() {
    if (!created_) return;
    world_target_.Destroy();
    world_ui_.Destroy();
    ui_renderer_.Destroy();
    pages_.Destroy();
    viewport_.Destroy();
    created_ = false;
}

void UiDemoGpu::Attach(UiDemoApp* app, ui::Surface* surface) {
    app_ = app;
    surface_ = surface;
    surface->SetFontProvider(this);
    app->World().GetSurface().SetFontProvider(this);
    app->SetViewportImage(kViewportImage);
    app->SetSceneScale(scene_->Radius() * 0.25f + 1.0f);
}

bool UiDemoGpu::Update(f32 dt) {
    const bool rebuilt = surface_->Frame(dt, &list_, &atlas_);
    const u32 w = app_->ViewportWidth();
    const u32 h = app_->ViewportHeight();
    if (w > 0 && h > 0 && viewport_.Live()) viewport_.Resize(w, h);
    if (app_->World().GetSurface().Frame(dt, &world_list_, &atlas_)) world_dirty_ = true;
    // The screen's material samples the texture of this frame; the handle changes after a reset, so it is bound again each frame.
    if (scene_->ScreenMaterial().IsValid()) frame_->Materials()->SetTexture(scene_->ScreenMaterial(), "albedo_tex", world_target_.Handle());
    return rebuilt;
}

renderer::Camera UiDemoGpu::MakeCamera() const {
    const OrbitCamera& orbit = app_->Camera();
    const Vec3 center = scene_->Center();
    f32 e[3];
    orbit.Eye(app_->SceneScale(), e);
    const Vec3 eye = center + Vec3(e[0], e[1], e[2]);
    renderer::Camera camera = renderer::MakeLookAtCamera(eye, center + Vec3(0, 0.5f, 0), Vec3(0, 1, 0));
    const u32 w = app_->ViewportWidth() > 0 ? app_->ViewportWidth() : kInitialViewport;
    const u32 h = app_->ViewportHeight() > 0 ? app_->ViewportHeight() : kInitialViewport;
    camera.aspect = static_cast<f32>(w) / static_cast<f32>(h);
    camera.fov_y = OrbitCamera::kFovY;
    camera.near_plane = 0.1f;
    camera.far_plane = scene_->Radius() * 6.0f + 60.0f;
    renderer::FitCameraDepthRange(scene_->Scene(), &camera, 0.1f);
    return camera;
}

bool UiDemoGpu::SceneTarget(const renderer::FrameContext& context, void* user, renderer::GraphResource* target, u32* width, u32* height, rhi::Format* format) {
    UiDemoGpu* self = static_cast<UiDemoGpu*>(user);
    if (!self->viewport_.Live()) return false;
    // The in-world screen first: painted when its surface changed (or its texture is new), then a pass that reads it, so the graph leaves the texture ready to sample before the scene's passes.
    self->world_target_.BeginFrame();
    renderer::GraphResource screen;
    if (self->world_dirty_ || self->world_target_.NeedsPaint()) {
        screen = self->world_target_.AddToGraph(context.graph, &self->world_ui_, &self->world_list_);
        self->world_dirty_ = false;
    } else {
        screen = self->world_target_.Sampled(context.graph);
    }
    context.graph->AddPass("world screen ready", nullptr, nullptr, renderer::PassFlags::SideEffects).Read(screen, renderer::GraphUsage::Sampled);
    self->viewport_resource_ = self->viewport_.ImportColor(context.graph);
    *target = self->viewport_resource_;
    *width = self->viewport_.Width();
    *height = self->viewport_.Height();
    *format = self->viewport_.ColorFormat();
    return true;
}

void UiDemoGpu::ClearPass(const renderer::PassExecuteContext& context, void* user) {
    const UiDemoGpu* self = static_cast<const UiDemoGpu*>(user);
    rhi::RenderingDesc rd;
    rd.color_count = 1;
    rd.color[0].texture = context.graph->ImportedTexture(self->frame_target_);
    context.list->BeginRendering(rd);
    context.list->EndRendering();
}

void UiDemoGpu::AfterScene(const renderer::FrameContext& context, void* user) {
    UiDemoGpu* self = static_cast<UiDemoGpu*>(user);
    // The frame's target holds nothing yet (the scene went to the viewport's texture): clear it, so the corners the rounded window panel leaves uncovered show black, not what was there.
    self->frame_target_ = context.target;
    context.graph->AddPass("clear target", &UiDemoGpu::ClearPass, self).Write(context.target, renderer::GraphUsage::ColorAttachment);
    self->ui_renderer_.AddToGraph(context.graph, &self->list_, context.target, context.format, context.width, context.height,
                                  ::engine_placeholder::Span<const renderer::GraphResource>(&self->viewport_resource_, 1));
}

} // namespace ui_demo
