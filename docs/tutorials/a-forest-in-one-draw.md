# Tutorial: A Forest in One Draw

**Goal:** Draw hundreds of thousands of trees without one entity, one draw call or one visibility test each, and understand what the work costs and why.

**You need:** nothing but the engine's libraries for the sample; `render_features_demo` draws 200,000 trees (1,200,000 nodes) and its overlay shows the counters this page explains. The guide: [Instancing](../guide/instancing.md) and [The render features demo](../guide/render-features-demo.md).

## The idea

A forest is the same few meshes over and over. The engine keeps the places, not the objects, and spends its time only on what can be seen:

- **An instance set** holds the places of copies of one mesh in *chunks* of up to 256 records, each chunk near each other in space and with a box around it. A cull tests the chunks first: one box test can accept 256 instances or reject them. What survives comes out as *runs* of consecutive instances, and the frame draws all of them with one instanced draw. A million rocks cost a few thousand box tests, not a million.
- **An instance hierarchy** is for things made of parts (a tree has a trunk, branches, twigs and leaves). It is one packed array in depth-first order, so the *subtree* of a node is a contiguous range. If the box of a subtree is outside the view, one jump (`i += subtree_size`) skips every part of the tree; if it is wholly inside and has no level of detail below, the whole range is emitted with no more tests. Moving a branch recomposes only its subtree.
- **Levels of detail** are per node: a coarser mesh beyond a distance, a *stand-in* (one simple mesh) drawn in place of a node and its subtree beyond another, and a distance beyond which nothing is drawn. A distant tree is one draw entry, not six.

The cost model follows: work is proportional to the nodes the walk looks at (the visible ones and the boxes that bound them), never to the forest. In `render_features_demo` a view of 200,000 trees looks at about 57,000 nodes.

## Steps

1. **Make a set.** `renderer::InstanceSet::Create` with a capacity, and `Add` (or `AddMany`, or `Scatter` for a seeded random placement over a rectangle, with a height function to follow the ground) to fill it. Positions are `f64`; each chunk stores offsets from its own origin, so a forest 10,000 km from the origin is as exact as one at it.
2. **Cull it.** `CullInstances` takes the frustum, the camera and the limits (`max_distance`, a minimum size) and returns runs and counters: chunks tested, skipped, accepted whole, and instances looked at one by one. Give it a scheduler and groups of chunks are tested in parallel and merged in chunk order: the result is the same at every thread count.
3. **Make a hierarchy.** `InstanceTreeBuilder` takes the list of meshes (their boxes) and nodes added under their parents: a root has an `f64` world position, every other node an `f32` offset, a rotation and a scale relative to its parent. `Finish` packs them and computes every world transform and box, in parallel by root.
4. **Give nodes levels of detail.** `TreeNodeDesc::lod` (`TreeLod`) lists coarser meshes and the distances at which they take over, the cull distance, a stand-in and a hysteresis (metres, so a node on a boundary does not flicker).
5. **Cull the hierarchy.** `CullInstanceTree` returns the visible nodes grouped into runs of one mesh, material slot and level, ready to draw: one draw per run. `TreeCullStats` tells you what it cost: nodes looked at, subtrees skipped and accepted whole, stand-ins.
6. **Move a part.** `InstanceTree::SetLocal` marks a node dirty; `UpdateWorldTransforms` recomposes only the marked subtrees (the sample moves one branch of 600). The scene sync does this for you for a hierarchy that is a component, and the wind sample sways nodes that way.
7. **Draw them.** Put the set or the tree in a `renderer::InstancedObject` (or give an entity the `InstancedMesh` or `InstanceHierarchy` component) with a material made with `GBufferOptions::instanced`; the node's tint colours it. The frame budget is 262,144 instances; what does not fit is counted in `instances_dropped`, never drawn wrongly.

