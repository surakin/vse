#pragma once

#include "containers/vector.h"
#include "string_view.h"
#include "types.h"

//! The clipboard as the toolkit sees it (Phase 15, Day 197): set text, ask for text and be called back. The in-process
//! implementation serves offscreen surfaces and tests; the OS-backed one (`ui::OsClipboard` in `ui_window`:
//! asynchronous, with the primary selection) sits behind the same interface (Day 200), so a widget written against this
//! does not change.

namespace engine_placeholder::ui
{

/// Where copied text goes and pasted text comes from.
///
/// Pasting is asynchronous in shape: `RequestPaste` takes a function that is called with the text, either before
/// `RequestPaste` returns (the in-process clipboard) or later, from the surface's thread (an OS clipboard that has to
/// ask another program). A caller therefore must not assume either, and cancels a request it no longer wants (a widget
/// being destroyed).
///
/// - Thread-safety: owner-thread-only.
/// - Ownership: owns the text it holds; the callback's user pointer is borrowed until the call or `CancelPaste`.
class Clipboard
{
public:
    /// What `RequestPaste` calls with the text (UTF-8, valid for the call only).
    using PasteFunction = void (*)(void* user, StringView text);
    /// Names a pending request; 0 means none is pending.
    using Ticket = u32;

    /// Destroys the clipboard.
    virtual ~Clipboard() = default;

    /// Replaces the clipboard's text.
    /// - Param text: UTF-8 text; copied.
    /// - Fails: never.
    virtual void SetText(StringView text) = 0;
    /// Returns whether there is text to paste (an asynchronous clipboard may answer true and later find nothing).
    /// - Fails: never.
    virtual bool HasText() = 0;
    /// Asks for the clipboard's text. `function` is called once with it, unless the request is cancelled or the
    /// clipboard has no text (then it is not called).
    /// - Param function: what to call with the text.
    /// - Param user: passed to `function`; borrowed until it is called or the request is cancelled.
    /// - Returns: a ticket for `CancelPaste` when the call is still pending, 0 when `function` already ran or nothing
    /// is pending.
    /// - Ownership: `user` is borrowed until the function runs or the ticket is cancelled.
    /// - Fails: never.
    virtual Ticket RequestPaste(PasteFunction function, void* user) = 0;
    /// Withdraws a pending request; a ticket that is 0, done or unknown is ignored.
    /// - Param ticket: the value `RequestPaste` returned.
    /// - Fails: never.
    virtual void CancelPaste(Ticket ticket) = 0;

    /// Returns whether the primary selection exists here (select-to-copy and middle-click paste); the default is false.
    /// - Fails: never.
    virtual bool SupportsPrimary() const { return false; }
    /// Replaces the primary selection's text (what selecting text does); the default does nothing.
    /// - Param text: UTF-8 text; copied.
    /// - Fails: never.
    virtual void SetPrimaryText(StringView text) { (void)text; }
    /// Like `RequestPaste` for the primary selection (what a middle click does); the default calls nothing and returns
    /// 0.
    /// - Ownership: `user` is borrowed until the function runs or the ticket is cancelled.
    /// - Fails: never.
    virtual Ticket RequestPrimaryPaste(PasteFunction function, void* user)
    {
        (void)function;
        (void)user;
        return 0;
    }
};

/// A clipboard that lives in the process: `RequestPaste` calls back before it returns.
///
/// - Thread-safety: owner-thread-only.
/// - Ownership: owns its text.
class LocalClipboard final : public Clipboard
{
public:
    /// Makes an empty clipboard.
    LocalClipboard() = default;
    /// Replaces the text.
    void SetText(StringView text) override;
    /// Returns whether the text is not empty.
    ///
    /// - Fails: never.
    bool HasText() override;
    /// Calls `function` with the text now, if there is any; returns 0.
    ///
    /// - Ownership: `user` is borrowed for the call.
    /// - Fails: never.
    Ticket RequestPaste(PasteFunction function, void* user) override;
    /// Does nothing (nothing is ever pending).
    void CancelPaste(Ticket ticket) override;
    /// Turns the primary selection on or off (default off), so a test can run the primary-selection rules on this
    /// clipboard.
    void SetPrimarySupported(bool supported) { primary_supported_ = supported; }
    /// Returns whether the primary selection is on.
    /// - Fails: never.
    bool SupportsPrimary() const override { return primary_supported_; }
    /// Replaces the primary text when it is on.
    void SetPrimaryText(StringView text) override;
    /// Calls `function` with the primary text now, if it is on and not empty; returns 0.
    ///
    /// - Ownership: `user` is borrowed for the call.
    Ticket RequestPrimaryPaste(PasteFunction function, void* user) override;
    /// Returns the primary text.
    ///
    /// - Fails: never.
    StringView PrimaryText() const { return StringView(primary_.Data(), primary_.Size()); }
    /// Returns the text held.
    ///
    /// - Fails: never.
    StringView Text() const { return StringView(text_.Data(), text_.Size()); }

private:
    Vector<char> text_;
    Vector<char> primary_;
    bool primary_supported_ = false;
};

}  // namespace engine_placeholder::ui
