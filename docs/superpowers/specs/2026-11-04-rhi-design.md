# RHI Design (Phase 3)

**Status:** settled for Phase 3 (Days 45-67); a living document — Day 67's
portability review may amend it.
**Scope:** the backend-agnostic Render Hardware Interface in `engine/rhi/`
and its first backend, Vulkan, in `engine/rhi/vulkan/`.
**Audience:** contributors. User-facing documentation is
`docs/guide/rhi.md` and the generated reference.

The RHI is the only layer allowed to talk to a GPU API. Renderer and
gameplay code call the RHI; the RHI calls a backend; only the backend
names Vulkan, Direct3D 12 or Metal. Day 66 enforces this with a ctest gate.

## 1. Object model (D1)

`Device`, `CommandList` and `Swapchain` are abstract classes (the one
virtual dispatch per command is acceptable: a command list call does tens
of nanoseconds of work, and a draw submission is far heavier than a
virtual call). Every GPU *resource* — buffer, texture, sampler, shader,
bind group layout, bind group, pipeline — is an **opaque, generation-
checked handle** (`Handle<Tag>`: 32-bit index + 32-bit generation, all-
zero is invalid) owned by the device. There are no virtual resource
objects.

*Why handles:* they cannot dangle silently (a stale handle fails a
generation check and the device reports it instead of dereferencing freed
memory); they are trivially copyable and cheap to put in render-graph and
ECS data; and they hide the backend type, so DX12/Metal need no interface
change. *Cost:* every call performs a pool lookup. Backends keep resources
in a dense slot array so this is an index and a compare.

## 2. Error model (D2)

Three classes of failure, each with one policy:

| Failure | Policy |
|---|---|
| **Programming error** (null/stale handle passed in, wrong state, API misuse, violated documented precondition) | `ENGINE_ASSERT`; documented as `Aborts:` on the function. The null device and the validation layers catch these during development. |
| **Environment problem** (no Vulkan loader, unsupported format, out of device memory, driver rejects a pipeline) | The creating call returns an **invalid handle** (or a failure status) and logs the reason on the `LogRhi` channel. Never an abort. Documented as `Fails:`. |
| **Device loss** | A sticky state, queried with `Device::DeviceLost()`. After loss, submits are no-ops that report failure. The loss is logged once with context. |

Resource creation never throws (no exceptions in the engine) and never
returns a half-built object.

## 3. Lifetime (D3)

`Destroy*(handle)` never frees immediately. The device records the
destruction against the current frame number and frees the resource only
when the GPU has finished every frame that could have used it
(`CollectRetired(completed_frame)`, driven by the frame fence). A stale
handle is invalid the moment `Destroy*` returns — the handle slot's
generation is bumped at once — even though the GPU memory lives longer.
Shutdown is `WaitIdle()` then collect everything; debug builds count live
backend objects per type and report any left at exit.

## 4. Synchronization (D4)

Synchronization is **explicit and in terms of resource states**, not
pipeline stages and access masks:

```
cmd.Barrier(resource, before, after)
```

`ResourceState` is the portable common denominator of Vulkan image
layouts + access masks, D3D12 resource states and Metal's implicit
tracking (undefined, vertex/index/uniform buffer, shader read/write, copy
source/destination, color/depth attachment, depth read, present). The
Vulkan backend maps each state to `(stage, access, layout)` in **one**
table (Day 54) and emits `VkImageMemoryBarrier2` / `VkBufferMemoryBarrier2`.
The RHI does not track states for the caller: the caller states `before`.
That keeps the RHI thin and lets Phase 4's render graph — which knows the
whole frame — insert and batch barriers automatically. `Undefined → X`
for images means "contents are discarded". The null device validates the
state machine so misuse is caught without a GPU.

CPU/GPU synchronization is a monotonically increasing **fence value** on a
device timeline (a Vulkan timeline semaphore; a D3D12 fence; a Metal
shared event). Binary semaphores exist only inside the swapchain.

## 5. Binding model (D5)

`BindGroupLayout` (the shape: binding index, type, stages, count),
`BindGroup` (a concrete set of resources matching a layout), and **push
constants** (a small inline range). A pipeline is created against an
ordered list of layouts; `SetBindGroup(slot, group)` binds one.

| RHI | Vulkan | D3D12 | Metal |
|---|---|---|---|
| bind group layout | `VkDescriptorSetLayout` | root-signature descriptor table | argument-buffer layout |
| bind group | `VkDescriptorSet` | descriptor table in a heap | argument buffer |
| push constants | push constants | root constants | `setBytes` |

Combined image-samplers are deliberately not exposed (D3D12 and Metal
separate textures and samplers). Bindless/descriptor indexing is out of
scope for Phase 3 (see `plans/phase-04-renderer-core.md`).

## 6. Rendering (D6)

Pipelines use **dynamic rendering**: the color/depth attachment formats go
in the pipeline description and `BeginRendering(RenderingDesc)` names the
attachments. There are no render-pass or framebuffer objects in the RHI.
This matches D3D12's render targets and Metal's render-pass descriptors
and removes the Vulkan render-pass compatibility rules from every layer
above the backend. Viewport and scissor are dynamic state.

## 7. Threading (D7)

