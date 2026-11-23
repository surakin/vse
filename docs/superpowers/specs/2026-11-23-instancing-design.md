# Instanced and hierarchical instanced meshes: design

Date: 2026-11-23 (Phase 28, Day 494). Decisions of the user (2026-11-23): culling and draws start on the CPU (the data is shaped so a GPU-driven
path can follow); the instance hierarchy is its own packed structure, not ECS entities (the entities that own an instanced mesh hold a handle to
it); the order is flat instanced meshes, then hierarchical ones. The spikes below were timed with throwaway code (a Release build, one core of a
Ryzen 9 7950X3D, the null device); the code is not kept, the method is in each section.

## What exists today

- `RenderScene` stores one `RenderObject` per drawable (160 bytes: mesh pointer, material id, `Mat4` transform, bounds sphere and box, flags,
  skin and morph fields) and a BVH over their boxes. `Cull` returns slots in increasing order.
- `GBufferPass::Prepare` culls, sorts the visible objects by (pipeline, material, mesh), merges a run of the same mesh, material and range into
  one draw with `instance_count > 1` and writes one `ObjectData` per object (two `Mat4`, 128 bytes: the world matrix and the normal matrix) into
  a per-frame storage buffer; draw `i` starts at `first_instance = i`.
- Every drawable is an entity with a mesh component, synced into the scene each frame.

What is missing: instances that are not entities (a forest of a million trees cannot be a million entities with a component each), culling by
chunk instead of by object, and a tree of instances.

## Spike 1: the baseline (100,000 objects, one mesh and material)

Objects spread over 400 m by 400 m, the camera above them.

| | 25,621 visible | all 100,000 visible |
|---|---|---|
| `AddObject` (with the BVH insert) | 2.35 us each, 235 ms for 100,000 | the same |
| `RenderScene::Cull` | 4.1 ms | 24.1 ms |
| `FrameResources::WriteObjects` | 0.55 ms | 1.27 ms |
| `GBufferPass::Prepare` (cull, sort, merge, write) | 8.9 ms | 30.7 ms |

Memory: 160 bytes a `RenderObject` plus the BVH plus 128 bytes of `ObjectData` per frame in flight, so about 300 bytes an instance. One
`InstanceSet` of the same 100,000 instances must cost a small fraction of the 30 ms to be worth having; the target of the benchmark day (Day 504)
is the whole path below 3 ms for 100,000 visible and below 10 ms for 1,000,000 instances of which a tenth is visible. **Result: the entity path
cannot carry the scale the feature is for, and the cull (24 ms for 100,000) is the largest part.**

## Spike 2: the instance layout

| | Position, quaternion, scale (`Vec3`) | `Mat4x3` |
|---|---|---|
| Bytes | 40 (32 with one uniform scale) | 48 |
| CPU pack from a `Mat4`, 1,000,000 | 3.4 ms | 3.3 ms |
| Rebuilding the matrix from the quaternion, 1,000,000, on the CPU | 0.93 ms | none |
| Normal matrix | rebuilt with the world matrix | needs an inverse transpose or a second matrix |
| Bytes of a frame's upload, 1,000,000 visible | 40 MB | 46 MB |

`f32` spacing at a distance from the origin: 0.008 mm at 100 m, 0.06 mm at 1 km, 1 mm at 10 km, 7.8 mm at 100 km, 6 cm at 1,000 km.

