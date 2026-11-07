# ARCHITECTURE.md — Modules, layers and dependencies

The engine's modules, the layer each belongs to, what each may depend on, and how the code measures against that. `PLAN.md` gives the mission and
the long-term layer diagram; this file is the **source of truth for dependencies between the modules that exist**. It is measured from the code, not
written from intent, and each mismatch is listed with what fixes it.

Written 2026-11-07 and brought to the state after Phase 12 (the module layering) the same day, from the `#include` graph of `engine/`, `game/` and `tools/` and the CMake link lines. The rule: **a module
may include and link only modules in lower layers, or other modules its row allows; never one above it, and no cycles.**

## 1. Layers

From the bottom up. "Platform code" means a per-OS backend directory (`linux/`, `windows/`, `macos/`, ...) under the module it implements.

| Layer | Module (directory) | What it is |
|---|---|---|
| 0. Base | `foundation` root headers: `types.h`, `int128.h`, `debug/compiler.h` | Fixed-width types, 128-bit integers, compiler macros. Depends on nothing. |
| 1. OS primitives | `foundation/os/os.h` (+ `os/<platform>/os.cpp`) | Descriptors, memory reservation, clocks, sleep, dynamic libraries, the environment. Base only. |
| 2. Memory | `foundation/mem` | Heap, arena, pool and stack allocators. Base, OS primitives. |
| 3. Assert and log core | `foundation/log` | `ENGINE_ASSERT` family, log channels and sinks to descriptors. Layers 0-2. The assert *failure report* (stack trace, crash context) belongs to debugging (layer 7), reached through a hook (section 3, item 1). |
| 4. Containers and strings | `foundation/containers`, `foundation/string`, `algorithm.h`, `foundation/math` | `Vector`, `HashMap`, `Span`, `String`, UTF-8, graphemes, normalization, `Sort`, vectors and matrices. Layers 0-3. |
| 5. OS services | `foundation/os` (`command_line.h`, `session.h`, `gpu_driver_probe.h`) | Command line, session state, GPU driver probe. Layers 0-4. |
| 6. File layer | `foundation/io` (+ `io/<platform>/`) | `Path`, `File`, `Directory`, file-system operations, special folders. Layers 0-5. |
| 7. Debugging | `foundation/debug` (+ `os/<platform>/debug/`) | Stack traces, crash handler, crash context and state, death tests, dumps. Layers 0-6; libbacktrace and sentry-native only in its two facade files (`library_boundary.txt`). |
| 8. Settings | `foundation/settings` | Registry, stores, layers, the `Settings` service. Layers 0-7. |
| 9. Platform: windows and input | `engine/platform` (`window.h`, `input.h`, `window_host.h`, `platform/<platform>/` backends) | Windows, input state, display backends (Wayland, X11, Win32), the CMake library `platform`, the only one that links the display libraries. Layers 0-8. Moved here from the foundation on Phase 12 Day 144. |
| 10. Jobs | `engine/jobs` | Scheduler, coroutines, frame pool, async I/O. Foundation (layers 0-8). |
| 11. RHI | `engine/rhi` (neutral), `engine/rhi/null`, `engine/rhi/vulkan`, later `rhi/dx12`, `rhi/metal` | The neutral RHI, and one library per backend. Backends depend on `rhi`, jobs and foundation, never on each other. Graphics API headers only inside their backend (`rhi_boundary_check`). |
| 12. Renderer | `engine/renderer` (+ `renderer_passes`) | Render graph, materials, passes. RHI (neutral only), jobs, foundation including settings. |
| 13. Programs | `game/*`, `tools/*`, `tests/*` | Demos, benches, tools and tests. Anything below. Neutral headers only: no `os/<platform>/` include (section 3, item 6). |

Later phases add modules above the renderer in the order `PLAN.md`'s diagram gives (text, UI, ECS, physics, audio, assets, scripting, Unreal import,
editor); each gets its row here when its phase is broken into days.

## 2. The measured graph (after Phase 12, 2026-11-07)

Measured by `module_deps_check` on the whole tree: **136 distinct module edges from includes, plus the CMake link lines, 0 problems.** There is no cycle and no
upward edge. Two allow-listed edges remain, both for Phase 13 Day 162 (the RHI backend registry):

| Edge | Where | Why it stays |
|---|---|---|
| rhi/vulkan → rhi/null | `rhi/vulkan/create_device.cpp` includes `rhi/null/null_device.h` | `CreateDevice` dispatches to the null backend from inside the Vulkan library; the registry needs the DX12 backend's shape to design well. |
| renderer → rhi/vulkan | `engine/renderer/CMakeLists.txt` links `rhi_vulkan` | The renderer reaches `CreateDevice`, `EnumerateAdapters` and `DeviceHost` through it. Consequence: `shader_mint` (which links `renderer`) still links the display libraries (`KNOWN-ISSUES.md`). |

The libraries follow the layers; each links only the ones below it (`docs/guide/building-and-testing.md`, "Libraries and layers"):

