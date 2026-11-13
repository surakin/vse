#pragma once

// The Metal swapchain (Phase 21 Day 305): the Cocoa window's CAMetalLayer, nextDrawable as Acquire and presentDrawable:
// as Present. Objective-C++.

#include "rhi/swapchain.h"

#import <QuartzCore/CAMetalLayer.h>

namespace engine_placeholder::os
{
class Window;
}

namespace engine_placeholder::rhi::metal
{

class MetalDevice;

// How a requested present mode lands on a CAMetalLayer: Fifo is display sync, Immediate is none, Mailbox (which macOS
// does not have) falls back to Fifo. Pure.
PresentMode MetalPresentModeFor(PresentMode requested);

// The swapchain over one window's layer. Its one texture handle follows the drawable of the current frame: the drawable
// is taken by Acquire and given up by Present, never kept past its frame.
//
// Thread-safety: not thread-safe; Acquire, Present and Reconfigure belong to the submission thread, which on macOS is
// the main thread (the window's).
class MetalSwapchain final : public Swapchain
{
public:
    MetalSwapchain(MetalDevice* device, const os::Window* window);
    ~MetalSwapchain() override;

    // Configures the layer for `desc`; false (logged) when the window has no Metal layer.
    bool Initialize(const SwapchainDesc& desc);

    SwapchainStatus Acquire() override;
    SwapchainStatus Present() override;
    SwapchainStatus Reconfigure(const SwapchainDesc& desc) override;
    PresentMode ActualPresentMode() const override { return present_mode_; }
    ColorSpace GetColorSpace() const override { return color_space_; }
    SwapchainCaps Capabilities() const override;
    SwapchainStatus Resize(u32 width, u32 height) override;
    Format ImageFormat() const override { return format_; }
    u32 Width() const override { return width_; }
    u32 Height() const override { return height_; }
    TextureHandle CurrentTexture() const override { return acquired_ ? texture_ : TextureHandle{}; }

    // The layer (tests).
    CAMetalLayer* Layer() const { return layer_; }
    // How long nextDrawable waits before Acquire reports Minimized: the layer's own timeout (one second, which Metal
    // does not let a program shorten; the RHI's contract notes it).
    static constexpr u32 kDrawableTimeoutMs = 1000;

private:
    // Applies the format, color space, present mode and size to the layer; the last request is kept for a window with
    // no area.
    SwapchainStatus Apply(const SwapchainDesc& desc);
    void SizeFromWindow();
    bool EdrPossible() const;

    MetalDevice* device_;
    const os::Window* window_;
    CAMetalLayer* layer_ = nil;
    id<CAMetalDrawable> drawable_ = nil;  // the current frame's, between Acquire and Present
    TextureHandle texture_;
    bool acquired_ = false;
    SwapchainDesc desc_;
    PresentMode present_mode_ = PresentMode::Fifo;
    ColorSpace color_space_ = ColorSpace::SrgbNonlinear;
    Format format_ = Format::BGRA8_SRGB;
    u32 width_ = 0;
    u32 height_ = 0;
    u64 resize_serial_ = 0;
    bool pending_ = false;       // a Reconfigure that waits for the window to have an area
    bool edr_possible_ = false;  // the screen's EDR, read at the last Apply (Capabilities answers it)
};

}  // namespace engine_placeholder::rhi::metal
