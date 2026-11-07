# Phase 12 — Windows Port + DX12 Backend

**Depends on:** Phase 5 (Renderer core) complete on Linux, and Phases 6-8 (test scoping, debugging facilities, RHI reset) complete — the reset lifecycle is part of the RHI interface this backend implements, and the crash/minidump work gets its Windows backend here — this phase ports
the stack built so far (platform layer, RHI, renderer, shaders, job system,
foundation); it doesn't build new engine features. Unreal import (Phases
23-24) is deliberately sequenced after multiplatform support, so it isn't
part of what this phase ports. Before it starts, every day of this phase
needs its own file under `plans/phase-12-windows-dx12/` (see `CLAUDE.md`,
planning policy).
**Goal:** Phases 1-5 running on Windows, with DX12 as the RHI backend
(Vulkan also available on Windows if useful, but DX12 is the native
target). Moved ahead of Font, UI, ECS and the rest on 2026-11-05 so the
RHI and platform abstractions are tested against a second backend while
they are still small.
**Definition of done:** the Phase 4 golden images (triangle, compute,
cube, lit cube) and the Phase 5 scene tests (render graph, materials,
deferred lighting, permuted shaders) pass on Windows through the DX12 RHI
backend, with the validation layers (D3D12 debug layer) silent; the job
system and foundation tests pass on Windows.
**Hardware:** the developer's Windows PC (GPU and Windows version to be
recorded here when the phase is broken into days).

## Key deliverables (to be expanded into day-by-day tasks when started)

