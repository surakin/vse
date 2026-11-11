#pragma once

#include "containers/span.h"
#include "containers/vector.h"
#include "ui/callback.h"
#include "ui/layout.h"
#include "ui/widget.h"

//! The splitter (Phase 15, Day 199): panes side by side or one over the other, with handles the user drags.

namespace engine_placeholder::ui {

/// Two or more panes along one axis, separated by draggable handles.
///
/// Every pane has a relative size (a fraction of the room left after the handles); the sizes follow the splitter when it resizes, in proportion, and a pane never gets less than its
/// minimum (`SetMinSize`) while the room allows it. A press on a handle captures the pointer and drags the boundary between the two panes beside it (the resize cursor shows over a
/// handle); a double click on a handle restores the sizes the panes were added with. A pane marked collapsible (`SetCollapsible`) collapses when a drag takes it below half its minimum and
/// opens again when a drag takes it back over that, and `SetPaneCollapsed` does the same from code. The layout is plain numbers: `Fractions` and `SetFractions` (and the collapsed flags)
/// are what a layout file would store.
///
/// `Horizontal` places the panes left to right (the handles are vertical bars); `Vertical` top to bottom. The setters run no listener; a drag and a double click run `OnChanged`.
///
/// - Thread-safety: owner-thread-only.
/// - Ownership: owns its panes.
class Splitter : public Widget {
public:
    /// The thickness of a handle, in logical units.
    static constexpr f32 kHandle = 6.0f;
    /// Seconds within which a second press on a handle is a double click.
    static constexpr f64 kDoubleClickSeconds = 0.4;
    /// How far (logical units) the second press of a double click may lie from the first.
    static constexpr f32 kClickSlop = 4.0f;
    /// A collapsible pane collapses when it is dragged below this fraction of its minimum.
    static constexpr f32 kCollapseFraction = 0.5f;
    /// No handle.
    static constexpr usize kNone = static_cast<usize>(-1);

    /// Makes an empty splitter.
    /// - Param orientation: the axis the panes follow.
    explicit Splitter(Orientation orientation, StringView name = StringView());
    /// Adds a pane at the end.
    /// - Param pane: the pane's widget.
    /// - Param weight: its relative size when the splitter starts (above 0); also what `Reset` returns to.
    /// - Returns: the pane's index.
    /// - Ownership: the splitter owns the pane.
    /// - Aborts: `pane` is null or `weight` is not above 0.
    usize AddPane(UniquePtr<Widget> pane, f32 weight = 1.0f);
    /// Returns how many panes there are.
    ///
    /// - Fails: never.
    usize PaneCount() const { return panes_.Size(); }
    /// Returns pane `index`.
    /// - Ownership: borrowed; the splitter owns it.
    /// - Aborts: `index` is out of range.
    Widget* Pane(usize index) const;
    /// Sets the smallest size (along the axis) a pane may be dragged or resized to; 0 for none.
    /// - Aborts: `index` is out of range.
    void SetMinSize(usize index, f32 min);
    /// Lets a drag collapse the pane.
    /// - Aborts: `index` is out of range.
    void SetCollapsible(usize index, bool collapsible);
    /// Collapses or opens a pane (a collapsed pane takes no room and is not shown). Runs no listener.
    /// - Aborts: `index` is out of range.
    void SetPaneCollapsed(usize index, bool collapsed);
    /// Returns whether pane `index` is collapsed.
    ///
    /// - Fails: returns false for an index out of range.
    bool PaneCollapsed(usize index) const { return index < panes_.Size() && panes_[index].collapsed; }
    /// Copies the relative sizes of the panes (they sum to 1 over the panes that are not collapsed) into `out`, cleared first.
    /// - Ownership: `out` must be non-null.
    void Fractions(Vector<f32>* out) const;
    /// Sets the relative sizes (normalized over the panes that are not collapsed). Runs no listener.
    /// - Param fractions: one positive number per pane; ignored when the count is wrong or a number is not above 0.
    void SetFractions(Span<const f32> fractions);
    /// Restores the sizes the panes were added with and opens every pane. Runs no listener.
    void Reset();
    /// Returns the size of pane `index` along the axis, in logical units, as of the last layout (0 when collapsed or out of range).
    f32 PaneSize(usize index) const { return index < panes_.Size() ? panes_[index].size : 0.0f; }
    /// Returns the rectangle of handle `index` (between pane `index` and the next) in surface coordinates.
    /// - Fails: returns an empty rectangle for an index out of range.
    Rect HandleRect(usize index) const;
    /// Returns the listeners called after a drag moves a boundary or a double click resets the sizes (with the splitter).
    Callback<Splitter*>& OnChanged() { return on_changed_; }
    /// Returns the sum of the panes' preferred sizes plus the handles.
    Size Measure(const Constraints& constraints) override;
    /// Sizes and places the panes.
    void Arrange(const Rect& rect) override;
    /// Draws the handles.
    void Draw(Canvas& canvas) override;
    /// Handles the pointer.
    void OnEvent(Event& event) override;

private:
    struct PaneInfo {
        Widget* widget = nullptr;
        f32 fraction = 0;
        f32 initial = 0;
        f32 min = 0;
        f32 size = 0;
        bool collapsible = false;
        bool collapsed = false;
    };
    bool Horizontal() const { return orientation_ == Orientation::Horizontal; }
    f32 Length() const { return Horizontal() ? Bounds().w : Bounds().h; }
    f32 Room() const;
    void Solve();
    usize HandleAt(Point local) const;
    f32 StartOf(usize pane) const;
    void DragTo(f32 boundary);
    void UpdateFractions();

    Orientation orientation_;
    Vector<PaneInfo> panes_;
    usize hover_ = kNone;
    usize drag_ = kNone;
    f32 grab_ = 0;
    f64 last_press_time_ = -1e9;
    Point last_press_;
    Callback<Splitter*> on_changed_;
};

} // namespace engine_placeholder::ui
