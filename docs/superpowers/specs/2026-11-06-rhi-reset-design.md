# RHI reset — design (Phase 8, Day 103)

Status: decided 2026-11-06 from the inventory and spikes below. Scope: Linux (Vulkan and null backends) implemented;
DX12 and Metal left ready (Day 114). `plans/phase-08-rhi-reset.md` holds the day list; this spec holds the reasons.

## 1. Goal

A user changes any rendering setting while the application runs. Re-initializing the device, the swapchain and every
device-dependent resource is a supported, tested operation, and a lost device recovers through the same path.

## 2. Inventory (what exists today)

**Where the device and swapchain are created.** `rhi::CreateDevice(DeviceCreateInfo)` builds, per device, its own
`VulkanInstance` (validation layers are an instance property), the presentation **surface** from the window's native handles
(`VulkanDevice::CreateSurface`) and the physical-device choice (`SelectPhysicalDevice`, `ENGINE_RHI_GPU` the only override).
Consequence: **every device re-creation already creates a new instance and a new surface**, so changing the adapter or
validation needs no extra mechanism, and a full reset never has to repair a surface. Recreating only the surface without
the device (Day 107) needs a new `Device` entry point.

**Holders of device handles** (each needs a CPU-side source and a registry entry):

| Holder | Handles it caches | CPU-side source today | Missing |
|---|---|---|---|
| `GpuMesh` (`renderer/mesh.h`) | vertex and index buffer | `MeshData` exists but is not kept by `GpuMesh` (`Create` takes a const reference) | an owner that keeps or can reload `MeshData` |
| demo textures (`game/common/textures.cpp`, `game/renderer_demo/demo_scene.cpp`, `rhi_demo` `CubeTexture`) | texture, sampler | `.rtex` file on disk or procedural pixels, not kept | a `GpuTexture` owner with pixels or a loader |
| `MaterialSystem` (`renderer/material.h`) | frame and object layouts, 3 default textures, default sampler, per-material bind groups, pipeline cache, cached groups | templates, reflection, parameter bytes (CPU); texture and sampler bindings are **raw handles supplied by the caller** | bindings by reference to owners |
| `GBufferPass`, `GBufferDebugView` | bind groups, shaders, sampler, layout, pipeline | embedded SPIR-V (CPU) | registration |
| `LightingPass`, `TonemapPass` | shaders, layouts, 2 pipelines, bind group | embedded SPIR-V | registration; the tonemap pipeline depends on the swapchain format |
| `FrameData` | frame, object, light buffers | scene (CPU) | full re-upload flag |
| `TransientPool` | pooled textures and buffers | none needed (contents are per frame) | `Release` |
| `UniformRing` | one buffer | none needed | recreation by its owner |
| `CubeRenderer` (`game/common`) | shaders, layout, pipeline, buffers | constant data | registration |
| `Renderer` | `Swapchain*`, device pointer | n/a | `Rebind` |

`RenderObject` holds `const GpuMesh*`, which stays valid across a reset if the `GpuMesh` object is the thing that is
recreated in place; no render object caches a buffer handle.

**What a caller can change on `DeviceCreateInfo` today:** backend, validation (plus `ENGINE_RHI_VALIDATION`,
`ENGINE_RHI_VALIDATION_SYNC`), sync validation, window, frames in flight, pipeline cache and directory, descriptor pool size,
fault injection. Only the environment: the GPU (`ENGINE_RHI_GPU`), the loader library. On the swapchain: present mode and
size at creation; `Resize(w,h)` afterwards. Nothing else (format, HDR, fullscreen) is settable. Demo options that are
**not** rendering settings: `--objects`, `--lights`, `--frames`, `--screenshot`. `--gbuffer` is a debug view (a `Dynamic`
setting). Settings later phases add (anti-aliasing, shadow quality, upscaling, UI scale) register themselves (Day 104).

## 3. Spike results

Throwaway harness (not committed): the `rhi_demo` window path in a loop on one `os::Window`. Per iteration: create device with
window, create swapchain, create the lit cube scene, render 10 presented frames, `WaitIdle`, destroy everything. Machine:
Linux 7.2, Wayland, RTX 3060 (the only Vulkan adapter), NVIDIA proprietary driver, clang debug build, pipeline cache on.

**Spike A — 100 re-creations on one window.** All 100 iterations rendered 10 frames. No validation message, no failed call.
Median per iteration: `CreateDevice` 119-127 ms (first call 196 ms; instance, surface, device), swapchain 3.5 ms, scene
(shaders, pipeline, texture, ring) 7.5 ms, 10 frames 27-31 ms, destruction 47 ms; **about 205-215 ms for the cycle**.
Resident memory: 25132 KiB from iteration 1 to 58, 25260 KiB from then on (**+128 KiB over 100 iterations**); open file
descriptors constant (4). With validation and sync validation on (20 iterations): same timings, no messages, +944 KiB.
Under ASan, 20 iterations with the existing `tests/rhi/lsan.supp`: no leak in engine code; the suppression counted 60
allocations (41256 bytes) in `libdbus-1.so`, about 3 per device creation and 2 KiB per cycle, held by the driver's loader
path (the existing "driver process-lifetime allocation" entry). It grows with the number of devices created, so a
100-reset loop retains about 200 KiB the suppression hides; the loop test (Day 112) budgets for it.

