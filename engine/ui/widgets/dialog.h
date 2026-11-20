#pragma once

#include "ui/callback.h"
#include "ui/widgets/buttons.h"
#include "ui/widgets/window_frame.h"

//! The dialog (Phase 15, Day 199): a window frame with a row of buttons, a default button and a result, shown modally
//! by `Surface::ShowModal`.

namespace vse::ui
{

/// The results the common dialogs return; a program may use any other integers for its own buttons.
namespace dialog_result
{
constexpr int kNone = 0;    ///< Not closed yet.
constexpr int kOk = 1;      ///< OK.
constexpr int kCancel = 2;  ///< Cancel, Escape and the close button.
constexpr int kYes = 3;     ///< Yes.
constexpr int kNo = 4;      ///< No.
}  // namespace dialog_result

/// A [[ui::WindowFrame]] with a button row at the bottom.
///
/// `AddButton` adds a button that closes the dialog with a result. Enter (when the focus owner does not take it)
/// presses the default button; Escape and the close button close the dialog with the cancel result
/// (`dialog_result::kCancel` unless `SetCancelResult` says otherwise). `Close(result)` runs `OnClosed` and removes the
/// dialog: from the surface's modal stack when it is modal (the focus goes back to its previous owner), else from its
/// parent. Removal is deferred while an event is delivered, so a button handler may close its own dialog; do not use
/// the dialog after `OnClosed` returns, the surface destroys it.
///
/// - Thread-safety: owner-thread-only.
/// - Ownership: owns its buttons and its content.
class Dialog : public WindowFrame
{
public:
    /// The gap between the buttons, in logical units.
    static constexpr f32 kButtonGap = 8.0f;

    /// Makes a dialog with no buttons.
    /// - Param name: the widget name.
    /// - Param title: the title text.
    explicit Dialog(StringView name = StringView(), StringView title = StringView());
    /// Adds a button at the right end of the row.
    /// - Param text: the button's text.
    /// - Param result: what `Close` gets when it is pressed.
    /// - Param is_default: whether Enter presses it (a later call with true takes the flag from the earlier button).
    /// - Returns: the button, for the program to name or disable.
    /// - Ownership: borrowed; the dialog owns it.
    Button* AddButton(StringView text, int result, bool is_default = false);
    /// Returns the default button, or null.
    ///
    /// - Ownership: borrowed; the dialog owns it.
    /// - Fails: never.
    Button* DefaultButton() const { return default_; }
    /// Sets the result of Escape and the close button.
    void SetCancelResult(int result) { cancel_result_ = result; }
    /// Returns the result the dialog closed with (`dialog_result::kNone` before).
    ///
    /// - Fails: never.
    int Result() const { return result_; }
    /// Closes the dialog with `result`: stores it, runs `OnClosed`, removes the dialog. Does nothing the second time.
    void Close(int result);
    /// Returns the listeners called when the dialog closes (with the dialog and the result).
    Callback<Dialog*, int>& OnClosed() { return on_closed_; }
    /// Closes the dialog with the cancel result.
    void CloseRequested() override;
    /// Presses the default button on Enter and cancels on Escape.
    void OnEvent(Event& event) override;

private:
    struct ButtonResult
    {
        Button* button = nullptr;
        int result = 0;
    };
    static void ButtonClicked(void* user, Button* button);

    Widget* row_ = nullptr;
    Vector<ButtonResult> buttons_;
    Button* default_ = nullptr;
    int cancel_result_ = dialog_result::kCancel;
    int result_ = dialog_result::kNone;
    bool closed_ = false;
    Callback<Dialog*, int> on_closed_;
};

}  // namespace vse::ui
