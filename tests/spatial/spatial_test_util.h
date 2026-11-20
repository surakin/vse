#pragma once

// Helpers shared by the spatial tests (one definition each: unity builds merge the test files).

#include "algorithm.h"
#include "containers/span.h"
#include "containers/vector.h"
#include "math/geometry.h"
#include "spatial/spatial.h"

namespace spatial_test
{

// The box with this center and these half extents.
inline vse::Aabb BoxAt(const vse::Vec3& c, const vse::Vec3& h)
{
    return vse::Aabb::FromMinMax(c - h, c + h);
}

// Sorts keys ascending.
inline void SortKeys(vse::Vector<vse::spatial::SpatialKey>* keys)
{
    vse::Sort(vse::Span<vse::spatial::SpatialKey>(keys->Data(), keys->Size()),
              [](vse::spatial::SpatialKey a, vse::spatial::SpatialKey b) { return a < b; });
}

}  // namespace spatial_test