- **Recording** is parallel: `CreateCommandList()` hands out a list from
  the calling thread's pool (one `VkCommandPool` per jobs participant per
  frame in flight), so recording takes no lock. A command list is
  `thread-compatible`: one thread records it.
- **Resource creation/destruction** is serialized behind one device mutex
  (creation is not a hot path; the mutex also protects the handle pools).
- **Submission** is serialized: `Submit` takes the submission mutex and
  there is one submission thread of record per queue. Documented as such
  on the function.
- Recording threads are normally jobs on the Phase 2 scheduler; the RHI
  itself does not start threads.

## 8. Vulkan baseline (D8) — and its risk

**Vulkan 1.3 core**: dynamic rendering, synchronization2 and timeline
semaphores are core features, so the backend has exactly one code path. A
device that lacks them is rejected at selection with the reasons logged.

> **Risk (Phase 14 — Android):** many Android devices ship Vulkan 1.1 or
> 1.2 drivers. Reaching them means mapping each of the three features to
> its `KHR`/`EXT` extension form (`VK_KHR_dynamic_rendering`,
> `VK_KHR_synchronization2`, `VK_KHR_timeline_semaphore`) in the backend.
> That is a backend-internal change — nothing above the RHI depends on the
> baseline — but it is real work, and it is recorded here so it is not a
> surprise. Phase 3 does not pre-build it.

## 9. Loader policy

The engine does **not** link `libvulkan`. The backend opens
`libvulkan.so.1` with `os::LoadLibrary` (the Phase 1 dlopen wrapper),
resolves `vkGetInstanceProcAddr`, then loads instance- and device-level
function tables of its own (`VkGlobalFns`, `VkInstanceFns`, `VkDeviceFns`).
Reasons: the same path works on Android and behind a future static-link
platform; the engine starts and reports "no Vulkan" cleanly on a machine
without a driver instead of failing to launch; and per-device function
tables skip the loader trampoline. Only the *compile-time* headers
(`vulkan_core.h` and the platform surface headers) come from the system —
the "GPU driver entry points" boundary of `PLAN.md` assumption #1.

## 10. Validation-layer policy

`VK_LAYER_KHRONOS_validation` is a **development tool**, never a
requirement, never shipped. It is enabled when `ENGINE_RHI_VALIDATION=1`,
and by default in Debug builds *when the layer is installed*; when it is
absent a single warning is logged and the run continues. Synchronization
validation is on in tests. Every message is routed to the log with its
severity and **counted**; tests assert the error count is zero through a
shared fixture, and `ENGINE_RHI_VALIDATION_FATAL=1` aborts on the first
error for interactive runs.

## 11. Shader policy

Shaders are **hand-written SPIR-V assembly** (`*.spvasm`, `PLAN.md`
assumption #4), assembled offline into committed `*.spv.inc` word arrays
(generated files, same pattern as the Unicode tables) and checked with
`spirv-val`. `spirv-as`/`spirv-val` are optional dev-time tools — the
committed arrays build without them — and are never linked or shipped.
There is no GLSL or HLSL compiler in Phase 3. The RHI performs its own
cheap SPIR-V sanity check (magic, version, entry point for the declared
stage) before a module reaches a driver.

## 12. GPU-test policy

- GPU tests carry the Catch2 tag `[gpu]` and the CTest label `gpu`.
- With no Vulkan loader or device they **skip**, not fail.
  `ENGINE_RHI_REQUIRE_GPU=1` turns a skip into a failure (set on the dev
  machine so a broken driver cannot hide).
- Everything that can be checked without a GPU is: the null backend, the
  handle pool, descriptor validation, the barrier table, device-selection
  and swapchain-policy functions (pure functions over plain data).
- Prefer **headless** GPU tests (device with no surface, offscreen render
  targets with readback, golden-image comparison with tolerance plus
  analytic pixel assertions) over window-bound ones; window-bound tests
  are the minority and skip without a display.
- Every GPU test runs under the validation fixture (§10).

## 13. Portability constraint

Per `PLAN.md` assumption #11, nothing in `engine/rhi/*.h` may name Vulkan,
Wayland, X11, Win32 or Cocoa types. The interface vocabulary is the
intersection of Vulkan, D3D12 and Metal; where they disagree the RHI picks
the model that all three can implement efficiently (explicit states,
bind groups, dynamic rendering, timeline fences). Day 67 reviews every type
against D3D12 and Metal.

## 14. Layout

```
engine/rhi/                 backend-agnostic headers + null backend + rhi library
engine/rhi/null/            validating no-GPU backend (Day 47)
engine/rhi/vulkan/          the only place Vulkan is named (rhi_vulkan library)
engine/rhi/{dx12,metal}/    reserved for Phases 12-13
tests/rhi/                  rhi_tests (+ _asan, + _tsan from Day 54)
game/rhi_demo/              dogfood app (Day 57 onward)
shaders/                    *.spvasm sources and generated *.spv.inc
```

Namespace `engine_placeholder::rhi`; headers are included as `"rhi/<name>.h"`
rooted at `engine/`. Log channel: `rhi::LogRhi` (default verbosity `Warn`).

## 15. Out of scope for Phase 3

MSAA, bindless descriptors, indirect draws, async compute and a parallel
transfer queue — recorded with their decision triggers in
`plans/phase-04-renderer-core.md`. Fields are reserved where free
(`samples`, `supports_bindless`, the `indirect` buffer usage).
