#include "platform/linux/wayland/wayland_clipboard.h"

#include <unistd.h>

#include <fcntl.h>

#include "log/log.h"
#include "os/os.h"
#include "platform/linux/wayland/wayland_cursor.h"

namespace engine_placeholder::os::wayland {

namespace {

constexpr const char* kMimeUtf8 = "text/plain;charset=utf-8";
constexpr const char* kMimePlain = "text/plain";
constexpr const char* kMimeUtf8String = "UTF8_STRING";
constexpr const char* kMimeUriList = "text/uri-list";

bool Same(const char* a, const char* b) {
    while (*a != '\0' && *a == *b) {
        ++a;
        ++b;
    }
    return *a == *b;
}

bool Same(const char* a, StringView b) {
    usize i = 0;
    for (; i < b.Length(); ++i) {
        if (a[i] == '\0' || a[i] != b[i]) return false;
    }
    return a[i] == '\0';
}

usize KindIdx(ClipboardKind kind) { return static_cast<usize>(kind); }

void CopyName(char* dst, usize cap, StringView src) {
    const usize n = src.Length() < cap - 1 ? src.Length() : cap - 1;
    for (usize i = 0; i < n; ++i) dst[i] = src[i];
    dst[n] = '\0';
}

} // namespace

WaylandClipboard::~WaylandClipboard() { Shutdown(); }

bool WaylandClipboard::Init(WaylandConnection& connection, InputEventQueue* queue) {
    queue_ = queue;
    requests_.Init(queue);
    display_ = connection.Display();
    connection_ = &connection;
    manager_ = connection.DataDeviceManager();
    primary_manager_ = connection.PrimarySelectionManager();
    if (manager_ == nullptr || connection.Seat() == nullptr) return false;
    static const wl_data_device_listener kDeviceListener = {
        .data_offer = HandleDeviceDataOffer,
        .enter = HandleDeviceEnter,
        .leave = HandleDeviceLeave,
        .motion = HandleDeviceMotion,
        .drop = HandleDeviceDrop,
        .selection = HandleDeviceSelection,
    };
    device_ = wl_data_device_manager_get_data_device(manager_, connection.Seat());
    wl_data_device_add_listener(device_, &kDeviceListener, this);
    if (primary_manager_ != nullptr) {
        static const zwp_primary_selection_device_v1_listener kPrimaryListener = {
            .data_offer = HandlePrimaryDeviceDataOffer,
            .selection = HandlePrimaryDeviceSelection,
        };
        primary_device_ = zwp_primary_selection_device_manager_v1_get_device(primary_manager_, connection.Seat());
        zwp_primary_selection_device_v1_add_listener(primary_device_, &kPrimaryListener, this);
    }
    return true;
}

void WaylandClipboard::Shutdown() {
    if (display_ == nullptr) return;
    requests_.FailAll(ClipboardStatus::Failed, now_ns_);
    for (Writer& w : writers_) {
        w.pipe.Close();
        w.active = false;
    }
    for (Reader& r : readers_) {
        r.pipe.Close();
        r.active = false;
    }
    if (drag_active_) EndDrag(DragAction::Nothing);
    ResetDrop();
    DestroySource(ClipboardKind::Clipboard);
    DestroySource(ClipboardKind::Primary);
    for (OfferSlot& slot : offers_) {
        if (slot.used) DestroyOffer(slot);
    }
    if (device_ != nullptr) {
        if (wl_data_device_get_version(device_) >= WL_DATA_DEVICE_RELEASE_SINCE_VERSION) wl_data_device_release(device_);
        else wl_data_device_destroy(device_);
        device_ = nullptr;
    }
    if (primary_device_ != nullptr) {
        zwp_primary_selection_device_v1_destroy(primary_device_);
        primary_device_ = nullptr;
    }
    display_ = nullptr;
}

bool WaylandClipboard::Supports(ClipboardKind kind) const {
    if (device_ == nullptr) return false;
    return kind == ClipboardKind::Clipboard || (kind == ClipboardKind::Primary && primary_device_ != nullptr);
}

void WaylandClipboard::QueueChanged(ClipboardKind kind) {
    if (queue_ == nullptr) return;
    InputEvent event;
    event.type = InputEventType::ClipboardChanged;
    event.clipboard_kind = static_cast<u8>(kind);
    event.time_ns = now_ns_;
    queue_->Push(event);
}

// ---- offers -------------------------------------------------------------------------------------------------------

WaylandClipboard::OfferSlot* WaylandClipboard::NewOffer(ClipboardKind kind, void* proxy) {
    for (OfferSlot& slot : offers_) {
        if (slot.used) continue;
        slot = OfferSlot();
        slot.owner = this;
        slot.kind = kind;
        slot.used = true;
        slot.proxy = proxy;
        return &slot;
    }
    // Full: reclaim an offer that is not the current selection (a compositor that never sends `selection` for an offer would otherwise fill the pool).
    for (OfferSlot& slot : offers_) {
        const i32 index = static_cast<i32>(&slot - offers_);
        if (index == current_[0] || index == current_[1] || index == dnd_slot_) continue;
        DestroyOffer(slot);
        slot = OfferSlot();
        slot.owner = this;
        slot.kind = kind;
        slot.used = true;
        slot.proxy = proxy;
        return &slot;
    }
    return nullptr;
}

void WaylandClipboard::DestroyOffer(OfferSlot& slot) {
    if (!slot.used) return;
    if (slot.proxy != nullptr) {
        if (slot.kind == ClipboardKind::Clipboard) wl_data_offer_destroy(static_cast<wl_data_offer*>(slot.proxy));
        else zwp_primary_selection_offer_v1_destroy(static_cast<zwp_primary_selection_offer_v1*>(slot.proxy));
    }
    slot.proxy = nullptr;
    slot.used = false;
}

void WaylandClipboard::AddMime(OfferSlot& slot, const char* mime) {
    if (slot.count >= kMaxMimes) return;
    usize n = 0;
    while (mime[n] != '\0' && n < kMimeLength - 1) {
        slot.mimes[slot.count][n] = mime[n];
        ++n;
    }
    slot.mimes[slot.count][n] = '\0';
    ++slot.count;
}

void WaylandClipboard::Selection(ClipboardKind kind, void* offer_proxy) {
    i32& current = current_[KindIdx(kind)];
    if (current >= 0) {
        DestroyOffer(offers_[current]);
        current = -1;
    }
    if (offer_proxy != nullptr) {
        for (u32 i = 0; i < kOfferSlots; ++i) {
            if (offers_[i].used && offers_[i].proxy == offer_proxy) current = static_cast<i32>(i);
        }
    }
    QueueChanged(kind);
}

void WaylandClipboard::HandleDeviceDataOffer(void* data, wl_data_device*, wl_data_offer* offer) {
    auto* self = static_cast<WaylandClipboard*>(data);
    OfferSlot* slot = self->NewOffer(ClipboardKind::Clipboard, offer);
    if (slot == nullptr) {
        wl_data_offer_destroy(offer);
        return;
    }
    static const wl_data_offer_listener kListener = {
        .offer = HandleOfferOffer,
        .source_actions = HandleOfferSourceActions,
        .action = HandleOfferAction,
    };
    wl_data_offer_add_listener(offer, &kListener, slot);
}

void WaylandClipboard::HandleOfferOffer(void* data, wl_data_offer*, const char* mime) {
    auto* slot = static_cast<OfferSlot*>(data);
    slot->owner->AddMime(*slot, mime);
}

void WaylandClipboard::HandleOfferSourceActions(void* data, wl_data_offer*, u32 actions) { static_cast<OfferSlot*>(data)->source_actions = actions; }
void WaylandClipboard::HandleOfferAction(void* data, wl_data_offer*, u32 action) { static_cast<OfferSlot*>(data)->action = action; }

void WaylandClipboard::HandleDeviceEnter(void* data, wl_data_device*, u32 serial, wl_surface*, wl_fixed_t x, wl_fixed_t y, wl_data_offer* offer) {
    auto* self = static_cast<WaylandClipboard*>(data);
    self->ResetDrop(); // a drag still open from before is over
    self->dnd_serial_ = serial;
    if (offer == nullptr) return;
    for (u32 i = 0; i < kOfferSlots; ++i) {
        if (self->offers_[i].used && self->offers_[i].proxy == offer) self->dnd_slot_ = static_cast<i32>(i);
    }
    if (self->dnd_slot_ < 0) return;
    self->dnd_x_ = wl_fixed_to_double(x);
    self->dnd_y_ = wl_fixed_to_double(y);
    self->DndPosition(true);
}

void WaylandClipboard::HandleDeviceMotion(void* data, wl_data_device*, u32, wl_fixed_t x, wl_fixed_t y) {
    auto* self = static_cast<WaylandClipboard*>(data);
    if (self->dnd_slot_ < 0 || self->dnd_dropped_) return;
    self->dnd_x_ = wl_fixed_to_double(x);
    self->dnd_y_ = wl_fixed_to_double(y);
    self->DndPosition(false);
}

void WaylandClipboard::HandleDeviceLeave(void* data, wl_data_device*) {
    auto* self = static_cast<WaylandClipboard*>(data);
    if (self->dnd_slot_ < 0 || self->dnd_dropped_) return; // after a drop the offer lives until it is finished
    if (self->dnd_entered_) self->DragEvent(InputEventType::DragLeave, 0, 0, 0, 0, DragAction::Nothing);
    self->ResetDrop();
}

void WaylandClipboard::HandleDeviceDrop(void* data, wl_data_device*) {
    auto* self = static_cast<WaylandClipboard*>(data);
    if (self->dnd_slot_ < 0 || !self->dnd_entered_) return;
    const OfferSlot& slot = self->offers_[self->dnd_slot_];
    const u8 formats = self->DropFormats(slot) & self->accept_.formats;
    if (formats == 0 || self->dnd_answer_ == DragAction::Nothing) {
        self->ResetDrop();
        return;
    }
    self->dnd_dropped_ = true;
    self->dnd_deadline_ns_ = (self->now_ns_ = os::MonotonicTimeNanos()) + kDropSessionTimeoutNs;
    DragAction action = slot.action == 2 ? DragAction::Move : slot.action == 1 ? DragAction::Copy : self->dnd_answer_;
    self->DragEvent(InputEventType::Drop, self->dnd_x_, self->dnd_y_, formats, static_cast<u8>(((slot.source_actions & 1) ? 1 : 0) | ((slot.source_actions & 2) ? 2 : 0)), action);
}

void WaylandClipboard::HandleDeviceSelection(void* data, wl_data_device*, wl_data_offer* offer) {
    static_cast<WaylandClipboard*>(data)->Selection(ClipboardKind::Clipboard, offer);
}

void WaylandClipboard::HandlePrimaryDeviceDataOffer(void* data, zwp_primary_selection_device_v1*, zwp_primary_selection_offer_v1* offer) {
    auto* self = static_cast<WaylandClipboard*>(data);
    OfferSlot* slot = self->NewOffer(ClipboardKind::Primary, offer);
    if (slot == nullptr) {
        zwp_primary_selection_offer_v1_destroy(offer);
        return;
    }
    static const zwp_primary_selection_offer_v1_listener kListener = {.offer = HandlePrimaryOfferOffer};
    zwp_primary_selection_offer_v1_add_listener(offer, &kListener, slot);
}

void WaylandClipboard::HandlePrimaryOfferOffer(void* data, zwp_primary_selection_offer_v1*, const char* mime) {
    auto* slot = static_cast<OfferSlot*>(data);
    slot->owner->AddMime(*slot, mime);
}

void WaylandClipboard::HandlePrimaryDeviceSelection(void* data, zwp_primary_selection_device_v1*, zwp_primary_selection_offer_v1* offer) {
    static_cast<WaylandClipboard*>(data)->Selection(ClipboardKind::Primary, offer);
}

// ---- sources ------------------------------------------------------------------------------------------------------

void WaylandClipboard::DestroySource(ClipboardKind kind) {
    Source& source = sources_[KindIdx(kind)];
    if (source.proxy != nullptr) {
        if (kind == ClipboardKind::Clipboard) wl_data_source_destroy(static_cast<wl_data_source*>(source.proxy));
        else zwp_primary_selection_source_v1_destroy(static_cast<zwp_primary_selection_source_v1*>(source.proxy));
    }
    source.proxy = nullptr;
    source.bytes.Reset();
}

void WaylandClipboard::Lost(ClipboardKind kind, void* source_proxy) {
    Source& source = sources_[KindIdx(kind)];
    if (source.proxy != source_proxy) return; // a source already replaced
    DestroySource(kind);
    QueueChanged(kind);
}

void WaylandClipboard::HandleSourceTarget(void*, wl_data_source*, const char*) {}

void WaylandClipboard::HandleSourceDropPerformed(void* data, wl_data_source*) {
    auto* source = static_cast<Source*>(data);
    if (source->drag) source->owner->drag_performed_ = true;
}

void WaylandClipboard::HandleSourceFinished(void* data, wl_data_source*) {
    auto* source = static_cast<Source*>(data);
    if (!source->drag) return;
    WaylandClipboard* self = source->owner;
    self->EndDrag(self->drag_wl_action_ == 2 ? DragAction::Move : self->drag_wl_action_ == 1 ? DragAction::Copy : DragAction::Nothing);
}

void WaylandClipboard::HandleSourceAction(void* data, wl_data_source*, u32 action) {
    auto* source = static_cast<Source*>(data);
    if (source->drag) source->owner->drag_wl_action_ = action;
}

void WaylandClipboard::HandleSourceSend(void* data, wl_data_source*, const char* mime, i32 fd) {
    auto* source = static_cast<Source*>(data);
    source->owner->SendFrom(*source, mime, fd);
}

void WaylandClipboard::HandleSourceCancelled(void* data, wl_data_source* proxy) {
    auto* source = static_cast<Source*>(data);
    if (source->drag) source->owner->EndDrag(DragAction::Nothing);
    else source->owner->Lost(source->kind, proxy);
}

void WaylandClipboard::HandlePrimarySourceSend(void* data, zwp_primary_selection_source_v1*, const char* mime, i32 fd) {
    auto* source = static_cast<Source*>(data);
    source->owner->Send(source->kind, mime, fd);
}

void WaylandClipboard::HandlePrimarySourceCancelled(void* data, zwp_primary_selection_source_v1* proxy) {
    auto* source = static_cast<Source*>(data);
    source->owner->Lost(source->kind, proxy);
}

void WaylandClipboard::Send(ClipboardKind kind, const char* mime, int fd) { SendFrom(sources_[KindIdx(kind)], mime, fd); }

bool WaylandClipboard::PayloadForMime(const Source& source, const char* mime, Vector<byte>& payload) const {
    if (source.proxy == nullptr) return false;
    bool ok = false;
    if (source.format == ClipboardFormat::Text) {
        ok = Same(mime, kMimeUtf8) || Same(mime, kMimePlain) || Same(mime, kMimeUtf8String);
        if (ok) {
            for (usize i = 0; i < source.bytes.Size(); ++i) payload.Push(source.bytes[i]);
        }
    } else if (source.format == ClipboardFormat::FileList) {
        ok = Same(mime, kMimeUriList);
        if (ok) BuildUriList(source.bytes.Data(), source.bytes.Size(), payload);
    } else {
        ok = Same(mime, source.custom_name);
        if (ok) {
            for (usize i = 0; i < source.bytes.Size(); ++i) payload.Push(source.bytes[i]);
        }
    }
    return ok;
}

void WaylandClipboard::SendFrom(Source& source, const char* mime, int fd) {
    Vector<byte> payload;
    bool ok = false;
    if (source.drag) {
        // The offers of a drag share one wl_data_source: the first that serves the type answers.
        for (usize i = 0; i < kMaxDragOffers && !ok; ++i) ok = PayloadForMime(drag_offers_[i], mime, payload);
    } else {
        ok = PayloadForMime(source, mime, payload);
    }
    Writer* slot = nullptr;
    for (Writer& w : writers_) {
        if (!w.active) {
            slot = &w;
            break;
        }
    }
    if (!ok || slot == nullptr) {
        ::close(fd); // the peer sees end of file: an empty answer
        return;
    }
    slot->active = true;
    slot->pipe.Start(fd, payload.Data(), payload.Size(), (now_ns_ = os::MonotonicTimeNanos()), kClipboardTimeoutNs);
}

bool WaylandClipboard::Set(ClipboardKind kind, const ClipboardOffer& offer) {
    if (!Supports(kind) || last_serial_ == 0) return false;
    if (offer.size > kMaxClipboardBytes || (offer.size > 0 && offer.data == nullptr)) return false;
    if (offer.format == ClipboardFormat::Custom && (offer.custom_name.Length() == 0 || offer.custom_name.Length() >= kMimeLength)) return false;
    Source next;
    next.format = offer.format;
    next.owner = this;
    next.kind = kind;
    if (offer.format == ClipboardFormat::Text) {
        SanitizeUtf8(offer.data, offer.size, next.bytes, nullptr);
    } else {
        for (usize i = 0; i < offer.size; ++i) next.bytes.Push(offer.data[i]);
    }
    if (offer.format == ClipboardFormat::Custom) CopyName(next.custom_name, kMimeLength, offer.custom_name);

    DestroySource(kind); // the old source is gone before the new one is set, so its `cancelled` cannot reach the new one
    Source& source = sources_[KindIdx(kind)];
    source = static_cast<Source&&>(next);
    const char* mimes[4] = {nullptr, nullptr, nullptr, nullptr};
    u32 count = 0;
    if (offer.format == ClipboardFormat::Text) {
        mimes[count++] = kMimeUtf8;
        mimes[count++] = kMimePlain;
        mimes[count++] = kMimeUtf8String;
    } else if (offer.format == ClipboardFormat::FileList) {
        mimes[count++] = kMimeUriList;
    } else {
        mimes[count++] = source.custom_name;
    }
    if (kind == ClipboardKind::Clipboard) {
        wl_data_source* proxy = wl_data_device_manager_create_data_source(manager_);
        wl_data_source_add_listener(proxy, &SourceListener(), &source);
        for (u32 i = 0; i < count; ++i) wl_data_source_offer(proxy, mimes[i]);
        wl_data_device_set_selection(device_, proxy, last_serial_);
        source.proxy = proxy;
    } else {
        static const zwp_primary_selection_source_v1_listener kListener = {
            .send = HandlePrimarySourceSend,
            .cancelled = HandlePrimarySourceCancelled,
        };
        zwp_primary_selection_source_v1* proxy = zwp_primary_selection_device_manager_v1_create_source(primary_manager_);
        zwp_primary_selection_source_v1_add_listener(proxy, &kListener, &source);
        for (u32 i = 0; i < count; ++i) zwp_primary_selection_source_v1_offer(proxy, mimes[i]);
        zwp_primary_selection_device_v1_set_selection(primary_device_, proxy, last_serial_);
        source.proxy = proxy;
    }
    wl_display_flush(display_);
    return true;
}

// ---- requests -----------------------------------------------------------------------------------------------------

const char* WaylandClipboard::SelectMime(const OfferSlot& slot, ClipboardFormat format, StringView custom_name, bool* plain) const {
    *plain = false;
    auto find = [&slot](const char* mime) -> const char* {
        for (u32 i = 0; i < slot.count; ++i) {
            if (Same(slot.mimes[i], mime)) return slot.mimes[i];
        }
        return nullptr;
    };
    if (format == ClipboardFormat::Text) {
        const char* m = find(kMimeUtf8);
        if (m == nullptr) m = find(kMimeUtf8String);
        if (m == nullptr) m = find(kMimePlain);
        return m;
    }
    if (format == ClipboardFormat::FileList) return find(kMimeUriList);
    for (u32 i = 0; i < slot.count; ++i) {
        if (Same(slot.mimes[i], custom_name)) return slot.mimes[i];
    }
    return nullptr;
}

bool WaylandClipboard::Has(ClipboardKind kind, ClipboardFormat format, StringView custom_name) const {
    if (!Supports(kind)) return false;
    const Source& source = sources_[KindIdx(kind)];
    if (source.proxy != nullptr) {
        if (source.format != format) return false;
        return format != ClipboardFormat::Custom || Same(source.custom_name, custom_name);
    }
    const i32 index = current_[KindIdx(kind)];
    if (index < 0) return false;
    bool plain = false;
    return SelectMime(offers_[index], format, custom_name, &plain) != nullptr;
}

u32 WaylandClipboard::Request(ClipboardKind kind, ClipboardFormat format, StringView custom_name) {
    if (!Supports(kind)) return 0;
    if (format == ClipboardFormat::Custom && custom_name.Length() == 0) return 0;
    Reader* slot = nullptr;
    for (Reader& r : readers_) {
        if (!r.active) {
            slot = &r;
            break;
        }
    }
    if (slot == nullptr) return 0;
    const u32 id = requests_.Begin(kind, format, (now_ns_ = os::MonotonicTimeNanos()));
    if (id == 0) return 0;
    Source& source = sources_[KindIdx(kind)];
    if (source.proxy != nullptr) {
        // Our own selection: answered without the compositor.
        if (source.format != format || (format == ClipboardFormat::Custom && !Same(source.custom_name, custom_name))) {
            requests_.Fail(id, ClipboardStatus::Missing, now_ns_);
            return id;
        }
        Vector<byte> out;
        for (usize i = 0; i < source.bytes.Size(); ++i) out.Push(source.bytes[i]);
        requests_.Complete(id, ClipboardStatus::Ok, static_cast<Vector<byte>&&>(out), 0, 0, now_ns_);
        return id;
    }
    const i32 index = current_[KindIdx(kind)];
    bool plain = false;
    const char* mime = index >= 0 ? SelectMime(offers_[index], format, custom_name, &plain) : nullptr;
    if (mime == nullptr) {
        requests_.Fail(id, ClipboardStatus::Missing, now_ns_);
        return id;
    }
    int fds[2];
    if (::pipe2(fds, O_CLOEXEC) != 0) {
        requests_.Fail(id, ClipboardStatus::Failed, now_ns_);
        return id;
    }
    void* proxy = offers_[index].proxy;
    if (kind == ClipboardKind::Clipboard) wl_data_offer_receive(static_cast<wl_data_offer*>(proxy), mime, fds[1]);
    else zwp_primary_selection_offer_v1_receive(static_cast<zwp_primary_selection_offer_v1*>(proxy), mime, fds[1]);
    wl_display_flush(display_);
    ::close(fds[1]); // the peer holds its copy; end of file arrives when it closes
    slot->active = true;
    slot->id = id;
    slot->format = format;
    slot->last_size = 0;
    slot->pipe.Start(fds[0], kMaxClipboardBytes, now_ns_, kClipboardTimeoutNs);
    return id;
}

void WaylandClipboard::Tick(u64 now_ns) {
    now_ns_ = now_ns;
    for (Writer& w : writers_) {
        if (!w.active) continue;
        const PipeWriter::State state = w.pipe.Pump(now_ns);
        if (state != PipeWriter::State::Writing) {
            w.pipe.Close();
            w.active = false;
        }
    }
    for (Reader& r : readers_) {
        if (!r.active) continue;
        const PipeReader::State state = r.pipe.Pump(now_ns);
        if (state == PipeReader::State::Reading) {
            if (r.pipe.Bytes().Size() != r.last_size) {
                r.last_size = r.pipe.Bytes().Size();
                requests_.Touch(r.id, now_ns); // progress keeps the request alive
            }
            if (!requests_.Pending(r.id)) {
                r.pipe.Close();
                r.active = false;
            }
            continue;
        }
        r.active = false;
        if (state == PipeReader::State::Done) {
            Vector<byte> out;
            u32 replaced = 0;
            u32 rejected = 0;
            Vector<byte>& raw = r.pipe.Bytes();
            if (r.format == ClipboardFormat::Text) SanitizeUtf8(raw.Data(), raw.Size(), out, &replaced);
            else if (r.format == ClipboardFormat::FileList) {
                ParseUriList(raw.Data(), raw.Size(), out, &rejected);
                rejected += LimitPathList(out, kMaxDropFileEntries);
            }
            else out = static_cast<Vector<byte>&&>(raw);
            requests_.Complete(r.id, ClipboardStatus::Ok, static_cast<Vector<byte>&&>(out), replaced, rejected, now_ns);
        } else if (state == PipeReader::State::TooLarge) {
            requests_.Fail(r.id, ClipboardStatus::TooLarge, now_ns);
        } else if (state == PipeReader::State::TimedOut) {
            requests_.Fail(r.id, ClipboardStatus::Timeout, now_ns);
        } else {
            requests_.Fail(r.id, ClipboardStatus::Failed, now_ns);
        }
        r.pipe.Close();
    }
    requests_.Expire(now_ns);
    if (dnd_dropped_ && now_ns >= dnd_deadline_ns_) FinishDrop(DragAction::Nothing); // the program never finished the drop
}

// ---- drag and drop ------------------------------------------------------------------------------------------------

const wl_data_source_listener& WaylandClipboard::SourceListener() {
    static const wl_data_source_listener kListener = {
        .target = HandleSourceTarget,
        .send = HandleSourceSend,
        .cancelled = HandleSourceCancelled,
        .dnd_drop_performed = HandleSourceDropPerformed,
        .dnd_finished = HandleSourceFinished,
        .action = HandleSourceAction,
    };
    return kListener;
}

void WaylandClipboard::DragEvent(InputEventType type, f64 x, f64 y, u8 formats, u8 actions, DragAction action) {
    if (queue_ == nullptr) return;
    InputEvent event;
    event.type = type;
    event.x = x;
    event.y = y;
    event.drag_formats = formats;
    event.drag_actions = actions;
    event.drag_action = static_cast<u8>(action);
    event.time_ns = now_ns_;
    queue_->Push(event);
}

u8 WaylandClipboard::DropFormats(const OfferSlot& slot) const {
    u8 formats = 0;
    bool plain = false;
    if (SelectMime(slot, ClipboardFormat::Text, StringView(), &plain) != nullptr) formats |= kDropText;
    if (SelectMime(slot, ClipboardFormat::FileList, StringView(), &plain) != nullptr) formats |= kDropFiles;
    if ((accept_.formats & kDropCustom) != 0 && SelectMime(slot, ClipboardFormat::Custom, StringView(accept_custom_), &plain) != nullptr) formats |= kDropCustom;
    return formats;
}

void WaylandClipboard::SetDropAccept(const DropAccept& accept) {
    accept_.formats = accept.formats & (kDropText | kDropFiles | kDropCustom);
    accept_custom_[0] = '\0';
    if ((accept_.formats & kDropCustom) != 0) {
        if (accept.custom_name.Length() == 0 || accept.custom_name.Length() >= kMimeLength) accept_.formats &= static_cast<u8>(~kDropCustom);
        else CopyName(accept_custom_, kMimeLength, accept.custom_name);
    }
}

void WaylandClipboard::ReplyToDrag() {
    if (dnd_slot_ < 0 || !offers_[dnd_slot_].used) return;
    const OfferSlot& slot = offers_[dnd_slot_];
    auto* offer = static_cast<wl_data_offer*>(slot.proxy);
    const u8 formats = DropFormats(slot) & accept_.formats;
    const bool accepted = formats != 0 && dnd_answer_ != DragAction::Nothing;
    const char* mime = nullptr;
    bool plain = false;
    if (accepted) {
        if ((formats & kDropText) != 0) mime = SelectMime(slot, ClipboardFormat::Text, StringView(), &plain);
        else if ((formats & kDropFiles) != 0) mime = SelectMime(slot, ClipboardFormat::FileList, StringView(), &plain);
        else mime = SelectMime(slot, ClipboardFormat::Custom, StringView(accept_custom_), &plain);
    }
    wl_data_offer_accept(offer, dnd_serial_, mime);
    if (wl_data_offer_get_version(offer) >= 3) {
        const u32 wanted = dnd_answer_ == DragAction::Move ? 2u : 1u; // Wayland has copy and move (and ask, unused); a link is a copy
        wl_data_offer_set_actions(offer, accepted ? 3u : 0u, accepted ? wanted : 0u);
    }
    wl_display_flush(display_);
}

void WaylandClipboard::DndPosition(bool first) {
    const OfferSlot& slot = offers_[dnd_slot_];
    const u8 formats = DropFormats(slot) & accept_.formats;
    const u8 actions = static_cast<u8>(((slot.source_actions & 1) ? 1 : 0) | ((slot.source_actions & 2) ? 2 : 0));
    if (first) {
        dnd_answer_set_ = false;
    }
    if (!dnd_answer_set_) {
        if (formats == 0) dnd_answer_ = DragAction::Nothing;
        else if (actions == 0 || (actions & 1) != 0) dnd_answer_ = DragAction::Copy;
        else dnd_answer_ = DragAction::Move;
    }
    if (formats == 0) dnd_answer_ = DragAction::Nothing;
    ReplyToDrag();
    if (!dnd_entered_) {
        dnd_entered_ = true;
        DragEvent(InputEventType::DragEnter, dnd_x_, dnd_y_, formats, actions, dnd_answer_);
    } else {
        DragEvent(InputEventType::DragOver, dnd_x_, dnd_y_, formats, actions, dnd_answer_);
    }
}

void WaylandClipboard::SetDropAction(DragAction action) {
    if (dnd_slot_ < 0 || dnd_dropped_) return;
    dnd_answer_ = action;
    dnd_answer_set_ = true;
    ReplyToDrag();
}

void WaylandClipboard::ResetDrop() {
    if (dnd_slot_ >= 0) DestroyOffer(offers_[dnd_slot_]);
    dnd_slot_ = -1;
    dnd_entered_ = false;
    dnd_dropped_ = false;
    dnd_answer_set_ = false;
}

void WaylandClipboard::FinishDrop(DragAction taken) {
    if (!dnd_dropped_ || dnd_slot_ < 0) return;
    auto* offer = static_cast<wl_data_offer*>(offers_[dnd_slot_].proxy);
    // `finish` is a protocol error unless the compositor has chosen an action (offer.action) for this drop.
    if (taken != DragAction::Nothing && offers_[dnd_slot_].action != 0 && wl_data_offer_get_version(offer) >= 3) wl_data_offer_finish(offer);
    ResetDrop();
    wl_display_flush(display_);
}

u32 WaylandClipboard::StartRead(ClipboardKind kind, void* offer_proxy, const char* mime, ClipboardFormat format, InputEventType event_type) {
    Reader* slot = nullptr;
    for (Reader& r : readers_) {
        if (!r.active) {
            slot = &r;
            break;
        }
    }
    if (slot == nullptr) return 0;
    int fds[2];
    if (::pipe2(fds, O_CLOEXEC) != 0) return 0;
    const u32 id = requests_.Begin(kind, format, (now_ns_ = os::MonotonicTimeNanos()), event_type);
    if (id == 0) {
        ::close(fds[0]);
        ::close(fds[1]);
        return 0;
    }
    wl_data_offer_receive(static_cast<wl_data_offer*>(offer_proxy), mime, fds[1]);
    wl_display_flush(display_);
    ::close(fds[1]);
    slot->active = true;
    slot->id = id;
    slot->format = format;
    slot->last_size = 0;
    slot->pipe.Start(fds[0], kMaxClipboardBytes, now_ns_, kClipboardTimeoutNs);
    return id;
}

u32 WaylandClipboard::RequestDropData(ClipboardFormat format, StringView custom_name) {
    if (!dnd_dropped_ || dnd_slot_ < 0) return 0;
    if (format == ClipboardFormat::Custom && (custom_name.Length() == 0 || !Same(accept_custom_, custom_name))) return 0;
    bool plain = false;
    const char* mime = SelectMime(offers_[dnd_slot_], format, custom_name, &plain);
    if (mime == nullptr) return 0;
    return StartRead(ClipboardKind::Clipboard, offers_[dnd_slot_].proxy, mime, format, InputEventType::DropData);
}

bool WaylandClipboard::BeginDrag(const DragSource& source) {
    if (device_ == nullptr || manager_ == nullptr || surface_ == nullptr || button_serial_ == 0 || drag_active_) return false;
    if (source.offers == nullptr || source.offer_count == 0 || source.offer_count > kMaxDragOffers) return false;
    for (usize i = 0; i < source.offer_count; ++i) {
        const ClipboardOffer& o = source.offers[i];
        if (o.size > kMaxClipboardBytes || (o.size > 0 && o.data == nullptr)) return false;
        if (o.format == ClipboardFormat::Custom && (o.custom_name.Length() == 0 || o.custom_name.Length() >= kMimeLength)) return false;
    }
    for (Source& s : drag_offers_) s = Source();
    wl_data_source* proxy = wl_data_device_manager_create_data_source(manager_);
    for (usize i = 0; i < source.offer_count; ++i) {
        const ClipboardOffer& o = source.offers[i];
        Source& s = drag_offers_[i];
        s.owner = this;
        s.drag = true;
        s.format = o.format;
        s.proxy = proxy; // marks the offer as live; the one proxy serves all of them
        if (o.format == ClipboardFormat::Text) {
            SanitizeUtf8(o.data, o.size, s.bytes, nullptr);
            wl_data_source_offer(proxy, kMimeUtf8);
            wl_data_source_offer(proxy, kMimePlain);
            wl_data_source_offer(proxy, kMimeUtf8String);
        } else if (o.format == ClipboardFormat::FileList) {
            for (usize k = 0; k < o.size; ++k) s.bytes.Push(o.data[k]);
            wl_data_source_offer(proxy, kMimeUriList);
        } else {
            for (usize k = 0; k < o.size; ++k) s.bytes.Push(o.data[k]);
            CopyName(s.custom_name, kMimeLength, o.custom_name);
            wl_data_source_offer(proxy, s.custom_name);
        }
    }
    wl_data_source_add_listener(proxy, &SourceListener(), &drag_offers_[0]);
    const u8 allowed = source.allowed_actions & kAllDragActions;
    wl_data_source_set_actions(proxy, (((allowed & 1) != 0) ? 1u : 0u) | (((allowed & 2) != 0) ? 2u : 0u));
    // The image: a surface with a shm buffer, offset so that the hotspot is under the pointer.
    drag_icon_ = nullptr;
    drag_icon_buffer_ = nullptr;
    const DragImage& image = source.image;
    if (image.rgba != nullptr && image.width > 0 && image.height > 0 && image.width <= 128 && image.height <= 128 && connection_ != nullptr && connection_->Shm() != nullptr) {
        drag_icon_buffer_ = CreateArgbShmBuffer(connection_->Shm(), image.rgba, image.width, image.height);
        if (drag_icon_buffer_ != nullptr) {
            drag_icon_ = wl_compositor_create_surface(connection_->Compositor());
            wl_surface_attach(drag_icon_, drag_icon_buffer_, -image.hotspot_x, -image.hotspot_y);
            wl_surface_commit(drag_icon_);
        }
    }
    drag_active_ = true;
    drag_performed_ = false;
    drag_wl_action_ = 0;
    wl_data_device_start_drag(device_, proxy, surface_, drag_icon_, button_serial_);
    wl_display_flush(display_);
    return true;
}

void WaylandClipboard::EndDrag(DragAction action) {
    if (!drag_active_) return;
    drag_active_ = false;
    if (drag_offers_[0].proxy != nullptr) wl_data_source_destroy(static_cast<wl_data_source*>(drag_offers_[0].proxy));
    for (Source& s : drag_offers_) s = Source();
    if (drag_icon_ != nullptr) {
        wl_surface_destroy(drag_icon_);
        drag_icon_ = nullptr;
    }
    if (drag_icon_buffer_ != nullptr) {
        wl_buffer_destroy(drag_icon_buffer_);
        drag_icon_buffer_ = nullptr;
    }
    DragEvent(InputEventType::DragEnded, 0, 0, 0, 0, action);
}

void WaylandClipboard::CancelDrag() { EndDrag(DragAction::Nothing); }

bool WaylandClipboard::ListenersComplete() {
    // The initialisers above name every member of each listener struct; a protocol version that adds an event fails to compile there (designated initialisers
    // leave new members null, which this check cannot see), so the table sizes are compared with the event counts of the bound versions.
    return wl_data_device_interface.event_count == 6 && wl_data_offer_interface.event_count == 3 && wl_data_source_interface.event_count == 6 &&
           zwp_primary_selection_device_v1_interface.event_count == 2 && zwp_primary_selection_offer_v1_interface.event_count == 1 &&
           zwp_primary_selection_source_v1_interface.event_count == 2;
}

} // namespace engine_placeholder::os::wayland
