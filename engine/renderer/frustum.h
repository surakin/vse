#pragma once

//! The view frustum and the tests that decide whether an object can be seen. The types moved to `math/geometry.h`
//! (Phase 18 Day 238), shared with the spatial module; the renderer keeps its names for them here, so no caller
//! changes. The frustum is built from a matrix of
//! [[renderer::PerspectiveVulkan]] (depth 0..1).

#include "math/geometry.h"
#include "renderer/bounds.h"

namespace engine_placeholder::renderer
{

using engine_placeholder::Containment;
using engine_placeholder::Frustum;
using engine_placeholder::Plane;

}  // namespace engine_placeholder::renderer