**Spike B — adapter switching.** Not measurable here: `vulkaninfo` lists one adapter, and only the NVIDIA ICD is installed
(`/usr/share/vulkan/icd.d`). The multi-adapter test therefore cannot run on this machine; it is written to skip with a
reason and filed in `KNOWN-ISSUES.md` as a check no human or machine has done. Mitigations: the null backend reports two
fake adapters so adapter selection, ids and precedence are fully tested without hardware; the Vulkan GPU test creates a
device on every adapter it finds (one here). Because the instance and surface are created per device (section 2), switching
adapter is the same code path as any other re-creation.

**Spike C — pipeline cache.** The scene creation took 37.5 ms in the very first iteration of a fresh process and 7.3-8.1 ms
in every later one; a run with an empty `ENGINE_RHI_CACHE_DIR` showed 9.6 ms for the first iteration, so the driver's own
shader cache hides the difference on this machine. One lit-cube pipeline is too small to tell the engine's file cache from
the driver's. The renderer's pipeline set (G-buffer permutations, lighting, tonemap) is measured on Day 113; the
lazy-versus-eager rebuild choice is decided there.

**Spike D — device loss.** `lose_device_at_frame = 5` in iteration 2: `BeginFrame` printed the device-lost ensure with a
trace, the 4 frames before it were presented, the loop destroyed the lost device (`WaitIdle`, scene, swapchain, device)
without hanging or any error, and iterations 3 and 4 created a new device on **the same window** and rendered all 10 frames.
So the surface of a lost device is not special (it dies with the device, as always). The loss iteration cost 5.4 MiB
resident (26412 to 31812 KiB) that was not returned, then stayed constant for the next two iterations; not investigated
further (a single sample); Day 111 repeats the loss several times and records the drift.

## 4. Decisions

The seven calls made when the phase was planned, confirmed or amended:

1. **A reset destroys the `Device` and creates a new one** (confirmed). Spike A shows it is cheap enough (about 210 ms for
   a trivial scene, zero fd and near-zero memory drift) and it is the only way to change adapter, validation or backend with
   one mechanism. No backend gets an in-place reset. The `VulkanInstance` is not shared between devices (a shared instance
   would save a part of the 120 ms creation, unmeasured; revisit only if Day 113 shows creation dominating a reset).
2. **Stale handles across devices are detected by seeding each device's generation counters.** Chosen over a device-id field
   in `Handle` (would grow it past 8 bytes and every key and table with it) and over invalidating per owner (misses
   anything not registered). Mechanism: a process-wide device serial; a pool's initial slot generation is
   `(serial & 0xFFF) << 20 | 1`, so handles of different devices differ in their top 12 bits. Limits, documented: a slot must be
   reused over 2^20 times before its generation could reach another device's range, and the serial wraps after 4096
   devices in one process. Slot generation `0` stays invalid and `NextGeneration` wraps past 0 as today.
3. **Owners rebuild from CPU-side sources through a tiered registry** (confirmed). Tiers: 0 samplers and textures, 1 meshes,
   2 layouts and shaders, 3 materials and pipelines, 4 passes and per-frame buffers. Pipelines are rebuilt from descriptors.
   `GpuMesh` gets a source it keeps (an owning wrapper of `MeshData` for procedural meshes, a loader callback for assets);
   textures become `GpuTexture` objects with pixels or a loader; materials bind textures by owner reference, not by raw handle.
4. **`backend` is a setting** (confirmed). Null to Vulkan is a tested transition. Vulkan to DX12 or Metal is deferred to
   Phases 13 and 21 (no second real backend exists); filed in `KNOWN-ISSUES.md`.
5. **Fullscreen** needs a window API that does not exist (`os::Window` has a resize serial and no fullscreen call). Day 107
   adds it for Wayland and X11 and the checklists for Windows and macOS; a platform that cannot do it reports the capability
   unsupported and the setting is unavailable.
6. **A failed reset rolls back** (confirmed): recreate the previous settings; if that fails too, the renderer reports
   `DeviceLost` with the reason and calls the app.
7. **Settings are an open registry with declared cost classes** (confirmed, Day 104).

Further calls made here:

8. **Surface re-creation without a device reset needs a new `Device` call** (`Device::RecreateSurface(window)`), because the
   surface belongs to the device. If the adapter cannot present to the new surface the caller falls back to a full reset.
9. **Pipeline rebuild after a reset is lazy by default** (first use) and `Prewarm` makes it eager; the default is decided by
   the Day 113 measurement.
