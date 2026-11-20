#include "platform/foreign_window.h"

#include "lang/utility.h"
#include "os/os.h"

namespace vse::os
{

ForeignWindow::ForeignWindow(const NativeWindowInfo& info, u32 width, u32 height, f32 scale)
    : info_(info), width_(width), height_(height), scale_(scale), serial_(detail::NextResizeSerial())
{
    requests_.Init(&events_);
}

void ForeignWindow::SetSize(u32 width, u32 height, f32 scale)
{
    if (width != width_ || height != height_)
    {
        serial_ = detail::NextResizeSerial();
    }
    width_ = width;
    height_ = height;
    scale_ = scale;
}

void ForeignWindow::Rebind(const NativeWindowInfo& info, u32 width, u32 height, f32 scale)
{
    info_ = info;
    width_ = width;
    height_ = height;
    scale_ = scale;
    serial_ = detail::NextResizeSerial();
    valid_ = true;
    locked_ = false;  // the lock belonged to the old window
}

void ForeignWindow::SetTextInputRect(i32 x, i32 y, i32 width, i32 height)
{
    if (hooks_ != nullptr)
    {
        hooks_->SetTextInputRect(x, y, width, height);
    }
}

void ForeignWindow::RequestFocus(bool focused)
{
    if (hooks_ != nullptr)
    {
        hooks_->RequestFocus(focused);
    }
}

bool ForeignWindow::SetPointerLock(bool locked)
{
    if (hooks_ == nullptr || !hooks_->SetPointerLock(locked))
    {
        return false;
    }
    locked_ = locked;
    return true;
}

bool ForeignWindow::SupportsClipboard(ClipboardKind kind) const
{
    return kind == ClipboardKind::Clipboard && hooks_ != nullptr && hooks_->SupportsClipboard();
}

bool ForeignWindow::SetClipboard(ClipboardKind kind, const ClipboardOffer& offer)
{
    if (!SupportsClipboard(kind) || offer.format != ClipboardFormat::Text)
    {
        return false;
    }
    return hooks_->SetClipboardText(StringView(reinterpret_cast<const char*>(offer.data), offer.size));
}

u32 ForeignWindow::RequestClipboard(ClipboardKind kind, ClipboardFormat format, StringView custom_name)
{
    (void)custom_name;
    if (!SupportsClipboard(kind) || format != ClipboardFormat::Text)
    {
        return 0;
    }
    const u64 now = MonotonicTimeNanos();
    const u32 id = requests_.Begin(kind, format, now);
    if (id == 0)
    {
        return 0;
    }
    // The host's clipboard answers at once: the request completes now and its event is queued for the next poll.
    Vector<byte> bytes;
    if (!hooks_->GetClipboardText(&bytes))
    {
        requests_.Fail(id, ClipboardStatus::Missing, now);
        return id;
    }
    if (bytes.Size() > kMaxClipboardBytes)
    {
        requests_.Fail(id, ClipboardStatus::TooLarge, now);
        return id;
    }
    Vector<byte> clean;
    u32 replaced = 0;
    SanitizeUtf8(bytes.Data(), bytes.Size(), clean, &replaced);
    requests_.Complete(id, ClipboardStatus::Ok, Move(clean), replaced, 0, now);
    return id;
}

bool ForeignWindow::ClipboardHasFormat(ClipboardKind kind, ClipboardFormat format, StringView custom_name) const
{
    (void)custom_name;
    if (!SupportsClipboard(kind) || format != ClipboardFormat::Text)
    {
        return false;
    }
    Vector<byte> probe;
    return hooks_->GetClipboardText(&probe) && probe.Size() != 0;
}

}  // namespace vse::os
