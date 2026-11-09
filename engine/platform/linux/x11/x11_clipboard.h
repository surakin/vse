#pragma once

#include "containers/vector.h"
#include "platform/clipboard.h"
#include "platform/input_event.h"
#include "types.h"

//! The X11 clipboard and primary selection for [[os::x11::X11Window]]: owning a selection and serving requests (with INCR for large data), and requesting from the owner.

struct _XDisplay;

namespace engine_placeholder::os::x11 {

/// What this window offers on one selection.
struct X11Owned {
    bool owned = false;                                  ///< This window owns the selection.
    ClipboardFormat format = ClipboardFormat::Text;      ///< What the bytes are.
    char custom_name[128] = {};                          ///< `Custom`: the atom name, NUL-terminated.
    unsigned long custom_atom = 0;                       ///< `Custom`: its atom.
    Vector<byte> bytes;                                  ///< The data (UTF-8 text, `\n`-separated paths, or raw bytes).
};

/// A request in progress on this side (we asked another program).
struct X11Incoming {
    bool active = false;
    u32 id = 0;                    ///< The request id, or 0 for the internal TARGETS query.
    ClipboardKind kind = ClipboardKind::Clipboard;
    ClipboardFormat format = ClipboardFormat::Text;
    unsigned long target = 0;      ///< The target asked for.
    unsigned long property = 0;    ///< Where the owner puts it.
    bool incr = false;             ///< The owner is sending in pieces.
    bool retried_string = false;   ///< `UTF8_STRING` was refused and `STRING` was asked.
    u64 deadline_ns = 0;           ///< For the internal query (user requests use [[os::ClipboardRequests]]).
    Vector<byte> data;             ///< The pieces so far.
};

/// A large answer being sent to another program in pieces (INCR).
struct X11Outgoing {
    bool active = false;
    ClipboardKind kind = ClipboardKind::Clipboard;
    unsigned long requestor = 0;
    unsigned long property = 0;
    unsigned long type = 0;
    usize offset = 0;
    u64 deadline_ns = 0;
    bool finished = false;         ///< The empty piece that ends the transfer was sent.
    Vector<byte> data;
};

/// The clipboard state of one X11 window. Movable; call [[os::x11::ClipboardInit]] again with the new queue address after a move.
struct X11ClipboardState {
    static constexpr u32 kSlots = 8;
    static constexpr u32 kOutgoing = 4;
    unsigned long atom_selection[static_cast<usize>(ClipboardKind::Count)] = {};
    unsigned long atom_targets = 0;
    unsigned long atom_utf8 = 0;
    unsigned long atom_text_plain = 0;
    unsigned long atom_text_plain_utf8 = 0;
    unsigned long atom_uri_list = 0;
    unsigned long atom_incr = 0;
    unsigned long atom_timestamp = 0;
    unsigned long atom_property[kSlots] = {};
    X11Owned owned[static_cast<usize>(ClipboardKind::Count)];
    X11Incoming incoming[kSlots];
    X11Outgoing outgoing[kOutgoing];
    Vector<unsigned long> cache[static_cast<usize>(ClipboardKind::Count)]; ///< The targets the other owner offers.
    bool cache_valid[static_cast<usize>(ClipboardKind::Count)] = {};
    bool xfixes = false;
    int xfixes_event_base = 0;
    ClipboardRequests requests;
    InputEventQueue* queue = nullptr;
};

/// Interns the atoms and asks for selection-owner notifications; `queue` receives the events.
void ClipboardInit(_XDisplay* display, unsigned long window, X11ClipboardState& state, InputEventQueue* queue);

/// Takes the selection (see [[os::Window::SetClipboard]]).
bool ClipboardSet(_XDisplay* display, unsigned long window, X11ClipboardState& state, ClipboardKind kind, const ClipboardOffer& offer, u64 now_ns);

/// Asks the owner (see [[os::Window::RequestClipboard]]). Returns the id or 0.
u32 ClipboardRequest(_XDisplay* display, unsigned long window, X11ClipboardState& state, ClipboardKind kind, ClipboardFormat format, StringView custom_name, u64 now_ns);

/// Whether the selection offers the format.
bool ClipboardHas(_XDisplay* display, const X11ClipboardState& state, ClipboardKind kind, ClipboardFormat format, StringView custom_name);

/// Handles a selection-related `XEvent` (passed as `void*`). Returns true if it was one.
bool ClipboardHandleEvent(_XDisplay* display, unsigned long window, X11ClipboardState& state, void* event, u64 now_ns);

/// Expires timed-out requests and stuck outgoing transfers; call once per pump.
void ClipboardTick(_XDisplay* display, X11ClipboardState& state, u64 now_ns);

/// Gives up the selections and fails the open requests; call before the display closes.
void ClipboardRelease(_XDisplay* display, X11ClipboardState& state, u64 now_ns);

} // namespace engine_placeholder::os::x11
