# STATUS.md

Current progress snapshot for this project. This file changes often —
update it as part of the same commit whenever the plan advances (a day
file's tasks get checked off, a phase starts or completes, a build/test
command changes). See `CLAUDE.md` for the durable rules that don't change
with progress.

Failing tests, flaky tests and deferred items are tracked in `KNOWN-ISSUES.md`.

## Build and test commands

What to install first, per platform: `BUILD.md`.

- **Windows (Phase 13, from Day 148):** from a plain PowerShell or Git Bash (no Developer Command Prompt): `cmake --preset windows-msvc` (or
  `windows-clang-cl`), `cmake --build --preset windows-msvc`, `ctest --preset windows-msvc`. The Windows build grows one layer per day. Scoped runs:
  `scripts/test-phase.sh --affected` (and `--flavor asan`) under Git for Windows' `bash` (default build directory `build-msvc`; no tsan on Windows, and
  `--flavor all` says so). Non-unity check: `cmake --preset windows-msvc -B build-windows-msvc-nounity -DENGINE_UNITY_BUILD=OFF`. Guide:
  `docs/guide/building-and-testing.md`, "Building and testing on Windows".


- Configure and build: `cmake -S . -B build && cmake --build build`. Our own targets build as unity (merged) translation
  units by default; `-DENGINE_UNITY_BUILD=OFF` compiles every source on its own (both must keep building; CLAUDE.md, "Unity builds").
- **Current phase** (the phase in `CURRENT_PHASE`, plain flavor): `scripts/test-phase.sh`. **Plus the modules a change
  touched:** `scripts/test-phase.sh --affected` (add `--flavor asan` or `--flavor tsan` for a sanitizer flavor). One phase:
  `scripts/test-phase.sh 4`. Add `--dry-run` to see what would run.