```cpp compile standalone run
#include <cstdio>

#include "renderer/instance_cull.h"
#include "renderer/instance_scatter.h"
#include "renderer/instance_set.h"
#include "renderer/instance_tree.h"
#include "renderer/instance_tree_cull.h"
#include "renderer/camera.h"

using namespace vse;
using namespace vse::renderer;

int main()
{
    const Camera camera = MakeLookAtCamera(Vec3(0.0f, 5.0f, -50.0f), Vec3(0.0f, 5.0f, 0.0f), Vec3(0, 1, 0));
    const Frustum frustum = Frustum::FromViewProjection(camera.ViewProjection());

    // 1 and 2: 100,000 rocks over 1 km by 1 km, and the ones in view.
    InstanceSetDesc desc;
    desc.max_instances = 100000;
    InstanceSetPtr set = InstanceSet::Create(desc);
    ScatterRule rule;
    rule.seed = 7;
    rule.count = 100000;
    rule.min_x = rule.min_z = -500.0;
    rule.max_x = rule.max_z = 500.0;
    const AddManyResult added = Scatter(set.Get(), rule, nullptr, nullptr, nullptr);
    InstanceCullParams cull;
    cull.frustum = frustum;
    cull.camera_position = camera.position;
    InstanceCullResult visible;
    CullInstances(*set, cull, nullptr, &visible);
    std::printf("rocks: %u placed, %u visible in %u runs; %u of %u chunks skipped whole\n", added.added,
                visible.stats.instances_emitted, visible.stats.runs, visible.stats.chunks_skipped,
                visible.stats.chunks_tested);

    // 3 and 4: 100 trees of a trunk and five branches (mesh 0 and mesh 1), a stand-in beyond 150 m.
    TreeMeshInfo meshes[2];
    meshes[0].bounds = Aabb(Vec3(-0.3f, 0.0f, -0.3f), Vec3(0.3f, 4.0f, 0.3f));
    meshes[1].bounds = Aabb(Vec3(-0.1f, 0.0f, -0.1f), Vec3(0.1f, 1.5f, 0.1f));
    InstanceTreeBuilder builder(Span<const TreeMeshInfo>(meshes, 2));
    TreeNodeId first_branch;
    for (u32 t = 0; t < 100; ++t)
    {
        TreeNodeDesc trunk;
        trunk.position = Vec3d(static_cast<f64>(t % 10) * 20.0 - 100.0, 0.0, static_cast<f64>(t / 10) * 20.0);
        trunk.mesh = 0;
        trunk.has_lod = true;
        trunk.lod.standin_mesh = 1;
        trunk.lod.standin_distance = 150.0f;
        TreeNodeId id;
        builder.AddRoot(trunk, &id);
        for (u32 b = 0; b < 5; ++b)
        {
            TreeNodeDesc branch;
            branch.position = Vec3d(0.0, 1.0 + 0.6 * static_cast<f64>(b), 0.0);
            branch.mesh = 1;
            TreeNodeId bid;
            builder.AddChild(id, branch, &bid);
            if (t == 0 && b == 0)
            {
                first_branch = bid;
            }
        }
    }
    InstanceTreePtr tree = builder.Finish(nullptr);

    // 5: cull it from where the camera is, then from far away.
    TreeLodState state;
    TreeCullResult result;
    TreeCullParams params;
    params.frustum = frustum;
    params.camera_position = camera.position;
    CullInstanceTree(*tree, params, &state, nullptr, &result);
    std::printf("trees: %u nodes, %u emitted, %u looked at, %u subtrees skipped, %u subtrees taken whole\n",
                tree->NodeCount(), result.stats.nodes_emitted, result.stats.nodes_looked_at,
                result.stats.subtrees_skipped, result.stats.subtrees_accepted);
    const Camera far_camera = MakeLookAtCamera(Vec3(0.0f, 5.0f, -300.0f), Vec3(0.0f, 5.0f, 0.0f), Vec3(0, 1, 0));
    params.frustum = Frustum::FromViewProjection(far_camera.ViewProjection());
    params.camera_position = far_camera.position;
    CullInstanceTree(*tree, params, &state, nullptr, &result);
    std::printf("far away: %u emitted, %u of them stand-ins\n", result.stats.nodes_emitted, result.stats.standins);

    // 6: move one branch; only its own subtree is recomposed.
    tree->SetLocal(first_branch, Vec3d(0.2, 1.0, 0.0), Quaternion::Identity(), Vec3(1.0f, 1.0f, 1.0f));
    std::printf("moved a branch: %u of %u nodes recomposed\n", tree->UpdateWorldTransforms(nullptr), tree->NodeCount());
    return 0;
}
```

```output
rocks: 100000 placed, 30730 visible in 4265 runs; 304 of 583 chunks skipped whole
trees: 600 nodes, 281 emitted, 290 looked at, 62 subtrees skipped, 190 subtrees taken whole
far away: 100 emitted, 100 of them stand-ins
moved a branch: 1 of 600 nodes recomposed
```

## Where to go next

- [Making a Landscape](making-a-landscape.md): the ground the trees stand on, and the ray casts that place them.
- [The render features demo](../guide/render-features-demo.md): all of it together, with the measured costs.
