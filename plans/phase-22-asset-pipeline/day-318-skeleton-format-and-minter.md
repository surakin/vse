# Phase 22 / Day 318 — The Skeleton Format, Its Source Form & the Skeleton Minter

**Phase:** [Phase 22 — Asset Pipeline & Resource System](../phase-22-asset-pipeline.md)
**Depends on:** Days 314-317 (blob container, the vertex formats); Phase 5 (`math` types).

A skeleton is a resource of its own: the joint hierarchy, the bind pose and the inverse bind matrices that skinned meshes and animation clips both refer to by id. Added on 2026-11-13 (the user): skeletal assets are part of Phase 22, so Phase 30 (Unreal import) has a format to write into.

## Tasks

- [ ] **The type:** `asset::types::kSkeleton` (`SKEL`, version 1) is added to `resource_type.h` and to the built-in registry (`TypeRegistry`), with its `///` documentation and a registry test; `engine/asset` gains `skeleton.h/.cpp` (format, loader) and the minter lives in `tools/asset_mint`.
- [ ] **Skeleton blob v1 (type `SKEL`, laid out for the sampler and the palette upload, no load-time conversion):** sections for the joint table (per joint: parent index as `u16`, with parents strictly before children and 0xFFFF for a root; flags), the bind pose as local translation (`f32 x3`), rotation (unit quaternion `f32 x4`) and scale (`f32 x3`) in a structure-of-arrays layout the sampler reads, the inverse bind matrices as 3x4 row-major `f32` (48 bytes, 64-byte aligned section, the exact bytes the joint palette buffer takes), an optional names section (joint names, deterministic order) and a name hash table (`u32` hash of each joint name, for clip retargeting by name); a bound-sphere hint for the bind pose; every count bounded: at most 256 joints in v1 (the palette limit of Day 322; a skeleton above it is refused by the minter with a message naming the limit), names at most 64 bytes.
- [ ] **Identity of a skeleton:** a `skeleton hash` (128-bit, over the joint count, parents and names) stored in the blob header section; a clip and a mesh record it, so a clip made for another skeleton is refused when it is bound, never silently misapplied. Retargeting to a different skeleton is out of scope (filed in `KNOWN-ISSUES.md`).
- [ ] **Source form `.skel.src`:** line-oriented text: `id <hex>` first (written by `asset_mint adopt`, Day 332), then `joint NAME PARENT tx ty tz  qx qy qz qw  sx sy sz` records in parent-first order, optional `ibm NAME m00 .. m23` overrides; when absent the minter computes each inverse bind matrix from the bind pose (parent chain product, inverted, deterministic and in `f64` before rounding to `f32`); a parser with line-numbered errors (unknown parent, parent after child, duplicate name, non-unit or NaN quaternion, zero scale, more than 256 joints, cycle); no glTF or FBX reader.
- [ ] **`SkeletonMinter`:** source to blob, deterministic (same bytes for the same text), normalizes quaternions with a fixed algorithm and a hemisphere rule (w >= 0), writes the dependency section empty (a skeleton has no dependencies), validates fully so the runtime need not.
- [ ] **Loader `LoadSkeletonBlob(bytes) -> SkeletonView`** through `BlobReader` (current version only): bounds-checked once, then spans into the buffer (no copy), `Joint(i)` accessors, `FindJoint(name)` by hash then name, and `ModelFromLocal(local_pose, out_model)` is left to Day 321 (the evaluator). The resource manager on Day 328 owns the buffer for the lifetime of every view.
- [ ] **Tests:** a table of source files (valid, and each error with its line number); a 5-joint chain and a 20-joint humanoid-shaped skeleton mint and reload to the same joint tables; the computed inverse bind matrices multiply with the bind-pose model matrices to the identity within 1e-5; the 257th joint is refused; hostile blobs (parent index out of range or after the child, counts that overflow, name offsets past the end, a non-unit bind quaternion) refused; a v0 golden blob refused with `asset_mint remint` in the message and the tool's upgrade rewrites it byte-identical to a fresh mint; minting twice is byte-identical.
- [ ] **Zero-copy rule:** the inverse bind section is 64-byte aligned and its bytes are what `rhi::Buffer` upload takes; a test asserts the alignment and the stride (48), so Day 322 never converts them.
- [ ] **Runs (CLAUDE.md "Tests run by phase"):** `scripts/test-phase.sh --affected` plain; then the asan and tsan flavors when the day touched concurrency, lifetime or memory code; quote each run's final `ran X of Y tests (phases ...)` line in this file and never say "all tests pass" for a reduced run. `ctest -L docs` when a public header or a guide changed. Build clean (warnings are errors) with Clang and GCC, unity on and off. New code uses the engine's wrappers and types, never `std` (`std_usage_check`); file access only through `io::` (`file_access_check`); settings only through `settings::Registry` (`config_access_check`).
- [ ] **Three machines (CLAUDE.md "Three machines"):** shared code is portable and checked on the Windows PC and the Mac no later than Day 335; until then the day is "open until the other machines run it" in `KNOWN-ISSUES.md`.
- [ ] **Documentation (CLAUDE.md "Public API is documented before a day is done"):** public headers carry `///` comments with the contract labels (`docs/DOC-CONVENTIONS.md`); `docgen lint` stays at zero; `cmake --build build --target docs` output committed; new modules are added to `docs/reference/modules.txt`; the guide page `docs/guide/assets.md` is updated and its tagged samples compile and run.
- [ ] **Module rules (CLAUDE.md "Module dependencies follow ARCHITECTURE.md"):** a new module or edge updates `ARCHITECTURE.md`, `tools/module_deps_check/modules.txt` and the CMake targets (with `_asan`/`_tsan` flavors) in the same commit; no upward edge, no cycle; every new test file has its line in `tests/phases.txt` (phase 22).
- [ ] **Day review (CLAUDE.md, required):** `/code-review high` over the day's diff after the scoped tests; verify, fix (with a regression test that fails without the fix) or decline each finding with a reason; record the count and outcome under "Post-implementation code review" below and in `STATUS.md`.
- [ ] `STATUS.md` and `KNOWN-ISSUES.md` updated in the same commit; commit and push.

**Done:** a `.skel.src` file mints to a blob that loads as a zero-copy view with correct hierarchy, bind pose and inverse bind matrices, hostile blobs are refused, and the output is deterministic.

## Outcome

(written when the day is done)

## Post-implementation code review

(written when the day is done)
