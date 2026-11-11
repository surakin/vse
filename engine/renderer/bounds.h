#pragma once

//! Bounding volumes used for culling. The types and functions moved to `math/geometry.h` (Phase 18 Day 238), shared
//! with the spatial module; the renderer keeps its names for them here, so no caller changes.

#include "math/geometry.h"

namespace engine_placeholder::renderer
{

using engine_placeholder::Aabb;
using engine_placeholder::BoundingSphere;
using engine_placeholder::Bounds;
using engine_placeholder::ComputeAabb;
using engine_placeholder::ComputeBounds;
using engine_placeholder::TransformBounds;

}  // namespace engine_placeholder::renderer
