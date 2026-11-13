// MetalDevice: the swapchain and presentation (Phase 21 Day 305).

#include "lang/utility.h"
#include "sync/mutex.h"

#include "log/assert.h"
#include "platform/window.h"
#include "rhi/metal/metal_device.h"
#include "rhi/metal/metal_swapchain.h"
#include "rhi/rhi_log.h"

#import <Foundation/Foundation.h>

namespace engine_placeholder::rhi::metal
{

Swapchain* MetalDevice::CreateSwapchain(const SwapchainDesc& desc)
{
    if (window_ == nullptr || window_->GetNativeInfo().kind != os::NativeWindowKind::Cocoa)
    {
        RhiLog(LogVerbosity::Error, "CreateSwapchain: the device was created without a Cocoa window");
        return nullptr;
    }
    if (swapchain_)
    {
        RhiLog(LogVerbosity::Error, "CreateSwapchain: the device already has a swapchain");
        return nullptr;
    }
    UniquePtr<MetalSwapchain> made = MakeUnique<MetalSwapchain>(this, window_);
    if (!made || !made->Initialize(desc))
    {
        return nullptr;
    }
    swapchain_ = Move(made);
    swapchain_live_.Store(1, MemoryOrder::Release);
    return swapchain_.Get();
}

void MetalDevice::DestroySwapchain(Swapchain* swapchain)
{
    if (swapchain == nullptr)
    {
        return;
    }
    ENGINE_ASSERT(swapchain == swapchain_.Get(), "DestroySwapchain: not this device's swapchain");
    swapchain_.Reset();
    swapchain_live_.Store(0, MemoryOrder::Release);
}

bool MetalDevice::RecreateSurface(const os::Window* window)
{
    ENGINE_ASSERT(!swapchain_, "RecreateSurface: destroy the swapchain first");
    ENGINE_ASSERT(!frame_open_.Load(MemoryOrder::Acquire), "RecreateSurface: called between BeginFrame and EndFrame");
    if (window == nullptr || window->GetNativeInfo().kind != os::NativeWindowKind::Cocoa)
    {
        RhiLog(LogVerbosity::Error, "RecreateSurface: no Cocoa window");
        return false;
    }
    // A CAMetalLayer belongs to its window's view and takes any Metal device: the next CreateSwapchain configures the
    // window's layer as it is then.
    WaitIdle();
    window_ = window;
    return true;
}

TextureHandle MetalDevice::RegisterSwapchainTexture()
{
    TextureRes res;
    res.external = true;  // the drawables are the layer's: not counted as the device's memory
    res.desc.usage = TextureUsage::ColorAttachment | TextureUsage::CopySrc | TextureUsage::Sampled;
    LockGuard<Mutex> lock(resource_mutex_);
    const TextureHandle handle = textures_.Allocate(static_cast<TextureRes&&>(res));
    if (!handle.IsValid())
    {
        RhiLog(LogVerbosity::Error, "CreateSwapchain: out of texture handles");
    }
    return handle;
}

void MetalDevice::SetSwapchainTexture(TextureHandle texture, id<MTLTexture> image, Format format, u32 width, u32 height)
{
    LockGuard<Mutex> lock(resource_mutex_);
    if (TextureRes* t = textures_.Get(texture))
    {
        t->texture = image;
        t->desc.format = format;
        t->desc.width = width;
        t->desc.height = height;
    }
}

void MetalDevice::ReleaseSwapchainTexture(TextureHandle texture)
{
    LockGuard<Mutex> lock(resource_mutex_);
    if (TextureRes* t = textures_.Get(texture))
    {
        t->texture = nil;
        textures_.Retire(texture, frame_open_.Load(MemoryOrder::Relaxed) ? frame_number_ : frame_number_ + 1);
    }
}

bool MetalDevice::PresentDrawable(id<CAMetalDrawable> drawable)
{
    LockGuard<Mutex> lock(submit_mutex_);
    if (DeviceLost())
    {
        return false;
    }
    if (lose_device_at_present_ != 0 &&
        present_count_.FetchAdd(1, MemoryOrder::Relaxed) + 1 == lose_device_at_present_ &&
        InjectLoss("present", lose_device_at_present_))
    {
        return false;  // the Nth present loses the device, as on the other backends
    }
    @autoreleasepool
    {
        id<MTLCommandBuffer> buffer = [queue_ commandBuffer];
        if (buffer == nil)
        {
            NoteLost("a present", "the queue gave no command buffer");
            return false;
        }
        buffer.label = @"engine present";
        [buffer presentDrawable:drawable];
        WatchCommandBuffer(buffer);
        SignalAfterLocked(buffer);
        [buffer commit];
    }
    return true;
}

}  // namespace engine_placeholder::rhi::metal
