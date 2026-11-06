# Phase 5 / Day 74 — The Material System

**Phase:** [Phase 5 — Renderer Core](../phase-05-renderer-core.md)
**Depends on:** Day 73.

A material is a shader permutation plus parameters and textures; the system owns bind groups and pipelines so the renderer never builds them per draw.

## Tasks

- [x] `MaterialTemplate` (minted shader permutation + reflection: parameter block layout, texture/sampler slots, render state: cull, depth, blend) and `Material` (an instance: parameter values, texture handles); creation validates values against the reflected block (names, types, sizes) and logs/returns invalid on mismatch.
- [x] Material bind group (set 1 by convention: set 0 is `FrameData`, set 2 the object buffer): one parameter uniform buffer + textures + samplers per material, rebuilt only when a texture changes; parameter writes go to a CPU shadow and are flushed once per frame in one batch.
- [x] Pipeline cache keyed by (template, permutation, target formats, vertex layout): lazily created, deduplicated, never created inside a frame's recording (a warm-up call creates what a scene needs); hit/miss counters exposed for tests.
- [x] Default textures (white, black, flat normal) created by the system; missing texture slots use them.
- [x] Tests: parameter offsets against the shader's std140 block, a material with a missing or ill-typed value is rejected, two materials of one template share one pipeline, changing a texture rebuilds only that bind group, destruction order with deferred release on the real device.
- [x] The draw sort key (pipeline, then material, then mesh) is defined and tested; it is what makes bind changes cheap in the G-buffer pass.
- [x] **Documentation (CLAUDE.md "Public API is documented before a day is done"):** Material types and the authoring rules (set/binding convention, parameter naming) are documented in headers and the guide. `docgen lint` stays at zero findings, `cmake --build build --target docs` output is committed, `ctest -L docs` passes.
- [x] **End-of-day review (CLAUDE.md):** run `/code-review high` on the day's diff, fix/decline each finding with a reason, record it under a "Post-implementation code review" section here, and mention it in `STATUS.md`.
- [x] Update `STATUS.md`; commit (code, tests, regenerated `docs/reference/` and the guide pages together).

## Result

- `engine/renderer/material.{h,cpp}`: `MaterialSystem` (templates, materials, default textures and sampler, bind groups, pipeline cache with hit/miss counters), `MakeDrawSortKey`. Set convention 0 frame / 1 material / 2 objects, checked against reflection.
- Variants: a template's fragment stage is a single module or a permuted shader; the set-1 layout, parameter block and slots are built per (template, key) when a material first asks for it. The permutation key applies to the fragment stage only; the vertex stage is one module (decision for now).
- Parameter buffer: one CpuToGpu buffer per material with three frame regions (the RHI has no dynamic offsets), three bind groups per material; a change is copied to the three regions over three flushes and then costs nothing. A texture or sampler change rebuilds that material's three groups at the next flush.
- Test shaders `material_test.vert/.frag` (+ `.perm` with option TINT). Finding: Slang's `SV_InstanceID` subtracts the base instance, so the per-object index must come from `SV_VulkanInstanceID`; recorded in `shaders/README.md` and as a deferred item for Phases 6-7 (`KNOWN-ISSUES.md`).
- Tests: `tests/renderer/material_test.cpp`, 14 cases: std140 offsets and a column-major matrix readback, rejections (types, names, conventions, corrupt blob, unbuilt key, limits), shared pipelines and the key dimensions, vertex-layout mismatch, rebuild only for the changed material, flush counts, sort key, defaults, an empty set 1, release order on the null device (zero live objects), and a GPU draw of three objects through three materials read back pixel by pixel (proves first_instance indexing, parameters, permutation, defaults and the matrix layout). Sync validation clean.
- Docs: lint 1979+ at 100% (baseline ratcheted), guide "Materials" with a compiled sample, `docs/reference/` regenerated.

## Post-implementation code review

`/code-review high`: 9 findings.

- **Fixed:** `GpuMesh::Draw` lost its guard when relaxed for same-list prepare: now asserts `RecordPrepare` was called (`prepared_`) instead of "submitted".
- **Fixed:** set 1 with no resources was rejected: an unlit or depth-only shader is a valid material now (empty layout and group); test added.
- **Fixed:** a failed bind-group rebuild left the slots and the groups disagreeing: slots revert to what the groups show.
- **Fixed:** `FlushMaterials` walked every slot up to capacity: now up to the highest slot used.
- **Fixed:** default texture chosen only by name: templates can list explicit `texture_defaults`; `EffectiveTexture` added; the name rule stays as the fallback and is documented.
- **Fixed:** `MaterialSystemDesc` capacities are checked against the sort key's range at construction.
- **Fixed:** `SetMat4` layout was untested: the test shader has a `float4x4`, a unit test reads the bytes back and the GPU test draws with an asymmetric transform (scale plus translation) that a transposed matrix would get wrong.
- **Declined (recorded in KNOWN-ISSUES):** linear scan in `GetPipeline` — the renderer calls it per material per pass, not per draw, and real counts are unknown until Days 79-80; not measured.
- **Process finding:** STATUS.md and this file: done in this commit.
- Not tested: the rebuild-failure revert needs RHI fault injection (Day 77); filed.

**Done:** Materials and pipelines are created once, deduplicated and validated against reflection; a draw needs no per-draw object creation.
