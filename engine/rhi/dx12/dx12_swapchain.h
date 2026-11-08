#pragma once

#include "containers/unique_ptr.h"
#include "rhi/dx12/com_ref.h"
#include "rhi/dx12/d3d12_api.h"
#include "rhi/swapchain.h"
#include "types.h"

//! The Direct3D 12 swapchain (Phase 13 Day 169).

namespace engine_placeholder::rhi::dx12 {

class Dx12Device;

/// A [[rhi::Swapchain]] on a Win32 window: a flip-discard DXGI swap chain of three buffers with a frame-latency waitable object.
///
/// The buffers are linear (`B8G8R8A8_UNORM`, `R8G8B8A8_UNORM`, or `R16G16B16A16_FLOAT` for scRGB) and an sRGB format is a render-target
/// view of them, as flip-model swap chains require. Each buffer is a device texture (`CurrentTexture`), released before `ResizeBuffers`,
/// which fails while any reference remains. Present modes: `Fifo` is a sync interval of 1, `Immediate` an interval of 0 with tearing
/// when the system allows it, `Mailbox` falls back to `Fifo`. Fullscreen is the window's borderless mode, so DXGI's Alt+Enter is off.
///
/// Thread-safety: owner-thread-only, as every [[rhi::Swapchain]].
/// Ownership: owned by its device; destroyed with [[rhi::Device::DestroySwapchain]] before the device.
class Dx12Swapchain final : public Swapchain {
public:
    /// The number of buffers.
    static constexpr u32 kBuffers = 3;

    /// Creates the swap chain for the device's window.
    ///
    /// Param device: the device; its window is a Win32 window.
    /// Param desc: what is asked for.
    /// Returns: the swapchain, or null.
    /// Fails: null (logged) if DXGI refuses; a window with no area gets a swapchain whose `Acquire` reports `Minimized` until it has one.
    static UniquePtr<Dx12Swapchain> Create(Dx12Device* device, const SwapchainDesc& desc);

    /// Use `Create`.
    ///
    /// Param device: the device.
    explicit Dx12Swapchain(Dx12Device* device) : device_(device) {}
    /// Waits for the GPU, then releases the buffers, the swap chain and the latency object.
    ~Dx12Swapchain() override;
    Dx12Swapchain(const Dx12Swapchain&) = delete;
    Dx12Swapchain& operator=(const Dx12Swapchain&) = delete;

    SwapchainStatus Acquire() override;
    SwapchainStatus Present() override;
    SwapchainStatus Reconfigure(const SwapchainDesc& desc) override;
    PresentMode ActualPresentMode() const override { return actual_mode_; }
    ColorSpace GetColorSpace() const override { return color_space_; }
    SwapchainCaps Capabilities() const override { return caps_; }
    SwapchainStatus Resize(u32 width, u32 height) override;
    Format ImageFormat() const override { return format_; }
    u32 Width() const override { return width_; }
    u32 Height() const override { return height_; }
    TextureHandle CurrentTexture() const override { return acquired_ ? textures_[current_] : TextureHandle{}; }

    /// Returns whether the swap chain was created with tearing allowed (`DXGI_FEATURE_PRESENT_ALLOW_TEARING`), for tests.
    bool TearingAllowed() const { return tearing_; }
    /// Returns the DXGI format of the buffers, for tests.
    DXGI_FORMAT BufferFormat() const { return buffer_format_; }
    /// Returns the frame-latency waitable object, for tests. Ownership: borrowed.
    HANDLE LatencyWaitable() const { return waitable_; }

private:
    SwapchainStatus Build(u32 width, u32 height);
    void ReleaseImages();
    bool RegisterImages();
    void FillCaps();

    Dx12Device* device_;
    ComRef<IDXGISwapChain3> swapchain_;
    HANDLE waitable_ = nullptr;
    TextureHandle textures_[kBuffers];
    u32 current_ = 0;
    bool acquired_ = false;
    bool lost_ = false;
    bool minimized_ = false;
    bool tearing_ = false;
    bool scrgb_ = true;    // whether scRGB can be presented: learned from a float swap chain, assumed until there was one
    SwapchainDesc desc_;
    SwapchainCaps caps_;
    PresentMode actual_mode_ = PresentMode::Fifo;
    ColorSpace color_space_ = ColorSpace::SrgbNonlinear;
    Format format_ = Format::Undefined;
    DXGI_FORMAT buffer_format_ = DXGI_FORMAT_UNKNOWN;
    u32 width_ = 0;
    u32 height_ = 0;
    u64 window_serial_ = 0;
};

} // namespace engine_placeholder::rhi::dx12
