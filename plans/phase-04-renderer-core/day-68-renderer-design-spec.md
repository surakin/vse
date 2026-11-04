# Phase 4 / Day 68 — Renderer Design Spec & Build Skeleton

**Phase:** [Phase 4 — Renderer Core](../phase-04-renderer-core.md)
**Depends on:** Phase 3 (complete).

Write down the Phase 4 architecture before any code: the layers, the data flow of a frame, and the decisions already made, so later days implement a spec instead of inventing one.

## Tasks

- [x] Write `docs/superpowers/specs/2026-11-04-renderer-design.md` (sections numbered like the RHI spec): R1 layering (`renderer` sits on the RHI only and never names Vulkan, enforced by the existing `rhi_boundary_check`, extended to cover `engine/renderer/`); R2 the frame data flow (scene list -> culling -> render graph build -> compile -> record on jobs -> submit); R3 the **deferred** pipeline (G-buffer pass, lighting pass, tonemap resolve; no MSAA, post-process AA is a later add-on); R4 render graph scope (generic, built only on the RHI, owns every barrier); R5 the material model (shader + parameter block + textures, one bind group per material, per-object data in one buffer indexed per draw); R6 conventions inherited from Phase 3 (right-handed Y-up, `PerspectiveVulkan`, CCW, column-major = std140); R7 error model (same as the RHI: asserts for programming errors, invalid handle/false + log for environment); R8 threading (graph compile single-threaded, pass recording parallel on the Phase 2 scheduler, submission serialized); R9 out of scope (transparency, shadows, IBL, skinning, post effects beyond tonemap, MSAA, bindless, indirect draws, async compute - each with its decision trigger).
- [x] Record the G-buffer layout in the spec: albedo RGBA8_SRGB (rgb + occlusion in alpha), normals RG16_Float (octahedral) or RGBA16_Float (decide here and justify against the RHI format table), material params RGBA8_UNorm (roughness, metallic, emissive scale, flags), depth D32_Float; light accumulation RGBA16_Float; every format checked with `Device::FormatSupport` at startup. Position is reconstructed from depth + inverse view-projection (the depth texture is sampled in `DepthRead` state).
- [x] State the Slang decision and what it constrains (R10): one `.slang` source per shader, offline `slangc`, reflection drives material layouts, committed `*.spv.inc` + reflection outputs so a checkout without `slangc` still builds, the clip-space Y rule from the Phase 3 portability review.
- [x] Create the build skeleton: `engine/renderer/` with CMake targets `renderer` (+ `_asan`, `_tsan` like `rhi`), linking `rhi`, `jobs` and `foundation` only; `tests/renderer/` with `renderer_tests` (+ asan/tsan flavors, labels `renderer`, `asan`, `tsan`; `[gpu]` tests skip without a device); logging through `RenderLog` on the existing `LogRender` channel (so `--log-level` applies); one smoke test per flavor.
- [x] Add module `renderer` to `docs/reference/modules.txt` (strict rules like `rhi`), create `docs/guide/renderer.md` as a skeleton with the standard sections and link it from `docs/guide/README.md`.
- [x] Extend `tools/rhi_boundary_check` so `engine/renderer/` is scanned and may not name Vulkan or include `rhi/vulkan/`; add a planted-violation fixture for it.
- [x] Update `plans/phase-04-renderer-core.md`: the decisions section (Slang, deferred, generic graph, no RHI extras), the day index with links, and remove "day-by-day breakdown deferred".
- [x] **Documentation (CLAUDE.md "Public API is documented before a day is done"):** the spec is linked from `docs/README.md`, the `renderer` module is in `modules.txt` and the guide skeleton is linked from the guide index. `docgen lint` stays at zero findings, `cmake --build build --target docs` output is committed, `ctest -L docs` passes.
- [x] **End-of-day review (CLAUDE.md):** run `/code-review high` on the day's diff, fix/decline each finding with a reason, record it under a "Post-implementation code review" section here, and mention it in `STATUS.md`.
- [x] Update `STATUS.md`; commit (code, tests, regenerated `docs/reference/` and the guide pages together).

**Done:** `renderer_tests` builds and passes in all three flavors, the boundary gate covers `engine/renderer/` and fails on a planted violation, and the spec records every decision above.

## Result

- **Spec:** `docs/superpowers/specs/2026-11-04-renderer-design.md`, R1-R11: layering, the frame, deferred shading without MSAA, the generic render graph, the material model (sets 0/1/2, no per-draw bind groups), inherited conventions, error model, threading, out-of-scope table with triggers, the Slang constraints, and the G-buffer/lighting formats (albedo `RGBA8_SRGB`, normal `RG16_Float` octahedral, material `RGBA8_UNorm`, depth `D32_Float`, light accumulation `RGBA16_Float`; GGX specular in a metallic-roughness workflow).
- **Skeleton:** `engine/renderer/` (`renderer`, `renderer_asan`, `renderer_tsan`; links `rhi`, `jobs`, `foundation` only) with `RenderLog` on the foundation's `LogRender` channel and `RendererName()`; `tests/renderer/` with `renderer_tests` (+ asan/tsan; header self-containment unit generated per header); module `renderer` in `modules.txt` (strict rules); `docs/guide/renderer.md` skeleton; docs 1694/1694, lint at zero.
- **Boundary gate:** also scans `tests/renderer/`; planted fixtures for a Vulkan include in `engine/renderer/` and an identifier in `tests/renderer/` (the gate now expects seven reports).
- **Plan:** `plans/phase-04-renderer-core.md` already carried the decisions and day index (written with the day files).
- Full suite 1283/1283 on a clean run. Observed once, not reproduced in 8 further runs: `A swapchain survives 200 resize requests ... on Wayland` failed in one full-suite run under load (the same load-sensitivity as the known flaky X11Window tests); if it recurs, capture its output and look at `Acquire` timeouts first.

## Post-implementation code review

`/code-review high` returned 6 findings.

Fixed: (1) The day file, `STATUS.md` and this section were not yet updated: done. (2) The renderer fixture added nothing renderer-specific and `tests/renderer` was not scanned: the gate now scans `tests/renderer/` too and the fixtures cover an include in `engine/renderer/` and an identifier in `tests/renderer/`. (3) A new `LogRenderer` channel duplicated the foundation's `LogRender` and `--log-level` would not have reached it: the renderer now logs on `LogRender` through `RenderLog`, and the spec says why. (4) The renderer tests link `rhi_vulkan` only for `rhi::CreateDevice`; commented in the CMake file, and the gate now stops tests naming Vulkan. (5) The smoke test depended on the global default verbosity: it now pins and restores the channel state with a guard.

Declined: (6) The lint baseline for `rhi`/`rhi-vulkan` moved (560 to 594, 341 to 408): the baseline had not been refreshed since Phase 3 grew those modules; it only ratchets upward in documented counts, the refresh is mechanical (`--update-baseline`) and splitting it into another commit would add nothing.
