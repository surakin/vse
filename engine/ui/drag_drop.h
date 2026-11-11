#pragma once

#include "containers/vector.h"
#include "platform/cursor.h"
#include "platform/drag_drop.h"
#include "string_view.h"
#include "types.h"

//! Drag and drop in the toolkit (Phase 15, Day 200): what is dragged. The routing is in [[ui::Surface]]; the widgets
//! that start and accept drags override the `Widget` drag virtuals.

namespace engine_placeholder::ui
{

/// What a drop does with the data; the platform layer's enumeration, so an OS drag needs no translation.
using DragAction = os::DragAction;

/// The kind of one entry of a [[ui::DragPayload]].
enum class DragEntryKind : u8
{
    Text,      ///< UTF-8 text.
    FileList,  ///< Absolute paths separated by `\n`.
    Custom,    ///< Bytes of an application format named by the entry's name.
    Pointer,   ///< An application pointer, valid only inside the process (never offered to the OS).
};

/// One typed piece of data in a payload.
///
/// - Thread-safety: thread-compatible.
/// - Ownership: owns its name and bytes; `pointer` is borrowed.
struct DragEntry
{
    DragEntryKind kind = DragEntryKind::Text;  ///< What the entry holds.
    Vector<char> name_chars;  ///< `Custom`: the format name (a MIME type); `Pointer`: the application's tag. Read it
                              ///< with `Name`.
    Vector<byte> bytes;       ///< `Text`, `FileList`, `Custom`: the data. Empty while `pending`.
    void* pointer = nullptr;  ///< `Pointer`: the application's pointer.
    bool pending = false;  ///< The entry is announced by another program but its data has not been read (a drag still
                           ///< moving); a target decides from the kind.
    /// Returns the name.
    ///
    /// - Fails: never.
    StringView Name() const { return StringView(name_chars.Data(), name_chars.Size()); }
    /// Returns the bytes as text.
    ///
    /// - Fails: never.
    StringView TextView() const { return StringView(reinterpret_cast<const char*>(bytes.Data()), bytes.Size()); }
};

/// What a drag carries: typed entries, the actions the source allows, and the action the user asks for.
///
/// A source builds one with the `Add` functions and gives it to [[ui::Surface::BeginDrag]]. A target reads it in
/// `CanAccept` and `OnDrop`; a payload that came from another program is `pending` until the drop (its entries name the
/// kinds on offer without data), so a target must not read bytes in `CanAccept`. Every entry is capped at
/// [[ui::DragPayload::kMaxEntryBytes]] and text is made valid UTF-8, so a hostile source cannot hand a target more than
/// a clipboard could.
///
/// - Thread-safety: thread-compatible.
/// - Ownership: owns its entries.
class DragPayload
{
public:
    /// The most entries one payload holds.
    static constexpr usize kMaxEntries = 8;
    /// The most bytes one entry holds.
    static constexpr usize kMaxEntryBytes = os::kMaxClipboardBytes;

    /// Adds a text entry (invalid UTF-8 becomes U+FFFD).
    /// - Param text: UTF-8 text; copied.
    /// - Returns: whether it was added.
    /// - Fails: returns false when the payload is full or the text is above `kMaxEntryBytes`.
    bool AddText(StringView text);
    /// Adds a file list.
    /// - Param paths: absolute paths separated by `\n`; copied.
    /// - Returns: whether it was added.
    /// - Fails: returns false when the payload is full or the list is above `kMaxEntryBytes`.
    bool AddFiles(StringView paths);
    /// Adds an application format.
    /// - Param name: the format name; 1 to 127 bytes.
    /// - Param data: the bytes; copied.
    /// - Param size: their count.
    /// - Returns: whether it was added.
    /// - Fails: returns false when the payload is full, the name is empty or too long, or the data is above
    /// `kMaxEntryBytes`.
    bool AddCustom(StringView name, const byte* data, usize size);
    /// Adds a pointer that stays inside the process.
    /// - Param tag: names what the pointer is; 1 to 127 bytes.
    /// - Param pointer: the application's pointer; borrowed.
    /// - Returns: whether it was added.
    /// - Fails: returns false when the payload is full or the tag is empty or too long.
    /// - Ownership: borrows the pointer; the caller keeps it.
    bool AddPointer(StringView tag, void* pointer);
    /// Adds an entry announced by another program without its data.
    /// - Param kind: what it is.
    /// - Param name: `Custom`: the format name.
    /// - Returns: whether it was added.
    /// - Fails: returns false when the payload is full.
    bool AddPending(DragEntryKind kind, StringView name = StringView());

    /// Returns how many entries there are.
    usize EntryCount() const { return entries_.Size(); }
    /// Returns entry `index`.
    /// - Aborts: `index` is out of range.
    const DragEntry& Entry(usize index) const;
    /// Returns the first entry of `kind` (and of `name`, for `Custom` and `Pointer`; an empty name matches any), or
    /// null.
    /// - Ownership: borrowed; valid while the payload is unchanged.
    /// - Fails: returns null when there is none.
    const DragEntry* Find(DragEntryKind kind, StringView name = StringView()) const;
    /// Returns whether an entry of `kind` is there.
    /// - Fails: never.
    bool Has(DragEntryKind kind) const { return Find(kind) != nullptr; }
    /// Returns the text of the first text entry, or empty (also while pending).
    StringView Text() const;
    /// Returns the pointer of the `Pointer` entry tagged `tag`, or null.
    /// - Fails: returns null when there is none.
    /// - Ownership: borrows the pointer; the caller keeps it.
    void* PointerOf(StringView tag) const;
    /// Returns whether any entry can leave the process (text, files, custom) so an OS drag can carry it.
    /// - Fails: never.
    bool OsRepresentable() const;
    /// Returns whether any entry is still pending.
    /// - Fails: never.
    bool Pending() const;

    /// Sets the actions the source allows (bits of `os::DragActionBit`); default copy, move and link.
    void SetAllowed(u8 actions) { allowed_ = actions; }
    /// Returns the actions the source allows.
    u8 Allowed() const { return allowed_; }
    /// Returns whether `action` is allowed.
    /// - Fails: never.
    bool Allows(DragAction action) const { return (allowed_ & os::DragActionBit(action)) != 0; }
    /// Returns the action the user asks for: Copy with Ctrl held, Link with Ctrl+Shift, else Move when allowed, else
    /// Copy, else Link; `Nothing` when nothing is allowed. A target that accepts the drag usually answers with this.
    DragAction Requested() const { return requested_; }

private:
    friend class Surface;
    bool Add(DragEntryKind kind, StringView name, const byte* data, usize size);
    Vector<DragEntry> entries_;
    u8 allowed_ = os::kAllDragActions;
    DragAction requested_ = DragAction::Nothing;
};

/// Returns the cursor for an action: not-allowed for `Nothing`, the four-way arrow for `Move`, the hand for `Link`, the
/// arrow for `Copy` (the platform layer has no copy cursor).
os::CursorShape CursorForDragAction(DragAction action);

}  // namespace engine_placeholder::ui