- **Everything, every flavor** (a phase's closing run): `scripts/test-phase.sh --all --flavor all`, or plain `ctest` from `build/`.
- Every test has one `phaseN` label (`ctest -L '^phase5$'`); `tests/phases.txt` is the only place a phase is decided, and
  `CURRENT_PHASE` changes in the commit that closes the previous phase or starts this one. Docs checks: `ctest -L docs` (not scoped).
- Tests that crash processes on purpose (label `crash`, the `tests/debug` suite) run only with `--all` or `--crashes`; the summary line says `crash tests: excluded`. A reduced run is quoted with its `ran X of Y tests` line, never as "all tests pass". Guide: `docs/guide/building-and-testing.md`.
- **Debugging facilities (Phase 7):** the suite is `tests/debug` (`ctest -L phase7`; the crash tests run child processes, a few need
  `lldb`, `gdb` or `objcopy` and skip with a printed reason without them). Gates: `signal_safety_check`, `platform_neutral_check`,
  `debug_backend_lists`, `debug_cross_syntax_windows|macos` (need `clang++`), `debug_no_network_*`. Scripts (Linux shell):
  `scripts/crash-report.sh`, `store-symbols.sh`, `split-debug.sh`, `find-core.sh`, `open-core.sh`. Every demo takes
  `--crash-handler=on|off` and `--crash-test=segv|abort|assert|ensure`. Release symbols: `-DENGINE_SPLIT_DEBUG_INFO=ON`.
  Guide: `docs/guide/foundation-debugging.md`, `porting-debugging.md`.
- **RHI reset (Phase 8):** the tests are labelled `phase8` (`ctest -L phase8`, `scripts/test-phase.sh 7`): `tests/renderer/{render_settings,device_resources,recreatable_renderer,apply_settings,device_loss_recovery,reset_soak}_test.cpp`,
  `tests/rhi/{adapter,device_host,swapchain_reconfigure,vulkan_reset_golden,lifecycle_conformance}_test.cpp`, `tests/foundation/window_fullscreen_test.cpp`
  and the demo ctests `renderer_demo_script_wayland|x11`, `renderer_demo_crash_after_reset`, `*_demo_list_gpus`, `*_demo_unknown_gpu`. Gates:
  `rhi_lifecycle_lists`, `rhi_cross_syntax_windows|macos`. GPU tests (tag `gpu`, they skip without a device) include the 100-reset loops and the
  pixel-identical-after-reset test; they run in the plain and ASan flavors. The demo takes `--gpu`, `--list-gpus`, `--prewarm` and `--script`
  and has live keys F1-F10 (`game/renderer_demo/README.md`). Guides: `docs/guide/render-settings.md`, `rhi.md` ("Lifecycle and reset",
  "Presentation", "Choosing a GPU"), `renderer.md` ("Surviving a reset", "Changing settings at run time", "Device loss and recovery").

## Phase 0 (Foundation layer)

**Complete — all 15 days are done and committed.** Real code exists
under `/engine/foundation`, built via CMake:
`cmake -B build && cmake --build build` produces `engine_main`
(`build/engine/foundation/engine_main`), now a minimal real entry point
(`return 0`) with no test logic of its own. A sanitized build exists as
a separate target: `cmake --build build --target engine_main_asan`,
then run `build/engine/foundation/engine_main_asan`. Foundation-layer
verification lives in `/tests/foundation`: `cmake --build build` also
builds `foundation_tests`, and `ctest` (run from `build/`) discovers and
runs each `TEST_CASE` as its own individual entry — 45 tests, all
green. Days 11-12 (grapheme
clusters, NFC/NFD normalization) built out an offline Unicode-data table
generator (`/tools/unicode_data_gen/`, pinned to Unicode 17.0.0 — see
`/UNICODE_VERSION`) that emits the committed tables under
`/engine/foundation/string/generated/` (grapheme-break, decomposition,
combining-class, composition-exclusions). `Array<T, Allocator>` and
`String<Allocator>` also gained move construction/assignment as part of
Day 12 — needed to return `String<Allocator>` by value from
`NormalizeNFC`/`NormalizeNFD`, previously missing since only copy had
been (deliberately) disabled. Day 13 added `Vec2`/`Vec3`/`Vec4`
(`/engine/foundation/math/vector.h`) and `Mat3`/`Mat4`
(`/engine/foundation/math/matrix.h`) — column-major storage,
column-vector convention (`v' = M * v`), documented in `matrix.h` since
it's load-bearing for later renderer/transform code. Day 14 added
`Quaternion` (`/engine/foundation/math/quaternion.h`: construction,
Hamilton product, conjugate/inverse, normalize, slerp, and both
directions of quaternion<->`Mat3` conversion) and `Transform`
(`/engine/foundation/math/transform.h`: position/rotation/scale,
`Compose`, `ToMatrix`). **Scope amendment made during Day 14** (PLAN.md
assumption #2, `CLAUDE.md`): scalar math functions
(`sqrt`/`sin`/`cos`/`acos`/etc.) now call `<cmath>` directly instead of
being hand-rolled — solved problems with no control/performance/learning
upside to reimplementing. `math_util.h`'s `Sqrt` was accordingly changed
from Day 13's hand-rolled Newton-Raphson to a thin `<cmath>` wrapper; the
vector/matrix/quaternion *types* themselves remain hand-rolled.

**Post-Day-14 reorg:** `/engine/foundation` was flat (20+ files) and got
split into subdirectories by subsystem: `os/` (`os.h` + `linux/`),
`mem/` (`mem`, arena/pool/stack allocators), `containers/` (`vector.h`,
`hash_map.h`), `string/` (`string.h`, `utf8*`, `grapheme*`,
`normalize*`, `generated/`), `math/` (`math_util.h`, `vector.h`,
`matrix.h`, `quaternion.h`, `transform.h`), `log/` (`log.h`/`log.cpp`).
`types.h`, `foundation.cpp`, `main.cpp`, and `CMakeLists.txt` stay at the
`foundation/` root since they're cross-cutting. All `#include`s and
`CMakeLists.txt` source paths were updated accordingly; the completed
day files under `plans/phase-00-foundation/` had their file-path
references updated to match too (at the user's request, overriding this
file's earlier note that they'd be left stale as a historical record —
the checked-off tasks themselves were left untouched, only the paths
inside their descriptions changed).

**Day 15** migrated the ad hoc `Expect()`-based tests to Catch2 v3
(`PLAN.md` assumption #16): vendored a pruned Catch2 v3.15.3 source tree
into `third_party/catch2/` (see `third_party/catch2/VENDORED.md`), added
a `foundation_tests` executable under `/tests/foundation` linking
`Catch2::Catch2WithMain`, and wired `catch_discover_tests` so `ctest`
sees each test individually. All 45 of Days 3-14's tests moved 1:1 into
15 per-subsystem files (see the day file for the full list) — `main.cpp`
is now just a real (empty) entry point. Fixing this up required changing
`ENGINE_PRODUCTION_COMPILE_OPTIONS` (`-fno-exceptions`/`-fno-rtti`/etc)
from a build-wide `add_compile_options()` to a `PRIVATE` compile option
on `foundation`/`foundation_asan` specifically, applied explicitly to
`engine_main`/`engine_main_asan` too — the old build-wide flag was a
`PUBLIC` usage requirement that leaked into every consumer of
`foundation`, including the new Catch2-based test target, which needs
exceptions. See
`docs/superpowers/specs/2026-08-14-unicode-string-support-design.md` for
the Days 10-12 Unicode design.

**Phase 0 is complete.** Every phase after it is still unimplemented — check the relevant
`plans/phase-NN-name/day-NN-*.md` files for what's actually done before
assuming a subsystem exists.

## Phase 1 (Linux Platform Layer)

**In progress.** Day 13 done: `os::CommandLine<Allocator>`
(`engine/foundation/os/command_line.h` + per-platform
`os/linux/command_line.cpp`) replaces raw `argc`/`argv` handling —
portable parsing (positional args, `--flag`, `--key=value`), sourced via
`CommandLine::FromNative()`/`StashNativeCommandLine()` so future
platforms (Windows/macOS/Android/iOS/consoles) plug in without changing
the class itself, mirroring `os.h`'s interface-header/per-platform-impl
split. Design: `docs/superpowers/specs/2026-08-16-command-line-wrapper-design.md`.
`main.cpp` now stashes argv, builds a `CommandLine`, and logs program
name/args/flags. 9 new tests in `tests/foundation/command_line_test.cpp`.

Day 14 done: `os::MonotonicTimeNanos()`/`os::SleepNanos()` added to
`os.h`/`os/linux/os.cpp` (`clock_gettime(CLOCK_MONOTONIC)`/`nanosleep`
wrappers). `main.cpp` runs a ~60Hz-targeted, ~3-second-bounded main loop
computing/logging frame delta-time under a sleep-based limiter (settles
to ~16.7-16.8ms/frame). 2 new tests in
`tests/foundation/os_timing_test.cpp`.

Day 15 done: no production code (per the day's already-resolved decision
not to add an engine `Thread`/`Mutex`/`ConditionVariable` wrapper type —
`std::thread`/`std::mutex`/`std::atomic`/`std::condition_variable` are
used directly). 3 new tests in
`tests/foundation/concurrency_primitives_test.cpp` (mutex-guarded
counter, atomic counter, condition-variable producer/consumer handoff;
500k increments/thread for the first two) — `Threads::Threads` linked
into `foundation_tests` via `find_package(Threads REQUIRED)`. Verified
with 30x randomized-order stress runs, 0 failures.

Day 16 done: `os::Lseek` added to `os.h`/`os/linux/os.cpp`. New portable
`os::ReadFileToArena` (`engine/foundation/os/file_util.h`/`.cpp`, no
per-platform split needed — built purely from `os.h`'s existing
primitives) reads a whole file into an `ArenaAllocator`-backed buffer. 3
new tests in `tests/foundation/file_io_test.cpp`, including reading
`/proc/self/exe` and checking the ELF magic number.

Day 17 done: new `os::DynamicLibrary` primitive in `os.h`
(`LoadLibrary`/`GetSymbol`/`UnloadLibrary`, per-platform impl in
`os/linux/os.cpp` via `dlopen`/`dlsym`/`dlclose`, `CMAKE_DL_LIBS` linked
`PUBLIC` on `foundation`/`foundation_asan`). **Design correction made
before writing code:** filenames are UTF-8, not `const char*` — so
`LoadLibrary` takes `StringView`, which exposed the same bug already
present in `os::Open`/`os::ReadFileToArena`, both retrofitted to
`StringView` in this pass. Since `StringView` isn't guaranteed
null-terminated, each Linux impl copies into a fixed `kMaxPathBytes`
(4096, matches `PATH_MAX`) stack buffer before calling the underlying
POSIX call, failing (not truncating) if the path doesn't fit; existing
string-literal call sites kept compiling via `StringView`'s existing
implicit `const char*` constructor. New
`engine/foundation/os/gpu_driver_probe.h`/`.cpp`
(`os::ProbeVulkanCreateInstance`) is a thin consumer: loads
`libvulkan.so.1`, resolves `vkGetInstanceProcAddr`, then
`vkCreateInstance`, logging each step through a new `LogPlatform`
channel rather than asserting — Vulkan types come from the system
`vulkan_core.h` header (`vulkan-headers` package), a deliberately minimal
choice deferring the real vendor-vs-system decision to Phase 4. 5 new
tests in `tests/foundation/gpu_driver_loading_test.cpp` (72 total tests,
all green); `engine_main_asan` still runs clean.

Day 18 done: `os::wayland::WaylandConnection`
(`engine/foundation/os/linux/wayland/wayland_connection.h`/`.cpp`)
connects to a Wayland compositor and binds
`wl_compositor`/`xdg_wm_base`/`wl_seat`/`wl_shm` via a registry listener
+ roundtrip, logging each bound global's interface name/version through
a new `LogPlatform` channel; move-only, invalid-on-failure rather than
crashing. **Resolved this day's open decision** (xdg-shell isn't part of
core Wayland): xdg-shell protocol bindings are generated once via
`wayland-scanner` against the system `xdg-shell.xml` and committed under
`engine/foundation/os/linux/wayland/generated/` — same pattern as the
Unicode tables (Days 11-12) — so `wayland-scanner`/`wayland-protocols`
stay dev-time-only; only `libwayland-client` (linked via
`pkg_check_modules`) is a real dependency. Compiling the generated `.c`
file required adding `C` to the root `project()` languages and scoping
`ENGINE_PRODUCTION_COMPILE_OPTIONS` to CXX-only translation units via
`$<COMPILE_LANGUAGE:CXX>`. 7 new tests in
`tests/foundation/wayland_connection_test.cpp`, run against the real
compositor present in this dev environment (76 total tests, all green).
Day 19 done: new `os::wayland::WaylandWindow`
(`engine/foundation/os/linux/wayland/wayland_window.h`/`.cpp`) wraps a
`wl_surface` as an `xdg_toplevel` and actually maps it — **caught and
fixed a protocol gap in the day's original task list before writing
code**: per xdg-shell, a surface isn't visible until a buffer is
attached+committed *after* the first configure/ack_configure
round-trip, so `Create()` also creates a minimal memfd-backed `wl_shm`
buffer (solid placeholder fill; real pixels are Phase 4's job) to
complete that sequence. Sizes from the compositor's suggested
`xdg_toplevel` size, falling back to 1280x720. Move-only,
invalid-on-failure. `main.cpp` now connects and creates a window before
its frame loop (non-fatally, same "log via LogPlatform, don't crash"
pattern as Day 17), loop bumped from Day 14's 3s to 6s and now pumps
`wl_display_dispatch_pending`/`flush` each frame as a minimal stand-in
for Day 20's real event loop. 4 new tests, including one that creates
and maps a real window against the compositor in this dev environment
(80 total tests, all green); `engine_main_asan` runs clean under
ASan/UBSan. **Human visual confirmation still pending** — this session
ran over a remote connection with no way to look at the actual screen;
the code/automated-verification side of the Done condition is met, the
eyeball check is deferred to the user's next local session.

Day 20 done: the Wayland connection now has a real, persistent event
loop instead of Day 19's stand-in. `main.cpp` gained
`PumpWaylandEvents()`, using
`wl_display_prepare_read`/`poll(fd, 0)`/`wl_display_read_events`/
`wl_display_dispatch_pending` — `wl_display_dispatch_pending` alone
never reads the socket, and a plain blocking `wl_display_dispatch`
would hang the frame loop (and `engine_main` itself, in headless/
automated runs) whenever the compositor has nothing queued, so this
non-blocking poll-then-dispatch pattern is what actually keeps both
properties. `WaylandWindow`'s `xdg_surface`/`xdg_toplevel` listeners
moved from Day 19's stack-local `ConfigureState` (which went out of
scope when `Create()` returned — **any configure arriving after
`Create()` was already latent UB**, a callback writing through a
dangling stack pointer, before this fix) to private static handlers
keyed on `this`; since `WaylandWindow` is movable, `wl_proxy_set_user_data`
now re-points the listeners' stored `data` at the object's current
address from both the move ctor and move-assignment, otherwise a move
after `Create()` (which `main.cpp` does) would leave them pointing at
a moved-from object. Toplevel `configure` buffers pending width/height
(spec: 0 means unspecified) applied atomically — and logged, only if
changed — when the matching surface `configure` is acked; toplevel
`close` sets a `should_close_` flag read via the new
`WaylandWindow::ShouldClose()`, which `main.cpp`'s loop now checks
each frame (breaking early) alongside its existing 6s wall-clock bound.
2 new tests (`ShouldClose()` starts false, both on a real `Create()`d
window and a default-constructed one); 82 total tests, all green
against the real compositor in this dev environment; `engine_main_asan`
runs a full 6s loop clean under ASan/UBSan. **Manual resize/close
verification not done** — same remote-session constraint as Day 19,
compounded here by no scriptable way to drive real WM resize/close
interactions either; left as an explicitly unchecked box in the day
file rather than assumed passing, pending the user's next local
session.

Day 21 done: Wayland keyboard input via **libxkbcommon** (new named
dependency, explicitly approved by the user 2026-11-03; linked via
`pkg_check_modules`). New `os/input.h` (engine `Key` enum — layout-aware,
so physical Q on AZERTY is `Key::A` — plus `KeyboardState`/modifier
bits), `os/key_translate.cpp` (`KeyFromKeysym`, shared with the future
X11 backend), `os/linux/xkb_keyboard.h/.cpp` (`XkbKeyboard`, compositor-
free so it's testable against `us`/`fr`/`de` keymaps), and
`os/linux/wayland/wayland_input.h/.cpp` (`WaylandInput`: seat
capabilities → `wl_keyboard`, keymap/enter/leave/key/modifiers). Releases
clear the Key a code was *pressed* as; focus loss releases all. 9 new
tests (91 total, green). **Live keypress check on real keyboards not
done** (remote session); layouts verified via compiled named keymaps.

Day 22 done: Wayland pointer input. `os::InputState` (`os/input.h`)
now wraps `KeyboardState` plus mouse position/inside flag, a
`MouseButtonBit` mask (evdev BTN_LEFT..EXTRA), and scroll deltas
accumulated until `BeginFrame()`. `WaylandInput` (renamed scope: seat
keyboard + pointer) takes an `InputState*` and installs the
`wl_pointer` enter/leave/motion/button/axis listeners; held buttons
survive pointer-leave (implicit grab). 2 new tests (93 total, green).
**Live mouse move/click/scroll check not done** (remote session) — the
readout is wired in Day 23.

Day 23 done: Wayland "hello window" milestone wired in `main.cpp` —
connection, window, event loop, `WaylandInput`, per-frame
`InputState::BeginFrame()`, and a change-driven `input:` debug readout.
New `--run-seconds=N` flag (default 6). A 20s run on the live KDE
compositor held ~16.7ms frames (max 16.77ms), exit 0; ASan run clean.
**Still pending a human:** live keyboard/mouse updates and the real
close-button exit (no input-injection tooling in this session).

Day 24 done: X11 fallback window. New `os::x11::X11Window`
(`engine/foundation/os/linux/x11/`, libX11 via `pkg_check_modules`):
`XOpenDisplay` (graceful failure), `XCreateWindow` with the full event
mask, `_NET_WM_NAME`, `WM_DELETE_WINDOW` via `XSetWMProtocols`, and a
non-blocking `PumpEvents()` handling Expose/ConfigureNotify (resize,
logged)/ClientMessage (close). Polled, so unlike the Wayland classes it
has no `this`-capturing callbacks and is freely movable. Temporary
`engine_main --backend=x11` flag until Day 26's real selection. 7 new
tests (100 total, green) run against a real X server — passing on both
this machine's Xwayland+KWin and a bare Xvfb — including an externally
sent `WM_DELETE_WINDOW` and `XResizeWindow`. **Visual check and real WM
close-button click still pending a human.**

**libc cleanup (2026-11-03):** removed `mem/mem.h`/`mem.cpp` (hand-rolled
`memcpy`/`memset`/`memmove`/`strlen` overriding libc's symbols), their
tests, and `-fno-builtin`; code uses `<cstring>`. See PLAN.md assumption
#2 amendment. `StringView`/`StringBuilder` remain hand-rolled.

Day 25 done: X11 input. `X11Window::PumpEvents(InputState*)` now
translates KeyPress/Release (XIC + `Xutf8LookupString`, fallback
`XLookupString`, same `KeyFromKeysym` as Wayland; releases clear the Key
pressed; FocusOut releases all; detectable autorepeat), Button 1/2/3/8/9,
wheel 4-7 (10 units/notch like Wayland), Motion/Enter/Leave, and
modifier masks. 6 new X11 tests via `XSendEvent` injection (102 total,
green; one layout test skips unless `ENGINE_X11_TEST_LAYOUT` is set).
**Gap:** the `fr`/`de` live-X11 layout check could not run — Xvfb here
ignored every keymap change, so only `us` was verified through X11;
multi-layout correctness is covered by Day 21's xkbcommon tests.

Day 26 done: backend selection. `os/window.h` defines the polled
`os::Window` interface; `WaylandPlatformWindow` (new; owns connection,
window, input and the event pump formerly in `main.cpp`) and `X11Window`
implement it; `PlatformWindowHost` (`os/linux/`) picks one: `--backend=`
flag > `ENGINE_WINDOW_BACKEND` env > auto (`$WAYLAND_DISPLAY` → Wayland,
falling back to X11 if unset or the connection fails; a forced backend
never falls back). `main.cpp` is backend-agnostic. 10 new tests (112
total, green). The hello-window run is identical under wayland/x11/auto
(10s, 598 frames, exit 0, also under ASan). **Still pending a human:**
live keyboard/mouse and real close-button checks (Days 19-26), and
`fr`/`de` through a live X server.

**Phase 1 Days 13-26 done — Phase 1's planned days are complete**
(modulo the human-verification items above). Phase 2 has since been planned and implemented (see below).

**Day 19 follow-up (found at the desk, 2026-11-03):** the Wayland window
had no titlebar/title/close button on Plasma — KWin only draws
decorations for clients that request them via `xdg-decoration`. Added
`zxdg_decoration_manager_v1` bindings (generated + committed under
`os/linux/wayland/generated/`, see `VENDORED.md`), an optional bind in
`WaylandConnection`, and `WaylandWindow` now requests server-side mode
(`ServerSideDecorated()` reports what the compositor granted). New test
confirms KWin grants it (113 tests green). Needs your eyeball: titlebar
with "re-engine" + working close/resize.

**Desk check 2026-11-03:** Day 19 visual confirmation passed (window appears
on Plasma/Wayland). Remaining desk checks: keyboard, mouse, close button,
resize, X11 backend.

**Desk fix 2026-11-03 (no `input:` lines):** `WaylandPlatformWindow::Open`
created the window (which roundtrips) *before* hooking the seat, and a
`wl_seat.capabilities` event with no listener is silently dropped — so no
keyboard/pointer was ever created. Input now hooks the seat first; a
regression test (`window_backend_test.cpp`) asserts keyboard, keymap and
pointer exist after `Open`. (The Day 21/22 tests missed this because they
called `WaylandInput::Init` straight after `Connect`.) Also: per-frame
`delta_ms` lines are now opt-in (`--frame-times`), and
`--log-level=error|warn|info|debug|trace` sets all channels (e.g. to see
resize lines). 114 tests green.

**Desk check 2026-11-03, Wayland input:** user confirmed live pointer
motion (position tracks, enter/leave flips `inside`) and key
press/release (`keys_down` rises and returns to 0, nothing stuck) in the
`input:` readout. **Not yet seen live:** mouse buttons, scroll, modifiers,
a specific key (`W=1`), a second keyboard layout, the close button,
resize, and the X11 backend — Day 20-23 manual boxes stay unchecked
until those are reported.

**Desk fix 2026-11-03 (resize):** user confirmed buttons, scroll,
modifiers, keys and `W` work live on Wayland; **resize did not.** Cause:
the configure handler only logged the new size — per xdg-shell the
window doesn't change size until a buffer of that size is attached, so
KWin kept it at 1280x720. `WaylandWindow::ReplaceBuffer()` now allocates
and commits a matching buffer on every size change (also used for the
initial map). New test drives a real compositor-initiated resize via
`xdg_toplevel_set_maximized`. 115 tests green. Needs your re-check.

**Desk check 2026-11-03 (cont.):** confirmed live — Wayland: pointer,
buttons, scroll, keys, modifiers, close button; X11 backend: window,
resize (fast), close. Wayland resize was functionally broken then slow;
fixed by buffer re-allocation (`8aae284`) plus resize coalescing
(`CommitPendingResize`, once per event pump) and a doubling-memcpy fill.
**Still unconfirmed live:** Wayland resize after the fix, a second
keyboard layout, X11 keyboard/mouse specifics.

**Desk check 2026-11-03 (final):** Wayland resize confirmed working and
fast after the coalescing fix; after switching the Plasma keyboard
layout `W` still registers (layout-aware keys work live; which layout
wasn't recorded). Remaining unconfirmed live: X11 under a non-US layout,
X11 keyboard/mouse specifics. Phase 1's manual checks are otherwise done.

**Desk check 2026-11-03 (X11):** user confirmed all X11 input live —
buttons, wheel, modifiers, keys, `W`. Phase 1's manual verification is
complete; the only soft spot is that no live X11 check under a known
non-US layout was recorded (`fr`/`de` covered by xkbcommon tests only).

## Phase 2 (Job System) — COMPLETE (Days 27-35, 2026-11-03)

Plan verified and revised 2026-11-03: nine day files (Days 27-35) under
`plans/phase-02-job-system/`, design decisions D1-D9 settled in
`plans/phase-02-job-system.md` (worker model A: main helps while
waiting; logical-CPU participants, `ENGINE_JOB_THREADS` override;
dedicated I/O thread; separate TSan build).

Day 27 done: `engine/jobs/` static library (`jobs`, `jobs_asan`,
`jobs_tsan`; headers included as `"jobs/<name>.h"`), `foundation_tsan`
(a separate ThreadSanitizer build — TSan can't combine with ASan), and
`FramePool` (`jobs/frame_pool.h`): size classes 128-2048 backed by
foundation `PoolAllocator` behind a mutex, per-thread caches for the
global instance, 16-byte block headers, a counted libc-`malloc` fallback
(never null), and `Stats()`. Tests in `tests/jobs/` build three ways
(`jobs_tests`, `jobs_tests_asan`, `jobs_tests_tsan`); `ctest -L tsan`
runs the TSan set, including `tsan_canary_detects_race`, an
expected-failure test that goes red if TSan stops detecting races. 137
tests total, all green. Run: `cmake --build build && cd build && ctest`
(add `-L tsan` for just the TSan set). Day 28 (Job coroutine type) is DONE (high-effort review recorded in its day file); Day 29 (worker pool) is DONE (review recorded in its day file); Day 30 (work-stealing deque) is DONE (review recorded in its day file); Day 31 (scheduler) is DONE (review recorded in its day file); Day 32 (counter/await) is DONE (review recorded in its day file); Day 33 (fan-out/join) is DONE (review recorded in its day file); Day 34 (async I/O) is DONE (review recorded in its day file); Day 35 (stress & throughput) is DONE (review recorded in its day file). **Phase 2 is complete:** `Scheduler`, `Counter`, `parallel_for`, async file I/O, the Chase-Lev deque and frame pool pass the randomized-graph stress, skewed-load stealing (other participants verifiably execute stolen work), a scaling table (1.96x at N=2, 3.87x at N=4 here) and 10,000-run nested `co_await` ordering tests, clean under ASan and TSan. Phase 4 (RHI / Vulkan) has no day files yet, so it is not startable until they are written and committed (CLAUDE.md).

**Code-review fixes (2026-11-03, `/code-review high a134e2c..HEAD`, 9
findings):** (1) `WaylandPlatformWindow::PumpEvents` now checks every
libwayland return value and flips `ShouldClose()` when the compositor
connection dies (tested by severing the socket); (2) the TSan canary now
requires the data-race report (`PASS_REGULAR_EXPRESSION`) instead of a
bare `WILL_FAIL`; (3) Wayland honors the requested window size (new
`width_hint`/`height_hint` on `WaylandWindow::Create`); (4) `0x0`
("client chooses") configures restore the last unconstrained size, via
tracked maximized/fullscreen/tiled states, and a failed resize-buffer
allocation is retried; (5) a failed `WaylandPlatformWindow::Open` now
tears everything down (`Close()`, `WaylandInput::Shutdown()`); (8) the
`buttons=`/`mods=` readout is real hex (`StringBuilder::AppendHex`).
(9) X11 `FocusOut` now ignores the spurious grab-mode events WMs send
during Alt-Tab/menus (`NotifyGrab`/`NotifyWhileGrabbed`/`NotifyInferior`)
and a real focus loss also resets the modifier mask; (6) `FramePool`
slabs are reserved up front and handed out by bump pointer + free list
(no more committing ~10 MB under the lock on first use; foundation
`PoolAllocator` no longer used); (7) `ThreadCache::destroyed` makes late
frees/allocs after a thread's cache is gone go straight to the central
pool (test verified to fail without the fix). X11 input tests now wait
for the WM's own focus churn to settle before injecting (40/40 clean
runs). 148 tests green.

**Roadmap change (2026-11-03):** a dedicated **Phase 7 — UI System
(Widgets & Viewport)** was inserted after Font & Text (retained widget
toolkit with menus/buttons/comboboxes/scrollbars/labels/text fields and a
viewport widget hosting the rendered 3D world). The old Phases 7-23 are
now 7-20 (plan files renamed, cross-references updated; the dated design
docs under `docs/superpowers/specs/` keep their original numbering). It
is a goal/deliverable-level plan only — not startable until its days are
broken out. Phase 2 (Job System) remains the active phase.


## Phase 3 (Documentation tooling) — complete (Days 36-44); Phase 4 plan written (Days 45-67), not started

Days 36-44 are broken out under `plans/phase-03-docs-tooling/` (design
settled: own C++ `docgen` on the foundation, purpose-built scanner, `///`
comments, committed generated reference, link/sample/lint checks). The
end-user docs skeleton lives in `docs/` (all pages are outlines). Phase 4
is broken out into Days 45-67 under `plans/phase-04-vulkan-rhi/` (planning
only; no code yet).

Day 36 (design & convention) is done: `docs/DOC-CONVENTIONS.md`, `tools/docgen/DESIGN.md` and the fixture corpus in `tests/docgen/fixtures/` are the contract for Days 37-44.
Day 37 (scanner) is done: `tools/docgen/` (`docgen_lib`, `docgen tokens FILE`) and `tests/docgen/` (`docgen_tests`, `docgen_tests_asan`; 31 test cases incl. fixtures, all of `engine/**/*.h`, truncation/junk fuzz); high-effort review recorded in the day file. Run: `build/tests/docgen/docgen_tests`.
Day 38 (declaration extractor) is done: `tools/docgen/src/extractor.{h,cpp}`, `docgen dump [--all] [--keep-going] FILE.h`, `*.tree` goldens for every good fixture; all 43 `engine/**/*.h` headers extract with zero diagnostics (counts in the day file); 57 docgen test cases pass plain and under ASan; high-effort review (10 findings, all fixed) recorded in the day file.
Day 39 (doc-comment parser) is done: `tools/docgen/src/doc_parser.{h,cpp}`, `docgen docs FILE.h`; summary/details/lists/code/labels/`[[refs]]` parsed and validated against the declaration (Param names, Returns on void, unknown/duplicate/empty labels, thread-safety categories, ...), override doc inheritance, undocumented-symbol list, normalized emit with a stable round trip; 88 docgen test cases pass plain and under ASan; high-effort review (9 findings, all fixed) recorded in the day file.
Day 40 (symbol index + Markdown emitter) is done: `docgen reference|check` (module map `docs/reference/modules.txt`, stable symbol IDs/anchors, module pages, index page, `symbols.txt`, golden fixtures under `tests/docgen/fixtures/ref/`, stale-output detection); 113 docgen test cases pass plain and under ASan; high-effort review (9 findings, 8 fixed, 1 declined with a measurement) recorded in the day file. The engine's own reference is not committed yet (no `///` comments until Day 44).
Day 41 (cross-references + link checker) is done: `docgen links` (file/anchor/image links, `[[Symbol]]` refs in `docs/**`, orphan pages, external links counted not fetched; scanner `md_scan.*` is shared with Day 42); 134 docgen test cases pass plain and under ASan; high-effort review (9 findings, 8 fixed, 1 declined) recorded in the day file.
Day 42 (sample compiler) is done: `docgen samples` (tags `cpp compile [standalone] [run]` + adjacent `output` block, `#line`-mapped diagnostics, posix_spawn with timeouts, parallel `--jobs`, scratch cleanup incl. on signals); 152 docgen test cases pass plain and under ASan; high-effort review (8 findings, all fixed) recorded in the day file. Wiring the real flags/libraries and the prelude is Day 43.
Day 43 (lint, coverage, build integration) is done: `docgen lint` (coverage table, strict-module label rules, ratcheting baseline `docs/reference/lint-baseline.txt`), the `docs` CMake target and ctest entries `docs_check`/`docs_links`/`docs_samples`/`docs_lint` (`ctest -L docs`), `docs/samples-prelude.h`, the generated `docs/reference/` committed (0% documented; Day 44 retrofits the headers); 166 docgen test cases pass plain and under ASan; high-effort review (9 findings, 7 fixed, 2 declined) recorded in the day file. Regenerate with `cmake --build build --target docs` after any header change.
Day 44 (header retrofit) is done: every public header of Phases 0-2 documented (667/667 symbols, 100% in all modules, lint baseline empty), `docs/reference/` regenerated and committed, guide pages carry `[[ref]]` API sections, all four docs checks green; high-effort review (7 findings, all fixed) and the observations list recorded in the day file. The "documented before done" rule was approved and added to CLAUDE.md and PLAN.md. Known unrelated failure: `X11Window::Create opens a mapped window on the real X server` fails here, also on unmodified HEAD.

Phase 4 (Vulkan RHI) is planned: 23 day files (Days 45-67) under `plans/phase-04-vulkan-rhi/`, indexed by `plans/phase-04-vulkan-rhi.md` with the settled design decisions. Phase 4 is under way; Phase 5+ still need their day files before they start.

Phase 4 Day 45 (RHI design spec & build skeleton) is done: design decisions D1-D8 in `docs/superpowers/specs/2026-11-04-rhi-design.md`, targets `rhi`/`rhi_vulkan` (+ `_asan`) and `rhi_tests`/`rhi_tests_asan` (label `rhi`), `LogRhi`, module `rhi` in the docs map, guide skeleton `docs/guide/rhi.md`; high-effort review (6 findings, 4 fixed, 2 declined) recorded in the day file. Run: `build/tests/rhi/rhi_tests`.
Phase 4 Day 46 (core types) is done: handles, `Format` table, flag enums, descriptors with `Validate()`, `DeviceCaps` in `engine/rhi/` (all documented, 328/328, baseline zero); 20 RHI test cases pass plain and under ASan, with a per-header self-containment check; high-effort review (8 findings, 6 fixed, 2 declined) recorded in the day file.
Phase 4 Day 47 (device interface, handle pool, null backend) is done: `Device`/`CommandList`/`Swapchain` interfaces, `HandlePool` with deferred destruction, and `NullDevice` (validating, counting, no GPU) in `engine/rhi/`; 71 RHI test cases pass plain and under ASan; docs 516/516 with a runnable guide sample; also fixed `docgen samples` resolving `clang++` to `clang-23`; high-effort review (9 findings, all fixed) recorded in the day file.

Phase 4 Day 48 (native window seam) is done: `os::NativeWindowInfo`, `Window::GetNativeInfo()/IsMinimized()/ResizeSerial()` on Wayland and X11 with a process-wide serial counter; docs 1207/1207; high-effort review (7 findings, 5 fixed, 2 declined) recorded in the day file. Known pre-existing failure: `X11Window::Create opens a mapped window on the real X server`.

Phase 4 Day 49 (Vulkan loader and function tables) is done: `engine/rhi/vulkan/` opens `libvulkan.so.1` at runtime, resolves generated global/instance/device tables from `vk_functions.inc`, checks Vulkan 1.3, and has `VkResultName`/`ClassifyVkResult`/`ENGINE_VK_CHECK`; new docs module `rhi-vulkan`; `[gpu]` tests pass on the dev GPU plain and under ASan; high-effort review (9 findings) recorded in the day file.

Phase 4 Day 50 (instance and validation) is done: `VulkanInstance` with extensions by purpose, validation layer policy (`ENGINE_RHI_VALIDATION`, `_SYNC`, `_BEST_PRACTICES`, `_FATAL`), a debug messenger that logs and counts errors/warnings, `SetValidationCapture`, and `gpu_test::GpuFixture` that fails any `[gpu]` test on an unexpected validation error; docs 1270/1270; high-effort review (9 findings) recorded in the day file.

Phase 4 Day 51 (physical device, queues, logical device) is done: `rhi::CreateDevice` (`engine/rhi/rhi.h`) creates a headless `VulkanDevice` — pure selection policy with `ENGINE_RHI_GPU` override, queue plan, exactly-the-needed features, `DeviceCaps` from real limits, `Format`↔`VkFormat` table; resource, command-list and fence methods are stubs until Days 53–57; docs 1356/1356; high-effort review (8 findings) in the day file.

Phase 4 Day 52 (GPU memory allocator) is done: `MemoryAllocator` (best-fit free-list blocks with coalescing, linear/optimal segregation, dedicated objects, persistent mapping, spare-block policy, non-coherent atom rounding, stats, never-abort failure policy) over a `MemoryBackend`; 14 pure tests against a fake backend including a 100k-cycle invariant stress, plus real-driver tests; docs 1455/1455; high-effort review (9 findings) in the day file.

Phase 4 Day 53 (buffers, staging, upload) is done: Vulkan `CreateBuffer`/`Map`/deferred `DestroyBuffer`, staging `RingAllocator`, chunked `UploadBuffer` returning `UploadTicket{fence, ok}`, `ReadbackBuffer`, timeline fence and frame tracking; round trips up to 40 MiB, plain and ASan; docs 1478/1478; high-effort review (10 findings, 9 fixed, 1 declined) in the day file.

Phase 4 Day 54 (command lists, submission, sync) is done: `GetBarrierScopes` state table, `VulkanCommandList`, per-thread command pools in a 3-frame ring, `Submit` on the timeline semaphore, sticky device loss with a fault-injection hook, multithreaded recording test; new `rhi_tests_tsan` target (non-GPU tests only, because the NVIDIA driver crashes TSan); docs 1528/1528; high-effort review (10 findings, 9 fixed, 1 declined) in the day file.

Phase 4 Day 55 (textures, samplers, layout transitions) is done: Vulkan textures with deferred destruction, shared reference-counted samplers, `FormatSupport`/`CanGenerateMips`, `UploadTexture`/`ReadbackTexture`, texture copies and barriers in command lists, `GenerateMips`; null backend mirrors the API; docs 1555/1555; high-effort review (10 findings, 8 fixed, 2 declined) in the day file.

Phase 4 Day 56 (swapchain and window presentation) is done: `DeviceCreateInfo::window`, Wayland and (isolated-TU) Xlib surfaces, tested swapchain policy functions, `VulkanSwapchain` with bounded `Acquire`, deferred recreation and minimize handling, null swapchain; presents to Wayland and X11 windows with validation clean; docs 1590/1590; high-effort review (10 findings, 8 fixed, 2 declined) in the day file.

Phase 4 Day 57 (frame loop, frames in flight, clear-color window) is done: `DeviceCreateInfo::frames_in_flight` (1-3), Vulkan `BeginRendering/EndRendering`, `VulkanDevice::GetLeakCounts`, `game/rhi_demo` (animated clear on Wayland and X11, validation clean), pixel-readback and 120-frame loop tests, guide "The frame loop" and tutorial `hello-rhi.md`; docs 1597/1597; high-effort review (8 findings, 5 fixed, 3 declined) in the day file. Only the Wayland/X11 visual check by eye is outstanding (see the day file).

Plan change before Day 58 (2026-11-04): shaders are now GLSL compiled offline by `glslc` instead of hand-written SPIR-V assembly (`PLAN.md` assumption #18). Day 58 and Days 61-65 were rewritten accordingly; the long-term shader toolchain (DX12/Metal path) is an open decision to settle before Phase 5's material system and no later than Phase 17, tracked in `PLAN.md`, `CLAUDE.md` and the Phase 5/16/17 plan files. Day 57's visual check on Wayland and X11 was confirmed by hand.

Phase 4 Day 58 (shader workflow and shader modules) is done: `rhi::ValidateSpirv` sanity check, Vulkan `CreateShader`/`DestroyShader`/`LookupShader`, `tools/spv_embed`, GLSL `shaders/` with committed `*.spv.inc`, the `shaders` target (needs `glslc`) and `shaders_check` ctest, triangle vertex/fragment shaders, guide `rhi-shaders.md`; docs 1607/1607; high-effort review (8 findings, 8 fixed, 0 declined except a documented constant) in the day file. Regenerate shaders with `cmake --build build --target shaders`.

Phase 4 Day 59 (graphics pipelines and the pipeline cache) is done: `CreateGraphicsPipeline` with dynamic rendering, shared bind group layouts and reference-counted pipeline layouts, the on-disk pipeline cache (`ENGINE_RHI_CACHE_DIR`, `SavePipelineCache`), and `SetPipeline`/`SetViewport`/`SetScissor`/`SetVertexBuffer`/`SetIndexBuffer`/`Draw`/`DrawIndexed`; a triangle draws into an offscreen target and reads back; docs 1652/1652; high-effort review (9 findings, 7 fixed, 2 declined) in the day file.

Phase 4 Day 60 (bind groups, descriptors and push constants) is done: Vulkan `CreateBindGroup`/`DestroyBindGroup` with persistent and per-frame (transient) descriptor pools that grow instead of failing, `SetBindGroup` (bound lazily at draw), `SetPushConstants`, `rhi::UniformRing` with `Flush()`, `Device::FlushMappedBuffer`; a draw reading a uniform buffer, texture and sampler plus a push constant is verified by readback; docs 1683/1683; high-effort review (9 findings, 6 fixed, 3 declined) in the day file.

Phase 4 Day 61 (triangle milestone and offscreen golden-image tests) is done: `tests/rhi/{render_target.h,image_compare.*}` and a 64x64 golden (`tests/rhi/golden/triangle.ppm`, regenerate with `ENGINE_RHI_UPDATE_GOLDEN=1`), analytic pixel checks independent of the golden, `rhi_demo` draws the triangle and takes `--frames N` / `--screenshot out.ppm`; guide section "Golden-image rendering tests" and the tutorial's "Draw a triangle"; docs 1683/1683; high-effort review (10 findings, 9 fixed, 1 declined) in the day file.

Phase 4 Day 62 (compute pipelines and dispatch) is done: Vulkan `CreateComputePipeline` and `Dispatch`, GLSL compute shaders `fill`/`saxpy`/`gradient`, 1M-element fill + saxpy and a storage-texture write verified headless under sync validation, guide section "Compute"; docs 1683/1683; high-effort review (8 findings, 6 fixed, 2 declined) in the day file.

Phase 4 Day 63 (depth, indexed draws and the spinning cube) is done: `game/common/cube_scene.*` (Vulkan-convention projection, look-at, cube mesh, `CubeRenderer`), `cube.vert`/`cube.frag`, `rhi_demo` draws a depth-tested spinning cube (resize-aware depth buffer, `--screenshot`), a 128x128 cube golden with analytic face probes, depth-occlusion, indexed-vs-expanded and culling tests, guide section with the 3D conventions (front faces are `CounterClockwise` with the Y-flipped projection); docs 1683/1683; high-effort review (8 findings, 7 fixed, 1 declined) in the day file.

Phase 4 Day 64 (textured cube) is done: `game/common/textures.*` (procedural textures, `.rtex` container, `CreateSampledTexture`), `textured.vert`/`textured.frag`, `CubeRenderer` textured mode, `rhi_demo` draws the textured cube from `game/rhi_demo/assets/checker.rtex` with trilinear + anisotropic sampling; quadrant, sRGB-vs-UNORM, mips-vs-no-mips and golden tests, guide section "Textures in draws" and the tutorial's "Add a texture"; docs 1683/1683; high-effort review (8 findings, 8 fixed, 0 declined findings; 3 notes) in the day file.

Phase 4 Day 65 (lit cube, the Phase 4 milestone) is done: `lit.vert`/`lit.frag`, `CubeRenderer::DrawLit`, a finished `rhi_demo` (orbit camera via `InputState`, orbiting light, `--backend/--frames/--screenshot/--no-vsync/--validation`, memory check, `game/rhi_demo/README.md`), lighting golden + Lambert/ambient/zero-light/mirror tests, guide/shaders/tutorial updates; docs 1683/1683; 1000-frame runs on Wayland and X11 with validation + sync validation: zero messages, 4.1-4.2 ms/frame (vsync, RTX 3060), memory +0.4 MiB; high-effort review (10 findings, 10 fixed) in the day file.

Phase 4 Day 66 (hardening, sanitizers and the Vulkan boundary gate) is done: `rhi_boundary_check` ctest (comment-aware CMake scan of engine/game/tools, reverse rule for `engine/rhi/*.h`, `path|reason` allow-list that rejects stale entries, planted-violation fixtures) found and fixed two real leaks of the boundary (the demo's `VulkanDevice` use; the null backend's `vk_array.h`, now `engine/rhi/grow_array.h`); `rhi::FaultInjection` (device loss at a frame, allocation failure from the Nth) with `rhi_demo --inject-device-loss/--inject-oom` (exit 3 / exit 1), `[fault]` tests including a 200-resize storm on Wayland and X11; `tests/rhi/lsan.supp` with a reason per entry; all 149 asserts and the log sites classified (table in the RHI guide); fixed a triangle golden test that failed instead of skipping without Vulkan; RHI suite ~15 s with a GPU, ~1 s without; 1277/1277 tests, docs 1689/1689; high-effort review (10 findings, 8 fixed, 2 declined) in the day file.

Phase 4 Day 67 (portability review, performance baseline, phase wrap-up) is done and **Phase 4 is complete**: `docs/superpowers/specs/2026-11-04-rhi-portability-review.md` (every RHI concept vs Direct3D 12 and Metal; one interface change - `DeviceCaps::buffer_texture_copy_offset/row_pitch_alignment`, 512/256, enforced by the null and Vulkan backends - and the rest filed as Phase 17/17 shims); `game/rhi_bench` baseline recorded in the day file and `docs/guide/rhi.md` (Release, RTX 3060: ~0.5-0.6 us per draw, 0.15-0.2 us per barrier, 14-18 GiB/s uploads, cube pipeline 3.6-5.8 ms; device mutex and per-draw transient bind groups are the hotspots); `docs/guide/rhi.md` is `draft`, the tutorial runs goal -> triangle -> cube -> texture -> lit; Phase 5 notes updated. Final run: 1280/1280 tests (plain, ASan, TSan, docs 1691/1691), validation and sync validation silent; high-effort review (7 findings, all fixed) in the day file. Phase 5 (Renderer core) has no day files yet, so it is not startable until they are written and committed (CLAUDE.md). Run the baseline: `cmake --build build --target rhi_bench && build/game/rhi_bench/rhi_bench` (use a Release build for meaningful numbers).

Phase 5 (Renderer core) is planned, not started: 17 day files (Days 68-84) under `plans/phase-05-renderer-core/`, indexed by `plans/phase-05-renderer-core.md`. Decisions (2026-11-04): Slang as the shader language (offline `slangc`; GLSL ported on Days 69-70), deferred lighting without MSAA, a generic render graph on the RHI, no bindless/indirect/async compute. Phase 5 can start.

Phase 5 Day 68 (renderer design spec and build skeleton) is done: `docs/superpowers/specs/2026-11-04-renderer-design.md` (R1-R11, including the G-buffer formats and the GGX lighting model), `engine/renderer/` + `tests/renderer/` (plain/ASan/TSan; `RenderLog` on `LogRender`), module `renderer` and a guide skeleton, the boundary gate extended to `tests/renderer/` (seven planted reports); docs 1694/1694; high-effort review (6 findings, 5 fixed, 1 declined) in the day file. Next: Day 69 (install `slangc`, first shader ports).

Phase 5 Day 69 (Slang toolchain and first shader ports) is done: `slangc` v2026.18.2 installed and pinned (every generated `.spv.inc` now records `// compiler: <name> <version>`); `*.slang` ports of `fullscreen.vert`, `triangle.vert/frag`, `bindings_test.frag`, `fill/saxpy/gradient.comp` with committed outputs; `-DENGINE_SHADER_LANG=glsl|slang` (default `glsl` until Day 70); flags and pitfalls in `shaders/README.md`; `tests/rhi/slang_shader_test.cpp` proves each port matches its GLSL interface; the GPU suite passes unchanged against the Slang outputs with validation and sync validation silent. Regenerate with `cmake --build build --target shaders`. High-effort review (8 findings, 7 fixed, 1 declined) in the day file. Known: `X11Window::Create opens a mapped window on the real X server` fails consistently under this session's Xwayland (unrelated). Next: Day 70.

Phase 5 Day 70 (remaining shaders ported, GLSL retired) is done: `cube`, `textured` and `lit` shaders ported to Slang; every golden image and analytic probe passed unchanged against the Slang outputs before anything was deleted; `rhi_demo --frames 1000 --validation` clean on Wayland and X11; the GLSL sources, the `glslc` path and `ENGINE_SHADER_LANG` are gone, Slang sources and `*.spv.inc` live in `shaders/` and `shaders_check` uses `slangc` only; `tests/rhi/shader_interface_test.cpp` asserts every shader's interface; HLSL/Metal emission checked (Y flip stays in the projection; Metal renames `main`, see `shaders/README.md`). PLAN.md #18, CLAUDE.md and the guides now describe only Slang. High-effort review (6 findings, all fixed) in the day file. Vsync-paced demo frame time was 50 ms (Wayland) / 17 ms (X11) vs 4.1 ms in Phase 4 in this session (compositor pacing, shader cost is negligible with `--no-vsync`): re-measure after the session restart together with the X11 test failure. Next: Day 71 (reflection).

Phase 5 Day 71 (shader reflection and the mint tool) is done: `tools/shader_mint` (replaces `spv_embed`) turns `slangc` output into committed `*.spv.inc` and `*.refl.inc`, reading `-reflection-json` with vendored nlohmann/json (tool-only; `PLAN.md` assumption #19, decided with you on 2026-11-05) and cross-checking it against the SPIR-V; `renderer::ShaderReflection` with a strict versioned blob (`ParseShaderReflection` rejects bad version, truncation, unknown types, corruption without out-of-bounds reads, ASan-tested), `BuildBindGroupLayout`/`BuildVertexLayout`/`CheckBlockSize`; generated block size/offset constants now back the `static_assert`s on `LitUniforms` and the cube `Mat4`; everything builds without `slangc`. Docs 1770/1770. High-effort review (10 findings, 9 fixed, 1 declined: the tool uses the STL because the JSON library does) in the day file. Regenerate with `cmake --build build --target shaders`. Next: Day 72.

Phase 5 Day 72 (shader permutations) is done: variants are compile-time defines (`-DOPTION=1`), not specialization constants; measurements and the portability reasoning are in the renderer spec R10.1 (pipeline creation time was not measured, and the spec says so). A shader declares `// permutation-options: ...`, a `<name>.perm` manifest lists up to 32 keys of up to 16 options, `shader_mint --permute` compiles each key itself, dedupes identical modules and commits `<name>.perm.inc`; `renderer::FindPermutation` returns the module or an error naming the shader and key, never a fallback. No RHI change. Test shader `permute_test.frag` with a GPU analytic probe per key; `shaders_check` regenerates and compares the tables. Docs 1797/1797, 11/11 samples. High-effort review (10 findings, all fixed) in the day file. Next: Day 73.

Phase 5 Day 73 (renderer foundation) is done: `renderer::Camera`, `Frustum`, bounds, `MeshData`/`GpuMesh` with cube, sphere and plane builders, `RenderScene` (generation-checked ids, lights, culling) and `FrameResources` (frame uniform plus one per-object buffer, three frame regions); the camera helpers moved out of the demo. 100k-object cull about 3 ms (recorded). Docs 1979/1979. High-effort review (8 findings, 4 fixed, 3 declined, 1 process) in the day file. Next: Day 74.

Phase 5 Day 74 (materials) is done: `renderer::MaterialSystem` with templates and materials validated against reflection (set 0 frame / 1 material / 2 objects), per-material parameter buffer with three frame regions and a batched flush, bind groups rebuilt only for the material whose texture changed, default textures, a pipeline cache keyed by (template, permutation, target, vertex layout) with hit/miss counters, and `MakeDrawSortKey` (pipeline, material, mesh). A GPU test draws three objects through three materials by `first_instance` and reads pixels back. Finding: use `SV_VulkanInstanceID` for the object index (D3D12/Metal need another route, filed). Docs 100%. High-effort review (9 findings, 7 fixed, 1 declined, 1 process) in the day file. Next: Day 75.

Phase 5 Day 75 (render graph I: declaration and compilation) is done: `renderer::RenderGraph` with virtual transient and imported resources, passes that declare reads/writes with a usage that fixes the resource state, edges derived from declaration order (raw/waw/war) plus explicit `AddDependency`, a stable topological schedule, culling of unused passes (roots: side effects and writes to imports), first/last use per resource, a text dump and Graphviz writer used as test oracles, and named compile errors. All graph storage comes from a per-frame arena (a test counts zero heap allocations). A 1000-pass random DAG matches an independent reference (0.18 ms). Docs 100%. High-effort review (8 findings, 7 fixed, 1 declined) in the day file.

Phase 5 Day 76 (render graph II: automatic barriers and transient resources) is done: `Compile` plans the barrier list (state tracking from each resource's usage sequence, merged reads, same-state write ordering, import entry and exit states such as swapchain to `Present`) and checks transient usage flags; `RenderGraph::Execute` records each pass's barriers before it and takes transients from the new `renderer::TransientPool` (reuse by description across frames and across non-overlapping lifetimes in one frame, idle destruction, eviction when full, counters). Null-device oracle tests (a deferred-shaped graph, zero errors, four frames) and a real-device four-pass test with validation and synchronization validation (zero messages) pass; mutations confirmed both bite. New shader `graph_sample.frag`. Docs 100%. High-effort review (10 findings, 6 fixed, 3 declined, 1 noted) in the day file. 

Phase 5 Day 77 (render graph III: execution, parallel recording, swapchain) is done: transients are resolved first and each scheduled pass records into its own command list, in parallel on `jobs::Scheduler` participants, submitted once in schedule order (`RenderGraph::ExecuteParallel`); `renderer::Renderer::RenderFrame` runs the frame loop (acquire/import target, build, compile, record, submit, present) with `Ok/Skipped/DeviceLost/Failed` statuses, minimize/resize/loss handling and a presented swapchain image on every acquired path. Tests: 1000 frames without growth, ordering of lists, parallel versus serial pixel equality on a real device, device loss, an allocation-failure sweep, TSan on the null parallel tests. Docs 100%. High-effort review (8 findings, 6 fixed, 1 declined, 1 process) in the day file. Next: Day 78.

Phase 5 Day 78 (mesh vertex colors & primitive topology) is done: `GraphicsPipelineDesc::primitive_restart` (Vulkan `primitiveRestartEnable`, validated: strip topologies only) with GPU strip tests under validation and sync validation; `MeshData`/`GpuMesh` carry a topology and restart flag; `ColorVertex`/`ColorVertexLayout` (UNorm8x4 sRGB color at location 4, interleaved) with `MakeColoredMesh`/`MakeUniformColoredMesh`; `MakePlaneStripMesh`, `StripToList`, `ListToStrip` (restart or degenerate joins); `MaterialSystem::GetPipeline`/`Warmup` take topology and restart and key the pipeline cache on them. Docs 100%, guide section "Vertex colors and primitive topology". High-effort review (8 findings, 3 fixed, 4 declined with reasons, 1 process) in the day file. Next: Day 79.

Phase 5 Day 79 (the G-buffer geometry pass) is done: `renderer::GBufferPass` (new library `renderer_passes`) culls, sorts by `MakeDrawSortKey` and draws the scene into albedo (`RGBA8_SRGB`), octahedral normal (`RG16_Float`), material (`RGBA8_UNorm`) and `D32_Float` depth with instanced runs; Slang shaders `gbuffer.vert`/`gbuffer.frag` (permutations normal map, alpha test, vertex color) and a `GBufferDebugView`; named rejections for unprepared meshes, missing materials and vertex-color mismatches; cached frame/object bind groups in `MaterialSystem`. Real-device tests (validation and sync validation silent) read every layer back and compare against CPU values. Docs 100%, guide section "The G-buffer geometry pass". High-effort review (10 findings, all fixed except the per-frame pipeline lookup cache and the first-message-only error, declined with reasons) in the day file. Next: Day 80.

Phase 5 Day 80 (deferred lighting: directional and ambient) is done: `LightType::Ambient`, a per-frame light storage buffer (`FrameResources::WriteLights`), `renderer::LightingPass` (HDR `RGBA16_Float` accumulation; world position rebuilt from depth; Lambert + GGX metallic-roughness in one shader function; background skipped), `renderer::TonemapPass` (exposure + ACES fit, sRGB through the target format), the CPU twins `ShadeLight`/`ShadeAmbient`/`TonemapAces`/`ReconstructWorldPosition`, and a `WorldPosition` debug layer. Real-device tests (validation and sync validation silent) match the CPU model at probes, sweep the light direction, check positions against planted geometry and compare a cube against the Phase 4 forward Lambert values. Docs 100%, guide section "Deferred lighting and the tonemap". High-effort review (9 findings, 7 fixed, 2 declined) in the day file. Next: Day 81 (not started; the request covered Days 77-80).

Phase 5 Day 83 (renderer hardening & performance) is done: failure-injection tests (`tests/renderer/renderer_fault_test.cpp`, plus real-device sweeps in `tests/rhi/vulkan_renderer_demo_test.cpp`) lose the device between frames, in the frame callback and in each pass, refuse a transient, lose a texture file, leave out an object with no pipeline and fail the G-buffer and lighting pipeline builds, each ending with a defined status, a logged cause and nothing left alive; the sweep found and fixed a null command-list crash in `demo::CreateSampledTexture`. All 118 assert/log sites in `engine/renderer` are classified (asserts = programming errors, logs = environment) and the missing `Aborts:` lines added; the guide has a failure-behavior table and performance notes. `game/renderer_bench` (run by hand: `renderer_bench [--section graph|frame|lights|pool] [--device null]`, Release build) measured 0.08 us per object (10000 objects in 0.78 ms, 16 draws), 0.05 us per graph pass, 0.1-0.2 us CPU per point light and a 99% transient pool hit rate; the profile and the budgets for the sanitizer flavors are in the day file. 1832 tests pass in plain, ASan and TSan; docs 100%. High-effort review (9 findings, 8 fixed, 1 noted) in the day file. Next: Day 84 (portability review & Phase 5 wrap-up).
Phase 5 Day 84 (portability proof & wrap-up) is done and **Phase 5 is complete**: `shaders_portability` compiles all 24 shaders and 39 permutation keys to HLSL and Metal (76 outputs; `fill.comp`/`saxpy.comp` cannot go to Metal because of `GetDimensions`, listed with the reason), `docs/superpowers/specs/2026-11-04-renderer-portability-review.md` gives a verdict per item (4 shims, 1 deferred change, no interface redesign; one cheap change made: `CheckGBufferFormats` checks the color attachment limit), `docs/guide/renderer.md` is finished and marked draft, `rhi-shaders.md` is reviewed, notes added to phases 6-14. Final run: 1839 tests pass in plain, ASan and TSan; docs 100%; both demos ran 1000 frames with validation on Wayland and on X11 with no messages and flat memory. High-effort review (9 findings, 6 fixed, 3 declined or noted) in the day file. **Next: Phase 6 (Windows port + DX12), which needs its day files written and committed before it starts.**
**Roadmap change (2026-11-05, later):** three phases were inserted after Phase 5 and before the Windows port: **5 Test scoping**, **6 Debugging facilities** (stack traces, core dumps, minidumps, assert/ensure macros) and **7 RHI reset** (any rendering setting changes at run time by re-initializing the RHI; device loss recovers through the same path). Everything from the old Phase 6 moved up by 3 (Windows is Phase 12, macOS 9, Font 10, UI 11, ECS 12, Physics 13, Audio 14, Assets 15, Scripting 16, Android + iOS 17, Consoles 18, Unreal import 19-20, Editor 21, First playable demo 22). Earlier paragraphs in this file keep the numbers they were written with. The new phases have plan files at goal level (`plans/phase-06-test-scoping.md`, `phase-07-debugging-facilities.md`, `phase-08-rhi-reset.md`) and need day files before any of them starts. The day files for Phase 6 (Days 85-90, `plans/phase-06-test-scoping/`) are written: inventory and design, phase labels (map file plus a CMake function), the RHI/renderer labels, the `scripts/test-phase.sh` runner with touched-module selection, workflow and policy docs, and measurement with the closing full run. **Next: Day 85 (nothing started; the request was to plan the days).**

**Roadmap change (2026-11-05):** the Windows and macOS ports moved from Phases 16-17 to **Phases 6 and 7**, right after the renderer core, and the later phases were renumbered (7 Font, 8 UI, 9 ECS, 10 Physics, 11 Audio, 12 Assets, 13 Scripting; 14 is now Android + iOS). Earlier paragraphs in this file keep the numbers they were written with. Phase 5 is still the active phase; Phases 6 and 7 need day files before they can start. See the 2026-11-05 entry in `PLAN.md`.


Phase 5 Day 80a (foundation `Array` and `Vector`; no raw `new[]`/`delete[]`) is done: the growable container is `Vector<T, Allocator = HeapAllocator>` (renamed from `Array`; owns and frees heap storage, `Resize`/`TryResize`/`TryResizeUninitialized`, `Data()`), `Array<T, N>` is the inline `std::array`-like type, and all 50 raw array sites in `engine/` use `Vector`. `ctest -L arrays` (`raw_array_check`, with planted-violation fixtures) fails if `new T[` / `delete[]` reappears in `engine/` or `game/`. 1741 tests pass (plain, ASan, TSan); docs 100%, guide section "Array and Vector: which one". High-effort review (8 findings, 6 fixed, 2 declined) in the day file. Next: Day 81 (not started).

Phase 5 Day 80b (`UniquePtr`, `SharedPtr` and no raw `new`/`delete`) is done: `UniquePtr<T, Deleter>` and `SharedPtr<T, Count>` (atomic or plain count) in `containers/`; `rhi::CreateDevice` returns a `rhi::DevicePtr` (the deleter waits for the GPU) and `DestroyDevice` is gone; the scheduler, Vulkan memory/command-pool/swapchain state, the null device and the async read state are owned by `UniquePtr`; the two leaked singletons are gone (`FallbackSlots` is an atomic bitmask table, `FramePool::Global()` lives in static storage). `ctest -L arrays` (`raw_new_check`) bans every raw `new`/`delete` in `engine/` and `game/`. 1754 tests pass (plain, ASan, TSan); docs 100%. High-effort review (10 findings, 8 fixed, 2 declined) in the day file. Next: Day 81 (not started).

Phase 5 Day 81 (point lights & light volumes) is done: `PointLightAttenuation` (windowed inverse square, exactly 0 at the radius), `kMaxLights` = 16384 with counted, logged overflow (`LightSummary::dropped`), frustum-culled and distance-sorted point lights in one storage buffer (`WriteLights(scene, frame_data)`), and `LightingPass` drawing one additive, front-face-culled, scissored sphere volume per light with one pipeline (the sphere is generated in the vertex shader; new `point_light.vert`/`point_light.frag`). Real-device tests (validation and sync validation silent) match the analytic model, light a camera inside a volume, sum overlapping lights, check that a culled light issues no draw, and handle a light larger than the far plane; measured at 1080p: 1 / 100 / 1000 lights = 0.6 / 1.1 / 5-6 ms wall time. 1785 tests pass; docs 100%, guide subsection "Point lights". High-effort review (8 findings, 5 fixed, 2 declined, 1 process) in the day file. Next: Day 82 (renderer demo & depth correctness).

Phase 5 Day 82 (the renderer demo & depth correctness) is done: `game/renderer_demo` (540 objects, eight materials, 300 point lights, a sun, orbit and fly cameras, `--gbuffer`/`--lights`/`--objects`/`--screenshot`/`--validation`, device-loss exit code 3) built on `renderer::Renderer` and the render graph; procedural textures written by `renderer_demo_mint` and checked byte for byte; a golden image, draw-order independence (shuffled add orders give identical pixels) and a crossing-planes depth test with analytic probes, validation silent; `ctest -L demo` covers screenshots and fault injection; README and the tutorial `hello-renderer.md`. Milestone: 1000 frames with `--validation` on Wayland and on X11, zero messages, 1.27 / 1.28 ms per frame, memory flat (details in the day file). 1807 tests pass; docs 100%. High-effort review (10 findings, all fixed) in the day file. Next: Day 83 (done below).
Phase 6 Day 85 (test inventory & scoping design) is done (planning only): `docs/superpowers/specs/2026-11-05-test-scoping-design.md` has the inventory (1839 tests; executables alone: foundation 22.7 s, jobs 4.6, docgen 4.2, renderer 5.0, rhi 49.0), the per-file phase assignment with the ambiguous cases explained, the label mechanism (Catch2 v3 has no per-file tag, so a small patch to the vendored `CatchAddTests.cmake` behind an `engine_discover_tests` wrapper), the `ctest -L` combinations (repeated `-L` is AND, `|` is OR, `-LE` excludes), the decisions (labels `phase0`..`phase5`, `phase3`; `tests/phases.txt`; `CURRENT_PHASE`; scoped = current phase + touched modules; always-full triggers; flavors are labels `asan`/`tsan`) and the risk table. **Next: Day 86 (phase labels).**
Phase 6 Day 86 (phase labels for every test) is done: `tests/phases.txt` is the one place a test's phase is decided; Catch2 tests get a `phaseN` label (`phase3` for 2.5) through `engine_discover_tests` (`cmake/EngineTestPhases.cmake`, a marked patch to the vendored `CatchAddTests.cmake`; a Catch2 test file missing from the map stops the build), other tests through `engine_label_tests`; `test_phase_labels` (with planted unlabeled/doubled fixtures and an unused-line check) enforces exactly one label. Counts: phase0 87, phase1 63, phase2 232, phase3 340, phase4 580, phase5 537, phase6 4 = 1843; names unchanged; the patch also made the ASan docgen tests carry their `asan` label. The rhi/renderer call sites were switched on the same day. 1843/1843 pass. High-effort review (9 findings, 6 fixed, 3 declined or process) in the day file. **Next: Day 87 (no-GPU discovery check, flavor coverage, per-phase baseline times).**
Phase 6 Day 87 (labels for the RHI and renderer, flavors) is done: discovery builds with no Vulkan driver; `flavors` lines in `tests/phases.txt` state each phase's sanitizer flavors and `test_phase_labels` enforces them (two planted fixtures); a `;`-in-test-name counting bug in the check was fixed. Baseline scoped times (wall, `-j16`): phase 4 plain 17 s / ASan 39 s, phase 5 plain 13 s / ASan 38 s, the whole suite about 65 s. 1845/1845 pass. High-effort review (9 findings, 6 fixed, 2 declined, 1 process) in the day file. **Next: Day 88 (the `scripts/test-phase.sh` runner and touched-module selection).**
Phase 6 Day 88 (the runner) is done: `CURRENT_PHASE` (5), `tests/module_map.txt`, and `scripts/test-phase.sh [N | --current | --affected | --all] [--flavor plain|asan|tsan|all] [--jobs N] [--dry-run]`, which builds, counts, runs the selected labels and ends with `ran X of Y tests (phases ...; flavors ...; display ...)`; `--affected` adds the phases of every module touched since `CURRENT_PHASE` last changed and runs everything for CMake files, `cmake/`, `third_party/`, `engine/foundation` headers, `tests/phases.txt` or a path in no map line. 29 `test_phase_script_*` cases (counts checked against `ctest -N`, planted regression verified). **`CURRENT_PHASE` is changed in the commit that starts a phase.** 1874/1874 pass. High-effort review (10 findings, 9 fixed, 1 declined) in the day file. **Next: Day 89 (workflow policy and docs).**
Phase 6 Day 89 (workflow, policy and docs) is done (documentation and rules only): `CLAUDE.md` has the scoped-test rule (run `--affected` before the review, quote the `ran X of Y tests` line, close a phase with `--all --flavor all`), `STATUS.md` has the build and test commands, `PLAN.md` the guiding principle, the guide "Test phases and scoped runs", and the Phase 7 and 8 plans a closing-run deliverable. **Next: Day 90 (measurements, planted regressions, the closing full run, Phase 6 complete).**
Phase 6 Day 90 (measurement and wrap-up) is done and **Phase 6 is complete**. Closing run: `ran 1874 of 1874 tests (phases: all; flavors: all) in 67 s`, all pass; `-j1` takes 341 s; docs 4/4; both demos 1000 frames with validation on Wayland and X11, silent. Savings: a new phase's own tests ~1.4 s, an RHI/renderer day 638 tests (26 s), a foundation day 871 tests (38 s). Planted faults: a foundation allocator overlap is caught by `--affected`; an RHI generation fault is missed by `--current` and caught by `--affected` and `--all`; two milder allocator faults were caught by no test (KNOWN-ISSUES). `CURRENT_PHASE` is now 6. **Next: Phase 7 (Debugging facilities) needs its day files written and committed before it starts.**

Phase 7 Day 91 (library selection and spikes) is done (planning and spikes only, no code): libbacktrace (commit `0b9b49cf4a`) and sentry-native 0.17.6 with the Breakpad backend and no transport are chosen (`PLAN.md` assumption #20, spec `docs/superpowers/specs/2026-11-06-debugging-design.md`); the Crashpad backend was rejected because on Linux it needs libcurl and a handler executable with about 25 shared libraries. Days 92-102 are corrected to match. `CURRENT_PHASE` is 6. **Next: Day 92 (death-test helper and the `tests/debug` suite).**

Phase 7 Day 92 (death-test helper, fault injection and the `tests/debug` suite) is done: `debug::RunInChild` re-executes the test program (no `fork`), runs a function registered with `ENGINE_DEATH_TEST_FN` and returns exit code, fault (`CrashReason` plus the native code), timeout and captured output; `debug::InjectFault(FaultKind)`; Linux backend under `engine/foundation/os/linux/debug/`; `tests/debug` in plain, ASan and TSan (67 tests, 160 repeat runs clean, also non-unity and clang); `tools/platform_neutral_check` keeps platform types out of the neutral headers and the library headers out of every file but their one implementation file. Docs: module `foundation/debug` 62/62, guide `foundation-debugging.md`. Scoped runs: `ran 894 of 1939 tests (phases: all; flavors: plain; display: included)`, asan `ran 655 of 1939`, tsan `ran 390 of 1939`, all pass. High-effort review (10 findings; 8 fixed, 1 declined, 1 bookkeeping) recorded in the day file. **Next: Day 93 (assert, ensure, verify family).**

Phase 7 Day 93 (assert, ensure, verify family and the handler chain) is done: `ENGINE_ASSERT`, `ENGINE_ENSURE` (once per call site, returns the condition), `ENGINE_VERIFY`, `ENGINE_CHECK_SLOW`, `ENGINE_UNREACHABLE`, `ENGINE_NOT_IMPLEMENTED`, `SetAssertHandler` and the default handler (`ASSERT FAILED: file:line function: condition: message`, then abort, break into a debugger, or for ensure continue); a failed assert now dies with `SIGABRT` instead of `exit(1)`; `IsDebuggerAttached`, `debug/compiler.h`; CMake `ENGINE_FORCE_DEBUG_CHECKS`/`ENGINE_FORCE_SLOW_CHECKS`; the exit sink is gone (two tests migrated); the gate bans the per-translation-unit macros in headers. 152 Phase 7 tests (also non-unity, clang, Release). Scoped runs: `ran 925 of 2024` plain (one known display failure), `ran 682 of 2024` asan, `ran 417 of 2024` tsan. High-effort review (9 findings; 6 fixed, 2 declined, 1 bookkeeping) in the day file. Release builds found a Day 92 bug (fixed) and a pre-existing jobs failure (KNOWN-ISSUES). **Next: Day 94 (vendoring libbacktrace and the stack-trace facade).**

Phase 7 Day 94 (libbacktrace and the stack-trace facade) is done: libbacktrace (commit `0b9b49cf4a`) is vendored under `third_party/libbacktrace/` with our CMake wrapper; `debug/stack_trace.h` captures, resolves (function, file, line, inlined frames, module and offset) and formats traces with no library type in sight, `debug/module_list.h` names the module of an address, and a failed `ENGINE_ASSERT` or the first failure of an `ENGINE_ENSURE` site prints its stack trace under the message in one write; the gate lets only `stack_trace_backtrace.cpp` include `backtrace.h`. 201 Phase 7 tests (also non-unity, clang, Release; the signal-handler test runs in all three flavors). Scoped runs: `ran 942 of 2073` plain (one known display failure), `ran 698 of 2073` asan, `ran 433 of 2073` tsan. High-effort review (10 findings; 9 fixed, 1 declined) in the day file; the new concurrency test found a real hang in the first init ordering (fixed). **Next: Day 95 (vendoring the crash library, sentry-native with the Breakpad backend, and the crash facade).**

Phase 7 Day 95 (sentry-native and the crash facade) is done: sentry-native 0.17.6 is vendored under `third_party/sentry_native/` (Breakpad backend, no network transport, 6.8 MB) behind `debug/crash_handler.h` (`InstallCrashHandler`, `UninstallCrashHandler`, `CrashHandlerOptions` with no address or account field); a crash leaves an envelope with a minidump of every thread that survives the next start (a local `saved/` transport; the null transport would have deleted it), the process still dies from the original fault, the dump directory is private to the user; `tools/crash_extract` pulls the minidump out of the envelope; `os::GetCrashDumpDirectory`, `os::EnsureDirectory`, `os::EnsurePrivateDirectory`; ctests check that no test binary links or imports anything network. 283 Phase 7 tests (the crash flows also in ASan and TSan; non-unity, clang, Release). Scoped runs: `ran 976 of 2155` plain (one known display failure), `ran 722 of 2155` asan, `ran 457 of 2155` tsan. High-effort review (9 findings; 8 fixed, 1 partly) in the day file; it found that dumps were deleted on relaunch. **Next: Day 96 (crash report content).**

Phase 7 Day 96 (crash report content) is done: `debug/crash_context.h` (build id from the binary's GNU note, platform, memory, command line), a lock-free 256-line log ring (`ReadRecentLogLines`, 33-48 ns per log call in Release including the append), `RegisterCrashStateProvider` (16 entries), `WriteCrashReport`/`WriteCrashContext` (fixed section order, written in small chunks, no allocation), thread names, and failed asserts now print the same context; the build id is the Sentry release. 346 Phase 7 tests. Scoped runs: `ran 997 of 2218` plain (one known display failure), `ran 743 of 2218` asan, `ran 478 of 2218` tsan. High-effort review (9 findings; 7 fixed, 1 declined, 1 deferred to Day 97) in the day file. **Next: Day 97 (crash handler integration).**

Phase 7 Day 97 (crash handling end to end) is done: a crash prints a report on standard error and to `crash-report.txt` (attached to the envelope), the library writes the minidump, and the process dies from the original signal; works for every fault kind, a stack overflow, other threads (`RegisterThreadForCrashHandling`), two threads at once, a faulting provider (`double fault`), a failed assert, and the dump opens in `lldb`. Design change: a recorder in front of Breakpad's handler writes the report, because the `on_crash` hook gets no signal information and runs on a helper thread (day file). `tools/signal_safety_check` gates the crash-path code. 395 Phase 7 tests. Scoped runs: `ran 1016 of 2267` plain (one known display failure), `ran 758 of 2267` asan, `ran 493 of 2267` tsan; 20 repeats clean. High-effort review (10 findings; 7 fixed, 2 declined, 1 deferred to Day 101) in the day file. **Next: Day 98 (core dumps).** Local commits only: the remote rejected the history rewrite (see the conversation), so nothing after `61c7ee7` is pushed.

Phase 7 Day 98 (core dumps) is done: `EnablePostMortemDumps` (neutral interface, Linux implementation: core limit, dumpable, `core_pattern`), `CrashHandlerOptions::post_mortem_dumps` with a `core dumps:` line in the crash report, `scripts/find-core.sh` and `scripts/open-core.sh` (systemd-coredump and pattern files, build-id check, gdb batch), the guide section, and a gdb end-to-end test. The log ring got a real fix (a lapped writer could corrupt a slot). 425 Phase 7 tests. Scoped runs: `ran 1026 of 2297` plain (one known display failure), `ran 768 of 2297` asan, `ran 503 of 2297` tsan. High-effort review (10 findings; 9 fixed, 1 partly) in the day file. The gdb test failed twice in loaded runs and could not be reproduced (`KNOWN-ISSUES.md`). **Next: Day 99 (dump tooling and symbols).** Commits are local only (the remote rejected the history rewrite).

Phase 7 Day 99 (dump tooling and symbols) is done: `scripts/crash-report.sh` (envelope or minidump to a trace with `lldb`, `minidump-stackwalk` when present, build-id check), `scripts/store-symbols.sh` (symbol store by build id), `scripts/split-debug.sh` and the CMake option `ENGINE_SPLIT_DEBUG_INFO`, the guide sections and the `llvm-symbolizer` recipe. 446 Phase 7 tests. Scoped runs: `ran 1033 of 2318` plain (one known display failure), `ran 775 of 2318` asan, `ran 510 of 2318` tsan. High-effort review (10 findings; 7 fixed, 2 declined, 1 for the next commit) in the day file. The Breakpad `.sym` path is documented but not exercised (`KNOWN-ISSUES.md`). **Next: Day 100 (subsystem integration).** Commits are local only (the remote rejected the history rewrite).

Phase 7 Day 100 (subsystem integration) is done: `debug::StartProgramDebugging` / `RunCrashTest` (build line, handler by flag, environment or build default, `--crash-test=segv|abort|assert|ensure`) in all four demos and benches with ctests per kind and a no-network check each; `SetLogErrorTraces`; crash state helpers (`SetCrashStateText`, `CrashStateCounter`) used for the `gpu`, `frame` and `window` lines; job workers and the I/O thread register by name; device loss and validation errors print an ensure trace; Release behavior recorded. 483 Phase 7 tests. Scoped runs: `ran 1054 of 2357` plain, `ran 784 of 2357` asan, `ran 519 of 2357` tsan. High-effort review (9 findings; 6 fixed, 2 declined, 1 partly) in the day file; it exposed and fixed a deadlock when the installing thread had too small a signal stack. **Next: Day 101 (Windows/macOS readiness).** Commits are local only (the remote rejected the history rewrite).

Phase 7 Day 101 (Windows and macOS readiness) is done: the platform audit (design spec section 10, with the known gaps and their decisions), `BACKEND.md` checklists for Windows and macOS kept in step with the headers by a new `Backend:` doc label and the ctest `debug_backend_lists`, `tests/debug/BACKEND-CONFORMANCE.md`, cross-target syntax checks for `x86_64-pc-windows-msvc` and `arm64-apple-macos13` (which found and fixed a clang-on-Windows bug in `ENGINE_NOINLINE_LAMBDA`), the porting guide `docs/guide/porting-debugging.md`, and the real remaining work in the Phase 13 and 14 files. 489 Phase 7 tests. Scoped runs: `ran 1061 of 2365` plain, `ran 785 of 2365` asan, `ran 519 of 2365` tsan. High-effort review (10 findings; 9 fixed, 1 accepted) in the day file. **Next: Day 102 (documentation sweep and closing run).** Commits are local only (the remote rejected the history rewrite).

**Phase 7 (Debugging facilities) is COMPLETE (Days 91-102, 2026-11-06).** Linux: stack traces with names, files and lines (libbacktrace), the assert/ensure/verify family, crash reports with build id, platform, engine state and recent log, minidumps through sentry-native's Breakpad backend with no upload, core dump setup and scripts, dump and symbol tooling, and every demo wired in (`--crash-handler`, `--crash-test`). Windows and macOS are ready (audit, `BACKEND.md`, conformance list, cross-target checks, porting guide). Closing run: `ran 2371 of 2371 tests (phases: all; flavors: all; display: included)`, all passing; docs 4/4; both demos 1000 frames clean on Wayland and X11; six planted regressions caught; measurements in the Day 102 file. End-of-phase high-effort review: 10 findings, 8 fixed, 2 declined. **Next: Phase 8 (RHI reset).**

Phase 8 (RHI reset) **started (Day 103 done, 2026-11-06)**; Days 104-114 are planned under `plans/phase-08-rhi-reset/`. Day 103 (spec and spike, no product code): `CURRENT_PHASE` is 7 (a phase with no test files runs nothing: `test-phase.sh` says so; the `flavors 7` line arrives with the first test file on Day 104); the design spec is `docs/superpowers/specs/2026-11-06-rhi-reset-design.md`. Spike results: a Vulkan device cycle on one window costs about 210 ms (device 120, destroy 47, scene 8), 100 cycles drift +128 KiB resident and 0 descriptors, validation silent, ASan clean apart from about 2 KiB per device held by the driver (suppressed); instance and surface are already created per device; one adapter on this machine (multi-GPU unchecked, `KNOWN-ISSUES.md`); loss then re-creation on the same window works. Scoped run: `ran 1070 of 2384 tests (phases: all; flavors: plain; display: included)`; `ctest -L docs` 4/4. No code review (no product code changed). **Next: Day 104 (settings model).**

Phase 8 Day 104 (settings model) is done: `engine/renderer/render_settings.h/.cpp` (open `SettingsRegistry` with cost classes `None`..`DeviceReset`, `RenderSettings`, `Capabilities`, `DiffSettings`, `ResolveSettings`, a stable text format with `ParseSettings`), 20 test cases in phase 8 (`flavors 7 plain,asan,tsan` now in `tests/phases.txt`; `tests/renderer/` is listed file by file because the phase map refuses two phases for one file), guide `docs/guide/render-settings.md`. Scoped runs: `ran 1090 of 2444` plain, `ran 810 of 2444` asan, `ran 544 of 2444` tsan, all pass; docs 4/4. High-effort review: 7 findings, 5 fixed (a gated setting could not return to its default, text round trip, enum names, a doc contradiction, a duplicated comparison), 2 declined (day file). **Next: Day 105 (adapter enumeration).**

Phase 8 Day 105 (adapter enumeration) is done: `engine/rhi/adapter.h` (`EnumerateAdapters`, `MatchAdapter`, stable Vulkan ids, null fake adapters), `DeviceCreateInfo::adapter` (beats `ENGINE_RHI_GPU`), `Device::Adapter()`, guide section "Choosing a GPU", Phase 13-14 portability notes. Scoped runs: `ran 937 of 2477` plain, `ran 668 of 2477` asan, `ran 400 of 2477` tsan, all pass; docs 4/4. High-effort review: 9 findings, 5 fixed, 4 declined (day file). **Next: Day 106 (device lifecycle).**

Phase 8 Day 106 (device lifecycle) is done: handle epochs (each device seeds its generations from a process-wide serial), `Device::Resources`/`Quiesce`/`InFrame`/`Epoch`, `rhi::DeviceHost` with tiered `DeviceObserver`s, guide "Lifecycle and reset". Scoped runs: `ran 951 of 2517` plain (only the known X11 window test failed), `ran 682 of 2517` asan, `ran 412 of 2517` tsan; docs 4/4. High-effort review: 10 findings, 7 fixed, 3 declined (day file). Gaps filed in `KNOWN-ISSUES.md`. **Next: Day 107 (swapchain and presentation).**

Phase 8 Day 107 (swapchain and presentation) is done: `Swapchain::Reconfigure` with format and HDR requests and truthful results, `SwapchainCaps`, `Device::RecreateSurface` and `DeviceHost::ReconfigureSwapchain/RecreateSurface` (with `OnSwapchainChanged`), `os::Window` fullscreen on Wayland and X11 with Windows/macOS checklists. Scoped runs: `ran 967 of 2555` plain, `ran 694 of 2555` asan, `ran 422 of 2555` tsan, all pass; docs 4/4. High-effort review: 10 findings, 7 fixed, 3 declined (day file). **Next: Day 108 (resource registry).**

Phase 8 Day 108 (resource registry) is done: `renderer::DeviceResources` with tiers and rollback-friendly failure policy, recreatable `GpuMesh`, `GpuTexture`, `GpuSampler`, the demo's textures and meshes registered, guide "Surviving a reset". Scoped runs: `ran 980 of 2593` plain (only the known X11 window test failed), `ran 707 of 2593` asan, `ran 434 of 2593` tsan; docs 4/4. High-effort review: 9 findings, 7 fixed, 2 declined (day file). **Next: Day 109 (recreatable materials, passes, frame data, transient pool).**

Phase 8 Day 109 (recreatable materials, passes, frame data, transient pool) is done: `MaterialSystem` and every pass, `FrameResources` and `TransientPool` rebuild through the registry; materials bind by owner; lazy or eager pipeline rebuild; `Renderer::Rebind`; an offscreen frame is pixel-identical before and after a reset on the real device. Scoped runs: `ran 986 of 2610` plain (only the known X11 window test failed), `ran 713 of 2610` asan, `ran 439 of 2610` tsan; docs 4/4. High-effort review: 11 findings, 10 fixed, 1 declined (day file). **Next: Day 110 (`Renderer::ApplySettings`).**

Phase 8 Day 110 (`Renderer::ApplySettings`) is done: the cheapest sufficient action per cost class, applied at the frame boundary on the owner thread, rollback to the previous settings (device-lost state if that fails), callbacks, stats and a crash-state counter; verified on the null backend and on a real device. Scoped runs: `ran 1002 of 2657` plain (only the known X11 window test failed), `ran 729 of 2657` asan, `ran 454 of 2657` tsan; docs 4/4. High-effort review: 9 findings, 8 fixed, 1 declined (day file). **Next: Day 111 (device-loss recovery).**

Phase 8 Day 111 (device-loss recovery) is done: a lost device or surface recovers through the settings path under `RecoveryPolicy` (immediate first attempt, frame-counted delays, fallback adapter, give-up), mid-frame loss injection at submit and present on both backends with `strikes_left`, tests on the null backend and on the real device. Scoped runs: `ran 1015 of 2694` plain (only the known X11 window test failed), `ran 742 of 2694` asan, `ran 465 of 2694` tsan; docs 4/4. High-effort review: 10 findings, 8 fixed, 2 declined, 2 gaps filed (day file). **Next: Day 112 (reset soak and golden equality).**

Phase 8 Day 112 (reset soak and golden equality) is done: 100 resets on the null backend and on the real device leave the counts, device memory and descriptors unchanged; the demo picture is byte-identical before and after a reset and matches the golden; resets during a frame sequence; each cost class under load; a reset with the scheduler running; six of seven planted regressions caught (the seventh is an equivalent mutant, filed). Scoped runs: `ran 1022 of 2713` plain, `ran 749 of 2713` asan, `ran 470 of 2713` tsan, all pass; docs 4/4. High-effort review: 9 findings, 8 fixed, 1 declined (day file). **Next: Day 113 (demo seams, settings file and cost measurements).**

Phase 8 Day 113 (demo seams and cost measurements) is done: `renderer_demo` changes rendering settings live through `Renderer::ApplySettings` (F1-F10 keys and `--script`), `--gpu`/`--list-gpus` in both demos, `--prewarm`, `Renderer::ForceDeviceReset`, `Device::InjectDeviceLoss`, per-stage reset timings; the settings-file and `--set` options are deliberately not there because the engine has no settings system yet (filed). Measurements (AMD Radeon 860M, 540 objects, 300 lights): swapchain change 3.3 ms, pipeline rebuild 2.9 ms, device reset about 36-40 ms regardless of object count; lazy pipeline rebuild stays the default; no progress display needed. Scoped runs: `ran 1030 of 2725` plain, `ran 751 of 2725` asan, `ran 472 of 2725` tsan, all pass; docs 4/4; both demos 1000 frames clean on Wayland and X11 with validation. High-effort review: 9 findings, 8 fixed, 1 declined (day file). **Next: Day 114 (portability, docs, closing full run, wrap-up).**

**Phase 8 (RHI reset) is COMPLETE (Days 103-114, 2026-11-06).** Settings change at run time through `Renderer::ApplySettings` with the cheapest sufficient action and rollback; the device, swapchain and every owner can be replaced and rebuild from CPU-side sources; a lost device or surface recovers under `RecoveryPolicy`; `renderer_demo` has live keys F1-F10 and `--script`; the lifecycle checklist and conformance suite for DX12 and Metal are in `engine/rhi/BACKEND-LIFECYCLE.md` and `tests/rhi/lifecycle_conformance_test.cpp`. Closing run: `ran 2737 of 2737 tests (phases: all; flavors: all; display: included; crash tests: included)`, all passing; docs 4/4; both demos 1000 frames clean on Wayland and X11 with validation, plus 100 scripted resets on each. End-of-phase high-effort review: 9 findings, 9 fixed or reverted (day file). Needed next: an engine settings system, and the backend registry (Phase 13). **Next: Phase 13 (Windows port + DX12 backend): its day files must be written and committed before it starts.**

**Plan change (2026-11-06): two phases inserted, everything after Phase 8 renumbered by two.** New **Phase 9, File handling** (`io::Path`, the `io::File` interface with sync and async read/write, `io::Directory`; all existing file-access code ported; an access gate) with day files for Days 115-124 under `plans/phase-09-file-handling/`, and new **Phase 11, Settings system** (`plans/phase-11-settings-system.md`, goal level; needs Phase 9 and its own design spec; the demos' `--settings`/`--set` options wait for it). Windows/macOS are now Phases 12/13, font and text 12, UI 13, ECS 14, physics 15, audio 16, assets 17, scripting 18, Android/iOS 19, consoles 20, Unreal import 21/22, editor 23, first demo 24, feature parity 25. `CURRENT_PHASE` is still 7 and the code is untouched. **Next: Phase 9 Day 115 (design spec and inventory).**


Phase 9 (File handling) **started (Day 115 done, 2026-11-06)**; Days 116-124 are planned under `plans/phase-09-file-handling/`. Day 115 (spec, inventory, spikes; no product code): `CURRENT_PHASE` is 8 (a phase with no test files runs nothing; the `flavors 8` line, the `io` module in `docs/reference/modules.txt` and the `tests/module_map.txt` line arrive on Day 116 with the first header and test, because their gates require them to exist); the spec is `docs/superpowers/specs/2026-11-06-file-layer-design.md` with the exact inventory (about 15 production sites, about 40 test files), the ten decisions confirmed and four `File` contract changes from Spike B. Spike A: `File::ReadAsync` through a foundation `IoExecutor` costs 2.32 us/op vs 2.28 us/op direct on `IoThread` (+1.7%, noise), `co_await` adapter 1.67 us/op, no foundation-to-jobs include. Spike C: 60-input tables per `PathStyle` are the spec appendix and Day 116's test data. Scoped run: `ran 1036 of 2737 tests (phases: all; flavors: plain; display: included; crash tests: excluded)`, all pass; `ctest -L docs` 4/4. No code review (no product code). **Next: Day 116 (`io::Path`).**

Plan note (2026-11-06): Phase 11's config class is an interface (`ConfigStore`) with the file-backed store built there and the Windows registry store (and other platform stores) as later backends; recorded in `plans/phase-11-settings-system.md` and `plans/phase-13-windows-dx12.md`.

Phase 9 Day 116 (`io::Path`) is done: `engine/foundation/io/path.h/.cpp` (`PathStyle`, `Path`, `PathBuilder<N>`), the Spike C table as `tests/io/path_table.inc`, 11 test cases / 16878 assertions in `tests/io/` (own executables: plain, ASan, TSan; `flavors 8 plain,asan,tsan`, `io` module in `docs/reference/modules.txt`, `engine/foundation/io` in the module map), `StringView::LastIndexOf/StartsWith/EndsWith/Substring(start)`, guide `docs/guide/io.md` (draft, two run samples), reference regenerated, baseline 3180/3180. Plan amendments (no allocator template or `PathView`; no trailing-separator helpers; builders abort, `Parse`/`TryJoin` report) are in the day file. Scoped runs: `ran 1048 of 2771 tests (phases: all; flavors: plain; display: included; crash tests: excluded)` (only the known X11 window test failed), `ran 765 of 2771 ... flavors: asan`, `ran 485 of 2771 ... flavors: tsan`; `ctest -L docs` 4/4. High-effort review: 9 findings, 7 fixed, 2 declined (day file). **Next: Day 117 (errors, `FileInfo`, file-system operations).**

Phase 9 Day 117 (errors, `FileInfo`, file-system operations) is done: `io/error.h` (`Error`, `Status`, `ErrorFromOsCode` with a tested table), `io/file_system.h` with the Linux backend (`Stat`/`Lstat`, `Remove`, descriptor-based `RemoveAll` with guards, `Rename`, `Copy`, `CreateDirectories`, `CreatePrivateDirectory`, temp files and directories, `Absolute`/`Canonical`/`ReadLink`, special folders including `UserStateDirectory`), 11 more test cases (22 in `tests/io/`, 17088 assertions, plain/ASan/TSan), guide sections Errors / Looking at the disk / Special folders, baseline 3250/3250. `XkbKeyboard` added to the X11 unity wrappers (adding sources shifted the unity grouping). Scoped run: `ran 1059 of 2804 tests (phases: all; flavors: plain; display: included; crash tests: excluded)` (only the known X11 window test failed); asan `ran 776 of 2804`, tsan `ran 496 of 2804`, all pass; docs 4/4. High-effort review: 10 findings, 9 fixed (a self-copy that destroyed the file, a `RemoveAll` that could delete a different tree than the kernel resolved, the symlink error code, ...), day file. **Next: Day 118 (`io::File` and `LocalFile`).**

Phase 9 Day 118 (`io::File` and `LocalFile`) is done: `io/file.h`/`file.cpp` (abstract `File`, open modes and flags, capabilities, stream position, scheme registry and `OpenLocation` with `file://`, `ReadExact`/`ReadAll`/`ReadFile`/`ReadTextFile`/`WriteFile`, `WriteFileAtomic` with directory sync, symlink following and permission carry-over), `io/linux/local_file.cpp`, `SetPermissions` and `SyncDirectory` in the file-system header, `Error::EndOfFile`, a reusable conformance suite (`tests/io/file_conformance.h`) that Day 119's double will run. 32 test cases / 17301 assertions in plain, ASan and TSan (5 GiB offsets, `/proc` and `/dev/full`, concurrent reads, atomic write with failures injected at each step). Guide sections on files and atomic replacement with two run samples; baseline 3299/3299. Scoped runs: `ran 1069 of 2834 tests (phases: all; flavors: plain; display: included; crash tests: excluded)` (only the known X11 window test failed), `ran 786 of 2834 ... flavors: asan`, `ran 506 of 2834 ... flavors: tsan`; docs 4/4. High-effort review: 10 findings, 8 fixed, 2 declined (day file). **Next: Day 119 (async operations and the network-like double).**

Phase 9 Day 119 (asynchronous operations and the network-like double) is done: `io::IoExecutor`/`IoWork` (the foundation seam; `jobs::IoThread` implements it and `jobs::IoRequest` is now an alias), caller-owned `AsyncRequest` with `File::ReadAsync/WriteAsync/FlushAsync/Cancel`, a built-in executor, `jobs::ReadAsync/WriteAsync/FlushAsync/ReadAllAsync` awaitables, `io::FilePtr`, and `tests/io/slow_file.h` (a file with its own thread, delay, chunked reads, unknown size and injected failures) that passes the same conformance suites as `LocalFile`. 43 test cases / 17540 assertions in plain, ASan and TSan (1000 concurrent reads, cancellation, a completion that destroys the file, executor shutdown, zero allocations on the local completion path in the plain flavor, `co_await` on a 4-participant scheduler). Guide sections "Asynchronous I/O" and "Writing a File backend"; baseline 3351/3351. Scoped runs: `ran 1080 of 2867 tests (phases: all; flavors: plain; display: included; crash tests: excluded)`, `ran 797 of 2867 ... flavors: asan`, `ran 517 of 2867 ... flavors: tsan`, all pass; docs 4/4. High-effort review: 10 findings, 8 fixed (mostly contract clarifications and a default-executor lifetime fix), 2 declined (day file). **Next: Day 120 (`io::Directory`).**

Phase 9 Day 120 (`io::Directory`) is done: streaming `Directory`, `ListDirectory` (sorted option), `MatchesWildcard`, `WalkDirectory` (depth limit, link loop detection, pre/post order, filter, skip/stop, errors reported once per entry), thin create/remove statics, 52 test cases / 58614 assertions in `tests/io/` (plain, ASan, TSan), guide section with a run sample, baseline 3393/3393. The new API (Days 116-120) is complete. Scoped runs: `ran 1089 of 2894 tests (phases: all; flavors: plain; display: included; crash tests: excluded)` (only the known X11 window test failed), `ran 806 of 2894 ... flavors: asan`, `ran 526 of 2894 ... flavors: tsan`; docs 4/4. High-effort review: 9 findings, 7 fixed, 2 declined (day file). **Next: Day 121 (port foundation, jobs and debugging startup code).**

Phase 9 Day 121 (port: foundation, jobs, debugging startup code) is done: `os::ReadFileToArena` (and `os/file_util.*`), `os::Lseek`, `os::EnsureDirectory`, `os::EnsurePrivateDirectory` and `os::GetCrashDumpDirectory` are gone, replaced by `io::ReadFile`, `File::Seek`, `io::CreateDirectories`, `io::CreatePrivateDirectory` and the new `io::CrashDumpDirectory`; `jobs::ReadFileAsync`, `log::LoadConfig`, the core-pattern and `/proc` startup reads, the crash handler's directory setup, `tools/docgen`'s loader and the tests use the file layer. The remaining descriptor primitives are documented (`os::Read/Write/Close/WriteAllWithTimeout`; `os::Open` and `os::CreateFileForWriting` only for the assert-path debugger check and the crash report, the documented signal-safe exceptions). The Phase 7 gates and the crash suite pass unchanged in behavior. Scoped runs: `ran 1255 of 2897 tests (phases: all; flavors: plain; display: included; crash tests: included)`, `ran 807 of 2897 ... flavors: asan`, `ran 527 of 2897 ... flavors: tsan`, all pass; docs 4/4. High-effort review: 9 findings, 6 fixed (a world-readable report file, `ReadFileAsync` on `/proc` files, ...), 3 declined (day file). **Next: Day 122 (port: RHI pipeline cache, textures, demos, test helpers).**

Phase 9 Day 122 (port: RHI pipeline cache, textures, demos, test helpers) is done: the pipeline cache reads and writes through `io` (atomic write, user cache directory, a configured directory that cannot be used means no cache), `ReadRtex`/`WriteRtex` and the PPM writers take paths and return statuses, both demos, the mint tool, `rhi_bench`, the image-compare helper, the golden and cache tests use the file layer and the shared `tests/io/temp_dir.h` fixture; new cases for a cache directory that is a file, unwritable, or too long. The `/proc` process-measurement readers stay raw (exception for the Day 124 gate). Scoped runs: `ran 1256 of 2899 tests (phases: all; flavors: plain; display: included; crash tests: included)`, `ran 808 of 2899 ... flavors: asan`, `ran 527 of 2899 ... flavors: tsan`, all pass; docs 4/4; both demos 1000 frames with `--validation` on Wayland and X11, flat memory. High-effort review: 8 findings, 7 fixed, 1 declined (day file). **Next: Day 123 (port: the offline tools).**

Phase 9 Day 123 (port: the offline tools) is done: `tools/docgen` reaches the file system only through `source_file.h/.cpp` (wrappers over `io`; every generated page and the baseline are written atomically, byte-identical output), `tools/shader_mint` and `tools/crash_extract` read and write through `io` (shader_mint keeps its stage-then-rename of one minting's outputs; crash_extract writes atomically), the seven docgen test files use a shared `io`-based fixture, and a new determinism test builds the reference under shuffled directory orders and compares bytes; a new error-path test shows an unwritable output directory keeps the previous files. `io::WriteFileAtomic` gained `new_file_permissions`. The gates written as CMake scripts and the Python generators have no C++ file access to port. Scoped runs: `ran 1258 of 2903 tests (phases: all; flavors: plain; display: included; crash tests: included)` (only the known X11 window test failed), `ran 810 of 2903 ... flavors: asan`, `ran 527 of 2903 ... flavors: tsan`; docs 4/4. High-effort review: 10 findings, 9 fixed, 1 declined (day file). **Next: Day 124 (the access gate, backend checklists, docs, closing full run).**

**Phase 9 (File handling) is COMPLETE (Days 115-124, 2026-11-06 to 2026-11-07).** `io::Path`, `io::Error`/`Status`, the file-system operations and special folders, `io::File` with `LocalFile` and a scheme registry, asynchronous operations over an `IoExecutor` with `co_await` adapters, `io::Directory`/`WalkDirectory`, a network-like test double that passes the same conformance suite, every access site ported, the `file_access_check` gate (allow list with a reason per entry), `engine/foundation/io/BACKEND.md` with `io_backend_lists` and the Windows/macOS cross-syntax checks, guide `docs/guide/io.md`, `CLAUDE.md` and `PLAN.md` updated. Closing run: `ran 2914 of 2914 tests (phases: all; flavors: all; display: included; crash tests: included)`, all passing; docs 4/4; `rhi_demo` and `renderer_demo` 1000 frames with `--validation` clean on Wayland and X11. End-of-phase high-effort review: 10 findings, 8 fixed (one reverted a Day 121 port that would have allocated on the assert path), 2 declined (day file). Commands added: `ctest -L io` runs the file-layer tests and gates (`file_access_check`, `io_backend_lists`, `io_cross_syntax_*`). **Next: Phase 11 (settings system, `ConfigStore` interface): its day files must be written and committed before it starts; `CURRENT_PHASE` stays 8 until then.**

**Phase 11 (Settings system) is planned (2026-11-07), not started:** Days 130-139 are written as files of checkable sub-tasks under `plans/phase-11-settings-system/` (design spec and inventory, keys/values/registry, the `ConfigStore` interface with doubles and a conformance suite, the text format and `FileConfigStore`, layers/environment/command line, the settings service, versioning and migration, the renderer migration, the demos and other consumers, platform checklists and the closing run) and linked from the phase file with the eleven design decisions Day 130 will confirm. Planning only; no code changed. `CURRENT_PHASE` stays 8 until Day 130's first task. **Next: Phase 11 Day 130 (design spec, inventory and phase setup).**

**Phase 10 (Standard types and container hygiene) is planned (2026-11-07), not started, and runs before Phase 11:** Days 125-129 are written under `plans/phase-10-types-and-containers/` (spec and inventory; `byte`, portable `i128`/`u128` and `Span`; byte buffers and the `game/` port; the Phase 9 test helpers and `shader_mint`'s JSON boundary; the `std_container_check` gate and the closing run). Raised by the user after Phase 9: the engine runtime has no `std::` containers (checked), but `game/` (about 10 uses), `tools/shader_mint` (about 135, partly legitimate) and `tests/` (about 1100, of which the Phase 9 additions are ported) do, and there was no `byte` or 128-bit type. Planning only; no code changed. `CURRENT_PHASE` stays 8 until Day 125's first task. **Next: Phase 10 Day 125 (design spec, inventory, phase setup), then Phase 11 Day 130.**


**Renumbering (2026-11-07): no decimal phases.** The documentation tooling (was "2.5") is now Phase 3 and every phase after it moved up by one: 4 Vulkan RHI, 5 renderer core, 6 test scoping, 7 debugging facilities, 8 RHI reset, 9 file handling (complete, `CURRENT_PHASE` is 9), 10 standard types and container hygiene (Days 125-129, planned), 11 settings system (Days 130-139, planned), 12 Windows/DX12 ... 27 feature parity. Test labels are `phase<N>` for the new numbers (`phase3` is the documentation tooling, `phase9` the file layer); `tests/phases.txt`, `tests/module_map.txt` and the test-phase script and its tests accept whole numbers only. Full suite after the renumbering: `ran 2914 of 2914 tests (phases: all; flavors: all; display: included; crash tests: included)`; the only failure was the known X11 window test. **Next: Phase 10 Day 125 (design spec, inventory, phase setup), then Phase 11 Day 130.**

Phase 10 (Standard types and container hygiene) **started (Day 125 done, 2026-11-07)**; Days 126-129 are planned under `plans/phase-10-types-and-containers/`. Day 125 (spec, inventory, spikes; no product code): `CURRENT_PHASE` is 10 (a phase with no tests runs nothing; the `flavors 10` line, `tests/types/` and the module-map line arrive on Day 126); the spec is `docs/superpowers/specs/2026-11-07-types-and-containers-design.md`. Inventory: `engine/` has no banned `std::` use; `game/` 10; `tools/shader_mint` about 140 (53 in the JSON boundary that stays); the Phase 9 test code 144; the older tests about 1090 (a counted deferral); about 120 `unsigned char`/`uint8_t` spellings in 30 files. Spike A: a portable `u128` (Knuth D) matched `unsigned __int128` on 60 million random checks with 0 mismatches, about 4 times slower than native; Spike B: `Span` works over the real `Vector`/`Array` once `Array` gets its own constructor, and const to mutable is rejected. Scoped run (the working tree changed the phase map, so it is the whole plain suite): `ran 1104 of 2914 tests (phases: all; flavors: plain; display: included; crash tests: excluded)`; only the known X11 window test failed. No code review (no product code). **Next: Day 126 (`byte`, `i128`/`u128`, `Span`).**

Phase 10 **Day 126 done (2026-11-07)**: `byte`, `isize`, portable/native `u128`/`i128`, `Span`/`ByteSpan` with 4 test executables and a guide page; `/code-review high` found 10, 7 fixed, 3 declined (day file). Scoped run: `ran 1136 of 2976 tests (phases: all; flavors: plain; display: included; crash tests: excluded)`; only the known X11 window test failed; asan `ran 825 of 2976`, tsan `ran 542 of 2976`. **Next: Day 127 (byte buffers, `game/` port).**

Phase 10 **Day 127 done (2026-11-07)**: `byte`/`uN` spellings in `engine/` and `game/`, `algorithm.h`, span overloads in the file layer, `game/` free of banned `std::` use; `/code-review high` found 9 (7 fixed, 2 declined). Scoped run: `ran 1149 of 3003 tests (phases: all; flavors: plain; display: included; crash tests: excluded)`; only the known X11 window test failed; asan `ran 832 of 3003`, tsan `ran 549 of 3003`; both demos 1000 frames `--validation` on Wayland and X11 clean. **Next: Day 128.**

Phase 10 **Day 128 done (2026-11-07)**: test support headers (Catch2 printers, `Text`), `tests/io` and the permutation tests on the engine's types, `tools/shader_mint` off `std::` outside `reflect_json.cpp`; review found 10 (8 fixed, 2 declined). Scoped run: `ran 1155 of 3015 tests (phases: all; flavors: plain; display: included; crash tests: excluded)`; only the known X11 test failed; asan `ran 835 of 3015`, tsan `ran 552 of 3015`. **Next: Day 129 (gate, rule text, closing run).**

Phase 10 (Standard types and container hygiene) **complete (2026-11-07)**: Day 129 added the `std_container_check` gate (scans engine, game, tools and all of tests; six older test directories allow-listed with counts), the CLAUDE.md/PLAN.md rule and the guide pass. Closing run: `ran 3030 of 3030 tests (phases: all; flavors: all; display: included; crash tests: included)`; only the known X11 window test failed; `ctest -L docs` 4 of 4; both demos 1000 frames `--validation` on Wayland and X11 exit 0. End-of-phase `/code-review high --max-findings all`: 10 findings, 7 fixed, 3 declined (Day 129 file). Open: `KNOWN-ISSUES.md` (older tests' `std::` use, shaders not regenerated without `slangc`, MSVC build of the portable 128-bit path). `CURRENT_PHASE` stays 10 until Phase 11 Day 130. **Next: Phase 11 Day 130 (settings system design spec).**

Shaders re-checked after the Phase 10 `tools/shader_mint` port (2026-11-07, slangc v2026.18.2 installed): `cmake --build build --target shaders` regenerated all 25 `*.inc` files and `git diff` is empty, so the output is byte-identical; the 16 `shaders`/`shader_mint` tests pass.

Phase 11 (Settings system) **started (Day 130 done, 2026-11-07)**: `CURRENT_PHASE` is 11; spec `docs/superpowers/specs/2026-11-07-settings-design.md` with the exact inventory and three spikes (relaxed-atomic reads at 0.6-0.9 ns, a document format that round-trips with a minimal diff, a `ConfigStore` draft that fits a file, a per-key registry and a failing store). Scoped run: `ran 1168 of 3032 tests (phases: all; flavors: plain; display: included; crash tests: excluded)`; only the known X11 test failed. **Next: Day 131 (keys, values and the registry; first `tests/settings/` and the `flavors 11` line).**

Phase 11 **Day 131 done (2026-11-07)**: `settings::Key`, `Value`, `SettingDesc`/`ParseValue`/`FormatValue`, `Registry`, `Setting<T>`/`TextSetting`, tests in `tests/settings/` (plain/asan/tsan), `docs/guide/settings.md`; review found 10 (9 fixed, 1 declined). Scoped run: `ran 1186 of 3086 tests (phases: all; flavors: plain; display: included; crash tests: excluded)`; only the known X11 test failed; asan `ran 855 of 3086`, tsan `ran 572 of 3086`. **Next: Day 132 (the `ConfigStore` interface).**

Phase 11 **Day 132 done (2026-11-07)**: `ConfigStore` interface, `MemoryConfigStore`, `ReadOnlyConfigStore`, four test doubles, the conformance suite, guide sections; review found 9 (8 fixed, 1 declined). Scoped run: `ran 1198 of 3122 tests (phases: all; flavors: plain; display: included; crash tests: excluded)`; only the known X11 test failed; asan `ran 867 of 3122`, tsan `ran 584 of 3122`. **Next: Day 133 (the text format and `FileConfigStore`).**

Phase 11 **Day 133 done (2026-11-07)**: the settings text format (`ConfigDocument`: parse, edit in place, round-trip byte for byte) and `FileConfigStore` (atomic `Commit`, `.bak`, damaged-file protection, BOM/CRLF), the conformance suite on it; review found 10 (all fixed). Scoped run: `ran 1222 of 3194 tests (phases: all; flavors: plain; display: included; crash tests: excluded)`; only the known X11 test failed; asan `ran 891 of 3194`, tsan `ran 608 of 3194`. **Next: Day 134 (layers, environment and command line).**

Phase 11 **Day 134 done (2026-11-07)**: `LayerStack` with provenance, `EnvironmentSource`, `CommandLineSource`, `SuggestKey`; review found 10 (8 fixed, 2 declined). Scoped run: `ran 1236 of 3236 tests (phases: all; flavors: plain; display: included; crash tests: excluded)`; only the known X11 test failed; asan `ran 905 of 3236`, tsan `ran 622 of 3236`. **Next: Day 135 (the `Settings` service).**

Phase 11 **Day 135 done (2026-11-07)**: the `Settings` service (load report, validation, listeners, apply with rollback, snapshots, save, autosave poll, crash-state text); review found 10 (all fixed; the lock design changed). Scoped run: `ran 1254 of 3290 tests (phases: all; flavors: plain; display: included; crash tests: excluded)`; only the known X11 test failed; asan `ran 923 of 3290`, tsan `ran 640 of 3290`. **Next: Day 136 (versioning and migration).**

Phase 11 **Day 136 done (2026-11-07)**: format and schema versions, migration steps (rename, value map, drop, function), compatibility mode for newer files, unknown-key notes, deprecated keys in layers, the `settings_fixtures` check; review found 10 (all fixed). Scoped run: `ran 1267 of 3321 tests (phases: all; flavors: plain; display: included; crash tests: excluded)`; only the known X11 test failed; asan `ran 932 of 3321`, tsan `ran 649 of 3321`. **Next: Day 137 (the renderer onto the shared system).**

Phase 11 **Day 137 done (2026-11-07)**: the rendering settings in the shared system (`renderer.*` keys, apply hook, rollback, schema-1 migration, fixture); the Phase 8 renderer API and tests are untouched. Review found 10 (all fixed). Scoped run: `ran 1276 of 3348 tests (phases: all; flavors: plain; display: included; crash tests: excluded)`; only the known X11 test failed; asan `ran 941 of 3348`, tsan `ran 658 of 3348`; `renderer_demo` 1000 frames clean on Wayland and X11. **Next: Day 138 (command line, the demos and the other consumers).**

Phase 11 **Day 138 done (2026-11-07)**: both demos read, list, set and save settings (`--set`, `--settings`, `--save-settings`, `--list-settings`, generated `--help`), the F keys and the script go through `Settings::Apply`, the engine's environment variables and the log channels are visible settings; review found 10 (all fixed). Scoped run: `ran 1303 of 3389 tests (phases: all; flavors: plain; display: included; crash tests: excluded)`; only the known X11 test failed; asan `ran 948 of 3389`, tsan `ran 665 of 3389`; both demos 1000 frames `--validation` on Wayland and X11 clean. **Next: Day 139 (platform checklists, the configuration check, the closing run).**

**Phase 11 (Settings system) is COMPLETE (Day 139, 2026-11-07).** Platform checklist `engine/foundation/settings/BACKEND.md` (`settings_backend_lists`, `settings_cross_syntax_*`), registry-shaped store passing the conformance suite, `tools/config_access_check` gate (ctest `config_access_check*`), guide and `CLAUDE.md`/`PLAN.md` (#22) updated, plan notes for Phases 13, 14, 16, 21, 22, 23. End-of-phase review `--max-findings all`: 18 findings, fixed or filed (see the Day 139 file). Closing run: `ran 3441 of 3441 tests (phases: all; flavors: all; display: included; crash tests: included) in 73 s`, only the known X11 window test fails; `ctest -L docs` passes; both demos 1000 frames with `--validation` on Wayland and X11. Next: Phase 13 needs its day files before it starts; `CURRENT_PHASE` stays 11.

**Session-state query (maintenance, 2026-11-07, `plans/maintenance/session-state-query.md`):** `os::QuerySessionState` (logind on Linux), checklists for Windows and macOS, and the window tests of foundation and the RHI skip with the state as the reason when the session is locked, idle or inactive. Scoped runs: plain `ran 1332 of 3444 tests (phases: all; flavors: plain; display: included; crash tests: excluded)`, asan 961, tsan 678; `ctest -L docs` passes; review recorded in the plan file.

**Known-issues pass (maintenance, 2026-11-07, `plans/maintenance/known-issues-pass.md`):** unity warning in `null_device.cpp`, `GrowArray` replaced by `Vector` (`RemoveRange` added), `raw_new_check` scans tools and tests, a TSan/ASan test for `SharedPtr`, arena tests that catch the planted faults, `u128`/`i128` `operator<=>`, stale `tests/io` directories swept, `CreatePrivateDirectory` checks ancestors, saved crash dumps pruned (`keep_saved_dumps`), `os::EnvironmentCount/Entry` under the settings environment source, incremental settings snapshots, old demo flags in the command-line layer. Scoped runs: plain `ran 1341 of 3464`, asan 965, tsan 682; docs pass; review recorded in the plan file.

**Phase 13 (Windows port + DX12 backend) is planned (2026-11-07), not started:** Days 147-174 are written as files of checkable sub-tasks under `plans/phase-13-windows-dx12/` and linked from the phase file with its twelve design decisions, the prerequisites and the per-day rules (two machines: every day that changes shared code quotes `--affected` from the Windows PC and the Linux machine; Windows flavors are plain and asan, tsan stays on Linux; MSVC primary with a `clang-cl` build; the CMake toolchain files find MSVC, the Windows SDK and `clang-cl` themselves, with no Developer Command Prompt). Order: spec and setup (147); CMake/compiler, `os`, test tooling and gates (148-150); file layer (151-152); jobs (153); debugging (154-156); registry settings store (157); Win32 window and input (158-159); Vulkan on Windows, renderer and baselines (160-161); backend registry and bytecode blobs (162); DXIL shader path (163); DX12 device, resources, descriptors, pipelines, command lists, swapchain (164-169); Phase 4 and Phase 5 suites on DX12 (170-171); lifecycle, device loss, GPU and backend switch (172); demos, benches, `clang-cl` (173); guides and closing runs on both machines (174). Windows PC recorded: Ryzen AI 7 350, Radeon 860M, Windows 11 build 26300, MSVC 14.51, SDK 10.0.26100. Planning only; no code changed. It now depends on Phase 12 (below) and starts after it.

**Phase 12 (Module layering) was planned and implemented on 2026-11-07 (complete), and ran before the Windows port (now Phase 13, Days 147-174; every later phase moved up by one):** `ARCHITECTURE.md` measured nine parts of the foundation in one dependency cycle. Days 140-146 under `plans/phase-12-module-layering/`: design spec and measured graph (140), the `module_deps_check` gate with today's ten violations allow-listed (141), the assert report and thread registration as hooks (142), log configuration, `os.h` below strings, placement and `engine_main` (143), windows and input moved to `engine/platform/` (144), one library per layer (145), docs and the closing run (146). Shared code only: every day runs on the Linux machine. Planning only; no code changed. `CURRENT_PHASE` stays 11 until Day 140's first task. **Next: Phase 12 Day 140 (design spec, measured graph, phase setup) on the Linux machine.**

Phase 12 Day 140 (design spec, measured graph, phase setup) is done: `CURRENT_PHASE` is 12; `docs/superpowers/specs/2026-11-07-module-layering-design.md` re-measures the include graph (the nine-module cycle stands, **plus three upward edges `ARCHITECTURE.md` had missed**: `log.h`, `span.h` and `int128.h` include `string/string.h`, now items 11-13) and settles the libraries, the two hooks (`log/diagnostics_hooks.h`), `StringView` moving to the base layer, the `platform/` move (namespace stays `os::`) and the gate's design. Day 141's allow list starts with thirteen entries. Run: `scripts/test-phase.sh --affected` = `ran 1341 of 3464 tests (phases: all; flavors: plain; display: included; crash tests: excluded)`, one failure: `renderer_demo_script_wayland` (segfault inside the NVIDIA driver on the first frame, filed in `KNOWN-ISSUES.md`; it fails the same way with no Phase 12 code); `ctest -L docs` passes. **Next: Day 141 (the `module_deps_check` gate).**

Phase 12 Day 141 (the `module_deps_check` gate) is done: `tools/module_deps_check` maps files, CMake targets and system libraries to modules and layers, fails on an upward or same-layer edge, a forbidden pair, a per-platform include from outside, an unresolved include, an unmapped file or target, a stale or reasonless allow-list entry and any cycle left after the allow list (17 entries, each with its removal day); 14 ctests (phase 12, `flavors 12 plain`), a planted regression in `mem/` fails it. `ARCHITECTURE.md` section 6 and `CLAUDE.md` name it. Run: `ran 1354 of 3477 tests (phases: all; flavors: plain; display: included; crash tests: excluded)`, only the known Wayland demo failure. High-effort review: 4 findings, 1 fixed, 3 declined (day file). **Next: Day 142 (assert report and thread registration as hooks).**

Phase 12 Day 142 (assert report and thread registration as hooks) is done: `log/diagnostics_hooks.h` (`FailureReportHooks`, `SetThreadStartHook`/`NotifyThreadStart`) lets `log`, `io` and `jobs` stay below `debug`; `debug::InstallDiagnosticsHooks()` (called by `InitDebugFacilities`) installs the trace, crash context, debugger check and thread registration; before that an assert prints its message and aborts. `docgen` and `shader_mint` install it first thing; `tests/debug` installs it in `install_hooks.cpp`. Allow list: the two `-> debug` entries deleted (15 left). Runs: plain `--affected --crashes` `ran 1528 of 3489 tests (phases: all; flavors: plain; display: included; crash tests: included)`, ASan `ran 965 of 3489`, TSan `ran 682 of 3489`; only the known Wayland demo failures (now two tests, in `KNOWN-ISSUES.md`). Review in the day file. **Next: Day 143.**

Phase 12 Day 143 (`StringView` to the base layer, `os.h`, log configuration, `engine_main`) is done: `string_view.h` is base-layer; `log.h`, `os.h`, `int128.h`, `span.h` no longer include `string/string.h`; `os::EnvironmentEntry` fills a caller buffer; **`log::LoadConfig` is retired** (nothing called it; use `--set log.<channel>.verbosity=...`, a settings file or `ENGINE_SETTING_<KEY>`); `engine_main` is now `game/hello_window` (`hello_window`, `hello_window_asan`). Six allow-list entries deleted (9 left: the demos' and `hello_window`'s platform includes, the three display libraries, the two RHI entries; `platform` entries go on Day 144). Runs: plain `--affected --crashes` `ran 1527 of 3488 tests (phases: all; flavors: plain; display: included; crash tests: included)`, ASan `ran 965 of 3488`, TSan `ran 682 of 3488`; only the two known Wayland demo failures. **Next: Day 144 (the platform layer).**

Phase 12 Day 144 (the platform layer) is done: windows and input live in `engine/platform/` (`platform/window.h`, `input.h`, `window_host.h`; `platform/linux/` backends; namespace still `os::`), built as the library `platform` (`_asan`, `_tsan`), the only one that links Wayland, xkbcommon and X11; `PlatformWindowHost` has a neutral header and allocates its backend on `Open`; `foundation*` no longer link the display libraries, `docgen` has none (`shader_mint` still does through `renderer -> rhi_vulkan`, item 8, filed). Allow list: only the two RHI entries (Phase 13 Day 162) remain, no cycle. Runs: plain `--affected --crashes` `ran 1527 of 3488 tests (phases: all; flavors: plain; display: included; crash tests: included)`, ASan `ran 965 of 3488`, TSan `ran 682 of 3488`; only the two known Wayland demo failures; both demos 1000 frames with `--validation` on X11 and `rhi_demo` on Wayland (`renderer_demo` on Wayland is the known crash).

Phase 12 Day 145 (one library per layer) is done: the foundation builds as `foundation_base`, `foundation_io`, `foundation_debug`, `foundation_settings` (each with `_asan`/`_tsan`) plus the `foundation` aggregate; `platform` links `foundation_debug`; each layer's tests link only that layer (`foundation_base_tests` is new); the gate knows the four targets and fails an upward link; both unity modes build (three missing includes fixed); build times unchanged (full 27 s before and after). Runs: plain `--affected --crashes` `ran 1527 of 3488 tests (phases: all; flavors: plain; display: included; crash tests: included)`, ASan `ran 1122 of 3488`, TSan `ran 839 of 3488`; only the two known Wayland demo failures.

Phase 12 Day 146 (docs and the closing run) is done, and **Phase 12 (module layering) is complete**: `ARCHITECTURE.md` section 2 is the measured graph (136 module edges, 0 problems, only the two RHI entries for Phase 13 Day 162), `PLAN.md` and the guides match, and the closing run `scripts/test-phase.sh --all --flavor all` printed `ran 3488 of 3488 tests (phases: all; flavors: all; display: included; crash tests: included)`; the only failures are the two known Wayland `renderer_demo` tests (`KNOWN-ISSUES.md`). The end-of-phase review found 2 stale comments, fixed. `CURRENT_PHASE` stays 12 until Phase 13 starts. **Next: Phase 13 (Windows port + DX12), Day 147.**

**Phase 13 (Windows port + DX12) started (Day 147, 2026-11-07, on the Windows PC).** `CURRENT_PHASE` is 13; line endings pinned with `.gitattributes`
(and the Windows checkout's files rewritten to LF, checked file by file); memory linked on Windows (`scripts/link-claude-memory.ps1`); the spec
`docs/superpowers/specs/2026-11-07-windows-port-design.md` with the inventory and three spikes: descriptors become an engine `HANDLE` table (the UCRT's
descriptor lock blocked a crash-path write that `WriteFile` completed), **the object index on D3D12 is a per-instance stream** (14.8 against 21.5 ns per draw
for a root constant; decision 9 amended), and 92 of 97 engine sources compile with MSVC once two GCC spellings are replaced. Not done: the Linux machine's
record and the Linux runs (`ctest -L docs`, `--affected`), and the D3D12 debug-layer check (Graphics Tools not installed); both filed in `KNOWN-ISSUES.md`.
No test run on Windows yet (the build does not configure until Day 148). No code review (setup only). **Next: Day 148 (CMake and the compiler on Windows);
the developer installs Graphics Tools before Day 164.**

Phase 13 **Day 148 (2026-11-07, Windows PC): CMake and the compiler on Windows.** The tree configures from a plain shell with MSVC and `clang-cl` (toolchain files
find Visual Studio, the Windows SDK, LLVM and the Vulkan SDK; a mismatched Developer Command Prompt stops the configure), builds `foundation_base` and the new
`tests/windows` with no warning in both unity modes, and embeds `cmake/engine.manifest` in every executable. `windows.h` goes through `os/windows/win32.h` (the
clashing names measured and restored). Shared changes: `ENGINE_PRINTF_FORMAT`, `ENGINE_TRAP`, `ENGINE_NO_UNIQUE_ADDRESS` in `debug/compiler.h`, and two silent MSVC
bugs fixed (an ignored lambda `noinline`, and `[[no_unique_address]]` ignored, doubling `UniquePtr`). Runs: `ctest --preset windows-msvc` 5 of 5 and
`windows-clang-cl` 5 of 5 (plain ctest; `test-phase.sh` runs on Windows from Day 150). Review: 10 findings, 9 fixed, 1 deferred (the reference regeneration). **Not
done: everything on the Linux machine** (both unity builds, `--affected` with asan and tsan, the new phase-label self-tests, docs regeneration), and `tests/types` under
MSVC (Day 149); filed in `KNOWN-ISSUES.md`. **Next: Day 149 (the `os` backend), after the Linux runs of Days 147-148.**

Phase 13 **Day 149 (2026-11-07, Windows PC): the `os` backend.** `os.h` on Windows: an engine table of handles behind the `int` descriptors (lock-free,
the crash path's), console output in UTF-16 with the code page untouched, `WriteAllWithTimeout` bounded by a helper thread, `QueryPerformanceCounter`,
a high-resolution sleep (steps of about 0.5 ms here), `LoadLibraryExW` that never searches the current directory, the environment without drive entries;
UTF-8/UTF-16 helpers with long-path handling; session state; `os::VulkanLoaderName`. The command line stays as on Linux (now the neutral
`os/command_line.cpp`): the manifest makes `argv` UTF-8. The base-layer tests run on Windows: `ctest --preset windows-msvc` 190 of 190 (plain and ASan),
`windows-clang-cl` 181 of 181 (plain `ctest`). Review: 10 findings, 7 fixed, 1 deferred to Day 150, 2 declined with reasons. The Phase 10 MSVC 128-bit
entry is closed. Linux: pending until the phase close. **Next: Day 150 (test tooling, gates and docs on Windows).**

Phase 13 **Day 150 (2026-11-07, Windows PC): test tooling and gates on Windows.** `scripts/test-phase.sh` runs under Git Bash (`build-msvc`, no tsan, the
summary names it); every phase has a `windows:` `flavors` line (`none` for 2, 3, 8), the unused-line check is Linux-only and the flavor check covers only the
flavors a configuration builds; `tests/phase_labels`, `tests/test_phase_script` and all nine source gates are in the Windows build. The gates know the Windows
rules: `windows.h` only through `os/windows/win32.h` and SDK headers only in the Windows directories (`platform_neutral_check` rule 4), no platform system
header in neutral code (`module_deps_check` `platform-header`), D3D12/DXGI only in `rhi/dx12/`, the Win32/CRT file, environment and command-line calls, and
the Win32 heap/lock/formatting calls in signal-safe code; each has planted Windows violations, and every gate prints a problem on one unwrapped line.
Runs: `ran 247 of 287 tests (phases: all; flavors: plain; display: included; crash tests: excluded)`, asan `ran 40 of 287`, `--all --flavor all`
`ran 287 of 287 tests (phases: all; flavors: all (plain and asan, no tsan on Windows); ...)`; `windows-clang-cl` 278 of 278. Moved: `docgen` and
`ctest -L docs` on Windows to Day 157, `shader_mint` (with its exceptions option) to Day 163. Review: 10 findings, 7 fixed (among them the 30 script tests
that were silently missing from the Windows build), 3 declined with reasons. Linux: pending until the phase close. **Next: Day 151 (the file layer on
Windows).**

Phase 13 **Day 151 (2026-11-07, Windows PC): the file layer I.** `foundation_io` builds on Windows: paths at the Win32 boundary (`io/windows/win_path.cpp`:
UTF-16, reserved device names and trailing dots or spaces refused, `\\?\` added past 248 characters after `GetFullPathNameW`), the Win32 error table,
the synchronous `LocalFile` (offset reads in an `OVERLAPPED`, atomic append, sparse growth, truncation through the handle) and `Stat`/`Lstat` (junctions
`Other`, `read_only` from the attribute and the ACL). One sharing rule for every platform: nothing is locked (`BACKEND.md` "Sharing",
`tests/io/share_test.cpp`). `tests/io` runs on Windows with `path_test`, `file_test`, `share_test` and the new `io_windows_test`; the rest of
`file_system.h` returns `Unsupported` until Day 152. Runs: `ran 283 of 357 tests (phases: all; flavors: plain; display: included; crash tests: excluded)`,
asan `ran 74 of 357`; `windows-clang-cl` 314 of 314; non-unity 357 of 357. Review: 10 findings, 6 fixed, 4 declined. Linux: pending until the phase
close. **Next: Day 152 (directories, the other file-system operations, asynchronous I/O).**

Phase 13 **Day 152 (2026-11-07, Windows PC): the file layer II.** The whole Windows file backend: directories (`io/windows/directory.cpp`; the listing,
walking and wildcards moved to the neutral `io/directory.cpp`) and every file-system operation (`io/windows/file_operations.cpp`): POSIX-semantics
rename and removal (an open, read-only or empty-directory target is replaced, as on Linux; measured against `MoveFileExW`), `SyncDirectory` through a
directory handle, private directories with a protected DACL, random temporary names, the known folders. Asynchronous I/O keeps one design on both
platforms (the executor). `tests/io` runs on Windows except `async_file_test` (needs `jobs`, Day 153). Runs: `ran 306 of 403 tests (phases: all;
flavors: plain; display: included; crash tests: excluded)`, asan `ran 97 of 403`; `windows-clang-cl` 337 of 337; non-unity 403 of 403. Review: 8 findings,
2 fixed, 6 declined. Linux: pending until the phase close. **Next: Day 153 (the job system on Windows).**

Phase 13 **Day 153 (2026-11-07, Windows PC): the job system.** The CPU list, thread ids, pinning and names moved behind `jobs/detail/thread_platform.h`
(Linux code unchanged; Windows: processor groups, `group * 64 + bit` ids, `SetThreadGroupAffinity`, `SetThreadDescription`); `jobs` and `tests/jobs` run on
Windows in plain and ASan, and `async_file_test` joined the io tests. Five older jobs tests were converted to engine types (the allow-list count went
from 102 to 33). Wake latency (new `jobs_tests "[latency]"`): median 72 us, p99 0.6-1.0 ms; throughput 11.4x at 16 participants. ASan jobs 20 runs: 0
failures; one flake recorded. Runs: `ran 400 of 591 tests (phases: all; flavors: plain; display: included; crash tests: excluded)`, asan `ran 191 of 591`;
`windows-clang-cl` 431 of 431; non-unity 591 of 591. Review: 5 findings, 1 fixed, 4 declined. Linux: pending until the phase close. **Next: Day 154
(debugging I: stack traces on Windows).**

Phase 13 **Day 154 (2026-11-07, Windows PC): debugging I.** `foundation_debug` builds on Windows with sentry-native (two Breakpad headers re-vendored
from the pinned release, checksum verified). Stack traces resolve through DbgHelp from the PDBs (`os/windows/debug/stack_trace_dbghelp.cpp`: inlined
frames, `address - 1`, a search path that never names a symbol server; never called while a crash is reported); `FormatStackTrace` is now neutral.
Module list, crash context (CodeView build id), debugger check, thread ids and names are built; the crash hook, death tests and fault injection
(Day 155) and post-mortem dumps (Day 156) are placeholders that report themselves unsupported. `tests/debug` runs on Windows with today's groups.
Runs: `ran 472 of 727 tests (phases: all; flavors: plain; display: included; crash tests: included)`, asan `ran 255 of 727`; `windows-clang-cl` 503 of
503; non-unity 727 of 727. Review: 6 findings, 2 fixed, 4 declined. Linux: pending until the phase close.

Phase 13 **Day 155 (2026-11-07, Windows PC): debugging II.** A crashing Windows process leaves a minidump and the engine report, with nothing
uploaded (`debug_no_network_*` read imports and strings). The report is written from the library's `on_crash` hook on Breakpad's handler thread
(`os/windows/debug/crash_hook.cpp`): an observing vectored handler records each exception's thread and name, the stack is walked with
`RtlVirtualUnwind`, frames are module and offset only (no DbgHelp during a crash), and a gate filter stops a second crashing thread from ending
the process before the dump (seen one run in three before it). Death tests (job objects, the `ENGINE-ABORT` marker) and fault injection
(`BusError` skips) work; fast fail is a documented gap; the allocation check is the debug C runtime's hook. Neutral fixes: a module offset
printed one digit; the report can say `thread: unknown`. Runs: `ran 515 of 807 tests (phases: all; flavors: plain; display: included; crash
tests: included)`, asan `ran 292 of 807`; MSVC 807 of 807, `windows-clang-cl` 545 of 545, non-unity 806 of 807 (a phase 2 flake, filed). Review:
9 findings, 5 fixed, 4 declined. Linux: pending until the phase close.

Phase 13 **Day 156 (2026-11-07, Windows PC): debugging III.** A Windows crash reads back with names: `scripts/store-symbols.ps1` files the PDB and
image under the build id (the WinDbg layout), `crash-report.ps1` prints the exception, the modules and the report with each `module+offset` frame
named from the store (`crash_extract --resolve`, DbgHelp, no symbol server), `find-dump.ps1` and `open-dump.ps1` find and open dumps.
`crash_extract` builds on Windows with `--modules`, `--report`, `--image` and `--resolve` (bounds-checked minidump and PE readers).
`EnablePostMortemDumps` only reads WER's machine-wide LocalDumps key (measured: the per-user key is ignored on this build) and writes nothing.
A death-test child could hang in `ExitProcess` under ASan; it now ends with `TerminateProcess`, as `_exit` on Linux. Runs: `ran 543 of 862 tests
(phases: all; flavors: plain; display: included; crash tests: included)`, asan `ran 319 of 862` (one known phase 2 flake); MSVC 862 of 862,
`windows-clang-cl` 573 of 573, non-unity 862 of 862. Review: 7 findings, 4 fixed, 1 not a defect, 2 declined; the full runs found two more, fixed.
Not done: a person opening a dump in WinDbg (no debugger installed), the reference for `post_mortem.h` (docgen on Windows is Day 157). Linux:
pending until the phase close. **Next: Day 157 (settings on Windows: the registry store).**

**Linux check after Days 147-156 (2026-11-07), and warnings as errors.** Linux broke in three places, all fixed: a unity name clash (`kEntriesPerFrame` in two
debug files), `tests/debug` missing the `tests/support` include path, and stale `docs/reference/` pages. New policy (CLAUDE.md, "Warnings are errors"):
`-Wall -Wextra -Werror` on Clang/GCC and `/W4 /WX` on cl and clang-cl for every target of ours; 34 Clang warnings fixed. Verified with Clang on Linux in
unity and non-unity builds; the Windows settings are untested (`KNOWN-ISSUES.md`). Run: `ran 1537 of 3510 tests (phases: all; flavors: plain; display: included;
crash tests: included)`; only the two known Wayland `renderer_demo` tests fail.

**Wayland `renderer_demo` crash fixed, and Clang first (2026-11-08).** The crash was ours to avoid: `Renderer::RefreshCapabilities` called `rhi::EnumerateAdapters`, which makes and destroys a
second Vulkan instance while the device is live, and destroying it clears driver state the live swapchain needs (NVIDIA 615.78.08 on Wayland: `vkAcquireNextImageKHR` calls a null function).
`Device::ListAdapters` returns the list the device already holds; the renderer and the demo's F3 use it. On Linux CMake now picks `clang++` when it is installed and GCC otherwise, and GCC
builds clean (seven `-Wformat-truncation` sites, one `-Wsubobject-linkage`, one missing include). Run: `ran 1540 of 3515 tests (phases: all; flavors: plain; display: included; crash tests: included) in 56 s`,
no failures; Clang and GCC build clean in unity and non-unity. asan `ran 1129 of 3515 tests (phases: all; flavors: asan; display: included; crash tests: included) in 41 s`, tsan `ran 846 of 3515 tests (phases: all; flavors: tsan; display: included; crash tests: included) in 10 s`, no failures.

**Windows under warnings as errors (2026-11-07).** The three Windows builds compile clean with `/W4 /WX` (cl, unity and non-unity) and clang-cl's
`-Wall -Wextra` equivalent: five warnings fixed (a `[[gnu::noinline]]` cl does not know, code after `SKIP` and after a fault that does not return, two
helpers unused on Windows). Full suites: MSVC 862 of 862, non-unity 862 of 862, `windows-clang-cl` 573 of 573. **Next: Day 157 (settings on Windows: the
registry store).**

Phase 13 **Day 157 (2026-11-07, Windows PC): settings on Windows.** `settings::RegistryConfigStore` (`settings/windows/registry_store.h`) keeps settings
under a registry key (`HKCU\Software\<app>` by default): sections are subkeys, every value is a `REG_SZ` of its canonical text (decided today: a typed
mapping cannot round-trip enums), per-key writes, and a watch thread that tells a listener about other writers' changes. Group policy is a new top layer,
`LayerRole::Policy` (`HKCU` then `HKLM` `Software\Policies\<name>`, read-only), and `Settings::Set` refuses an enforced setting (`SetError::Enforced`).
Windows environment names ignore case (`os::EnvironmentNamesIgnoreCase`). `config_access_check` reports registry reads outside `settings/`. The default
user store stays the file on every platform. `docgen` builds on Windows: the reference generated there matches Linux's byte for byte, `ctest -L docs` runs
`docs_check`, `docs_links` and `docs_lint` (the samples come on Day 161). The full runs also found the Windows test guard counting unrelated processes
whose parent id was reused (fixed, with a test). Runs: `ran 545 of 1125 tests (phases: all; flavors: plain; display: included; crash tests: excluded)`,
asan `ran 318 of 1125`; MSVC 1125 of 1125, `windows-clang-cl` 708 of 708, non-unity 1125 of 1125; docs 3 of 3. Review: 8 findings, 5 fixed, 3 declined.
Not done: the demos' `--settings` round trip on Windows (Day 161, with the demos). Linux: pending until the phase close. **Next: Day 158 (the Win32
window).**

Phase 13 **Day 158 (2026-11-07, Windows PC): the Win32 window.** `os::win32::Win32Window` (`platform/windows/win32_window.h`) opens a top-level
window with the exact client size at its DPI (per-monitor-v2), reports size, minimize, close and DPI changes, hands its `HWND` and `HINSTANCE` to
graphics code, toggles borderless fullscreen, and calls `os::Window::SetModalLoopCallback`'s function during Windows' drag and menu loops so a
program can keep drawing. `PlatformWindowHost` on Windows opens it (`WindowBackend::Win32`; Wayland and X11 are refused with the reason).
Windowed programs write `ENGINE_WINDOWED_PROGRAM(EngineMain)` (`platform/entry_point.h`): `main` on Linux, `wWinMain` on Windows with a linker
directive for the GUI subsystem (clearing CMake's console flag instead breaks every test link); UTF-8 arguments, output to inherited handles or
the parent's console. `hello_window` uses it; `platform_neutral_check` rule 5 keeps windowed programs under `game/` off `main` (the demos are
listed until Day 161). Runs: `ran 573 of 1168 tests (phases: all; flavors: plain; display: included; crash tests: excluded)`, asan `ran 333 of
1168`; MSVC, `windows-clang-cl` and non-unity full runs pass except a stale `docs_check`, then regenerated: docs 3 of 3 in all three, lint
4099/4099. Review: 7 findings, 3 fixed, 4 declined. Not done: the human check from Explorer (filed). Linux: pending until the phase close.
**Next: Day 159 (input on Windows).**

Phase 13 **Day 159 (2026-11-08, Windows PC): input on Windows.** `Win32Window` fills `InputState` with the meaning the Linux backends give it: keys are
layout-aware (a key that types a character is named by the layout's character, so on AZERTY the Q-position key is `A`; the scan code names the rest
and tells left from right; `os::win32::KeyFromWin32`, a table test covers every `Key`), a release clears the Key its key was pressed as, AltGr is the
right Alt alone, Alt alone opens no menu while Alt+F4 still closes, the mouse is captured while a button is held, the wheel uses Linux's 10 units per
notch, and a focus loss releases everything held. The keysym translation moved to `platform/linux/`. `rhi_boundary_check` no longer reads Win32
virtual keys (`VK_ESCAPE`) as Vulkan in `platform/windows/`. Not done: text input (no backend has it; the UI phase), relative mouse motion (no API),
the human check. Runs: `ran 588 of 1198 tests (phases: all; flavors: plain; display: included; crash tests: excluded)`, asan `ran 348 of 1198`; MSVC 1198
of 1198, `windows-clang-cl` 751 of 751, non-unity 1198 of 1198. Review: 5 findings, 2 fixed, 3 declined. Linux: pending until the phase close.
**Next: Day 160 (Vulkan on Windows).**

Phase 13 **Day 160 (2026-11-08, Windows PC): Vulkan on Windows.** The Vulkan backend runs on Windows: the loader is `vulkan-1.dll` (named in one place,
`os::VulkanLoaderName`), windows present through `VK_KHR_win32_surface` (its own file, `rhi/vulkan/windows/vk_win32_surface.cpp`), validation comes
from the SDK, and the pipeline cache defaults to `%LOCALAPPDATA%\<app>\cache`. The Phase 4 RHI suite builds and runs on the AMD Radeon 860M: 289
tests, 281 pass and 8 skip (the Wayland and X11 window cases; four Win32 window cases were added and pass), with validation silent; every golden image
matches with the tolerances unchanged. Found on the way: AMD's driver reads the timeline one value short for about 100 us after `vkDeviceWaitIdle`, so
`WaitIdle` now also waits on the timeline (a regression test without validation fails 9 rounds in 10 without it). The renderer libraries build for
the RHI tests; their own suite is Day 161. Runs: `ran 880 of 1775 tests (phases: all; flavors: plain; display: included; crash tests: excluded)`, asan
`ran 633 of 1775`; MSVC 1775 of 1775, `windows-clang-cl` 1043 of 1043, non-unity 1774 of 1775 (the known FramePool ASan flake). Review: 5 findings, 2 fixed, 3 declined. Linux:
pending until the phase close. **Next: Day 161 (the renderer and the demos on Windows through Vulkan).**

Phase 13 **Day 161 (2026-11-08, Windows PC): the renderer, the demos and the baselines on Windows through Vulkan.** The renderer suite runs on Windows
(236 tests, plain and ASan, validation silent; `renderer_demo.ppm` matches). `rhi_demo` and `renderer_demo` are windowed programs
(`ENGINE_WINDOWED_PROGRAM`), their POSIX calls go through `os::`, and they draw from the modal-loop callback while the frame is dragged; all 46 demo
ctests pass on Windows, among them a scripted live-settings run on Win32 and the settings round trip; 1000 frames with validation exit 0 with memory
flat. New: `os::ResidentMemoryKiB` and `os::OpenHandleCount` (five `/proc` readers removed); the reset and loss memory checks compare low points, as
the Windows process holds whole 64 MiB blocks after some resets (filed). Found by the runs and fixed: a Debug `abort()` showed a dialog before the
crash handler ran (now off while it is installed), and test runs wrote into the user's profile (the Windows file layer reads `%APPDATA%` and
`%LOCALAPPDATA%` first, and the demo ctests use a profile in the build tree). `docgen samples` runs on Windows (a job object per child; 49 of 49 samples).
Baselines in section 8 of the Windows design spec (Release; `rhi_bench` 0.73-0.87 us per draw, the demo scene 1.8-1.9 ms per frame); a device reset
takes 239 ms here against 40 ms under Linux on the same GPU. Runs: `ran 1174 of 2316 tests (phases: all; flavors: plain; display: included; crash
tests: excluded)`, asan `ran 877 of 2316`; MSVC 2316 of 2316, `windows-clang-cl` 1339 of 1339, non-unity 2316 of 2316. Review: 5 findings, 3 fixed,
2 declined. Not done by a person: a drag with the mouse, the F-keys. Linux: pending until the phase close. **Next: Day 162 (the backend registry and the shader bytecode, the start of DX12).**

Phase 13 **Day 162 (2026-11-08, Windows PC): the RHI backend registry and bytecode blobs.** `CreateDevice`, `EnumerateAdapters` and `DeviceHost` live in the
neutral `rhi` and reach backends through a registry (`rhi/backend_registry.h`); programs call `rhi::RegisterCompiledBackends()` (library `rhi_backends`), test
binaries from a Catch2 listener, and the renderer links `rhi` only (the module allow list is empty). `Backend` gains `Dx12` and `Auto` (the default, the
available backend of highest rank); the `backend` setting has `dx12` and `auto`, and a backend a program does not have is refused with the reason.
`ShaderDesc` carries a format-tagged blob (`ShaderFormat` SPIR-V, DXIL, MSL reserved); `ShaderBlobSet`, `CreateShaderFromSet` and
`PermutationModule::Blob` pick the device's format (`DeviceCaps::shader_format`). `MatchAdapter` accepts a LUID (`AdapterInfo::luid`). Runs: `ran 1185 of
2338 tests (phases: all; flavors: plain; display: included; crash tests: excluded)`, asan `ran 888 of 2338`; all three Windows trees build clean. Review: 6
findings, 4 fixed, 2 declined. Linux: pending until the phase close. **Next: Day 163 (DXIL minting, the object index stream, the clip-space flip).**

Phase 13 **Day 163 (2026-11-08, Windows PC): the shader path for D3D12.** Every shader and permutation key is minted to signed DXIL
(`shaders/*.dxil.inc`, shader model 6.0, `slangc` with the Windows SDK's `dxcompiler` 1.9.2602.17 found by the build), with the blob set
`k<Base>Shader` and the hash of the SPIR-V it was minted beside (`shader_dxil_test`). One source serves both targets (`target_common.slang`): the
D3D12 build flips clip-space Y in the shader (amended from a D3D projection), uses `SV_VertexID`, and reads the object index from an instance-rate
stream (`MaterialSystem::ObjectIndexStream`, `DeviceCaps::object_index_stream`); vertex inputs are `TEXCOORD<location>` and registers follow the
bindings, both checked by `shader_mint`. New `BindingType::ReadOnlyStorageBuffer`. `shader_mint --permute` runs on Windows (`run_process.h`). The
SPIR-V of all 24 shaders is unchanged. Runs: `ran 1212 of 2369 tests (phases: all; flavors: plain; display: included; crash tests: excluded)`, asan
`ran 892 of 2369` (it found a descriptor-pool overflow, fixed). Review: 5 findings, 3 fixed, 2 declined. Linux: pending until the phase close.
**Next: Day 164 (the DX12 device).**

Phase 13 **Day 164 (2026-11-08, Windows PC): the DX12 device.** New library `rhi_dx12` (`engine/rhi/dx12/`), registered as `dx12` (`Auto` never
picks it until Day 172): adapters from DXGI in high-performance order plus WARP, with stable ids (`dx12-<vendor>-<device>-<subsystem>-<revision>`)
and LUIDs; devices at feature level 12_0 with the debug layer routed to `LogRhi` and counted, GPU-based validation for `sync_validation`, and
live objects checked at destruction; capabilities from the D3D12 constants; one direct queue and fence with bounded waits; frames and fault
injection as on Vulkan. Resources, descriptors, pipelines, command lists and the swapchain are placeholders until Days 165-169. Devices were made on
the Radeon 860M and WARP with the debug layer silent. Runs: `ran 1222 of 2389 tests (phases: all; flavors: plain; display: included; crash tests:
excluded)`, asan `ran 902 of 2389`. Review: 4 findings, 2 fixed, 2 declined. Linux: pending until the phase close. **Next: Day 165 (DX12 resources).**

Phase 13 **Day 165 (2026-11-08, Windows PC): DX12 resources.** Buffers as committed resources on the default, upload and readback heaps
(mapped for life, uniform buffers rounded to 256), textures with their views from CPU descriptor heaps (sampled depth typeless with typed depth
and shader views), a 64 MiB staging ring (the ring allocator moved into the neutral `rhi/staging_ring.h`), and uploads and readbacks repacked to
256-byte row pitches through `GetCopyableFootprints`, tested in 13 formats at odd sizes, mips, arrays, volumes, depth and BC1/BC7. The device
records each texture's actual D3D12 state. Counts follow the Vulkan accounting. Committed buffers cost 311 ms per 1000 against Vulkan's 22 ms
(filed). Runs: `ran 1228 of 2401 tests (phases: all; flavors: plain; display: included; crash tests: excluded)`, asan `ran 908 of 2401`. Review: 4
findings, 2 fixed, 2 declined. Linux: pending until the phase close. **Next: Day 166 (DX12 descriptors and bind groups).**

Linux check after Day 165 (2026-11-08): two breaks fixed. `tools/docgen/src/samples.cpp` defined `Hex` for all platforms but used it only under `_WIN32`
(`-Wunused-function` under `-Werror` on Clang); it is now under the same `#if`. `test_phase_labels` failed because Days 162-163 added cross-platform
phase 13 tests (backend registry, minted DXIL) that run on Linux while the map listed phase 13 for Windows only; `tests/phases.txt` now has
`flavors 13 linux: plain,asan,tsan` and the planted-platform test expects the Linux line. Unity and `-DENGINE_UNITY_BUILD=OFF` build with no
warnings of ours; `scripts/test-phase.sh --affected`: ran 1393 of 3577 tests (phases: all; flavors: plain; display: included; crash tests: excluded), no failures.

Phase 13 **Day 166 (2026-11-08, Windows PC): DX12 descriptors and bind groups.** Samplers (every RHI state, de-duplicated), bind group layouts,
bind groups whose views sit together in a CPU heap, a shader-visible view ring of 3 frame slots emptied as each slot comes round, shared sampler
tables in the 2048-entry sampler heap, and root signatures 1.1 per pipeline layout (a view and a sampler table per group, root constants at
`b0, space15`) cached by layout content. `BindingDesc` gained `stride` for storage buffers (D3D12's structured views), and reflection records it
(version 2, every `*.refl.inc` re-minted). The `SetBindGroup` measurement and the end-to-end binding tests moved to Day 168, the reflection-against-
root-signature check to Day 167 (plan amended). Runs: `ran 1237 of 2418 tests (phases: all; flavors: plain; display: included; crash tests: excluded)`, asan `ran 916 of 2418`. Review: 8 findings, 6 fixed, 2
declined. Linux: pending until the phase close. **Next: Day 167 (DX12 pipelines and the pipeline cache).**

Platform and compiler wrappers (2026-11-08, maintenance between Days 165 and 166, no plan day): `engine/foundation/build_defs.h` (base layer) defines `ENGINE_PLATFORM_{WINDOWS,LINUX,MACOS,IOS,ANDROID}`,
`ENGINE_COMPILER_{MSVC,CLANG_CL,CLANG,GCC}` (+ `_CLANG_FAMILY`, `_MSVC_ABI`), `ENGINE_ARCH_{X64,X86,ARM64}`, `ENGINE_SANITIZER_{ADDRESS,THREAD}`, `ENGINE_SANITIZED` and
`ENGINE_HAS_BUILTIN`, always 0 or 1. All 135 uses of `_WIN32`, `__linux__`, `__clang__`, `_MSC_VER`, `__x86_64__`, `__SANITIZE_*`, `__has_feature` etc. in engine/, game/, tools/ and tests/
moved to them (the sanitizer detection blocks in 11 tests collapsed to one line each). Rule 6 of `platform_neutral_check` fails on a raw macro (exempt: the header, `tests/windows/discovery/`,
generated code); CLAUDE.md, ARCHITECTURE.md and `docs/guide/foundation-types.md` say so; `tests/foundation/build_defs_test.cpp` (phase 0) holds the one-of-each-family rule. Also fixed a non-unity
`-Wunused-const-variable` in `vulkan_device_bindings.cpp`. Not reviewed with `/code-review` (a mechanical rename, no plan day). Windows (cl, clang-cl) not built: the Windows machine must build once.
`scripts/test-phase.sh --affected`: ran 1398 of 3582 tests (phases: all; flavors: plain; display: included; crash tests: excluded), asan ran 989 of 3582, tsan ran 701 of 3582 (before the macOS-detection edit; plain and asan after), no failures.

Phase 13 **Day 167 (2026-11-08, Windows PC): DX12 pipelines and the pipeline cache.** Shaders keep a shared copy of their DXIL (SPIR-V refused
by name); graphics pipelines from the RHI state (`TEXCOORD<location>` inputs, the front face unchanged under the Y flip, alpha-blend factors
mapped, two states for a restart strip) and compute pipelines, all through the root signatures of Day 166. The cache is an
`ID3D12PipelineLibrary` per adapter and driver, with a checked header and the list of stored names, and the directory rule every backend now
shares (`rhi/pipeline_cache_file.h`, moved out of the Vulkan backend). Every committed shader forms a pipeline with the layouts its reflection
gives; damaged, truncated and foreign cache files are discarded. Measured: 33 pipelines 12.2 ms cold, 10.7 ms warm (the driver's cache is warm
in both). Runs: `ran 1246 of 2436 tests (phases: all; flavors: plain; display: included; crash tests: excluded)`, asan `ran 925 of 2436`. Review: 6 findings, 3 fixed, 3 declined. Linux: pending until the
phase close. **Next: Day 168 (DX12 command lists, barriers, first images).**

Windows DPI fix (2026-11-08, between Days 167 and 168, asked for by the user, no plan day): the user saw tiny windows on the high-DPI Windows
PC. Engine programs are per-monitor-v2 aware, and `Win32Window::Open` took the requested size as physical pixels; it is logical now, multiplied by
the window's DPI over 96 (`Win32Window::ScaleForDpi`), while `Width`/`Height` stay physical (the swapchain's size). The Win32 window tests
compare against the scaled size, a new test checks the scaling; guide `platform-windowing-and-input.md`, `window_host.h` and
`win32_window.h` say so. `/code-review high`: 3 findings, all declined (a scaled size larger than the monitor is not clamped, as before for a
large size; the demos render more pixels at high DPI, no benchmark opens a window; X11 on a high-density display is still physical, filed).
Runs: `ran 1252 of 2443 tests (phases: all; flavors: plain; display: included; crash tests: excluded)` with one flaky failure in phase 2
(filed), asan `ran 926 of 2443`; all three Windows trees build clean. Linux: `window_host.h` documentation only.

Phase 13 **Day 168 (2026-11-08, Windows PC): DX12 command lists.** A command list per allocator, pooled per frame slot (recording on any
number of threads); `Submit` runs each list on its own, buffers tracked within a list from `COMMON`, textures moved into the state a list needs
by a prologue built from the device's recorded states. Passes with clears and discards, draws (the cut-at-0xFFFF state for 16-bit restart
strips), dispatches, copies through footprints, debug labels, and `GenerateMips` by a compute shader (`shaders/mipgen.comp`, sRGB through a
typeless resource). The triangle matches `triangle.ppm`; fill, saxpy and gradient match; mips match a CPU reference. GPU-based validation silent
over the DX12 tests. `SetBindGroup` with a draw: 0.40 us on DX12 against 0.74 to 1.00 us on Vulkan. Runs: `ran 1260 of 2459 tests (phases: all; flavors: plain; display: included; crash tests: excluded)`, asan
`ran 934 of 2459 tests (phases: all; flavors: asan; display: included; crash tests: excluded)` (an earlier run hit the filed FramePool flake once). Review: 4 findings, 2 fixed, 2 declined. Linux: pending until the phase close.
**Next: Day 169 (the DX12 swapchain).**
