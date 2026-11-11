#pragma once

#include "types.h"

//! The mouse cursor: the standard shapes, a custom image, and the pointer lock state machine the window backends share.
//!
//! Pure data and logic, no display library: the header is part of the `platform_input` target, so the UI and the tests
//! can use it without a window.

namespace engine_placeholder::os
{

/// The cursor shapes every backend can show (where it can: see [[os::Window::SupportsCursorShape]]).
enum class CursorShape : u8
{
    Arrow = 0,           ///< The default pointer arrow.
    IBeam,               ///< The text insertion beam.
    Hand,                ///< A pointing hand, over links and buttons.
    ResizeHorizontal,    ///< A left-right arrow, over a vertical edge.
    ResizeVertical,      ///< An up-down arrow, over a horizontal edge.
    ResizeDiagonalNWSE,  ///< A diagonal arrow from top left to bottom right.
    ResizeDiagonalNESW,  ///< A diagonal arrow from top right to bottom left.
    ResizeAll,           ///< A four-way arrow, for moving.
    NotAllowed,          ///< A forbidden sign.
    Hidden,              ///< No cursor at all.
    Count,               ///< The number of shapes; not a shape.
};

/// The largest width or height of a custom cursor image, in pixels.
inline constexpr u32 kMaxCursorImageSize = 128;

/// The largest buffer scale of a custom cursor image.
inline constexpr u32 kMaxCursorImageScale = 4;

/// A custom cursor: pixels, a hotspot and a scale factor. The window copies the pixels; the caller keeps its own.
///
/// Pixels are RGBA8 with **premultiplied** alpha, four bytes per pixel in the order red, green, blue, alpha, rows from
/// the top, no padding (`width * height * 4` bytes). `scale` is the pixel density the image was drawn for: the cursor
/// is shown `width / scale` logical units wide, so one image for a scale-2 display is 64x64 pixels with `scale` 2 and a
/// hotspot given in pixels.
///
/// - Thread-safety: thread-compatible plain data.
/// - Ownership: borrows `rgba`; the pointer must stay valid for the call that receives the image.
struct CursorImage
{
    const byte* rgba = nullptr;  ///< `width * height * 4` bytes of premultiplied RGBA.
    u32 width = 0;               ///< Width in pixels, 1 to [[os::kMaxCursorImageSize]].
    u32 height = 0;              ///< Height in pixels, 1 to [[os::kMaxCursorImageSize]].
    i32 hotspot_x = 0;           ///< The click point, in pixels from the left; inside the image.
    i32 hotspot_y = 0;           ///< The click point, in pixels from the top; inside the image.
    u32 scale = 1;               ///< The pixel density, 1 to [[os::kMaxCursorImageScale]].
};

/// Why a [[os::CursorImage]] was refused.
enum class CursorImageError : u8
{
    Ok = 0,          ///< The image is acceptable.
    NoPixels,        ///< `rgba` is null.
    Empty,           ///< `width` or `height` is zero.
    TooLarge,        ///< `width` or `height` is above [[os::kMaxCursorImageSize]].
    HotspotOutside,  ///< The hotspot is not inside the image.
    BadScale,        ///< `scale` is zero or above [[os::kMaxCursorImageScale]].
};

/// Checks a cursor image.
/// - Param image: the image to check.
/// - Returns: [[os::CursorImageError::Ok]] if a backend may use it, else the first problem found.
inline CursorImageError ValidateCursorImage(const CursorImage& image)
{
    if (image.rgba == nullptr)
    {
        return CursorImageError::NoPixels;
    }
    if (image.width == 0 || image.height == 0)
    {
        return CursorImageError::Empty;
    }
    if (image.width > kMaxCursorImageSize || image.height > kMaxCursorImageSize)
    {
        return CursorImageError::TooLarge;
    }
    if (image.scale == 0 || image.scale > kMaxCursorImageScale)
    {
        return CursorImageError::BadScale;
    }
    if (image.hotspot_x < 0 || image.hotspot_y < 0 || static_cast<u32>(image.hotspot_x) >= image.width ||
        static_cast<u32>(image.hotspot_y) >= image.height)
    {
        return CursorImageError::HotspotOutside;
    }
    return CursorImageError::Ok;
}

/// Hashes everything that makes an image look different (size, hotspot, scale, pixels), so a backend can tell that it
/// already has the cursor.
/// - Param image: a valid image.
/// - Returns: a 64-bit FNV-1a hash; equal images give equal hashes.
inline u64 HashCursorImage(const CursorImage& image)
{
    u64 hash = 1469598103934665603ull;
    auto mix = [&hash](u64 value)
    {
        for (int i = 0; i < 8; ++i)
        {
            hash ^= (value >> (i * 8)) & 0xFFu;
            hash *= 1099511628211ull;
        }
    };
    mix(image.width);
    mix(image.height);
    mix(static_cast<u64>(static_cast<u32>(image.hotspot_x)));
    mix(static_cast<u64>(static_cast<u32>(image.hotspot_y)));
    mix(image.scale);
    const usize size = static_cast<usize>(image.width) * image.height * 4;
    for (usize i = 0; i < size; ++i)
    {
        hash ^= image.rgba[i];
        hash *= 1099511628211ull;
    }
    return hash;
}

/// The request and release rules of the pointer lock, shared by the backends and testable without a display.
///
/// The lock should be held exactly while it is **wanted** (the program asked), the window has **focus**, and the
/// backend has not **lost** it by itself since focus last changed. Each call that can change what should hold returns
/// the [[os::PointerLockState::Action]] the backend must carry out; the state counts the lock as pending from the
/// moment it returns `Acquire` until the backend reports `Granted` or `Failed`.
///
/// - Thread-safety: not-thread-safe; owner-thread only.
class PointerLockState
{
public:
    /// What the backend has to do after a call.
    enum class Action : u8
    {
        Nothing,  ///< Nothing.
        Acquire,  ///< Ask for the lock now, then report [[os::PointerLockState::Granted]] or
                  ///< [[os::PointerLockState::Failed]].
        Release,  ///< Give the lock up now.
    };

