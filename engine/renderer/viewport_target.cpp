#include "renderer/viewport_target.h"

#include "cstr/cstr.h"

#include "log/assert.h"
#include "renderer/renderer_log.h"

namespace engine_placeholder::renderer {

bool ViewportTarget::Make(rhi::Device* device, u32 width, u32 height, rhi::TextureHandle* color, rhi::TextureHandle* depth) {
    if (width == 0 || height == 0) {
        RenderLog(LogVerbosity::Error, "ViewportTarget '%s': a size of %ux%u is empty", name_, width, height);
        return false;
    }
    const rhi::DeviceCaps& caps = device->Caps();
    if (width > caps.max_texture_dimension_2d || height > caps.max_texture_dimension_2d) {
        RenderLog(LogVerbosity::Error, "ViewportTarget '%s': %ux%u exceeds the device limit of %u", name_, width, height, caps.max_texture_dimension_2d);
        return false;
    }
    rhi::TextureDesc td;
    td.width = width;
    td.height = height;
    td.format = desc_.color_format;
    td.usage = rhi::TextureUsage::ColorAttachment | rhi::TextureUsage::Sampled | rhi::TextureUsage::CopySrc;
    td.debug_name = name_;
    const char* why = td.Validate();
    if (why != nullptr) {
        RenderLog(LogVerbosity::Error, "ViewportTarget '%s': %s", name_, why);
        return false;
    }
    if (!device->FormatSupport(td.format, td.usage)) {
        RenderLog(LogVerbosity::Error, "ViewportTarget '%s': the device cannot use the colour format as a target that is also sampled", name_);
        return false;
    }
    *color = device->CreateTexture(td);
    if (!color->IsValid()) {
        RenderLog(LogVerbosity::Error, "ViewportTarget '%s': the colour texture could not be made", name_);
        return false;
    }
    *depth = rhi::TextureHandle();
    if (desc_.with_depth) {
        rhi::TextureDesc dd = td;
        dd.format = desc_.depth_format;
        dd.usage = rhi::TextureUsage::DepthAttachment | rhi::TextureUsage::Sampled;
        *depth = device->CreateTexture(dd);
        if (!depth->IsValid()) {
            RenderLog(LogVerbosity::Error, "ViewportTarget '%s': the depth texture could not be made", name_);
            device->DestroyTexture(*color);
            *color = rhi::TextureHandle();
            return false;
        }
    }
    return true;
}

bool ViewportTarget::Create(rhi::Device* device, const ViewportTargetDesc& desc, u32 width, u32 height, DeviceResources* resources, const char* debug_name) {
    ENGINE_ASSERT(device != nullptr && !color_.IsValid() && device_ == nullptr, "ViewportTarget::Create: no device, or the object already exists");
    desc_ = desc;
    if (debug_name != nullptr) {
        CStrCopyN(name_, debug_name, sizeof(name_) - 1);
        name_[sizeof(name_) - 1] = '\0';
    }
    rhi::TextureHandle color;
    rhi::TextureHandle depth;
    if (!Make(device, width, height, &color, &depth)) return false;
    // The registry asks `Live()` when it registers an owner, so the members are set first.
    device_ = device;
    width_ = width;
    height_ = height;
    color_ = color;
    depth_ = depth;
    if (!registration_.Attach(resources, this, tiers::kTextures)) {
        RenderLog(LogVerbosity::Error, "ViewportTarget '%s': the registry refused it", name_);
        device->DestroyTexture(color);
        if (depth.IsValid()) device->DestroyTexture(depth);
        device_ = nullptr;
        width_ = 0;
        height_ = 0;
        color_ = {};
        depth_ = {};
        return false;
    }
    color_state_ = rhi::ResourceState::Undefined;
    depth_state_ = rhi::ResourceState::Undefined;
    return true;
}

void ViewportTarget::Destroy() {
    registration_.Reset();
    if (device_ != nullptr) {
        if (color_.IsValid()) device_->DestroyTexture(color_);
        if (depth_.IsValid()) device_->DestroyTexture(depth_);
    }
    color_ = {};
    depth_ = {};
    device_ = nullptr;
    width_ = 0;
    height_ = 0;
}

bool ViewportTarget::Resize(u32 width, u32 height) {
    ENGINE_ASSERT(width_ != 0, "ViewportTarget::Resize: the object does not exist");
    if (width == 0 || height == 0) {
        RenderLog(LogVerbosity::Error, "ViewportTarget '%s': a size of %ux%u is empty", name_, width, height);
        return false;
    }
    if (device_ == nullptr) {
        // Between a release and the recreate: only the size to recreate at changes.
        if (width != width_ || height != height_) ++resizes_;
        width_ = width;
        height_ = height;
        return true;
    }
    if (width == width_ && height == height_ && color_.IsValid()) return true;
    rhi::TextureHandle color;
    rhi::TextureHandle depth;
    if (!Make(device_, width, height, &color, &depth)) return false; // the old textures stay
    const rhi::TextureHandle old_color = color_;
    const rhi::TextureHandle old_depth = depth_;
    color_ = color;
    depth_ = depth;
    width_ = width;
    height_ = height;
    color_state_ = rhi::ResourceState::Undefined;
    depth_state_ = rhi::ResourceState::Undefined;
    ++resizes_;
    // The device holds a destroyed texture until the frames in flight that may use it have finished.
    if (old_color.IsValid()) device_->DestroyTexture(old_color);
    if (old_depth.IsValid()) device_->DestroyTexture(old_depth);
    return true;
}

GraphResource ViewportTarget::ImportColor(RenderGraph* graph) {
    ENGINE_ASSERT(graph != nullptr && color_.IsValid(), "ViewportTarget::ImportColor: no graph, or the target holds no texture");
    const rhi::ResourceState initial = color_state_;
    color_state_ = rhi::ResourceState::ShaderRead;
    return graph->ImportTexture(name_, color_, initial, rhi::ResourceState::ShaderRead, false);
}

GraphResource ViewportTarget::ImportDepth(RenderGraph* graph) {
    ENGINE_ASSERT(graph != nullptr && depth_.IsValid(), "ViewportTarget::ImportDepth: no graph, or the target has no depth texture");
    const rhi::ResourceState initial = depth_state_;
    depth_state_ = rhi::ResourceState::DepthAttachment;
    return graph->ImportTexture(name_, depth_, initial, rhi::ResourceState::DepthAttachment, false);
}

FrameTarget ViewportTarget::AsFrameTarget() {
    ENGINE_ASSERT(color_.IsValid(), "ViewportTarget::AsFrameTarget: the target holds no texture");
    FrameTarget target;
    target.texture = color_;
    target.width = width_;
    target.height = height_;
    target.format = desc_.color_format;
    target.initial = color_state_;
    target.final_state = rhi::ResourceState::ShaderRead;
    color_state_ = rhi::ResourceState::ShaderRead;
    return target;
}

void ViewportTarget::Release() {
    if (device_ != nullptr) {
        if (color_.IsValid()) device_->DestroyTexture(color_);
        if (depth_.IsValid()) device_->DestroyTexture(depth_);
    }
    color_ = {};
    depth_ = {};
    device_ = nullptr; // the device is going away; Recreate gets the new one
}

bool ViewportTarget::Recreate(rhi::Device* device) {
    ENGINE_ASSERT(device != nullptr, "ViewportTarget::Recreate: no device");
    rhi::TextureHandle color;
    rhi::TextureHandle depth;
    if (!Make(device, width_, height_, &color, &depth)) return false;
    device_ = device;
    color_ = color;
    depth_ = depth;
    color_state_ = rhi::ResourceState::Undefined;
    depth_state_ = rhi::ResourceState::Undefined;
    return true;
}

} // namespace engine_placeholder::renderer
