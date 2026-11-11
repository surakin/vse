#pragma once

#include "text/glyph_atlas.h"
#include "types.h"
#include "ui/draw_list.h"
#include "ui/surface.h"

//! When a surface runs a frame (Phase 15, Day 202): every frame, only when something changed, or at a fixed rate. A
//! screen in the world that nobody touches should cost nothing.

namespace engine_placeholder::ui
{

/// How often a surface runs a frame.
enum class UpdatePolicy : u8
{
    EveryFrame,  ///< Every call runs a frame: animations and the viewport stay smooth.
    OnDemand,    ///< A frame runs the first time and then only when `Surface::NeedsFrame()` says something changed; an
                 ///< idle surface does no work at all.
    FixedRate,   ///< A frame runs at most `rate` times a second, whatever the caller's frame rate (a low-priority
                 ///< monitor).
};

/// What one call of [[ui::SurfaceUpdater::Frame]] did.
///
/// - Thread-safety: thread-compatible plain data.
struct UpdateResult
{
    bool ran = false;      ///< The surface ran a frame.
    bool rebuilt = false;  ///< The draw list was rebuilt: the picture changed and the surface's texture must be drawn
                           ///< again.
};

/// Applies an [[ui::UpdatePolicy]] to one surface.
///
/// The time of skipped frames is not lost: the next frame that runs is given the sum, so animations keep their speed.
/// The draw list keeps its last contents between updates, and the texture it was drawn into keeps its last picture.
///
/// - Thread-safety: owner-thread-only.
/// - Ownership: borrows the surface, which must outlive this object.
class SurfaceUpdater
{
public:
    /// Makes an updater for `surface` (every frame).
    /// - Param surface: the surface; borrowed.
    /// - Ownership: borrows `surface`.
    explicit SurfaceUpdater(Surface* surface) : surface_(surface) {}
    /// Sets the policy; `rate` is the frames a second for `FixedRate` (at least 1; ignored by the others).
    void SetPolicy(UpdatePolicy policy, f32 rate = 10.0f);
    /// Returns the policy.
    ///
    /// - Fails: never.
    UpdatePolicy Policy() const { return policy_; }
    /// Calls the surface's `Frame` when the policy says so, with the time gathered since it last ran.
    /// - Param dt: seconds since the caller's last call.
    /// - Param list: the draw list to fill, or null to run only the logic.
    /// - Param atlas: the glyph atlas for text.
    /// - Returns: whether a frame ran and whether the list was rebuilt.
    /// - Ownership: borrowed; the caller keeps ownership and it must outlive its use here.
    UpdateResult Frame(f32 dt, DrawList* list, text::GlyphAtlas* atlas);
    /// Returns the frames run.
    ///
    /// - Fails: never.
    u64 FramesRun() const { return run_; }
    /// Returns the calls that ran nothing.
    ///
    /// - Fails: never.
    u64 FramesSkipped() const { return skipped_; }

private:
    Surface* surface_;
    UpdatePolicy policy_ = UpdatePolicy::EveryFrame;
    f32 period_ = 0.1f;
    f32 pending_ = 0;
    bool started_ = false;
    u64 run_ = 0;
    u64 skipped_ = 0;
};

}  // namespace engine_placeholder::ui
