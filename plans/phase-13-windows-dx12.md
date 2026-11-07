# Phase 13 — Windows Port + DX12 Backend

**Depends on:** Phase 5 (Renderer core) complete on Linux, Phases 6-11 (test scoping, debugging facilities, RHI reset, file layer, standard types,
settings) complete, and **Phase 12 (module layering)** complete: the foundation is a set of layered libraries with no dependency cycle, so each Windows
backend below is built and tested as soon as its layer is ported (`ARCHITECTURE.md`). This phase ports the stack built so far (foundation, file layer, settings, job system, debugging facilities, platform layer, RHI,
renderer, shaders, demos, tools and gates); it builds no new engine features. Unreal import (Phases 24-25) is deliberately sequenced after multiplatform
support, so it isn't part of what this phase ports.
**Goal:** everything that runs on Linux today runs on Windows, with DX12 as the native RHI backend and the existing Vulkan backend also running on
Windows. Moved ahead of Font, UI, ECS and the rest on 2026-11-05 so the RHI and platform abstractions are tested against a second backend while they are
still small.
**Definition of done:** on Windows, the foundation, file-layer, settings, job-system and debugging suites pass (plain and ASan); the Win32 window and
input pass Phase 1's tests; the Phase 4 golden images (triangle, compute, cube, textured cube, lit cube) and the Phase 5 scene tests (render graph,
materials, deferred lighting, permuted shaders, `renderer_demo.ppm`) pass through **both** the DX12 and the Vulkan backends with the D3D12 debug layer
and the Vulkan validation layers silent; the DX12 backend passes the lifecycle conformance suite, recovers from an injected device loss, and the
`backend` setting switches between Vulkan and DX12 at run time; both demos run on DX12 and Vulkan; every gate and `ctest -L docs` pass on Windows; and the
Linux machine still passes its full suite in every flavor.

**Status: planned (2026-11-07), Days 147-174; not started.** Day numbers continue from Phase 12 (which ends at Day 146). Day 147 writes the spec
(`docs/superpowers/specs/2026-11-NN-windows-port-design.md`) that confirms or amends the decisions below with experiments.

**Hardware.** Windows: the developer's PC (`archvile-win`), AMD Ryzen AI 7 350 (8 cores, 16 threads), AMD Radeon 860M integrated GPU (driver
32.0.32015.7029), 63 GiB RAM, Windows 11 Pro build 26300; MSVC 14.51.36231 (Visual Studio Build Tools 2026, 18.10), Windows SDK 10.0.26100.0 (with
`dxc`), CMake 4.4.3, Ninja. Linux: the separate Linux development machine (NVIDIA RTX 3060, Wayland and X11; the golden images were blessed on it); Day
147 records its details and how it is reached. **One GPU on each machine:** the second DX12 adapter is WARP (the software rasterizer), which is what the
run-time GPU switch is tested against on Windows.

## Days

