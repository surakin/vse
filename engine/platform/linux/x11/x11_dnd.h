#pragma once

#include "containers/vector.h"
#include "platform/drag_drop.h"
#include "platform/input_event.h"
#include "platform/linux/x11/x11_clipboard.h"
#include "types.h"

//! XDND (version 5) for [[os::x11::X11Window]]: receiving drops from other programs and starting drags to them.

struct _XDisplay;

namespace engine_placeholder::os::x11
{

/// The receiving half: what the window accepts and the drag over it, if any.
struct X11DndTarget
{
    u8 accept_formats = 0;       ///< `kDropText | kDropFiles | kDropCustom`; 0 means not `XdndAware`.
    char custom_name[128] = {};  ///< `kDropCustom`: the atom name.
    unsigned long custom_atom = 0;
    bool in_drag = false;         ///< An `XdndEnter` was received and neither `XdndLeave` nor a finished drop followed.
    bool entered = false;         ///< `DragEnter` has been queued (it carries the first position).
    unsigned long source = 0;     ///< The source window.
    int version = 0;              ///< The protocol version the source speaks (at most 5).
    Vector<unsigned long> types;  ///< The types offered, at most [[os::kMaxDragTypes]].
    u8 formats = 0;               ///< The accepted formats among them.
    u8 source_actions = 0;        ///< The actions the source allows (bits).
    DragAction answer = DragAction::Copy;  ///< The action answered in `XdndStatus`.
    bool answer_set = false;               ///< The program chose `answer` itself.
    int last_root_x = 0;
    int last_root_y = 0;
    bool dropped = false;  ///< `XdndDrop` was received; `XdndFinished` is owed.
    unsigned long drop_time = 0;
    u64 drop_deadline_ns = 0;
    DragAction drop_action = DragAction::Nothing;
};

/// The sending half: a drag this window started.
struct X11DndSource
{
    bool active = false;
    u8 allowed = kAllDragActions;
    unsigned long target = 0;     ///< The window under the pointer that speaks XDND (a toplevel or its proxy target).
    unsigned long target_to = 0;  ///< Where messages go (the `XdndProxy` window, else `target`).
    int target_version = 0;
    bool accepted = false;                    ///< The last `XdndStatus` said the target would accept.
    DragAction action = DragAction::Nothing;  ///< The action it would take.
    bool waiting_status = false;              ///< A position was sent and its status is awaited.
    bool pending_position = false;            ///< A newer position arrived meanwhile.
    int root_x = 0;
    int root_y = 0;
    unsigned long state_mask = 0;  ///< Modifier state of the latest motion (Shift and Control choose the action).
    u64 status_deadline_ns = 0;
    bool dropped = false;  ///< `XdndDrop` was sent; `XdndFinished` is awaited.
    u64 drop_deadline_ns = 0;
    bool keyboard_grabbed = false;
};

/// The XDND state of one X11 window. Movable; call [[os::x11::DndInit]] again with the new queue address after a move.
struct X11DndState
{
    unsigned long atom_aware = 0;
    unsigned long atom_enter = 0;
    unsigned long atom_position = 0;
    unsigned long atom_status = 0;
    unsigned long atom_leave = 0;
    unsigned long atom_drop = 0;
    unsigned long atom_finished = 0;
    unsigned long atom_type_list = 0;
    unsigned long atom_proxy = 0;
    unsigned long atom_action_copy = 0;
    unsigned long atom_action_move = 0;
    unsigned long atom_action_link = 0;
    unsigned long atom_action_ask = 0;
    unsigned long atom_action_private = 0;
    X11DndTarget target;
    X11DndSource source;
    InputEventQueue* queue = nullptr;
};

/// Interns the atoms; events go to `queue`.
void DndInit(_XDisplay* display, unsigned long window, X11DndState& dnd, InputEventQueue* queue);
/// Sets what drops are accepted and advertises `XdndAware`.
void DndSetAccept(_XDisplay* display, unsigned long window, X11DndState& dnd, const DropAccept& accept);
/// The program's answer to the drag over the window.
void DndSetAction(_XDisplay* display, unsigned long window, X11DndState& dnd, DragAction action);
/// Reads one format of the dropped data; returns the request id or 0.
u32 DndRequestData(_XDisplay* display, unsigned long window, X11ClipboardState& cstate, X11DndState& dnd,
                   ClipboardFormat format, StringView custom_name, u64 now_ns);
/// Sends `XdndFinished`.
void DndFinish(_XDisplay* display, unsigned long window, X11DndState& dnd, DragAction taken);
/// Starts a drag out of the window.
bool DndBegin(_XDisplay* display, unsigned long window, X11ClipboardState& cstate, X11DndState& dnd,
              const DragSource& source, u64 now_ns);
/// The pointer moved during a drag of this window.
void DndSourceMotion(_XDisplay* display, unsigned long window, X11ClipboardState& cstate, X11DndState& dnd, int root_x,
                     int root_y, unsigned long state_mask, u64 now_ns);
/// The button was released during a drag of this window: drops or ends the drag.
void DndSourceRelease(_XDisplay* display, unsigned long window, X11ClipboardState& cstate, X11DndState& dnd,
                      u64 now_ns);
/// Cancels the drag this window started.
void DndCancel(_XDisplay* display, unsigned long window, X11ClipboardState& cstate, X11DndState& dnd, u64 now_ns);
/// Handles an XDND client message (`event` is an `XEvent*`). Returns true if it was one.
bool DndHandleClientMessage(_XDisplay* display, unsigned long window, X11ClipboardState& cstate, X11DndState& dnd,
                            void* event, u64 now_ns);
/// Ends drops the program never finished and drags whose target stopped answering; call once per pump.
void DndTick(_XDisplay* display, unsigned long window, X11ClipboardState& cstate, X11DndState& dnd, u64 now_ns);
/// Ends everything before the display closes.
void DndRelease(_XDisplay* display, unsigned long window, X11ClipboardState& cstate, X11DndState& dnd, u64 now_ns);

}  // namespace engine_placeholder::os::x11
