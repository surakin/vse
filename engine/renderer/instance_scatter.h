#pragma once

//! Filling an [[InstanceSet]] in bulk: a list of transforms in one call, and a seeded, deterministic scatter over a
//! rectangle, optionally following a height field (a terrain).
//!
//! The scatter draws from a fixed generator (SplitMix64) in a fixed order, so a seed, a rule and a height function give
//! the same instances on every machine and thread count.

#include "containers/span.h"
#include "math/vector.h"
#include "renderer/instance_set.h"
#include "types.h"

namespace vse::renderer
{

/// What [[AddMany]] did.
///
/// - Thread-safety: thread-compatible plain data.
struct AddManyResult
{
    u32 added = 0;                               ///< Instances added, from the front of the list.
    InstanceStatus status = InstanceStatus::Ok;  ///< `Ok`, or the status that stopped the call.
};

/// Adds a list of instances.
///
/// - Param set: the set; not null.
/// - Param transforms: the placements.
/// - Param data: the data of each instance (parallel to `transforms`), or empty for the default; ignored when the set
///   has none; when it is not empty it must have the size of `transforms`.
/// - Returns: how many were added and why it stopped. It stops at the first instance the set refuses (`Full` or
///   `InvalidValue`); the ones before it stay.
/// - Fails: reported through the result; a `data` of another size than `transforms` adds nothing (`InvalidValue`).
/// - Ownership: `set` is borrowed for the call.
AddManyResult AddMany(InstanceSet* set, Span<const InstanceTransform> transforms, Span<const InstanceExtra> data);

/// Where a height function says the surface is: it is called with the caller's pointer, the world x and z, and a place
/// to write the surface height at (x, z), and returns false when there is no surface there (the point is skipped).
///
using ScatterHeightFn = bool (*)(void* user, f64 x, f64 z, f64* height);

/// How a scatter places instances.
///
/// - Thread-safety: thread-compatible plain data.
struct ScatterRule
{
    u64 seed = 1;            ///< The generator's seed; the same seed gives the same instances.
    u32 count = 0;           ///< Points to try; each one that has a surface (or all, with no height function) is added.
    f64 min_x = 0.0;         ///< The rectangle's smallest x.
    f64 max_x = 0.0;         ///< Its largest x; not below `min_x`.
    f64 min_z = 0.0;         ///< Its smallest z.
    f64 max_z = 0.0;         ///< Its largest z; not below `min_z`.
    f64 height = 0.0;        ///< The y of every instance when there is no height function.
    f32 min_scale = 1.0f;    ///< The smallest uniform scale, above 0.
    f32 max_scale = 1.0f;    ///< The largest uniform scale, not below `min_scale`.
    bool random_yaw = true;  ///< Turn each instance by a random angle about y.
    f32 lift = 0.0f;         ///< Added to the surface height (a negative value sinks the instance).
};

/// Scatters instances over a rectangle, on a height field when one is given.
///
/// - Param set: the set; not null.
/// - Param rule: where and how; a rectangle with a negative size or a coordinate beyond `kMaxInstanceCoordinate`, a
/// `count` above `kMaxInstances`, a scale range that is not above 0 and ordered, or a
///   non-finite value places nothing.
/// - Param data: the data every scattered instance gets (a set with data), or null for the default.
/// - Param height_fn: the surface, or null for the flat `rule.height`.
/// - Param user: passed to `height_fn`.
/// - Returns: the instances added and why it stopped; points with no surface are skipped and not counted.
/// - Fails: reported through the result (`InvalidValue` for a bad rule, `Full` at the set's capacity).
/// - Ownership: `set` and `data` are borrowed for the call.
AddManyResult Scatter(InstanceSet* set, const ScatterRule& rule, const InstanceExtra* data, ScatterHeightFn height_fn,
                      void* user);

}  // namespace vse::renderer
