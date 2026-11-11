#pragma once

#include "ui/widgets/dialog.h"

//! Message boxes (Phase 15, Day 199): common dialogs built from the widgets, shown modally.

namespace engine_placeholder::ui
{

class Surface;

/// The buttons of a message box.
enum class MessageButtons : u8
{
    Ok,           ///< OK (`dialog_result::kOk`).
    OkCancel,     ///< OK and Cancel.
    YesNo,        ///< Yes and No.
    YesNoCancel,  ///< Yes, No and Cancel.
};

/// What a message box calls when it closes: the user pointer and the result (a `dialog_result` value).
using MessageBoxFunction = void (*)(void* user, int result);

/// Shows a message box: a title, wrapped text and the buttons, modal on `surface`.
///
/// The first button is the default one (Enter). Escape and the close button give `dialog_result::kCancel` for the sets
/// that have Cancel, `kNo` for Yes/No, `kOk` for OK alone.
///
/// - Param surface: the surface to show it on.
/// - Param title: the title text.
/// - Param text: the message; it wraps.
/// - Param buttons: which buttons to show.
/// - Param function: called with the result when the box closes; may be null.
/// - Param user: passed to `function`; borrowed.
/// - Returns: the dialog, valid until it closes.
/// - Ownership: the surface owns the dialog; `user` is borrowed until `function` runs.
/// - Aborts: `surface` is null.
Dialog* ShowMessageBox(Surface* surface, StringView title, StringView text, MessageButtons buttons,
                       MessageBoxFunction function, void* user);

/// Shows a confirmation: the text, a default button called `confirm_text` (result `dialog_result::kOk`) and Cancel
/// (`kCancel`).
///
/// - Param surface: the surface to show it on.
/// - Param title: the title text.
/// - Param text: the question; it wraps.
/// - Param confirm_text: the confirm button's text ("Restart", "Delete").
/// - Param function: called with the result when the box closes; may be null.
/// - Param user: passed to `function`; borrowed.
/// - Returns: the dialog, valid until it closes.
/// - Ownership: the surface owns the dialog; `user` is borrowed until `function` runs.
/// - Aborts: `surface` is null.
Dialog* ShowConfirm(Surface* surface, StringView title, StringView text, StringView confirm_text,
                    MessageBoxFunction function, void* user);

}  // namespace engine_placeholder::ui
