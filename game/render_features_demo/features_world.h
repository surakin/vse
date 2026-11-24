#pragma once

// The render features demo's world, the part that needs no device: a 2048 x 2048 terrain (a procedural height map made
// from integers and `+ - * /` only, so every machine gets the same samples) with its height-field collider, a forest of
// instanced trees (an instance hierarchy: trunk, two branch levels, leaves, with levels of detail and a distant
// stand-in) scattered on it by ray casts, instanced rocks, a character that walks on the height field and bodies that
// roll on it. `--headless` runs only this part; the picture mode draws it (features_gpu.h).

#include "containers/array.h"
#include "containers/vector.h"
#include "jobs/scheduler.h"
#include "math/vector.h"
#include "physics/character.h"
#include "physics/height_field.h"
#include "physics/world.h"
#include "renderer/camera.h"
#include "renderer/instance_cull.h"
#include "renderer/instance_set.h"
#include "renderer/instance_tree.h"
#include "renderer/instance_tree_cull.h"
#include "renderer/mesh.h"
#include "types.h"

namespace render_features
{

using namespace vse;

/// What the world is made of.
struct WorldOptions
{
    u32 side = 2048;     ///< Height map samples across and down.
    u32 trees = 200000;  ///< Trees to place (each is a hierarchy of `kNodesPerTree` nodes).
    u32 rocks = 400;     ///< Instanced rocks.
    u32 bodies = 12;     ///< Spheres and boxes dropped on the terrain.
    u32 threads = 1;     ///< Threads of the job scheduler the world uses (0: no scheduler).
};

inline constexpr u32 kNodesPerTree = 6;
inline constexpr f32 kStepSeconds = 1.0f / 60.0f;
inline constexpr f32 kHeightRange = 90.0f;  // metres of the height map's sample range (0 to this)

// Mesh indices of the forest tree (`InstanceTree::Mesh`) and the geometry they are.
enum TreeMeshIndex : u32
{
    kMeshTrunk = 0,
    kMeshTrunkLow,
    kMeshBranch,
    kMeshTwig,
    kMeshLeaves,
    kMeshLeavesLow,
    kMeshStandin,
    kTreeMeshCount,
};

/// What the visible-instance list of one camera came to.
struct VisibleSummary
{
    u64 hash = 0;  ///< FNV-1a of the visible tree nodes (node, mesh, level, in order) and the rock runs.
    renderer::TreeCullStats tree;
    renderer::InstanceCullStats rocks;
};

class FeaturesWorld
{
public:
    FeaturesWorld() = default;
    FeaturesWorld(const FeaturesWorld&) = delete;
    FeaturesWorld& operator=(const FeaturesWorld&) = delete;
    ~FeaturesWorld();

    /// Makes the terrain, the physics world, the forest, the rocks, the character and the bodies.
    ///
    /// - Param scheduler: runs the physics step and the forest's transforms; null for the calling thread.
    /// - Returns: false when the height field or the forest was refused; `Error()` says why.
    bool Build(const WorldOptions& options, jobs::Scheduler* scheduler);

    /// Advances the simulation by one fixed step: the character's scripted walk, the physics step, the rolling bodies'
    /// nudges.
    void Step();

    /// The physics state hash, and whether the world is valid.
    u64 PhysicsHash(bool* valid) const;

    /// Culls the forest and the rocks for `camera` and hashes what is visible.
    VisibleSummary Visible(const renderer::Camera& camera, jobs::Scheduler* scheduler);

    /// The camera that follows the character, `aspect` wide.
    renderer::Camera ChaseCamera(f32 aspect) const;

    const char* Error() const { return error_; }
    u32 Side() const { return options_.side; }
    f32 HeightAt(f32 x, f32 z) const { return field_ ? field_->HeightAtMap(x, z) : 0.0f; }
    const Vector<u16>& Samples() const { return samples_; }
    const renderer::InstanceTreePtr& Forest() const { return forest_; }
    const renderer::InstanceSetPtr& Rocks() const { return rocks_; }
    const Array<renderer::MeshData, kTreeMeshCount>& TreeMeshes() const { return tree_meshes_; }
    const renderer::MeshData& RockMesh() const { return rock_mesh_; }
    physics::PhysicsWorld& Physics() { return world_; }
    const physics::PhysicsWorld& Physics() const { return world_; }
    Vec3 CharacterPosition() const;
    u32 TreesPlaced() const { return trees_placed_; }
    u32 StepCount() const { return steps_; }
    const Vector<physics::BodyId>& Bodies() const { return bodies_; }
    f32 BodyRadius() const { return 0.5f; }
    f32 CellSize() const { return 1.0f; }

private:
    bool Fail(const char* what);
    bool BuildForest(jobs::Scheduler* scheduler);
    const char* BuildMeshes();  // null, or why a mesh could not be made

    WorldOptions options_;
    const char* error_ = "";
    Vector<u16> samples_;
    physics::HeightFieldConstPtr field_;
    physics::PhysicsWorld world_;
    physics::BodyId floor_;
    physics::CharacterController character_;
    Vector<physics::BodyId> bodies_;
    Array<renderer::MeshData, kTreeMeshCount> tree_meshes_;
    renderer::MeshData rock_mesh_;
    renderer::InstanceTreePtr forest_;
    renderer::InstanceSetPtr rocks_;
    renderer::TreeLodState lod_state_;
    renderer::TreeCullResult tree_cull_;
    renderer::InstanceCullResult rock_cull_;
    u32 trees_placed_ = 0;
    u32 steps_ = 0;
};

/// The terrain's samples: rolling hills from lattice value noise at four scales, in integers and `+ - * /`.
Vector<u16> MakeTerrainSamples(u32 side);

}  // namespace render_features
