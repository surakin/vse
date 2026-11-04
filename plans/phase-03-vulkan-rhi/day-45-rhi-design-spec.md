# Phase 3 / Day 45 — RHI Design Spec & Build Skeleton

**Phase:** [Phase 3 — Vulkan RHI](../phase-03-vulkan-rhi.md)
**Depends on:** Phases 1-2.5 complete.

Pins down the RHI before any code exists: the design is the deliverable, plus empty-but-building CMake targets so every later day has somewhere to put code. Nothing GPU-specific runs today.

## Tasks

- [ ] Write `docs/superpowers/specs/2026-11-05-rhi-design.md` (linked from `docs/README.md`) covering, each with a one-paragraph rationale: (a) object model — `Device`, `CommandList`, `Swapchain` as abstract classes, every GPU resource an opaque generation-checked handle owned by the device (no virtual resource objects); (b) error model — creation returns an invalid handle and logs on a `LogRhi` channel, programming errors are `ENGINE_ASSERT` (documented as `Aborts:`), device loss is a queryable state; (c) lifetime — destroy requests are deferred until the GPU has finished the frames that could use the resource; (d) synchronization — explicit resource-state transitions (`Barrier(resource, before, after)`), the portable common denominator of Vulkan/DX12/Metal, so Phase 4's render graph can insert them automatically; (e) binding model — `BindGroupLayout` + `BindGroup` + push constants (maps to Vulkan descriptor sets, DX12 root tables/constants, Metal argument buffers); (f) pipelines use dynamic rendering (attachment formats in the pipeline description, no render-pass objects in the RHI); (g) threading — command lists are recorded on any thread (one pool per jobs participant), submission is serialized on the device; (h) the Vulkan baseline.
- [ ] Settle and record the Vulkan baseline: Vulkan 1.3 core (dynamic rendering, synchronization2, timeline semaphores). Note the Android implication (Phase 14: 1.1/1.2 devices need the KHR/EXT extension paths) as an explicit risk in the spec, not a silent assumption.
- [ ] Settle and record the loader policy: no link-time dependency on libvulkan; `libvulkan.so.1` is opened with `os::LoadLibrary` (as Day 17 did) and every entry point resolved into the RHI's own function tables, so the same code path works on Android and behind a future static-link platform. Only the compile-time Vulkan headers (`vulkan_core.h`) are taken from the system (the "GPU driver entry points" boundary of `PLAN.md` assumption #1).
- [ ] Settle and record the validation-layer policy (closing the open question in the phase file): `VK_LAYER_KHRONOS_validation` is a dev-time tool, enabled by `ENGINE_RHI_VALIDATION=1` (default on in Debug builds when the layer is installed), never required; all validation messages are routed to the log and counted so tests can assert zero.
- [ ] Settle and record the shader policy: shaders are SPIR-V assembly text (`*.spvasm`) written by hand, assembled offline into committed `*.spv.inc` word arrays (generated files, same pattern as the Unicode tables), checked with `spirv-val` in the dev workflow. `spirv-as`/`spirv-val` are dev-time tools, never linked or shipped. No GLSL/HLSL compiler (assumption #4).
- [ ] Settle and record the GPU-test policy: GPU tests are tagged `[gpu]`, carry the CTest label `gpu`, and `SKIP` (not fail) when no Vulkan loader/device is present; `ENGINE_RHI_REQUIRE_GPU=1` turns a skip into a failure (used on the dev machine). Headless tests (device without a surface, offscreen rendering with readback) are preferred; window-bound tests are the minority.
- [ ] Update `plans/phase-03-vulkan-rhi.md`: replace the "to be expanded" deliverables with the day index (Days 45-67) and the settled design decisions D1-D8 above; fix the "Notes / open questions".
- [ ] Add `engine/rhi/CMakeLists.txt` (`rhi` static library: `rhi.cpp` placeholder, null backend added Day 47), `engine/rhi/vulkan/CMakeLists.txt` (`rhi_vulkan`), and `tests/rhi/CMakeLists.txt` (`rhi_tests`, Catch2, plain + `_asan` variants like `jobs_tests`), wired from the root `CMakeLists.txt`. Production flags on the libraries, none on the tests.
- [ ] Add `rhi` to `docs/reference/modules.txt` (`header engine/rhi/*.h`, `strict thread-safety`, `strict ownership`, `strict failure`) and `rhi/vulkan` (`engine/rhi/vulkan/*.h`, non-strict: backend internals); `docs/guide/rhi.md` skeleton page linked from `docs/guide/README.md`; `LogRhi` channel declared.
- [ ] Verify: a clean configure + build produces the empty targets; `ctest -L docs` still passes with the new modules (zero symbols is allowed).
- [ ] **Documentation (CLAUDE.md "Public API is documented before a day is done"):** the design spec and the `docs/guide/rhi.md` skeleton (outline with `_TODO_` sections) exist and are linked; there are no public symbols yet.
- [ ] **End-of-day review (CLAUDE.md):** run `/code-review high` on the
  day's diff, fix/decline each finding with a reason, record it under a
  "Post-implementation code review" section here, and mention it in
  `STATUS.md`.
- [ ] Update `STATUS.md`; commit (code, tests, the regenerated `docs/reference/` and the guide page together).

**Done:** the design spec is committed and linked, the phase file lists all 23 days, the empty `rhi`/`rhi_vulkan`/`rhi_tests` targets build in plain and ASan flavors, and the docs checks pass.
