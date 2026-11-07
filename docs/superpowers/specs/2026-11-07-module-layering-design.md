# Module layering — design (Phase 12, Day 140)

Status: settled for implementation, 2026-11-07. Decisions D1-D9 below confirm or amend the seven in `plans/phase-12-module-layering.md`.
The rules and the measured graph are in `ARCHITECTURE.md`; this file says how the code is brought to them.

## 1. Re-measured graph (Linux, 2026-11-07)

Method: every `#include "..."` in `engine/`, `game/` and `tools/` (test fixtures excluded) resolved first against the including file's directory, then
against the include directories (`engine/foundation`, `engine`, `engine/jobs`, the repo root, `game`, `shaders`); files mapped to the modules of
`ARCHITECTURE.md` section 1; strongly connected groups by Tarjan's algorithm. 13 quoted includes do not resolve (four are generated Wayland files that
include the system `wayland-*.h`; nine are `game/` headers found through a CMake include directory of the demos); none is a system header.

**One strongly connected group, as `ARCHITECTURE.md` says:** `algorithm`, base, `containers`, `debug`, `io`, `log`, `mem`, `os`, `string`.
The window code is not in it (nothing in the cycle includes it) but it is compiled into the same library.

**Three edges the document did not list** (the cycle was already there; these keep it connected even after items 1-4 are fixed):

| Edge | Where | Fix |
|---|---|---|
| log → string | `log/log.h` includes `string/string.h` for `StringView` | D3: `StringView` moves to the base layer |
| containers → string | `containers/span.h` includes `string/string.h` (and `string.h` includes `containers/vector.h`) | D3 |
| base → string, log | `int128.h` includes `string/string.h` (`FromText(StringView)`); `int128.cpp` includes `log/log.h` | D3 and D4 |

Everything else is as `ARCHITECTURE.md` section 2 states: log → debug (4 includes in `log/log.cpp`), log → io (`log.cpp:2`), io → debug (`io/file.cpp:10`),
os → string and mem (`os/os.h:2,4`), the window files inside the foundation library, `engine_main` in the library directory, the demos including
`os/linux/platform_window_host.h`, and the display libraries linked PUBLIC. Other findings: `jobs` also registers its worker threads with the crash handler
(`jobs/worker_pool.cpp:91`, `jobs/async_io.cpp:56`; allowed by layer, but they use the same hook as `io`), and `rhi/device.h`, `rhi/rhi.h` and
`rhi/vulkan/vulkan_device.h` include `os/window.h` (allowed: platform is below the RHI).

Link edges before the split: `jobs`, `rhi`, `tools/docgen`, `tools/crash_extract` link `foundation`; `renderer` links `rhi_vulkan`, `rhi`, `jobs` and
`foundation` (rule break, `ARCHITECTURE.md` item 8); `foundation` links `backtrace` and `sentry` (PRIVATE) and the Wayland, xkbcommon and X11 libraries (PUBLIC).

## 2. Decisions

**D1. Libraries** (confirms phase decision 1). `foundation_base` = layers 0-5, `foundation_io` = layer 6, `foundation_debug` = layer 7,
`foundation_settings` = layer 8, `platform` = layer 9 (new, `engine/platform/`); `foundation` and the sanitizer variants stay as `INTERFACE` aggregates.
Sources of each are fixed on Day 145 from the `modules.txt` map of the gate (Day 141), which is the single list of which file is in which module.

**D2. Hooks installed from above** (confirms decision 2). Two small registrations in one base-layer header, `log/diagnostics_hooks.h`:

- `FailureReportHooks` (a struct of three plain function pointers: append a stack trace to a buffer, write the crash context, ask whether a debugger is
  attached) installed with `SetFailureReportHooks(const FailureReportHooks*)`. The pointer is stored in an atomic and the struct must have static storage
  duration. `DefaultAssertHandler` and the error-trace path of `Log` use the hooks when present; with none installed the assert prints its one message line to
  the write sink and returns `Abort` (`Ensure`: `Continue`), no trace and no context, allocating nothing and taking no lock. `SetAssertHandler` is unchanged and
  keeps its role (tests and tools replace the handler; the hooks only fill in the default one).
- `SetThreadStartHook(void (*)(const char* name))` and `NotifyThreadStart(const char* name)`. `io`'s I/O thread and the `jobs` workers call
  `NotifyThreadStart` first thing on the new thread. Rule for threads started before installation: **they are not registered later.** Registration already
  returns false until the crash handler is installed (`tests/debug/crash_state_test.cpp`: `RegisterThreadForCrashHandling("early-thread")` is false), so a
  thread started before setup was never covered either; no replay list is kept.
- Installation: `debug::InitDebugFacilities` installs both (it is idempotent and already called first by `StartProgramDebugging` and lazily by the stack
  trace code), through `debug::InstallDiagnosticsHooks()` which is also callable alone. Installation is a release-store of one pointer; a failure on another
  thread during installation sees either the old or the new table, both complete.
