// The Direct3D 12 swapchain (Phase 13 Day 169); see dx12_swapchain.h.

#include "rhi/dx12/dx12_swapchain.h"

#include "log/assert.h"
#include "platform/window.h"
#include "rhi/dx12/dx12_device.h"
#include "rhi/dx12/dx12_result.h"
#include "rhi/rhi_log.h"

namespace engine_placeholder::rhi::dx12
{

namespace
{

// How long Acquire waits for the frame-latency object: a window Windows stopped composing (hidden, on a locked desktop)
// may not free a buffer, so the wait is bounded and the caller sees Minimized, as on Vulkan.
constexpr DWORD kLatencyWaitMs = 500;

// The buffer format for an RHI format: flip-model buffers are linear, an sRGB format is a view.
DXGI_FORMAT BufferFormatFor(Format f)
{
    switch (f)
    {
    case Format::BGRA8_SRGB:
    case Format::BGRA8_UNorm: return DXGI_FORMAT_B8G8R8A8_UNORM;
    case Format::RGBA8_SRGB:
    case Format::RGBA8_UNorm: return DXGI_FORMAT_R8G8B8A8_UNORM;
    case Format::RGBA16_Float: return DXGI_FORMAT_R16G16B16A16_FLOAT;
    default: return DXGI_FORMAT_UNKNOWN;
    }
}

}  // namespace

UniquePtr<Dx12Swapchain> Dx12Swapchain::Create(Dx12Device* device, const SwapchainDesc& desc)
{
    UniquePtr<Dx12Swapchain> swapchain = MakeUnique<Dx12Swapchain>(device);
    if (!swapchain)
    {
        return UniquePtr<Dx12Swapchain>();
    }
    swapchain->desc_ = desc;
    const os::Window* window = device->Window();
    const u32 width = desc.width != 0 ? desc.width : window->Width();
    const u32 height = desc.height != 0 ? desc.height : window->Height();
    const SwapchainStatus status = swapchain->Build(width, height);
    if (status == SwapchainStatus::Lost)
    {
        return UniquePtr<Dx12Swapchain>();
    }
    return swapchain;  // a window with no area yet: Acquire builds it when the window has one
}

Dx12Swapchain::~Dx12Swapchain()
{
    device_->WaitForGpu();
    ReleaseImages();
    if (swapchain_)
    {
        swapchain_->SetFullscreenState(FALSE,
                                       nullptr);  // a no-op for a windowed swap chain; DXGI wants it before release
        swapchain_.Reset();
    }
    if (waitable_ != nullptr)
    {
        CloseHandle(waitable_);
        waitable_ = nullptr;
    }
}

void Dx12Swapchain::ReleaseImages()
{
    for (TextureHandle& t : textures_)
    {
        if (t.IsValid())
        {
            device_->ReleaseSwapchainImage(t);
        }
        t = TextureHandle{};
    }
    acquired_ = false;
}

bool Dx12Swapchain::RegisterImages()
{
    for (u32 i = 0; i < kBuffers; ++i)
    {
        ComRef<ID3D12Resource> buffer;
        if (!device_->Check(swapchain_->GetBuffer(i, __uuidof(ID3D12Resource), buffer.PutVoid()),
                            "IDXGISwapChain::GetBuffer"))
        {
            return false;
        }
        textures_[i] = device_->RegisterSwapchainImage(static_cast<ComRef<ID3D12Resource>&&>(buffer), format_, width_,
                                                       height_);
        if (!textures_[i].IsValid())
        {
            return false;
        }
    }
    return true;
}

void Dx12Swapchain::FillCaps()
{
    SwapchainCaps c;
    c.present_mode_supported[static_cast<usize>(PresentMode::Fifo)] = true;
    c.present_mode_supported[static_cast<usize>(PresentMode::Mailbox)] = false;  // not offered: falls back to Fifo
    c.present_mode_supported[static_cast<usize>(PresentMode::Immediate)] = tearing_;
    const Format formats[] = {Format::BGRA8_SRGB, Format::BGRA8_UNorm, Format::RGBA8_SRGB, Format::RGBA8_UNorm};
    for (Format f : formats)
    {
        c.formats[c.format_count++] = f;
    }
    // CheckColorSpaceSupport answers for the buffers' current format: an 8-bit swap chain cannot present scRGB, a
    // 16-bit float one can on Windows 10 and later (the compositor maps it to the output). So the answer of a float
    // swap chain is kept, and until there was one scRGB counts as available; a refused grant says otherwise.
    if (buffer_format_ == DXGI_FORMAT_R16G16B16A16_FLOAT && swapchain_)
    {
        UINT support = 0;
        scrgb_ = SUCCEEDED(swapchain_->CheckColorSpaceSupport(DXGI_COLOR_SPACE_RGB_FULL_G10_NONE_P709, &support)) &&
                 (support & DXGI_SWAP_CHAIN_COLOR_SPACE_SUPPORT_FLAG_PRESENT) != 0;
    }
    c.hdr_scrgb = scrgb_;
    c.hdr10 = false;  // needs a 10-bit format the RHI does not have (rhi::HdrMode::Hdr10)
    caps_ = c;
}

SwapchainStatus Dx12Swapchain::Build(u32 width, u32 height)
{
    if (width == 0 || height == 0)
    {
        minimized_ = true;  // remembered: the next Acquire with an area builds it
        return SwapchainStatus::Minimized;
    }
    // The format: the request, else BGRA8 sRGB; scRGB HDR is a 16-bit float buffer.
    Format format = desc_.format != Format::Undefined ? desc_.format : Format::BGRA8_SRGB;
    const bool want_hdr = desc_.hdr == HdrMode::ScRgbLinear;
    if (desc_.hdr == HdrMode::Hdr10)
    {
        RhiLog(LogVerbosity::Warn,
               "D3D12: swapchain request not granted: HDR10 needs a 10-bit format the RHI does not have; using SDR");
    }
    if (want_hdr)
    {
        format = Format::RGBA16_Float;
    }
    else if (BufferFormatFor(format) == DXGI_FORMAT_UNKNOWN || format == Format::RGBA16_Float)
    {
        RhiLog(LogVerbosity::Warn,
               "D3D12: swapchain request not granted: format %s cannot be presented; using bgra8_srgb",
               FormatInfo(format).name);
        format = Format::BGRA8_SRGB;
    }
    const DXGI_FORMAT buffer_format = BufferFormatFor(format);
    IDXGIFactory6* factory = device_->Factory();
    BOOL tearing = FALSE;
    if (!swapchain_)
    {
        tearing_ = SUCCEEDED(
                       factory->CheckFeatureSupport(DXGI_FEATURE_PRESENT_ALLOW_TEARING, &tearing, sizeof(tearing))) &&
                   tearing == TRUE;
    }
    // Tearing cannot be added or removed by ResizeBuffers, so it is allowed from the start when the system has it.
    const UINT flags = DXGI_SWAP_CHAIN_FLAG_FRAME_LATENCY_WAITABLE_OBJECT |
                       (tearing_ ? DXGI_SWAP_CHAIN_FLAG_ALLOW_TEARING : 0);
    if (!swapchain_)
    {
        const os::NativeWindowInfo native = device_->Window()->GetNativeInfo();
        const HWND hwnd = static_cast<HWND>(native.surface);
        DXGI_SWAP_CHAIN_DESC1 sd{};
        sd.Width = width;
        sd.Height = height;
        sd.Format = buffer_format;
        sd.SampleDesc.Count = 1;
        sd.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
        sd.BufferCount = kBuffers;
        sd.Scaling = DXGI_SCALING_STRETCH;
        sd.SwapEffect = DXGI_SWAP_EFFECT_FLIP_DISCARD;
        sd.AlphaMode = DXGI_ALPHA_MODE_IGNORE;
        sd.Flags = flags;
        ComRef<IDXGISwapChain1> one;
        if (!device_->Check(factory->CreateSwapChainForHwnd(device_->Queue(), hwnd, &sd, nullptr, nullptr, one.Put()),
                            "CreateSwapChainForHwnd"))
        {
            lost_ = true;
            return SwapchainStatus::Lost;
        }
        if (!device_->Check(one->QueryInterface(__uuidof(IDXGISwapChain3), swapchain_.PutVoid()), "IDXGISwapChain3"))
        {
            lost_ = true;
            return SwapchainStatus::Lost;
        }
        factory->MakeWindowAssociation(
            hwnd, DXGI_MWA_NO_ALT_ENTER);  // fullscreen is the window's borderless mode (FULLSCREEN.md)
        // At most as many frames queued as the device keeps in flight; Acquire waits on the object before each frame.
        swapchain_->SetMaximumFrameLatency(device_->FramesInFlight());
        waitable_ = swapchain_->GetFrameLatencyWaitableObject();
    }
    else
    {
        // Every reference to the old buffers goes first: ResizeBuffers fails while one remains. Only the GPU is waited
        // for: this may run inside a frame (Acquire), whose destroyed resources its unsubmitted lists may still use.
        device_->WaitForGpu();
        ReleaseImages();
        const HRESULT hr = swapchain_->ResizeBuffers(kBuffers, width, height, buffer_format, flags);
        if (FAILED(hr))
        {
            device_->Check(hr, "IDXGISwapChain::ResizeBuffers");
            lost_ = true;
            return SwapchainStatus::Lost;
        }
    }
    format_ = format;
    buffer_format_ = buffer_format;
    width_ = width;
    height_ = height;
    FillCaps();  // with a float buffer, this learns whether scRGB is presentable
    // The color space: scRGB when asked for and the output can show it, else sRGB (falling back to an 8-bit buffer).
    color_space_ = ColorSpace::SrgbNonlinear;
    if (want_hdr && caps_.hdr_scrgb && SUCCEEDED(swapchain_->SetColorSpace1(DXGI_COLOR_SPACE_RGB_FULL_G10_NONE_P709)))
    {
        color_space_ = ColorSpace::ScRgbLinear;
    }
    else
    {
        if (want_hdr)
        {
            scrgb_ = false;
            caps_.hdr_scrgb = false;
        }
        if (want_hdr)
        {
            RhiLog(LogVerbosity::Warn,
                   "D3D12: swapchain request not granted: this output cannot present scRGB; using SDR");
            format_ = Format::BGRA8_SRGB;
            buffer_format_ = DXGI_FORMAT_B8G8R8A8_UNORM;
            if (!device_->Check(swapchain_->ResizeBuffers(kBuffers, width, height, buffer_format_, flags),
                                "IDXGISwapChain::ResizeBuffers"))
            {
                lost_ = true;
                return SwapchainStatus::Lost;
            }
        }
        swapchain_->SetColorSpace1(DXGI_COLOR_SPACE_RGB_FULL_G22_NONE_P709);
    }
    PresentMode mode = desc_.present_mode;
    if (mode == PresentMode::Mailbox)
    {
        RhiLog(LogVerbosity::Info, "D3D12: the Mailbox present mode is not offered; using Fifo");
        mode = PresentMode::Fifo;
    }
    else if (mode == PresentMode::Immediate && !tearing_)
    {
        RhiLog(LogVerbosity::Info, "D3D12: Immediate needs tearing, which this system does not allow; using Fifo");
        mode = PresentMode::Fifo;
    }
    actual_mode_ = mode;
    if (!RegisterImages())
    {
        lost_ = true;
        return SwapchainStatus::Lost;
    }
    minimized_ = false;
    window_serial_ = device_->Window()->ResizeSerial();
    return SwapchainStatus::Ok;
}

SwapchainStatus Dx12Swapchain::Resize(u32 width, u32 height)
{
    if (lost_ || device_->DeviceLost())
    {
        return SwapchainStatus::Lost;
    }
    if (width == 0 || height == 0)
    {
        minimized_ = true;
        return SwapchainStatus::Minimized;
    }
    SwapchainDesc desc = desc_;
    desc.width = width;
    desc.height = height;
    return Reconfigure(desc);
}

SwapchainStatus Dx12Swapchain::Reconfigure(const SwapchainDesc& desc)
{
    ENGINE_ASSERT(!acquired_, "Swapchain::Reconfigure: the previous image was not presented");
    if (lost_ || device_->DeviceLost())
    {
        return SwapchainStatus::Lost;
    }
    desc_ = desc;  // remembered even when the window has no area now: the rebuild on return uses it
    const os::Window* window = device_->Window();
    return Build(desc.width != 0 ? desc.width : window->Width(), desc.height != 0 ? desc.height : window->Height());
}

SwapchainStatus Dx12Swapchain::Acquire()
{
    if (lost_ || device_->DeviceLost())
    {
        return SwapchainStatus::Lost;
    }
    ENGINE_ASSERT(!acquired_, "Swapchain::Acquire: the previous image was not presented");
    const os::Window* window = device_->Window();
    bool recreated = false;
    if (window->IsMinimized())
    {
        minimized_ = minimized_ || !swapchain_;
        return SwapchainStatus::Minimized;  // nothing to draw to; no wait, so a minimized window does not spin the GPU
                                            // or block
    }
    if (minimized_ || !swapchain_ || window->ResizeSerial() != window_serial_)
    {
        const SwapchainStatus status = Build(window->Width(), window->Height());
        if (status != SwapchainStatus::Ok)
        {
            return status;
        }
        recreated = true;
    }
    const DWORD wait = WaitForSingleObjectEx(waitable_, kLatencyWaitMs,
                                             FALSE);  // not alertable: an APC would end it early
    if (wait == WAIT_TIMEOUT)
    {
        return SwapchainStatus::Minimized;  // Windows is not showing the window: skip the frame
    }
    if (wait != WAIT_OBJECT_0)
    {
        RhiLog(LogVerbosity::Error, "D3D12: waiting for the swap chain's frame-latency object failed (%lu)",
               static_cast<unsigned long>(GetLastError()));
        lost_ = true;
        return SwapchainStatus::Lost;
    }
    current_ = swapchain_->GetCurrentBackBufferIndex();
    acquired_ = true;
    return recreated ? SwapchainStatus::Recreated : SwapchainStatus::Ok;
}

SwapchainStatus Dx12Swapchain::Present()
{
    ENGINE_ASSERT(acquired_, "Swapchain::Present: no image has been acquired");
    acquired_ = false;
    if (lost_ || device_->DeviceLost() || device_->CountPresent())
    {
        return SwapchainStatus::Lost;
    }
    const bool immediate = actual_mode_ == PresentMode::Immediate;
    const HRESULT hr = device_->PresentSwapchain(swapchain_.Get(), immediate ? 0 : 1,
                                                 immediate ? DXGI_PRESENT_ALLOW_TEARING : 0);
    if (hr == S_OK || hr == DXGI_STATUS_OCCLUDED)
    {
        return SwapchainStatus::Ok;  // occluded: nothing was shown, and the next frame goes on as usual
    }
    if (IsDeviceLostResult(hr))
    {
        return SwapchainStatus::Lost;  // reported by the device (PresentSwapchain); recovery is a device reset
    }
    RhiLog(LogVerbosity::Error, "D3D12: IDXGISwapChain::Present failed (%s)", HresultName(hr));
    lost_ = true;
    return SwapchainStatus::Lost;
}

}  // namespace engine_placeholder::rhi::dx12
