# ARCHITECTURE.md — Modules, layers and dependencies

The engine's modules, the layer each belongs to, what each may depend on, and how the code measures against that. `PLAN.md` gives the mission and
the long-term layer diagram; this file is the **source of truth for dependencies between the modules that exist**. It is measured from the code, not
written from intent, and each mismatch is listed with what fixes it.

Written 2026-11-07 (before Phase 12), from the `#include` graph of `engine/`, `game/` and `tools/` and the CMake link lines. The rule: **a module
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
| 9. Platform: windows and input | `foundation/os/window.h`, `input.h`, `os/<platform>/` window backends and `PlatformWindowHost` | Windows, input state, display backends (Wayland, X11, Win32). Layers 0-8. **`PLAN.md` puts this in `engine/platform/`, which is empty** (section 3, item 5). |
| 10. Jobs | `engine/jobs` | Scheduler, coroutines, frame pool, async I/O. Foundation (layers 0-8). |
| 11. RHI | `engine/rhi` (neutral), `engine/rhi/null`, `engine/rhi/vulkan`, later `rhi/dx12`, `rhi/metal` | The neutral RHI, and one library per backend. Backends depend on `rhi`, jobs and foundation, never on each other. Graphics API headers only inside their backend (`rhi_boundary_check`). |
| 12. Renderer | `engine/renderer` (+ `renderer_passes`) | Render graph, materials, passes. RHI (neutral only), jobs, foundation including settings. |
| 13. Programs | `game/*`, `tools/*`, `tests/*` | Demos, benches, tools and tests. Anything below. Neutral headers only: no `os/<platform>/` include (section 3, item 6). |

Later phases add modules above the renderer in the order `PLAN.md`'s diagram gives (text, UI, ECS, physics, audio, assets, scripting, Unreal import,
editor); each gets its row here when its phase is broken into days.

## 2. The measured graph (2026-11-07)

**Above the foundation the graph is acyclic and already follows the table:**

```
game/*, tools/*  ->  renderer, rhi, rhi/null, jobs, foundation
renderer         ->  rhi, jobs, foundation (incl. settings, math)
rhi/vulkan       ->  rhi, jobs, foundation          (+ rhi/null: item 7)
rhi/null         ->  rhi, foundation
rhi              ->  foundation
jobs             ->  foundation
```

**Inside the foundation, nine parts form one cycle:** base, `mem`, `os`, `os/linux`, `log`, `containers`, `string`, `io` and `debug` all reach each other.
`settings` and `math` sit outside the cycle (nothing in the foundation depends on them). The cycle is why `foundation` is a single static library
(`engine/foundation/CMakeLists.txt`), why no part of it can be built, tested or ported alone, and why Phase 12's plan could not run a Windows test until
the OS, file and debugging backends were all ported.

Most edges in the cycle go downward by the table and are fine (`containers -> mem`, `debug -> io`, `mem -> os.h`). The cycle exists because of these
upward edges (section 3 says how each is fixed):

| # | Upward edge | Where | Why it is there |
|---|---|---|---|
| 1 | log (3) → debug (7) | `log/log.cpp` includes `debug/stack_trace.h`, `crash_report.h`, `crash_context.h`, `debugger.h` | The assert failure path captures a stack trace, writes the crash context and checks for a debugger. |
| 2 | log (3) → io (6) | `log/log.cpp` includes `io/file.h` | `log::LoadConfig` reads a channel configuration file. |
| 3 | io (6) → debug (7) | `io/file.cpp` includes `debug/crash_handler.h` | The I/O thread calls `debug::RegisterThreadForCrashHandling("io")`. |
| 4 | OS primitives (1) → strings (4), memory (2) | `os/os.h` includes `string/string.h`, `mem/heap_allocator.h` | Paths are `StringView`s; `EnvironmentEntry` fills a `String<HeapAllocator>`. |
| 5 | platform (9) code inside the foundation library | `os/linux/platform_window_host.cpp` includes `debug/crash_state.h` (fine by layer) but is compiled into `foundation` | Windows and input live in `foundation/os/`, so the window backends and their display libraries are part of every foundation consumer. |
| 6 | program inside a library directory | `engine/foundation/main.cpp` (`engine_main`) includes `os/linux/platform_window_host.h` | The Phase 1 hello-window program lives beside the library sources. |

Measured edges outside the cycle that break a rule:

