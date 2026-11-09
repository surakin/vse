#pragma once

#include "ui/callback.h"
#include "ui/canvas.h"
#include "ui/geometry.h"
#include "ui/layout_types.h"
#include "ui/widget.h"

//! The scrolling widgets (Phase 15, Day 196): the scroll bar and the scroll view that the list box and the text field share.

namespace engine_placeholder::ui {

/// A scroll bar along one axis, with a value in content units.
///
/// The thumb's length is the track's length times the view/content ratio, never below the theme's minimum hit size and never above the track; its place follows the value. A press on the
/// thumb drags it (the pointer is captured, so the drag may leave the bar), a press on the track outside the thumb pages by one view towards the pointer, a press on an arrow (when
/// shown) moves by one line. With content not larger than the view the value is 0 and the thumb fills the track. The auto-hide variant draws nothing unless the pointer is over it, it
/// is being dragged, or it changed within the last `kFadeSeconds`.
///
/// Thread-safety: owner-thread-only.
/// Ownership: owns nothing.
class ScrollBar : public Widget {
public:
    /// The seconds an auto-hide bar stays visible after its value changed.
    static constexpr f32 kFadeSeconds = 1.0f;
    /// The distance a press on an arrow moves, as a fraction of the view when no line step was set.
    static constexpr f32 kLineFraction = 0.1f;

    /// Makes a bar.
    /// Param vertical: whether the bar runs top to bottom.
    explicit ScrollBar(StringView name = StringView(), bool vertical = true) : Widget(name), vertical_(vertical) {}
    /// Sets the content and the view length in content units; the value is clamped to the new range; marks the draw dirty.
    void SetRange(f64 content, f64 view);
    /// Returns the content length.
    ///
    /// Fails: never.
    f64 Content() const { return content_; }
    /// Returns the view length.
    ///
    /// Fails: never.
    f64 View() const { return view_; }
    /// Returns the largest value: the content minus the view, or 0.
    ///
    /// Fails: never.
    f64 MaxValue() const { return content_ > view_ ? content_ - view_ : 0.0; }
    /// Sets the value (the offset of the view's start in content units), clamped to 0..`MaxValue()`, without running the listeners; marks the draw dirty.
    void SetValue(f64 value);
    /// Returns the value.
    ///
    /// Fails: never.
    f64 Value() const { return value_; }
    /// Sets how far an arrow press and a wheel notch move, in content units; 0 (the default) means a tenth of the view.
    void SetLineStep(f64 step) { line_step_ = step; }
    /// Returns the line step in content units (a tenth of the view when none was set).
    ///
    /// Fails: never.
    f64 LineStep() const { return line_step_ > 0 ? line_step_ : view_ * kLineFraction; }
    /// Shows arrow buttons at the ends of the track (default off); marks layout dirty.
    void SetArrows(bool arrows);
    /// Makes the bar the auto-hide variant (default off); marks the draw dirty.
    void SetAutoHide(bool auto_hide);
    /// Returns the thumb's rectangle in surface coordinates, for tests and for a view that draws over the bar.
    ///
    /// Fails: never.
    Rect ThumbRect() const;
    /// Returns the listeners called when the user moves the bar (with the bar); not when `SetValue` or `SetRange` change it.
    Callback<ScrollBar*>& OnChanged() { return on_changed_; }
    /// Returns the thickness across and nothing along.
    Size Measure(const Constraints& constraints) override;
    /// Draws the track, the arrows and the thumb.
    void Draw(Canvas& canvas) override;
    /// Handles the pointer.
    void OnEvent(Event& event) override;
    /// Repaints while an auto-hide bar fades.
    void Update(f32) override { MarkDirty(kDrawDirty); }
    /// Returns whether an auto-hide bar is fading.
    ///
    /// Fails: never.
    bool WantsUpdate() const override;

private:
    struct Geometry {
        f32 start = 0;     // track start along the axis, local
        f32 length = 0;    // track length
        f32 thumb = 0;     // thumb length
        f32 thumb_pos = 0; // thumb start along the axis, local
        f32 arrow = 0;     // arrow button length (0 when none)
    };
    Geometry Measure2() const;
    f32 Along(Point local) const { return vertical_ ? local.y : local.x; }
    void Change(f64 value);
    bool Visible2() const;

