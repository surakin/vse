# Phase 4 / Day 68 — Renderer Design Spec & Build Skeleton

**Phase:** [Phase 4 — Renderer Core](../phase-04-renderer-core.md)
**Depends on:** Phase 3 (complete).

Write down the Phase 4 architecture before any code: the layers, the data flow of a frame, and the decisions already made, so later days implement a spec instead of inventing one.

## Tasks

- [ ] Write `docs/superpowers/specs/2026-11-04-renderer-design.md` (sections numbered like the RHI spec): R1 layering (`renderer` sits on the RHI only and never names Vulkan, enforced by the existing `rhi_boundary_check`, extended to cover `engine/renderer/`); R2 the frame data flow (scene list -> culling -> render graph build -> compile -> record on jobs -> submit); R3 the **deferred** pipeline (G-buffer pass, lighting pass, tonemap resolve; no MSAA, post-process AA is a later add-on); R4 render graph scope (generic, built only on the RHI, owns every barrier); R5 the material model (shader + parameter block + textures, one bind group per material, per-object data in one buffer indexed per draw); R6 conventions inherited from Phase 3 (right-handed Y-up, `PerspectiveVulkan`, CCW, column-major = std140); R7 error model (same as the RHI: asserts for programming errors, invalid handle/false + log for environment); R8 threading (graph compile single-threaded, pass recording parallel on the Phase 2 scheduler, submission serialized); R9 out of scope (transparency, shadows, IBL, skinning, post effects beyond tonemap, MSAA, bindless, indirect draws, async compute - each with its decision trigger).
- [ ] Record the G-buffer layout in the spec: albedo RGBA8_SRGB (rgb + occlusion in alpha), normals RG16_Float (octahedral) or RGBA16_Float (decide here and justify against the RHI format table), material params RGBA8_UNorm (roughness, metallic, emissive scale, flags), depth D32_Float; light accumulation RGBA16_Float; every format checked with `Device::FormatSupport` at startup. Position is reconstructed from depth + inverse view-projection (the depth texture is sampled in `DepthRead` state).
- [ ] State the Slang decision and what it constrains (R10): one `.slang` source per shader, offline `slangc`, reflection drives material layouts, committed `*.spv.inc` + reflection outputs so a checkout without `slangc` still builds, the clip-space Y rule from the Phase 3 portability review.
- [ ] Create the build skeleton: `engine/renderer/` with CMake targets `renderer` (+ `_asan`, `_tsan` like `rhi`), linking `rhi`, `jobs` and `foundation` only; `tests/renderer/` with `renderer_tests` (+ asan/tsan flavors, labels `renderer`, `asan`, `tsan`; `[gpu]` tests skip without a device); a `renderer::LogRenderer` channel; one smoke test per flavor.
- [ ] Add module `renderer` to `docs/reference/modules.txt` (strict rules like `rhi`), create `docs/guide/renderer.md` as a skeleton with the standard sections and link it from `docs/guide/README.md`.
- [ ] Extend `tools/rhi_boundary_check` so `engine/renderer/` is scanned and may not name Vulkan or include `rhi/vulkan/`; add a planted-violation fixture for it.
- [ ] Update `plans/phase-04-renderer-core.md`: the decisions section (Slang, deferred, generic graph, no RHI extras), the day index with links, and remove "day-by-day breakdown deferred".
- [ ] **Documentation (CLAUDE.md "Public API is documented before a day is done"):** the spec is linked from `docs/README.md`, the `renderer` module is in `modules.txt` and the guide skeleton is linked from the guide index. `docgen lint` stays at zero findings, `cmake --build build --target docs` output is committed, `ctest -L docs` passes.
- [ ] **End-of-day review (CLAUDE.md):** run `/code-review high` on the day's diff, fix/decline each finding with a reason, record it under a "Post-implementation code review" section here, and mention it in `STATUS.md`.
- [ ] Update `STATUS.md`; commit (code, tests, regenerated `docs/reference/` and the guide pages together).

**Done:** `renderer_tests` builds and passes in all three flavors, the boundary gate covers `engine/renderer/` and fails on a planted violation, and the spec records every decision above.
