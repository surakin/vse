#include "demo_frame.h"

#include "rhi/rhi_log.h"

namespace renderer_demo {

using namespace engine_placeholder::renderer;

DemoFrame::DemoFrame() = default;
DemoFrame::~DemoFrame() { Destroy(); }

void DemoFrame::Destroy() {
    if (device_ != nullptr) device_->WaitIdle(); // frames may still be in flight
    // Reverse order of creation: the renderer (and its pooled textures) first, then the passes, then what they borrow.
    renderer_.Reset();
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
    materials_ = MakeUnique<MaterialSystem>(setup.device, MaterialSystemDesc{});
    frame_ = MakeUnique<FrameResources>(setup.device, setup.max_objects, setup.max_lights);
    if (!materials_->Valid() || !frame_->Valid()) {
        rhi::RhiLogV(LogVerbosity::Error, "renderer_demo: could not create the material system or the frame buffers");
        Destroy();
        return false;
    }
    templates_ = CreateGBufferTemplates(materials_.Get());
    gbuffer_ = MakeUnique<GBufferPass>(setup.device, materials_.Get(), frame_.Get());
    lighting_ = MakeUnique<LightingPass>(setup.device, materials_.Get(), frame_.Get());
    tonemap_ = MakeUnique<TonemapPass>(setup.device);
    debug_view_ = MakeUnique<GBufferDebugView>(setup.device);
    if (!templates_.plain.IsValid() || !templates_.colored.IsValid() || !lighting_->Valid() || !tonemap_->Valid() || !debug_view_->Valid()) {
        rhi::RhiLogV(LogVerbosity::Error, "renderer_demo: could not create the render passes");
        Destroy();
        return false;
    }
    RendererDesc rd;
    rd.device = setup.device;
    rd.swapchain = setup.swapchain;
    rd.scheduler = setup.scheduler;
    rd.build = &DemoFrame::Build;
    rd.build_user = this;
    renderer_ = MakeUnique<Renderer>(rd);
    return true;
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
    return true;
}

} // namespace renderer_demo
