# Instancing

An instanced mesh is many copies of one mesh and one material, each with its own place, held by one owner instead of one entity each. The pieces are in the renderer module; this page is the map between them.

## The instance set

`renderer::InstanceSet` (`renderer/instance_set.h`) holds the instances. It is made by `InstanceSet::Create` (or `InstanceSet::FromChunks`, which adopts whole chunks) and shared by a `SharedPtr` (`InstanceSetPtr`), so a component, a render scene entry and a loader can each hold it; a description (`InstanceSetDesc`) says whether every instance carries data, the most instances (at most `kMaxInstances`, 2^24, so an index is exact in an `f32`), the size of a Morton cell and how far an instance may move before it changes chunk.

An instance is a position (`f64`, in the world), a unit rotation and a scale on each axis (`InstanceTransform`). It is stored as a 40-byte `InstanceRecord`: the position as an `f32` offset from its chunk's `f64` origin, the rotation and the scale. A set made with data also keeps, parallel to the records, an `InstanceExtra` per instance: a colour tint and a `u32` custom value (20 bytes).

Instances live in chunks of up to 256 (`kInstanceChunkSize`). A chunk has an `InstanceChunkInfo`: its origin, the exact box of its instances' positions (offsets from the origin), the largest scale component, the count, the index of its first record (`chunk * 256`) and a version. The version is a number from one counter for the whole set, set on every edit of the chunk, so an upload copies exactly the chunks whose version differs from the one it saw. A culler inflates a chunk's box by the mesh's radius times `max_scale`.

Because the origin is an `f64` and the offsets are `f32`, a set placed 10,000 km from the world origin stores the same offsets as one at the origin; a chunk is exact as long as its instances are near one another (the box is in offsets, never in world coordinates).

## Edits

Every edit returns an `InstanceStatus` and never aborts on a bad value:

