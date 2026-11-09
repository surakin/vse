#include "ui_window/os_clipboard.h"

namespace engine_placeholder::ui {

void OsClipboard::Offer(os::ClipboardKind kind, StringView text) {
    if (text.Length() == 0) return;
    os::ClipboardOffer offer;
    offer.format = os::ClipboardFormat::Text;
    offer.data = reinterpret_cast<const byte*>(text.Data());
    offer.size = text.Length();
    window_->SetClipboard(kind, offer);
}

void OsClipboard::SetText(StringView text) { Offer(os::ClipboardKind::Clipboard, text); }

bool OsClipboard::HasText() { return window_->ClipboardHasFormat(os::ClipboardKind::Clipboard, os::ClipboardFormat::Text, StringView()); }

Clipboard::Ticket OsClipboard::Request(os::ClipboardKind kind, PasteFunction function, void* user) {
    if (function == nullptr) return 0;
    const u32 id = pending_.Size() < kMaxPending ? window_->RequestClipboard(kind, os::ClipboardFormat::Text, StringView()) : 0;
    if (id == 0) {
        on_failed_.Invoke(this, os::ClipboardStatus::Failed);
        return 0;
    }
    Pending p;
    p.id = id;
    p.function = function;
    p.user = user;
    pending_.Push(p);
    return id;
}

Clipboard::Ticket OsClipboard::RequestPaste(PasteFunction function, void* user) { return Request(os::ClipboardKind::Clipboard, function, user); }

void OsClipboard::CancelPaste(Ticket ticket) {
    for (usize i = 0; i < pending_.Size(); ++i) {
        if (pending_[i].id == ticket) {
            pending_.Remove(i); // the result is still taken when it arrives, and dropped
            return;
        }
    }
}

bool OsClipboard::SupportsPrimary() const { return window_->SupportsClipboard(os::ClipboardKind::Primary); }

void OsClipboard::SetPrimaryText(StringView text) {
    if (SupportsPrimary()) Offer(os::ClipboardKind::Primary, text);
}

Clipboard::Ticket OsClipboard::RequestPrimaryPaste(PasteFunction function, void* user) {
    return SupportsPrimary() ? Request(os::ClipboardKind::Primary, function, user) : 0;
}

bool OsClipboard::HandleData(u32 request_id) {
    os::ClipboardResult result;
    const bool have = window_->TakeClipboardResult(request_id, &result);
    Pending p;
    bool mine = false;
    for (usize i = 0; i < pending_.Size(); ++i) {
        if (pending_[i].id == request_id) {
            p = pending_[i];
            pending_.Remove(i);
            mine = true;
            break;
        }
    }
    if (!mine) return false;
    if (!have || result.status != os::ClipboardStatus::Ok) {
        on_failed_.Invoke(this, have ? result.status : os::ClipboardStatus::Failed);
        return true;
    }
    p.function(p.user, StringView(reinterpret_cast<const char*>(result.bytes.Data()), result.bytes.Size()));
    return true;
}

} // namespace engine_placeholder::ui
