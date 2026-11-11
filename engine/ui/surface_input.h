#pragma once

#include "platform/input.h"
#include "types.h"
#include "ui/event.h"
#include "ui/surface.h"

//! Input for a surface that has no window (Phase 15, Day 202): a screen in the world that the application feeds from a ray cast, a replay or a test.

namespace engine_placeholder::ui {

/// A ray: where it starts and which way it goes (the direction need not be a unit vector).
///
/// - Thread-safety: thread-compatible plain data.
struct Ray3 {
    f32 origin[3] = {0, 0, 0};    ///< The start.
    f32 direction[3] = {0, 0, 1}; ///< The direction.
};

/// A flat parallelogram in the world that shows a surface: `corner` is the surface's top-left corner, `edge_u` goes along its top edge to the top-right corner and `edge_v` down its
/// left edge to the bottom-left corner.
///
/// The front face is the side from which `edge_u` points to the right and `edge_v` points down. The two edges need not be at right angles or of equal length to the surface's shape:
/// the hit is returned as a fraction of each edge.
///
/// - Thread-safety: thread-compatible plain data.
struct SurfaceQuad {
    f32 corner[3] = {0, 0, 0}; ///< The top-left corner.
    f32 edge_u[3] = {1, 0, 0}; ///< From the top-left to the top-right corner.
    f32 edge_v[3] = {0, -1, 0}; ///< From the top-left to the bottom-left corner.
};

/// Where a ray met a quad.
///
/// - Thread-safety: thread-compatible plain data.
struct SurfaceHit {
    bool hit = false;       ///< The ray crossed the front face inside the quad: `u` and `v` are set.
    bool back_face = false; ///< The ray crossed the quad inside its edges but from behind: `hit` is false.
    f32 u = 0;              ///< Along `edge_u`, 0 at the left edge and 1 at the right.
    f32 v = 0;              ///< Along `edge_v`, 0 at the top edge and 1 at the bottom.
    f32 t = 0;              ///< The distance along the ray, in lengths of `direction`.
};

/// Finds where a ray crosses a quad. Math only: no scene type is involved.
///
/// - Param ray: the ray; a zero direction hits nothing.
/// - Param quad: the quad; a degenerate one (edges parallel or of length 0) hits nothing.
/// - Returns: the hit; a ray parallel to the quad, pointing away from it, or crossing outside it misses.
/// - Fails: reported through the result (`hit` false), never by aborting.
SurfaceHit RayToSurfacePoint(const Ray3& ray, const SurfaceQuad& quad);

/// Feeds a surface the events a window adapter would, from pointer positions in the surface's own pixels.
///
/// A surface with input off (the default) is display-only: every call is ignored. An interactive one reacts like any other: hover and pressed states, capture and focus work per
/// surface. `Lost` ends what a vanished pointer leaves behind: buttons still held are released and the hover is cleared.
///
/// - Thread-safety: owner-thread-only.
/// - Ownership: borrows the surface, which must outlive this object.
class SurfaceInput {
public:
    /// Makes an input for `surface`, display-only.
    /// - Param surface: the surface; borrowed.
    /// - Ownership: borrows `surface`.
    explicit SurfaceInput(Surface* surface) : surface_(surface) {}
    /// Turns input on or off; turning it off first does what `Lost` does.
    void SetInteractive(bool interactive);
    /// Returns whether input is accepted.
    ///
    /// - Fails: never.
    bool Interactive() const { return interactive_; }
    /// Moves the pointer to (`x`, `y`) in surface pixels (the surface's logical units times its scale).
    void PointerMove(f32 x, f32 y, u32 modifiers = 0);
    /// Moves the pointer to the point (`u`, `v`) of the surface, each 0 to 1 (what `RayToSurfacePoint` gives).
    void PointerMoveUv(f32 u, f32 v, u32 modifiers = 0);
    /// Presses or releases `button` (an `os::MouseButtonBit`) at (`x`, `y`) in surface pixels.
    void PointerButton(f32 x, f32 y, u32 button, bool down, u32 modifiers = 0);
    /// Like `PointerButton` at the point (`u`, `v`).
    void PointerButtonUv(f32 u, f32 v, u32 button, bool down, u32 modifiers = 0);
    /// Turns the wheel by (`dx`, `dy`) notches at the last pointer position.
    void Wheel(f32 dx, f32 dy, u32 modifiers = 0);
    /// Sends a key press or release to the focus owner.
    void Key(ui::Key key, bool down, bool repeat = false, u32 modifiers = 0);
    /// Types `utf8`, one scalar at a time.
    void Text(StringView utf8);
    /// Tells the surface whether the program's focus is on it (a screen the player is using has the focus): losing it releases the pointer, closes popups and unfocuses.
    void Focus(bool focused);
    /// The pointer left the surface or its source vanished (the ray no longer hits): releases the buttons still held, then clears the hover.
    void Lost();
    /// Returns the mask of buttons this input holds down.
    ///
    /// - Fails: never.
    u32 ButtonsDown() const { return buttons_; }

private:
    Point ToLogical(f32 x, f32 y) const;
    void Post(Event& e);

    Surface* surface_;
    bool interactive_ = false;
    u32 buttons_ = 0;
    Point last_;
};

} // namespace engine_placeholder::ui