    /// The program asks for the lock on or off. A second request for the same state does nothing.
    Action SetWanted(bool wanted)
    {
        wanted_ = wanted;
        return Reconcile();
    }

    /// The window gained or lost keyboard focus. A lock the backend lost by itself is asked for again when focus
    /// returns.
    Action SetFocused(bool focused)
    {
        focused_ = focused;
        lost_ = false;
        return Reconcile();
    }

    /// The backend reports that the lock is now in effect (immediately for X11, on the `locked` event for Wayland).
    void Granted()
    {
        if (pending_)
        {
            pending_ = false;
            active_ = true;
        }
    }

    /// The backend reports that the request was refused; it is asked for again on the next focus change or request.
    void Failed() { pending_ = false; }

    /// The backend lost the lock by itself (the compositor unlocked it, the grab was broken). It stays wanted but is
    /// not asked for again until focus changes.
    void Lost()
    {
        pending_ = false;
        active_ = false;
        lost_ = true;
    }

    /// The window is going away or the backend released everything: forget the lock but keep what the program wanted.
    void Cleared()
    {
        pending_ = false;
        active_ = false;
    }

    /// Returns whether the lock is in effect.
    bool Active() const { return active_; }
    /// Returns whether a request is waiting for the backend's answer.
    bool Pending() const { return pending_; }
    /// Returns whether the program wants the lock.
    bool Wanted() const { return wanted_; }
    /// Returns whether the window has focus.
    bool Focused() const { return focused_; }

private:
    Action Reconcile()
    {
        const bool should = wanted_ && focused_ && !lost_;
        const bool held = pending_ || active_;
        if (should && !held)
        {
            pending_ = true;
            return Action::Acquire;
        }
        if (!should && held)
        {
            pending_ = false;
            active_ = false;
            return Action::Release;
        }
        return Action::Nothing;
    }

    bool wanted_ = false;
    bool focused_ = false;
    bool lost_ = false;
    bool pending_ = false;
    bool active_ = false;
};

}  // namespace engine_placeholder::os
