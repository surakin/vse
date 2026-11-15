// The Metal swapchain (Phase 21 Day 305; metal_swapchain.h).
//
// The layer is the window's own CAMetalLayer (os::NativeWindowInfo::surface). The images are its drawables:
// `nextDrawable` waits at most the layer's timeout (allowsNextDrawableTimeout, one second) and a timeout counts as
// Minimized, as a stalled compositor does elsewhere. Present encodes `presentDrawable:` in a command buffer of its own,
// which the queue runs after the frame's lists, and lets the drawable go. `framebufferOnly` is off so a swapchain image
// can be copied from (screenshots), as on the other backends.

#include "rhi/metal/metal_swapchain.h"

#include "log/assert.h"
#include "platform/window.h"
#include "rhi/metal/metal_device.h"
#include "rhi/metal/metal_format.h"
#include "rhi/rhi_log.h"

#import <AppKit/AppKit.h>
#import <Foundation/Foundation.h>

namespace engine_placeholder::rhi::metal
{

PresentMode MetalPresentModeFor(PresentMode requested)
{
    return requested == PresentMode::Immediate ? PresentMode::Immediate : PresentMode::Fifo;
}

MetalSwapchain::MetalSwapchain(MetalDevice* device, const os::Window* window) : device_(device), window_(window) {}

MetalSwapchain::~MetalSwapchain()
{
    drawable_ = nil;
    if (texture_.IsValid())
    {
        device_->ReleaseSwapchainTexture(texture_);
    }
}

bool MetalSwapchain::Initialize(const SwapchainDesc& desc)
{
    @autoreleasepool
    {
        const os::NativeWindowInfo native = window_->GetNativeInfo();
        if (native.kind != os::NativeWindowKind::Cocoa || native.surface == nullptr)
        {
            RhiLog(LogVerbosity::Error, "CreateSwapchain: the window has no Metal layer");
            return false;
        }
        layer_ = (__bridge CAMetalLayer*)native.surface;
        layer_.device = device_->Mtl();
        layer_.framebufferOnly = NO;
        layer_.allowsNextDrawableTimeout = YES;
        texture_ = device_->RegisterSwapchainTexture();
        if (!texture_.IsValid())
        {
            return false;
        }
        Apply(desc);  // Ok, or Minimized for a window with no area yet (applied when it has one)
        return true;
    }
}

bool MetalSwapchain::EdrPossible() const
{
    @autoreleasepool
    {
        // AppKit: called from Apply, on the swapchain's (the main) thread.
        NSScreen* screen = NSScreen.mainScreen;
        const os::NativeWindowInfo native = window_->GetNativeInfo();
        if (native.display != nullptr)
        {
            NSView* view = (__bridge NSView*)native.display;
            if (view.window.screen != nil)
            {
                screen = view.window.screen;
            }
        }
        return screen != nil && screen.maximumPotentialExtendedDynamicRangeColorComponentValue > 1.0;
    }
}

SwapchainCaps MetalSwapchain::Capabilities() const
{
    SwapchainCaps caps;
    caps.present_mode_supported[static_cast<usize>(PresentMode::Fifo)] = true;
    caps.present_mode_supported[static_cast<usize>(PresentMode::Mailbox)] = false;   // macOS has no mailbox
    caps.present_mode_supported[static_cast<usize>(PresentMode::Immediate)] = true;  // displaySyncEnabled = NO
    caps.formats[0] = Format::BGRA8_SRGB;
    caps.formats[1] = Format::BGRA8_UNorm;
    caps.format_count = 2;
    caps.hdr_scrgb = edr_possible_;  // read when the swapchain was last built (Apply), not here
    caps.hdr10 = false;              // needs a 10-bit format the RHI does not have
    return caps;
}

void MetalSwapchain::SizeFromWindow()
{
    width_ = desc_.width != 0 ? desc_.width : window_->Width();
    height_ = desc_.height != 0 ? desc_.height : window_->Height();
    resize_serial_ = window_->ResizeSerial();
}

SwapchainStatus MetalSwapchain::Apply(const SwapchainDesc& desc)
{
    @autoreleasepool
    {
        desc_ = desc;
        // The format: an 8-bit BGRA (sRGB by default), or 16-bit float for scRGB.
        Format format = desc.format == Format::Undefined ? Format::BGRA8_SRGB : desc.format;
        ColorSpace space = ColorSpace::SrgbNonlinear;
        bool edr = false;
        edr_possible_ = EdrPossible();
        if (desc.hdr == HdrMode::ScRgbLinear)
        {
            if (edr_possible_)
            {
                format = Format::RGBA16_Float;
                space = ColorSpace::ScRgbLinear;
                edr = true;
            }
            else
            {
                RhiLog(LogVerbosity::Info, "Swapchain: this screen has no extended dynamic range; HDR off");
            }
        }
        else if (desc.hdr == HdrMode::Hdr10)
        {
            RhiLog(LogVerbosity::Info, "Swapchain: HDR10 needs a 10-bit format the RHI does not have; HDR off");
        }
        if (!edr && format != Format::BGRA8_SRGB && format != Format::BGRA8_UNorm)
        {
            RhiLog(LogVerbosity::Info, "Swapchain: the layer does not take %s; using bgra8_srgb",
                   FormatInfo(format).name);
            format = Format::BGRA8_SRGB;
        }
        format_ = format;
        color_space_ = space;
        present_mode_ = MetalPresentModeFor(desc.present_mode);
        if (desc.present_mode == PresentMode::Mailbox)
        {
            RhiLog(LogVerbosity::Info, "Swapchain: macOS has no mailbox presentation; using fifo");
        }
        layer_.pixelFormat = ToMtlPixelFormat(format_, device_->Mtl());
        layer_.wantsExtendedDynamicRangeContent = edr;
        CGColorSpaceRef cs = CGColorSpaceCreateWithName(edr ? kCGColorSpaceExtendedLinearSRGB : kCGColorSpaceSRGB);
        layer_.colorspace = cs;
        CGColorSpaceRelease(cs);
        layer_.displaySyncEnabled = present_mode_ == PresentMode::Fifo;
        layer_.maximumDrawableCount = device_->FramesInFlight() >= 3 ? 3 : 2;
        SizeFromWindow();
        if (width_ == 0 || height_ == 0 || window_->IsMinimized())
        {
            pending_ = true;  // kept: applied when the window has an area again
            return SwapchainStatus::Minimized;
        }
        pending_ = false;
        layer_.drawableSize = CGSizeMake(width_, height_);
        return SwapchainStatus::Ok;
    }
}

SwapchainStatus MetalSwapchain::Acquire()
{
    @autoreleasepool
    {
        ENGINE_ASSERT(!acquired_, "Acquire: the previous image was not presented");
        if (device_->DeviceLost())
        {
            return SwapchainStatus::Lost;
        }
        if (window_->IsMinimized())
        {
            return SwapchainStatus::Minimized;
        }
        SwapchainStatus status = SwapchainStatus::Ok;
        if (pending_)
        {
            // A request that waited for the window to have an area: applied whole now.
            const SwapchainStatus applied = Apply(desc_);
            if (applied != SwapchainStatus::Ok)
            {
                return applied;
            }
            status = SwapchainStatus::Recreated;
        }
        else if (window_->ResizeSerial() != resize_serial_)
        {
            // The window changed size: the images follow the window (an explicit Resize size gives way to it, as on the
            // other backends), and only the drawable size changes.
            desc_.width = 0;
            desc_.height = 0;
            SizeFromWindow();
            if (width_ == 0 || height_ == 0)
            {
                pending_ = true;
                return SwapchainStatus::Minimized;
            }
            layer_.drawableSize = CGSizeMake(width_, height_);
            status = SwapchainStatus::Recreated;
        }
        drawable_ = [layer_ nextDrawable];
        if (drawable_ == nil)
        {
            return SwapchainStatus::Minimized;  // the layer's timeout: nothing on screen is taking frames
        }
        device_->SetSwapchainTexture(texture_, drawable_.texture, format_, width_, height_);
        acquired_ = true;
        return status;
    }
}

SwapchainStatus MetalSwapchain::Present()
{
    @autoreleasepool
    {
        ENGINE_ASSERT(acquired_, "Present: no image has been acquired");
        acquired_ = false;
        const bool shown = device_->PresentDrawable(drawable_);
        device_->SetSwapchainTexture(texture_, nil, format_, width_, height_);
        drawable_ = nil;  // never kept past its frame
        // A resize since Acquire is picked up by the next Acquire (which says Recreated once), as on Direct3D 12.
        return shown ? SwapchainStatus::Ok : SwapchainStatus::Lost;
    }
}

SwapchainStatus MetalSwapchain::Reconfigure(const SwapchainDesc& desc)
{
    ENGINE_ASSERT(!acquired_, "Reconfigure: an image is still acquired");
    if (device_->DeviceLost())
    {
        return SwapchainStatus::Lost;
    }
    return Apply(desc);
}

SwapchainStatus MetalSwapchain::Resize(u32 width, u32 height)
{
    if (device_->DeviceLost())
    {
        return SwapchainStatus::Lost;
    }
    SwapchainDesc d = desc_;
    d.width = width;
    d.height = height;
    if (width == 0 || height == 0)
    {
        desc_ = d;
        pending_ = true;
        return SwapchainStatus::Minimized;
    }
    return Reconfigure(d);
}

}  // namespace engine_placeholder::rhi::metal