- `Add(transform, data, &id)` puts the instance in the chunk with the nearest Morton key (a new chunk when that one and its neighbour are full). `Full` at the set's capacity, `InvalidValue` for a value that is not finite, a coordinate beyond `kMaxInstanceCoordinate` or a scale not above 0.
- `Remove(id)` moves the chunk's last instance into the hole and refits the box; an empty chunk is released and reused. Other ids stay valid.
- `SetTransform(id, t)` keeps the instance in its chunk and grows (or, if it was on the box's edge, refits) the box, unless the new place is beyond the box by more than the move threshold: then it joins the chunk its new key selects.
- `SetData(id, data)` changes the tint and the custom value (`NoData` for a set without them).
- `Rebuild()` sorts everything by (Morton key, id) with a stable sort and cuts new full chunks; ids and contents are unchanged and the layout depends only on the content. `Clear()` removes everything.

An `InstanceId` is a table index and a generation: removing an instance, or clearing the set, makes its id `StaleId` for good, even when the table entry is reused.

## Culling

`CullInstances(set, params, scheduler, &result)` (`renderer/instance_cull.h`) finds the visible instances. A chunk whose box, grown by the mesh's radius times the chunk's largest scale, is outside the frustum is skipped; a chunk wholly inside is emitted as one run with no per-instance test; a chunk that straddles a plane tests each instance (a sphere at its place, the mesh radius times its largest scale). The result is a list of `InstanceRun`s (a chunk, the first slot, a count) in chunk and slot order. It equals testing every instance alone, as a set, and it is the same for any number of threads: groups of 32 chunks are culled in parallel and joined in chunk order.

`InstanceCullParams` carries the frustum, the viewer's place, the mesh radius and two optional limits: `max_distance` (0 for none) and `min_size_ratio`, the smallest radius-to-distance ratio worth drawing (`InstanceSizeRatio` converts a pixel size, the viewport height and the field of view). A set culled by size tests every instance of a chunk that is not rejected, because a chunk's smallest scale is not kept.

The frustum and the camera are in the space whose origin is `render_origin`, a world position in `f64` (the camera's place once the renderer is camera-relative; zero until then). `CompactInstances` writes the records of the runs relative to it, each position computed in `f64` and then narrowed, so a set 10,000 km from the world origin gives records as precise as one at the origin.

`InstanceCullStats` counts what the cull did: chunks tested, skipped, accepted whole and tested one by one, and the instances tested and emitted.

## Rays

`InstanceQuery` (`renderer/instance_query.h`) answers "which instance does this ray hit, by bounds". It keeps the set's chunk boxes in a `spatial::StaticBvh` (rebuilt by `Refresh` when the set's version has changed, a pass over the chunks) and tests the instances of the chunks the ray crosses by their own boxes.

## Cost


Edits never repack chunks: a chunk emptied by removals is reused, but one left with a few instances stays, and an instance moved far from every other (more than 64 key cells and the move threshold from a chunk's box) starts a chunk of its own, up to `InstanceSetDesc::max_chunks` chunks (default four times the chunks `max_instances` fill): past that `Add` is `Full`, and a move that finds no room stays in its old chunk. `Rebuild` keeps an offset as it is when the chunk's origin does not change, so rebuilding again moves nothing. Each chunk reserves room for 256 records, so call `Rebuild` after heavy editing of a scattered set to pack them again.

A record is 40 bytes; with the id table entry and the back reference a set costs about 60 bytes an instance without data and 80 with (`MemoryBytes()` reports the reserved bytes). Moving an instance within its chunk refits at most 256 records.

Measured with `render_features_bench` (a Release build with Clang on a 16-core, 32-thread CPU): a set of 1,000,000 instances over 4 km by 4 km holds 104.7 MiB (about 110 bytes an instance, the reserved chunk room included) and is made in 114 ms. Culling it for a camera that sees about a third of it takes 1.23 ms on one thread, 0.66 ms on 2, 0.52 ms on 4, 0.32 ms on 8 and 0.21 ms on 16; the visible list is the same at every thread count. A tree node is about 219 bytes (a node of 100,000 in 16,666 trees), so the demo's forest of 1,200,000 nodes holds about 250 MiB. Moving 1% of 100,000 nodes and recomposing the 4,002 nodes that depend on them takes 0.18 ms on one thread; the work is too small for more threads to help (0.13 to 0.31 ms at 2 to 16).

## Drawing a set

An `InstancedObject` (`renderer/render_scene.h`) puts a set in a `RenderScene`: the set, one mesh, one material and the set's own transform (a position, a rotation and a uniform scale; the instances are in the set's space, so moving it moves them all). `AddInstanced`, `SetInstanced`, `RemoveInstanced` and `FindInstanced` work like the object ones, with generation-checked `InstancedId`s, and `RenderSceneDesc::max_instanced` is the capacity. The scene shares the set and borrows the mesh; the set must not be edited while a frame is being prepared.

The material comes from `CreateGBufferMaterial` with `GBufferOptions::instanced`: it uses the `gbuffer_instanced` template, whose vertex shader (the `INSTANCED` permutation of `gbuffer.vert`) reads its instance from a storage buffer by the instance index and builds the position, the normal and the tangent from the instance's position, rotation and scale. The instance's tint multiplies the albedo exactly as a vertex colour does (sRGB-encoded); the custom value is passed to the shader but the standard material ignores it. The mesh must be rigid, in the standard layout, without vertex colours or morph targets; an instanced material cannot be skinned or use vertex colours either. Scales are always above 0, so a mirrored instance (and with it a winding flip) cannot occur.

`GBufferPass::Prepare` culls each set in the set's own space (the frustum planes and the camera are carried into it), composes every visible instance with the set's transform and writes it to the frame's instance buffer (`FrameResources::WriteInstances`, `kDefaultFrameInstances` = 262,144 instances a frame). A set is one draw with one pipeline bind; `GBufferPass::SetViewPosition` gives the camera for the distance limits (`InstancedObject::max_distance`, `min_size_ratio`), which `SceneFrame` sets for you. Instances that do not fit in the frame's buffer are not drawn and counted in `GBufferStats::instances_dropped`; the other counters (`instanced_sets`, `instances_visible`, the chunk counters) report what the cull did. A set with nothing visible adds no draw.

Instanced meshes are drawn on Vulkan; a Direct3D 12 device refuses them with a message until the Direct3D 12 shaders are minted, and a macOS device until the Metal ones are.


## Filling a set

`AddMany(set, transforms, data)` (`renderer/instance_scatter.h`) adds a list in order and reports how many went in and the first refusal; the list is all-or-nothing for the values it checks first, so a bad entry leaves the set as it was. `Scatter(set, rule, data, height_fn, user)` places a seeded, deterministic scatter on a rectangle: a count, the area, a scale range and a seed; with a height function (a plain function pointer and a user pointer, for example the terrain's height query) each instance stands on the ground. The same rule and seed give the same set on every machine.

## As a component

`scene::InstancedMesh` (`scene/components.h`) is a component naming the mesh, the set, the material and the draw limits. The instances are not entities. The entity's world transform (a translation, a rotation and a uniform positive scale) is the set's own transform, so moving the entity moves the whole forest without touching the set. `scene::MakeInstancedSyncSystem` mirrors the component into the render scene: it adds the set, updates the transform and flags when they change, hides it with `Hidden`, and removes it when the component or the entity goes; `SyncReport` counts what it did and what it refused as invalid.

## As an asset

An `INST` resource (`asset/instance_blob.h`) holds a set's chunks as minted bytes: a header, a chunk table, the records and, when the set has them, the tints and custom values. `asset_mint mint-instances SOURCE.inst.src OUTPUT.blob` writes one from a text list (an `id` line, optional `cell_size` and `move_threshold`, and one `instance` line per instance with a position, a rotation, a scale and optionally a tint and a custom value). The blob is a function of the list, so minting twice gives the same bytes. `asset::MakeInstanceSet` makes a set from an opened view by copying the records chunk by chunk with no per-instance `Add` (`AddCalls()` stays 0); a blob that holds a value the set refuses, or chunks out of order, is refused as corrupt.

## Hierarchies

`renderer::InstanceTree` (`renderer/instance_tree.h`) is a forest of instanced nodes (a tree with branches and leaves, a building with its parts) in one packed array, in depth-first order: the subtree of a node is the contiguous range `[index, index + subtree_size)` and a parent comes before its children. A node has a local transform, a mesh (an index into the up to 256 meshes the tree names, or none for a group), a material slot, a tint and custom value, its own box and the box of its subtree. A root has an `f64` world position; every other node has an `f32` offset from its parent, and every world transform and box is in the space of the node's root, so a tree 10,000 km away is as exact as one at the origin. World transforms compose by the rule of `Compose` in `math/transform.h` (scale, then rotation, then translation, no shear), the form an instance record holds.

`InstanceTreeBuilder` adds a root (`AddRoot`) or a child of a node added before (`AddChild`) and `Finish` packs them, children in the order they were added. Nodes get stable ids in that order; a parent that was not added yet, a value that is not finite, a scale not above 0, a mesh or slot out of range, a tree deeper than `kMaxTreeDepth` (255) and more than `kMaxTreeNodes` (2^22) nodes are refused by `TreeStatus`, never by abort. A cycle cannot be asked for, because a parent must exist first; `Attach` refuses to put a node under itself or its own subtree (`Cycle`).

`SetLocal` changes a node's transform and marks it. `UpdateWorldTransforms` recomposes the marked nodes and their subtrees in one forward pass over each affected range, recomputes the boxes they change (the node's own, its subtree's and its ancestors'), and clears the marks; roots with marks run in parallel on the scheduler and the result is the same for any number of threads. The work is the marked ranges, not the tree: a range runs from the first marked node of a root to the end of the last marked subtree.

`Find` maps an id to its index, `Parent`, `SubtreeEnd` and `RootOf` walk the structure, and `WorldPosition` adds the root's `f64` position to a node's offset. `Attach` moves a subtree under a new parent as its last child, keeping its local transform; `Detach` makes a node a root that stays where it is in the world. Both move only the records between the old and the new place and repair the sizes, flags and bounds of the ancestors of both (`LastEditCost` reports the records touched), and keep ids stable. A coarser level must fit inside the node's own mesh box (the box the cull tests), or the builder refuses it (a stand-in replaces the subtree and is judged by the subtree's box, which the builder cannot check at the node); a `TreeLodState` remembers its tree (`InstanceTree::Identity`) and is reset, with a warning, when another tree is culled with it. `Validate` checks every invariant and is what the tests run after each edit.