    bool vertical_;
    bool arrows_ = false;
    bool auto_hide_ = false;
    bool dragging_ = false;
    f32 grab_ = 0;
    f64 content_ = 0;
    f64 view_ = 0;
    f64 value_ = 0;
    f64 line_step_ = 0;
    f64 changed_at_ = -1.0e9;
    Callback<ScrollBar*> on_changed_;
};

/// When a [[ui::ScrollView]] shows a bar.
enum class ScrollPolicy : u8 {
    Auto,   ///< Only when the content is larger than the view along that axis.
    Always, ///< Always.
    Never,  ///< Never; the content is given the view's size on that axis.
};

/// One child that may be larger than the view, with scroll bars.
///
/// The content is the first child; the bars follow. The content gets its preferred size along an axis that scrolls (at least the view's size, so it fills a small view) and exactly the
/// view's size along one that does not. The view clips its content's drawing and hit testing to its own bounds (the bars sit on top). The wheel scrolls by `LineStep` per notch and is
/// handled only when it moved something, so a wheel at the end of the content bubbles on to an outer view; a focus change inside the content scrolls the focused widget into view. There
/// is no inertia or elastic overscroll.
///
/// Thread-safety: owner-thread-only.
/// Ownership: owns the content and the bars.
class ScrollView : public Widget {
public:
    /// Makes a view with no content.
    explicit ScrollView(StringView name = StringView());
    /// Sets the content, replacing the old one; marks layout dirty.
    /// Param content: the widget to scroll.
    /// Returns: the content, owned by the view.
    /// Ownership: the view takes ownership of `content`.
    Widget* SetContent(UniquePtr<Widget> content);
    /// Returns the content, or null.
    ///
    /// Ownership: borrowed; the view owns it.
    /// Fails: returns null when there is none.
    Widget* Content() const { return content_; }
    /// Sets when each bar is shown; marks layout dirty.
    void SetPolicy(ScrollPolicy horizontal, ScrollPolicy vertical);
    /// Sets the distance of one wheel notch in logical units; 0 (the default) uses the theme's minimum hit size.
    void SetLineStep(f32 step) { line_step_ = step; }
    /// Returns the scroll offset: how far the content has moved up and to the left.
    ///
    /// Fails: never.
    Point Offset() const { return offset_; }
    /// Scrolls to `offset`, clamped to the content; marks layout dirty.
    void SetOffset(Point offset);
    /// Returns the size of the content after the last layout.
    ///
    /// Fails: never.
    Size ContentSize() const { return content_size_; }
    /// Returns the rectangle the content is seen through (the bounds without the bars) after the last layout.
    ///
    /// Fails: never.
    Rect Viewport() const { return viewport_; }
    /// Scrolls the least that makes `rect` (surface coordinates) fully visible; a rectangle larger than the view shows its start. Returns whether the offset changed.
    ///
    /// Fails: never.
    bool ScrollTo(const Rect& rect);
    /// Brings `widget` into view (see the rectangle overload). Returns whether the offset changed.
    ///
    /// Fails: a null widget changes nothing and returns false.
    /// Ownership: borrows `widget` for the call.
    bool ScrollTo(const Widget* widget);
    /// Returns the vertical bar.
    ///
    /// Fails: never.
    /// Ownership: borrowed; the view owns it.
    ScrollBar* VerticalBar() const { return vbar_; }
    /// Returns the horizontal bar.
    ///
    /// Fails: never.
    /// Ownership: borrowed; the view owns it.
    ScrollBar* HorizontalBar() const { return hbar_; }
    /// Returns nothing: the view takes the space it is given.
    Size Measure(const Constraints& constraints) override;
    /// Places the content, sets the bars and clamps the offset.
    void Arrange(const Rect& rect) override;
    /// Handles the wheel.
    void OnEvent(Event& event) override;
    /// Scrolls a widget that took the focus into view.
    ///
    /// Ownership: borrows the widget for the call.
    void OnDescendantFocused(Widget* widget) override;

private:
    static void BarMoved(void* user, ScrollBar* bar);
    f32 Step() const;

    Widget* content_ = nullptr;
    ScrollBar* vbar_ = nullptr;
    ScrollBar* hbar_ = nullptr;
    ScrollPolicy h_policy_ = ScrollPolicy::Auto;
    ScrollPolicy v_policy_ = ScrollPolicy::Auto;
    Point offset_;
    Size content_size_;
    Rect viewport_;
    f32 line_step_ = 0;
};

} // namespace engine_placeholder::ui
