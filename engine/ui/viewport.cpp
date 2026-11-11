#include "ui/viewport.h"

#include "lang/scalar.h"

#include "log/log.h"
#include "ui/surface.h"

namespace engine_placeholder::ui
{

namespace
{

/// Rounds a size in logical units times the scale to whole physical pixels, at least 0.
u32 ToPixels(f32 logical, f32 scale)
{
    const f32 pixels = Round(logical * scale);
    return pixels > 0 ? static_cast<u32>(pixels) : 0u;
}

}  // namespace

Viewport::Viewport(StringView name) : Widget(name), look_button_(os::kMouseRight)
{
    SetFocusable(true);
    SetTabIndex(-1);  // a click focuses it; Tab walks past it
}

void Viewport::SetBinding(ViewportBinding* binding)
{
    if (binding == binding_)
    {
        return;
    }
    if (looking_)
    {
        EndLook();
    }
    binding_ = binding;
    pixel_w_ = 0;
    pixel_h_ = 0;  // the next frame tells the new binding the size
    MarkDirty(kDrawDirty);
}

void Viewport::SetLookButton(u32 button)
{
    if (button == look_button_)
    {
        return;
    }
    if (looking_)
    {
        EndLook();
    }
    look_button_ = button;
}

Size Viewport::Measure(const Constraints&)
{
    return Size();
}

void Viewport::Update(f32 dt)
{
    frame_delta_ = dt;
    if (looking_ && (GetSurface() == nullptr || GetSurface()->Capture() != this ||
                     (look_locked_ && GetSurface()->PointerLockOwner() != this)))
    {
        EndLook();  // the surface took the pointer back (the window lost the focus, a dialog opened) or freed the lock
                    // (the viewport was hidden)
    }
    MarkDirty(kDrawDirty);
}

void Viewport::Draw(Canvas& canvas)
{
    if (binding_ == nullptr || GetSurface() == nullptr)
    {
        return;
    }
    const Rect& b = Bounds();
    const f32 scale = GetSurface()->Scale();
    const u32 w = ToPixels(b.w, scale);
    const u32 h = ToPixels(b.h, scale);
    if (w == 0 || h == 0)
    {
        return;  // a collapsed pane: nothing is called, the binding keeps its target
    }
    if (w != pixel_w_ || h != pixel_h_)
    {
        pixel_w_ = w;
        pixel_h_ = h;
        ++resize_count_;
        binding_->OnResize(w, h);
    }
    ViewportFrame frame;
    frame.width = w;
    frame.height = h;
    frame.scale = scale;
    frame.delta = frame_delta_;
    frame.time = GetSurface()->Time();
    ++render_count_;
    binding_->Render(frame);
    const ImageId image = binding_->Image();
    if (image == 0)
    {
        return;
    }
    Rect uv;
    uv.w = 1.0f;
    uv.h = 1.0f;
    canvas.DrawImage(b, image, uv, 0xFFFFFFFFu);
}

ViewportInput Viewport::MakeInput(ViewportInputType type, const Event& event) const
{
    ViewportInput in;
    in.type = type;
    in.modifiers = event.modifiers;
    in.button = event.button;
    in.scale = GetSurface() != nullptr ? GetSurface()->Scale() : 1.0f;
    in.x = event.position.x * in.scale;
    in.y = event.position.y * in.scale;
    return in;
}

void Viewport::Send(ViewportInput& input, Event* event)
{
    binding_->OnInput(input);
    if (event != nullptr && (input.handled || input.type == ViewportInputType::PointerMove ||
                             input.type == ViewportInputType::PointerDown ||
                             input.type == ViewportInputType::PointerUp || input.type == ViewportInputType::Look))
    {
        event->handled = true;
    }
}

void Viewport::BeginLook(const Event& event)
{
    Surface* surface = GetSurface();
    looking_ = true;
    look_last_ = event.position;
    look_locked_ = surface != nullptr && surface->RequestPointerLock(this);
    if (!look_locked_ && !warned_no_lock_)
    {
        warned_no_lock_ = true;
        Log(LogCore, LogVerbosity::Warn,
            StringView("Viewport: the pointer cannot be locked here; mouse look uses captured absolute motion"));
    }
    ViewportInput in = MakeInput(ViewportInputType::LookBegin, event);
    in.locked = look_locked_;
    binding_->OnInput(in);
}

void Viewport::EndLook()
{
    if (!looking_)
    {
        return;
    }
    looking_ = false;
    Surface* surface = GetSurface();
    if (look_locked_ && surface != nullptr)
    {
        surface->ReleasePointerLock(this);
    }
    look_locked_ = false;
    if (binding_ != nullptr)
    {
        ViewportInput in;
        in.type = ViewportInputType::LookEnd;
        in.scale = surface != nullptr ? surface->Scale() : 1.0f;
        binding_->OnInput(in);
    }
}

void Viewport::OnEvent(Event& event)
{
    if (binding_ == nullptr)
    {
        return;
    }
    switch (event.type)
    {
    case EventType::PointerEnter:
    {
        ViewportInput in = MakeInput(ViewportInputType::Enter, event);
        Send(in, nullptr);
        break;
    }
    case EventType::PointerLeave:
    {
        ViewportInput in = MakeInput(ViewportInputType::Leave, event);
        Send(in, nullptr);
        break;
    }
    case EventType::PointerMove:
    {
        if (looking_ && !look_locked_)
        {
            ViewportInput in = MakeInput(ViewportInputType::Look, event);
            const f32 scale = in.scale;
            in.delta_x = (event.position.x - look_last_.x) * scale;
            in.delta_y = (event.position.y - look_last_.y) * scale;
            look_last_ = event.position;
            Send(in, &event);
        }
        else if (!looking_)
        {
            ViewportInput in = MakeInput(ViewportInputType::PointerMove, event);
            Send(in, &event);
        }
        break;
    }
    case EventType::RelativeMotion:
    {
        if (!looking_ || !look_locked_)
        {
            break;
        }
        ViewportInput in;
        in.type = ViewportInputType::Look;
        in.locked = true;
        in.modifiers = event.modifiers;
        in.delta_x = event.rel_x;
        in.delta_y = event.rel_y;
        in.scale = GetSurface() != nullptr ? GetSurface()->Scale() : 1.0f;
        Send(in, &event);
        break;
    }
    case EventType::PointerDown:
    {
        ViewportInput in = MakeInput(ViewportInputType::PointerDown, event);
        Send(in, &event);
        if (look_button_ != 0 && event.button == look_button_ && !looking_)
        {
            BeginLook(event);
        }
        break;
    }
    case EventType::PointerUp:
    {
        if (looking_ && event.button == look_button_)
        {
            EndLook();
        }
        ViewportInput in = MakeInput(ViewportInputType::PointerUp, event);
        Send(in, &event);
        break;
    }
    case EventType::Wheel:
    {
        ViewportInput in = MakeInput(ViewportInputType::Wheel, event);
        in.wheel_x = event.wheel_x;
        in.wheel_y = event.wheel_y;
        Send(in, &event);
        break;
    }
    case EventType::KeyDown:
    case EventType::KeyUp:
    {
        ViewportInput in = MakeInput(
            event.type == EventType::KeyDown ? ViewportInputType::KeyDown : ViewportInputType::KeyUp, event);
        in.key = event.key;
        in.repeat = event.repeat;
        Send(in, &event);
        break;
    }
    case EventType::FocusGained:
    {
        ViewportInput in = MakeInput(ViewportInputType::FocusGained, event);
        Send(in, nullptr);
        break;
    }
    case EventType::FocusLost:
    {
        if (looking_)
        {
            EndLook();
        }
        ViewportInput in = MakeInput(ViewportInputType::FocusLost, event);
        Send(in, nullptr);
        break;
    }
    default: break;
    }
}

}  // namespace engine_placeholder::ui