An `ITRE` resource (`asset/instance_tree_blob.h`) holds a tree as minted bytes: the mesh boxes and the nodes with their parent indices, in the order given. `asset_mint mint-instance-tree SOURCE.itree.src OUTPUT.blob` writes one from text (an `id` line, `mesh` lines with a box each, and `node PARENT X Y Z QX QY QZ QW SX SY SZ MESH SLOT [R G B A CUSTOM]` lines, a parent being `-` or the index of an earlier node). `asset::MakeInstanceTree` rebuilds the tree through the builder, so a blob with a parent that is not earlier, a mesh out of range or a scale not above 0 is refused as corrupt.

## Culling a hierarchy

`CullInstanceTree` (`renderer/instance_tree_cull.h`) finds the visible nodes of a tree for one view. It walks the packed array: a node whose subtree box, grown by a small margin against rounding, is outside the frustum skips its whole subtree with one jump (`i += subtree_size`); a subtree wholly inside, with no level of detail below it, is emitted without another test; the rest is tested node by node, each node's own box against the frustum. Roots are culled in groups on the scheduler and joined in root order, so the result is the same for any number of threads. The counters in `TreeCullStats` (entries looked at, subtrees skipped and accepted, nodes emitted, stand-ins, runs) show that the work follows the visible nodes: a tree of a million nodes seen from one place looks at a few thousand entries.

