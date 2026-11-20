#include "ui/widgets/dialog.h"

#include "ui/layout.h"
#include "ui/surface.h"

namespace vse::ui
{

Dialog::Dialog(StringView name, StringView title) : WindowFrame(name, title)
{
    SetClosable(true);
    UniquePtr<BoxLayout> row = MakeUnique<BoxLayout>(Orientation::Horizontal, StringView("buttons"));
    row->SetSpacing(kButtonGap);
    row->AddChild(MakeUnique<Spacer>());  // pushes the buttons to the right
    row_ = row.Get();
    SetFooter(UniquePtr<Widget>(row.Release()));
}

Button* Dialog::AddButton(StringView text, int result, bool is_default)
{
    UniquePtr<Button> button = MakeUnique<Button>(text, text);
    Button* raw = button.Get();
    LayoutProps props;
    props.align_x = Align::Start;
    raw->SetProps(props);
    raw->OnClick().Connect(&Dialog::ButtonClicked, this);
    row_->AddChild(UniquePtr<Widget>(button.Release()));
    ButtonResult entry;
    entry.button = raw;
    entry.result = result;
    buttons_.Push(entry);
    if (is_default)
    {
        if (default_ != nullptr)
        {
            default_->SetDefault(false);
            default_->SetVariant(StringView());
        }
        default_ = raw;
        raw->SetDefault(true);
        raw->SetVariant(StringView("primary"));
    }
    return raw;
}

void Dialog::ButtonClicked(void* user, Button* button)
{
    Dialog* dialog = static_cast<Dialog*>(user);
    for (usize i = 0; i < dialog->buttons_.Size(); ++i)
    {
        if (dialog->buttons_[i].button == button)
        {
            dialog->Close(dialog->buttons_[i].result);
            return;
        }
    }
}

void Dialog::Close(int result)
{
    if (closed_)
    {
        return;
    }
    closed_ = true;
    result_ = result;
    on_closed_.Invoke(this, result);
    Surface* surface = GetSurface();
    if (surface != nullptr && surface->IsModalOpen(this))
    {
        surface->CloseModal(this);
    }
    else
    {
        RemoveFromParent();
    }
}

void Dialog::CloseRequested()
{
    WindowFrame::CloseRequested();
    Close(cancel_result_);
}

void Dialog::OnEvent(Event& event)
{
    if (event.type == EventType::KeyDown && !event.repeat)
    {
        if (event.key == Key::Escape)
        {
            event.handled = true;
            Close(cancel_result_);
            return;
        }
        if (event.key == Key::Enter && default_ != nullptr &&
            (event.modifiers & (os::kModCtrl | os::kModAlt | os::kModSuper)) == 0)
        {
            event.handled = true;
            default_->Click();
            return;
        }
    }
    WindowFrame::OnEvent(event);
}

}  // namespace vse::ui
