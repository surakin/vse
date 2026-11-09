// The implementation of x11_clipboard.h. Included by x11_window.cpp inside its X11 region, not a translation unit of its own (see x11_cursor_impl.inc).
#include <X11/extensions/Xfixes.h>

namespace engine_placeholder::os::x11 {

namespace {

constexpr usize kIncrThreshold = 256 * 1024; // answers above this go in pieces
constexpr usize kIncrChunk = 128 * 1024;

usize KindIndex(ClipboardKind kind) { return static_cast<usize>(kind); }

// Answering a request means writing to a window of another program, which may be gone by now; Xlib's default error handler would then end our process. For the
// duration of the scope errors are swallowed (the answer is simply lost), and the scope's end waits for the server so they arrive inside it.
int g_clipboard_x_errors = 0;
int SwallowXError(Display*, XErrorEvent*) {
    ++g_clipboard_x_errors;
    return 0;
}
class XErrorScope {
public:
    explicit XErrorScope(Display* display) : display_(display), previous_(XSetErrorHandler(SwallowXError)) {}
    ~XErrorScope() {
        XSync(display_, False);
        XSetErrorHandler(previous_);
    }
    XErrorScope(const XErrorScope&) = delete;
    XErrorScope& operator=(const XErrorScope&) = delete;

private:
    Display* display_;
    int (*previous_)(Display*, XErrorEvent*);
};

bool KindOf(const X11ClipboardState& state, unsigned long selection, ClipboardKind* kind) {
    for (usize i = 0; i < static_cast<usize>(ClipboardKind::Count); ++i) {
        if (state.atom_selection[i] == selection) {
            *kind = static_cast<ClipboardKind>(i);
            return true;
        }
    }
    return false;
}

void QueueChanged(X11ClipboardState& state, ClipboardKind kind, u64 now_ns) {
    if (state.queue == nullptr) return;
    InputEvent event;
    event.type = InputEventType::ClipboardChanged;
    event.clipboard_kind = static_cast<u8>(kind);
    event.time_ns = now_ns;
    state.queue->Push(event);
}

// Builds the bytes for `target` from what we own; false if we do not offer it.
bool PayloadFor(Display* display, const X11ClipboardState& state, const X11Owned& owned, unsigned long target, Vector<byte>& payload, unsigned long* type) {
    if (owned.format == ClipboardFormat::Text) {
        if (target == state.atom_utf8 || target == state.atom_text_plain_utf8 || target == state.atom_text_plain) {
            payload.Clear();
            for (usize i = 0; i < owned.bytes.Size(); ++i) payload.Push(owned.bytes[i]);
            *type = target == state.atom_utf8 ? state.atom_utf8 : target;
            return true;
        }
        if (target == XA_STRING) {
            *type = XA_STRING;
            return Utf8ToLatin1(owned.bytes.Data(), owned.bytes.Size(), payload); // refused when not lossless
        }
        return false;
    }
    if (owned.format == ClipboardFormat::FileList) {
        if (target == state.atom_uri_list) {
            BuildUriList(owned.bytes.Data(), owned.bytes.Size(), payload);
            *type = target;
            return true;
        }
        return false;
    }
    if (target == owned.custom_atom && target != 0) {
        payload.Clear();
        for (usize i = 0; i < owned.bytes.Size(); ++i) payload.Push(owned.bytes[i]);
        *type = target;
        return true;
    }
    (void)display;
    return false;
}

void SendNotify(Display* display, const XSelectionRequestEvent& request, unsigned long property) {
    XSelectionEvent reply = {};
    reply.type = SelectionNotify;
    reply.display = display;
    reply.requestor = request.requestor;
    reply.selection = request.selection;
    reply.target = request.target;
    reply.property = property; // None refuses
    reply.time = request.time;
    XSendEvent(display, request.requestor, False, NoEventMask, reinterpret_cast<XEvent*>(&reply));
    XFlush(display);
}

void SendChunk(Display* display, X11Outgoing& out) {
    const usize remaining = out.data.Size() - out.offset;
    const usize n = remaining < kIncrChunk ? remaining : kIncrChunk;
    XChangeProperty(display, out.requestor, out.property, out.type, 8, PropModeReplace, out.data.Data() + out.offset, static_cast<int>(n));
    out.offset += n;
    if (n == 0) out.finished = true; // the empty piece ends the transfer
    XFlush(display);
}

void HandleSelectionRequest(Display* display, unsigned long own_window, X11ClipboardState& state, const XSelectionRequestEvent& request, u64 now_ns) {
    XErrorScope errors(display);
    ClipboardKind kind = ClipboardKind::Clipboard;
    unsigned long property = request.property != None ? request.property : request.target;
    // What is on offer on this selection: the clipboard's one offer, or the offers of a running drag (XdndSelection).
    const X11Owned* offers[kMaxDragOffers];
    usize offer_count = 0;
    if (request.selection == state.atom_xdnd_selection) {
        if (state.drag_active) {
            for (usize i = 0; i < state.drag_offer_count; ++i) offers[offer_count++] = &state.drag_offers[i];
        }
    } else if (KindOf(state, request.selection, &kind) && state.owned[KindIndex(kind)].owned) {
        offers[offer_count++] = &state.owned[KindIndex(kind)];
    }
    if (offer_count == 0) {
        SendNotify(display, request, None);
        return;
    }
    if (request.target == state.atom_targets) {
        unsigned long atoms[2 + 6 * kMaxDragOffers];
        int count = 0;
        atoms[count++] = state.atom_targets;
        atoms[count++] = state.atom_timestamp;
        for (usize i = 0; i < offer_count; ++i) {
            const X11Owned& owned = *offers[i];
            if (owned.format == ClipboardFormat::Text) {
                atoms[count++] = state.atom_utf8;
                atoms[count++] = XA_STRING;
                atoms[count++] = state.atom_text_plain_utf8;
                atoms[count++] = state.atom_text_plain;
            } else if (owned.format == ClipboardFormat::FileList) {
                atoms[count++] = state.atom_uri_list;
            } else {
                atoms[count++] = owned.custom_atom;
            }
        }
        XChangeProperty(display, request.requestor, property, XA_ATOM, 32, PropModeReplace, reinterpret_cast<const unsigned char*>(atoms), count);
        SendNotify(display, request, property);
        return;
    }
    if (request.target == state.atom_timestamp) {
        long time = static_cast<long>(request.time);
        XChangeProperty(display, request.requestor, property, XA_INTEGER, 32, PropModeReplace, reinterpret_cast<const unsigned char*>(&time), 1);
        SendNotify(display, request, property);
        return;
    }
    Vector<byte> payload;
    unsigned long type = 0;
    bool found = false;
    for (usize i = 0; i < offer_count && !found; ++i) found = PayloadFor(display, state, *offers[i], request.target, payload, &type);
    if (!found || payload.Size() > kMaxClipboardBytes) {
        SendNotify(display, request, None);
        return;
    }
    if (payload.Size() <= kIncrThreshold) {
        XChangeProperty(display, request.requestor, property, type, 8, PropModeReplace, payload.Data(), static_cast<int>(payload.Size()));
        SendNotify(display, request, property);
        return;
    }
    // INCR: announce the size, then send a piece each time the requestor deletes the property.
    X11Outgoing* slot = nullptr;
    for (X11Outgoing& o : state.outgoing) {
        if (!o.active) {
            slot = &o;
            break;
        }
    }
    if (slot == nullptr) {
        SendNotify(display, request, None);
        return;
    }
    slot->active = true;
    slot->kind = kind;
    slot->requestor = request.requestor;
    slot->property = property;
    slot->type = type;
    slot->offset = 0;
    slot->finished = false;
    slot->deadline_ns = now_ns + kClipboardTimeoutNs * 2;
    slot->data = static_cast<Vector<byte>&&>(payload);
    if (request.requestor != own_window) {
        XSelectInput(display, request.requestor, PropertyChangeMask); // to see the requestor delete the property
    }
    long size = static_cast<long>(slot->data.Size());
    XChangeProperty(display, request.requestor, property, state.atom_incr, 32, PropModeReplace, reinterpret_cast<const unsigned char*>(&size), 1);
    SendNotify(display, request, property);
}

void HandlePropertyDelete(Display* display, X11ClipboardState& state, const XPropertyEvent& event, u64 now_ns) {
    XErrorScope errors(display);
    for (X11Outgoing& out : state.outgoing) {
        if (out.active && out.requestor == event.window && out.property == event.atom) {
            if (out.finished) {
                out.active = false;
                out.data.Reset();
                return;
            }
            out.deadline_ns = now_ns + kClipboardTimeoutNs * 2;
            SendChunk(display, out);
            return;
        }
    }
}

// The target to ask for in `format`.
unsigned long TargetFor(Display* display, const X11ClipboardState& state, ClipboardFormat format, StringView custom_name) {
    if (format == ClipboardFormat::Text) return state.atom_utf8;
    if (format == ClipboardFormat::FileList) return state.atom_uri_list;
    char name[128];
    const usize n = custom_name.Length() < sizeof(name) - 1 ? custom_name.Length() : sizeof(name) - 1;
    for (usize i = 0; i < n; ++i) name[i] = custom_name[i];
    name[n] = '\0';
    return XInternAtom(display, name, False);
}

X11Incoming* FindIncoming(X11ClipboardState& state, unsigned long selection, unsigned long target, unsigned long property) {
    ClipboardKind kind;
    if (!KindOf(state, selection, &kind)) return nullptr;
    for (X11Incoming& in : state.incoming) {
        if (in.active && !in.drop && in.kind == kind && in.property == property && (in.target == target || in.incr)) return &in;
    }
    return nullptr;
}

void FinishIncoming(Display* display, X11ClipboardState& state, X11Incoming& in, ClipboardStatus status, u64 now_ns) {
    (void)display;
    Vector<byte> data = static_cast<Vector<byte>&&>(in.data);
    const bool internal = in.id == 0;
    const unsigned long target = in.target;
    const u32 id = in.id;
    const ClipboardKind kind = in.kind;
    const ClipboardFormat format = in.format;
    in.active = false;
    in.incr = false;
    in.data = Vector<byte>();
    if (internal) {
        if (status == ClipboardStatus::Ok) {
            Vector<unsigned long>& cache = state.cache[KindIndex(kind)];
            cache.Clear();
            for (usize i = 0; i + sizeof(unsigned long) <= data.Size(); i += sizeof(unsigned long)) {
                unsigned long atom = 0;
                for (usize k = 0; k < sizeof(unsigned long); ++k) atom |= static_cast<unsigned long>(data[i + k]) << (8 * k);
                cache.Push(atom);
            }
            state.cache_valid[KindIndex(kind)] = true;
        }
        return;
    }
    if (status != ClipboardStatus::Ok) {
        state.requests.Fail(id, status, now_ns);
        return;
    }
    if (data.Size() > kMaxClipboardBytes) {
        state.requests.Fail(id, ClipboardStatus::TooLarge, now_ns);
        return;
    }
    Vector<byte> out;
    u32 replaced = 0;
    u32 rejected = 0;
    if (format == ClipboardFormat::Text) {
        if (target == XA_STRING) {
            Vector<byte> utf8;
            Latin1ToUtf8(data.Data(), data.Size(), utf8);
            SanitizeUtf8(utf8.Data(), utf8.Size(), out, &replaced);
        } else {
            SanitizeUtf8(data.Data(), data.Size(), out, &replaced);
        }
    } else if (format == ClipboardFormat::FileList) {
        ParseUriList(data.Data(), data.Size(), out, &rejected);
        rejected += LimitPathList(out, kMaxDropFileEntries); // a hostile owner may list millions of files
    } else {
        out = static_cast<Vector<byte>&&>(data);
    }
    state.requests.Complete(id, ClipboardStatus::Ok, static_cast<Vector<byte>&&>(out), replaced, rejected, now_ns);
}

// Reads (and deletes) the property; appends its bytes. Returns false on error. `type` receives the property type.
bool ReadProperty(Display* display, unsigned long window, unsigned long property, Vector<byte>& out, unsigned long* type, int* format_out, bool* too_large) {
    *too_large = false;
    long offset = 0;
    for (;;) {
        Atom actual_type = 0;
        int actual_format = 0;
        unsigned long count = 0;
        unsigned long after = 0;
        unsigned char* data = nullptr;
        if (XGetWindowProperty(display, window, property, offset, 262144, True, AnyPropertyType, &actual_type, &actual_format, &count, &after, &data) != Success) {
            return false;
        }
        *type = actual_type;
        *format_out = actual_format;
        if (data != nullptr) {
            // Format 32 data comes back as an array of long.
            const usize unit = actual_format == 32 ? sizeof(long) : actual_format == 16 ? 2 : 1;
            const usize bytes = count * unit;
            if (out.Size() + bytes > kMaxClipboardBytes + 1) {
                XFree(data);
                *too_large = true; // the caller ends the request with TooLarge; this read already deleted the property
                return true;
            }
            for (usize i = 0; i < bytes; ++i) out.Push(data[i]);
            offset += static_cast<long>((count * static_cast<unsigned long>(actual_format) / 8 + 3) / 4); // the offset counts 32-bit units of the property itself
            XFree(data);
        }
        if (after == 0) return true;
    }
}

void StartRequest(Display* display, unsigned long window, X11ClipboardState& state, X11Incoming& in) {
    XConvertSelection(display, in.drop ? state.atom_xdnd_selection : state.atom_selection[KindIndex(in.kind)], in.target, in.property, window, in.drop ? in.time : CurrentTime);
    XFlush(display);
}

void HandleSelectionNotify(Display* display, unsigned long window, X11ClipboardState& state, const XSelectionEvent& event, u64 now_ns) {
    X11Incoming* in = nullptr;
    if (event.selection == state.atom_xdnd_selection) {
        for (X11Incoming& candidate : state.incoming) {
            if (candidate.active && candidate.drop && (candidate.target == event.target || candidate.incr) && (event.property == None || candidate.property == event.property)) in = &candidate;
        }
        if (in == nullptr) return;
    } else {
        in = FindIncoming(state, event.selection, event.target, event.property != None ? event.property : 0);
    }
    if (in == nullptr) {
        // A refusal carries property None: find the request by selection and target.
        ClipboardKind kind;
        if (event.property == None && KindOf(state, event.selection, &kind)) {
            for (X11Incoming& candidate : state.incoming) {
                if (candidate.active && candidate.kind == kind && candidate.target == event.target) in = &candidate;
            }
        }
        if (in == nullptr) return;
    }
    if (event.property == None) {
        if (in->format == ClipboardFormat::Text && in->target == state.atom_utf8 && !in->retried_string) {
            in->retried_string = true;
            in->target = XA_STRING; // an old owner without UTF8_STRING: ask for Latin-1
            StartRequest(display, window, state, *in);
            return;
        }
        FinishIncoming(display, state, *in, ClipboardStatus::Missing, now_ns);
        return;
    }
    Vector<byte> bytes;
    unsigned long type = 0;
    int format = 0;
    bool too_large = false;
    if (!ReadProperty(display, window, event.property, bytes, &type, &format, &too_large)) {
        FinishIncoming(display, state, *in, ClipboardStatus::Failed, now_ns);
        return;
    }
    if (too_large) {
        FinishIncoming(display, state, *in, ClipboardStatus::TooLarge, now_ns);
        return;
    }
    if (type == state.atom_incr) {
        in->incr = true;
        in->data.Clear();
        if (in->id != 0) state.requests.Touch(in->id, now_ns);
        else in->deadline_ns = now_ns + kClipboardTimeoutNs;
        return;
    }
    if (bytes.Size() > kMaxClipboardBytes) {
        FinishIncoming(display, state, *in, ClipboardStatus::TooLarge, now_ns);
        return;
    }
    in->data = static_cast<Vector<byte>&&>(bytes);
    FinishIncoming(display, state, *in, ClipboardStatus::Ok, now_ns);
}

void HandleIncrChunk(Display* display, unsigned long window, X11ClipboardState& state, const XPropertyEvent& event, u64 now_ns) {
    if (event.window != window) return; // property changes on a requestor's window (selected for INCR sending) are not pieces of our transfers
    for (X11Incoming& in : state.incoming) {
        if (!in.active || !in.incr || in.property != event.atom) continue;
        Vector<byte> chunk;
        unsigned long type = 0;
        int format = 0;
        bool too_large = false;
        if (!ReadProperty(display, window, event.atom, chunk, &type, &format, &too_large)) {
            FinishIncoming(display, state, in, ClipboardStatus::Failed, now_ns);
            return;
        }
        if (too_large) {
            FinishIncoming(display, state, in, ClipboardStatus::TooLarge, now_ns);
            return;
        }
        if (chunk.Empty()) {
            FinishIncoming(display, state, in, ClipboardStatus::Ok, now_ns); // the empty piece ends it
            return;
        }
        if (in.data.Size() + chunk.Size() > kMaxClipboardBytes) {
            FinishIncoming(display, state, in, ClipboardStatus::TooLarge, now_ns);
            return;
        }
        for (usize i = 0; i < chunk.Size(); ++i) in.data.Push(chunk[i]);
        if (in.id != 0) state.requests.Touch(in.id, now_ns);
        else in.deadline_ns = now_ns + kClipboardTimeoutNs;
        return;
    }
}

void QueryTargets(Display* display, unsigned long window, X11ClipboardState& state, ClipboardKind kind, u64 now_ns) {
    for (X11Incoming& in : state.incoming) {
        if (in.active && in.id == 0 && in.kind == kind) in.active = false; // a newer owner replaces the older query
    }
    for (u32 slot = 0; slot < X11ClipboardState::kSlots; ++slot) {
        X11Incoming& in = state.incoming[slot];
        if (in.active) continue;
        in = X11Incoming();
        in.active = true;
        in.id = 0;
        in.kind = kind;
        in.target = state.atom_targets;
        in.property = state.atom_property[slot];
        in.deadline_ns = now_ns + kClipboardTimeoutNs;
        StartRequest(display, window, state, in);
        return;
    }
}

} // namespace

void ClipboardInit(_XDisplay* display, unsigned long window, X11ClipboardState& state, InputEventQueue* queue) {
    state.queue = queue;
    state.requests.Init(queue);
    if (state.atom_targets != 0) return; // atoms are interned once
    state.atom_selection[0] = XInternAtom(display, "CLIPBOARD", False);
    state.atom_selection[1] = XA_PRIMARY;
    state.atom_targets = XInternAtom(display, "TARGETS", False);
    state.atom_utf8 = XInternAtom(display, "UTF8_STRING", False);
    state.atom_text_plain = XInternAtom(display, "text/plain", False);
    state.atom_text_plain_utf8 = XInternAtom(display, "text/plain;charset=utf-8", False);
    state.atom_uri_list = XInternAtom(display, "text/uri-list", False);
    state.atom_incr = XInternAtom(display, "INCR", False);
    state.atom_timestamp = XInternAtom(display, "TIMESTAMP", False);
    state.atom_xdnd_selection = XInternAtom(display, "XdndSelection", False);
    for (u32 i = 0; i < X11ClipboardState::kSlots; ++i) {
        char name[32] = "RE_CLIPBOARD_0";
        name[13] = static_cast<char>('0' + i);
        state.atom_property[i] = XInternAtom(display, name, False);
    }
    int error_base = 0;
    if (XFixesQueryExtension(display, &state.xfixes_event_base, &error_base)) {
        state.xfixes = true;
        for (unsigned long selection : state.atom_selection) {
            XFixesSelectSelectionInput(display, DefaultRootWindow(display), selection, XFixesSetSelectionOwnerNotifyMask);
        }
    }
    // A selection some program already owns when we start: learn what it offers now, since no change will announce it.
    for (usize i = 0; i < static_cast<usize>(ClipboardKind::Count); ++i) {
        if (XGetSelectionOwner(display, state.atom_selection[i]) != None) {
            QueryTargets(display, window, state, static_cast<ClipboardKind>(i), MonotonicTimeNanos());
        }
    }
}

bool ClipboardSet(_XDisplay* display, unsigned long window, X11ClipboardState& state, ClipboardKind kind, const ClipboardOffer& offer, u64 now_ns) {
    if (display == nullptr || static_cast<usize>(kind) >= static_cast<usize>(ClipboardKind::Count)) return false;
    if (offer.size > kMaxClipboardBytes || (offer.size > 0 && offer.data == nullptr)) return false;
    if (offer.format == ClipboardFormat::Custom && (offer.custom_name.Length() == 0 || offer.custom_name.Length() >= sizeof(state.owned[0].custom_name))) return false;
    X11Owned next;
    next.format = offer.format;
    if (offer.format == ClipboardFormat::Text) {
        SanitizeUtf8(offer.data, offer.size, next.bytes, nullptr);
    } else {
        for (usize i = 0; i < offer.size; ++i) next.bytes.Push(offer.data[i]);
    }
    if (offer.format == ClipboardFormat::Custom) {
        for (usize i = 0; i < offer.custom_name.Length(); ++i) next.custom_name[i] = offer.custom_name[i];
        next.custom_atom = XInternAtom(display, next.custom_name, False);
    }
    const unsigned long selection = state.atom_selection[KindIndex(kind)];
    XSetSelectionOwner(display, selection, window, CurrentTime);
    if (XGetSelectionOwner(display, selection) != window) return false;
    next.owned = true;
    state.owned[KindIndex(kind)] = static_cast<X11Owned&&>(next);
    state.cache_valid[KindIndex(kind)] = false;
    XFlush(display);
    (void)now_ns;
    return true;
}

u32 ClipboardRequest(_XDisplay* display, unsigned long window, X11ClipboardState& state, ClipboardKind kind, ClipboardFormat format, StringView custom_name, u64 now_ns) {
    if (display == nullptr || static_cast<usize>(kind) >= static_cast<usize>(ClipboardKind::Count)) return 0;
    if (format == ClipboardFormat::Custom && custom_name.Length() == 0) return 0;
    u32 slot = X11ClipboardState::kSlots;
    for (u32 i = 0; i < X11ClipboardState::kSlots; ++i) {
        if (!state.incoming[i].active) {
            slot = i;
            break;
        }
    }
    if (slot == X11ClipboardState::kSlots) return 0;
    const u32 id = state.requests.Begin(kind, format, now_ns);
    if (id == 0) return 0;
    const X11Owned& own = state.owned[KindIndex(kind)];
    if (own.owned) {
        // Our own selection: answered without the server (asking ourselves would also need a second event round trip).
        if (own.format != format || (format == ClipboardFormat::Custom && !(StringView(own.custom_name) == custom_name))) {
            state.requests.Fail(id, ClipboardStatus::Missing, now_ns);
        } else {
            Vector<byte> copy;
            for (usize i = 0; i < own.bytes.Size(); ++i) copy.Push(own.bytes[i]);
            state.requests.Complete(id, ClipboardStatus::Ok, static_cast<Vector<byte>&&>(copy), 0, 0, now_ns);
        }
        return id;
    }
    X11Incoming& in = state.incoming[slot];
    in = X11Incoming();
    in.active = true;
    in.id = id;
    in.kind = kind;
    in.format = format;
    in.target = TargetFor(display, state, format, custom_name);
    in.property = state.atom_property[slot];
    StartRequest(display, window, state, in);
    return id;
}

bool ClipboardHas(_XDisplay* display, const X11ClipboardState& state, ClipboardKind kind, ClipboardFormat format, StringView custom_name) {
    if (display == nullptr || static_cast<usize>(kind) >= static_cast<usize>(ClipboardKind::Count)) return false;
    const X11Owned& owned = state.owned[KindIndex(kind)];
    if (owned.owned) {
        if (owned.format != format) return false;
        if (format != ClipboardFormat::Custom) return true;
        return StringView(owned.custom_name) == custom_name;
    }
    if (!state.cache_valid[KindIndex(kind)]) return false;
    const Vector<unsigned long>& atoms = state.cache[KindIndex(kind)];
    auto has = [&atoms](unsigned long atom) {
        for (usize i = 0; i < atoms.Size(); ++i) {
            if (atoms[i] == atom) return true;
        }
        return false;
    };
    if (format == ClipboardFormat::Text) return has(state.atom_utf8) || has(XA_STRING) || has(state.atom_text_plain_utf8) || has(state.atom_text_plain);
    if (format == ClipboardFormat::FileList) return has(state.atom_uri_list);
    return has(TargetFor(display, state, format, custom_name));
}

bool ClipboardHandleEvent(_XDisplay* display, unsigned long window, X11ClipboardState& state, void* event_ptr, u64 now_ns) {
    XEvent& event = *static_cast<XEvent*>(event_ptr);
    if (state.xfixes && event.type == state.xfixes_event_base + XFixesSelectionNotify) {
        const XFixesSelectionNotifyEvent& notify = *reinterpret_cast<const XFixesSelectionNotifyEvent*>(&event);
        ClipboardKind kind;
        if (KindOf(state, notify.selection, &kind) && notify.owner != window) {
            state.cache_valid[KindIndex(kind)] = false;
            QueueChanged(state, kind, now_ns);
            if (notify.owner != None) {
                QueryTargets(display, window, state, kind, now_ns);
            } else {
                state.cache[KindIndex(kind)].Clear();
                state.cache_valid[KindIndex(kind)] = true;
            }
        }
        return true;
    }
    switch (event.type) {
    case SelectionRequest: HandleSelectionRequest(display, window, state, event.xselectionrequest, now_ns); return true;
    case SelectionClear: {
        ClipboardKind kind;
        if (KindOf(state, event.xselectionclear.selection, &kind)) {
            state.owned[KindIndex(kind)] = X11Owned();
            QueueChanged(state, kind, now_ns);
        }
        return true;
    }
    case SelectionNotify: HandleSelectionNotify(display, window, state, event.xselection, now_ns); return true;
    case PropertyNotify:
        if (event.xproperty.state == PropertyDelete) HandlePropertyDelete(display, state, event.xproperty, now_ns);
        else HandleIncrChunk(display, window, state, event.xproperty, now_ns);
        return true;
    default: return false;
    }
}

void ClipboardTick(_XDisplay* display, X11ClipboardState& state, u64 now_ns) {
    state.requests.Expire(now_ns);
    for (X11Incoming& in : state.incoming) {
        if (!in.active) continue;
        const bool gone = in.id != 0 ? !state.requests.Pending(in.id) : now_ns >= in.deadline_ns;
        if (gone) {
            in.active = false;
            in.incr = false;
            in.data.Reset();
        }
    }
    for (X11Outgoing& out : state.outgoing) {
        if (out.active && now_ns >= out.deadline_ns) {
            out.active = false; // the requestor stopped taking pieces
            out.data.Reset();
        }
    }
    (void)display;
}

void ClipboardRelease(_XDisplay* display, X11ClipboardState& state, u64 now_ns) {
    if (display == nullptr) return;
    for (usize i = 0; i < static_cast<usize>(ClipboardKind::Count); ++i) {
        if (state.owned[i].owned) {
            XSetSelectionOwner(display, state.atom_selection[i], None, CurrentTime);
            state.owned[i] = X11Owned();
            XSync(display, False); // the server has given the selection up before another connection can take it (else the late request would clear the new owner)
        }
    }
    state.requests.FailAll(ClipboardStatus::Failed, now_ns);
    for (X11Incoming& in : state.incoming) in = X11Incoming();
    for (X11Outgoing& out : state.outgoing) out = X11Outgoing();
    XFlush(display);
}

} // namespace engine_placeholder::os::x11
