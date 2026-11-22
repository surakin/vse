#pragma once

//! Bounding volumes used for culling. The types and functions moved to `math/geometry.h`, shared
//! with the spatial module; the renderer keeps its names for them here, so no caller changes.

#include "math/geometry.h"

namespace vse::renderer
{

using vse::Aabb;
using vse::BoundingSphere;
using vse::Bounds;
using vse::ComputeAabb;
using vse::ComputeBounds;
using vse::TransformBounds;

}  // namespace vse::renderer
