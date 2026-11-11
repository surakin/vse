#pragma once

#include "ui/text_run.h"
#include "ui/widget.h"

//! The tooltip bubble (Phase 15, Day 198): a short text in a box near the pointer, shown by the surface for a widget with a tooltip text.

namespace engine_placeholder::ui {

/// A bubble with wrapped text, drawn in the `tooltip` style. It never takes the pointer (the pointer goes through it).
///
/// - Thread-safety: owner-thread-only.
/// - Ownership: owns its text.
class TooltipBubble : public Widget {
public:
    /// The widest the text may be before it wraps, as a multiple of the theme's icon size.
    static constexpr f32 kMaxWidthIcons = 20.0f;

    /// Makes an empty bubble that is transparent to the pointer.
    TooltipBubble();
    /// Sets the text; marks layout dirty.
    void SetText(StringView text);
    /// Returns the text size plus the style's padding.
    Size Measure(const Constraints& constraints) override;
    /// Draws the box and the text.
    void Draw(Canvas& canvas) override;

private:
    TextRun run_;
};

} // namespace engine_placeholder::ui
