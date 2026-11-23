# The engine as a loadable library: design and spike results

Phase 23, Day 342 (2026-11-15, Linux). Plan: `plans/phase-23-engine-as-library.md`. The planner's defaults below were confirmed by the user on 2026-11-15; the
spikes found nothing that revises them, and found six things the later days must handle (the end of "Spike results").

## Decisions

1. **One aggregate shared library** (`libengine.so.1`, `engine.dll`, `libengine.1.dylib`) for distribution; per-module shared targets only as a development
   option (`ENGINE_BUILD_SHARED_MODULES`, added only if the rebuild time ever hurts). One binary means one copy of every singleton and one export list.
2. **A hand-written flat C API behind one function table.** The only exported symbol is `engine_get_api(major, minor, table_size, &table)`; the table's `size`
   field says which functions exist, so a newer host detects an older engine and the reverse. The header carries `ENGINE_ABI_VERSION_MAJOR/MINOR`: a major
   change breaks (never made after v1 without a plan), a minor change only appends. Hosts load the library themselves (`engine_loader.h`, header-only C): no
   import library, no link-time dependency.
3. **Symbols hidden by default, and exported through a version script as well.** `-fvisibility=hidden` is not enough (Spike 1: the C++ standard headers and
   the vendored C libraries still export); the linker's version script (`abi/engine.exports`, local: `*`) is the export list, checked by `abi_check`.
4. **Errors:** `engine_status` is a small enum (`OK`, `INVALID`, `NOT_FOUND`, `UNSUPPORTED`, `IN_USE`, `BUSY`, `TOO_LARGE`, `CORRUPT`, `OFFLINE`, `CANCELLED`,
   `OUT_OF_MEMORY`, `VERSION`, `STALE_HANDLE`, `OTHER`), mapped from `asset::ErrorCode` and the other `Status` codes by one table with a completeness test;
   `engine_last_error(engine*)` gives the sentence of the last failure on the calling thread.
5. **Ownership:** strings are `(pointer, length)`, never NUL-required; a buffer the engine returns is an `engine_buffer {data, size, release}` freed through the
   engine's release function, never with the host's `free`; a buffer the host gives is copied unless the call says "borrowed with a release callback", which is
   called exactly once, after the last reader, on an engine worker thread named in the header; nothing allocated by one side is freed by the other.
6. **Callbacks** carry a `void* user`, run on the thread the header names (log: the calling thread; resource release: an engine worker; host requests such as
   cursor and clipboard: the thread that called `engine_frame`), and must not call back into the engine except the functions marked callback-safe.
7. **Handles** are 64 bits: 32-bit index and 32-bit generation; a stale handle fails with `STALE_HANDLE`, never crashes.
8. **Structs** that can grow start with `u32 struct_size` (and `u32 version` where a layout can change); the engine reads only the fields inside the size.
9. **What v1 covers:** version and caps, create and destroy with settings, logging hook, the frame loop, worlds and entities with the built-in components,
   jobs, the crash handler, resources and host-provided content, glTF import (a build option), the foreign surface and host-owned input. **Not in v1:**
   minting entry points (the host adds minted blobs), a game as a plugin loaded and reloaded by the host (the C ABI makes it possible; the work needs a
   second shutdown/reload path for game code and is left to a later phase), a stable C++ ABI, language bindings beyond the table (Phase 27 binds through it).
