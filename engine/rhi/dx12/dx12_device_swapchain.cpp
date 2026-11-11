// Dx12Device: the swapchain, its images as device textures, and presentation (Phase 13 Day 169).

#include "log/assert.h"
#include "platform/window.h"
#include "rhi/dx12/dx12_device.h"
#include "rhi/dx12/dx12_format.h"
#include "rhi/dx12/dx12_result.h"
#include "rhi/dx12/dx12_swapchain.h"
#include "rhi/rhi_log.h"
#include "sync/atomic.h"
#include "sync/mutex.h"

namespace engine_placeholder::rhi::dx12
{

Swapchain* Dx12Device::CreateSwapchain(const SwapchainDesc& desc)
{
    if (window_ == nullptr || window_->GetNativeInfo().kind != os::NativeWindowKind::Win32)
    {
        RhiLog(LogVerbosity::Error, "CreateSwapchain: the device was created without a Win32 window");
        return nullptr;
    }
    if (swapchain_)
    {
        RhiLog(LogVerbosity::Error, "CreateSwapchain: the device already has a swapchain");
        return nullptr;
    }
    swapchain_ = Dx12Swapchain::Create(this, desc);
    swapchain_live_.Store(swapchain_ ? 1 : 0, MemoryOrder::Release);
    return swapchain_.Get();
}

void Dx12Device::DestroySwapchain(Swapchain* swapchain)
{
    if (swapchain == nullptr)
    {
        return;
    }
    ENGINE_ASSERT(swapchain == swapchain_.Get(), "DestroySwapchain: not this device's swapchain");
    swapchain_.Reset();
    swapchain_live_.Store(0, MemoryOrder::Release);
}

bool Dx12Device::RecreateSurface(const os::Window* window)
{
    ENGINE_ASSERT(!swapchain_, "RecreateSurface: destroy the swapchain first");
    ENGINE_ASSERT(!frame_open_.Load(MemoryOrder::Acquire), "RecreateSurface: called between BeginFrame and EndFrame");
    if (window == nullptr || window->GetNativeInfo().kind != os::NativeWindowKind::Win32)
    {
        RhiLog(LogVerbosity::Error, "RecreateSurface: no Win32 window");
        return false;
    }
    // DXGI has no surface object: a swap chain is made for a window, so the next CreateSwapchain uses this one. Any
    // D3D12 adapter can present to a window of its desktop.
    WaitIdle();
    window_ = window;
    return true;
}

TextureHandle Dx12Device::RegisterSwapchainImage(ComRef<ID3D12Resource> buffer, Format format, u32 width, u32 height)
{
    TextureRes res;
    res.desc.width = width;
    res.desc.height = height;
    res.desc.format = format;
    res.desc.usage = TextureUsage::ColorAttachment |
                     TextureUsage::CopySrc;  // a back buffer is also a copy source (screenshots)
    res.resource_format = buffer->GetDesc().Format;
    res.state = D3D12_RESOURCE_STATE_PRESENT;
    res.external = true;
    LockGuard<Mutex> lock(resource_mutex_);
    if (!rtv_heap_.Allocate(&res.rtv))
    {
        return {};
    }
    // The sRGB format is the view; the buffer itself is linear (flip-model swap chains require it).
    D3D12_RENDER_TARGET_VIEW_DESC v{};
    v.Format = ToDxgi(format);
    v.ViewDimension = D3D12_RTV_DIMENSION_TEXTURE2D;
    device_->CreateRenderTargetView(buffer.Get(), &v, res.rtv);
    res.resource = static_cast<ComRef<ID3D12Resource>&&>(buffer);
    const TextureHandle handle = textures_.Allocate(static_cast<TextureRes&&>(res));
    if (!handle.IsValid())
    {
        rtv_heap_.Free(res.rtv);
    }
    return handle;
}

void Dx12Device::ReleaseSwapchainImage(TextureHandle texture)
{
    // The swapchain waited for the GPU first: the image is freed at once (ResizeBuffers needs every reference gone),
    // not retired.
    LockGuard<Mutex> lock(resource_mutex_);
    TextureRes* t = textures_.Get(texture);
    if (t == nullptr)
    {
        return;
    }
    ReleaseTextureViews(*t);
    t->resource.Reset();
    textures_.Free(texture);
}

bool Dx12Device::CountPresent()
{
    const u32 n = present_count_.FetchAdd(1, MemoryOrder::Relaxed) + 1;
    if (lose_device_at_present_ != 0 && n == lose_device_at_present_)
    {
        InjectLoss("present", n);
    }
    return device_lost_.Load(MemoryOrder::Relaxed);
}

HRESULT Dx12Device::PresentSwapchain(IDXGISwapChain3* swapchain, UINT interval, UINT flags)
{
    LockGuard<Mutex> lock(submit_mutex_);  // ordered with the submissions on the queue
    const HRESULT hr = swapchain->Present(interval, flags);
    if (IsDeviceLostResult(hr))
    {
        NoteRemoved("IDXGISwapChain::Present");
        return hr;
    }
    // The present is work on the queue that reads the back buffer: a fence signal after it makes WaitIdle (and so
    // ResizeBuffers and the swap chain's release) wait for it too.
    SignalLocked();
    return hr;
}

}  // namespace engine_placeholder::rhi::dx12
