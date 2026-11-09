// OLE drag and drop of Win32Window (Phase 15 Day 205): an IDropTarget registered on the window turns OLE's calls into DragEnter, DragOver,
// DragLeave and Drop events; BeginDrag runs DoDragDrop with an IDataObject holding copies of the offers. See win32_dragdrop.h for the lifetime
// rules and platform/BACKEND.md for the contract. Where OLE differs from the Linux backends:
//   - the dropped data is readable only while OLE's Drop call runs, so every accepted format is read then and RequestDropData answers from that
//     copy (FinishDrop, or the session timeout, drops it);
//   - DoDragDrop is modal: BeginDrag returns when the drag is over, after queueing DragEnded. The modal-loop callback keeps running meanwhile
//     (OLE pumps the window's messages), so a program can still draw. It refuses to start unless a mouse button is down, because OLE drops at
//     once on whatever window is under the cursor when none is.
#include "platform/windows/win32_dragdrop.h"

#include <cstring>

#include "log/log.h"
#include "os/os.h"
#include "platform/windows/win32_transfer.h"
#include "platform/windows/win32_window.h"

#include "unity/windows_enable.h"
#include <shlobj.h> // SHCreateStdEnumFmtEtc
#include "unity/windows_disable.h"

namespace engine_placeholder::os::win32 {

using namespace detail;

namespace {

FORMATETC HglobalFormat(UINT format) { return FORMATETC{static_cast<CLIPFORMAT>(format), nullptr, DVASPECT_CONTENT, -1, TYMED_HGLOBAL}; }

bool Offers(IDataObject* data, UINT format) {
    if (format == 0) return false;
    FORMATETC f = HglobalFormat(format);
    return data->QueryGetData(&f) == S_OK;
}

// DragAction bits and DROPEFFECT values are the same three bits (copy 1, move 2, link 4).
DWORD EffectOf(DragAction action) { return action == DragAction::Nothing ? DROPEFFECT_NONE : static_cast<DWORD>(DragActionBit(action)); }

DragAction ActionOfEffect(DWORD effect) {
    if ((effect & DROPEFFECT_MOVE) != 0) return DragAction::Move;
    if ((effect & DROPEFFECT_COPY) != 0) return DragAction::Copy;
    if ((effect & DROPEFFECT_LINK) != 0) return DragAction::Link;
    return DragAction::Nothing;
}

} // namespace

// ---- Win32DataObject ----

bool Win32DataObject::Set(const ClipboardOffer* offers, usize count) {
    Clear();
    if (offers == nullptr || count == 0 || count > kMaxDragOffers) return false;
    for (usize i = 0; i < count; ++i) {
        const ClipboardOffer& o = offers[i];
        if (o.data == nullptr || o.size == 0 || o.size > kMaxClipboardBytes) {
            Clear();
            return false;
        }
        Offer& out = offers_[count_];
        out.bytes.Clear();
        if (o.format == ClipboardFormat::Text) {
            Vector<byte> clean;
            SanitizeUtf8(o.data, o.size, clean, nullptr);
            Vector<wchar_t> wide;
            ToWide(StringView(reinterpret_cast<const char*>(clean.Data()), clean.Size()), &wide);
            out.format = CF_UNICODETEXT;
            out.bytes.Resize(wide.Size() * sizeof(wchar_t));
            std::memcpy(out.bytes.Data(), wide.Data(), out.bytes.Size());
        } else if (o.format == ClipboardFormat::FileList) {
            HGLOBAL block = MakeHdrop(o.data, o.size);
            if (block == nullptr) {
                Clear();
                return false;
            }
            out.format = CF_HDROP;
            out.bytes.Resize(GlobalSize(block));
            std::memcpy(out.bytes.Data(), GlobalLock(block), out.bytes.Size());
            GlobalUnlock(block);
            GlobalFree(block);
        } else {
            out.format = CustomFormat(o.custom_name);
            if (out.format == 0) {
                Clear();
                return false;
            }
            out.bytes.Resize(o.size);
            std::memcpy(out.bytes.Data(), o.data, o.size);
        }
        ++count_;
    }
    return true;
}

void Win32DataObject::Clear() {
    for (Offer& o : offers_) {
        o.format = 0;
        o.bytes.Clear();
    }
    count_ = 0;
}

i32 Win32DataObject::Find(UINT format) const {
    for (usize i = 0; i < count_; ++i) {
        if (offers_[i].format == format) return static_cast<i32>(i);
    }
    return -1;
}

HRESULT Win32DataObject::QueryInterface(REFIID riid, void** out) {
    if (out == nullptr) return E_POINTER;
    if (riid == IID_IUnknown || riid == IID_IDataObject) {
        *out = static_cast<IDataObject*>(this);
        AddRef();
        return S_OK;
    }
    *out = nullptr;
    return E_NOINTERFACE;
}

HRESULT Win32DataObject::QueryGetData(FORMATETC* format) {
    if (format == nullptr) return E_INVALIDARG;
    if ((format->tymed & TYMED_HGLOBAL) == 0 || format->dwAspect != DVASPECT_CONTENT || Find(format->cfFormat) < 0) return DV_E_FORMATETC;
    return S_OK;
}

HRESULT Win32DataObject::GetData(FORMATETC* format, STGMEDIUM* medium) {
    if (format == nullptr || medium == nullptr) return E_INVALIDARG;
    const HRESULT supported = QueryGetData(format);
    if (supported != S_OK) return supported;
    const Offer& offer = offers_[static_cast<usize>(Find(format->cfFormat))];
    HGLOBAL block = GlobalCopy(offer.bytes.Data(), offer.bytes.Size());
    if (block == nullptr) return E_OUTOFMEMORY;
    medium->tymed = TYMED_HGLOBAL;
    medium->hGlobal = block;
    medium->pUnkForRelease = nullptr; // the receiver frees the block (ReleaseStgMedium)
    return S_OK;
}

HRESULT Win32DataObject::GetCanonicalFormatEtc(FORMATETC* in, FORMATETC* out) {
    if (out == nullptr) return E_INVALIDARG;
    if (in != nullptr) *out = *in;
    out->ptd = nullptr;
    return DATA_S_SAMEFORMATETC;
}

HRESULT Win32DataObject::EnumFormatEtc(DWORD direction, IEnumFORMATETC** out) {
    if (out == nullptr) return E_INVALIDARG;
    if (direction != DATADIR_GET) return E_NOTIMPL;
    Array<FORMATETC, kMaxDragOffers> formats = {};
    for (usize i = 0; i < count_; ++i) formats[i] = HglobalFormat(offers_[i].format);
    return SHCreateStdEnumFmtEtc(static_cast<UINT>(count_), formats.Data(), out);
}

// ---- Win32DropSource ----

HRESULT Win32DropSource::QueryInterface(REFIID riid, void** out) {
    if (out == nullptr) return E_POINTER;
    if (riid == IID_IUnknown || riid == IID_IDropSource) {
        *out = static_cast<IDropSource*>(this);
        AddRef();
        return S_OK;
    }
    *out = nullptr;
    return E_NOINTERFACE;
}

HRESULT Win32DropSource::QueryContinueDrag(BOOL escape, DWORD keys) {
    if (escape || cancel) return DRAGDROP_S_CANCEL;
    if ((keys & (MK_LBUTTON | MK_RBUTTON)) == 0) return DRAGDROP_S_DROP;
    return S_OK;
}

// The window side of the drop target's calls: a friend of Win32Window, so the window's header needs no OLE type.
struct Win32DndAccess {
    static POINT ClientPoint(const Win32Window& w, POINTL screen);
    static DragAction Choose(const Win32Window& w, DWORD keys);
    static HRESULT Enter(Win32Window& w, IDataObject* data, DWORD keys, POINTL point, DWORD* effect);
    static HRESULT Over(Win32Window& w, DWORD keys, POINTL point, DWORD* effect);
    static HRESULT Leave(Win32Window& w);
    static HRESULT Drop(Win32Window& w, IDataObject* data, DWORD keys, POINTL point, DWORD* effect);
};

// ---- Win32DropTarget ----

HRESULT Win32DropTarget::QueryInterface(REFIID riid, void** out) {
    if (out == nullptr) return E_POINTER;
    if (riid == IID_IUnknown || riid == IID_IDropTarget) {
        *out = static_cast<IDropTarget*>(this);
        AddRef();
        return S_OK;
    }
    *out = nullptr;
    return E_NOINTERFACE;
}

HRESULT Win32DropTarget::DragEnter(IDataObject* data, DWORD keys, POINTL point, DWORD* effect) {
    if (effect == nullptr) return E_INVALIDARG;
    return Win32DndAccess::Enter(*window, data, keys, point, effect);
}

HRESULT Win32DropTarget::DragOver(DWORD keys, POINTL point, DWORD* effect) {
    if (effect == nullptr) return E_INVALIDARG;
    return Win32DndAccess::Over(*window, keys, point, effect);
}

HRESULT Win32DropTarget::DragLeave() { return Win32DndAccess::Leave(*window); }

HRESULT Win32DropTarget::Drop(IDataObject* data, DWORD keys, POINTL point, DWORD* effect) {
    if (effect == nullptr) return E_INVALIDARG;
    return Win32DndAccess::Drop(*window, data, keys, point, effect);
}

// ---- the window's side ----

void Win32Window::DndOpen() {
    dnd_ = MakeUnique<Win32Dnd>();
    dnd_->target.window = this;
    // OLE needs a single-threaded apartment on the window's thread. A thread already in the multithreaded apartment (RPC_E_CHANGED_MODE) cannot
    // take part: drag and drop is then unsupported, and nothing else changes.
    const HRESULT ole = OleInitialize(nullptr);
    dnd_->ole_initialized = SUCCEEDED(ole); // S_OK or S_FALSE (already initialized): either way one OleUninitialize is owed
    if (!dnd_->ole_initialized) {
        Log(LogPlatform, LogVerbosity::Warn, "Window: OleInitialize failed on this thread (it is in the multithreaded apartment?): no drag and drop");
        return;
    }
    dnd_->registered = SUCCEEDED(RegisterDragDrop(hwnd_, &dnd_->target));
    if (!dnd_->registered) Log(LogPlatform, LogVerbosity::Warn, "Window: RegisterDragDrop failed: no drag and drop into this window");
}

void Win32Window::DndClose() {
    if (dnd_.Get() == nullptr) return;
    if (dnd_->registered) RevokeDragDrop(hwnd_); // OLE releases its references to the target here
    if (dnd_->target.References() != 0 || dnd_->outgoing.References() != 0) {
        Log(LogPlatform, LogVerbosity::Warn, "Window: a drag and drop object is still referenced at close");
    }
    if (dnd_->ole_initialized) OleUninitialize();
    dnd_.Reset();
}

POINT Win32DndAccess::ClientPoint(const Win32Window& w, POINTL screen) {
    POINT p{screen.x, screen.y};
    ScreenToClient(w.hwnd_, &p);
    return p;
}

DragAction Win32DndAccess::Choose(const Win32Window& w, DWORD keys) {
    const Win32Dnd& d = *w.dnd_;
    DragAction action = DragAction::Copy;
    if (d.answered) {
        action = d.answer;
    } else if ((keys & MK_SHIFT) != 0 && (keys & MK_CONTROL) != 0) {
        action = DragAction::Link; // the Windows keys: Ctrl copies, Shift moves, both link
    } else if ((keys & MK_SHIFT) != 0) {
        action = DragAction::Move;
    }
    if (action != DragAction::Nothing && (d.allowed & DragActionBit(action)) == 0) action = DragAction::Nothing;
    return action;
}

HRESULT Win32DndAccess::Enter(Win32Window& w, IDataObject* data, DWORD keys, POINTL point, DWORD* effect) {
    Win32Dnd& d = *w.dnd_;
    d.session = false;
    u8 offered = 0;
    if (data != nullptr) {
        if (Offers(data, CF_UNICODETEXT)) offered |= kDropText;
        if (Offers(data, CF_HDROP)) offered |= kDropFiles;
        if (Offers(data, d.accept_custom)) offered |= kDropCustom;
    }
    offered &= d.accept_formats;
    if (offered == 0) {
        *effect = DROPEFFECT_NONE; // refused, and the program hears nothing of it
        return S_OK;
    }
    d.session = true;
    d.offered = offered;
    d.allowed = static_cast<u8>(*effect & kAllDragActions);
    d.answered = false;
    const POINT p = ClientPoint(w, point);
    InputEvent e;
    e.type = InputEventType::DragEnter;
    e.modifiers = w.input_->keyboard.modifiers;
    e.x = p.x;
    e.y = p.y;
    e.drag_formats = offered;
    e.drag_actions = d.allowed;
    w.QueueEvent(e);
    *effect = EffectOf(Choose(w, keys));
    return S_OK;
}

HRESULT Win32DndAccess::Over(Win32Window& w, DWORD keys, POINTL point, DWORD* effect) {
    Win32Dnd& d = *w.dnd_;
    if (!d.session) {
        *effect = DROPEFFECT_NONE;
        return S_OK;
    }
    d.allowed = static_cast<u8>(*effect & kAllDragActions); // OLE gives the source's allowed effects on every move
    const POINT p = ClientPoint(w, point);
    InputEvent e;
    e.type = InputEventType::DragOver;
    e.modifiers = w.input_->keyboard.modifiers;
    e.x = p.x;
    e.y = p.y;
    e.drag_actions = d.allowed;
    w.QueueEvent(e);
    *effect = EffectOf(Choose(w, keys));
    return S_OK;
}

HRESULT Win32DndAccess::Leave(Win32Window& w) {
    Win32Dnd& d = *w.dnd_;
    if (d.session) {
        InputEvent e;
        e.type = InputEventType::DragLeave;
        w.QueueEvent(e);
    }
    d.session = false;
    return S_OK;
}

HRESULT Win32DndAccess::Drop(Win32Window& w, IDataObject* data, DWORD keys, POINTL point, DWORD* effect) {
    Win32Dnd& d = *w.dnd_;
    if (!d.session || data == nullptr) {
        *effect = DROPEFFECT_NONE;
        return S_OK;
    }
    d.session = false;
    const DragAction action = Choose(w, keys);
    if (action == DragAction::Nothing) { // refused here: the source gets no drop, the program a leave
        InputEvent e;
        e.type = InputEventType::DragLeave;
        w.QueueEvent(e);
        *effect = DROPEFFECT_NONE;
        return S_OK;
    }
    // Read every accepted format now: the data object is not valid after this call.
    const Array<UINT, 3> natives = {{CF_UNICODETEXT, CF_HDROP, d.accept_custom}};
    const Array<u8, 3> bits = {{kDropText, kDropFiles, kDropCustom}};
    for (usize i = 0; i < 3; ++i) {
        Win32DroppedFormat& f = d.dropped[i];
        f = Win32DroppedFormat{};
        if ((d.offered & bits[i]) == 0) continue;
        f.present = true;
        FORMATETC want = HglobalFormat(natives[i]);
        STGMEDIUM medium{};
        if (data->GetData(&want, &medium) != S_OK || medium.tymed != TYMED_HGLOBAL) {
            f.status = ClipboardStatus::Failed;
            continue;
        }
        const void* p = GlobalLock(medium.hGlobal);
        const usize size = GlobalSize(medium.hGlobal);
        f.status = ClipboardStatus::Ok;
        if (p == nullptr) {
            f.status = ClipboardStatus::Failed;
        } else if (i == 0) {
            if (!ReadUnicodeText(p, size, &f.bytes, &f.replaced)) f.status = ClipboardStatus::TooLarge;
        } else if (i == 1) {
            if (!ReadHdrop(static_cast<HDROP>(medium.hGlobal), &f.bytes, &f.rejected)) {
                f.status = ClipboardStatus::TooLarge;
            } else {
                f.rejected += LimitPathList(f.bytes, kMaxDropFileEntries);
            }
        } else if (size > kMaxClipboardBytes) {
            f.status = ClipboardStatus::TooLarge;
        } else {
            f.bytes.Resize(size);
            std::memcpy(f.bytes.Data(), p, size);
        }
        if (p != nullptr) GlobalUnlock(medium.hGlobal);
        ReleaseStgMedium(&medium);
        if (f.status != ClipboardStatus::Ok) f.bytes.Clear();
    }
    d.drop_waiting = true;
    d.drop_time_ns = MonotonicTimeNanos();
    const POINT p = ClientPoint(w, point);
    InputEvent e;
    e.type = InputEventType::Drop;
    e.modifiers = w.input_->keyboard.modifiers;
    e.x = p.x;
    e.y = p.y;
    e.drag_formats = d.offered;
    e.drag_actions = d.allowed;
    e.drag_action = static_cast<u8>(action);
    w.QueueEvent(e);
    *effect = EffectOf(action);
    return S_OK;
}

bool Win32Window::SupportsDragAndDrop() const { return dnd_.Get() != nullptr && dnd_->registered; }

void Win32Window::SetDropAccept(const DropAccept& accept) {
    if (dnd_.Get() == nullptr) return;
    dnd_->accept_formats = accept.formats & (kDropText | kDropFiles | kDropCustom);
    dnd_->accept_custom = (accept.formats & kDropCustom) != 0 ? CustomFormat(accept.custom_name) : 0;
    if (dnd_->accept_custom == 0) dnd_->accept_formats &= static_cast<u8>(~kDropCustom);
}

void Win32Window::SetDropAction(DragAction action) {
    if (dnd_.Get() == nullptr || !dnd_->session) return;
    dnd_->answer = action;
    dnd_->answered = true;
}

u32 Win32Window::RequestDropData(ClipboardFormat format, StringView custom_name) {
    if (dnd_.Get() == nullptr || !dnd_->drop_waiting) return 0;
    const u64 now = MonotonicTimeNanos();
    const u32 id = clipboard_requests_.Begin(ClipboardKind::Clipboard, format, now, InputEventType::DropData);
    if (id == 0) return 0;
    const usize index = format == ClipboardFormat::Text ? 0 : format == ClipboardFormat::FileList ? 1 : 2;
    Win32DroppedFormat& f = dnd_->dropped[index];
    const bool name_matches = index != 2 || (CustomFormat(custom_name) != 0 && CustomFormat(custom_name) == dnd_->accept_custom);
    if (!f.present || !name_matches) {
        clipboard_requests_.Fail(id, ClipboardStatus::Missing, now);
        return id;
    }
    Vector<byte> copy;
    copy.Resize(f.bytes.Size());
    if (copy.Size() != 0) std::memcpy(copy.Data(), f.bytes.Data(), copy.Size());
    clipboard_requests_.Complete(id, f.status, static_cast<Vector<byte>&&>(copy), f.replaced, f.rejected, now);
    return id;
}

void Win32Window::FinishDrop([[maybe_unused]] DragAction taken) {
    // OLE took the answer when Drop returned; finishing drops the copy of the data.
    if (dnd_.Get() == nullptr) return;
    dnd_->drop_waiting = false;
    for (Win32DroppedFormat& f : dnd_->dropped) f = Win32DroppedFormat{};
}

void Win32Window::DndTick(u64 now_ns) {
    if (dnd_.Get() != nullptr && dnd_->drop_waiting && now_ns - dnd_->drop_time_ns > kDropSessionTimeoutNs) FinishDrop(DragAction::Nothing);
}

bool Win32Window::BeginDrag(const DragSource& source) {
    if (!SupportsDragAndDrop() || dnd_->dragging || source.offers == nullptr || source.offer_count == 0 || source.offer_count > kMaxDragOffers) return false;
    // OLE drops at once on whatever window is under the cursor when no button is down; a drag starts only from a press or a move with it held.
    if ((GetAsyncKeyState(VK_LBUTTON) & 0x8000) == 0 && (GetAsyncKeyState(VK_RBUTTON) & 0x8000) == 0) return false;
    if (!dnd_->outgoing.Set(source.offers, source.offer_count)) return false;
    dnd_->dragging = true;
    dnd_->source.cancel = false;
    in_modal_loop_ = true; // the modal-loop callback keeps the program drawing while OLE runs the drag
    if (modal_callback_ != nullptr) SetTimer(hwnd_, 1, USER_TIMER_MINIMUM, nullptr);
    DWORD effect = DROPEFFECT_NONE;
    const HRESULT result = DoDragDrop(&dnd_->outgoing, &dnd_->source, static_cast<DWORD>(source.allowed_actions & kAllDragActions), &effect);
    KillTimer(hwnd_, 1);
    in_modal_loop_ = false;
    dnd_->dragging = false;
    dnd_->outgoing.Clear();
    InputEvent ended;
    ended.type = InputEventType::DragEnded;
    ended.drag_action = static_cast<u8>(result == DRAGDROP_S_DROP ? ActionOfEffect(effect) : DragAction::Nothing);
    QueueEvent(ended);
    return true;
}

void Win32Window::CancelDrag() {
    if (dnd_.Get() != nullptr && dnd_->dragging) dnd_->source.cancel = true;
}

bool Win32Window::Dragging() const { return dnd_.Get() != nullptr && dnd_->dragging; }

void* Win32Window::DropTargetForTest() const { return dnd_.Get() != nullptr && dnd_->registered ? static_cast<IDropTarget*>(&dnd_->target) : nullptr; }

} // namespace engine_placeholder::os::win32
