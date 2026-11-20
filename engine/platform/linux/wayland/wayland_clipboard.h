#pragma once

#include <wayland-client.h>

#include "containers/vector.h"
#include "platform/clipboard.h"
#include "platform/drag_drop.h"
#include "platform/input_event.h"
#include "platform/linux/clipboard_pipe.h"
#include "platform/linux/wayland/generated/primary-selection-unstable-v1-client-protocol.h"
#include "platform/linux/wayland/wayland_connection.h"
#include "string/string.h"
#include "types.h"

//! The Wayland clipboard and primary selection: a `wl_data_device` and a `zwp_primary_selection_device_v1` on the seat,
//! with the transfers done through non-blocking pipes.

namespace vse::os::wayland
{

/// The clipboard of a Wayland seat.
///
/// Owning a selection needs the serial of a recent key, button or enter event: the input handler passes each one to
/// [[os::wayland::WaylandClipboard::RecordSerial]], and
/// [[os::wayland::WaylandClipboard::Set]] fails before the first. Data is sent to a peer and received from it through
/// pipes that [[os::wayland::WaylandClipboard::Tick]] advances without blocking (a peer that never reads or writes
/// costs only its timeout). Not movable: the listeners capture `this`.
///
/// - Thread-safety: owner-thread-only (the thread that dispatches the connection).
/// - Ownership: owns the data devices, the sources and offers it creates, and the pipes; the managers belong to the
/// connection.
class WaylandClipboard
{
public:
    WaylandClipboard() = default;
    /// Calls [[os::wayland::WaylandClipboard::Shutdown]].
    ~WaylandClipboard();
    WaylandClipboard(const WaylandClipboard&) = delete;
    WaylandClipboard& operator=(const WaylandClipboard&) = delete;

    /// Creates the data devices on the connection's seat. Events go to `queue`.
    /// - Returns: false if the compositor offers no `wl_data_device_manager` (then nothing is supported).
    bool Init(WaylandConnection& connection, InputEventQueue* queue);

    /// Destroys everything this object created, while the display is still connected. Idempotent.
    void Shutdown();

    /// Remembers the serial of an input event, the proof of user action a compositor wants before it accepts a
    /// selection.
    void RecordSerial(u32 serial) { last_serial_ = serial; }
    /// Records the serial of a pointer button press: `wl_data_device.start_drag` needs the press that holds the
    /// implicit grab, not the latest key or enter serial.
    void RecordButtonSerial(u32 serial) { button_serial_ = serial; }

    /// Returns whether the selection `kind` works (the primary selection needs the compositor to offer the protocol).
    bool Supports(ClipboardKind kind) const;
    /// Takes the selection; false before the first input event, on a bad offer or an unsupported kind.
    bool Set(ClipboardKind kind, const ClipboardOffer& offer);
    /// Starts a read of the selection; returns the request id or 0.
    u32 Request(ClipboardKind kind, ClipboardFormat format, StringView custom_name);
    /// Whether the selection offers the format.
    bool Has(ClipboardKind kind, ClipboardFormat format, StringView custom_name) const;
    /// Advances the pipes and expires the requests; call once per pump.
    void Tick(u64 now_ns);
    /// Hands over a finished result once.
    bool Take(u32 id, ClipboardResult* out) { return requests_.Take(id, out); }

    /// The surface of the window: the origin of drags this window starts.
    void SetSurface(wl_surface* surface) { surface_ = surface; }

    /// Drag and drop: the data device also carries drags (see the same-named [[os::Window]] operations).
    bool SupportsDnd() const { return device_ != nullptr; }
    /// Sets what drops are accepted.
    void SetDropAccept(const DropAccept& accept);
    /// The program's answer to the drag over the window.
    void SetDropAction(DragAction action);
    /// Reads one format of the dropped data; returns the request id or 0.
    u32 RequestDropData(ClipboardFormat format, StringView custom_name);
    /// Ends the drop (`wl_data_offer.finish` when something was taken).
    void FinishDrop(DragAction taken);
    /// Starts a drag with the serial of the last button or key event.
    bool BeginDrag(const DragSource& source);
    /// Cancels the drag this window started.
    void CancelDrag();
    /// Whether a drag started by this window is running.
    bool Dragging() const { return drag_active_; }

    /// Whether every event of the `wl_data_device`, `wl_data_offer`, `wl_data_source` and the primary selection objects
    /// has a listener function (libwayland aborts on a null slot).
    static bool ListenersComplete();

private:
    static constexpr u32 kMaxMimes = 24;
    static constexpr u32 kMimeLength = 96;
    static constexpr u32 kOfferSlots = 8;
    static constexpr u32 kWriters = 4;
    static constexpr u32 kReaders = 8;
    struct OfferSlot
    {
        WaylandClipboard* owner = nullptr;
        ClipboardKind kind = ClipboardKind::Clipboard;
        bool used = false;
        void* proxy = nullptr;
        u32 count = 0;
        char mimes[kMaxMimes][kMimeLength] = {};
        u32 source_actions = 0;  ///< `wl_data_offer.source_actions` (bits of `wl_data_device_manager.dnd_action`).
        u32 action = 0;          ///< `wl_data_offer.action`: the action the compositor chose.
    };
    struct Source
    {
        void* proxy = nullptr;
        ClipboardFormat format = ClipboardFormat::Text;
        char custom_name[kMimeLength] = {};
        Vector<byte> bytes;
        WaylandClipboard* owner = nullptr;
        ClipboardKind kind = ClipboardKind::Clipboard;
        bool drag = false;  ///< The source of a drag, not of a selection.
    };
    struct Writer
    {
        bool active = false;
        PipeWriter pipe;
    };
    struct Reader
    {
        bool active = false;
        u32 id = 0;
        ClipboardFormat format = ClipboardFormat::Text;
        usize last_size = 0;
        PipeReader pipe;
    };

