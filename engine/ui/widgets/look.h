#pragma once

#include "ui/canvas.h"
#include "ui/event.h"
#include "ui/theme.h"

//! Shared drawing of the widgets of Phase 15 Day 195: a resolved style turned into a box, in logical units.

namespace engine_placeholder::ui {

/// Returns whether a packed `0xAABBGGRR` color is not fully transparent.
///
/// - Fails: never.
inline bool IsVisibleColor(u32 abgr) { return (abgr >> 24) != 0; }

/// Returns whether `c` is an ASCII digit.
///
/// - Fails: never.
inline bool IsAsciiDigit(char c) { return c >= '0' && c <= '9'; }

/// Returns `c` in lower case when it is an ASCII capital.
///
/// - Fails: never.
inline char AsciiLower(char c) { return (c >= 'A' && c <= 'Z') ? static_cast<char>(c - 'A' + 'a') : c; }

/// Returns the lower-case letter a key stands for, or 0 when it is not a letter key.
///
/// - Fails: never.
inline char KeyLetter(Key key) {
    const u32 k = static_cast<u32>(key);
    const u32 a = static_cast<u32>(Key::A);
    return (k >= a && k <= static_cast<u32>(Key::Z)) ? static_cast<char>('a' + (k - a)) : static_cast<char>(0);
}

/// Returns `abgr` with its alpha replaced by `alpha` (0 to 255).
inline u32 WithAlpha(u32 abgr, u32 alpha) { return (abgr & 0x00FFFFFFu) | (alpha << 24); }

/// The width of the band along an edge in which a drag auto-scrolls, in logical units.
constexpr f32 kDragScrollBand = 24.0f;
/// The auto-scroll speed at the very edge, in logical units per second (it falls to 0 at the inner side of the band).
constexpr f32 kDragScrollSpeed = 600.0f;
/// The thickness of an insertion line, in logical units.
constexpr f32 kDropMarkerThickness = 2.0f;

/// Returns how far a drag at `along` (0 to `length`) scrolls in `dt` seconds: negative near the start, positive near the end, 0 elsewhere; it grows with the depth into the band.
///
/// - Fails: never.
inline f32 DragScrollDelta(f32 along, f32 length, f32 dt) {
    if (length <= kDragScrollBand * 2) return 0.0f; // a view too small for two bands does not scroll under a drag
    if (along < kDragScrollBand) {
        const f32 depth = along <= 0 ? 1.0f : (kDragScrollBand - along) / kDragScrollBand;
        return -depth * kDragScrollSpeed * dt;
    }
    if (along > length - kDragScrollBand) {
        const f32 depth = along >= length ? 1.0f : (along - (length - kDragScrollBand)) / kDragScrollBand;
        return depth * kDragScrollSpeed * dt;
    }
    return 0.0f;
}

/// Returns the style's corner radius in logical units.
///
/// - Fails: never.
inline f32 StyleRadius(const ResolvedStyle& style, f32 scale) { return style.radius / scale; }

/// Draws the background and the border of `style` in `rect`; a transparent background and a zero border width draw nothing.
inline void DrawStyleBox(Canvas& canvas, const Rect& rect, const ResolvedStyle& style, f32 scale) {
    const CornerRadii radii(StyleRadius(style, scale));
    if (IsVisibleColor(style.background)) {
        if (radii.AllZero()) {
            canvas.DrawRect(rect, style.background);
        } else {
            canvas.DrawRoundedRect(rect, radii, style.background);
        }
    }
    if (style.border_width > 0) canvas.DrawBorder(rect, radii, style.border_width / scale, style.border_color);
}

} // namespace engine_placeholder::ui