```
game/*, tools/*   ->  renderer, rhi, jobs, platform, foundation
renderer          ->  rhi, jobs, foundation (incl. settings, math)           (+ rhi/vulkan: allow list)
rhi/vulkan        ->  rhi, jobs, platform, foundation                        (+ rhi/null: allow list)
rhi/null          ->  rhi, foundation
rhi               ->  platform, foundation
jobs              ->  foundation
platform          ->  foundation_debug                                       (the only library linking Wayland, xkbcommon, X11)
foundation        =   foundation_settings -> foundation_debug -> foundation_io -> foundation_base     (INTERFACE aggregate of the four)
```

`foundation_base` holds layers 0-5, `foundation_io` layer 6, `foundation_debug` layer 7 (the only library linking libbacktrace and sentry-native), `foundation_settings`
layer 8; each has `_asan` and `_tsan` flavors. A test of a layer links that layer only, so an upward reference fails to link.

## 3. How the mismatches were fixed (Phase 12, Days 140-145)

The cycle measured on 2026-11-07 (nine foundation parts reaching each other through six upward includes, plus the three found by Day 140's re-measure) is gone.
The numbers are the items of the original table; the plan is `plans/phase-12-module-layering.md`.

1. **[Day 142, done] Assert failure report as a hook** (`log/diagnostics_hooks.h`: `FailureReportHooks`). `debug::InstallDiagnosticsHooks()` (called by
   `InitDebugFacilities`) installs the stack trace, crash context and debugger check; with none installed an assert prints its message and aborts. Removed item 1.
2. **[Day 143, done] `log::LoadConfig` retired**: Phase 11 made the log channels settings, so the file form had no caller. Removed item 2.
3. **[Day 142, done] Crash registration as a hook** (`SetThreadStartHook` / `NotifyThreadStart`): `io` and `jobs` call it, `debug` installs it; a thread started
   before installation is not registered later. Removed item 3.
4. **[Day 143, done] OS primitives below strings**: `StringView` is a base-layer type (`string_view.h`), `os::EnvironmentEntry` fills a caller buffer, `os.h` no
   longer includes `mem/heap_allocator.h`. Removed item 4 (and items 11-13 with `log.h`, `int128.h` and `span.h` including `string_view.h`).
5. **[Day 144, done] Windows and input are the platform layer** in `engine/platform/` (`platform/window.h`, `input.h`, `window_host.h`; namespace still `os::`), library
   `platform`, which alone links the display libraries; `PlatformWindowHost` has a neutral header. Removed items 5, 9 and 10 (except `shader_mint`, see above).
6. **[Day 143, done] `engine_main` moved** to `game/hello_window/`. Removed item 6.
7. **[Phase 13 Day 162] The RHI backend registry**: backends register with the neutral `rhi`; the renderer and `DeviceHost` stop linking `rhi_vulkan`, and `rhi/vulkan`
   stops including `rhi/null`. Removes items 7 and 8 (the two allow-list entries).
8. **[Day 145, done] One library per layer** (see section 2).
9. **[Day 141, done] The gate** keeps it this way (section 6).

## 4. Platform-specific code

- Neutral headers declare; per-platform directories implement (`os/<platform>/`, `io/<platform>/`, `os/<platform>/debug/`, `settings/<platform>/`,
  `rhi/<backend>/`). Each has a `BACKEND.md` checklist kept in step by a ctest.
- OS and graphics headers (`windows.h`, `wayland-client.h`, `X11/Xlib.h`, `vulkan.h`, `d3d12.h`, Metal) appear only in those directories
  (`platform_neutral_check`, `rhi_boundary_check`).
- Programs and neutral code never include a per-platform header. A per-platform directory may include the same platform's directory of a
  lower layer (`io/windows/` and `jobs/windows/` use `os/windows/win32.h` and `os/windows/utf16.h`; Phase 13 Days 151 and 153), named as an owner on its
  `platform-dir` line in `tools/module_deps_check/modules.txt`.

## 5. Third-party code

| Library | Used by | Boundary |
|---|---|---|
| Catch2 | tests only | `tests/` |
| libbacktrace | `debug/stack_trace_backtrace.cpp` (Linux) | `library_boundary.txt` |
| sentry-native (Breakpad) | `debug/crash_handler_sentry.cpp` | `library_boundary.txt` |
| nlohmann/json | `tools/shader_mint/reflect_json.cpp` and its test | `std_container_check` allow list, `PLAN.md` #19 |

## 6. Keeping this file true

A change that adds a module, moves one between layers or adds an allowed edge updates this file in the same commit. The gate **`module_deps_check`**
(`tools/module_deps_check`, ctest `module_deps_check`, phase 12) measures the graph on every run: `modules.txt` maps files to modules and layers (and lists
the same-layer edges a row permits, the forbidden pairs, the per-platform directories, the CMake targets and the system libraries only one module may link),
`check.cmake` resolves every `#include "..."` the way the compiler does and every `target_link_libraries` line, and `allowlist.txt` lists the known
violations (kind, edge, reason, the day that removes it). It fails on an upward edge, a same-layer edge without an `allow` line, a forbidden pair, a file
outside a platform directory's owners including a file in it, an include that resolves nowhere, a file or CMake target no module owns, a stale allow-list
entry, an entry without a reason and day, and any cycle left after the allow list is applied. A floor on the number of edges found keeps it from passing on
an empty graph. Its output is `module_deps_check: <what>: <from> (<layer>) -> <to> (<layer>): <file>:<line>`. A new module gets a `module` line there and a row
in section 1; a new violation is not added to the allow list (CLAUDE.md).