10. **iOS and consoles stay static.** `engine_c.h` compiles there; the loader does not.
11. **Embedding:** the host owns the window, its size, visibility, DPI and event loop, input and the clipboard. The foreign-surface descriptor is
    `{u32 struct_size, u32 kind, kind-specific handles, width, height, scale}` with kinds X11 (display and window), Wayland (`wl_display`, `wl_surface`: **the
    host's connection**, never a second one), Win32 (`HWND`), Cocoa (`NSView` or `CAMetalLayer`). `engine_caps` reports `SURFACE_*`, `READBACK`,
    `SHARED_TEXTURE`, `IMPORT_GLTF`; a backend that cannot do a mode returns `UNSUPPORTED`. **CPU readback comes first** (screenshots and tests);
    **shared GPU textures second** (Vulkan external memory, D3D12 shared handle, `IOSurface`), built on Day 356 or filed. The assumption that a host's toolkit
    exposes the raw native handle was confirmed by the user; if one does not, that toolkit uses readback or a shared texture.

## Spike results (Linux x86-64, Clang, PIC build of the engine's static libraries)

All three spikes used `foundation_*`, `jobs` and their vendored C libraries (sentry, breakpad, libunwind, libbacktrace) linked `--whole-archive` into one `.so`
with a version script, and tiny C hosts (`dlopen`, `dlsym`, `dlclose`). The code is throwaway (not committed); the results are what the days build on.

**Spike 1, a shared foundation.** It builds and runs: 1,000 jobs on a 4-participant scheduler twice in one process, logging, `settings::Registry`, the crash
facade linked in. Size 4.8 MiB with debug info (the whole foundation, debug facade and its C libraries). Findings:
(a) *the engine libraries are not PIC today*: a PIC build needs `CMAKE_POSITION_INDEPENDENT_CODE` for every module (Day 343);
(b) *`-fvisibility=hidden` leaves about 500 symbols exported* (the standard library's inline templates, `sentry_*`, `unw_*`): the version script is the export
list (Decision 3);
(c) *a `.so` that exports C++ standard-library symbols is pinned in memory*: `ld.so` marks libstdc++ NODELETE through a relocation dependency on it; with only the
version script's symbols exported the dependency is gone (`LD_DEBUG=files` shows it);
(d) `RTLD_LOCAL` and `RTLD_GLOBAL` behave the same for a library that exports eight C symbols; the loader uses `RTLD_LOCAL | RTLD_NOW`;
(e) the crash facade's signal handlers and the job scheduler's threads need an explicit shutdown (Day 348).

**Spike 2, one of everything.** A second library that links the static foundation itself ("DUP") and one that calls the engine's exports only ("SHARED"):
| Singleton | DUP | SHARED |
|---|---|---|
| log ring and `LogLinesWritten()` | two (engine 1, plugin 1) | one (2 and 2) |
| `HeapAllocator::Default()` (a function-local static in a header) | two addresses | one |
| `jobs::Counter::PathStats()` (a function-local static in a header) | two | one |
Two libraries that each carry the foundation therefore see two logs, two allocators and two scheduler worlds: the rule is **one engine library; a plugin links
the C API only**. The survey of `engine/` found the state that must live once: the log (ring, sink, assert handler, ensure-site list, thread-start hook,
channel list), the crash facade (state entries, providers, context, handler, signal arrays), the debugger state, the module list, `os` command-line state, the
job system's thread-locals (`tls_current`, `tls_job_depth`, the frame pool's `ThreadCache`), settings' `g_depth`, the Vulkan backend's thread-locals; and
**in headers** (duplicated per library under hidden visibility): `HeapAllocator::Default()` (stateless, harmless), `jobs::Counter::PathStats()`, the
`EnsureSite` statics of `ENGINE_ENSURE` (per call site, registered into the log's list), and **`ecs::World`'s `static U tag`** (the component type identity by
address: a host that instantiated `World::Add<T>` in its own code would get other ids than the engine's; the C API forbids C++ component access across the
boundary, and the shared-state gate (Day 345) bans new header statics). The settings registry is an object, not a global: one per engine instance.

**Spike 3, sanitizers.** A sanitized engine library loads only into a host built with the same sanitizer (Clang's runtime lives in the executable:
`undefined symbol: __asan_report_store4` / `__tsan_vptr_read` otherwise) or with the runtime `LD_PRELOAD`ed; the C host tests therefore build the host with
`-fsanitize=address` or `thread` in those flavors. AddressSanitizer and ThreadSanitizer run the jobs and the log through `dlopen` without reports. Known
limit: ThreadSanitizer cannot follow libpng's `longjmp` (Phase 22, Day 338), which matters only when the importer is in the library.

**Unload.** The library leaves the address space after `dlclose` when no engine code ran on the host's calling thread; when it did, `ld.so` keeps it mapped
(it counts the `thread_local` with a non-trivial destructor that was created on that thread: the job system's frame-pool cache and the crash facade's alternate
stack) until that thread exits. The same calls made from a host thread that was joined first unload cleanly. **Decision:** on a host thread the engine registers
no thread-local destructor: `thread_local` objects on the engine's API path are trivially destructible and are released by an explicit `engine_thread_detach`
and by `engine_destroy` for the calling thread; Day 345 converts the two offenders, Day 348 tests unload from the main thread.

## Consequences for the days

- Day 343: PIC for every module and vendored library; the aggregate target links `--whole-archive`.
- Day 344: version script plus `-fvisibility=hidden`; `abi_check` reads the dynamic symbol table; the export list is tiny (the table's entry point).
- Day 345: the shared-state gate; convert the non-trivial thread-locals; the dup-singleton test is Spike 2 made permanent.
- Day 348: the unload test runs on the main thread, under asan and tsan.
- Days 347 and later: the C hosts of the sanitizer flavors are built with the sanitizer.

## Day 343 results: the shared build on Linux

`-DENGINE_BUILD_SHARED=ON` (Clang, unity; also checked non-unity and with GCC, below): `engine`, `engine_asan`, `engine_tsan`, each the whole-archive of the
24 module static libraries of its flavor (every library under `engine/` except tests; the third-party C libraries join through the modules' usage
requirements and are position independent through `CMAKE_POSITION_INDEPENDENT_CODE`, set before any target). `SONAME libengine.so.1`, files `libengine.so.1.0`.
The version script exports one symbol (`engine_shared_probe`, which runs jobs: Day 346 replaces it with `engine_get_api`).

| Measure | Result |
|---|---|
| Size, debug build (no optimization, `-g`): plain / asan / tsan | 25.3 / 56.9 / 28.5 MB |
| Exported symbols (`nm -D --defined-only`) | 1 (`engine_shared_probe@@ENGINE_1`) |
| Load and scheduler start through `libengine.so.1` (`ldprobe` with 0 jobs) | 2 ms (an empty C program: under 1 ms) |
| A C program linking it runs 2,000 jobs, plain, asan, tsan | passes |
| Static default (`ENGINE_BUILD_SHARED=OFF`) | no compile command changed: `ninja -n` after the change rebuilds nothing, so `renderer_bench` and `ecs_bench` cannot differ; no figures were taken |

Left for later days: the second set of the engine's own test suites linked against the shared library (Day 356 runs the whole suite shared; the mode argument of
`engine_add_*_tests` is added then, where the suites are run), the install rule (a `libengine.so.1` install with the headers belongs with the C API on Day 346),
`-Wl,-rpath` for installed hosts, and sizes and start-up of a Release build (Day 356's benchmarks).

## Day 356 results: the cost of the shared build, and the fallbacks

Release builds (`CMAKE_BUILD_TYPE=Release`, Clang, unity, Linux x86-64): `build-rel` (static default) and `build-rel-shared` (`-DENGINE_BUILD_SHARED=ON`).

| Measure | Static default | Shared build |
|---|---|---|
| `libengine.so.1.0` size | not built | 7.7 MB |
| `libengine_c.a` (the C API layer alone) | 306,320 bytes | 306,456 bytes |
| Start-up, whole `engine_c_api_bench` process (create, 5 measured loops, destroy) | 0.27 s | 0.27 s |
| `ecs_bench` iteration at 100k entities, speed-up over the baseline (pass: 3.0) | x9.07 | x9.59 |
| `ecs_bench` parallel efficiency, 4 workers (pass: 70%) | 84.6% | 84.8% |
| `renderer_bench` (needs a GPU: built, not run) | built | built |

`ecs_bench` and `renderer_bench` link the engine's modules as static libraries in both trees (they are C++ programs and cannot link a hidden-symbol
library), so their two columns are the same code: the difference above is run-to-run noise, and **the static default did not get slower** (the shared
option changes only position-independence flags, which x86-64 does not charge for). The Phase 22 asset benchmarks (`[.][asset-bench]`) link the same way and
were not repeated.

Per-call cost of the C API (`engine_c_api_bench`: a C host calling the function table; Release, ns per call):

| Call | Linked statically (`libengine_c.a`) | Through `libengine.so.1` | Static tree, same C host |
|---|---|---|---|
| `frame` (one empty world) | 99.1 | 89.9 | 94.8 |
| entity create + destroy | 35.6 | 32.8 | 35.1 |
| `component_set` (transform) | 27.5 | 26.0 | 26.5 |
| `component_get` (transform) | 17.6 | 15.5 | 17.8 |
| `resource_mounted` (absent) | 44.0 | 42.9 | 46.2 |

The function-table call (one indirect call plus handle validation) costs 15-45 ns; going through the shared library is within the noise of the static
link, because the table is the only crossing and it is an indirect call in both. A resource *load* is not timed: it is dominated by file I/O, which the
C layer does not change. The debug build is about 8 times slower (frame 715 ns), so only Release figures are quoted.

### Shared GPU textures: not built

The second embedding fallback (the host composites the engine's output itself) needs `VK_KHR_external_memory_fd` on Linux, a shared handle on D3D12
and `IOSurface` on macOS. Not built in this phase: the primary path (render into a host-owned window, Days 349-354) and the read-back path
(`surface_read_pixels`, Day 352) already cover every host the phase names; an importing host needs its own device from the same physical GPU with
matching external-memory handle types, a synchronization contract (semaphore export) that the C API has no vocabulary for yet, and a test that creates a
second Vulkan instance in the C host (the host would need the Vulkan loader as a test dependency). There is no `engine_caps` bit for it (a bit claiming a
feature that is absent would be a lie, and a bit added later is free: size-prefixed structs and the caps word grow). Phase 24's scenes-for-hosts days are the place
to revisit it when a host names the need.

### The plugin question: not in this phase

A game as a loadable library that the host reloads while running is **not in this phase**. The C ABI makes it possible (a game would be a second library
that receives the function table through `engine_get_api`, exactly as a host does), and the `engine_reload_cycles` tests already prove the engine itself
unloads and reloads cleanly. What it would need: a game-side entry-point contract, ownership of components registered by a module that is about to unload
(their type descriptors point into its code), a quiesce point where no job of the module is running, and a state hand-over format. That is Phase 24-29
scope and is decided with the first host that asks.

### The suites against the shared build: a deviation from the day file

The C++ suites include internal headers and call hidden functions, so they cannot link `libengine.so` (hidden visibility and a one-symbol export list are
the point of Day 344). Day 343's "mode argument" is therefore not built. The suite was run in the shared configuration instead: `build-shared` configured with
`-DENGINE_BUILD_SHARED=ON` builds every test as in the static tree (against the static libraries) and, in addition, builds `libengine.so` and every
C host (`tests/engine_library/*.c`) against it, so the C-side tests exercise the shared library and the C++ side proves the shared option disturbs nothing.
Result: `scripts/test-phase.sh --all --flavor all --build-dir build-shared`: `ran 8915 of 8915 tests (phases: all; flavors: all; display: included; crash tests: included)`
in 263 s, with four failures that were bugs of the shared configuration, now fixed:

- `debug_test_plugin` (3 tests): the MODULE the debug tests `dlopen` was compiled with the shared build's hidden visibility, so `dlsym` could not find its entry
  points. The target now sets `CXX_VISIBILITY_PRESET default` (`tests/debug/CMakeLists.txt`).
- `engine_reload_cycles_tsan`: a flake (not reproducible alone or 24 in parallel) where the first load/unload cycle counted the scheduler's worker threads
  before they had finished starting; `host_reload.c` now waits for the thread count to settle on the first cycle (`waited_first_ms`).
