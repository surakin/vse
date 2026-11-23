#pragma once

//! Culling the instances of an [[InstanceSet]] against a view, by chunk first.
//!
//! A chunk whose box (grown by the mesh's radius times the chunk's largest scale) is outside the frustum is skipped;
//! one wholly inside is emitted as a single run without a per-instance test; one that straddles a plane tests each of
//! its instances (a sphere at the instance's place with the mesh's radius times the instance's largest scale). The
//! result is a list of runs of consecutive instances of a chunk, in chunk order and slot order, the same for any number
//! of threads and equal, as a set, to testing every instance on its own.
//!
//! Where the numbers are: the frustum and the camera are in the space whose origin is `render_origin` (a world
//! position, `f64`): the camera's place once the renderer is camera-relative, zero until then. [[CompactInstances]]
//! writes the visible records relative to it, so their precision is that of the offsets, not of the distance from the
//! world origin.

#include "containers/span.h"
#include "containers/vector.h"
#include "jobs/scheduler.h"
#include "math/geometry.h"
#include "math/vector.h"
#include "renderer/instance_set.h"
#include "types.h"

namespace vse::renderer
{

/// How a set is culled.
///
/// - Thread-safety: thread-compatible plain data.
struct InstanceCullParams
{
    /// The view volume, in the space whose origin is `render_origin`.
    Frustum frustum;
    /// The world position of that space's origin.
    Vec3d render_origin;
    /// The viewer's place in that space, for the distance tests.
    Vec3 camera_position;
    /// The radius of a sphere that holds the mesh at scale 1, in metres; finite, not below 0.
    f32 mesh_radius = 1.0f;
    /// Instances farther than this from the camera are not drawn; 0 means no limit.
    f32 max_distance = 0.0f;
    /// Instances whose radius is below this fraction of their distance are not drawn (the size on screen): see
    /// [[InstanceSizeRatio]]; 0 means no limit. A set culled by size has no whole-chunk fast path.
    f32 min_size_ratio = 0.0f;
};

/// Returns the `min_size_ratio` that hides instances smaller than `pixels` on a screen `viewport_height` pixels tall
/// with a vertical field of view of `fov_y` radians.
///
/// - Fails: never; a non-positive `viewport_height` or `fov_y` gives 0 (no limit).
f32 InstanceSizeRatio(f32 pixels, f32 viewport_height, f32 fov_y);

/// A run of consecutive instances of one chunk.
///
/// - Thread-safety: thread-compatible plain data.
struct InstanceRun
{
    u32 chunk = 0;  ///< The chunk.
    u32 first = 0;  ///< The slot of the run's first instance in the chunk.
    u32 count = 0;  ///< Instances in the run; at least 1.
};

/// What one cull did.
///
/// - Thread-safety: thread-compatible plain data.
struct InstanceCullStats
{
    u32 chunks_tested = 0;      ///< Non-empty chunks whose box was tested.
    u32 chunks_skipped = 0;     ///< Chunks rejected whole.
    u32 chunks_accepted = 0;    ///< Chunks emitted whole, with no per-instance test.
    u32 chunks_partial = 0;     ///< Chunks whose instances were tested one by one.
    u32 instances_tested = 0;   ///< Instances tested one by one.
    u32 instances_emitted = 0;  ///< Instances in the runs.
    u32 runs = 0;               ///< Runs in the result.
};

/// The visible runs of a set and what finding them cost.
///
/// - Thread-safety: thread-compatible plain data.
struct InstanceCullResult
{
    Vector<InstanceRun> runs;  ///< In chunk order, then slot order.
    InstanceCullStats stats;   ///< The counters.
};

/// Finds the visible instances of a set.
///
/// - Param set: the set; not edited during the call.
/// - Param params: the view and the limits.
/// - Param scheduler: runs groups of chunks in parallel and merges them in chunk order, or null for the calling thread.
///   Called from the main thread (participant 0), outside any job, like `jobs::ParallelForBlocking`.
/// - Param out: receives the runs and the counters; its storage is reused.
/// - Ownership: `out` is borrowed for the call.
/// - Thread-safety: thread-compatible; one `out` is for one call at a time.
/// - Fails: a non-finite `mesh_radius`, `max_distance` or `min_size_ratio`, or a negative one, finds nothing (an empty
///   result).
void CullInstances(const InstanceSet& set, const InstanceCullParams& params, jobs::Scheduler* scheduler,
                   InstanceCullResult* out);

/// Writes the records of the runs, in order, relative to `render_origin`: each position is the chunk's origin plus the
/// stored offset, minus `render_origin`, computed in `f64` and then narrowed.
///
/// - Param set: the set the runs were found in.
/// - Param runs: the runs.
/// - Param render_origin: the world position the records are relative to.
/// - Param out: receives the records (cleared first), `instances_emitted` of them.
/// - Ownership: `out` is borrowed for the call.
/// - Fails: never; a run outside the set's chunks is skipped.
void CompactInstances(const InstanceSet& set, Span<const InstanceRun> runs, const Vec3d& render_origin,
                      Vector<InstanceRecord>* out);

}  // namespace vse::renderer
