#pragma once

// Static colliders whose size depends on the world scale (glTF colliders Day 5): at the end of a frame, after the
// transforms were propagated, every `ColliderSource` still waiting becomes a `physics::Collider` scaled by its entity's
// world scale, a mesh one through the world's cache of physics meshes made from `MCOL`s.

#include "engine_c/resources.h"
#include "engine_c/runtime.h"
#include "types.h"

namespace engine_placeholder::engine_c
{

/// What one resolve did.
///
/// - Thread-safety: plain data.
struct ColliderResolveCounts
{
    u32 made = 0;           ///< Colliders made.
    u32 refused = 0;        ///< Sources that got none (a shear, a zero scale, an empty or missing mesh, a bad size).
    u32 scale_ignored = 0;  ///< Colliders whose entity's world scale changed after they were made (counted once each).
    u32 triangles_dropped = 0;  ///< Mesh triangles a scale made degenerate, left out.
};

/// Makes the waiting colliders of `object` and counts the scale changes of the made ones.
///
/// - Param object: the world, its transforms propagated this frame.
/// - Param resources: the engine's resource host (may be null: mesh sources are refused).
/// - Param counts: incremented.
/// - Fails: never; a source that cannot be made is counted and marked refused.
/// - Thread-safety: under the world mutex.
/// - Ownership: borrows its arguments for the call only.
void ResolveColliderSources(WorldObject& object, ResourceHost* resources, ColliderResolveCounts* counts);

}  // namespace engine_placeholder::engine_c