147. [Design Spec, Inventory, Toolchain & Phase Setup](phase-13-windows-dx12/day-147-design-spec-and-setup.md)
148. [CMake and the Compiler on Windows](phase-13-windows-dx12/day-148-cmake-and-compiler.md)
149. [The `os` Backend, Console and Log; Foundation Tests Pass](phase-13-windows-dx12/day-149-os-backend.md)
150. [Test Tooling, Gates and Docs on Windows](phase-13-windows-dx12/day-150-test-tooling-and-gates.md)
151. [File Layer I: Paths, Errors, Local Files](phase-13-windows-dx12/day-151-file-layer-files.md)
152. [File Layer II: Directories, File-System Operations, Asynchronous I/O](phase-13-windows-dx12/day-152-file-layer-directories-async.md)
153. [The Job System on Windows](phase-13-windows-dx12/day-153-job-system.md)
154. [Debugging I: Stack Traces, Symbols, Crash Context](phase-13-windows-dx12/day-154-debug-stack-traces.md)
155. [Debugging II: Crash Handler, Death Tests, Fault Injection](phase-13-windows-dx12/day-155-debug-crash-handler.md)
156. [Debugging III: Dumps, Symbol Store and Scripts](phase-13-windows-dx12/day-156-debug-dumps-and-scripts.md)
157. [Settings on Windows: the Registry Store](phase-13-windows-dx12/day-157-settings-registry-store.md)
158. [Win32 Window](phase-13-windows-dx12/day-158-win32-window.md)
159. [Win32 Input](phase-13-windows-dx12/day-159-win32-input.md)
160. [Vulkan on Windows: Loader, Surface, RHI Tests and Golden Images](phase-13-windows-dx12/day-160-vulkan-on-windows.md)
161. [Renderer, Demos and Baselines on Windows through Vulkan](phase-13-windows-dx12/day-161-renderer-on-windows-vulkan.md)
162. [RHI Backend Registry and Bytecode Blobs](phase-13-windows-dx12/day-162-backend-registry-and-bytecode.md)
163. [The Shader Path for D3D12: DXIL, Object Index, Clip Space](phase-13-windows-dx12/day-163-dxil-shader-path.md)
164. [DX12 I: Factory, Adapters, Device, Debug Layer, Fences](phase-13-windows-dx12/day-164-dx12-device.md)
165. [DX12 II: Buffers, Textures, Upload and Readback](phase-13-windows-dx12/day-165-dx12-resources.md)
166. [DX12 III: Descriptor Heaps, Root Signatures, Bind Groups](phase-13-windows-dx12/day-166-dx12-descriptors.md)
167. [DX12 IV: Pipelines and the Pipeline Cache](phase-13-windows-dx12/day-167-dx12-pipelines.md)
168. [DX12 V: Command Lists, Barriers, Passes, Mips](phase-13-windows-dx12/day-168-dx12-command-lists.md)
169. [DX12 VI: Swapchain and Presentation](phase-13-windows-dx12/day-169-dx12-swapchain.md)
170. [DX12 Golden Images: the Phase 4 Suite](phase-13-windows-dx12/day-170-dx12-golden-images.md)
171. [DX12 Renderer: the Phase 5 Suite](phase-13-windows-dx12/day-171-dx12-renderer.md)
172. [DX12 Lifecycle, Device Loss and the Backend Switch](phase-13-windows-dx12/day-172-dx12-lifecycle.md)
173. [Demos, Benchmarks and the clang-cl Build](phase-13-windows-dx12/day-173-demos-benches-clang-cl.md)
174. [Guides, Closing Full Runs on Both Machines & Wrap-up](phase-13-windows-dx12/day-174-docs-and-closing-run.md)

## Rules for every day of this phase

- **Two machines.** Shared code is everything outside `engine/foundation/os/windows/`, `engine/foundation/io/windows/`,
  `engine/foundation/settings/windows/`, `engine/rhi/dx12/`, Windows-only tests (`*_windows_test.cpp`, `tests/rhi/dx12_*`) and Windows-only scripts
  (`scripts/*.ps1`). A day that changes shared code ends with `scripts/test-phase.sh --affected` on **both** the Windows PC and the Linux machine (the
  work moves between them through `git push`), and the day file and `STATUS.md` quote both final lines. A day that touches only Windows files says so
  and quotes the Windows line alone.
- **Flavors.** Windows has `plain` and `asan` (MSVC `/fsanitize=address`); MSVC has no ThreadSanitizer or UBSan, so `tsan` (and the UBSan half of Linux's
  `asan` flavor) run on the Linux machine only. `scripts/test-phase.sh` on Windows reports the flavors it skipped (Day 150). Concurrency, lifetime and
  memory days run the Windows ASan flavor and the Linux sanitizer flavors of the same selection.
- **Compilers.** MSVC (`cl`) is the primary Windows compiler. A second configuration with `clang-cl` must build every target and pass the phase 0 tests;
  build it on every day that changes shared or Windows code (Day 148 adds it; Day 173 runs its full suite once).
