#pragma once

//! The spatial module (Phase 18): bounding-volume hierarchies, spatial sets and the queries every system shares ("what
//! is near this box, ray, frustum"). Design: `docs/superpowers/specs/2026-11-10-spatial-design.md`; guide:
//! `docs/guide/spatial.md`. The shapes it works on are the foundation's (`math/geometry.h`), shared with the renderer.
//! Structures are generic over a 64-bit key and know nothing of the ECS; the ECS tier (components, the spatial world,
//! the sync system) comes later in the phase.

#include "math/geometry.h"
#include "types.h"

namespace vse::spatial
{

/// The most named spatial sets a world has (`render`, `physics`, `audio`, `gameplay`, ...); an entity joins sets
/// through a bitmask of this width.
inline constexpr u32 kMaxSpatialSets = 8;

/// The key a structure stores with each item: an entity id, an index, or anything else that fits in 64 bits. The
/// structures never interpret it.
using SpatialKey = u64;

}  // namespace vse::spatial
