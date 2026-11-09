#include "ui/widgets/message_box.h"

#include "log/assert.h"
#include "ui/surface.h"
#include "ui/widgets/label.h"

namespace engine_placeholder::ui {

namespace {

// A dialog that remembers the caller's function and user and calls them when it closes.
class MessageDialog final : public Dialog {
public:
    MessageDialog(StringView title, StringView text, MessageBoxFunction function, void* user) : Dialog(StringView("messagebox"), title), function_(function), user_(user) {
        UniquePtr<Label> label = MakeUnique<Label>(StringView("message"), text);
        label->SetWrap(true);
        SetContent(UniquePtr<Widget>(label.Release()));
        OnClosed().Connect(&MessageDialog::Done, this);
    }

private:
    static void Done(void* self, Dialog*, int result) {
        MessageDialog* d = static_cast<MessageDialog*>(self);
        if (d->function_ != nullptr) d->function_(d->user_, result);
    }
    MessageBoxFunction function_;
    void* user_;
};

} // namespace

Dialog* ShowMessageBox(Surface* surface, StringView title, StringView text, MessageButtons buttons, MessageBoxFunction function, void* user) {
    ENGINE_ASSERT(surface != nullptr, "ShowMessageBox: the surface is null");
    UniquePtr<MessageDialog> dialog = MakeUnique<MessageDialog>(title, text, function, user);
    switch (buttons) {
        case MessageButtons::Ok:
            dialog->AddButton(StringView("OK"), dialog_result::kOk, true);
            dialog->SetCancelResult(dialog_result::kOk);
            break;
        case MessageButtons::OkCancel:
            dialog->AddButton(StringView("OK"), dialog_result::kOk, true);
            dialog->AddButton(StringView("Cancel"), dialog_result::kCancel);
            break;
        case MessageButtons::YesNo:
            dialog->AddButton(StringView("Yes"), dialog_result::kYes, true);
            dialog->AddButton(StringView("No"), dialog_result::kNo);
            dialog->SetCancelResult(dialog_result::kNo);
            break;
        case MessageButtons::YesNoCancel:
            dialog->AddButton(StringView("Yes"), dialog_result::kYes, true);
            dialog->AddButton(StringView("No"), dialog_result::kNo);
            dialog->AddButton(StringView("Cancel"), dialog_result::kCancel);
            break;
    }
    Dialog* raw = dialog.Get();
    surface->ShowModal(UniquePtr<Widget>(dialog.Release()));
    return raw;
}

Dialog* ShowConfirm(Surface* surface, StringView title, StringView text, StringView confirm_text, MessageBoxFunction function, void* user) {
    ENGINE_ASSERT(surface != nullptr, "ShowConfirm: the surface is null");
    UniquePtr<MessageDialog> dialog = MakeUnique<MessageDialog>(title, text, function, user);
    dialog->AddButton(confirm_text, dialog_result::kOk, true);
    dialog->AddButton(StringView("Cancel"), dialog_result::kCancel);
    Dialog* raw = dialog.Get();
    surface->ShowModal(UniquePtr<Widget>(dialog.Release()));
    return raw;
}

} // namespace engine_placeholder::ui