10. **A reset is performed inline by `RenderFrame`.** Open on Day 110 only as to naming the status; the frame that starts a
    reset returns `Ok` after it, so an app that never learns of the reset still works. The app is told through callbacks.
11. **Resets are counted in frames, not wall-clock time**, for retry limits and delays (deterministic tests).
12. **Tests use the null backend wherever the behavior is not GPU-specific**; GPU tests carry the `gpu` label and skip
    without a GPU, as in Phases 4-5.

## 5. Cost classes

| Class | Definition | Example setting |
|---|---|---|
| `None` | Takes effect without touching the GPU or the next frame | a setting stored for later (a name, a preference no pass reads yet) |
| `Dynamic` | Changes a parameter read by the next frame | `gbuffer_debug_layer`, `render_scale` as a viewport factor |
| `Swapchain` | Recreates the swapchain, and what depends on its extent or format | `present_mode`, window size, `fullscreen`, `swapchain_format`, `hdr` |
| `PipelineRebuild` | Releases and recreates pipelines, materials' bind groups and passes (tiers 3-4); the device stays | `shader_quality` |
| `DeviceReset` | Destroys and creates the device; every registered owner rebuilds | `backend`, `adapter`, `validation`, `sync_validation`, `frames_in_flight` |

A change set costs its highest class (a higher class includes the actions of the lower ones).

## 6. The sequence of a full reset

1. At a frame boundary on the owning thread (no `BeginFrame` open), the renderer joins any worker still recording and calls
   `Device::Quiesce` (wait idle, drain the deferred-destruction queue).
2. Observers are called `OnDeviceReleasing(epoch)` in descending tier: every owner destroys its device objects and keeps its
   CPU source.
3. `ResourceCounts` is checked; leftovers are logged as leaks (an error in tests).
4. The swapchain, then the device, are destroyed (the pipeline cache file is written).
5. The new `DeviceCreateInfo` is built from the new settings; the device and swapchain are created. On failure: rollback
   (section 4, item 6).
6. Observers are called `OnDeviceCreated(device, epoch)` in ascending tier; each owner recreates and re-uploads. A failing
   owner stops the sequence, everything is released again, and the caller rolls back.
7. `Renderer::Rebind` sets the new pointers; the next frame uploads all dirty data and renders. The scheduler is untouched.

**What survives:** the scene (objects, lights, transforms, material parameters), the camera, input state, the asset system,
the job scheduler, the window. **What does not:** every handle of the old device (detectable, decision 2), transient pool
contents, in-flight frames (drained), pipelines (rebuilt).

## 7. Threads

A reset runs on the owning thread (the one that owns the swapchain and, with a scheduler, its participant 0) at a frame
boundary. A call to `ApplySettings` between `BeginFrame` and `EndFrame`, or from a job, is queued and applied at the start of
the next `RenderFrame`; a newer pending request replaces an older one. `DeviceHost::Recreate` itself aborts if called
inside a frame or off the owning thread (debug builds). Workers that record command lists are joined before the reset starts;
the scheduler keeps running other work.

## 8. Interface sketches (names are final unless a day file amends them)

```cpp
namespace engine_placeholder::rhi {
struct AdapterInfo;                       // Day 105: id, name, kind, memory, presentable, suitable + reason
Result EnumerateAdapters(const DeviceCreateInfo&, AdapterList*);
class DeviceObserver { OnDeviceReleasing(u32 epoch); OnDeviceCreated(Device*, u32 epoch); };
class DeviceHost {                        // Day 106
  bool Create(const DeviceCreateInfo&, const SwapchainDesc&);
  bool Recreate(const DeviceCreateInfo&, const SwapchainDesc&);   // aborts inside a frame
  Device* Device(); Swapchain* Swapchain(); u32 Epoch() const;
  void AddObserver(DeviceObserver*, u32 tier);
};
}
namespace engine_placeholder::renderer {
class RenderSettings;                     // Day 104: open registry, Get/Set by SettingId, ==, Serialize/Parse
SettingsChange DiffSettings(const RenderSettings& from, const RenderSettings& to, const Capabilities&);
class DeviceResources;                    // Day 108: tiered registry, ScopedRegistration
ApplyResult Renderer::ApplySettings(const RenderSettings&);   // Day 110
}
```

## 9. Not in scope

Backend switch from Vulkan to DX12 or Metal; saving GPU state across a process restart; hot-swapping shaders during a reset;
a settings user interface (the Phase 15 widget toolkit will show them; this phase gives command line, file and demo keys).

## 10. Risks

- The driver retains a few allocations per device creation (section 3, Spike A); fine for 100 resets, relevant if a game
  resets all day. Recorded, with the measurement, in `KNOWN-ISSUES.md`.
- Only one adapter exists on the development machine, so adapter switching on real hardware is unverified.
- Generation seeding bounds, not eliminates, aliasing (decision 2); the tests plant a stale handle to show it is caught.
