#pragma once

#include "image_compare.h"
#include "rhi/rhi.h"

// An offscreen RGBA8 color target with readback, for rendering tests that need no window or swapchain.
namespace gpu_test
{

class RenderTarget
{
public:
    RenderTarget(engine_placeholder::rhi::Device* device, engine_placeholder::u32 width, engine_placeholder::u32 height)
        : device_(device), width_(width), height_(height)
    {
        using namespace engine_placeholder::rhi;
        TextureDesc td;
        td.width = width;
        td.height = height;
        td.format = Format::RGBA8_UNorm;
        td.usage = TextureUsage::ColorAttachment | TextureUsage::CopySrc;
        td.debug_name = "render target";
        texture_ = device->CreateTexture(td);
    }
    ~RenderTarget()
    {
        if (texture_.IsValid())
        {
            device_->DestroyTexture(texture_);
        }
    }
    RenderTarget(const RenderTarget&) = delete;
    RenderTarget& operator=(const RenderTarget&) = delete;

    bool Valid() const { return texture_.IsValid(); }
    engine_placeholder::rhi::TextureHandle Texture() const { return texture_; }
    engine_placeholder::u32 Width() const { return width_; }
    engine_placeholder::u32 Height() const { return height_; }

    // Reads the target back; it must be in `ColorAttachment` (where rendering leaves it).
    bool Readback(Image* out) const
    {
        out->width = width_;
        out->height = height_;
        out->rgba.Assign(static_cast<engine_placeholder::usize>(width_) * height_ * 4, 0);
        return device_->ReadbackTexture(texture_, 0, 0, out->rgba.Data(), out->rgba.Size(),
                                        engine_placeholder::rhi::ResourceState::ColorAttachment);
    }

private:
    engine_placeholder::rhi::Device* device_;
    engine_placeholder::u32 width_;
    engine_placeholder::u32 height_;
    engine_placeholder::rhi::TextureHandle texture_;
};

}  // namespace gpu_test
