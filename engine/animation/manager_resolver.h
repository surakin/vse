#pragma once

//! [[animation::ManagerResolver]]: the animation system's view of the resource manager.

#include "animation/components.h"
#include "asset/cpu_loaders.h"
#include "asset/resource_manager.h"

namespace vse::animation
{

/// Finds skeletons and clips by id among the resources the manager has loaded. It takes no references: something else
/// (the level that names them, a handle of the game's) keeps them loaded while entities use them.
///
/// - Thread-safety: thread-safe; the manager serializes the lookups.
/// - Ownership: borrows the manager, which must outlive it.
class ManagerResolver final : public AnimationResolver
{
public:
    /// Makes a resolver over `manager`.
    ///
    /// - Ownership: borrows `manager`.
    explicit ManagerResolver(const asset::ResourceManager* manager) : manager_(manager) {}

    /// The skeleton with this id when it is loaded as a [[asset::SkeletonView]], else null.
    ///
    /// - Fails: returns null as described.
    /// - Ownership: the view belongs to the manager.
    const asset::SkeletonView* FindSkeleton(asset::ResourceId id) const override
    {
        return manager_->Find<asset::SkeletonView>(id);
    }
    /// The clip with this id when it is loaded as a [[asset::ClipView]], else null.
    ///
    /// - Fails: returns null as described.
    /// - Ownership: the view belongs to the manager.
    const asset::ClipView* FindClip(asset::ResourceId id) const override { return manager_->Find<asset::ClipView>(id); }

private:
    const asset::ResourceManager* manager_;
};

}  // namespace vse::animation