A node can carry a `TreeLod` (`TreeNodeDesc::has_lod`): up to four meshes (its own and three coarser ones) with the distances beyond which each coarser one is used, a `cull_distance` beyond which the node's own mesh is not drawn (with `cull_children_beyond`, nor are the nodes below it), and a stand-in, a simplified mesh drawn at the node's place instead of the node and its whole subtree beyond `standin_distance` (a distant tree as one merged mesh, with no node of its subtree drawn a second time). Distances are from the camera to the node's world position, and every level of a node must fit inside the box of its level 0 mesh. With a `TreeLodState` (one per tree and camera) the `hysteresis` margin applies: a level, the cull and the stand-in change only once the distance has passed the boundary by that many metres, so a camera moving back and forth across a boundary changes the level once; without a state the plain rule is used (`TreeLevelForDistance`).

The result lists the emitted nodes grouped into `TreeDrawRun`s of one mesh, material slot and level (two counting sorts, a fixed order: mesh, level, slot, nodes in array order inside a run), so a draw is one instanced draw per run. `CompactTreeNodes` writes the instance records and data of those nodes relative to a render origin, each position summed in `f64` from the root and narrowed.

## Drawing a hierarchy

An `InstancedObject` of the render scene holds either a set or a tree. A tree object carries up to 16 meshes and materials (`tree_meshes`, `tree_materials`, indexed by a node's mesh slot), and every material must be built with `GBufferOptions::instanced`; `GBufferPass::Prepare` names the first one that is not. For each tree the pass carries the view's frustum and camera into the tree's space, culls it (`CullInstanceTree`, with a `TreeLodState` it keeps per object), writes the visible nodes' instance records with `CompactTreeNodes` composed with the object's own placement, and makes one instanced draw per run. `GBufferStats` counts `instance_trees`, `instance_tree_runs` and `instance_tree_nodes`.

## A hierarchy as a component

`scene::InstanceHierarchy` holds the tree, the meshes and the materials of an entity; `MakeHierarchySyncSystem` mirrors it into the render scene as `MakeInstancedSyncSystem` does for sets (hide with `Hidden`, replace on a change, remove with the component or the entity; a tree whose mesh slots are not all filled counts as `invalid`), and recomposes the marked subtrees each frame, counted in `SceneSyncReport::hierarchy_nodes_updated`. Moving the entity moves the whole tree through the placement without touching a node.

## Wind

`scene::MakeWindSystem` sways the nodes flagged `kTreeNodeWind`: each gets a rotation about x of `WindAngle(wind, id)`, a function of the `WindState` resource (time, amplitude, frequency) and the node's id alone, so the result is the same for any thread count. Only the flagged nodes are marked, so only their subtrees are recomposed. Register it before the hierarchy sync, and advance `WindState::time` yourself.

## From a host: the C API

A host makes sets and trees by id, with no engine pointers (`include/engine/engine_c.h`, "Instanced meshes"). `instance_set_create` takes a placement (`engine_instance_placement`: an exact position, a unit rotation, a uniform scale, the distance limits and the hidden flag), the mesh resource and the most instances; `instance_set_add_many` adds up to 65,536 `engine_instance`s a call from an array (the host passes the size of one, so a mismatch is `STRUCT_SIZE`), checks all of them before adding any and returns an id for each; `instance_set_set_transform`, `instance_set_get_transform` and `instance_set_remove` edit and read one by id. `instance_tree_create` takes the mesh resources and each mesh's box, and an array of `engine_tree_node`s in which a node's parent is an earlier node; the node at index `i` is addressed by `i` in `instance_tree_set_local`, which recomposes only that node's subtree at the next frame. `instance_set_update` and `instance_tree_update` move a set or a tree, `instance_destroy` removes either.

A surface bound to the world draws them with its default instanced material, each instance coloured by its tint; a set or tree whose mesh is not mounted draws nothing and counts in `engine_surface_state.entities_unresolved` until the mesh arrives. A world holds at most 4,096 sets and trees. The C++ layer (`engine.h`) wraps each call.

## From scripts

A script in a world with sets and trees changes them with four commands, all applied at the sync point in the fixed merge order (so the result is the same for any number of partitions and threads): `instances.add(set, position, rotation, scale, tint)`, `instances.set(set, id, position, rotation, scale)`, `instances.remove(set, id)` and `tree.set_local(tree, node, position, rotation, scale)`. The set, instance, tree and node ids are the numbers the host got from the C API (a script cannot learn the id of an instance it adds: the add is a command); a script that names an unknown set or tree, a value out of range or a rotation that is not a unit quaternion gets an error naming it, and an instance that was removed counts a failed command at the sync point. The reference is in `script-api.md`.
