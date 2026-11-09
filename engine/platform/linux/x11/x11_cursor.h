#pragma once

#include "platform/cursor.h"
#include "platform/input_event.h"
#include "types.h"

//! X11 cursor shapes, custom cursor images and the pointer lock for [[os::x11::X11Window]].

struct _XDisplay;

namespace engine_placeholder::os::x11 {

/// The cursor and lock state of one X11 window: plain data, copied when the window moves.
///
/// Created cursors are server objects named by their XID (`0` means none); [[os::x11::CursorRelease]] frees them. `live` counts the server cursors this state owns, so a
/// test can check that replacing and clearing images does not leak.
struct X11CursorState {
    unsigned long shape_cursor[static_cast<usize>(CursorShape::Count)] = {}; ///< Cursors made for each shape on first use.
    unsigned long image_cursor = 0;                                          ///< The custom image's cursor, or 0.
    unsigned long blank_cursor = 0;                                          ///< A fully transparent cursor, made on first use.
    u64 image_hash = 0;                                                      ///< [[os::HashCursorImage]] of `image_cursor`.
    CursorShape shape = CursorShape::Arrow;                                  ///< The shape set last.
    bool image_active = false;                                               ///< A custom image is showing.
    bool grab_held = false;                                                  ///< This window holds the pointer grab.
    bool xi_ready = false;                                                   ///< XInput 2 raw motion is available.
    int xi_opcode = 0;                                                       ///< The XInput major opcode, for generic events.
    u32 live = 0;                                                            ///< Server cursors owned.
    PointerLockState lock;                                                   ///< The lock rules.
};

/// Looks for XInput 2 (raw motion) on the display; call once after the window exists.
void CursorInit(_XDisplay* display, X11CursorState& state);

/// Shows a standard shape (see [[os::Window::SetCursor]]); applies it at once.
/// Returns: false for an out-of-range shape.
bool CursorSetShape(_XDisplay* display, unsigned long window, X11CursorState& state, CursorShape shape);

/// Shows a custom image (see [[os::Window::SetCursorImage]]); an image equal to the one showing is not made again.
/// Returns: false if the image is invalid or the server could not make the cursor; then nothing changes.
bool CursorSetImage(_XDisplay* display, unsigned long window, X11CursorState& state, const CursorImage& image);

/// Removes the custom image and frees its cursor.
void CursorClearImage(_XDisplay* display, unsigned long window, X11CursorState& state);

/// Asks for the pointer lock on or off (see [[os::Window::SetPointerLock]]). Takes effect only while `focused` tracks the window's focus.
void CursorSetLock(_XDisplay* display, unsigned long window, X11CursorState& state, bool locked);

/// Tells the lock the window gained or lost keyboard focus.
void CursorSetFocus(_XDisplay* display, unsigned long window, X11CursorState& state, bool focused);

/// Handles an XInput 2 generic event: queues a `RelativeMotion` while the lock is active. `event` is an `XEvent*` of type `GenericEvent`.
void CursorHandleGeneric(_XDisplay* display, X11CursorState& state, void* event, InputEventQueue& queue);

/// Releases the grab and frees every cursor; the program's wanted state is kept. Call before the display closes.
void CursorRelease(_XDisplay* display, unsigned long window, X11CursorState& state);

} // namespace engine_placeholder::os::x11
