#pragma once

#include "ui/canvas.h"
#include "ui/layout.h"
#include "ui/layout_types.h"
#include "ui/theme.h"
#include "ui/widget.h"

//! The display widgets: image, progress bar, separator and panel. Labels are in
//! `ui/widgets/label.h`, buttons in `ui/widgets/buttons.h`.

namespace vse::ui
{

/// How an [[ui::Image]] fills its bounds.
enum class ImageFit : u8
{
    Stretch,  ///< The image fills the bounds, distorted if the shapes differ.
    Contain,  ///< The whole image, scaled to fit, centered, keeping its shape.
    Cover,    ///< The image scaled to cover the bounds, keeping its shape; the overflow is cut.
    None,     ///< The natural size, centered; cut by the bounds when larger.
};

/// A picture by [[ui::ImageId]].
///
/// The id is opaque to the toolkit (the renderer's texture registry resolves it); the natural size is given with the
/// image. `Measure` reports the natural size. Cutting is done by narrowing the texture coordinates, so nothing is drawn
/// outside the bounds.
///
/// - Thread-safety: owner-thread-only.
/// - Ownership: owns nothing; the image is borrowed from whoever registered it.
class Image : public Widget
{
public:
    /// Makes an image widget with no picture.
    explicit Image(StringView name = StringView()) : Widget(name) {}
    /// Shows `image`, whose natural size is `natural` logical units; marks layout dirty.
    void SetImage(ImageId image, Size natural);
    /// Returns the image id (0 for none).
    ImageId GetImage() const { return image_; }
    /// Sets the fit (default `Contain`); marks the draw dirty.
    void SetFit(ImageFit fit);
    /// Sets the tint, packed `0xAABBGGRR` (default opaque white: the picture as it is); marks the draw dirty.
    void SetTint(u32 abgr);
    /// Returns the natural size.
    Size Measure(const Constraints& constraints) override;
    /// Draws the picture.
    void Draw(Canvas& canvas) override;

private:
    ImageId image_ = 0;
    Size natural_;
    ImageFit fit_ = ImageFit::Contain;
    u32 tint_ = 0xFFFFFFFFu;
};

/// A bar that fills from the left with a value from 0 to 1, or a segment that slides while the progress is unknown.
///
/// The colors and corner radius come from the `panel` style (the track) and the theme's accent color (the fill). The
/// indeterminate segment moves with the surface clock, one pass per `kProgressPeriod` seconds, and the widget asks for
/// `Update` only while it is indeterminate so that a still bar costs nothing.
///
/// - Thread-safety: owner-thread-only.
/// - Ownership: owns nothing.
class ProgressBar : public Widget
{
public:
    /// The seconds one pass of the indeterminate segment takes.
    static constexpr f32 kProgressPeriod = 1.2f;
    /// The length of the indeterminate segment as a fraction of the track.
    static constexpr f32 kSegment = 0.3f;
    /// The preferred length of the bar as a multiple of the theme's icon size.
    static constexpr f32 kLengthInIcons = 8.0f;

    /// Makes a bar at 0.
    explicit ProgressBar(StringView name = StringView()) : Widget(name) {}
    /// Sets the value, clamped to 0..1; marks the draw dirty. A NaN is taken as 0.
    void SetValue(f32 value);
    /// Returns the value.
    f32 Value() const { return value_; }
    /// Switches the sliding segment on or off; marks the draw dirty.
    void SetIndeterminate(bool indeterminate);
    /// Returns whether the bar is indeterminate.
    ///
    /// - Fails: never.
    bool Indeterminate() const { return indeterminate_; }
    /// Returns the preferred size: a length of `kLengthInIcons` icons and half an icon of height.
    Size Measure(const Constraints& constraints) override;
    /// Draws the track and the fill or the segment.
    void Draw(Canvas& canvas) override;
    /// Redraws while indeterminate.
    void Update(f32) override { MarkDirty(kDrawDirty); }
    /// Returns whether the bar is indeterminate.
    ///
    /// - Fails: never.
    bool WantsUpdate() const override { return indeterminate_; }

private:
    f32 value_ = 0;
    bool indeterminate_ = false;
};

/// A line that divides a layout: horizontal or vertical, in the theme's border color and thickness.
///
/// - Thread-safety: owner-thread-only.
/// - Ownership: owns nothing.
class Separator : public Widget
{
public:
    /// Makes a separator.
    /// - Param vertical: whether the line is vertical (it divides a row) instead of horizontal.
    explicit Separator(StringView name = StringView(), bool vertical = false) : Widget(name), vertical_(vertical) {}
    /// Returns the thickness on one axis and nothing on the other.
    Size Measure(const Constraints& constraints) override;
    /// Draws the line across the bounds.
    void Draw(Canvas& canvas) override;

private:
    bool vertical_;
};

/// A container with a background and a border from the `panel` style: a [[ui::StackLayout]] that paints itself.
///
/// - Thread-safety: owner-thread-only.
/// - Ownership: owns its children.
class Panel : public StackLayout
{
public:
    /// Makes a panel.
    explicit Panel(StringView name = StringView()) : StackLayout(name) {}
    /// Uses the style variant called `variant`; marks the draw dirty.
    void SetVariant(StringView variant);
    /// Draws the background and the border.
    void Draw(Canvas& canvas) override;

private:
    VariantId variant_ = 0;
};

}  // namespace vse::ui
