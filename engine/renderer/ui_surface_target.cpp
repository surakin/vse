#include "renderer/ui_surface_target.h"

#include <cmath>

#include "log/assert.h"

namespace engine_placeholder::renderer {

namespace {

constexpr f32 kByteMax = 255.0f;
constexpr f32 kSrgbLinearCut = 0.04045f;
constexpr f32 kSrgbLinearDivisor = 12.92f;
constexpr f32 kSrgbOffset = 0.055f;
constexpr f32 kSrgbScale = 1.055f;
constexpr f32 kSrgbGamma = 2.4f;

// The value a clear must be given so that an sRGB target holds the byte the theme names: the clear value is linear and the hardware encodes it.
f32 ToLinear(f32 encoded) { return encoded <= kSrgbLinearCut ? encoded / kSrgbLinearDivisor : std::pow((encoded + kSrgbOffset) / kSrgbScale, kSrgbGamma); }

} // namespace

bool UiSurfaceTarget::Create(rhi::Device* device, const UiSurfaceTargetDesc& desc, u32 width, u32 height, DeviceResources* resources, const char* debug_name) {
    ViewportTargetDesc vd;
    vd.color_format = desc.format;
    if (!target_.Create(device, vd, width, height, resources, debug_name)) return false;
    SetBackground(desc.background);
    painted_ = false;
    return true;
}

bool UiSurfaceTarget::Resize(u32 width, u32 height) {
    const u32 old_w = target_.Width();
    const u32 old_h = target_.Height();
    if (!target_.Resize(width, height)) return false;
    if (width != old_w || height != old_h) painted_ = false;
    return true;
}

void UiSurfaceTarget::SetBackground(u32 abgr) {
    background_ = abgr;
    painted_ = false;
    const bool srgb = rhi::FormatInfo(target_.ColorFormat()).is_srgb;
    for (u32 channel = 0; channel < 4; ++channel) {
        const f32 value = static_cast<f32>((abgr >> (8 * channel)) & 0xFFu) / kByteMax;
        clear_color_[channel] = (srgb && channel < 3) ? ToLinear(value) : value;
    }
}

void UiSurfaceTarget::ClearPass(const PassExecuteContext& context, void* user) {
    const UiSurfaceTarget* self = static_cast<const UiSurfaceTarget*>(user);
    rhi::RenderingDesc rd;
    rd.color_count = 1;
    rd.color[0].texture = context.graph->ImportedTexture(self->clear_target_);
    for (int i = 0; i < 4; ++i) rd.color[0].clear_color[i] = self->clear_color_[i];
    context.list->BeginRendering(rd);
    context.list->EndRendering();
}

GraphResource UiSurfaceTarget::AddToGraph(RenderGraph* graph, UiRenderer* ui_renderer, const ui::DrawList* list) {
    ENGINE_ASSERT(graph != nullptr && ui_renderer != nullptr && list != nullptr && target_.Handle().IsValid(), "UiSurfaceTarget::AddToGraph: bad arguments or no texture");
    ENGINE_ASSERT(!has_resource_, "UiSurfaceTarget::AddToGraph: the target is already in this frame's graph (call BeginFrame once per frame, and paint once)");
    frame_resource_ = target_.ImportColor(graph);
    has_resource_ = true;
    clear_target_ = frame_resource_;
    graph->AddPass("ui surface clear", &UiSurfaceTarget::ClearPass, this).Write(frame_resource_, GraphUsage::ColorAttachment);
    ui_renderer->AddToGraph(graph, list, frame_resource_, target_.ColorFormat(), target_.Width(), target_.Height());
    painted_ = true;
    painted_handle_ = target_.Handle();
    ++paints_;
    return frame_resource_;
}

GraphResource UiSurfaceTarget::Sampled(RenderGraph* graph) {
    ENGINE_ASSERT(graph != nullptr && target_.Handle().IsValid(), "UiSurfaceTarget::Sampled: no graph or no texture");
    if (!has_resource_) {
        frame_resource_ = target_.ImportColor(graph);
        has_resource_ = true;
    }
    return frame_resource_;
}

} // namespace engine_placeholder::renderer