    static void HandleDeviceDataOffer(void* data, wl_data_device* device, wl_data_offer* offer);
    static void HandleDeviceEnter(void* data, wl_data_device* device, u32 serial, wl_surface* surface, wl_fixed_t x,
                                  wl_fixed_t y, wl_data_offer* offer);
    static void HandleDeviceLeave(void* data, wl_data_device* device);
    static void HandleDeviceMotion(void* data, wl_data_device* device, u32 time, wl_fixed_t x, wl_fixed_t y);
    static void HandleDeviceDrop(void* data, wl_data_device* device);
    static void HandleDeviceSelection(void* data, wl_data_device* device, wl_data_offer* offer);
    static void HandleOfferOffer(void* data, wl_data_offer* offer, const char* mime);
    static void HandleOfferSourceActions(void* data, wl_data_offer* offer, u32 actions);
    static void HandleOfferAction(void* data, wl_data_offer* offer, u32 action);
    static void HandleSourceTarget(void* data, wl_data_source* source, const char* mime);
    static void HandleSourceSend(void* data, wl_data_source* source, const char* mime, i32 fd);
    static void HandleSourceCancelled(void* data, wl_data_source* source);
    static void HandleSourceDropPerformed(void* data, wl_data_source* source);
    static void HandleSourceFinished(void* data, wl_data_source* source);
    static void HandleSourceAction(void* data, wl_data_source* source, u32 action);
    static void HandlePrimaryDeviceDataOffer(void* data, zwp_primary_selection_device_v1* device,
                                             zwp_primary_selection_offer_v1* offer);
    static void HandlePrimaryDeviceSelection(void* data, zwp_primary_selection_device_v1* device,
                                             zwp_primary_selection_offer_v1* offer);
    static void HandlePrimaryOfferOffer(void* data, zwp_primary_selection_offer_v1* offer, const char* mime);
    static void HandlePrimarySourceSend(void* data, zwp_primary_selection_source_v1* source, const char* mime, i32 fd);
    static void HandlePrimarySourceCancelled(void* data, zwp_primary_selection_source_v1* source);

    void SendFrom(Source& source, const char* mime, int fd);
    void DragEvent(InputEventType type, f64 x, f64 y, u8 formats, u8 actions, DragAction action);
    void ReplyToDrag();
    void DndPosition(bool first);
    bool PayloadForMime(const Source& source, const char* mime, Vector<byte>& payload) const;
    static const wl_data_source_listener& SourceListener();
    void ResetDrop();
    void EndDrag(DragAction action);
    u32 StartRead(ClipboardKind kind, void* offer_proxy, const char* mime, ClipboardFormat format,
                  InputEventType event_type);
    u8 DropFormats(const OfferSlot& slot) const;
    OfferSlot* NewOffer(ClipboardKind kind, void* proxy);
    void DestroyOffer(OfferSlot& slot);
    void AddMime(OfferSlot& slot, const char* mime);
    void Selection(ClipboardKind kind, void* offer_proxy);
    void Send(ClipboardKind kind, const char* mime, int fd);
    void Lost(ClipboardKind kind, void* source_proxy);
    void DestroySource(ClipboardKind kind);
    void QueueChanged(ClipboardKind kind);
    const char* SelectMime(const OfferSlot& slot, ClipboardFormat format, StringView custom_name,
                           bool* latin_or_plain) const;

    static constexpr usize K = static_cast<usize>(ClipboardKind::Count);
    wl_display* display_ = nullptr;
    wl_data_device* device_ = nullptr;
    zwp_primary_selection_device_v1* primary_device_ = nullptr;
    wl_data_device_manager* manager_ = nullptr;
    zwp_primary_selection_device_manager_v1* primary_manager_ = nullptr;
    InputEventQueue* queue_ = nullptr;
    u32 last_serial_ = 0;
    u32 button_serial_ = 0;  // of the last pointer button press
    OfferSlot offers_[kOfferSlots];
    i32 current_[K] = {-1, -1};
    Source sources_[K];
    Writer writers_[kWriters];
    Reader readers_[kReaders];
    ClipboardRequests requests_;
    u64 now_ns_ = 0;
    wl_surface* surface_ = nullptr;
    WaylandConnection* connection_ = nullptr;
    // Receiving a drag.
    DropAccept accept_;
    char accept_custom_[kMimeLength] = {};
    i32 dnd_slot_ = -1;
    u32 dnd_serial_ = 0;
    bool dnd_entered_ = false;
    bool dnd_dropped_ = false;
    DragAction dnd_answer_ = DragAction::Copy;
    bool dnd_answer_set_ = false;
    f64 dnd_x_ = 0.0;
    f64 dnd_y_ = 0.0;
    u64 dnd_deadline_ns_ = 0;
    // Sending a drag.
    Source drag_offers_[kMaxDragOffers];
    bool drag_active_ = false;
    bool drag_performed_ = false;
    u32 drag_wl_action_ = 0;
    wl_surface* drag_icon_ = nullptr;
    wl_buffer* drag_icon_buffer_ = nullptr;
};

}  // namespace vse::os::wayland
