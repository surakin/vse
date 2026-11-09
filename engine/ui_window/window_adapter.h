#pragma once

#include "platform/cursor.h"
#include "platform/window.h"
#include "types.h"
#include "ui/surface.h"

//! The window adapter (Phase 15, Day 194): the one place where an [[os::Window]] meets a [[ui::Surface]]. `ui` itself names no window.

namespace engine_placeholder::ui {

/// Feeds a window's input events to a surface and gives the surface's cursor to the window.
///
/// A frame of a windowed program reads: `window.PumpEvents()`, `adapter.PumpInto(window, surface)`, `surface.Frame(...)`, `adapter.UpdateCursor(window, surface)`. The window's coordinates are
/// physical pixels; the adapter divides pointer positions by the surface scale to get logical units. A `Resize` of the window becomes `Surface::Resize` at the same scale (several in one
/// pump cost one layout). A wheel event is placed at the last pointer position. The window's pointer enter becomes a move, its leave a surface-level pointer leave, its focus gain and loss the surface's window focus. Clipboard and drag events
/// are dropped for now (they arrive with the widgets that use them).
///
/// Thread-safety: owner-thread-only.
/// Ownership: owns nothing; the window and the surface are borrowed for each call.
class WindowAdapter {
public:
    /// Drains `window.PollEvent` into `surface.Post`, in order. Does not call `window.PumpEvents`.
    /// Returns: the number of events posted to the surface (a resize is applied at once and not counted).
    usize PumpInto(os::Window& window, Surface& surface);

    /// Asks the surface for the cursor shape ([[ui::Surface::RequestedCursor]]) and calls `window.SetCursor` when it differs from the last one set through this adapter (the window starts as Arrow).
    /// Returns: whether the window was called.
    /// Fails: a window that refuses the shape (it returns false) is asked again next time.
    bool UpdateCursor(os::Window& window, const Surface& surface);

    /// Returns how many times `UpdateCursor` called `SetCursor`.
    u64 CursorChanges() const { return cursor_changes_; }

private:
    Point pointer_;
    os::CursorShape last_cursor_ = os::CursorShape::Arrow;
    u64 cursor_changes_ = 0;
};

} // namespace engine_placeholder::ui