- Windows platform layer: raw Win32 API calls (window creation, message
  loop, raw input) — no CRT, matching the Linux platform layer's
  no-dependency stance; Windows x64 calling convention/ABI differs from
  System V and needs its own syscall-adjacent groundwork (Win32 isn't a
  raw syscall interface the way Linux is — it's the OS boundary itself,
  consistent with assumption #1).
- DX12 RHI backend implementing the same interface Phase 4 defined for
  Vulkan.
- Build system support for cross-compiling/targeting Windows from the
  existing toolchain, or a native Windows build path — decide when
  reached.

## Notes / open questions

- Full day-by-day breakdown deferred until Phase 5 is complete.
- **Open, decided at Phase 13 completion:** how Phases 14-20 keep the Windows build working (see `PLAN.md` assumption #11). Until then those phases are developed on Linux first.
- Windows' lack of a raw "syscall" layer the way Linux has one means the
  no-CRT boundary is drawn differently here — Win32 itself is the
  platform contract. Document the precise boundary when this phase starts.

## Shader toolchain (from PLAN.md assumption #18)

The backend needs DXIL shaders. The toolchain that produces them from the
engine's shader source was to be settled before Phase 5; confirm that
decision here and that the mint emits DXIL blobs for `ShaderDesc`
before this phase's day files are written.

- **From the Phase 4 portability review** (`docs/superpowers/specs/2026-11-04-rhi-portability-review.md`, section 5):
  typeless formats for sampled depth; compute-shader `GenerateMips`; bind-group descriptors copied into per-frame
  shader-visible heaps at `SetBindGroup` (measure it against the Vulkan number in the Day 67 baseline); uniform buffers
  and CBV ranges rounded to 256 bytes; CPU repacking in `UploadTexture`/`ReadbackTexture` for 256-byte pitches;
  `ResizeBuffers` after idling; sRGB render-target views over UNorm flip-model buffers; the clip-space Y flip lives in
  the shader translation path; `Mailbox` falls back to `Fifo`. Copy alignment (512 / 256) is already in
  `DeviceCaps::buffer_texture_copy_*_alignment`.

## What Phase 5 gives this phase (Phase 5 Day 84)

From the renderer (see `docs/superpowers/specs/2026-11-04-renderer-portability-review.md`; every row there was a prediction this phase tests):

- A render graph whose barriers are explicit `ResourceState` transitions: D3D12 consumes them directly (legacy `ResourceBarrier` or
  enhanced barriers); the golden images (`tests/rhi/golden/`, including `renderer_demo.ppm`) are the oracle.
- The shader path: `ctest -R shaders_portability` already compiles every shader to HLSL; this phase adds the DXIL step (`dxc`, optional in
  the test, not yet run anywhere), the `PerspectiveD3D` (Y up) projection, and the **object index delivery**: the renderer draws object
  `i` with `first_instance = i` and the shaders read `SV_VulkanInstanceID`, which D3D12 has no equivalent of; use a root constant or a
  per-draw vertex stream, behind a shader define (touches every material shader's vertex stage and `GBufferPass` recording).
- The depth sampling shim (typeless `R32_TYPELESS` resource) is an RHI-backend item, not a renderer one.
- `rhi_bench` and `renderer_bench` are the performance baselines to compare descriptor copy cost and instanced-draw cost against.

- **Debugging facilities backend (from Phase 7; Day 101 made it ready; the remaining work is `engine/foundation/os/windows/debug/BACKEND.md`):** libbacktrace and sentry-native's Breakpad backend both support Windows (`PLAN.md` assumption #20; audit in `docs/superpowers/specs/2026-11-06-debugging-design.md` section 10). To write: the CMake source list (replace the stop message), `SENTRY_BACKEND=breakpad` with `SENTRY_TRANSPORT=none` (no handler executable), the PE CodeView build id in `InitCrashContext`, `GetCrashDumpDirectory` (`%LOCALAPPDATA%`, private ACL), `RunInChild` (`CreateProcess` + job object) with the NTSTATUS-to-`CrashReason` decode, `FaultSupported`/`InjectFault`, `IsDebuggerAttached`, `CurrentThreadId`, `DemangleSymbol`, the module list, `RegisterThreadForCrashHandling` (`SetThreadStackGuarantee`), `EnablePostMortemDumps` (WER `LocalDumps`), and the report written from the library's `on_crash` hook (which receives the exception pointers; the Linux recorder has no counterpart). PowerShell variants of the scripts (`crash-report`, symbol store, dump finder). **Decision for this phase:** libbacktrace reads DWARF only, so build with clang and DWARF or add a DbgHelp (PDB) resolver behind `ResolveStackTrace`. Known gap: fast-fail crashes bypass the library's hook. Reused unchanged: the facade, assert family, report writer, ring buffer, crash state, `tests/debug` with `BACKEND-CONFORMANCE.md` (run its groups), `docs/guide/porting-debugging.md`. Done when the conformance groups pass or skip with a reason, and `debug_backend_lists` and `debug_cross_syntax_windows` still pass.

- **Adapter enumeration (from Phase 8 Day 105):** `rhi::EnumerateAdapters` and `DeviceCreateInfo::adapter` are backend-neutral (`engine/rhi/adapter.h`). DX12 maps onto `AdapterInfo` with `IDXGIFactory6::EnumAdapterByGpuPreference` (high-performance order) and `DXGI_ADAPTER_DESC1`: the **id** is built from the vendor id, device id, subsystem id and revision as lowercase hex (the LUID is only valid until reboot, so it is not part of the id; two identical cards need the LUID to tell them apart within one run, so `MatchAdapter` accepts it as a second form this phase must add), `name` is `Description` converted to UTF-8, `kind` is `Software` for `DXGI_ADAPTER_FLAG_SOFTWARE` (WARP), `Discrete` when `DedicatedVideoMemory` is large and the adapter is not UMA (`D3D12_FEATURE_DATA_ARCHITECTURE`), otherwise `Integrated`, `dedicated_memory_bytes` is `DedicatedVideoMemory`, `driver` comes from `CheckInterfaceSupport(__uuidof(IDXGIDevice))`. `can_present` is true when a device on the adapter can create a swap chain for the window (flip model); `suitable` requires feature level 12_0 (or whichever the RHI settles on) and the capabilities the RHI contract needs; `unsuitable_reason` is a static string. Enumeration creates a throwaway factory and no device. `ENGINE_RHI_GPU` has no DX12 meaning unless this phase adds it; `DeviceCreateInfo::adapter` always wins.

- **Backend registry (from Phase 8 Day 110):** `rhi::CreateDevice`, `EnumerateAdapters` and `DeviceHost` are compiled into `rhi_vulkan`, and the renderer links that library. Add a small backend registry to `rhi` (each backend library registers a factory and an adapter lister) so the renderer and `DeviceHost` live in the backend-neutral library and the DX12 backend plugs in without the renderer changing its link line.

- **Device lifecycle (from Phase 8 Day 114):** implement `engine/rhi/BACKEND-LIFECYCLE.md` (fifteen functions) and add a DX12 factory to `tests/rhi/lifecycle_conformance_test.cpp`; handle generation seeding (`AllocateDeviceSerial`, `GenerationSeed`); `ResizeBuffers` for `Swapchain::Reconfigure`; bounded fence waits so destruction after `DXGI_ERROR_DEVICE_REMOVED` cannot hang; `DeviceHost` and `renderer::Renderer::ApplySettings` then work unchanged. Details: `docs/superpowers/specs/2026-11-04-rhi-portability-review.md`, section 6. **Backend switch at run time** (Vulkan to DX12 through the `backend` setting) is deferred from Phase 8 to here: it needs the backend registry above; Null to Vulkan is tested.

- **File layer (from Phase 9, planned 2026-11-06):** implement `engine/foundation/io/BACKEND.md` for Windows (UTF-16 at the boundary, drive letters and UNC, long paths, `CreateFileW`, overlapped I/O, `MoveFileEx`/`ReplaceFile` for atomic replace, known folders) and run the `File` conformance suite on it. Phase 9 completes before this phase starts.

- **Added 2026-11-06:** Phase 11's `ConfigStore` is an interface; the Windows port implements a registry-backed store (`HKCU\Software\<app>`, mapping of sections and lists in Phase 11's `BACKEND.md` section) and passes the store conformance suite.

- **Settings stores (from Phase 11 Day 139):** implement `engine/foundation/settings/BACKEND.md` for Windows: the registry-backed `ConfigStore` (typed `REG_DWORD`/`REG_QWORD`/`REG_SZ`, per-key writes, no comments or order, `RegNotifyChangeKeyValue` for the change capability, 32/64-bit view, group-policy keys as a read-only `System` layer) and the standard directories for the file store. Start from `RegistryShapedConfigStore` in `tests/settings/store_doubles.h`, which already passes the conformance suite (`tests/settings/config_store_conformance.h`) in a registry's shape; add a Windows factory to the conformance tests and `settings_cross_syntax_windows` must keep passing. The `config_access_check` gate applies to the new code: no `GetEnvironmentVariable` or flag parsing outside the settings system without an allow-list entry.

- **Environment (from the maintenance pass, 2026-11-07):** implement `os::EnvironmentCount` and `os::EnvironmentEntry` (`engine/foundation/os/os.h`) with `GetEnvironmentStringsW`, converted to UTF-8 `NAME=value`; the settings system's `EnvironmentSnapshot::Capture` is neutral and needs nothing else. macOS (Phase 13): `environ` through `_NSGetEnviron()`.
