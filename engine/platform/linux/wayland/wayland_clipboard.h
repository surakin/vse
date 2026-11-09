#pragma once

#include <wayland-client.h>

#include "containers/vector.h"
#include "platform/clipboard.h"
#include "platform/input_event.h"
#include "platform/linux/clipboard_pipe.h"
#include "platform/linux/wayland/generated/primary-selection-unstable-v1-client-protocol.h"
#include "platform/linux/wayland/wayland_connection.h"
#include "string/string.h"
#include "types.h"

//! The Wayland clipboard and primary selection: a `wl_data_device` and a `zwp_primary_selection_device_v1` on the seat, with the transfers done through non-blocking pipes.

namespace engine_placeholder::os::wayland {

/// The clipboard of a Wayland seat.
///
/// Owning a selection needs the serial of a recent key, button or enter event: the input handler passes each one to [[os::wayland::WaylandClipboard::RecordSerial]], and
/// [[os::wayland::WaylandClipboard::Set]] fails before the first. Data is sent to a peer and received from it through pipes that [[os::wayland::WaylandClipboard::Tick]]
/// advances without blocking (a peer that never reads or writes costs only its timeout). Not movable: the listeners capture `this`.
///
/// Thread-safety: owner-thread-only (the thread that dispatches the connection).
/// Ownership: owns the data devices, the sources and offers it creates, and the pipes; the managers belong to the connection.
class WaylandClipboard {
public:
    WaylandClipboard() = default;
    /// Calls [[os::wayland::WaylandClipboard::Shutdown]].
    ~WaylandClipboard();
    WaylandClipboard(const WaylandClipboard&) = delete;
    WaylandClipboard& operator=(const WaylandClipboard&) = delete;

    /// Creates the data devices on the connection's seat. Events go to `queue`.
    /// Returns: false if the compositor offers no `wl_data_device_manager` (then nothing is supported).
    bool Init(WaylandConnection& connection, InputEventQueue* queue);

    /// Destroys everything this object created, while the display is still connected. Idempotent.
    void Shutdown();

    /// Remembers the serial of an input event, the proof of user action a compositor wants before it accepts a selection.
    void RecordSerial(u32 serial) { last_serial_ = serial; }

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

    /// Whether every event of the `wl_data_device`, `wl_data_offer`, `wl_data_source` and the primary selection objects has a listener function (libwayland aborts on a null slot).
    static bool ListenersComplete();

private:
    static constexpr u32 kMaxMimes = 24;
    static constexpr u32 kMimeLength = 96;
    static constexpr u32 kOfferSlots = 8;
    static constexpr u32 kWriters = 4;
    static constexpr u32 kReaders = 8;
    struct OfferSlot {
        WaylandClipboard* owner = nullptr;
        ClipboardKind kind = ClipboardKind::Clipboard;
        bool used = false;
        void* proxy = nullptr;
        u32 count = 0;
        char mimes[kMaxMimes][kMimeLength] = {};
    };
    struct Source {
        void* proxy = nullptr;
        ClipboardFormat format = ClipboardFormat::Text;
        char custom_name[kMimeLength] = {};
        Vector<byte> bytes;
        WaylandClipboard* owner = nullptr;
        ClipboardKind kind = ClipboardKind::Clipboard;
    };
    struct Writer {
        bool active = false;
        PipeWriter pipe;
    };
    struct Reader {
        bool active = false;
        u32 id = 0;
        ClipboardFormat format = ClipboardFormat::Text;
        usize last_size = 0;
        PipeReader pipe;
    };

    static void HandleDeviceDataOffer(void* data, wl_data_device* device, wl_data_offer* offer);
    static void HandleDeviceEnter(void* data, wl_data_device* device, u32 serial, wl_surface* surface, wl_fixed_t x, wl_fixed_t y, wl_data_offer* offer);
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
    static void HandlePrimaryDeviceDataOffer(void* data, zwp_primary_selection_device_v1* device, zwp_primary_selection_offer_v1* offer);
    static void HandlePrimaryDeviceSelection(void* data, zwp_primary_selection_device_v1* device, zwp_primary_selection_offer_v1* offer);
    static void HandlePrimaryOfferOffer(void* data, zwp_primary_selection_offer_v1* offer, const char* mime);
    static void HandlePrimarySourceSend(void* data, zwp_primary_selection_source_v1* source, const char* mime, i32 fd);
    static void HandlePrimarySourceCancelled(void* data, zwp_primary_selection_source_v1* source);

    OfferSlot* NewOffer(ClipboardKind kind, void* proxy);
    void DestroyOffer(OfferSlot& slot);
    void AddMime(OfferSlot& slot, const char* mime);
    void Selection(ClipboardKind kind, void* offer_proxy);
    void Send(ClipboardKind kind, const char* mime, int fd);
    void Lost(ClipboardKind kind, void* source_proxy);
    void DestroySource(ClipboardKind kind);
    void QueueChanged(ClipboardKind kind);
    const char* SelectMime(const OfferSlot& slot, ClipboardFormat format, StringView custom_name, bool* latin_or_plain) const;

    static constexpr usize K = static_cast<usize>(ClipboardKind::Count);
    wl_display* display_ = nullptr;
    wl_data_device* device_ = nullptr;
    zwp_primary_selection_device_v1* primary_device_ = nullptr;
    wl_data_device_manager* manager_ = nullptr;
    zwp_primary_selection_device_manager_v1* primary_manager_ = nullptr;
    InputEventQueue* queue_ = nullptr;
    u32 last_serial_ = 0;
    OfferSlot offers_[kOfferSlots];
    i32 current_[K] = {-1, -1};
    Source sources_[K];
    Writer writers_[kWriters];
    Reader readers_[kReaders];
    ClipboardRequests requests_;
    u64 now_ns_ = 0;
};

} // namespace engine_placeholder::os::wayland
