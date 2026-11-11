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
inline engine_placeholder::Aabb BoxAt(const engine_placeholder::Vec3& c, const engine_placeholder::Vec3& h)
{
    return engine_placeholder::Aabb::FromMinMax(c - h, c + h);
}

// Sorts keys ascending.
inline void SortKeys(engine_placeholder::Vector<engine_placeholder::spatial::SpatialKey>* keys)
{
    engine_placeholder::Sort(
        engine_placeholder::Span<engine_placeholder::spatial::SpatialKey>(keys->Data(), keys->Size()),
        [](engine_placeholder::spatial::SpatialKey a, engine_placeholder::spatial::SpatialKey b) { return a < b; });
}

}  // namespace spatial_test
