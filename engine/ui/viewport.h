#pragma once

#include "types.h"
#include "ui/canvas.h"
#include "ui/event.h"
#include "ui/geometry.h"
#include "ui/widget.h"

//! The viewport widget (Phase 15, Day 201): a rectangle that shows a rendered 3D world and hands the input a camera
//! needs to the application. No RHI type appears here: the application implements [[ui::ViewportBinding]] over its own
//! render target (the renderer's `ViewportTarget` is one).

namespace vse::ui
{

/// What a [[ui::ViewportInput]] reports.
enum class ViewportInputType : u8
{
    PointerMove,  ///< The pointer moved (also while captured by a drag); `position` is where.
    PointerDown,  ///< A button went down; `button` says which.
    PointerUp,    ///< A button went up.
    Wheel,        ///< The wheel turned by (`wheel_x`, `wheel_y`) notches.
    Enter,        ///< The pointer entered the viewport.
    Leave,        ///< The pointer left the viewport.
    Look,         ///< Mouse look: the pointer moved by (`delta_x`, `delta_y`) while the look button is held, locked or
                  ///< captured.
    LookBegin,    ///< The look button went down: mouse look starts; `locked` says whether the pointer is locked.
    LookEnd,      ///< Mouse look ended (button up, focus lost or the viewport hidden).
    KeyDown,      ///< A key went down while the viewport has the keyboard focus.
    KeyUp,        ///< A key went up.
    FocusGained,  ///< The viewport got the keyboard focus.
    FocusLost,    ///< The viewport lost the keyboard focus.
};

/// One input event for the application's camera.
///
/// Positions are in the viewport's own pixel space (physical pixels, the top-left corner is the origin), not in logical
/// units: multiply a logical distance by `scale` to compare.
///
/// - Thread-safety: thread-compatible plain data.
struct ViewportInput
{
    ViewportInputType type = ViewportInputType::PointerMove;  ///< What happened.
    bool handled = false;  ///< The binding sets it to stop the event from bubbling to the viewport's ancestors (keys
                           ///< and the wheel only; pointer events are always taken).
    bool locked = false;   ///< `LookBegin`, `Look`: the pointer is locked (deltas are the window's relative motion) and
                           ///< not merely captured.
    bool repeat = false;   ///< `KeyDown`: an auto-repeat.
    Key key = Key::Unknown;  ///< `KeyDown`, `KeyUp`: the key.
    u32 button = 0;          ///< `PointerDown`, `PointerUp`, `LookBegin`: an `os::MouseButtonBit` value.
    u32 modifiers = 0;       ///< Mask of `os::ModifierBit` values.
    f32 x = 0;               ///< Pointer events: x in viewport pixels.
    f32 y = 0;               ///< Pointer events: y in viewport pixels.
    f32 delta_x = 0;         ///< `Look`: horizontal movement in pixels, positive to the right.
    f32 delta_y = 0;         ///< `Look`: vertical movement in pixels, positive downwards.
    f32 wheel_x = 0;         ///< `Wheel`: horizontal notches.
    f32 wheel_y = 0;         ///< `Wheel`: vertical notches, positive downwards.
    f32 scale = 1.0f;        ///< Physical pixels per logical unit of the surface.
};

/// What the viewport tells the binding about a frame.
///
/// - Thread-safety: thread-compatible plain data.
struct ViewportFrame
{
    u32 width = 0;     ///< The viewport's width in physical pixels.
    u32 height = 0;    ///< The viewport's height in physical pixels.
    f32 scale = 1.0f;  ///< Physical pixels per logical unit.
    f32 delta = 0;     ///< Seconds since the previous frame of the surface.
    f64 time = 0;      ///< The surface clock in seconds.
};

/// Returns the aspect ratio (width over height) of a viewport size, for a camera.
///
/// - Param width: width in pixels.
/// - Param height: height in pixels.
/// - Returns: `width / height`, or 1 when either is 0 (a collapsed viewport has no shape; its camera is left alone).
/// - Fails: never.
inline f32 AspectOf(u32 width, u32 height)
{
    return (width == 0 || height == 0) ? 1.0f : static_cast<f32>(width) / static_cast<f32>(height);
}

/// The application's side of a viewport: it owns the render target and the camera.
///
/// - Thread-safety: owner-thread-only.
/// - Ownership: owns nothing by itself; the viewport borrows the binding.
class ViewportBinding
{
public:
    virtual ~ViewportBinding() = default;
    /// The viewport's size in physical pixels changed (it is also called once for the first size). Called at most once
    /// per frame, after layout, with the final size, never with a zero size; the application resizes its target and
    /// sets its camera's aspect (see [[ui::AspectOf]]).
    virtual void OnResize(u32 width, u32 height) = 0;
    /// Produces the picture for this frame: typically it records that the world is to be drawn into the target. Called
    /// at most once per frame while the viewport is shown and not empty.
    virtual void Render(const ViewportFrame& frame) = 0;
    /// Returns the image the viewport draws in its rectangle, or 0 for none yet (nothing is drawn).
    ///
    /// - Fails: never.
    virtual ImageId Image() const = 0;
    /// Receives input; set `input.handled` to take a key or wheel event.
    virtual void OnInput(ViewportInput& input) = 0;
};

/// A widget that shows a rendered world.
///
/// Per frame, after layout, `Draw` works out the size in physical pixels (`bounds * scale`, rounded), calls
/// `ViewportBinding::OnResize` when it differs from the last size given (so a window resized ten times between two
/// frames costs one call), then `Render`, then draws the binding's image over the bounds. A zero-size viewport (a
/// collapsed pane) calls nothing and keeps the binding's target; a hidden or collapsed one is not drawn, so it renders
/// nothing and changes nothing. The viewport asks for `Update` and marks itself dirty each frame, so it redraws every
/// frame while shown. Children of the viewport (a HUD label, a gizmo handle) draw after the image inside the viewport's
/// clip and get the pointer before the viewport does, because hit testing finds the deepest widget.
///
/// About input, it is focusable, takes the keyboard focus on a click and holds the capture while a button is down.
/// Pointer events go to the binding in viewport pixels. Mouse look: when `SetLookButton` names a button, pressing it
/// starts look: the surface is asked to lock the pointer (`Surface::RequestPointerLock`) and relative motion reaches
/// the binding as `Look` deltas; where the pointer cannot be locked the viewport falls back to the captured absolute
/// motion, as differences between positions, and logs a warning once. Releasing the button, losing the focus, being
/// hidden or removed ends the look.
///
/// - Thread-safety: owner-thread-only.
/// - Ownership: borrows the binding, which must outlive the viewport or be cleared with `SetBinding(nullptr)`.
class Viewport : public Widget
{
public:
    /// Makes a viewport with no binding; mouse look is on the right button.
    explicit Viewport(StringView name = StringView());
    /// Sets the binding (null for none: the viewport draws nothing and ignores input). The next frame calls `OnResize`
    /// again with the current size.
    /// - Ownership: borrowed.
    void SetBinding(ViewportBinding* binding);
    /// Returns the binding.
    ///
    /// - Ownership: borrowed.
    /// - Fails: returns null when there is none.
    ViewportBinding* Binding() const { return binding_; }
    /// Names the button that starts mouse look (an `os::MouseButtonBit`); 0 turns mouse look off. Default: the right
    /// button.
    void SetLookButton(u32 button);
    /// Returns the look button.
    ///
    /// - Fails: never.
    u32 LookButton() const { return look_button_; }
    /// Returns whether mouse look is going on.
    ///
    /// - Fails: never.
    bool Looking() const { return looking_; }
    /// Returns whether mouse look holds a pointer lock (false when it runs on captured absolute motion).
    ///
    /// - Fails: never.
    bool LookLocked() const { return look_locked_; }
    /// Returns the width in physical pixels the binding was last told, 0 before the first call.
    ///
    /// - Fails: never.
    u32 PixelWidth() const { return pixel_w_; }
    /// Returns the height in physical pixels the binding was last told, 0 before the first call.
    ///
    /// - Fails: never.
    u32 PixelHeight() const { return pixel_h_; }
    /// Returns how many times `Render` was called on the binding.
    ///
    /// - Fails: never.
    u64 RenderCount() const { return render_count_; }
    /// Returns how many times `OnResize` was called on the binding.
    ///
    /// - Fails: never.
    u64 ResizeCount() const { return resize_count_; }
    /// Returns the size of the bounds as the widget's preferred size: 0 x 0, the viewport fills the space it is given.
    Size Measure(const Constraints& constraints) override;
    /// Calls the binding and draws its image.
    void Draw(Canvas& canvas) override;
    /// Turns events into `ViewportInput`s.
    void OnEvent(Event& event) override;
    /// Advances the frame time and marks the draw dirty.
    void Update(f32 dt) override;
    /// Returns true: the viewport redraws every frame.
    ///
    /// - Fails: never.
    bool WantsUpdate() const override { return true; }

private:
    void Send(ViewportInput& input, Event* event);
    ViewportInput MakeInput(ViewportInputType type, const Event& event) const;
    void BeginLook(const Event& event);
    void EndLook();

    ViewportBinding* binding_ = nullptr;
    u32 look_button_ = 0;
    bool looking_ = false;
    bool look_locked_ = false;
    bool warned_no_lock_ = false;
    Point look_last_;
    u32 pixel_w_ = 0;
    u32 pixel_h_ = 0;
    f32 frame_delta_ = 0;
    u64 render_count_ = 0;
    u64 resize_count_ = 0;
};

}  // namespace vse::ui
