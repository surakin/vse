#include "demo_frame.h"

#include <cstring>

#include "io/path.h"
#include "rhi/rhi_log.h"

namespace renderer_demo {

using namespace engine_placeholder::renderer;

DemoFrame::DemoFrame() = default;
DemoFrame::~DemoFrame() { Destroy(); }

void DemoFrame::Destroy() {
    if (device_ != nullptr) device_->WaitIdle(); // frames may still be in flight
    // Reverse order of creation: the renderer (and its pooled textures) first, then the passes, then what they borrow.
    renderer_.Reset();
    text_.Destroy();
    font_.Close();
    overlay_on_ = false;
    debug_view_.Reset();
    tonemap_.Reset();
    lighting_.Reset();
    gbuffer_.Reset();
    frame_.Reset();
    materials_.Reset();
    templates_ = GBufferTemplates{};
    gbuffer_stats_ = GBufferStats{};
    light_summary_ = LightSummary{};
    device_ = nullptr;
}

bool DemoFrame::Create(const FrameSetup& setup) {
    Destroy();
    char error[256];
    if (!CheckGBufferFormats(setup.device, error, sizeof(error))) {
        rhi::RhiLogV(LogVerbosity::Error, "renderer_demo: %s", error);
        return false;
    }
    device_ = setup.device;
    gbuffer_layer_ = setup.gbuffer_layer;
    exposure_ = setup.exposure;
    MaterialSystemDesc md;
    md.resources = setup.resources;
    md.prewarm_pipelines = setup.prewarm_pipelines;
    materials_ = MakeUnique<MaterialSystem>(setup.device, md);
    frame_ = MakeUnique<FrameResources>(setup.device, setup.max_objects, setup.max_lights);
    if (!frame_->RegisterWith(setup.resources) || !materials_->Valid() || !frame_->Valid()) {
        rhi::RhiLogV(LogVerbosity::Error, "renderer_demo: could not create the material system or the frame buffers");
        Destroy();
        return false;
    }
    templates_ = CreateGBufferTemplates(materials_.Get());
    gbuffer_ = MakeUnique<GBufferPass>(setup.device, materials_.Get(), frame_.Get());
    lighting_ = MakeUnique<LightingPass>(setup.device, materials_.Get(), frame_.Get());
    tonemap_ = MakeUnique<TonemapPass>(setup.device);
    debug_view_ = MakeUnique<GBufferDebugView>(setup.device);
    const bool registered = lighting_->RegisterWith(setup.resources) && tonemap_->RegisterWith(setup.resources) && debug_view_->RegisterWith(setup.resources);
    if (!registered || !templates_.plain.IsValid() || !templates_.colored.IsValid() || !lighting_->Valid() || !tonemap_->Valid() || !debug_view_->Valid()) {
        rhi::RhiLogV(LogVerbosity::Error, "renderer_demo: could not create the render passes");
        Destroy();
        return false;
    }
    RendererDesc rd;
    rd.device = setup.device;
    rd.swapchain = setup.swapchain;
    rd.scheduler = setup.scheduler;
    rd.resources = setup.resources;
    rd.host = setup.host;
    rd.create_info = setup.create_info;
    rd.initial_settings = setup.initial_settings;
    rd.window = setup.window;
    rd.recovery = setup.recovery;
    rd.build = &DemoFrame::Build;
    rd.build_user = this;
    renderer_ = MakeUnique<Renderer>(rd);
    if (setup.font_path != nullptr) {
        // The overlay is optional: a missing font or a device without the text shaders leaves the frame without it, with the reason logged.
        text::FontOptions fo;
        if (font_library_.IsInitialized() || font_library_.Initialize().Ok()) {
            const text::Status opened = text::Font::OpenFile(font_library_, io::Path(StringView(setup.font_path, std::strlen(setup.font_path))), fo, &font_);
            if (opened.Ok() && !text_.Create(setup.device, &atlas_, renderer::TextRendererDesc{rhi::Format::RGBA8_SRGB, 4096}, setup.resources)) {
                font_.Close();
            } else if (!opened.Ok()) {
                rhi::RhiLogV(LogVerbosity::Warn, "renderer_demo: the overlay font '%s' did not open: %s", setup.font_path, opened.message);
            }
        }
    }
    return true;
}

bool DemoFrame::SetOverlay(bool on, StringView greeting, StringView status) {
    if (!font_.IsOpen()) {
        return false;
    }
    overlay_on_ = on;
    greeting_.Clear();
    for (usize i = 0; i < greeting.Length(); ++i) greeting_.Push(greeting[i]);
    status_.Clear();
    for (usize i = 0; i < status.Length(); ++i) status_.Push(status[i]);
    return true;
}

void DemoFrame::Rebind(rhi::Device* device, rhi::Swapchain* swapchain) {
    if (!renderer_) return;
    device_ = device;
    renderer_->Rebind(device, swapchain);
}

FrameStatus DemoFrame::Render(const RenderScene* scene, const Camera* camera, const FrameTarget* offscreen) {
    return renderer_->RenderFrame(scene, camera, offscreen);
}

bool DemoFrame::Build(const FrameContext& context, void* user) {
    DemoFrame* self = static_cast<DemoFrame*>(user);
    if (context.scene == nullptr || context.camera == nullptr) {
        return false;
    }
    self->materials_->BeginFrame(context.frame_index);
    self->materials_->FlushMaterials();
    self->frame_->BeginFrame(context.frame_index);
    const FrameData data = MakeFrameData(*context.camera, context.width, context.height, self->time_, self->delta_, context.frame_index);
    self->frame_->WriteFrameData(data);
    self->gbuffer_ok_ = self->gbuffer_->Prepare(*context.scene, Frustum::FromViewProjection(context.camera->ViewProjection()));
    if (!self->gbuffer_ok_) {
        rhi::RhiLogV(LogVerbosity::Warn, "renderer_demo: some objects were not drawn: %s", self->gbuffer_->LastError());
    }
    self->gbuffer_stats_ = self->gbuffer_->Stats();
    self->light_summary_ = self->lighting_->Prepare(*context.scene, data);
    self->frame_->Flush();

    const GBufferTargets targets = self->gbuffer_->AddToGraph(context.graph, context.width, context.height);
    if (self->gbuffer_layer_ >= 0) {
        self->debug_view_->SetInverseViewProjection(data.inverse_view_projection);
        self->debug_view_->AddToGraph(context.graph, targets, static_cast<GBufferLayer>(self->gbuffer_layer_), context.target, context.format,
                                      context.camera->near_plane, context.camera->far_plane);
    } else {
        const GraphResource accum = self->lighting_->AddToGraph(context.graph, targets, context.width, context.height);
        self->tonemap_->AddToGraph(context.graph, accum, context.target, context.format, self->exposure_);
    }
    if (self->overlay_on_ && self->font_.IsOpen() && self->text_.Live()) {
        // After the passes that wrote the target (the tonemap or the debug view), over their picture.
        text::TextStyle big;
        big.font = &self->font_;
        big.pixel_size = 32;
        big.color = 0xFFFFFFFFu;
        text::TextStyle small = big;
        small.pixel_size = 14;
        small.color = 0xFFC8C8C8u;
        self->text_.BeginFrame();
        self->text_.Draw(StringView(self->greeting_.Data(), self->greeting_.Size()), big, text::LayoutOptions{}, 16, 12);
        self->text_.Draw(StringView(self->status_.Data(), self->status_.Size()), small, text::LayoutOptions{}, 16, 52);
        self->text_.AddToGraph(context.graph, context.target, context.format, context.width, context.height);
    }
    return true;
}

} // namespace renderer_demo