- Frame counts: the hook adapter is one frame deeper than the old inline call; it passes `skip + 1` and is `ENGINE_NOINLINE`.

**D3. `StringView` is a base-layer type** (amends decision 3: it moves, as the phase file allowed). New header `string_view.h` (foundation root, base layer,
needs only `<cstring>` and `types.h`); `string/string.h` includes it, so every existing user keeps compiling. `os/os.h`, `log/log.h`, `int128.h` and
`containers/span.h` include `string_view.h` and no longer include `string/string.h`. `log/assert.h` (includes only `debug/compiler.h` and `types.h`) is treated
as base-layer in the gate's map, because `containers/vector.h` and `StringView` users need it.

**D4. `os.h` below strings, `int128` placement.** Paths stay `StringView`. `EnvironmentEntry(usize index, String<HeapAllocator>*)` becomes
`EnvironmentEntry(usize index, char* buffer, usize capacity, usize* needed)`-style (caller buffer, reports the needed size; exact shape and `Fails:` on Day 143).
`os.h` no longer includes `mem/heap_allocator.h`. `int128.h`'s `FromText` takes the base `StringView`; `int128.cpp` stops including `log/log.h` (it includes
`log/assert.h`, base), so `int128` stays in the base layer. `algorithm.h` is mapped to layer 4 (it uses `Span` and the heap allocator); it stays in the
foundation root, listed in the gate's map.

**D5. Log configuration.** `log::LoadConfig` reads a file for the `<Channel>.verbosity/enabled` form; its callers are `debug/program_setup.cpp` and
`tests/foundation/log_test.cpp`. Phase 11 made the log channels settings (`--set log.<channel>...`, settings files, environment). Day 143 checks that
`program_setup.cpp` can drop the call; if it can, the file form is retired (the guide says what replaces it) and `log` loses its last `io` include. If the
check shows a use the settings do not cover, the file reading moves to `foundation_settings`.

**D6. Windows and input move to `engine/platform/`** (confirms decision 4). Namespace: **keep `os::`** (25 files use `os::Window`, `os::Input` and friends;
the same files also use `os::Write` and the rest of `os.h`, so renaming would touch every one for no layering gain). Include prefix: `platform/`
(`platform/window.h`, `platform/input.h`, `platform/window_host.h`). Include sites to update: 17 files inside the moved code, `rhi/device.h`, `rhi/rhi.h`,
`rhi/vulkan/vulkan_device.h`, `game/rhi_demo/main.cpp`, `game/renderer_demo/main.cpp`, `engine/foundation/main.cpp`, nine tests, one guide page, plus the
patterns in `tests/phases.txt` (none: tests are named by file, the window tests keep phase 1), `tests/module_map.txt`, `docs/reference/modules.txt` and the
gate allow lists. No forwarding headers.

**D7. `engine_main`.** Day 143 lists what it does that the demos do not and either moves it to `game/hello_window/` or retires it; the foundation directory
holds no program afterwards.

**D8. The gate** (Day 141). A CMake script (`tools/module_deps_check/check.cmake`), same form as the other gates, with:

- `modules.txt`: `<path glob> <module> <layer>` lines (the longest match wins), plus `allow <from> <to>` lines for edges a module's row permits that the layer
  rule does not (the RHI backends reaching `rhi`, for example);
- `allowlist.txt`: one line per tolerated violation, `<from-module> -> <to-module>  <reason>  <day that removes it>`; a stale entry and an entry without
  a reason fail;
- checks: upward edge; cycle (Tarjan over the module graph); a program or neutral file including a per-platform directory; an unresolvable include that is
  not a system header; the CMake link lines (`target_link_libraries`) with the same rules;
- a self-test counts the resolved edges on the real tree and fails under a floor, so a gate that resolved nothing cannot pass;
- output: `module_deps_check: <from> (<layer>) -> <to> (<layer>): <file>:<line>`, ending in `N problem(s)`.

**D9. Tests after the split** (Day 145): `tests/types` and the core `tests/foundation` files link `foundation_base`; `tests/io` links `foundation_io`;
`tests/debug` links `foundation_debug`; `tests/settings` links `foundation_settings`; the window tests link `platform`. A test that needs a higher layer
moves or links the higher library with a one-line reason.

## 3. Spikes: what was done

- **A (hooks):** done as analysis of every use (`log.cpp:220-222` error traces, `log.cpp:330-349` the default handler, `io/file.cpp:82`,
  `jobs/worker_pool.cpp:91`, `jobs/async_io.cpp:56`) and of `tests/debug/crash_state_test.cpp`; the throwaway build was not made, the real change on Day 142
  is its own experiment and runs every assert and death test. If Day 142 finds a test that depended on the old coupling, it is recorded there.
- **B (the split):** the unresolved-symbol list of a base-only library is the include graph above: after D2-D5 no layer 0-5 file includes a higher layer.
  Day 145 builds the libraries and any symbol that still does not resolve is a finding.
- **C (the platform move):** counted in D6.
