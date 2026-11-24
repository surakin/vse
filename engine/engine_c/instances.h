#pragma once

//! The instanced sets and trees a world holds for the C API: made, edited and destroyed by id, drawn by the surface
//! bound to the world.

#include "containers/array.h"
#include "containers/vector.h"
#include "engine/engine_c.h"
#include "math/quaternion.h"
#include "math/vector.h"
#include "renderer/instance_set.h"
#include "renderer/instance_tree.h"
#include "types.h"

namespace vse::engine_c
{

/// Where a set or a tree is, as the host gave it (checked).
///
/// - Thread-safety: thread-compatible plain data.
struct InstancePlacement
{
    Vec3d position = Vec3d(0, 0, 0);
    Quaternion rotation = Quaternion::Identity();
    f32 scale = 1.0f;
    f32 max_distance = 0.0f;
    f32 min_size_ratio = 0.0f;
    bool hidden = false;
};

/// A set of instances of one mesh a world holds.
///
/// - Thread-safety: thread-compatible; the world's mutex guards it.
/// - Ownership: shares the set with the render scene's object.
struct WorldInstanceSet
{
    u64 id = 0;
    u64 mesh = 0;  ///< The mesh resource id.
    renderer::InstanceSetPtr set;
    InstancePlacement placement;
};

/// A tree of instances a world holds.
///
/// - Thread-safety: thread-compatible; the world's mutex guards it.
/// - Ownership: shares the tree with the render scene's object.
struct WorldInstanceTree
{
    u64 id = 0;
    u32 mesh_count = 0;
    Array<u64, ENGINE_INSTANCE_MESHES_MAX> meshes = {};  ///< The mesh resource ids.
    renderer::InstanceTreePtr tree;
    InstancePlacement placement;
};

/// The sets and trees of one world.
///
/// - Thread-safety: thread-compatible; the world's mutex guards it.
struct WorldInstances
{
    u64 next_id = 1;
    Vector<WorldInstanceSet> sets;
    Vector<WorldInstanceTree> trees;
};

/// The most sets and trees one world holds.
inline constexpr u32 kMaxWorldInstanceObjects = 4096;

/// Checks and converts one instance as the C API reads it (finite, in range, a unit rotation, scales above 0).
///
/// - Param extra: receives the tint and the custom value; null to ignore them.
/// - Returns: ENGINE_OK, STRUCT_SIZE (`item_size`) or INVALID_ARGUMENT, with the failure text set.
engine_status ReadInstanceItem(const engine_instance* item, u32 item_size, renderer::InstanceTransform* transform,
                               renderer::InstanceExtra* extra);
/// The set `id` of a world, or null.
WorldInstanceSet* FindInstanceSet(WorldInstances& instances, u64 id);
/// The tree `id` of a world, or null.
WorldInstanceTree* FindInstanceTree(WorldInstances& instances, u64 id);

/// See `engine_api.instance_set_create`.
engine_status InstanceSetCreateFn(engine_handle world, const engine_instance_placement* placement, u64 mesh,
                                  u32 max_instances, u64* out_set);
/// See `engine_api.instance_set_update`.
engine_status InstanceSetUpdateFn(engine_handle world, u64 set, const engine_instance_placement* placement, u64 mesh);
/// See `engine_api.instance_set_add_many`.
engine_status InstanceSetAddManyFn(engine_handle world, u64 set, const engine_instance* items, u32 count, u32 item_size,
                                   u64* out_ids);
/// See `engine_api.instance_set_set_transform`.
engine_status InstanceSetSetTransformFn(engine_handle world, u64 set, u64 instance, const engine_instance* item,
                                        u32 item_size);
/// See `engine_api.instance_set_remove`.
engine_status InstanceSetRemoveFn(engine_handle world, u64 set, u64 instance);
/// See `engine_api.instance_set_get_transform`.
engine_status InstanceSetGetTransformFn(engine_handle world, u64 set, u64 instance, engine_instance* out,
                                        u32 item_size);
/// See `engine_api.instance_tree_create`.
engine_status InstanceTreeCreateFn(engine_handle world, const engine_instance_placement* placement, const u64* meshes,
                                   const f32* mesh_bounds, u32 mesh_count, const engine_tree_node* nodes,
                                   u32 node_count, u32 node_size, u64* out_tree);
/// See `engine_api.instance_tree_update`.
engine_status InstanceTreeUpdateFn(engine_handle world, u64 tree, const engine_instance_placement* placement);
/// See `engine_api.instance_tree_set_local`.
engine_status InstanceTreeSetLocalFn(engine_handle world, u64 tree, u32 node, const engine_instance* item,
                                     u32 item_size);
/// See `engine_api.instance_destroy`.
engine_status InstanceDestroyFn(engine_handle world, u64 id);

}  // namespace vse::engine_c
