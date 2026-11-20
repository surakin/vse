# glTF colliders / Day 2: the `static_collider` level component, the `MCOL` loader, one degenerate-triangle rule

**Series:** [glTF colliders](../gltf-colliders.md) (design sections 2 and 4)
**Depends on:** nothing in the series.

## Tasks

- [ ] **One rule for "a triangle with no area"** (`engine/asset/mesh_collider.{h,cpp}`): `bool MeshColliderTriangleHasArea(const
  Array<f32, 3>& a, const Array<f32, 3>& b, const Array<f32, 3>& c)`, the physics rule exactly (`TriangleMesh::Create`: the f32 cross
  product of `b - a` and `c - a`, squared length finite and above `1e-20f`). `BuildMeshColliderBlob` and `MeshColliderView::Open` use
  it in place of the f64 `1e-24` test, so an `MCOL` that opens always makes a physics mesh. Test (`tests/asset/mesh_collider_test.cpp`):
  a triangle of area about 1e-11 (cross squared 1e-22) is accepted by neither the builder nor physics; one well above passes both; a
  table of 1,000 random triangles gets the same answer from `MeshColliderTriangleHasArea` and from `physics::TriangleMesh::Create` on
  a one-triangle mesh (in `tests/physics`, which may link `asset` in the test only, or by copying the vertices: the test owner decides).
- [ ] **The loader** (`engine/asset/cpu_loaders.{h,cpp}`): `template <> struct ResourceTraits<MeshColliderView>` (type `MCOL`) and
  `LoadMeshCollider`, registered by `RegisterCpuLoaders`. Test: an `MCOL` added to a memory backend loads through the manager by id and
  by path; a blob of another type is `TypeMismatch`.
- [ ] **The level component** (`engine/asset/level.{h,cpp}`): `kStaticColliderId = 53` and
  ```cpp
  struct StaticCollider
  {
      StaticColliderShape shape = StaticColliderShape::Mesh;  // Mesh = 0, Box = 1, Sphere = 2, Capsule = 3 (stored values)
      TypedRef<MeshColliderView> mesh;                         // the MCOL for Mesh, null otherwise (in the reference table)
      Array<f32, 3> half_extents = {};                         // Box, unscaled
      f32 radius = 0.0f;                                       // Sphere, Capsule, unscaled
      f32 half_height = 0.0f;                                  // Capsule: the straight part, along Y, unscaled
      f32 friction = 0.5f;                                     // physics' default material values
      f32 restitution = 0.0f;
  };
  ```
  (use the typed-reference type the level's `MeshReference` uses; match the default friction and restitution to
  `physics::PhysicsMaterial`'s). Registered by `RegisterLevelComponents`, version 1, with field validation where the registry offers it.
  The level's closure includes the `MCOL`s (the reference table), so `ResourceHost::ReadLevel`'s "every resource is mounted" check
  covers them.
  Tests: a level with a mesh and a box `static_collider` round-trips through `BuildLevelBlob`/`ParseLevelBlob`; its summary lists the
  `MCOL`; a level naming an `MCOL` that is not mounted is refused by the closure check; a record with a bad shape value is refused.
- [ ] **Docs**: `mesh_collider.h`, `cpu_loaders.h`, `level.h` (`///` with contract labels); `docs/guide/assets.md` ("Mesh colliders":
  the loader exists now, the one area rule; "Levels": the component); `KNOWN-ISSUES.md`: remove "a manager loader" from the `MCOL`
  "not built yet" entry.
- [ ] **Standing rows** (series rules).

**Done:** the area rule is one function both sides agree on; `MCOL` loads through the manager; `static_collider` is a stored level
component whose `MCOL` is part of the level's closure; tests pass in the scoped runs; docs and `docs/reference` regenerated.
