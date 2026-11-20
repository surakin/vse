#pragma once

#include "ui/text_block.h"

//! The label widget (Phase 15, Day 195).

namespace vse::ui
{

/// Static text drawn from the `label` style of the theme.
///
/// A [[ui::TextBlock]] with the label's look: the font and color come from the style of the label kind and the chosen
/// variant (`heading`), in the widget's state, so a disabled label uses the disabled color. Wrapping, alignment and the
/// ellipsis for a line that does not fit are the block's.
///
/// - Thread-safety: owner-thread-only.
/// - Ownership: as [[ui::TextBlock]].
class Label : public TextBlock
{
public:
    /// Makes a label.
    /// - Param name: the widget name.
    /// - Param text: the UTF-8 text.
    explicit Label(StringView name = StringView(), StringView text = StringView()) : TextBlock(name, text)
    {
        SetLook(widget_kind::kLabel, 0);
    }
    /// Uses the style variant called `variant` (for example `heading`); the empty name is the default style. Marks
    /// layout dirty.
    void SetVariant(StringView variant) { SetLook(widget_kind::kLabel, RegisterVariant(variant)); }
    // Out of line on purpose: it gives the class a key function, so its vtable lives in the library (built without
    // run-time type information) and a test that has it does not need the base's type information.
    /// Returns the size of the text, as [[ui::TextBlock::Measure]].
    Size Measure(const Constraints& constraints) override;
};

}  // namespace vse::ui
