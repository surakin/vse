#pragma once

// OLE drag and drop for Win32Window (Phase 15 Day 205). Internal to engine/platform/windows (and its tests, which drive the drop target with a
// data object directly: OLE's own drag needs the real mouse).
//
// The COM objects live inside one Win32Dnd, which the window owns (UniquePtr): their reference counts are counted but never free anything.
// That is safe because the drop target is revoked before the window is destroyed and DoDragDrop is synchronous, so no reference outlives the
// window (a reference left over is logged).

#include "containers/array.h"
#include "containers/vector.h"
#include "os/windows/win32.h"
#include "platform/clipboard.h"
#include "platform/drag_drop.h"
#include "platform/input_event.h"
#include "types.h"

#include "unity/windows_enable.h"
#include <ole2.h>
#include "unity/windows_disable.h"

namespace engine_placeholder::os::win32 {

class Win32Window;

/// The id of the window timer that calls the modal-loop callback, during the system's move and size loop and during a drag (`DoDragDrop`).
inline constexpr UINT_PTR kWin32ModalTimerId = 1;

/// The data a drag offers, as an `IDataObject`: copies of up to [[os::kMaxDragOffers]] offers (text as `CF_UNICODETEXT`, a file list as
/// `CF_HDROP`, custom bytes under their registered name).
class Win32DataObject final : public IDataObject {
public:
    /// Replaces the offers with copies of `offers`.
    /// Returns: false if an offer is empty, too large, unnamed (custom) or holds a refused path (files); then nothing is kept.
    bool Set(const ClipboardOffer* offers, usize count);
    /// Drops the offers.
    void Clear();
    /// Returns the COM references held by others.
    ULONG References() const { return references_; }

    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID riid, void** out) override;
    ULONG STDMETHODCALLTYPE AddRef() override { return ++references_; }
    ULONG STDMETHODCALLTYPE Release() override { return references_ > 0 ? --references_ : 0; }
    HRESULT STDMETHODCALLTYPE GetData(FORMATETC* format, STGMEDIUM* medium) override;
    HRESULT STDMETHODCALLTYPE GetDataHere(FORMATETC*, STGMEDIUM*) override { return E_NOTIMPL; }
    HRESULT STDMETHODCALLTYPE QueryGetData(FORMATETC* format) override;
    HRESULT STDMETHODCALLTYPE GetCanonicalFormatEtc(FORMATETC*, FORMATETC* out) override;
    HRESULT STDMETHODCALLTYPE SetData(FORMATETC*, STGMEDIUM*, BOOL) override { return E_NOTIMPL; }
    HRESULT STDMETHODCALLTYPE EnumFormatEtc(DWORD direction, IEnumFORMATETC** out) override;
    HRESULT STDMETHODCALLTYPE DAdvise(FORMATETC*, DWORD, IAdviseSink*, DWORD*) override { return OLE_E_ADVISENOTSUPPORTED; }
    HRESULT STDMETHODCALLTYPE DUnadvise(DWORD) override { return OLE_E_ADVISENOTSUPPORTED; }
    HRESULT STDMETHODCALLTYPE EnumDAdvise(IEnumSTATDATA**) override { return OLE_E_ADVISENOTSUPPORTED; }

private:
    struct Offer {
        UINT format = 0;      // the clipboard format
        Vector<byte> bytes;   // the payload as GetData hands it out (UTF-16 text with its NUL, a DROPFILES block, raw bytes)
    };
    i32 Find(UINT format) const; // the offer of a format, or -1
    Array<Offer, kMaxDragOffers> offers_ = {};
    usize count_ = 0;
    ULONG references_ = 0;
};

/// The drag source's answers to OLE's loop: Escape or a cancel request cancels, releasing the button drops.
class Win32DropSource final : public IDropSource {
public:
    bool cancel = false; ///< `CancelDrag` was called.
    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID riid, void** out) override;
    ULONG STDMETHODCALLTYPE AddRef() override { return ++references_; }
    ULONG STDMETHODCALLTYPE Release() override { return references_ > 0 ? --references_ : 0; }
    HRESULT STDMETHODCALLTYPE QueryContinueDrag(BOOL escape, DWORD keys) override;
    HRESULT STDMETHODCALLTYPE GiveFeedback(DWORD) override { return DRAGDROP_S_USEDEFAULTCURSORS; }

private:
    ULONG references_ = 0;
};

/// The window's drop target: turns OLE's calls into the window's drag events and answers with the program's chosen action.
class Win32DropTarget final : public IDropTarget {
public:
    Win32Window* window = nullptr;
    /// Returns the COM references held by others (OLE's, between RegisterDragDrop and RevokeDragDrop).
    ULONG References() const { return references_; }
    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID riid, void** out) override;
    ULONG STDMETHODCALLTYPE AddRef() override { return ++references_; }
    ULONG STDMETHODCALLTYPE Release() override { return references_ > 0 ? --references_ : 0; }
    HRESULT STDMETHODCALLTYPE DragEnter(IDataObject* data, DWORD keys, POINTL point, DWORD* effect) override;
    HRESULT STDMETHODCALLTYPE DragOver(DWORD keys, POINTL point, DWORD* effect) override;
    HRESULT STDMETHODCALLTYPE DragLeave() override;
    HRESULT STDMETHODCALLTYPE Drop(IDataObject* data, DWORD keys, POINTL point, DWORD* effect) override;

private:
    ULONG references_ = 0;
};

/// One format of a finished drop, read while OLE's `Drop` call ran (the data object is not valid afterwards).
struct Win32DroppedFormat {
    bool present = false;
    ClipboardStatus status = ClipboardStatus::Missing;
    Vector<byte> bytes;
    u32 replaced = 0;
    u32 rejected = 0;
};

/// The drag and drop state of one window.
struct Win32Dnd {
    Win32DropTarget target;
    Win32DropSource source;
    Win32DataObject outgoing;
    bool ole_initialized = false; ///< OleInitialize succeeded on this thread for this window (OleUninitialize at the end).
    bool registered = false;      ///< RegisterDragDrop succeeded.
    // What the program accepts.
    u8 accept_formats = 0;
    UINT accept_custom = 0; ///< The registered format of the accepted custom name, or 0.
    // The drag over the window now.
    bool session = false;      ///< DragEnter was answered with an accepted format and no DragLeave or Drop yet.
    u8 offered = 0;            ///< kDrop* bits the data offers and the program accepts.
    u8 allowed = 0;            ///< DragAction bits the source allows.
    bool answered = false;     ///< SetDropAction was called during this drag.
    DragAction answer = DragAction::Copy;
    // The last drop, until FinishDrop or a new drag.
    bool drop_waiting = false;
    u64 drop_time_ns = 0;
    Array<Win32DroppedFormat, 3> dropped = {}; ///< Text, FileList, Custom.
    // A drag this window started.
    bool dragging = false;
};

} // namespace engine_placeholder::os::win32
