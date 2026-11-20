#pragma once

#include "string/string.h"
#include "ui/event.h"
#include "ui/geometry.h"

//! Popups and accelerators, the plain types (Phase 15, Day 198): where a popup goes relative to its anchor, how it
//! closes, and the key chords of accelerators.

namespace vse::ui
{

class Widget;

/// Which side of the anchor a popup prefers.
enum class Placement : u8
{
    Below,  ///< Under the anchor, left edges aligned.
    Above,  ///< Over the anchor, left edges aligned.
    Right,  ///< Right of the anchor, top edges aligned (submenus).
    Left,   ///< Left of the anchor, top edges aligned.
};

/// How a popup opens and closes.
///
/// - Thread-safety: thread-compatible plain data.
/// - Ownership: `owner` is borrowed.
struct PopupOptions
{
    Placement placement = Placement::Below;  ///< The preferred side; the opposite one is used when this one does not
                                             ///< fit and that one does.
    bool consume_outside_click = true;       ///< A press outside the popups closes them and goes no further (false: it
                                             ///< closes them and is delivered as usual).
    bool take_focus = true;   ///< The popup widget gets the keyboard focus when it opens (when it is focusable).
    Widget* owner = nullptr;  ///< The anchor widget: a press on it while the popup is open closes the popup and is
                              ///< consumed; destroying it closes the popup.
};

/// A key and the modifiers that must be held with it (Ctrl, Shift, Alt and Super count; Super is Command on macOS).
///
/// - Thread-safety: thread-compatible plain data.
struct KeyChord
{
    Key key = Key::Unknown;  ///< The key.
    u32 modifiers = 0;       ///< A mask of `os::kModCtrl`, `os::kModShift`, `os::kModAlt` and `os::kModSuper`
                             ///< (`os::kModShortcut` for a command).

    /// Returns whether both fields are equal.
    ///
    /// - Fails: never.
    bool operator==(const KeyChord& o) const { return key == o.key && modifiers == o.modifiers; }
};

/// Returns where a popup of `size` goes for `anchor`, kept inside `bounds`.
///
/// The preferred side is used when the popup fits there; otherwise the opposite side when it fits, otherwise the
/// preferred side. Then the popup is shifted along the anchor's edge to stay inside `bounds`, and finally clamped so
/// that it starts inside `bounds` even when it is larger.
///
/// - Param size: the popup's size.
/// - Param anchor: the anchor rectangle, in surface coordinates (a context menu passes an empty rectangle at the
/// pointer).
/// - Param placement: the preferred side.
/// - Param bounds: the area to stay in (the surface).
/// - Returns: the popup's rectangle.
/// - Fails: never.
Rect PlacePopup(Size size, const Rect& anchor, Placement placement, const Rect& bounds);

/// Returns the text of a chord, such as "Ctrl+Shift+S" or "F10" (modifiers in the order Ctrl, Cmd or Super, Shift,
/// Alt).
///
/// - Param chord: the chord.
/// - Returns: the text; letters are upper case.
/// - Ownership: the caller owns the string.
/// - Fails: `OutOfMemory` aborts like any string.
String<HeapAllocator> ChordText(const KeyChord& chord);

}  // namespace vse::ui
