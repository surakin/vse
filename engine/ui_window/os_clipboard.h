#pragma once

#include "containers/vector.h"
#include "platform/clipboard.h"
#include "platform/window.h"
#include "types.h"
#include "ui/callback.h"
#include "ui/clipboard.h"

//! The OS clipboard behind the toolkit's [[ui::Clipboard]] (Phase 15, Day 200): asynchronous, with the primary
//! selection where the platform has one.

namespace vse::ui
{

/// A [[ui::Clipboard]] over an [[os::Window]]'s clipboard.
///
/// `SetText` offers the text to other programs. `RequestPaste` asks the owner of the selection and returns a ticket;
/// nothing is called until the window's `ClipboardData` event arrives and the
/// [[ui::WindowAdapter]] hands it to `HandleData`, which calls the function with the text. A request that ended without
/// text (a timeout, an empty selection, an oversized transfer) calls no paste function and runs the `OnFailed`
/// listeners with the status. `HasText` reads the window's record of the selection ([[os::Window::ClipboardHasFormat]],
/// kept by `ClipboardChanged`), so a Paste command can be enabled without asking. The primary selection is on when the
/// window supports it. Offscreen surfaces keep using [[ui::LocalClipboard]].
///
/// - Thread-safety: owner-thread-only.
/// - Ownership: borrows the window, which must outlive the clipboard; owns its pending requests.
class OsClipboard final : public Clipboard
{
public:
    /// The most requests waiting for an answer.
    static constexpr usize kMaxPending = 8;

    /// Makes a clipboard over `window`.
    /// - Ownership: borrows `window`.
    explicit OsClipboard(os::Window* window) : window_(window) {}
    /// Offers the text to other programs.
    void SetText(StringView text) override;
    /// Returns whether the selection offers text.
    ///
    /// - Fails: never.
    bool HasText() override;
    /// Asks for the text; the function is called later, from `HandleData`.
    /// - Returns: the ticket, or 0 when the request could not be made (then nothing is called and `OnFailed` ran with
    /// `Failed`).
    ///
    /// - Ownership: `user` is borrowed until the function runs or the ticket is cancelled.
    Ticket RequestPaste(PasteFunction function, void* user) override;
    /// Withdraws a request; a result that arrives for it is dropped.
    void CancelPaste(Ticket ticket) override;
    /// Returns whether the window has a primary selection.
    ///
    /// - Fails: never.
    bool SupportsPrimary() const override;
    /// Offers the text as the primary selection.
    void SetPrimaryText(StringView text) override;
    /// Like `RequestPaste` for the primary selection.
    /// - Ownership: borrows the pointer; the caller keeps it.
    Ticket RequestPrimaryPaste(PasteFunction function, void* user) override;
    /// Handles a window `ClipboardData` event: reads the result and calls the function of the request, if it is still
    /// wanted.
    /// - Param request_id: the event's `request_id`.
    /// - Returns: whether the id was one of this clipboard's.
    bool HandleData(u32 request_id);
    /// Returns how many requests are waiting.
    ///
    /// - Fails: never.
    usize PendingCount() const { return pending_.Size(); }
    /// Returns the listeners called when a request ended without text (with the clipboard and the status).
    Callback<OsClipboard*, os::ClipboardStatus>& OnFailed() { return on_failed_; }

private:
    struct Pending
    {
        u32 id = 0;
        PasteFunction function = nullptr;
        void* user = nullptr;
    };
    void Offer(os::ClipboardKind kind, StringView text);
    Ticket Request(os::ClipboardKind kind, PasteFunction function, void* user);

    os::Window* window_;
    Vector<Pending> pending_;
    Callback<OsClipboard*, os::ClipboardStatus> on_failed_;
};

}  // namespace vse::ui
