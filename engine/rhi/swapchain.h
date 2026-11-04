#pragma once

#include "rhi/format.h"
#include "rhi/handle.h"
#include "types.h"

//! The presentable images of one window.

namespace engine_placeholder::rhi {

/// The outcome of acquiring or presenting a swapchain image.
enum class SwapchainStatus : u8 {
    Ok = 0,    ///< The operation succeeded.
    Recreated, ///< The swapchain was recreated (resize or out-of-date); the image extent or handles may have changed.
    Minimized, ///< The window has no drawable area; skip rendering this frame.
    Lost,      ///< The surface or device is lost; the swapchain cannot be used again.
};

/// The images of one window's swapchain, acquired and presented once per frame.
///
/// The backend-specific creation function is declared with the window integration (Phase 3 Day 56), where
/// the native window handle from the platform layer is available.
///
/// Thread-safety: owner-thread-only. Use one swapchain from the thread that owns its window.
/// Ownership: created by and owned by a device; the device destroys it, and the textures it exposes are
///   valid only until the next `Acquire` or `Resize`.
class Swapchain {
public:
    virtual ~Swapchain() = default;

    /// Acquires the next image to render to and makes it the current texture.
    ///
    /// Returns: `Ok`, or `Recreated`, `Minimized` or `Lost` as described for [[rhi::SwapchainStatus]].
    /// Fails: a status other than `Ok` means no image was acquired, except `Recreated`, after which an image is acquired.
    virtual SwapchainStatus Acquire() = 0;

    /// Presents the current image to the window.
    ///
    /// Returns: `Ok`, `Recreated` (presented, but the swapchain was recreated for next frame), or `Lost`.
    /// Fails: `Lost` means the image was not presented.
    /// Aborts: no image has been acquired.
    virtual SwapchainStatus Present() = 0;

    /// Recreates the images for a new window size.
    ///
    /// Param width: new width in pixels.
    /// Param height: new height in pixels; zero in either dimension is treated as minimized.
    /// Returns: `Ok`, `Minimized`, or `Lost`.
    /// Fails: `Lost` when the swapchain cannot be recreated.
    virtual SwapchainStatus Resize(u32 width, u32 height) = 0;

    /// Returns the format of the swapchain images.
    virtual Format ImageFormat() const = 0;

    /// Returns the width of the images in pixels.
    virtual u32 Width() const = 0;

    /// Returns the height of the images in pixels.
    virtual u32 Height() const = 0;

    /// Returns the texture for the most recently acquired image.
    ///
    /// Returns: the invalid handle before the first successful `Acquire`.
    /// Ownership: the handle is owned by the swapchain; do not destroy it.
    virtual TextureHandle CurrentTexture() const = 0;
};

} // namespace engine_placeholder::rhi