- **Where Windows code goes.** `windows.h` and the other Win32, DXGI and D3D12 headers are included only under the directories above, through
  `os/windows/win32.h` (which wraps them in `unity/windows_enable.h` / `windows_disable.h`, CLAUDE.md "Unity builds"); `platform_neutral_check` and
  `rhi_boundary_check` enforce it from Day 150. UTF-8 everywhere; UTF-16 exists only at the Win32 call boundary.
- **Labels.** A shared test keeps its phase when it starts running on Windows; a new Windows-only test is labeled phase 13.
- **The rest of CLAUDE.md applies unchanged:** end-of-day `/code-review high`, public API documented, known issues filed, unity and non-unity builds
  both pass (on both machines), the standard containers are not used, file access goes through the file layer, configuration goes through settings.

## Design decisions made while planning (2026-11-07; Day 147 confirms or amends them in the spec)

1. **Toolchain:** MSVC 14.51 with the Windows SDK, CMake presets (`windows-msvc`, `windows-clang-cl`) and Ninja; a native Windows build, not a cross
   build from Linux. **The CMake files locate MSVC, the Windows SDK and `clang-cl` themselves** (developer requirement, 2026-11-07): toolchain files use
   `vswhere` and the Windows Kits registry key, so configuring needs no Developer Command Prompt or `vcvars`; a stripped-environment ctest proves it (Day 148). Flags: `/std:c++20 /permissive- /utf-8 /Zc:__cplusplus /Zc:preprocessor /EHs-c- /GR- /W4`, `_HAS_EXCEPTIONS=0`; the
   production options become a per-compiler list (`ENGINE_PRODUCTION_COMPILE_OPTIONS` names GCC flags today).
