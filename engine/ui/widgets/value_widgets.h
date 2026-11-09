#pragma once

#include "ui/callback.h"
#include "ui/text_run.h"
#include "ui/widget.h"
#include "ui/widgets/range.h"

//! The widgets that hold a number (Phase 15, Day 196): the slider and the spin box.

namespace engine_placeholder::ui {

/// A thumb on a track that chooses a value in a range.
///
/// Horizontal (minimum at the left) or vertical (minimum at the bottom). The value snaps to the step exactly ([[ui::ValueRange]]). The pointer drags the thumb (captured, so the drag may
/// leave the slider); a press on the track jumps there and goes on dragging, or, with `SetTrackPages(true)`, moves one page towards the pointer. Keys: arrows by a step, PageUp/PageDown by a
/// page, Home and End to the ends; the wheel only when switched on. `OnChanged` runs whenever the user changes the value (while dragging too), `OnCommit` when a drag ends or a key
/// changed it. `SetValue` changes the value without running either.
///
/// Thread-safety: owner-thread-only.
/// Ownership: owns nothing.
class Slider : public Widget {
public:
    /// The page as a fraction of the range when none was set.
    static constexpr f64 kPageFraction = 0.1;
    /// The arrow key's distance as a fraction of the range when the range has no step.
    static constexpr f64 kKeyFraction = 0.01;
    /// The thickness of the track as a multiple of the theme's border width.
    static constexpr f32 kTrackBorders = 4.0f;
    /// The length of a tick mark as a fraction of the thumb's thickness.
    static constexpr f32 kTickFraction = 0.25f;
    /// The preferred length of the track as a multiple of the thumb's thickness.
    static constexpr f32 kLengthInThumbs = 8.0f;

    /// Makes a slider over 0..1.
    /// Param vertical: whether the track runs bottom to top.
    explicit Slider(StringView name = StringView(), bool vertical = false) : Widget(name), vertical_(vertical) { SetFocusable(true); }
    /// Sets the range (and the step and policy in it) and applies it to the current value.
    void SetRange(const ValueRange& range);
    /// Returns the range.
    const ValueRange& Range() const { return range_; }
    /// Sets the value through the range's policy; marks the draw dirty. Runs no listener.
    void SetValue(f64 value);
    /// Returns the value.
    ///
    /// Fails: never.
    f64 Value() const { return value_; }
    /// Sets the distance of a page (PageUp, a track press with `SetTrackPages`); 0 means a tenth of the range.
    void SetPage(f64 page) { page_ = page; }
    /// Makes a press on the track move by a page instead of jumping to the pointer (default off).
    void SetTrackPages(bool pages) { track_pages_ = pages; }
    /// Lets the wheel change the value by a step per notch (default off).
    void SetWheel(bool wheel) { wheel_ = wheel; }
    /// Draws `count` evenly spaced tick marks along the track (default 0); marks the draw dirty.
    void SetTicks(u32 count);
    /// Returns the thumb's rectangle in surface coordinates.
    Rect ThumbRect() const;
    /// Returns the listeners called when the user changes the value (with the slider).
    Callback<Slider*>& OnChanged() { return on_changed_; }
    /// Returns the listeners called when a drag ends or a key changed the value (with the slider).
    Callback<Slider*>& OnCommit() { return on_commit_; }
    /// Returns the size of a track long enough to use, and a thumb thick.
    Size Measure(const Constraints& constraints) override;
    /// Draws the track, the ticks and the thumb.
    void Draw(Canvas& canvas) override;
    /// Handles the pointer, the wheel and the keys.
    void OnEvent(Event& event) override;

private:
    f32 ThumbLength() const;
    f32 TrackLength() const;
    f64 ValueAt(f32 along) const;
    f32 PositionOf(f64 value) const;
    void UserSet(f64 value);
    f64 Page() const;
    f64 KeyStep() const;

    bool vertical_;
    ValueRange range_;
    f64 value_ = 0;
    f64 page_ = 0;
    bool track_pages_ = false;
    bool wheel_ = false;
    bool dragging_ = false;
    f32 grab_ = 0;
    u32 ticks_ = 0;
    Callback<Slider*> on_changed_;
    Callback<Slider*> on_commit_;
};

/// A number with up and down buttons.
///
/// The value text is drawn in the `textfield` style; the right part of the box holds the buttons (up above, down below). A press on a button, an arrow key or a wheel notch changes the value
/// by the step, PageUp and PageDown by ten steps, Home and End go to the ends; every change goes through the range, so the value is always in range and on the grid. Typing a number is
/// the text field's job (Day 197), which this widget embeds then; today the text is the formatted value.
///
/// Thread-safety: owner-thread-only.
/// Ownership: owns its text.
class SpinBox : public Widget {
public:
    /// The width of the button column as a multiple of the theme's icon size.
    static constexpr f32 kButtonIcons = 1.5f;
    /// How many steps PageUp and PageDown move.
    static constexpr f64 kPageSteps = 10.0;
    /// The default maximum.
    static constexpr f64 kDefaultMax = 100.0;
    /// The most digits after the decimal point.
    static constexpr int kMaxDecimals = 9;

    /// Makes a spin box over 0..100 with step 1 and no decimals.
    explicit SpinBox(StringView name = StringView());
    /// Sets the range and applies it to the value; marks layout dirty.
    void SetRange(const ValueRange& range);
    /// Returns the range.
    const ValueRange& Range() const { return range_; }
    /// Sets the value through the range's policy; runs no listener.
    void SetValue(f64 value);
    /// Returns the value.
    ///
    /// Fails: never.
    f64 Value() const { return value_; }
    /// Sets how many digits follow the decimal point in the text (0 to 9); marks layout dirty.
    void SetDecimals(int decimals);
    /// Returns the listeners called when the user changes the value (with the box).
    Callback<SpinBox*>& OnChanged() { return on_changed_; }
    /// Returns the value as the box shows it.
    String<HeapAllocator> ValueText() const;
    /// Returns the size of the widest value plus the buttons.
    Size Measure(const Constraints& constraints) override;
    /// Draws the box, the text and the buttons.
    void Draw(Canvas& canvas) override;
    /// Handles the buttons, the wheel and the keys.
    void OnEvent(Event& event) override;

private:
    void UserSet(f64 value);
    f64 Step() const;
    f32 ButtonWidth() const;

    ValueRange range_;
    f64 value_ = 0;
    int decimals_ = 0;
    TextRun run_;
    Callback<SpinBox*> on_changed_;
};

} // namespace engine_placeholder::ui
