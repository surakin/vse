#pragma once

#include "containers/vector.h"
#include "platform/window.h"

// A window for the adapter tests of Day 200: events are queued by the test, clipboard and drag calls are recorded, and the data of a request is kept until the test chooses to finish it.
namespace engine_placeholder {

class ScriptedWindow final : public os::Window {
public:
    struct Offered {
        os::ClipboardFormat format = os::ClipboardFormat::Text;
        os::ClipboardKind kind = os::ClipboardKind::Clipboard;
        Vector<byte> bytes;
        Vector<char> name;
    };
    struct Request {
        u32 id = 0;
        os::ClipboardKind kind = os::ClipboardKind::Clipboard;
        os::ClipboardFormat format = os::ClipboardFormat::Text;
        bool drop = false;
    };

    bool Valid() const override { return true; }
    void PumpEvents() override {}
    bool PollEvent(os::InputEvent* out) override {
        if (next_ >= events.Size()) return false;
        *out = events[next_++];
        return true;
    }
    bool ShouldClose() const override { return false; }
    u32 Width() const override { return width; }
    u32 Height() const override { return height; }
    const char* BackendName() const override { return "scripted"; }
    os::NativeWindowInfo GetNativeInfo() const override { return os::NativeWindowInfo(); }
    bool IsMinimized() const override { return false; }
    u64 ResizeSerial() const override { return 0; }

    bool SupportsClipboard(os::ClipboardKind kind) const override { return kind == os::ClipboardKind::Clipboard || primary_supported; }
    bool SetClipboard(os::ClipboardKind kind, const os::ClipboardOffer& offer) override {
        Offered o;
        o.kind = kind;
        o.format = offer.format;
        for (usize i = 0; i < offer.size; ++i) o.bytes.Push(offer.data[i]);
        offered.Push(static_cast<Offered&&>(o));
        return true;
    }
    u32 RequestClipboard(os::ClipboardKind kind, os::ClipboardFormat format, StringView) override {
        if (refuse_requests) return 0;
        Request r;
        r.id = ++last_id;
        r.kind = kind;
        r.format = format;
        requests.Push(r);
        return r.id;
    }
    bool TakeClipboardResult(u32 request_id, os::ClipboardResult* out) override {
        for (usize i = 0; i < results.Size(); ++i) {
            if (results[i].id == request_id) {
                out->status = results[i].status;
                out->bytes.Clear();
                for (usize k = 0; k < results[i].bytes.Size(); ++k) out->bytes.Push(results[i].bytes[k]);
                results.Remove(i);
                return true;
            }
        }
        return false;
    }
    bool ClipboardHasFormat(os::ClipboardKind, os::ClipboardFormat, StringView) const override { return clipboard_has_text; }

    bool SupportsPointerLock() const override { return pointer_lock_supported; }
    bool SetPointerLock(bool locked) override {
        if (!pointer_lock_supported) return false;
        ++pointer_lock_calls;
        pointer_locked = locked;
        return true;
    }
    bool PointerLocked() const override { return pointer_locked; }

    bool SupportsDragAndDrop() const override { return true; }
    void SetDropAccept(const os::DropAccept& accept) override { accept_formats = accept.formats; }
    void SetDropAction(os::DragAction action) override { last_drop_action = action; }
    u32 RequestDropData(os::ClipboardFormat format, StringView) override {
        if (refuse_requests) return 0;
        Request r;
        r.id = ++last_id;
        r.format = format;
        r.drop = true;
        requests.Push(r);
        return r.id;
    }
    void FinishDrop(os::DragAction taken) override {
        ++finished_drops;
        finished_with = taken;
    }
    bool BeginDrag(const os::DragSource& source) override {
        ++drags_begun;
        offered_drag.Clear();
        for (usize i = 0; i < source.offer_count; ++i) {
            Offered o;
            o.format = source.offers[i].format;
            for (usize k = 0; k < source.offers[i].size; ++k) o.bytes.Push(source.offers[i].data[k]);
            for (usize k = 0; k < source.offers[i].custom_name.Length(); ++k) o.name.Push(source.offers[i].custom_name[k]);
            offered_drag.Push(static_cast<Offered&&>(o));
        }
        drag_allowed = source.allowed_actions;
        return accept_drag;
    }

    // Finishes request `id` with `status` and `text`, queuing the event the window would (a clipboard or a drop-data event).
    void Finish(u32 id, os::ClipboardStatus status, StringView text) {
        Result r;
        r.id = id;
        r.status = status;
        for (usize i = 0; i < text.Length(); ++i) r.bytes.Push(static_cast<byte>(text[i]));
        results.Push(static_cast<Result&&>(r));
        os::InputEvent e;
        e.request_id = id;
        e.clipboard_status = static_cast<u8>(status);
        e.type = os::InputEventType::ClipboardData;
        for (usize i = 0; i < requests.Size(); ++i) {
            if (requests[i].id == id && requests[i].drop) e.type = os::InputEventType::DropData;
        }
        events.Push(e);
    }

    Vector<os::InputEvent> events;
    Vector<Offered> offered;
    Vector<Offered> offered_drag;
    Vector<Request> requests;
    u32 width = 0;
    u32 height = 0;
    u32 last_id = 0;
    u32 drags_begun = 0;
    u32 finished_drops = 0;
    u8 accept_formats = 0;
    u8 drag_allowed = 0;
    bool primary_supported = false;
    bool pointer_lock_supported = true;
    bool pointer_locked = false;
    u32 pointer_lock_calls = 0;
    bool clipboard_has_text = false;
    bool refuse_requests = false;
    bool accept_drag = true;
    os::DragAction last_drop_action = os::DragAction::Nothing;
    os::DragAction finished_with = os::DragAction::Nothing;

private:
    struct Result {
        u32 id = 0;
        os::ClipboardStatus status = os::ClipboardStatus::Ok;
        Vector<byte> bytes;
    };
    Vector<Result> results;
    usize next_ = 0;
};

} // namespace engine_placeholder
