#pragma once

#include "rhi/format.h"
#include "rhi/handle.h"
#include "types.h"

//! The presentable images of one window.

namespace engine_placeholder::rhi
{

/// The outcome of acquiring or presenting a swapchain image.
enum class SwapchainStatus : u8
{
    Ok = 0,     ///< The operation succeeded.
    Recreated,  ///< The swapchain was recreated (resize or out-of-date); the image extent or handles may have changed.
    Minimized,  ///< The window has no drawable area; skip rendering this frame.
    Lost,       ///< The surface or device is lost; the swapchain cannot be used again.
};

/// How finished images reach the screen.
enum class PresentMode : u8
{
    Fifo = 0,   ///< Wait for the display's refresh (vertical sync); always available.
    Mailbox,    ///< Replace the queued image with the newest one, no tearing, if the system supports it; otherwise
                ///< `Fifo`.
    Immediate,  ///< Present at once, possibly tearing, if the system supports it; otherwise `Fifo`.
};

/// Whether and how the swapchain shows high dynamic range.
enum class HdrMode : u8
{
    Off = 0,      ///< Standard dynamic range.
    ScRgbLinear,  ///< Linear extended sRGB in 16-bit float (the "scRGB" mode); 1.0 is SDR white.
    Hdr10,  ///< 10-bit PQ (ST 2084, BT.2020). Needs a 10-bit format the RHI does not have yet, so it is never granted
            ///< today.
};

/// The color space of the images a swapchain presents.
enum class ColorSpace : u8
{
    SrgbNonlinear = 0,  ///< sRGB transfer function (what an 8-bit sRGB format means).
    ScRgbLinear,        ///< Linear extended sRGB.
    Hdr10,              ///< HDR10 (PQ, BT.2020).
};

/// What a swapchain on a given window can do, so a settings screen can grey out what is unavailable.
///
/// - Thread-safety: thread-compatible plain data.
struct SwapchainCaps
{
    /// Whether each [[rhi::PresentMode]] (by value) is really offered; `Fifo` is always true.
    bool present_mode_supported[3] = {true, false, false};
    /// The image formats the surface offers in the sRGB-nonlinear color space; the first `format_count` are valid.
    Format formats[8] = {};
    /// Number of valid entries in `formats`.
    u32 format_count = 0;
    /// Whether [[rhi::HdrMode::ScRgbLinear]] can be granted.
    bool hdr_scrgb = false;
    /// Whether [[rhi::HdrMode::Hdr10]] can be granted.
    bool hdr10 = false;
};

/// What to create a [[rhi::Swapchain]] for.
///
/// - Thread-safety: thread-compatible plain data.
struct SwapchainDesc
{
    /// The preferred presentation mode; falls back to `Fifo` when the system does not offer it.
    PresentMode present_mode = PresentMode::Fifo;
    /// Initial image width in pixels; 0 uses the window's current width.
    u32 width = 0;
    /// Initial image height in pixels; 0 uses the window's current height.
    u32 height = 0;
    /// The preferred image format; `Undefined` lets the backend choose (an sRGB 8-bit format). Falls back, with a log
    /// line, when the surface does not offer it; read the result from `ImageFormat`.
    Format format = Format::Undefined;
    /// The dynamic range wanted; falls back to `Off` with a log line when the surface cannot do it.
    HdrMode hdr = HdrMode::Off;
};

/// The images of one window's swapchain, acquired and presented once per frame.
///
/// A device is created for one window (`DeviceCreateInfo::window`) and makes the swapchain for it with
/// [[rhi::Device::CreateSwapchain]]. Render into `CurrentTexture()` after moving it to `ColorAttachment` with a
/// barrier, move it to `Present`, submit, then `Present`.
///
/// - Thread-safety: owner-thread-only. Use one swapchain from the thread that owns its window.
/// - Ownership: created by a device; destroy it with [[rhi::Device::DestroySwapchain]] before the device is destroyed.
///   The textures it exposes are valid only until the next `Acquire` or `Resize`.
class Swapchain
{
public:
    virtual ~Swapchain() = default;

    /// Acquires the next image to render to and makes it the current texture.
    ///
    /// Never blocks for long: a window the compositor has stopped drawing (which Wayland does not report as
    /// minimized) times out after half a second (on Metal, the layer's own one second, which a program cannot
    /// shorten) and counts as `Minimized`. If the window changed size, or the
    /// surface is out of date, the swapchain is recreated first and the call still acquires an image.
    ///
    /// - Returns: `Ok`, or `Recreated`, `Minimized` or `Lost` as described for [[rhi::SwapchainStatus]].
    /// - Fails: a status other than `Ok` means no image was acquired, except `Recreated`, after which an image is
    /// acquired.
    /// - Aborts: the previously acquired image was not presented.
    virtual SwapchainStatus Acquire() = 0;

    /// Presents the current image to the window.
    ///
    /// - Returns: `Ok`, `Recreated` (the surface changed: the image may not have been shown, and the swapchain is
    /// rebuilt by
    ///   the next `Acquire`), or `Lost`.
    /// - Fails: `Lost` means the image was not presented.
    /// - Aborts: no image has been acquired.
    virtual SwapchainStatus Present() = 0;

    /// Changes the present mode, size, format and HDR mode in one call, reusing the old swapchain where the API allows.
    ///
    /// A request the surface cannot grant falls back with a log line; the result is readable from `ActualPresentMode`,
    /// `ImageFormat` and `GetColorSpace`. A zero width or height is remembered (the swapchain is rebuilt with the rest
    /// of the request when the window has an area again). The images and textures from before are invalid afterwards.
    ///
    /// - Param desc: the new description; `width` and `height` of 0 mean the window's current size.
    /// - Returns: `Ok` (rebuilt), `Minimized` (nothing to draw to yet), or `Lost`.
    /// - Fails: `Lost` when the swapchain cannot be rebuilt.
    /// - Aborts: an image is still acquired and not presented.
    virtual SwapchainStatus Reconfigure(const SwapchainDesc& desc) = 0;

    /// Returns the present mode really in use (a request for `Mailbox` or `Immediate` may have fallen back to `Fifo`).
    virtual PresentMode ActualPresentMode() const = 0;

    /// Returns the color space of the images.
    virtual ColorSpace GetColorSpace() const = 0;

    /// Returns what the surface and device offer.
    ///
    /// - Returns: the capabilities, read when the swapchain was last built.
    virtual SwapchainCaps Capabilities() const = 0;

    /// Recreates the images for a new window size, keeping the other settings; `Reconfigure` with a new size.
    ///
    /// - Param width: new width in pixels.
    /// - Param height: new height in pixels; zero in either dimension is treated as minimized.
    /// - Returns: `Ok`, `Minimized`, or `Lost`.
    /// - Fails: `Lost` when the swapchain cannot be recreated.
    virtual SwapchainStatus Resize(u32 width, u32 height) = 0;

    /// Returns the format of the swapchain images.
    virtual Format ImageFormat() const = 0;

    /// Returns the width of the images in pixels.
    virtual u32 Width() const = 0;

    /// Returns the height of the images in pixels.
    virtual u32 Height() const = 0;

    /// Returns the texture for the most recently acquired image.
    ///
    /// - Returns: the invalid handle before the first successful `Acquire`.
    /// - Ownership: the handle is owned by the swapchain; do not destroy it.
    virtual TextureHandle CurrentTexture() const = 0;
};

}  // namespace engine_placeholder::rhi
