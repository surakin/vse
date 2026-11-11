#pragma once

#include "platform/clipboard.h"
#include "string/string.h"
#include "types.h"

//! Drag and drop between programs: the values shared by every platform backend. The operations are on [[os::Window]].

namespace engine_placeholder::os
{

/// What a drop will do with the data.
enum class DragAction : u8
{
    Nothing = 0,  ///< No action: the drop is refused (or, in a `DragEnded` event, the drag was cancelled).
    Copy,         ///< The target copies the data.
    Move,         ///< The target takes the data and the source removes its own.
    Link,         ///< The target makes a link to the data.
};

/// A set of [[os::DragAction]] values: bit `1 << action`.
inline constexpr u8 DragActionBit(DragAction action)
{
    return action == DragAction::Nothing ? u8(0) : static_cast<u8>(1u << (static_cast<u8>(action) - 1u));
}
/// Every action but `Nothing`.
inline constexpr u8 kAllDragActions = 7;

/// Formats a drag offers or a window accepts, as bits.
inline constexpr u8 kDropText = 1;    ///< UTF-8 text.
inline constexpr u8 kDropFiles = 2;   ///< A file list.
inline constexpr u8 kDropCustom = 4;  ///< The custom format named in [[os::DropAccept::custom_name]].

/// What a window is willing to receive; nothing set refuses every drag.
///
/// - Thread-safety: thread-compatible plain data.
/// - Ownership: borrows `custom_name` for the call; the window copies it.
struct DropAccept
{
    u8 formats = 0;          ///< `kDropText | kDropFiles | kDropCustom`.
    StringView custom_name;  ///< `kDropCustom`: the MIME type or atom name.
};

/// A small image drawn next to the pointer while dragging (RGBA8, premultiplied, up to 128x128).
///
/// - Thread-safety: thread-compatible plain data.
/// - Ownership: borrows `rgba` for the call.
struct DragImage
{
    const byte* rgba = nullptr;  ///< `width * height * 4` bytes, or null for no image.
    u32 width = 0;               ///< Width in pixels.
    u32 height = 0;              ///< Height in pixels.
    i32 hotspot_x = 0;           ///< The pixel under the pointer.
    i32 hotspot_y = 0;           ///< The pixel under the pointer.
};

/// What a program drags out of its window.
///
/// - Thread-safety: thread-compatible plain data.
/// - Ownership: borrows everything for the call; the window copies the bytes.
struct DragSource
{
    const ClipboardOffer* offers = nullptr;  ///< The formats offered (at most [[os::kMaxDragOffers]]).
    usize offer_count = 0;                   ///< Their count.
    u8 allowed_actions = kAllDragActions;    ///< A set of [[os::DragAction]] bits the source can serve.
    DragImage image;                         ///< Optional.
    /// Called once when the OS drag starts, before `BeginDrag` returns; may be null. Win32's drag is modal (`BeginDrag`
    /// returns when it is over), so it calls this before the drag runs: a program hands its own drag to the OS here
    /// rather than after the return.
    void (*started)(void* user) = nullptr;
    void* started_user = nullptr;  ///< Passed to `started`.
};

/// The most formats one drag offers.
inline constexpr usize kMaxDragOffers = 3;
/// The most types of an incoming drag that are looked at (a hostile offer may list thousands).
inline constexpr usize kMaxDragTypes = 64;
/// The most entries of a dropped file list that are kept; the rest are counted in `rejected_entries`.
inline constexpr usize kMaxDropFileEntries = 10000;
/// How long a drop waits for the program to call `FinishDrop`, and a drag waits for a target's answer (nanoseconds).
inline constexpr u64 kDropSessionTimeoutNs = 5'000'000'000ull;
/// How long the source of a drop waits for the target to finish (1 s: a target that does not answer cancels).
inline constexpr u64 kDragTargetTimeoutNs = 1'000'000'000ull;

/// Keeps the first `max_entries` paths of a `\n`-separated list.
/// - Param paths: the list, edited in place.
/// - Param max_entries: the most to keep.
/// - Returns: how many entries were removed.
u32 LimitPathList(Vector<byte>& paths, usize max_entries);

}  // namespace engine_placeholder::os
