#pragma once

#include "platform/cursor.h"
#include "platform/window.h"
#include "types.h"
#include "ui/surface.h"
#include "ui_window/os_clipboard.h"

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

    /// Lets the adapter pass the window's `ClipboardData` events to `clipboard` (null stops). The surface is given the clipboard separately with `Surface::SetClipboard`.
    /// Ownership: borrows `clipboard`, which must outlive its use here.
    void SetClipboard(OsClipboard* clipboard) { clipboard_ = clipboard; }
    /// Returns how many `ClipboardChanged` events were seen (a Paste command re-reads `Clipboard::HasText` after one).
    u64 ClipboardChanges() const { return clipboard_changes_; }
    /// Tells the window to accept drags of text, file lists and (when `custom_name` is not empty) that custom format, so `PumpInto` turns them into external drags on the surface. Call it once
    /// after the window exists; without it other programs' drags are refused.
    /// Param custom_name: the MIME type of an application format, or empty for none; copied.
    void EnableDrops(os::Window& window, StringView custom_name = StringView());

    // Drag and drop in `PumpInto`: a drag from another program becomes an external drag on the surface (its kinds first, the data read once it is dropped; the target sees the drop only with
    // complete data, and a failed read ends the drag and runs `Surface::OnDropFailed`); a drag started on the surface that leaves the window (or whose pointer leaves it) becomes an OS drag
    // when it has any text, file or custom entry (a payload of pointers only never leaves), and the window's `DragEnded` ends it.

private:
    struct DropRequest {
        u32 id = 0;
        os::ClipboardFormat format = os::ClipboardFormat::Text;
    };
    void TryHandOff(os::Window& window, Surface& surface);
    void StartDrop(os::Window& window, Surface& surface, const os::InputEvent& in, Point position);
    void FinishDropData(os::Window& window, Surface& surface, u32 request_id);
    void FailDrop(os::Window& window, Surface& surface);

    Point pointer_;
    OsClipboard* clipboard_ = nullptr;
    u64 clipboard_changes_ = 0;
    Vector<char> custom_name_;
    bool dropping_ = false;
    Point drop_position_;
    os::DragAction drop_action_ = os::DragAction::Nothing;
    Vector<DropRequest> drop_requests_;
    DragPayload drop_payload_;
    os::CursorShape last_cursor_ = os::CursorShape::Arrow;
    u64 cursor_changes_ = 0;
};

} // namespace engine_placeholder::ui
