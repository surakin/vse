# Phase 4 / Day 45 — RHI Design Spec & Build Skeleton

**Phase:** [Phase 4 — Vulkan RHI](../phase-04-vulkan-rhi.md)
**Depends on:** Phases 1-3 complete.

Pins down the RHI before any code exists: the design is the deliverable, plus empty-but-building CMake targets so every later day has somewhere to put code. Nothing GPU-specific runs today.

## Tasks

- [x] Write `docs/superpowers/specs/2026-11-04-rhi-design.md` (linked from `docs/README.md`) covering, each with a one-paragraph rationale: (a) object model — `Device`, `CommandList`, `Swapchain` as abstract classes, every GPU resource an opaque generation-checked handle owned by the device (no virtual resource objects); (b) error model — creation returns an invalid handle and logs on a `LogRhi` channel, programming errors are `ENGINE_ASSERT` (documented as `Aborts:`), device loss is a queryable state; (c) lifetime — destroy requests are deferred until the GPU has finished the frames that could use the resource; (d) synchronization — explicit resource-state transitions (`Barrier(resource, before, after)`), the portable common denominator of Vulkan/DX12/Metal, so Phase 5's render graph can insert them automatically; (e) binding model — `BindGroupLayout` + `BindGroup` + push constants (maps to Vulkan descriptor sets, DX12 root tables/constants, Metal argument buffers); (f) pipelines use dynamic rendering (attachment formats in the pipeline description, no render-pass objects in the RHI); (g) threading — command lists are recorded on any thread (one pool per jobs participant), submission is serialized on the device; (h) the Vulkan baseline.
- [x] Settle and record the Vulkan baseline: Vulkan 1.3 core (dynamic rendering, synchronization2, timeline semaphores). Note the Android implication (Phase 25: 1.1/1.2 devices need the KHR/EXT extension paths) as an explicit risk in the spec, not a silent assumption.
- [x] Settle and record the loader policy: no link-time dependency on libvulkan; `libvulkan.so.1` is opened with `os::LoadLibrary` (as Day 17 did) and every entry point resolved into the RHI's own function tables, so the same code path works on Android and behind a future static-link platform. Only the compile-time Vulkan headers (`vulkan_core.h`) are taken from the system (the "GPU driver entry points" boundary of `PLAN.md` assumption #1).
- [x] Settle and record the validation-layer policy (closing the open question in the phase file): `VK_LAYER_KHRONOS_validation` is a dev-time tool, enabled by `ENGINE_RHI_VALIDATION=1` (default on in Debug builds when the layer is installed), never required; all validation messages are routed to the log and counted so tests can assert zero.
- [x] Settle and record the shader policy: shaders are GLSL 4.50 sources compiled offline by `glslc` into SPIR-V, embedded as committed `*.spv.inc` word arrays (generated files, same pattern as the Unicode tables) and checked with `spirv-val` in the dev workflow. `glslc`/`spirv-val` are dev-time tools, never linked or shipped (revised on Day 58 from hand-written SPIR-V assembly; `PLAN.md` assumption #18).
- [x] Settle and record the GPU-test policy: GPU tests are tagged `[gpu]`, carry the CTest label `gpu`, and `SKIP` (not fail) when no Vulkan loader/device is present; `ENGINE_RHI_REQUIRE_GPU=1` turns a skip into a failure (used on the dev machine). Headless tests (device without a surface, offscreen rendering with readback) are preferred; window-bound tests are the minority.
- [x] Update `plans/phase-04-vulkan-rhi.md`: replace the "to be expanded" deliverables with the day index (Days 45-67) and the settled design decisions D1-D8 above; fix the "Notes / open questions".
- [x] Add `engine/rhi/CMakeLists.txt` (`rhi` static library: `rhi.cpp` defining `LogRhi`, null backend added Day 47; and `rhi_vulkan` from `vulkan/vulkan_backend.cpp`, a placeholder replaced on Day 49 — both built from one function in the single `engine/rhi` CMakeLists, plain and `_asan`, so no separate `engine/rhi/vulkan/CMakeLists.txt` exists), and `tests/rhi/CMakeLists.txt` (`rhi_tests`, Catch2, plain + `_asan` variants like `jobs_tests`), wired from the root `CMakeLists.txt`. Production flags on the libraries, none on the tests.
- [x] Add `rhi` to `docs/reference/modules.txt` (`header engine/rhi/*.h`, `strict thread-safety`, `strict ownership`, `strict failure`); the `rhi/vulkan` module (`engine/rhi/vulkan/*.h`, non-strict: backend internals) is added on Day 49 when its first header exists, because docgen rejects a header pattern that matches no files; `docs/guide/rhi.md` skeleton page linked from `docs/guide/README.md`; `LogRhi` channel declared.
- [x] Verify: a clean configure + build produces the empty targets; `ctest -L docs` still passes with the new modules (zero symbols is allowed).
- [x] **Documentation (CLAUDE.md "Public API is documented before a day is done"):** the design spec and the `docs/guide/rhi.md` skeleton (outline with `_TODO_` sections) exist and are linked; there are no public symbols yet.
- [ ] **End-of-day review (CLAUDE.md):** run `/code-review high` on the
  day's diff, fix/decline each finding with a reason, record it under a
  "Post-implementation code review" section here, and mention it in
  `STATUS.md`.
- [ ] Update `STATUS.md`; commit (code, tests, the regenerated `docs/reference/` and the guide page together).

**Done:** the design spec is committed and linked, the phase file lists all 23 days, the empty `rhi`/`rhi_vulkan`/`rhi_tests` targets build in plain and ASan flavors, and the docs checks pass.

## Result

- Design spec: `docs/superpowers/specs/2026-11-04-rhi-design.md` (D1-D8, loader, validation, shader and GPU-test policies, Android risk, layout, out-of-scope list).
- Targets `rhi`, `rhi_vulkan` (+ `_asan`), tests `rhi_tests`, `rhi_tests_asan` (3 cases, label `rhi` / `asan`); `LogRhi` + `RhiLog` in `engine/rhi/rhi_log.h`.
- Docs: module `rhi` registered (strict), baseline refreshed (`rhi 3/3`), `docs/guide/rhi.md` skeleton linked from the guide and docs index; `ctest -L docs` 4/4.

## Post-implementation code review

`/code-review high` found 6 issues:

- STATUS.md not updated — **fixed** (this commit).
- Task text claimed a `engine/rhi/vulkan/CMakeLists.txt` that does not exist — **fixed**: the task text now describes the actual layout.
- Smoke test depended on global mutable channel state and never exercised `RhiLogV` — **fixed**: tests now pin and restore the channel state, capture output through the write sink, and cover formatting, filtering and truncation.
- `rhi_vulkan` not linked by any test, so its wiring would first be exercised on Day 49 — **fixed**: both test executables link `rhi_vulkan`.
- `RhiLogV` duplicates `JobsLogV` — **declined for now**: a shared `LogFormatted` helper in foundation would also change the jobs module, which is out of scope for a skeleton day; revisit when a third module needs it.
- Use of `vsnprintf` in the header — **declined**: libc is allowed (CLAUDE.md: "not because linking libc is banned outright"), the jobs module sets the precedent, and the helper is the single place to replace it.
- (Part of the TSan point) no `rhi_tsan` flavor yet — **declined**: nothing in `rhi` is multithreaded until Day 54, where the plan adds it.