**Pick: position (3 `f32`), rotation (a unit quaternion, 4 `f32`) and scale (3 `f32`): 40 bytes.** The CPU cost of both is the same; the 40-byte
record is 17% smaller, keeps rotation and scale separate (so the normal matrix is the rotation divided by the scale with no inverse), and a tint
and a custom value ride beside it in a second array. The vertex shader rebuilds the world and normal matrices from the record; its GPU cost is a
few multiplies a vertex and is **measured on the real device in the draw-path day (Day 497) against a `Mat4x3` record before that day is
closed**: the spike measured the CPU side only, and the number to beat is the CPU cost above. A set whose measurement says the vertex cost
matters switches the layout behind the same `InstanceSet` interface. Large worlds: positions are absolute `f32`, so the world is limited to
+-100 km (7.8 mm) and documented as such; keeping chunk-local offsets would not help a renderer that is not camera-relative (the record is
compacted into the frame's buffer in world space), and a camera-relative renderer is a separate feature, not part of this phase.

## Spike 3: chunking (1,000,000 instances in 4 km by 4 km by 40 m, a 600 m view)

Instances sorted by a 30-bit Morton key of their position (a three-pass radix sort of 1,000,000 records: 5.9 ms); chunks of consecutive
instances with a box each; a chunk the frustum rejects is skipped, a chunk wholly inside is copied, a chunk that straddles is tested instance by
instance.

| | per-instance test | chunks of 64 | 256 | 1,024 |
|---|---|---|---|---|
| Build the chunk boxes | none | 1.3 ms | 0.8 ms | 0.7 ms |
| Cull, 21,265 visible | 2.33 ms | 0.24 ms | 0.26 ms | 0.38 ms |
| Chunks, of which straddling | | 15,625, 691 | 3,907, 286 | 977, 121 |
| Instances tested one by one | 1,000,000 | 44,224 | 73,216 | 123,904 |
| Move one instance (refit its chunk) | | 82 ns | 197 ns | 724 ns |

**Pick: 256 instances a chunk in Morton order.** It is within 10% of the best cull time, builds 40% faster than 64, has a quarter of the chunk
metadata, and a refit stays under 0.2 us. A chunk is a unit a job can take (so culling a million instances can split across the scheduler with a
fixed merge in chunk order). The instance count of a chunk may drop below 256 after removals (a removal swaps with the chunk's last element) and
fill again on adds; `Rebuild` re-sorts on (key, id) so the layout is a function of the content.

## Spike 4: the hierarchy structure (a forest of 100 trees, 90,101 nodes, 4 levels)

| | Packed depth-first array with `subtree_size` | Parent index, parents before children |
|---|---|---|
| World transforms of all 90,101 nodes | 0.79 ms (a stack of parent matrices) | 0.79 ms (`world[i] = world[parent[i]] * local[i]`) |
| Cull with a box per subtree, 24,139 nodes visited | 0.20 ms (a rejected subtree is skipped by `i += subtree_size`) | 0.26 ms (a flag per node from its parent's) |
| Update of one subtree | the range `[i, i + subtree_size)` with the stack seeded from the parent | the same range if the order is depth-first |
| Level of detail | a node can stop the descent (draw this node's mesh, skip its children) | needs the same jump |

**Pick: the depth-first array with `subtree_size` per node, plus a stored parent index.** The two cost the same to update (about 9 ns a node, so
a million nodes is 9 ms on one core and parallel by root subtrees on the scheduler), the jump is 25% faster to cull and is what a level of
detail needs (stop at a node and skip its subtree). The parent index (4 bytes) gives `Parent`, a one-subtree update that seeds the stack and an
`Attach` or `Detach` (a move of a range of the array, then the ids and `subtree_size` of the ancestors fixed). Node record: local transform (40
bytes), world transform (a `Mat4x3`, 48), bounds (a sphere and a box of the subtree, 40), parent, `subtree_size`, mesh id, material slot, flags:
about 160 bytes on the CPU, never uploaded whole: the visible nodes' world transforms are packed into 40-byte instance records for the draw.

## The data structures

- `renderer::InstanceSet`: instances in chunks of up to 256 in Morton order; each chunk has a box and a range; ids are generation-checked
  handles into an id map; optional second array of a `Vec4` tint and a `u32` custom value; at most 2^24 instances (the index fits a `f32`
  exactly if a shader needs it); edits are `Add`, `Remove` (swap with the chunk's last), `SetTransform` (refits, or moves between chunks when the
  box would grow past a threshold), `Rebuild`.
- `renderer::InstanceCuller` (Day 496): frustum culling by chunk (reject, accept whole, test instances), on the job scheduler by chunk ranges
  with a fixed merge in chunk order; the output is a compacted list of instance records ready to upload.
- A draw path (Day 497): one draw per (set, mesh, material) with the compacted records in a second per-frame storage buffer; a vertex shader
  permutation reads the record instead of the object buffer.
- `renderer::InstanceTree` and `InstanceTreeBuilder` (Days 499-500): the packed hierarchy, with subtree culling and levels of detail by node
  distance (a node lists meshes by distance range).
- Scene: an `InstancedMesh` component holds a handle to a set, `scene::` syncs the component to a set in the `RenderScene` (up to a configured
  number of sets), the set's transform is the entity's; the C API creates and edits sets by id and bytes.

## Draw path and the GPU-driven door

The CPU culls, compacts and uploads each frame, which is correct and testable and costs a pass over the visible records. The records, the chunk
boxes and the compaction output are plain arrays so a compute pass can take over the cull later (the chunk boxes are the input; the output is
the same compacted buffer plus an indirect draw argument). It becomes a day only if the benchmark (Day 504) shows the CPU path above its budget.

## Days 495-501

No change of scope. Choices the day files should read: Day 495 uses 256-instance chunks, a 40-byte record and a Morton key (the day file says
"the picked size"); Day 497 measures the vertex-rebuild cost on the real device against `Mat4x3` before it is closed; Day 499 uses the
depth-first array with `subtree_size` and a parent index; the world limit of +-100 km is stated in the guide.
