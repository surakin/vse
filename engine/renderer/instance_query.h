#pragma once

//! Which instance of a set does a ray hit, by bounds. The chunk boxes of the set are kept in a [[spatial::StaticBvh]]
//! (the spatial module's tree and ray query, not a private grid); the instances of the chunks the ray crosses are then
//! tested by their own bounding boxes (the mesh's radius times the instance's largest scale around its place).

#include "containers/vector.h"
#include "math/geometry.h"
#include "renderer/instance_set.h"
#include "spatial/static_bvh.h"
#include "types.h"

namespace vse::renderer
{

/// An instance a ray met.
///
/// - Thread-safety: thread-compatible plain data.
struct InstanceHit
{
    InstanceId id;  ///< The instance.
    u32 chunk = 0;  ///< Its chunk.
    u32 slot = 0;   ///< Its slot in the chunk.
    f64 t = 0.0;    ///< The ray parameter of the hit, in units of the direction's length; 0 when the ray starts inside.
};

/// Ray queries over one set.
///
/// - Thread-safety: thread-compatible; queries are safe in parallel while nobody calls `Refresh`.
/// - Ownership: owns its tree; the set is passed to each call and not kept.
class InstanceQuery
{
public:
    /// Brings the tree in step with the set: rebuilds it when the set has changed since the last call (or the radius
    /// has), which costs a pass over the chunks, not over the instances.
    ///
    /// - Param set: the set.
    /// - Param mesh_radius: the radius of a sphere holding the mesh at scale 1; finite, not below 0.
    /// - Returns: whether the tree was rebuilt.
    /// - Fails: a non-finite or negative radius is treated as 0.
    bool Refresh(const InstanceSet& set, f32 mesh_radius);

    /// Finds the nearest instance whose bounding box the ray meets.
    ///
    /// - Param set: the set `Refresh` last saw.
    /// - Param ray: the ray in world space.
    /// - Param max_t: how far along the ray to look.
    /// - Param hit: receives the hit.
    /// - Returns: whether an instance was hit.
    /// - Ownership: `*hit` is written on a hit only.
    /// - Fails: a ray that is not finite finds nothing.
    bool Raycast(const InstanceSet& set, const Rayd& ray, f64 max_t, InstanceHit* hit) const;

private:
    spatial::StaticBvh tree_;
    u64 version_ = 0;
    u32 chunks_ = 0;
    f32 radius_ = 0.0f;
    bool built_ = false;
};

}  // namespace vse::renderer
