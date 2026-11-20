#pragma once

#include "containers/vector.h"
#include "math/geometry.h"
#include "physics/contact.h"
#include "physics/world.h"
#include "spatial/spatial_set.h"
#include "types.h"

//! The query index (query and hierarchy indices series, Day 1): every collider's exact world box in a
//! `spatial::SpatialSet`, kept current by the world on every change, so the queries ask it for candidates instead of
//! testing every collider. Private to the physics module.
namespace vse::physics
{

// A collider's place in the world: the body's pose with the collider's offset and rotation. The queries and the index
// compute boxes from it with the same code, so a box the index holds equals the one a query computes, bit for bit.
inline Pose ColliderPose(const Body& body, const Collider& collider)
{
    Pose pose;
    pose.position = body.position + body.orientation.ToMat3() * collider.offset;
    pose.rotation = body.orientation * collider.rotation;
    return pose;
}

// A collider's exact world box (no margin, no sweep).
inline Aabb ColliderBounds(const Body& body, const Collider& collider)
{
    const Pose pose = ColliderPose(body, collider);
    return collider.shape.Bounds(pose.position, pose.rotation);
}

// What the index did, for the tests.
struct QueryIndexStats
{
    u32 indexed = 0;       // colliders in the set
    u32 unindexed = 0;     // colliders the set could not take (full, or a box it refuses): the queries then test all
    u64 refreshes = 0;     // Refresh calls that found the collider's box changed
    u64 reinsertions = 0;  // of those, the ones that changed the set's structure (a box left its fat box, a new
                           // collider)
};

// Every collider's exact world box, static colliders in the set's static tree and the others in its dynamic tree (fat
// boxes). The world calls Refresh after every change of a collider's body, shape or class, RefreshMoving at the end of
// a step, and Rebuild after a restore; so between changes every box the index holds is the collider's current exact
// box, and a node box contains it. A query's own box or ray test is monotone in the box (rounding is monotone), so a
// collider the test accepts is found under every node the same test accepts: the candidates are a superset of what the
// test accepts, and visiting them in slot order gives today's results bit for bit.
//
// - Thread-safety: thread-compatible. The world changes it; Collect is const and may run on any number of threads
//   between changes.
class QueryIndex
{
public:
    QueryIndex();

    // Brings one collider in line with the world: adds it, moves it, moves it across (its class changed) or, for a
    // collider that is gone, removes what the slot held.
    void Refresh(const PhysicsWorld& world, ColliderId id);
    // Refreshes the colliders of every body that is not static (after a step moved them), and rebuilds the static tree
    // when static colliders wait for it.
    void RefreshMoving(const PhysicsWorld& world);
    // Empties the index and adds every collider again (after a restore).
    void Rebuild(const PhysicsWorld& world);

    // Whether every collider is in the set; when not, the queries test every collider.
    bool Complete() const { return unindexed_ == 0; }

    // The slots of the colliders whose box `test` accepts, below a node box `test` accepts, in ascending slot order.
    template <class Test> void Collect(Test&& test, Vector<u32>* slots) const
    {
        slots->Clear();
        set_.Traverse([&](const Aabb& node)
                      { return test(node) ? spatial::NodeVisit::Descend : spatial::NodeVisit::Reject; },
                      [&](spatial::SetHandle, const spatial::Item& item)
                      {
                          if (test(item.box))
                          {
                              slots->Push(static_cast<u32>(item.key));
                          }
                          return true;
                      });
        SortSlots(slots);
    }

    // Whether the index holds `slot`'s collider of generation `generation` with exactly `box` (for the slow check).
    bool Holds(u32 slot, u32 generation, const Aabb& box) const;

    QueryIndexStats Stats() const;

private:
    struct Entry
    {
        u32 generation = 0;  // 0: the slot holds nothing
        spatial::SetHandle handle = spatial::kInvalidSetHandle;
        bool is_static = false;
        bool unindexed = false;  // the collider exists but the set did not take it
        bool asleep = false;     // the body slept at the last refresh (a sleeping body is not moved by a step)
    };

    void Drop(u32 slot);
    // Refresh for a live collider whose record and body the caller has; its entry exists.
    void Update(ColliderId id, const ColliderRecord& collider, const Body& owner);
    static void SortSlots(Vector<u32>* slots);

    spatial::SpatialSet set_;
    Vector<Entry> entries_;
    u32 unindexed_ = 0;
    u64 refreshes_ = 0;
    u64 reinsertions_ = 0;
};

}  // namespace vse::physics