| # | Edge | Where | Rule broken |
|---|---|---|---|
| 7 | rhi/vulkan → rhi/null | `rhi/vulkan/create_device.cpp` includes `rhi/null/null_device.h` | Backends never depend on each other: `CreateDevice` dispatches to the null backend from inside the Vulkan library (no backend registry yet). |
| 8 | renderer links rhi_vulkan | `engine/renderer/CMakeLists.txt` | The renderer may use only the neutral RHI; it links the Vulkan library to reach `CreateDevice`, `EnumerateAdapters` and `DeviceHost`. |
| 9 | programs include `os/linux/` | `game/rhi_demo/main.cpp`, `game/renderer_demo/main.cpp` include `os/linux/platform_window_host.h` | Programs use neutral headers only; these do not compile on Windows. |
| 10 | display libraries are PUBLIC on foundation | `target_link_libraries(foundation PUBLIC PkgConfig::WaylandClient ... X11Client)` | Every consumer, including `docgen` and `shader_mint`, links Wayland and X11. A consequence of item 5. |

Placement only (not a dependency problem): `algorithm.h` is in the foundation root but uses `Span` and the heap allocator, so it is layer 4;
`int128.h`'s `FromText` takes a `StringView`, so either it moves to layer 4 or `FromText` moves to the string module.

## 3. How each mismatch is fixed (proposed; to be scheduled)

1. **Assert failure report as a hook.** `log` keeps the assert macros and a minimal failure path (message to standard error, then abort). The debugging
   module installs the full report (stack trace, crash context, debugger break) at start-up, through the existing handler slot (`SetAssertHandler`) or a
   dedicated report hook. Asserts that fire before installation still print and abort. Removes item 1.
2. **Log configuration above the file layer.** `log::LoadConfig`'s file reading moves out of `log` (Phase 11 made the log channels settings, so the file
   form may be retired or moved into `settings`). Removes item 2.
3. **Crash registration as a hook.** The job system and the I/O thread register threads through a thread-start hook the debugging module installs, not by
   calling into `debug` from `io`. Removes item 3.
4. **OS primitives below strings.** `os.h` takes paths as pointer and length (or a base-layer `StringView` moved to layer 0), and `EnvironmentEntry` writes into
   a caller buffer. Removes item 4.
5. **Windows and input become the platform layer.** `window.h`, `input.h`, `key_translate.cpp`, `PlatformWindowHost` and the display backends move to
   `engine/platform/` (as `PLAN.md` always said) as their own library, which alone links Wayland, X11 or Win32. `PlatformWindowHost` gets a neutral header.
   Removes items 5, 9 and 10.
6. **`engine_main` moves** to `game/hello_window/` (or is retired if the demos cover it). Removes item 6.
7. **The RHI backend registry** (already Phase 12 Day 155): backends register with the neutral `rhi`; the renderer and `DeviceHost` stop linking
   `rhi_vulkan`, and `rhi/vulkan` stops including `rhi/null`. Removes items 7 and 8.
8. **One library per layer** once the cycle is gone: `foundation` becomes a set of libraries (for example `foundation_base` with layers 0-5,
   `foundation_io`, `foundation_debug`, `foundation_settings`), with `foundation` kept as the aggregate consumers link today; and `platform` beside them. A port
   can then build and test each layer as soon as its backend exists.
9. **A gate keeps it this way:** a `module_deps_check` ctest computes the include graph, maps files to the modules of section 1 and fails on an upward edge, a
   cycle or a program including `os/<platform>/`, with an allow list (a reason per entry, stale entries fail) that starts with the items above still open.

## 4. Platform-specific code

- Neutral headers declare; per-platform directories implement (`os/<platform>/`, `io/<platform>/`, `os/<platform>/debug/`, `settings/<platform>/`,
  `rhi/<backend>/`). Each has a `BACKEND.md` checklist kept in step by a ctest.
- OS and graphics headers (`windows.h`, `wayland-client.h`, `X11/Xlib.h`, `vulkan.h`, `d3d12.h`, Metal) appear only in those directories
  (`platform_neutral_check`, `rhi_boundary_check`).
- Programs and neutral code never include a per-platform header.

## 5. Third-party code

| Library | Used by | Boundary |
|---|---|---|
| Catch2 | tests only | `tests/` |
| libbacktrace | `debug/stack_trace_backtrace.cpp` (Linux) | `library_boundary.txt` |
| sentry-native (Breakpad) | `debug/crash_handler_sentry.cpp` | `library_boundary.txt` |
| nlohmann/json | `tools/shader_mint/reflect_json.cpp` and its test | `std_container_check` allow list, `PLAN.md` #19 |

## 6. Keeping this file true

A change that adds a module, moves one between layers or adds an allowed edge updates this file in the same commit. Until the `module_deps_check` gate
exists (section 3, item 9), the graph in section 2 is re-measured by hand when a phase starts.
