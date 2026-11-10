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
| 0. Base | `foundation/lang` (Phase 16), `foundation/sync` (Phase 16), `foundation/cstr` (Phase 16), `foundation` root headers: `types.h`, `int128.h`, `build_defs.h`, `debug/compiler.h` | Fixed-width types, 128-bit integers, platform/compiler/processor/sanitizer macros, compiler constructs. Depends on nothing. |
| 1. OS primitives | `foundation/os/os.h` (+ `os/<platform>/os.cpp`) | Descriptors, memory reservation, clocks, sleep, dynamic libraries, the environment. Base only. |
| 2. Memory | `foundation/mem` | Heap, arena, pool and stack allocators. Base, OS primitives. |
| 3. Assert and log core | `foundation/log` | `ENGINE_ASSERT` family, log channels and sinks to descriptors. Layers 0-2. The assert *failure report* (stack trace, crash context) belongs to debugging (layer 7), reached through a hook (section 3, item 1). |
| 4. Containers and strings | `foundation/containers`, `foundation/string`, `algorithm.h`, `foundation/math` | `Vector`, `HashMap`, `Span`, `String`, UTF-8, graphemes, normalization, `Sort`, vectors and matrices. Layers 0-3. |
| 5. OS services | `foundation/os` (`command_line.h`, `session.h`, `gpu_driver_probe.h`) | Command line, session state, GPU driver probe. Layers 0-4. |
| 6. File layer | `foundation/io` (+ `io/<platform>/`) | `Path`, `File`, `Directory`, file-system operations, special folders. Layers 0-5. |
| 7. Debugging | `foundation/debug` (+ `os/<platform>/debug/`) | Stack traces, crash handler, crash context and state, death tests, dumps. Layers 0-6; libbacktrace and sentry-native only in its two facade files (`library_boundary.txt`). |
| 8. Settings | `foundation/settings` | Registry, stores, layers, the `Settings` service. Layers 0-7. |
| 9. Platform: windows and input | `engine/platform` (`window.h`, `input.h`, `window_host.h`, `platform/<platform>/` backends) | Windows, input state, display backends (Wayland, X11, Win32), the CMake library `platform`, the only one that links the display libraries. Layers 0-8. Moved here from the foundation on Phase 12 Day 144. |
| 10. Jobs, text | `engine/jobs`; `engine/text` (Phase 14) | Jobs: scheduler, coroutines, frame pool, async I/O. Text: font faces, glyph rasterization and atlas packing, layout, cursor and hit-testing; FreeType (vendored) only in `text/font_freetype.cpp`, the CPU side only (the atlas texture and the text pass are in the renderer). Both depend on the foundation (layers 0-8) and not on each other. |
| 11. UI toolkit (CPU) | `engine/ui` (Phase 15) | Widget tree, surfaces, layout, themes, input routing and the 2D draw list; no RHI type (the renderer turns the draw list into a pass; images are opaque `ImageId`s). Depends on text, the platform's input headers (the header-only `platform_input`, no display library) and the foundation. Sits beside the RHI and depends on none of it. |
| 11. RHI | `engine/rhi` (neutral, with the backend registry, `CreateDevice` and `DeviceHost`), `engine/rhi/null`, `engine/rhi/vulkan`, later `rhi/dx12`, `rhi/metal`; `engine/rhi/backends` | The neutral RHI, and one library per backend. Backends depend on `rhi`, jobs and foundation, never on each other; each registers with the neutral registry. `rhi/backends` (library `rhi_backends`, Phase 13 Day 162) is the only module that names every backend, to register them; programs link it, the renderer never does. Graphics API headers only inside their backend (`rhi_boundary_check`). |
| 12. Renderer | `engine/renderer` (+ `renderer_passes`) | Render graph, materials, passes, and from Phase 14 the glyph atlas texture and the text renderer. RHI (neutral only), jobs, text (the CPU atlas and layout types), foundation including settings. |
| 12. UI window adapter | `engine/ui_window` (Phase 15 Day 194) | `WindowAdapter`: drains a window's input events into a surface (pointer positions divided by the scale, resize, focus) and sets the window's cursor from the surface. The only module naming both `ui` and `os::Window`. Depends on ui, platform and the foundation. |
| 13. Programs | `game/*`, `tools/*`, `tests/*` | Demos, benches, tools and tests. Anything below. Neutral headers only: no `os/<platform>/` include (section 3, item 6). |

Later phases add modules above the renderer in the order `PLAN.md`'s diagram gives (text, UI, ECS, physics, audio, assets, scripting, Unreal import,
editor); each gets its row here when its phase is broken into days.

## 2. The measured graph (after Phase 12, 2026-11-07)

Measured by `module_deps_check` on the whole tree: **172 module edges from includes, plus the CMake link lines, 0 problems.** There is no cycle and no
upward edge. The last two allow-listed edges (rhi/vulkan → rhi/null and renderer → rhi/vulkan) were removed by the RHI backend registry on Phase 13 Day 162;
the allow list is empty.

