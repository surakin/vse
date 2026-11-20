#include "ui_window/window_adapter.h"

#include "platform/input_event.h"

namespace vse::ui
{

usize WindowAdapter::PumpInto(os::Window& window, Surface& surface)
{
    usize posted = 0;
    if (follow_window_scale_)
    {
        const f32 wanted = window.ContentScale();
        if (wanted > 0.0f && wanted != surface.Scale() && window.Width() > 0 && window.Height() > 0)
        {
            surface.Resize(static_cast<f32>(window.Width()) / wanted, static_cast<f32>(window.Height()) / wanted,
                           wanted);
        }
    }
    const f32 scale = surface.Scale();
    const f64 inverse = 1.0 / static_cast<f64>(scale);
    os::InputEvent in;
    while (window.PollEvent(&in))
    {
        Event e;
        e.modifiers = in.modifiers;
        switch (in.type)
        {
        case os::InputEventType::KeyDown:
        case os::InputEventType::KeyUp:
            e.type = in.type == os::InputEventType::KeyDown ? EventType::KeyDown : EventType::KeyUp;
            e.key = in.key;
            e.repeat = in.repeat;
            break;
        case os::InputEventType::Text:
            e.type = EventType::Text;
            e.text_length = in.text_length;
            for (u8 i = 0; i < 4; ++i)
            {
                e.text[i] = in.text[i];
            }
            break;
        case os::InputEventType::PointerMove:
        case os::InputEventType::PointerEnter:
            e.type = EventType::PointerMove;
            e.position = Point(static_cast<f32>(in.x * inverse), static_cast<f32>(in.y * inverse));
            pointer_ = e.position;
            if (in.type == os::InputEventType::PointerMove && surface.Dragging() && window.Width() > 0 &&
                (in.x < 0 || in.y < 0 || in.x >= static_cast<f64>(window.Width()) ||
                 in.y >= static_cast<f64>(window.Height())))
            {
                TryHandOff(
                    window,
                    surface);  // the pointer is outside the window while a drag is held: the drag goes on in the OS
            }
            break;
        case os::InputEventType::PointerButton:
            e.type = in.down ? EventType::PointerDown : EventType::PointerUp;
            e.button = in.button;
            e.position = Point(static_cast<f32>(in.x * inverse), static_cast<f32>(in.y * inverse));
            pointer_ = e.position;
            break;
        case os::InputEventType::Wheel:
            e.type = EventType::Wheel;
            e.position = pointer_;  // a window wheel event carries no position: it turns where the pointer last was
            e.wheel_x = static_cast<f32>(in.x);
            e.wheel_y = static_cast<f32>(in.y);
            break;
        case os::InputEventType::RelativeMotion:
            e.type = EventType::RelativeMotion;
            e.rel_x = static_cast<f32>(in.x);
            e.rel_y = static_cast<f32>(in.y);
            break;
        case os::InputEventType::PointerLeave:
            e.type = EventType::PointerLeave;
            TryHandOff(window, surface);
            break;
        case os::InputEventType::ClipboardData:
            if (clipboard_ != nullptr)
            {
                clipboard_->HandleData(in.request_id);
            }
            continue;
        case os::InputEventType::ClipboardChanged: ++clipboard_changes_; continue;
        case os::InputEventType::DragEnter:
        {
            DragPayload payload;
            if ((in.drag_formats & os::kDropText) != 0)
            {
                payload.AddPending(DragEntryKind::Text);
            }
            if ((in.drag_formats & os::kDropFiles) != 0)
            {
                payload.AddPending(DragEntryKind::FileList);
            }
            if ((in.drag_formats & os::kDropCustom) != 0 && !custom_name_.Empty())
            {
                payload.AddPending(DragEntryKind::Custom, StringView(custom_name_.Data(), custom_name_.Size()));
            }
            surface.ExternalDragEnter(static_cast<DragPayload&&>(payload),
                                      Point(static_cast<f32>(in.x * inverse), static_cast<f32>(in.y * inverse)),
                                      in.drag_actions);
            window.SetDropAction(surface.DragIsExternal() ? surface.CurrentDragAction() : os::DragAction::Nothing);
            continue;
        }
        case os::InputEventType::DragOver:
            surface.ExternalDragOver(Point(static_cast<f32>(in.x * inverse), static_cast<f32>(in.y * inverse)),
                                     in.drag_actions);
            window.SetDropAction(surface.DragIsExternal() ? surface.CurrentDragAction() : os::DragAction::Nothing);
            continue;
        case os::InputEventType::DragLeave:
            dropping_ = false;
            surface.ExternalDragLeave();
            continue;
        case os::InputEventType::Drop:
            StartDrop(window, surface, in, Point(static_cast<f32>(in.x * inverse), static_cast<f32>(in.y * inverse)));
            continue;
        case os::InputEventType::DropData: FinishDropData(window, surface, in.request_id); continue;
        case os::InputEventType::DragEnded:
            surface.EndHandedOffDrag(static_cast<os::DragAction>(in.drag_action));
            continue;
        case os::InputEventType::FocusGained: e.type = EventType::WindowFocusGained; break;
        case os::InputEventType::FocusLost: e.type = EventType::WindowFocusLost; break;
        case os::InputEventType::Resize:
            if (in.width > 0 && in.height > 0)
            {
                surface.Resize(static_cast<f32>(in.width * inverse), static_cast<f32>(in.height * inverse), scale);
            }
            continue;
        default: continue;
        }
        surface.Post(e);
        ++posted;
    }
    return posted;
}

void WindowAdapter::EnableDrops(os::Window& window, StringView custom_name)
{
    custom_name_.Clear();
    for (usize i = 0; i < custom_name.Length(); ++i)
    {
        custom_name_.Push(custom_name[i]);
    }
    os::DropAccept accept;
    accept.formats = static_cast<u8>(os::kDropText | os::kDropFiles | (custom_name_.Empty() ? 0 : os::kDropCustom));
    accept.custom_name = custom_name;
    window.SetDropAccept(accept);
}

void WindowAdapter::TryHandOff(os::Window& window, Surface& surface)
{
    if (!surface.Dragging() || surface.DragIsExternal() || surface.DragHandedOff() || !window.SupportsDragAndDrop())
    {
        return;
    }
    const DragPayload* payload = surface.ActiveDrag();
    if (payload == nullptr || !payload->OsRepresentable())
    {
        return;  // a payload of pointers never leaves the process
    }
    os::ClipboardOffer offers[os::kMaxDragOffers];
    usize count = 0;
    bool have_text = false;
    bool have_files = false;
    bool have_custom = false;
    for (usize i = 0; i < payload->EntryCount() && count < os::kMaxDragOffers; ++i)
    {
        const DragEntry& entry = payload->Entry(i);
        os::ClipboardOffer& offer = offers[count];
        if (entry.kind == DragEntryKind::Text && !have_text)
        {
            offer.format = os::ClipboardFormat::Text;
            have_text = true;
        }
        else if (entry.kind == DragEntryKind::FileList && !have_files)
        {
            offer.format = os::ClipboardFormat::FileList;
            have_files = true;
        }
        else if (entry.kind == DragEntryKind::Custom && !have_custom)
        {
            offer.format = os::ClipboardFormat::Custom;
            offer.custom_name = entry.Name();
            have_custom = true;
        }
        else
        {
            continue;
        }
        offer.data = entry.bytes.Data();
        offer.size = entry.bytes.Size();
        ++count;
    }
    if (count == 0)
    {
        return;
    }
    os::DragSource source;
    source.offers = offers;
    source.offer_count = count;
    source.allowed_actions = payload->Allowed();
    // A modal drag (Win32) runs inside BeginDrag, with the program drawing from the modal-loop callback: the surface
    // must stop running the drag as its own when the OS drag starts, not when it is over. HandOffDrag a second time
    // does nothing.
    source.started = [](void* user) { static_cast<Surface*>(user)->HandOffDrag(); };
    source.started_user = &surface;
    if (window.BeginDrag(source))
    {
        surface.HandOffDrag();
    }
}

void WindowAdapter::FailDrop(os::Window& window, Surface& surface)
{
    dropping_ = false;
    drop_requests_.Clear();
    drop_payload_ = DragPayload();
    surface.ExternalDropFailed(StringView("The dropped data could not be read."));
    window.FinishDrop(os::DragAction::Nothing);
}

void WindowAdapter::StartDrop(os::Window& window, Surface& surface, const os::InputEvent& in, Point position)
{
    if (!surface.DragIsExternal())
    {
        window.FinishDrop(os::DragAction::Nothing);
        return;
    }
    dropping_ = true;
    drop_position_ = position;
    drop_action_ = static_cast<os::DragAction>(in.drag_action);
    drop_requests_.Clear();
    drop_payload_ = DragPayload();
    const os::ClipboardFormat formats[3] = {os::ClipboardFormat::Text, os::ClipboardFormat::FileList,
                                            os::ClipboardFormat::Custom};
    const u8 bits[3] = {os::kDropText, os::kDropFiles, os::kDropCustom};
    for (usize i = 0; i < 3; ++i)
    {
        if ((in.drag_formats & bits[i]) == 0)
        {
            continue;
        }
        if (formats[i] == os::ClipboardFormat::Custom && custom_name_.Empty())
        {
            continue;
        }
        DropRequest r;
        r.format = formats[i];
        r.id = window.RequestDropData(formats[i], formats[i] == os::ClipboardFormat::Custom
                                                      ? StringView(custom_name_.Data(), custom_name_.Size())
                                                      : StringView());
        if (r.id == 0)
        {
            FailDrop(window, surface);
            return;
        }
        drop_requests_.Push(r);
    }
    if (drop_requests_.Empty())
    {
        FailDrop(window, surface);
    }
}

void WindowAdapter::FinishDropData(os::Window& window, Surface& surface, u32 request_id)
{
    if (!dropping_)
    {
        os::ClipboardResult unused;
        window.TakeClipboardResult(request_id, &unused);
        return;
    }
    usize at = drop_requests_.Size();
    for (usize i = 0; i < drop_requests_.Size(); ++i)
    {
        if (drop_requests_[i].id == request_id)
        {
            at = i;
        }
    }
    if (at == drop_requests_.Size())
    {
        return;
    }
    os::ClipboardResult result;
    const bool have = window.TakeClipboardResult(request_id, &result);
    if (!have || result.status != os::ClipboardStatus::Ok)
    {
        FailDrop(window, surface);
        return;
    }
    const os::ClipboardFormat format = drop_requests_[at].format;
    drop_requests_.Remove(at);
    bool added = false;
    const StringView bytes(reinterpret_cast<const char*>(result.bytes.Data()), result.bytes.Size());
    if (format == os::ClipboardFormat::Text)
    {
        added = drop_payload_.AddText(bytes);
    }
    else if (format == os::ClipboardFormat::FileList)
    {
        added = drop_payload_.AddFiles(bytes);
    }
    else
    {
        added = drop_payload_.AddCustom(StringView(custom_name_.Data(), custom_name_.Size()), result.bytes.Data(),
                                        result.bytes.Size());
    }
    if (!added)
    {
        FailDrop(window, surface);
        return;
    }
    if (!drop_requests_.Empty())
    {
        return;
    }
    dropping_ = false;
    const os::DragAction taken = surface.ExternalDrop(static_cast<DragPayload&&>(drop_payload_), drop_position_,
                                                      drop_action_);
    drop_payload_ = DragPayload();
    window.FinishDrop(taken);
}

bool WindowAdapter::UpdateCursor(os::Window& window, const Surface& surface)
{
    const os::CursorShape wanted = surface.RequestedCursor();
    if (wanted == last_cursor_)
    {
        return false;
    }
    if (!window.SetCursor(wanted))
    {
        return true;
    }
    last_cursor_ = wanted;
    ++cursor_changes_;
    return true;
}

}  // namespace vse::ui
