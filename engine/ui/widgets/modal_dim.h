#pragma once

#include "ui/widget.h"

//! The shade under a modal dialog (Phase 15, Day 199): it covers the surface, takes every pointer event, and is drawn
//! in the `modaldim` style.

namespace vse::ui
{

/// A full-surface widget that shades what lies beneath a modal dialog and stops the pointer from reaching it. The
/// surface adds one under each modal dialog.
///
/// - Thread-safety: owner-thread-only.
/// - Ownership: owns nothing.
class ModalDim : public Widget
{
public:
    /// Makes a dim layer that fills its parent.
    ModalDim();
    /// Draws the shade.
    void Draw(Canvas& canvas) override;
};

}  // namespace vse::ui