The libraries follow the layers; each links only the ones below it (`docs/guide/building-and-testing.md`, "Libraries and layers"):

```
game/*, tools/*   ->  renderer, rhi_backends, rhi, jobs, platform, foundation
renderer          ->  rhi, jobs, text, ui (from Phase 15 Day 190), foundation (incl. settings, math)
ui                ->  text, platform_input (header-only), foundation
ui_window         ->  ui, platform, foundation                               (Phase 15 Day 194)
rhi_backends      ->  rhi/vulkan, rhi/null (in rhi), later rhi/dx12          (registers each backend; linked by programs)
rhi/vulkan        ->  rhi, jobs, platform, foundation
rhi/null          ->  rhi, foundation
rhi               ->  platform, foundation
jobs              ->  foundation
text              ->  foundation (FreeType, through one file)
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
7. **[Phase 13 Day 162, done] The RHI backend registry** (`rhi/backend_registry.h`): backends register with the neutral `rhi` through an explicit call
   (`rhi::RegisterCompiledBackends`, library `rhi_backends`), `CreateDevice`, `EnumerateAdapters` and `DeviceHost` live in `rhi`, the renderer links `rhi` only,
   and `rhi/vulkan` no longer includes `rhi/null`. Removed items 7 and 8 (the two allow-list entries).
8. **[Day 145, done] One library per layer** (see section 2).
9. **[Day 141, done] The gate** keeps it this way (section 6).

## 4. Platform-specific code

- Neutral headers declare; per-platform directories implement (`os/<platform>/`, `io/<platform>/`, `os/<platform>/debug/`, `settings/<platform>/`,
  `rhi/<backend>/`). Each has a `BACKEND.md` checklist kept in step by a ctest.
- OS and graphics headers (`windows.h`, `wayland-client.h`, `X11/Xlib.h`, `vulkan.h`, `d3d12.h`, Metal) appear only in those directories
  (`platform_neutral_check`, `rhi_boundary_check`).
- Programs and neutral code never include a per-platform header. A per-platform directory may include the same platform's directory of a
  lower layer (`io/windows/` and `jobs/windows/` use `os/windows/win32.h` and `os/windows/utf16.h`; Phase 13 Days 151 and 153; the `crash_extract` tool's
  `tools/crash_extract/windows/` uses `win32.h` for DbgHelp, Day 156; `settings/windows/`, the registry store, uses `win32.h` and `utf16.h`, Day 157; `platform/windows/`, the Win32 window and the windowed programs'
  entry point, the same two, Day 158; `rhi/vulkan/windows/`, the Vulkan backend's Win32 surface, `win32.h`, Day 160;
  `tools/docgen/src/windows/`, the sample runner's child processes, `win32.h` and `utf16.h`, Day 161), named as an owner on its
  `platform-dir` line in `tools/module_deps_check/modules.txt`.

## 5. Third-party code

| Library | Used by | Boundary |
|---|---|---|
| Catch2 | tests only | `tests/` |
| libbacktrace | `debug/stack_trace_backtrace.cpp` (Linux) | `library_boundary.txt` |
| sentry-native (Breakpad) | `debug/crash_handler_sentry.cpp` | `library_boundary.txt` |
| nlohmann/json | `tools/shader_mint/reflect_json.cpp` and its test | `std_container_check` allow list, `PLAN.md` #19 |

### Planned modules (Phase 16, standard-library wrappers; not built yet)

`foundation/lang` and `foundation/sync` (layer 0, no dependencies; traits, `Move`, `Limits`, scalar math; `Atomic<T>`, `Mutex`, `Thread`), `foundation/cstr` (also layer 0 in `tools/module_deps_check/modules.txt`, because `types.h` and `lang` need `mem_ops.h`; C strings, formatting, number parsing, raw memory),
and the wrapper functions that join `foundation/mem` (memory operations), `foundation/os` (console output, process, environment, signals, time) and `engine/jobs` (`CoroutineHandle`). Each row is added to section 1 and to
`tools/module_deps_check/modules.txt` on the day that builds it (Days 208-215); `docs/superpowers/specs/2026-11-09-std-wrappers-design.md` has the layer reasoning.

### Planned modules (Phase 17, ECS; not built yet)

`engine/ecs` (layer 10, beside `jobs` and `text`; depends on `foundation` and `jobs` with the edge `ecs -> jobs` allowed; no renderer, RHI or platform header): entities, component registry, archetype chunks, world, queries, command buffers, change detection, systems and the
parallel scheduler, transforms and hierarchy. `engine/scene` (layer 13, above `ecs` and `renderer`; nothing below includes it): `MeshRenderer`, `Light`, `Camera`, `RenderHandle` and the `RenderSync` system that mirrors entities into `renderer::RenderScene`.
Each row is added to section 1 and to `tools/module_deps_check/modules.txt` on the day that builds it (Day 223 for `ecs`, Day 233 for `scene`); `docs/superpowers/specs/2026-11-10-ecs-design.md` has the reasoning.

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
