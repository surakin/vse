#include "ui/update_policy.h"

namespace engine_placeholder::ui
{

void SurfaceUpdater::SetPolicy(UpdatePolicy policy, f32 rate)
{
    policy_ = policy;
    period_ = 1.0f / (rate >= 1.0f ? rate : 1.0f);
}

UpdateResult SurfaceUpdater::Frame(f32 dt, DrawList* list, text::GlyphAtlas* atlas)
{
    pending_ += dt > 0 ? dt : 0;
    bool go = true;
    if (policy_ == UpdatePolicy::OnDemand)
    {
        go = !started_ || surface_->NeedsFrame();  // the first frame always runs: the caller's list holds nothing yet
    }
    else if (policy_ == UpdatePolicy::FixedRate)
    {
        go = pending_ >= period_;
    }
    UpdateResult result;
    if (!go)
    {
        ++skipped_;
        return result;
    }
    const f32 elapsed = pending_;
    pending_ = 0;
    if (policy_ == UpdatePolicy::FixedRate)
    {
        // Keep the remainder past one period, so the average rate holds when the caller's frames do not divide the
        // period.
        const f32 remainder = elapsed - period_;
        pending_ = remainder > period_ ? 0 : remainder;
    }
    result.ran = true;
    started_ = true;
    ++run_;
    result.rebuilt = surface_->Frame(policy_ == UpdatePolicy::FixedRate ? period_ : elapsed, list, atlas);
    return result;
}

}  // namespace engine_placeholder::ui
