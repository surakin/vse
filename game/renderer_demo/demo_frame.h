#pragma once

// The renderer demo's frame: material system, per-frame buffers, the G-buffer, lighting and tonemap passes, and the
// renderer::Renderer frame loop that runs them. One object per device; used by the demo and the tests.

#include "containers/unique_ptr.h"
#include "renderer/frame_data.h"
#include "renderer/gbuffer.h"
#include "renderer/lighting.h"
#include "renderer/material.h"
#include "renderer/renderer.h"
#include "rhi/rhi.h"
#include "types.h"

namespace renderer_demo {

using namespace engine_placeholder;

struct FrameSetup {
    rhi::Device* device = nullptr;
    // The swapchain to present to, or null to render to a FrameTarget given to each Render call.
    rhi::Swapchain* swapchain = nullptr;
    // The most objects and lights one frame writes.
    u32 max_objects = 1024;
    u32 max_lights = 1024;
    // A G-buffer layer to show instead of the lit picture, or -1 for the lit picture.
    int gbuffer_layer = -1;
    // The tonemap exposure.
    f32 exposure = 1.0f;
};

class DemoFrame {
public:
    DemoFrame();
    ~DemoFrame();
    DemoFrame(const DemoFrame&) = delete;
    DemoFrame& operator=(const DemoFrame&) = delete;

    // Creates everything. Returns false, logging why, if a part could not be created (the G-buffer formats are unsupported, a
    // shader or a pipeline fails).
    bool Create(const FrameSetup& setup);
    // Releases everything; idle the device first. The destructor calls it.
    void Destroy();

    renderer::MaterialSystem* Materials() { return materials_.Get(); }
    const renderer::GBufferTemplates& Templates() const { return templates_; }

    // Sets the clock the next frame's FrameData carries.
    void SetTime(f32 seconds, f32 delta_seconds) {
        time_ = seconds;
        delta_ = delta_seconds;
    }
    // Renders one frame of `scene` seen by `camera`. See renderer::Renderer::RenderFrame.
    renderer::FrameStatus Render(const renderer::RenderScene* scene, const renderer::Camera* camera, const renderer::FrameTarget* offscreen);

    const char* LastError() const { return renderer_ ? renderer_->LastError() : "not created"; }
    renderer::RendererStats Stats() const { return renderer_ ? renderer_->Stats() : renderer::RendererStats{}; }
    // What the last frame drew.
    const renderer::GBufferStats& GBufferStatistics() const { return gbuffer_stats_; }
    const renderer::LightSummary& Lights() const { return light_summary_; }
    u32 PointDraws() const { return lighting_ ? lighting_->PointDrawsRecorded() : 0; }
    // Rejected objects of the last frame (see renderer::GBufferPass::Prepare).
    bool LastGBufferOk() const { return gbuffer_ok_; }

private:
    static bool Build(const renderer::FrameContext& context, void* user);

    rhi::Device* device_ = nullptr;
    UniquePtr<renderer::MaterialSystem> materials_;
    UniquePtr<renderer::FrameResources> frame_;
    UniquePtr<renderer::GBufferPass> gbuffer_;
    UniquePtr<renderer::LightingPass> lighting_;
    UniquePtr<renderer::TonemapPass> tonemap_;
    UniquePtr<renderer::GBufferDebugView> debug_view_;
    UniquePtr<renderer::Renderer> renderer_;
    renderer::GBufferTemplates templates_;
    renderer::GBufferStats gbuffer_stats_;
    renderer::LightSummary light_summary_;
    bool gbuffer_ok_ = true;
    int gbuffer_layer_ = -1;
    f32 exposure_ = 1.0f;
    f32 time_ = 0.0f;
    f32 delta_ = 0.0f;
};

} // namespace renderer_demo
