#include "renderer/scene_frame.h"

#include "lang/scalar.h"
#include "cstr/cstr.h"

#include "io/path.h"
#include "rhi/rhi_log.h"

namespace engine_placeholder::renderer
{

SceneFrame::SceneFrame() = default;
SceneFrame::~SceneFrame()
{
    Destroy();
}

void SceneFrame::Destroy()
{
    if (device_ != nullptr)
    {
        device_->WaitIdle();  // frames may still be in flight
    }
    // Reverse order of creation: the renderer (and its pooled textures) first, then the passes, then what they borrow.
    renderer_.Reset();
    text_.Destroy();
    font_.Close();
    overlay_on_ = false;
    labels_.Clear();
    label_text_.Clear();
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

bool SceneFrame::Create(const SceneFrameSetup& setup)
{
    Destroy();
    char error[256];
    if (!CheckGBufferFormats(setup.device, error, sizeof(error)))
    {
        rhi::RhiLogV(LogVerbosity::Error, "scene frame: %s", error);
        return false;
    }
    device_ = setup.device;
    resources_ = setup.resources;
    gbuffer_layer_ = setup.gbuffer_layer;
    exposure_ = setup.exposure;
    MaterialSystemDesc md;
    md.resources = setup.resources;
    md.prewarm_pipelines = setup.prewarm_pipelines;
    materials_ = MakeUnique<MaterialSystem>(setup.device, md);
    frame_ = MakeUnique<FrameResources>(setup.device, setup.max_objects, setup.max_lights);
    if (!frame_->RegisterWith(setup.resources) || !materials_->Valid() || !frame_->Valid())
    {
        rhi::RhiLogV(LogVerbosity::Error, "scene frame: could not create the material system or the frame buffers");
        Destroy();
        return false;
    }
    templates_ = CreateGBufferTemplates(materials_.Get());
    gbuffer_ = MakeUnique<GBufferPass>(setup.device, materials_.Get(), frame_.Get());
    lighting_ = MakeUnique<LightingPass>(setup.device, materials_.Get(), frame_.Get());
    tonemap_ = MakeUnique<TonemapPass>(setup.device);
    debug_view_ = MakeUnique<GBufferDebugView>(setup.device);
    const bool registered = lighting_->RegisterWith(setup.resources) && tonemap_->RegisterWith(setup.resources) &&
                            debug_view_->RegisterWith(setup.resources);
    if (!registered || !templates_.plain.IsValid() || !templates_.colored.IsValid() || !lighting_->Valid() ||
        !tonemap_->Valid() || !debug_view_->Valid())
    {
        rhi::RhiLogV(LogVerbosity::Error, "scene frame: could not create the render passes");
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
    rd.before_present = setup.before_present;
    rd.before_present_user = setup.before_present_user;
    rd.build = &SceneFrame::Build;
    rd.build_user = this;
    renderer_ = MakeUnique<Renderer>(rd);
    if (setup.font_path != nullptr)
    {
        // The overlay is optional: a missing font or a device without the text shaders leaves the frame without it,
        // with the reason logged.
        text::FontOptions fo;
        if (font_library_.IsInitialized() || font_library_.Initialize().Ok())
        {
            const text::Status opened = text::Font::OpenFile(
                font_library_, io::Path(StringView(setup.font_path, CStrLength(setup.font_path))), fo, &font_);
            if (opened.Ok() &&
                !text_.Create(setup.device, &atlas_, TextRendererDesc{rhi::Format::RGBA8_SRGB, 4096}, setup.resources))
            {
                font_.Close();
            }
            else if (!opened.Ok())
            {
                rhi::RhiLogV(LogVerbosity::Warn, "scene frame: the overlay font '%s' did not open: %s", setup.font_path,
                             opened.message);
            }
        }
    }
    return true;
}

bool SceneFrame::OpenLabelFont(ByteSpan data)
{
    if (device_ == nullptr)
    {
        return false;
    }
    if (font_.IsOpen())
    {
        return text_.Live();
    }
    if (!font_library_.IsInitialized() && !font_library_.Initialize().Ok())
    {
        return false;
    }
    const text::Status opened = text::Font::Open(font_library_, data, text::FontOptions{}, &font_);
    if (!opened.Ok())
    {
        rhi::RhiLogV(LogVerbosity::Warn, "scene frame: the label font did not open: %s", opened.message);
        return false;
    }
    if (!text_.Create(device_, &atlas_, TextRendererDesc{rhi::Format::RGBA8_SRGB, 8192}, resources_))
    {
        font_.Close();
        return false;
    }
    return true;
}

void SceneFrame::BeginLabels()
{
    labels_.Clear();
    label_text_.Clear();
}

bool SceneFrame::AddLabel(StringView utf8, f32 x, f32 y, u32 pixel_size, u32 color, u32 anchor)
{
    if (!LabelsAvailable() || labels_.Size() >= 1024)
    {
        return false;
    }
    Label label;
    label.x = x;
    label.y = y;
    label.pixel_size = pixel_size;
    label.color = color;
    label.anchor = anchor;
    label.text_begin = static_cast<u32>(label_text_.Size());
    label.text_length = static_cast<u32>(utf8.Length());
    for (usize i = 0; i < utf8.Length(); ++i)
    {
        label_text_.Push(utf8[i]);
    }
    labels_.Push(label);
    return true;
}

bool SceneFrame::SetOverlay(bool on, StringView greeting, StringView status)
{
    if (!font_.IsOpen())
    {
        return false;
    }
    overlay_on_ = on;
    greeting_.Clear();
    for (usize i = 0; i < greeting.Length(); ++i)
    {
        greeting_.Push(greeting[i]);
    }
    status_.Clear();
    for (usize i = 0; i < status.Length(); ++i)
    {
        status_.Push(status[i]);
    }
    return true;
}

void SceneFrame::Rebind(rhi::Device* device, rhi::Swapchain* swapchain)
{
    if (!renderer_)
    {
        return;
    }
    device_ = device;
    renderer_->Rebind(device, swapchain);
}

FrameStatus SceneFrame::Render(const RenderScene* scene, const Camera* camera, const FrameTarget* offscreen)
{
    return renderer_->RenderFrame(scene, camera, offscreen);
}

bool SceneFrame::Build(const FrameContext& context, void* user)
{
    SceneFrame* self = static_cast<SceneFrame*>(user);
    if (context.scene == nullptr || context.camera == nullptr)
    {
        return false;
    }
    self->materials_->BeginFrame(context.frame_index);
    self->materials_->FlushMaterials();
    self->frame_->BeginFrame(context.frame_index);
    // The scene goes to the compose hook's target (a viewport's texture) when there is one, else to the frame's target.
    GraphResource scene_target = context.target;
    u32 scene_width = context.width;
    u32 scene_height = context.height;
    rhi::Format scene_format = context.format;
    if (self->compose_.scene_target != nullptr &&
        !self->compose_.scene_target(context, self->compose_.user, &scene_target, &scene_width, &scene_height,
                                     &scene_format))
    {
        return false;
    }
    const FrameData data = MakeFrameData(*context.camera, scene_width, scene_height, self->time_, self->delta_,
                                         context.frame_index);
    self->frame_->WriteFrameData(data);
    self->gbuffer_ok_ = self->gbuffer_->Prepare(*context.scene,
                                                Frustum::FromViewProjection(context.camera->ViewProjection()));
    if (!self->gbuffer_ok_)
    {
        rhi::RhiLogV(LogVerbosity::Warn, "scene frame: some objects were not drawn: %s", self->gbuffer_->LastError());
    }
    self->gbuffer_stats_ = self->gbuffer_->Stats();
    self->light_summary_ = self->lighting_->Prepare(*context.scene, data);
    self->frame_->Flush();

    const GBufferTargets targets = self->gbuffer_->AddToGraph(context.graph, scene_width, scene_height);
    if (self->gbuffer_layer_ >= 0)
    {
        self->debug_view_->SetInverseViewProjection(data.inverse_view_projection);
        self->debug_view_->AddToGraph(context.graph, targets, static_cast<GBufferLayer>(self->gbuffer_layer_),
                                      scene_target, scene_format, context.camera->near_plane,
                                      context.camera->far_plane);
    }
    else
    {
        const GraphResource accum = self->lighting_->AddToGraph(context.graph, targets, scene_width, scene_height);
        self->tonemap_->AddToGraph(context.graph, accum, scene_target, scene_format, self->exposure_);
    }
    const bool has_labels = self->labels_.Size() != 0;
    if ((self->overlay_on_ || has_labels) && self->font_.IsOpen() && self->text_.Live())
    {
        // After the passes that wrote the target (the tonemap or the debug view), over their picture.
        // Sizes and margins in logical pixels, times the display scale (TextStyle sizes are device pixels).
        const f32 scale = self->overlay_scale_;
        auto px = [scale](f32 logical) { return static_cast<u32>(LRound(logical * scale)); };
        text::TextStyle big;
        big.font = &self->font_;
        big.pixel_size = px(32);
        big.color = 0xFFFFFFFFu;
        text::TextStyle small = big;
        small.pixel_size = px(14);
        small.color = 0xFFC8C8C8u;
        self->text_.BeginFrame();
        if (self->overlay_on_)
        {
            self->text_.Draw(StringView(self->greeting_.Data(), self->greeting_.Size()), big, text::LayoutOptions{},
                             static_cast<f32>(px(16)), static_cast<f32>(px(12)));
            self->text_.Draw(StringView(self->status_.Data(), self->status_.Size()), small, text::LayoutOptions{},
                             static_cast<f32>(px(16)), static_cast<f32>(px(52)));
        }
        for (usize i = 0; i < self->labels_.Size(); ++i)
        {
            const Label& label = self->labels_[i];
            text::TextStyle style = big;
            style.pixel_size = label.pixel_size;
            style.color = label.color;
            const StringView text(self->label_text_.Data() + label.text_begin, label.text_length);
            if (!text::Layout(text, style, text::LayoutOptions{}, &self->label_layout_).Ok())
            {
                continue;
            }
            for (usize g = 0; g < self->label_layout_.glyphs.Size(); ++g)
            {
                if (self->label_layout_.glyphs[g].glyph == 0)
                {
                    ++self->label_missing_;
                }
            }
            const f32 width = self->label_layout_.width;
            const f32 left = label.anchor == 1 ? label.x : label.anchor == 2 ? label.x - width : label.x - width * 0.5f;
            self->text_.Add(self->label_layout_, style, Floor(left + 0.5f),
                            Floor(label.y - self->label_layout_.height * 0.5f + 0.5f));
        }
        self->text_.AddToGraph(context.graph, context.target, context.format, context.width, context.height);
    }
    if (self->compose_.after_scene != nullptr)
    {
        self->compose_.after_scene(context, self->compose_.user);
    }
    return true;
}

}  // namespace engine_placeholder::renderer
