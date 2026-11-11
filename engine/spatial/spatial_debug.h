#pragma once

//! Observing the spatial module (Phase 18 Day 248): statistics of a set, line segments for drawing its structure, and the settings that control the sync.
//! The module holds no renderer type: a program turns the lines into a line pass.

#include "containers/span.h"
#include "containers/vector.h"
#include "math/geometry.h"
#include "math/vector.h"
#include "settings/registry.h"
#include "spatial/spatial_set.h"
#include "spatial/spatial_world.h"
#include "types.h"

namespace engine_placeholder::spatial {

/// What a set holds and how good its structures are.
///
/// - Thread-safety: thread-compatible plain data.
struct SpatialStats {
    SpatialSetStats set;           ///< The set's own counts.
    u32 static_nodes = 0;          ///< Nodes of the static tree.
    u32 static_depth = 0;          ///< Depth of the static tree.
    u32 dynamic_nodes = 0;         ///< Nodes of the dynamic tree (0 for a grid).
    u32 dynamic_height = 0;        ///< Height of the dynamic tree.
    f32 dynamic_sah_cost = 0.0f;   ///< The dynamic tree's SAH cost now (the quality metric: grows as it degrades).
    f32 dynamic_built_sah_cost = 0.0f; ///< And right after its last full build.
    u64 moves = 0;                 ///< Moves of dynamic items.
    u64 reinserts = 0;             ///< Of those, the moves that re-inserted a leaf.
    u64 full_rebuilds = 0;         ///< Full rebuilds of the dynamic tree.
    u64 memory_bytes = 0;          ///< Bytes of the dynamic tree's nodes and slots.
};

/// Collects the statistics of one set.
///
/// - Param set: the set.
/// - Returns: the numbers.
/// - Fails: never.
SpatialStats DescribeSet(const SpatialSet& set);

/// A line segment of the debug drawing, with a packed 0xAABBGGRR color.
///
/// - Thread-safety: thread-compatible plain data.
struct DebugLine {
    Vec3 a;          ///< One end.
    Vec3 b;          ///< The other end.
    u32 color = 0;   ///< 0xAABBGGRR.
};

/// Colors of the debug drawing.
inline constexpr u32 kDebugNodeColor = 0xFF804020u;  ///< Tree node boxes.
inline constexpr u32 kDebugItemColor = 0xFFE0E0E0u;  ///< Item boxes.
inline constexpr u32 kDebugQueryColor = 0xFF2020FFu; ///< The query volume.
inline constexpr u32 kDebugHitColor = 0xFF20E0FFu;   ///< The items a query found.

/// What `DebugLines` draws.
///
/// - Thread-safety: thread-compatible plain data.
struct DebugOptions {
    bool nodes = true;               ///< The boxes of the tree nodes.
    bool items = true;               ///< The boxes of the items.
    u32 max_lines = 100000;          ///< Stops adding lines past this many (a drawing of a huge set would flood the line pass).
    bool has_query = false;          ///< Draw `query` too.
    Aabb query;                      ///< A query volume to show.
    Span<const Aabb> hits;           ///< Boxes of the items a query found, drawn in the hit color.
};

/// Appends the 12 edges of a box.
///
/// - Param box: the box.
/// - Param color: the color.
/// - Param out: receives the lines.
/// - Fails: never.
/// - Ownership: `out` is borrowed for the call.
void AppendBoxLines(const Aabb& box, u32 color, Vector<DebugLine>* out);

/// Produces line segments that show a set: the node boxes, the item boxes, a query volume and its hits.
///
/// - Param set: the set.
/// - Param options: what to draw.
/// - Param out: receives the lines (cleared first).
/// - Returns: how many lines were produced.
/// - Fails: stops at `options.max_lines`.
/// - Ownership: `out` is borrowed for the call.
usize DebugLines(const SpatialSet& set, const DebugOptions& options, Vector<DebugLine>* out);

/// Declares `spatial.parallel` (bool, default true), `spatial.fat_margin` (float, the dynamic trees' margin as a fraction of item size, default 0.25),
/// `spatial.stats` (bool, default false: time the phases of the sync) and `spatial.debug_draw` (bool, default false: for programs that draw the structure).
/// Each has an environment variable and a flag.
///
/// - Param registry: the registry to declare them in.
/// - Fails: a key already registered is left as it is.
/// - Ownership: borrows `registry` for the call; the descriptions are string literals.
void RegisterSpatialSettings(settings::Registry* registry);

/// The configuration of a set made from the settings (`spatial.fat_margin`).
///
/// - Param registry: the registry the settings were declared in.
/// - Returns: a configuration with the margin set; the other fields are the defaults.
/// - Fails: a missing setting leaves the default.
SpatialSetConfig SetConfigFromSettings(const settings::Registry& registry);

} // namespace engine_placeholder::spatial
