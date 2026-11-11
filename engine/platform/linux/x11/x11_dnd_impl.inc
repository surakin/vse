// The implementation of x11_dnd.h. Included by x11_window.cpp after x11_clipboard_impl.inc (see x11_cursor_impl.inc for
// why it is not a translation unit).

namespace engine_placeholder::os::x11
{

namespace
{

// ---- helpers ------------------------------------------------------------------------------------------------------

unsigned long ActionAtom(const X11DndState& dnd, DragAction action)
{
    switch (action)
    {
    case DragAction::Copy: return dnd.atom_action_copy;
    case DragAction::Move: return dnd.atom_action_move;
    case DragAction::Link: return dnd.atom_action_link;
    default: return None;
    }
}

DragAction ActionFromAtom(const X11DndState& dnd, unsigned long atom)
{
    if (atom == dnd.atom_action_move)
    {
        return DragAction::Move;
    }
    if (atom == dnd.atom_action_link)
    {
        return DragAction::Link;
    }
    if (atom == dnd.atom_action_copy)
    {
        return DragAction::Copy;
    }
    return DragAction::Nothing;  // ask, private and unknown actions are not taken
}

void SendDndMessage(Display* display, unsigned long to, unsigned long type, unsigned long from, long d1, long d2,
                    long d3, long d4)
{
    XClientMessageEvent message = {};
    message.type = ClientMessage;
    message.display = display;
    message.window = to;
    message.message_type = type;
    message.format = 32;
    message.data.l[0] = static_cast<long>(from);
    message.data.l[1] = d1;
    message.data.l[2] = d2;
    message.data.l[3] = d3;
    message.data.l[4] = d4;
    XSendEvent(display, to, False, NoEventMask, reinterpret_cast<XEvent*>(&message));
}

u8 FormatsOf(const X11ClipboardState& cstate, const X11DndTarget& target, const Vector<unsigned long>& types)
{
    u8 formats = 0;
    for (usize i = 0; i < types.Size(); ++i)
    {
        const unsigned long t = types[i];
        if (t == cstate.atom_utf8 || t == cstate.atom_text_plain_utf8 || t == cstate.atom_text_plain || t == XA_STRING)
        {
            formats |= kDropText;
        }
        if (t == cstate.atom_uri_list)
        {
            formats |= kDropFiles;
        }
        if (target.custom_atom != 0 && t == target.custom_atom)
        {
            formats |= kDropCustom;
        }
    }
    return formats;
}

void QueueDrag(X11DndState& dnd, InputEventType type, int x, int y, u8 formats, u8 actions, DragAction action,
               u64 now_ns)
{
    if (dnd.queue == nullptr)
    {
        return;
    }
    InputEvent event;
    event.type = type;
    event.x = x;
    event.y = y;
    event.drag_formats = formats;
    event.drag_actions = actions;
    event.drag_action = static_cast<u8>(action);
    event.time_ns = now_ns;
    dnd.queue->Push(event);
}

void SendStatus(Display* display, unsigned long window, const X11DndState& dnd)
{
    const X11DndTarget& t = dnd.target;
    const bool accept = (t.formats & t.accept_formats) != 0 && t.answer != DragAction::Nothing;
    SendDndMessage(display, t.source, dnd.atom_status, window, accept ? 1 : 0, 0, 0,
                   accept ? static_cast<long>(ActionAtom(dnd, t.answer)) : 0);
    XFlush(display);
}

DragAction DefaultAnswer(const X11DndTarget& t, DragAction requested)
{
    if ((t.formats & t.accept_formats) == 0)
    {
        return DragAction::Nothing;
    }
    const u8 allowed = t.source_actions == 0 ? kAllDragActions : t.source_actions;
    if (requested != DragAction::Nothing && (allowed & DragActionBit(requested)) != 0)
    {
        return requested;
    }
    for (DragAction candidate : {DragAction::Copy, DragAction::Move, DragAction::Link})
    {
        if ((allowed & DragActionBit(candidate)) != 0)
        {
            return candidate;
        }
    }
    return DragAction::Nothing;
}

void ResetTarget(X11DndTarget& t)
{
    t.in_drag = false;
    t.entered = false;
    t.source = 0;
    t.types.Reset();
    t.formats = 0;
    t.answer_set = false;
    t.dropped = false;
}

void FinishDropNow(Display* display, unsigned long window, X11DndState& dnd, DragAction taken)
{
    X11DndTarget& t = dnd.target;
    if (!t.dropped)
    {
        return;
    }
    if (t.version >= 2)
    {
        SendDndMessage(display, t.source, dnd.atom_finished, window, taken != DragAction::Nothing ? 1 : 0,
                       taken != DragAction::Nothing ? static_cast<long>(ActionAtom(dnd, taken)) : 0, 0, 0);
        XFlush(display);
    }
    ResetTarget(t);
}

}  // namespace

// ---- setup --------------------------------------------------------------------------------------------------------

void DndInit(_XDisplay* display, unsigned long, X11DndState& dnd, InputEventQueue* queue)
{
    dnd.queue = queue;
    if (dnd.atom_aware != 0)
    {
        return;
    }
    dnd.atom_aware = XInternAtom(display, "XdndAware", False);
    dnd.atom_enter = XInternAtom(display, "XdndEnter", False);
    dnd.atom_position = XInternAtom(display, "XdndPosition", False);
    dnd.atom_status = XInternAtom(display, "XdndStatus", False);
    dnd.atom_leave = XInternAtom(display, "XdndLeave", False);
    dnd.atom_drop = XInternAtom(display, "XdndDrop", False);
    dnd.atom_finished = XInternAtom(display, "XdndFinished", False);
    dnd.atom_type_list = XInternAtom(display, "XdndTypeList", False);
    dnd.atom_proxy = XInternAtom(display, "XdndProxy", False);
    dnd.atom_action_copy = XInternAtom(display, "XdndActionCopy", False);
    dnd.atom_action_move = XInternAtom(display, "XdndActionMove", False);
    dnd.atom_action_link = XInternAtom(display, "XdndActionLink", False);
    dnd.atom_action_ask = XInternAtom(display, "XdndActionAsk", False);
    dnd.atom_action_private = XInternAtom(display, "XdndActionPrivate", False);
}

void DndSetAccept(_XDisplay* display, unsigned long window, X11DndState& dnd, const DropAccept& accept)
{
    X11DndTarget& t = dnd.target;
    t.accept_formats = accept.formats & (kDropText | kDropFiles | kDropCustom);
    t.custom_name[0] = '\0';
    t.custom_atom = 0;
    if ((t.accept_formats & kDropCustom) != 0)
    {
        if (accept.custom_name.Length() == 0 || accept.custom_name.Length() >= sizeof(t.custom_name))
        {
            t.accept_formats &= static_cast<u8>(~kDropCustom);
        }
        else
        {
            for (usize i = 0; i < accept.custom_name.Length(); ++i)
            {
                t.custom_name[i] = accept.custom_name[i];
            }
            t.custom_name[accept.custom_name.Length()] = '\0';
            t.custom_atom = XInternAtom(display, t.custom_name, False);
        }
    }
    if (t.accept_formats != 0)
    {
        const long version = 5;
        XChangeProperty(display, window, dnd.atom_aware, XA_ATOM, 32, PropModeReplace,
                        reinterpret_cast<const byte*>(&version), 1);
    }
    else
    {
        XDeleteProperty(display, window, dnd.atom_aware);  // not aware: other programs refuse the drag at once
    }
    XFlush(display);
}

void DndSetAction(_XDisplay* display, unsigned long window, X11DndState& dnd, DragAction action)
{
    X11DndTarget& t = dnd.target;
    if (!t.in_drag || t.dropped)
    {
        return;
    }
    XErrorScope errors(display);  // the source window may be gone
    t.answer = action;
    t.answer_set = true;
    SendStatus(display, window, dnd);  // the new answer reaches the source at once, not only at its next position
}

u32 DndRequestData(_XDisplay* display, unsigned long window, X11ClipboardState& cstate, X11DndState& dnd,
                   ClipboardFormat format, StringView custom_name, u64 now_ns)
{
    X11DndTarget& t = dnd.target;
    if (!t.dropped)
    {
        return 0;
    }
    if (format == ClipboardFormat::Custom && (custom_name.Length() == 0 || !(StringView(t.custom_name) == custom_name)))
    {
        return 0;  // only the accepted custom format is readable
    }
    u32 slot = X11ClipboardState::kSlots;
    for (u32 i = 0; i < X11ClipboardState::kSlots; ++i)
    {
        if (!cstate.incoming[i].active)
        {
            slot = i;
            break;
        }
    }
    if (slot == X11ClipboardState::kSlots)
    {
        return 0;
    }
    // The target atom: the best type the source listed for this format.
    unsigned long target = 0;
    auto offered = [&t](unsigned long atom)
    {
        for (usize i = 0; i < t.types.Size(); ++i)
        {
            if (t.types[i] == atom)
            {
                return true;
            }
        }
        return false;
    };
    if (format == ClipboardFormat::Text)
    {
        for (unsigned long candidate : {cstate.atom_utf8, cstate.atom_text_plain_utf8,
                                        static_cast<unsigned long>(XA_STRING), cstate.atom_text_plain})
        {
            if (target == 0 && offered(candidate))
            {
                target = candidate;
            }
        }
    }
    else if (format == ClipboardFormat::FileList)
    {
        if (offered(cstate.atom_uri_list))
        {
            target = cstate.atom_uri_list;
        }
    }
    else if (offered(t.custom_atom))
    {
        target = t.custom_atom;
    }
    if (target == 0)
    {
        return 0;
    }
    const u32 id = cstate.requests.Begin(ClipboardKind::Clipboard, format, now_ns, InputEventType::DropData);
    if (id == 0)
    {
        return 0;
    }
    X11Incoming& in = cstate.incoming[slot];
    in = X11Incoming();
    in.active = true;
    in.id = id;
    in.drop = true;
    in.time = t.drop_time;
    in.format = format;
    in.target = target;
    in.property = cstate.atom_property[slot];
    XConvertSelection(display, cstate.atom_xdnd_selection, in.target, in.property, window, in.time);
    XFlush(display);
    return id;
}

void DndFinish(_XDisplay* display, unsigned long window, X11DndState& dnd, DragAction taken)
{
    XErrorScope errors(display);  // the source window may be gone
    FinishDropNow(display, window, dnd, taken);
}

// ---- receiving ----------------------------------------------------------------------------------------------------

namespace
{

void HandleEnter(Display* display, unsigned long window, X11ClipboardState& cstate, X11DndState& dnd,
                 const XClientMessageEvent& m, u64 now_ns)
{
    X11DndTarget& t = dnd.target;
    if (t.dropped)
    {
        FinishDropNow(display, window, dnd, DragAction::Nothing);  // a new drag while the last drop was never finished
    }
    if (t.in_drag && t.entered)
    {
        QueueDrag(dnd, InputEventType::DragLeave, 0, 0, 0, 0, DragAction::Nothing,
                  now_ns);  // the last drag never said it left
    }
    ResetTarget(t);
    t.in_drag = true;
    t.source = static_cast<unsigned long>(m.data.l[0]);
    t.version = static_cast<int>((m.data.l[1] >> 24) & 0xFF);
    if (t.version > 5)
    {
        t.version = 5;
    }
    if ((m.data.l[1] & 1) != 0)
    {
        // More than three types: they are in the source's XdndTypeList property.
        XErrorScope errors(display);
        Atom type = 0;
        int format = 0;
        unsigned long count = 0;
        unsigned long after = 0;
        byte* data = nullptr;
        if (XGetWindowProperty(display, t.source, dnd.atom_type_list, 0, kMaxDragTypes, False, XA_ATOM, &type, &format,
                               &count, &after, &data) == Success &&
            data != nullptr)
        {
            // Only a 32-bit ATOM list is read as longs; another format from a hostile source would be read past its
            // end.
            if (type == XA_ATOM && format == 32)
            {
                const unsigned long* atoms = reinterpret_cast<const unsigned long*>(data);
                for (unsigned long i = 0; i < count && t.types.Size() < kMaxDragTypes; ++i)
                {
                    t.types.Push(atoms[i]);
                }
            }
            XFree(data);
        }
    }
    else
    {
        for (int i = 2; i < 5; ++i)
        {
            if (m.data.l[i] != 0)
            {
                t.types.Push(static_cast<unsigned long>(m.data.l[i]));
            }
        }
    }
    t.formats = FormatsOf(cstate, t, t.types);
    t.answer_set = false;
}

void HandlePosition(Display* display, unsigned long window, X11DndState& dnd, const XClientMessageEvent& m, u64 now_ns)
{
    X11DndTarget& t = dnd.target;
    if (!t.in_drag || t.dropped || static_cast<unsigned long>(m.data.l[0]) != t.source)
    {
        return;
    }
    // Root coordinates are signed 16-bit halves (a monitor left of or above the origin has negative ones).
    const int root_x = static_cast<int>(static_cast<i16>((m.data.l[2] >> 16) & 0xFFFF));
    const int root_y = static_cast<int>(static_cast<i16>(m.data.l[2] & 0xFFFF));
    t.last_root_x = root_x;
    t.last_root_y = root_y;
    const DragAction requested = ActionFromAtom(dnd, static_cast<unsigned long>(m.data.l[4]));
    t.source_actions = kAllDragActions;  // XDND names only the action wanted now, not the set the source allows
    if (!t.answer_set)
    {
        t.answer = DefaultAnswer(t, requested);
    }
    int x = 0, y = 0;
    unsigned long child = None;
    XTranslateCoordinates(display, DefaultRootWindow(display), window, root_x, root_y, &x, &y, &child);
    if ((t.formats & t.accept_formats) == 0)
    {
        t.answer = DragAction::Nothing;
        SendStatus(display, window, dnd);  // nothing wanted is on offer: refused without bothering the program
        return;
    }
    SendStatus(display, window, dnd);
    if (!t.entered)
    {
        t.entered = true;
        QueueDrag(dnd, InputEventType::DragEnter, x, y, t.formats & t.accept_formats, t.source_actions, t.answer,
                  now_ns);
    }
    else
    {
        QueueDrag(dnd, InputEventType::DragOver, x, y, t.formats & t.accept_formats, t.source_actions, t.answer,
                  now_ns);
    }
}

void HandleDrop(Display* display, unsigned long window, X11DndState& dnd, const XClientMessageEvent& m, u64 now_ns)
{
    X11DndTarget& t = dnd.target;
    if (!t.in_drag || t.dropped || static_cast<unsigned long>(m.data.l[0]) != t.source)
    {
        return;
    }
    if ((t.formats & t.accept_formats) == 0 || t.answer == DragAction::Nothing)
    {
        // Refused: the drop is answered at once.
        t.dropped = true;
        FinishDropNow(display, window, dnd, DragAction::Nothing);
        return;
    }
    t.dropped = true;
    t.drop_time = static_cast<unsigned long>(m.data.l[2]);
    t.drop_deadline_ns = now_ns + kDropSessionTimeoutNs;
    t.drop_action = t.answer;
    int x = 0, y = 0;
    unsigned long child = None;
    XTranslateCoordinates(display, DefaultRootWindow(display), window, t.last_root_x, t.last_root_y, &x, &y, &child);
    QueueDrag(dnd, InputEventType::Drop, x, y, t.formats & t.accept_formats, t.source_actions, t.drop_action, now_ns);
}

}  // namespace

// ---- sending ------------------------------------------------------------------------------------------------------

namespace
{

bool IsAware(Display* display, X11DndState& dnd, unsigned long window, int* version)
{
    Atom type = 0;
    int format = 0;
    unsigned long count = 0;
    unsigned long after = 0;
    byte* data = nullptr;
    bool aware = false;
    if (XGetWindowProperty(display, window, dnd.atom_aware, 0, 1, False, XA_ATOM, &type, &format, &count, &after,
                           &data) == Success &&
        data != nullptr)
    {
        if (type == XA_ATOM && format == 32 && count >= 1)
        {
            *version = static_cast<int>(*reinterpret_cast<unsigned long*>(data));
            aware = *version >=
                    3;  // below version 3 there is no XdndTypeList and no proxy; version 3 and up are handled alike
        }
        XFree(data);
    }
    return aware;
}

// Finds the XDND-aware window under (rx, ry): walks from the root through the children containing the point and keeps
// the last aware window.
bool FindTarget(Display* display, X11DndState& dnd, int rx, int ry, unsigned long* target, unsigned long* send_to,
                int* version)
{
    XErrorScope errors(display);
    const unsigned long root = DefaultRootWindow(display);
    unsigned long w = root;
    bool found = false;
    for (int depth = 0; depth < 32 && w != None; ++depth)
    {
        int v = 0;
        if (IsAware(display, dnd, w, &v))
        {
            *target = w;
            *send_to = w;
            *version = v > 5 ? 5 : v;
            found = true;
            // A proxy receives the messages in the target's place (the target's XdndAware still says the version).
            Atom type = 0;
            int format = 0;
            unsigned long count = 0;
            unsigned long after = 0;
            byte* data = nullptr;
            if (XGetWindowProperty(display, w, dnd.atom_proxy, 0, 1, False, XA_WINDOW, &type, &format, &count, &after,
                                   &data) == Success &&
                data != nullptr)
            {
                if (type == XA_WINDOW && format == 32 && count >= 1)
                {
                    const unsigned long proxy = *reinterpret_cast<unsigned long*>(data);
                    int pv = 0;
                    if (proxy != None && IsAware(display, dnd, proxy, &pv))
                    {
                        *send_to = proxy;
                    }
                }
                XFree(data);
            }
        }
        int dx = 0, dy = 0;
        unsigned long child = None;
        if (XTranslateCoordinates(display, root, w, rx, ry, &dx, &dy, &child) == 0)
        {
            break;
        }
        w = child;
    }
    return found;
}

void SendPosition(Display* display, unsigned long window, X11DndState& dnd, u64 now_ns)
{
    X11DndSource& s = dnd.source;
    DragAction wanted = DragAction::Copy;
    if ((s.state_mask & ShiftMask) != 0 && (s.allowed & DragActionBit(DragAction::Move)) != 0)
    {
        wanted = DragAction::Move;
    }
    else if ((s.state_mask & ControlMask) != 0 && (s.allowed & DragActionBit(DragAction::Copy)) != 0)
    {
        wanted = DragAction::Copy;
    }
    else if ((s.allowed & DragActionBit(DragAction::Copy)) == 0)
    {
        wanted = (s.allowed & DragActionBit(DragAction::Move)) != 0 ? DragAction::Move : DragAction::Link;
    }
    SendDndMessage(display, s.target_to, dnd.atom_position, window, 0,
                   ((static_cast<long>(s.root_x) & 0xFFFF) << 16) | (static_cast<long>(s.root_y) & 0xFFFF), 0,
                   static_cast<long>(ActionAtom(dnd, wanted)));
    s.waiting_status = true;
    s.pending_position = false;
    s.status_deadline_ns = now_ns + kDragTargetTimeoutNs;
    XFlush(display);
}

void EndSource(Display* display, unsigned long window, X11ClipboardState& cstate, X11DndState& dnd, DragAction result,
               u64 now_ns)
{
    X11DndSource& s = dnd.source;
    if (!s.active)
    {
        return;
    }
    XErrorScope errors(display);
    XUngrabPointer(display, CurrentTime);
    if (s.keyboard_grabbed)
    {
        XUngrabKeyboard(display, CurrentTime);
    }
    if (XGetSelectionOwner(display, cstate.atom_xdnd_selection) == window)
    {
        XSetSelectionOwner(display, cstate.atom_xdnd_selection, None, CurrentTime);
    }
    XDeleteProperty(display, window, dnd.atom_type_list);
    for (X11Owned& offer : cstate.drag_offers)
    {
        offer = X11Owned();
    }
    cstate.drag_offer_count = 0;
    cstate.drag_active = false;
    s = X11DndSource();
    QueueDrag(dnd, InputEventType::DragEnded, 0, 0, 0, 0, result, now_ns);
}

void LeaveTarget(Display* display, unsigned long window, X11DndState& dnd)
{
    X11DndSource& s = dnd.source;
    if (s.target == 0)
    {
        return;
    }
    XErrorScope errors(display);
    SendDndMessage(display, s.target_to, dnd.atom_leave, window, 0, 0, 0, 0);
    XFlush(display);
    s.target = 0;
    s.target_to = 0;
    s.accepted = false;
    s.waiting_status = false;
    s.pending_position = false;
}

}  // namespace

bool DndBegin(_XDisplay* display, unsigned long window, X11ClipboardState& cstate, X11DndState& dnd,
              const DragSource& source, u64)
{
    X11DndSource& s = dnd.source;
    if (display == nullptr || s.active || source.offers == nullptr || source.offer_count == 0 ||
        source.offer_count > kMaxDragOffers)
    {
        return false;
    }
    for (usize i = 0; i < source.offer_count; ++i)
    {
        const ClipboardOffer& o = source.offers[i];
        if (o.size > kMaxClipboardBytes || (o.size > 0 && o.data == nullptr))
        {
            return false;
        }
        if (o.format == ClipboardFormat::Custom &&
            (o.custom_name.Length() == 0 || o.custom_name.Length() >= sizeof(cstate.drag_offers[0].custom_name)))
        {
            return false;
        }
    }
    for (usize i = 0; i < source.offer_count; ++i)
    {
        const ClipboardOffer& o = source.offers[i];
        X11Owned& out = cstate.drag_offers[i];
        out = X11Owned();
        out.owned = true;
        out.format = o.format;
        if (o.format == ClipboardFormat::Text)
        {
            SanitizeUtf8(o.data, o.size, out.bytes, nullptr);
        }
        else
        {
            for (usize k = 0; k < o.size; ++k)
            {
                out.bytes.Push(o.data[k]);
            }
        }
        if (o.format == ClipboardFormat::Custom)
        {
            for (usize k = 0; k < o.custom_name.Length(); ++k)
            {
                out.custom_name[k] = o.custom_name[k];
            }
            out.custom_atom = XInternAtom(display, out.custom_name, False);
        }
    }
    cstate.drag_offer_count = source.offer_count;
    // The pointer grab makes every motion and the release come to this window.
    if (XGrabPointer(display, DefaultRootWindow(display), False,
                     ButtonReleaseMask | PointerMotionMask | ButtonPressMask, GrabModeAsync, GrabModeAsync, None, None,
                     CurrentTime) != GrabSuccess)
    {
        for (X11Owned& offer : cstate.drag_offers)
        {
            offer = X11Owned();
        }
        cstate.drag_offer_count = 0;
        return false;
    }
    s = X11DndSource();
    s.active = true;
    s.allowed = source.allowed_actions & kAllDragActions;
    if (s.allowed == 0)
    {
        s.allowed = DragActionBit(DragAction::Copy);
    }
    s.keyboard_grabbed = XGrabKeyboard(display, DefaultRootWindow(display), False, GrabModeAsync, GrabModeAsync,
                                       CurrentTime) == GrabSuccess;
    cstate.drag_active = true;
    XSetSelectionOwner(display, cstate.atom_xdnd_selection, window, CurrentTime);
    // The types, in XdndTypeList (read when there are more than three).
    unsigned long types[2 + 4 * kMaxDragOffers];
    int type_count = 0;
    for (usize i = 0; i < cstate.drag_offer_count; ++i)
    {
        const X11Owned& o = cstate.drag_offers[i];
        if (o.format == ClipboardFormat::Text)
        {
            types[type_count++] = cstate.atom_utf8;
            types[type_count++] = cstate.atom_text_plain_utf8;
            types[type_count++] = cstate.atom_text_plain;
            types[type_count++] = XA_STRING;
        }
        else if (o.format == ClipboardFormat::FileList)
        {
            types[type_count++] = cstate.atom_uri_list;
        }
        else
        {
            types[type_count++] = o.custom_atom;
        }
    }
    XChangeProperty(display, window, dnd.atom_type_list, XA_ATOM, 32, PropModeReplace,
                    reinterpret_cast<const byte*>(types), type_count);
    XFlush(display);
    return true;
}

namespace
{

// The types announced in XdndEnter: three inline, or a flag when there are more.
void SendEnter(Display* display, unsigned long window, X11ClipboardState& cstate, X11DndState& dnd)
{
    X11DndSource& s = dnd.source;
    unsigned long types[2 + 4 * kMaxDragOffers];
    int count = 0;
    for (usize i = 0; i < cstate.drag_offer_count; ++i)
    {
        const X11Owned& o = cstate.drag_offers[i];
        if (o.format == ClipboardFormat::Text)
        {
            types[count++] = cstate.atom_utf8;
            types[count++] = cstate.atom_text_plain_utf8;
            types[count++] = cstate.atom_text_plain;
            types[count++] = XA_STRING;
        }
        else if (o.format == ClipboardFormat::FileList)
        {
            types[count++] = cstate.atom_uri_list;
        }
        else
        {
            types[count++] = o.custom_atom;
        }
    }
    const bool many = count > 3;
    const int version = s.target_version < 5 ? s.target_version : 5;
    SendDndMessage(display, s.target_to, dnd.atom_enter, window, (static_cast<long>(version) << 24) | (many ? 1 : 0),
                   many ? 0 : (count > 0 ? static_cast<long>(types[0]) : 0),
                   many ? 0 : (count > 1 ? static_cast<long>(types[1]) : 0),
                   many ? 0 : (count > 2 ? static_cast<long>(types[2]) : 0));
    XFlush(display);
}

}  // namespace

void DndSourceMotion(_XDisplay* display, unsigned long window, X11ClipboardState& cstate, X11DndState& dnd, int root_x,
                     int root_y, unsigned long state_mask, u64 now_ns)
{
    X11DndSource& s = dnd.source;
    if (!s.active || s.dropped)
    {
        return;
    }
    s.root_x = root_x;
    s.root_y = root_y;
    s.state_mask = state_mask;
    unsigned long target = 0, send_to = 0;
    int version = 0;
    const bool found = FindTarget(display, dnd, root_x, root_y, &target, &send_to, &version);
    XErrorScope errors(display);
    if (!found || target != s.target)
    {
        LeaveTarget(display, window, dnd);
        if (found)
        {
            s.target = target;
            s.target_to = send_to;
            s.target_version = version;
            SendEnter(display, window, cstate, dnd);
            SendPosition(display, window, dnd, now_ns);
        }
        return;
    }
    if (s.waiting_status)
    {
        s.pending_position = true;  // sent after the status arrives
    }
    else
    {
        SendPosition(display, window, dnd, now_ns);
    }
}

void DndSourceRelease(_XDisplay* display, unsigned long window, X11ClipboardState& cstate, X11DndState& dnd, u64 now_ns)
{
    X11DndSource& s = dnd.source;
    if (!s.active || s.dropped)
    {
        return;
    }
    XErrorScope errors(display);
    XUngrabPointer(display, CurrentTime);
    if (s.target != 0 && s.accepted)
    {
        s.dropped = true;
        s.drop_deadline_ns = now_ns + kDragTargetTimeoutNs;
        SendDndMessage(display, s.target_to, dnd.atom_drop, window, 0, static_cast<long>(CurrentTime), 0, 0);
        XFlush(display);
        return;
    }
    LeaveTarget(display, window, dnd);
    EndSource(display, window, cstate, dnd, DragAction::Nothing, now_ns);
}

void DndCancel(_XDisplay* display, unsigned long window, X11ClipboardState& cstate, X11DndState& dnd, u64 now_ns)
{
    X11DndSource& s = dnd.source;
    if (!s.active)
    {
        return;
    }
    if (!s.dropped)
    {
        LeaveTarget(display, window, dnd);
    }
    EndSource(display, window, cstate, dnd, DragAction::Nothing, now_ns);
}

// ---- events and time ----------------------------------------------------------------------------------------------

bool DndHandleClientMessage(_XDisplay* display, unsigned long window, X11ClipboardState& cstate, X11DndState& dnd,
                            void* event_ptr, u64 now_ns)
{
    const XClientMessageEvent& m = static_cast<XEvent*>(event_ptr)->xclient;
    if (m.message_type != dnd.atom_enter && m.message_type != dnd.atom_position && m.message_type != dnd.atom_leave &&
        m.message_type != dnd.atom_drop && m.message_type != dnd.atom_status && m.message_type != dnd.atom_finished)
    {
        return false;
    }
    XErrorScope errors(display);  // the other program's window may be gone
    if (m.message_type == dnd.atom_enter)
    {
        HandleEnter(display, window, cstate, dnd, m, now_ns);
        return true;
    }
    if (m.message_type == dnd.atom_position)
    {
        HandlePosition(display, window, dnd, m, now_ns);
        return true;
    }
    if (m.message_type == dnd.atom_leave)
    {
        X11DndTarget& t = dnd.target;
        if (t.in_drag && !t.dropped && static_cast<unsigned long>(m.data.l[0]) == t.source)
        {
            if (t.entered)
            {
                QueueDrag(dnd, InputEventType::DragLeave, 0, 0, 0, 0, DragAction::Nothing, now_ns);
            }
            ResetTarget(t);
        }
        return true;
    }
    if (m.message_type == dnd.atom_drop)
    {
        HandleDrop(display, window, dnd, m, now_ns);
        return true;
    }
    if (m.message_type == dnd.atom_status)
    {
        X11DndSource& s = dnd.source;
        if (s.active && s.target_to != 0 && static_cast<unsigned long>(m.data.l[0]) == s.target)
        {
            s.accepted = (m.data.l[1] & 1) != 0;
            s.action = s.accepted ? ActionFromAtom(dnd, static_cast<unsigned long>(m.data.l[4])) : DragAction::Nothing;
            if (s.action == DragAction::Nothing)
            {
                s.accepted = false;
            }
            s.waiting_status = false;
            if (s.pending_position && !s.dropped)
            {
                SendPosition(display, window, dnd, now_ns);
            }
        }
        return true;
    }
    if (m.message_type == dnd.atom_finished)
    {
        X11DndSource& s = dnd.source;
        if (s.active && s.dropped && static_cast<unsigned long>(m.data.l[0]) == s.target)
        {
            const DragAction taken = (m.data.l[1] & 1) != 0
                                         ? ActionFromAtom(dnd, static_cast<unsigned long>(m.data.l[2]))
                                         : DragAction::Nothing;
            EndSource(display, window, cstate, dnd, taken, now_ns);
        }
        return true;
    }
    return false;
}

void DndTick(_XDisplay* display, unsigned long window, X11ClipboardState& cstate, X11DndState& dnd, u64 now_ns)
{
    X11DndTarget& t = dnd.target;
    if (t.dropped && now_ns >= t.drop_deadline_ns)
    {
        XErrorScope errors(display);
        FinishDropNow(display, window, dnd, DragAction::Nothing);  // the program never finished the drop
    }
    X11DndSource& s = dnd.source;
    if (!s.active)
    {
        return;
    }
    XErrorScope source_errors(display);  // the target window may be gone
    if (s.dropped && now_ns >= s.drop_deadline_ns)
    {
        EndSource(display, window, cstate, dnd, DragAction::Nothing,
                  now_ns);  // the target did not answer: the drag is cancelled
    }
    else if (s.waiting_status && now_ns >= s.status_deadline_ns)
    {
        s.waiting_status = false;  // a target that stops answering takes no drop
        s.accepted = false;
        if (s.pending_position && !s.dropped)
        {
            SendPosition(display, window, dnd, now_ns);
        }
    }
}

void DndRelease(_XDisplay* display, unsigned long window, X11ClipboardState& cstate, X11DndState& dnd, u64 now_ns)
{
    if (display == nullptr)
    {
        return;
    }
    XErrorScope errors(display);
    if (dnd.target.dropped)
    {
        FinishDropNow(display, window, dnd, DragAction::Nothing);
    }
    DndCancel(display, window, cstate, dnd, now_ns);
}

}  // namespace engine_placeholder::os::x11
