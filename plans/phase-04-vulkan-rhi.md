# Phase 4 — Vulkan RHI (Linux backend)

**Status: complete (2026-11-04).** All 23 days (45-67) are done; the portability review, performance baseline and wrap-up are in [Day 67](phase-04-vulkan-rhi/day-67-portability-review-perf-wrapup.md). Phase 5 still needs its day files before it can start.

**Depends on:** Phase 1 (Linux platform layer), Phase 2 (job system, for
multithreaded command recording), Phase 3 (docs tooling: public API is
documented before a day is done).
**Goal:** the backend-agnostic Render Hardware Interface, with Vulkan as
its first implementation. Everything above this layer (renderer, ECS,
etc.) only ever calls the RHI, never Vulkan directly.
**Definition of done:** the RHI renders a lit, textured cube through
Vulkan on Linux (Wayland and X11); grepping the codebase outside
`/engine/rhi/vulkan` turns up zero direct Vulkan API calls (enforced by a
ctest gate, Day 66).

## Days

Each day has its own file of individually checkable sub-tasks, a docs task
(CLAUDE.md "documented before done"), an end-of-day `/code-review high`
task, and a concrete Done condition.

45. [RHI Design Spec & Build Skeleton](phase-04-vulkan-rhi/day-45-rhi-design-spec.md)
46. [Core Types: Handles, Formats, Descriptors](phase-04-vulkan-rhi/day-46-rhi-core-types.md)
47. [Device Interface, Handle Pool & Null Backend](phase-04-vulkan-rhi/day-47-rhi-interfaces-null-backend.md)
48. [Native Window Handle Seam (Platform Layer)](phase-04-vulkan-rhi/day-48-native-window-seam.md)
49. [Vulkan Loader & Function Tables](phase-04-vulkan-rhi/day-49-vulkan-loader.md)
50. [Instance, Validation Layer & Debug Messenger](phase-04-vulkan-rhi/day-50-vulkan-instance-validation.md)
51. [Physical Device Selection, Queues & Logical Device](phase-04-vulkan-rhi/day-51-vulkan-physical-device-logical-device.md)
52. [GPU Memory Allocator](phase-04-vulkan-rhi/day-52-vulkan-memory-allocator.md)
53. [Buffers, Staging & Upload Path](phase-04-vulkan-rhi/day-53-vulkan-buffers-staging-upload.md)
54. [Command Lists, Submission & Synchronization](phase-04-vulkan-rhi/day-54-vulkan-command-lists-sync.md)
55. [Textures, Samplers & Layout Transitions](phase-04-vulkan-rhi/day-55-vulkan-textures-samplers.md)
56. [Swapchain & Window Presentation](phase-04-vulkan-rhi/day-56-vulkan-swapchain.md)
57. [Frame Loop, Frames in Flight & the Clear-Color Window](phase-04-vulkan-rhi/day-57-frame-loop-clear-milestone.md)
58. [SPIR-V Shader Workflow & Shader Modules](phase-04-vulkan-rhi/day-58-spirv-shader-pipeline.md)
59. [Graphics Pipelines, Layouts & Pipeline Cache](phase-04-vulkan-rhi/day-59-vulkan-pipelines.md)
60. [Bind Groups, Descriptors & Push Constants](phase-04-vulkan-rhi/day-60-bind-groups-push-constants.md)
61. [Triangle Milestone & Offscreen Golden-Image Tests](phase-04-vulkan-rhi/day-61-triangle-offscreen-golden.md)
62. [Compute Pipelines & Dispatch](phase-04-vulkan-rhi/day-62-compute-pipelines.md)
63. [Depth Buffer, Indexed Draws & the Spinning Cube](phase-04-vulkan-rhi/day-63-depth-indexed-cube.md)
64. [Textured Cube](phase-04-vulkan-rhi/day-64-textured-cube.md)
65. [Lit Cube — the Phase 4 Milestone](phase-04-vulkan-rhi/day-65-lit-cube-milestone.md)
66. [Hardening, Sanitizers & the Vulkan Boundary Gate](phase-04-vulkan-rhi/day-66-hardening-boundary-gate.md)
67. [Portability Review, Performance Baseline & Phase Wrap-up](phase-04-vulkan-rhi/day-67-portability-review-perf-wrapup.md)

## WSI surface creation (no longer an open risk)

`VK_KHR_wayland_surface`/`VK_KHR_xlib_surface` take `struct wl_display*`/
`struct wl_surface*` or `Display*`/`Window` — types the Phase 1 platform
layer already owns (root `PLAN.md` assumption #3). Day 48 adds the one
missing piece: a portable `Window::GetNativeInfo()`, so the RHI never
includes Wayland/X11 headers.

## Settled design decisions (recorded in the Day 45 spec)

- **D1 Object model:** abstract `Device`/`CommandList`/`Swapchain`; all GPU
  resources are opaque generation-checked handles owned by the device.
- **D2 Errors:** creation failure → invalid handle + log; programming
  errors → `ENGINE_ASSERT`; device loss is a sticky queryable state.
- **D3 Lifetime:** destruction is deferred until the GPU finished the
  frames that could use the resource.
- **D4 Synchronization:** explicit `ResourceState` transitions
  (`Barrier(resource, before, after)`) — the common denominator of
  Vulkan/DX12/Metal, so Phase 5's render graph can automate them.
- **D5 Binding:** bind group layouts + bind groups + push constants.
- **D6 Rendering:** dynamic rendering; no render-pass objects in the RHI.
- **D7 Threading:** command lists recorded on any thread (pool per jobs
  participant); submission serialized on the device.
- **D8 Baseline:** Vulkan 1.3 core; loaded at runtime from
  `libvulkan.so.1` via `os::LoadLibrary`, no link-time dependency.
- **Validation layers:** dev-time only, `ENGINE_RHI_VALIDATION`, never
  required; messages are counted and any unexpected one fails a test.
- **Shaders:** GLSL 4.50 sources (`*.vert`/`*.frag`/`*.comp`) compiled
  offline by `glslc` into SPIR-V and embedded as committed `*.spv.inc`;
  `glslc`/`spirv-val` are optional dev tools, never shipped. The
  long-term toolchain is an open decision (`PLAN.md` assumption #18).
- **GPU tests:** label `gpu`, tag `[gpu]`, skip (not fail) without a
  device; `ENGINE_RHI_REQUIRE_GPU=1` makes a skip a failure. Offscreen
  golden-image tests with analytic pixel assertions are preferred over
  window-bound tests.

## Notes / open questions

- **Android risk (Phase 23):** the 1.3 baseline excludes many Android
  devices; the extension paths (KHR/EXT equivalents) are the plan, noted
  in the spec.
- Golden images differ by vendor at triangle edges; tests compare away
  from edges with documented tolerances (Day 61).
- Async compute / transfer-queue parallelism, MSAA, bindless descriptors
  and indirect draws are deliberately out of scope here; fields are
  reserved where cheap. Phase 5 decides what it needs.
- The dev machine has an NVIDIA ICD and the validation layer but no
  software rasterizer (lavapipe); CI without a GPU runs only the headless
  null-device tests.