2. **The C runtime is allowed** (the UCRT is Windows' libc, `PLAN.md` assumption #2; the earlier "no CRT" wording of this file contradicted it). The
   foundation still hand-rolls its containers, strings and allocators, and Win32 is the platform contract (assumption #1), not a syscall layer.
3. **Descriptors.** `os::Open`/`Write`/`Read`/`Close` keep `int` descriptors (console, pipes, the crash report). Day 147 decides between the UCRT's
   descriptors (`_open`, `_write`) and an engine table of `HANDLE`s behind the same `int`; whichever is chosen, the crash path must not take the CRT's
   per-descriptor lock.
4. **Every executable carries a manifest:** UTF-8 active code page, `longPathAware`, per-monitor-v2 DPI awareness, Windows 10/11 `supportedOS`.
5. **Symbols are PDBs; stack traces use DbgHelp** behind `ResolveStackTrace` (libbacktrace reads DWARF only and is not built on Windows). Crash capture is
   sentry-native's Breakpad backend with no transport, as on Linux; the report is written from its `on_crash` hook. The symbol store uses the standard
   `<pdb>/<GUID+age>/<pdb>` layout so WinDbg and Visual Studio read it without extra tools.
6. **One Win32 window backend** (`WindowBackend::Win32`; `auto` means Win32 on Windows); input from window messages plus raw input for relative mouse
   motion.
7. **Vulkan on Windows comes before DX12** (Days 160-161): it proves the window, the renderer and the golden images on Windows with a backend that
   already works, and it is the baseline the DX12 numbers are compared with on the same machine.
8. **Neutral RHI changes first** (Day 162, on both machines): a backend registry in `rhi` (each backend library registers a device factory and an
   adapter lister, so the renderer and `DeviceHost` stop linking `rhi_vulkan`), and `ShaderDesc` becomes a format-tagged bytecode blob (SPIR-V, DXIL;
   MSL in Phase 14).
9. **Shaders:** `slangc` (the same version as Linux, v2026.18.2) mints DXIL offline; outputs committed as `*.dxil.inc` beside `*.spv.inc`, minted
   deterministically; the per-object index reaches D3D12 shaders through a per-instance vertex stream (`OBJECT_INDEX` from a static buffer `[0, N)`, draw
   `i` issued with `StartInstanceLocation = i`), behind the define `ENGINE_OBJECT_INDEX_STREAM` (**amended on Day 147**: Spike B measured 14.8 ns per draw
   against 21.5 for a root constant, and the renderer's `first_instance = i` needs no change); the clip-space Y flip lives in the projection (`PerspectiveD3D` or a per-backend flag).
10. **D3D12 baseline:** feature level 12_0, shader model 6.0, resource binding tier 2, legacy `ResourceBarrier` (enhanced barriers only if measured),
    flip-model swap chains, the OS's own `d3d12.dll` (no Agility SDK redistributable, no D3D12MA, no DirectX-Headers package, no WRL `ComPtr`: the
    backend has a small `ComRef<T>` of its own). Committed resources first; placed resources only if Day 165's measurements ask for them.
11. **Golden images are not re-blessed for Windows.** The existing images and tolerances (blessed on the RTX 3060) are the oracle on the AMD iGPU too; a
    difference outside tolerance is investigated, and any tolerance change is recorded with the measured numbers.
12. **Line endings are pinned** (`.gitattributes`, `eol=lf` for text, binary for images and dumps): this checkout has `core.autocrlf=true`, which would
    turn byte-identical minted outputs and generated docs into CRLF on Windows.

## Prerequisites the developer installs (Day 147 checks each and records the version)

The full per-platform list, with install commands, is [`BUILD.md`](../BUILD.md) (added 2026-11-07); the items below are the ones that were missing on the
Windows PC when the phase was planned. Every day that changes a prerequisite updates `BUILD.md`.

- The **Graphics Tools** optional Windows feature (the D3D12 debug layer, `d3d12SDKLayers.dll`; not installed on 2026-11-07).
- The **Vulkan SDK** for Windows (validation layers; the loader `vulkan-1.dll` already ships with the driver).
- **`slangc`** v2026.18.2 for Windows (the version the Linux machine uses, so minted outputs match byte for byte).
- **`clang-cl`: installed 2026-11-07** as LLVM 23.1.5 through winget (`LLVM.LLVM`) in `C:\Program Files\LLVM`; it writes no `HKLM\SOFTWARE\LLVM\LLVM`
  key and was not on `PATH` in an open shell, so the `%ProgramFiles%\LLVM\bin` fallback of Day 148's search is what finds it. Checked by hand: `clang-cl`
  23.1.5 with MSVC 14.51's headers, the 10.0.26100 SDK and `lld-link` compiled, linked and ran a C++20 `windows.h` program from a plain shell (no
  `INCLUDE`/`LIB`). The Visual Studio "C++ Clang tools" component is not needed. `clang-cl` has native `unsigned __int128` and MSVC does not, so the two
  Windows configurations cover both `int128.h` paths.
- **Debugging Tools for Windows** (`cdb`, WinDbg) from the Windows SDK installer, for the minidump checks.
- Optional: PIX for Windows, for GPU captures while debugging DX12.

## Inputs collected from earlier phases (each is a task in the day named)

- **From the Phase 4 portability review** (`docs/superpowers/specs/2026-11-04-rhi-portability-review.md`, section 5) — Days 165-169:
  typeless formats for sampled depth; compute-shader `GenerateMips`; bind-group descriptors copied into per-frame shader-visible heaps at `SetBindGroup`
  (measure it against the Vulkan number); uniform buffers and CBV ranges rounded to 256 bytes; CPU repacking in `UploadTexture`/`ReadbackTexture` for
  256-byte pitches; `ResizeBuffers` after idling; sRGB render-target views over UNorm flip-model buffers; the clip-space Y flip in the shader path;
  `Mailbox` falls back to `Fifo`. Copy alignment (512 / 256) is already in `DeviceCaps::buffer_texture_copy_*_alignment`.
- **From Phase 5** (`docs/superpowers/specs/2026-11-04-renderer-portability-review.md`) — Days 163, 171: explicit `ResourceState` transitions consumed
  directly as D3D12 barriers; the DXIL step of `shaders_portability` (`dxc` never ran usefully: the Linux package aborts, `KNOWN-ISSUES.md`); the
  `PerspectiveD3D` projection; the object index delivery (every material vertex shader reads `SV_VulkanInstanceID` today, and `GBufferPass` records
  `first_instance = i`); the depth sampling shim; `rhi_bench` and `renderer_bench` as the baselines.
- **Debugging facilities backend (Phase 7)** — Days 154-156: everything in `engine/foundation/os/windows/debug/BACKEND.md`; re-vendor the Windows
  private headers of Breakpad (`KNOWN-ISSUES.md`, Phase 7 Day 95); PowerShell variants of the scripts; the fast-fail gap documented; the conformance
  groups of `tests/debug/BACKEND-CONFORMANCE.md` pass or skip with a reason; `debug_backend_lists` and `debug_cross_syntax_windows` still pass.
- **Adapter enumeration (Phase 8 Day 105)** — Day 164: `AdapterInfo` from `IDXGIFactory6::EnumAdapterByGpuPreference` and `DXGI_ADAPTER_DESC1`; id
  from vendor, device, subsystem and revision ids as lowercase hex; `MatchAdapter` also accepts the LUID; `kind`, `dedicated_memory_bytes`, `driver`,
  `can_present`, `suitable` as described there; enumeration creates no device.
- **Backend registry (Phase 8 Day 110)** — Day 162.
- **Device lifecycle (Phase 8 Day 114)** — Day 172: `engine/rhi/BACKEND-LIFECYCLE.md` (fifteen functions), a DX12 factory in
  `tests/rhi/lifecycle_conformance_test.cpp`, `AllocateDeviceSerial`/`GenerationSeed`, `ResizeBuffers` for `Swapchain::Reconfigure`, bounded fence waits
  after `DXGI_ERROR_DEVICE_REMOVED`, and the run-time backend switch (deferred from Phase 8).
- **Fullscreen and session state** — Day 158: `engine/platform/windows/FULLSCREEN.md` and `SESSION.md`.
- **File layer (Phase 9)** — Days 151-152: `engine/foundation/io/BACKEND.md` for Windows (UTF-16 at the boundary, drive letters and UNC, long paths,
  `CreateFileW`, overlapped I/O, `MoveFileEx`/`ReplaceFile`, known folders), the `File` conformance suite, share modes (filed in Phase 9).
- **Settings stores (Phase 11 Day 139)** — Day 157: `engine/foundation/settings/BACKEND.md` for Windows, starting from `RegistryShapedConfigStore`.
- **Environment (maintenance pass, 2026-11-07)** — Day 149: `os::EnvironmentCount` and `os::EnvironmentEntry` with `GetEnvironmentStringsW`; Day 157
  moves `env_source.cpp`'s platform-specific `environ` behind `os::` (`KNOWN-ISSUES.md`, settings item 11).
- **Standard types (Phase 10)** — Day 148: the portable `u128`/`i128` path built and run with MSVC (`KNOWN-ISSUES.md`, Phase 10 Day 129).
- **Shader mint** — Day 163: `shader_mint --permute` starts `slangc` with `posix_spawnp` (`KNOWN-ISSUES.md`).

## Notes / open questions

- **Open, decided at Phase 14 completion:** how Phases 15-21 keep the Windows build working (`PLAN.md` assumption #11). Until then those phases are
  developed on Linux first.
- **HDR on Windows** (`SetColorSpace1` with scRGB, HDR10 once the RHI has a 10-bit format): the swapchain day maps `HdrMode::ScRgbLinear`; HDR10 and a
  real HDR display check stay filed unless the PC's display supports HDR (Day 169 records it).
- **Exclusive fullscreen** is not implemented (`FULLSCREEN.md`); borderless only.
- **Enhanced barriers, placed resources, `ExecuteIndirect`, mesh shaders, DirectStorage**: not in this phase; each needs a measurement or a feature
  that asks for it.
- **ARM64 Windows** is not a target in this phase (no hardware); interfaces stay neutral (the debugging checklist already notes the divide-by-zero
  difference).
