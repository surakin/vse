# STATUS.md

Current progress snapshot for this project. This file changes often —
update it as part of the same commit whenever the plan advances (a day
file's tasks get checked off, a phase starts or completes, a build/test
command changes). See `CLAUDE.md` for the durable rules that don't change
with progress.

Failing tests, flaky tests and deferred items are tracked in `KNOWN-ISSUES.md`.

## Build and test commands

- Configure and build: `cmake -S . -B build && cmake --build build`. Our own targets build as unity (merged) translation
  units by default; `-DENGINE_UNITY_BUILD=OFF` compiles every source on its own (both must keep building; CLAUDE.md, "Unity builds").
- **Current phase** (the phase in `CURRENT_PHASE`, plain flavor): `scripts/test-phase.sh`. **Plus the modules a change
  touched:** `scripts/test-phase.sh --affected` (add `--flavor asan` or `--flavor tsan` for a sanitizer flavor). One phase:
  `scripts/test-phase.sh 4`. Add `--dry-run` to see what would run.
- **Everything, every flavor** (a phase's closing run): `scripts/test-phase.sh --all --flavor all`, or plain `ctest` from `build/`.
- Every test has one `phaseN` label (`ctest -L '^phase4$'`); `tests/phases.txt` is the only place a phase is decided, and
  `CURRENT_PHASE` changes in the commit that closes the previous phase or starts this one. Docs checks: `ctest -L docs` (not scoped).
- A reduced run is quoted with its `ran X of Y tests` line, never as "all tests pass". Guide: `docs/guide/building-and-testing.md`.
- **Debugging facilities (Phase 6):** the suite is `tests/debug` (`ctest -L phase6`; the crash tests run child processes, a few need
  `lldb`, `gdb` or `objcopy` and skip with a printed reason without them). Gates: `signal_safety_check`, `platform_neutral_check`,
  `debug_backend_lists`, `debug_cross_syntax_windows|macos` (need `clang++`), `debug_no_network_*`. Scripts (Linux shell):
  `scripts/crash-report.sh`, `store-symbols.sh`, `split-debug.sh`, `find-core.sh`, `open-core.sh`. Every demo takes
  `--crash-handler=on|off` and `--crash-test=segv|abort|assert|ensure`. Release symbols: `-DENGINE_SPLIT_DEBUG_INFO=ON`.
  Guide: `docs/guide/foundation-debugging.md`, `porting-debugging.md`.

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
choice deferring the real vendor-vs-system decision to Phase 3. 5 new
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
buffer (solid placeholder fill; real pixels are Phase 3's job) to
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
(add `-L tsan` for just the TSan set). Day 28 (Job coroutine type) is DONE (high-effort review recorded in its day file); Day 29 (worker pool) is DONE (review recorded in its day file); Day 30 (work-stealing deque) is DONE (review recorded in its day file); Day 31 (scheduler) is DONE (review recorded in its day file); Day 32 (counter/await) is DONE (review recorded in its day file); Day 33 (fan-out/join) is DONE (review recorded in its day file); Day 34 (async I/O) is DONE (review recorded in its day file); Day 35 (stress & throughput) is DONE (review recorded in its day file). **Phase 2 is complete:** `Scheduler`, `Counter`, `parallel_for`, async file I/O, the Chase-Lev deque and frame pool pass the randomized-graph stress, skewed-load stealing (other participants verifiably execute stolen work), a scaling table (1.96x at N=2, 3.87x at N=4 here) and 10,000-run nested `co_await` ordering tests, clean under ASan and TSan. Phase 3 (RHI / Vulkan) has no day files yet, so it is not startable until they are written and committed (CLAUDE.md).

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

**Roadmap change (2026-11-03):** a dedicated **Phase 6 — UI System
(Widgets & Viewport)** was inserted after Font & Text (retained widget
toolkit with menus/buttons/comboboxes/scrollbars/labels/text fields and a
viewport widget hosting the rendered 3D world). The old Phases 6-19 are
now 7-20 (plan files renamed, cross-references updated; the dated design
docs under `docs/superpowers/specs/` keep their original numbering). It
is a goal/deliverable-level plan only — not startable until its days are
broken out. Phase 2 (Job System) remains the active phase.


## Phase 2.5 (Documentation tooling) — complete (Days 36-44); Phase 3 plan written (Days 45-67), not started

Days 36-44 are broken out under `plans/phase-02-5-docs-tooling/` (design
settled: own C++ `docgen` on the foundation, purpose-built scanner, `///`
comments, committed generated reference, link/sample/lint checks). The
end-user docs skeleton lives in `docs/` (all pages are outlines). Phase 3
is broken out into Days 45-67 under `plans/phase-03-vulkan-rhi/` (planning
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

Phase 3 (Vulkan RHI) is planned: 23 day files (Days 45-67) under `plans/phase-03-vulkan-rhi/`, indexed by `plans/phase-03-vulkan-rhi.md` with the settled design decisions. Phase 3 is under way; Phase 4+ still need their day files before they start.

Phase 3 Day 45 (RHI design spec & build skeleton) is done: design decisions D1-D8 in `docs/superpowers/specs/2026-11-04-rhi-design.md`, targets `rhi`/`rhi_vulkan` (+ `_asan`) and `rhi_tests`/`rhi_tests_asan` (label `rhi`), `LogRhi`, module `rhi` in the docs map, guide skeleton `docs/guide/rhi.md`; high-effort review (6 findings, 4 fixed, 2 declined) recorded in the day file. Run: `build/tests/rhi/rhi_tests`.
Phase 3 Day 46 (core types) is done: handles, `Format` table, flag enums, descriptors with `Validate()`, `DeviceCaps` in `engine/rhi/` (all documented, 328/328, baseline zero); 20 RHI test cases pass plain and under ASan, with a per-header self-containment check; high-effort review (8 findings, 6 fixed, 2 declined) recorded in the day file.
Phase 3 Day 47 (device interface, handle pool, null backend) is done: `Device`/`CommandList`/`Swapchain` interfaces, `HandlePool` with deferred destruction, and `NullDevice` (validating, counting, no GPU) in `engine/rhi/`; 71 RHI test cases pass plain and under ASan; docs 516/516 with a runnable guide sample; also fixed `docgen samples` resolving `clang++` to `clang-23`; high-effort review (9 findings, all fixed) recorded in the day file.

Phase 3 Day 48 (native window seam) is done: `os::NativeWindowInfo`, `Window::GetNativeInfo()/IsMinimized()/ResizeSerial()` on Wayland and X11 with a process-wide serial counter; docs 1207/1207; high-effort review (7 findings, 5 fixed, 2 declined) recorded in the day file. Known pre-existing failure: `X11Window::Create opens a mapped window on the real X server`.

Phase 3 Day 49 (Vulkan loader and function tables) is done: `engine/rhi/vulkan/` opens `libvulkan.so.1` at runtime, resolves generated global/instance/device tables from `vk_functions.inc`, checks Vulkan 1.3, and has `VkResultName`/`ClassifyVkResult`/`ENGINE_VK_CHECK`; new docs module `rhi-vulkan`; `[gpu]` tests pass on the dev GPU plain and under ASan; high-effort review (9 findings) recorded in the day file.

Phase 3 Day 50 (instance and validation) is done: `VulkanInstance` with extensions by purpose, validation layer policy (`ENGINE_RHI_VALIDATION`, `_SYNC`, `_BEST_PRACTICES`, `_FATAL`), a debug messenger that logs and counts errors/warnings, `SetValidationCapture`, and `gpu_test::GpuFixture` that fails any `[gpu]` test on an unexpected validation error; docs 1270/1270; high-effort review (9 findings) recorded in the day file.

Phase 3 Day 51 (physical device, queues, logical device) is done: `rhi::CreateDevice` (`engine/rhi/rhi.h`) creates a headless `VulkanDevice` — pure selection policy with `ENGINE_RHI_GPU` override, queue plan, exactly-the-needed features, `DeviceCaps` from real limits, `Format`↔`VkFormat` table; resource, command-list and fence methods are stubs until Days 53–57; docs 1356/1356; high-effort review (8 findings) in the day file.

Phase 3 Day 52 (GPU memory allocator) is done: `MemoryAllocator` (best-fit free-list blocks with coalescing, linear/optimal segregation, dedicated objects, persistent mapping, spare-block policy, non-coherent atom rounding, stats, never-abort failure policy) over a `MemoryBackend`; 14 pure tests against a fake backend including a 100k-cycle invariant stress, plus real-driver tests; docs 1455/1455; high-effort review (9 findings) in the day file.

Phase 3 Day 53 (buffers, staging, upload) is done: Vulkan `CreateBuffer`/`Map`/deferred `DestroyBuffer`, staging `RingAllocator`, chunked `UploadBuffer` returning `UploadTicket{fence, ok}`, `ReadbackBuffer`, timeline fence and frame tracking; round trips up to 40 MiB, plain and ASan; docs 1478/1478; high-effort review (10 findings, 9 fixed, 1 declined) in the day file.

Phase 3 Day 54 (command lists, submission, sync) is done: `GetBarrierScopes` state table, `VulkanCommandList`, per-thread command pools in a 3-frame ring, `Submit` on the timeline semaphore, sticky device loss with a fault-injection hook, multithreaded recording test; new `rhi_tests_tsan` target (non-GPU tests only, because the NVIDIA driver crashes TSan); docs 1528/1528; high-effort review (10 findings, 9 fixed, 1 declined) in the day file.

Phase 3 Day 55 (textures, samplers, layout transitions) is done: Vulkan textures with deferred destruction, shared reference-counted samplers, `FormatSupport`/`CanGenerateMips`, `UploadTexture`/`ReadbackTexture`, texture copies and barriers in command lists, `GenerateMips`; null backend mirrors the API; docs 1555/1555; high-effort review (10 findings, 8 fixed, 2 declined) in the day file.

Phase 3 Day 56 (swapchain and window presentation) is done: `DeviceCreateInfo::window`, Wayland and (isolated-TU) Xlib surfaces, tested swapchain policy functions, `VulkanSwapchain` with bounded `Acquire`, deferred recreation and minimize handling, null swapchain; presents to Wayland and X11 windows with validation clean; docs 1590/1590; high-effort review (10 findings, 8 fixed, 2 declined) in the day file.

Phase 3 Day 57 (frame loop, frames in flight, clear-color window) is done: `DeviceCreateInfo::frames_in_flight` (1-3), Vulkan `BeginRendering/EndRendering`, `VulkanDevice::GetLeakCounts`, `game/rhi_demo` (animated clear on Wayland and X11, validation clean), pixel-readback and 120-frame loop tests, guide "The frame loop" and tutorial `hello-rhi.md`; docs 1597/1597; high-effort review (8 findings, 5 fixed, 3 declined) in the day file. Only the Wayland/X11 visual check by eye is outstanding (see the day file).

Plan change before Day 58 (2026-11-04): shaders are now GLSL compiled offline by `glslc` instead of hand-written SPIR-V assembly (`PLAN.md` assumption #18). Day 58 and Days 61-65 were rewritten accordingly; the long-term shader toolchain (DX12/Metal path) is an open decision to settle before Phase 4's material system and no later than Phase 12, tracked in `PLAN.md`, `CLAUDE.md` and the Phase 4/12/13 plan files. Day 57's visual check on Wayland and X11 was confirmed by hand.

Phase 3 Day 58 (shader workflow and shader modules) is done: `rhi::ValidateSpirv` sanity check, Vulkan `CreateShader`/`DestroyShader`/`LookupShader`, `tools/spv_embed`, GLSL `shaders/` with committed `*.spv.inc`, the `shaders` target (needs `glslc`) and `shaders_check` ctest, triangle vertex/fragment shaders, guide `rhi-shaders.md`; docs 1607/1607; high-effort review (8 findings, 8 fixed, 0 declined except a documented constant) in the day file. Regenerate shaders with `cmake --build build --target shaders`.

Phase 3 Day 59 (graphics pipelines and the pipeline cache) is done: `CreateGraphicsPipeline` with dynamic rendering, shared bind group layouts and reference-counted pipeline layouts, the on-disk pipeline cache (`ENGINE_RHI_CACHE_DIR`, `SavePipelineCache`), and `SetPipeline`/`SetViewport`/`SetScissor`/`SetVertexBuffer`/`SetIndexBuffer`/`Draw`/`DrawIndexed`; a triangle draws into an offscreen target and reads back; docs 1652/1652; high-effort review (9 findings, 7 fixed, 2 declined) in the day file.

Phase 3 Day 60 (bind groups, descriptors and push constants) is done: Vulkan `CreateBindGroup`/`DestroyBindGroup` with persistent and per-frame (transient) descriptor pools that grow instead of failing, `SetBindGroup` (bound lazily at draw), `SetPushConstants`, `rhi::UniformRing` with `Flush()`, `Device::FlushMappedBuffer`; a draw reading a uniform buffer, texture and sampler plus a push constant is verified by readback; docs 1683/1683; high-effort review (9 findings, 6 fixed, 3 declined) in the day file.

Phase 3 Day 61 (triangle milestone and offscreen golden-image tests) is done: `tests/rhi/{render_target.h,image_compare.*}` and a 64x64 golden (`tests/rhi/golden/triangle.ppm`, regenerate with `ENGINE_RHI_UPDATE_GOLDEN=1`), analytic pixel checks independent of the golden, `rhi_demo` draws the triangle and takes `--frames N` / `--screenshot out.ppm`; guide section "Golden-image rendering tests" and the tutorial's "Draw a triangle"; docs 1683/1683; high-effort review (10 findings, 9 fixed, 1 declined) in the day file.

Phase 3 Day 62 (compute pipelines and dispatch) is done: Vulkan `CreateComputePipeline` and `Dispatch`, GLSL compute shaders `fill`/`saxpy`/`gradient`, 1M-element fill + saxpy and a storage-texture write verified headless under sync validation, guide section "Compute"; docs 1683/1683; high-effort review (8 findings, 6 fixed, 2 declined) in the day file.

Phase 3 Day 63 (depth, indexed draws and the spinning cube) is done: `game/common/cube_scene.*` (Vulkan-convention projection, look-at, cube mesh, `CubeRenderer`), `cube.vert`/`cube.frag`, `rhi_demo` draws a depth-tested spinning cube (resize-aware depth buffer, `--screenshot`), a 128x128 cube golden with analytic face probes, depth-occlusion, indexed-vs-expanded and culling tests, guide section with the 3D conventions (front faces are `CounterClockwise` with the Y-flipped projection); docs 1683/1683; high-effort review (8 findings, 7 fixed, 1 declined) in the day file.

Phase 3 Day 64 (textured cube) is done: `game/common/textures.*` (procedural textures, `.rtex` container, `CreateSampledTexture`), `textured.vert`/`textured.frag`, `CubeRenderer` textured mode, `rhi_demo` draws the textured cube from `game/rhi_demo/assets/checker.rtex` with trilinear + anisotropic sampling; quadrant, sRGB-vs-UNORM, mips-vs-no-mips and golden tests, guide section "Textures in draws" and the tutorial's "Add a texture"; docs 1683/1683; high-effort review (8 findings, 8 fixed, 0 declined findings; 3 notes) in the day file.

Phase 3 Day 65 (lit cube, the Phase 3 milestone) is done: `lit.vert`/`lit.frag`, `CubeRenderer::DrawLit`, a finished `rhi_demo` (orbit camera via `InputState`, orbiting light, `--backend/--frames/--screenshot/--no-vsync/--validation`, memory check, `game/rhi_demo/README.md`), lighting golden + Lambert/ambient/zero-light/mirror tests, guide/shaders/tutorial updates; docs 1683/1683; 1000-frame runs on Wayland and X11 with validation + sync validation: zero messages, 4.1-4.2 ms/frame (vsync, RTX 3060), memory +0.4 MiB; high-effort review (10 findings, 10 fixed) in the day file.

Phase 3 Day 66 (hardening, sanitizers and the Vulkan boundary gate) is done: `rhi_boundary_check` ctest (comment-aware CMake scan of engine/game/tools, reverse rule for `engine/rhi/*.h`, `path|reason` allow-list that rejects stale entries, planted-violation fixtures) found and fixed two real leaks of the boundary (the demo's `VulkanDevice` use; the null backend's `vk_array.h`, now `engine/rhi/grow_array.h`); `rhi::FaultInjection` (device loss at a frame, allocation failure from the Nth) with `rhi_demo --inject-device-loss/--inject-oom` (exit 3 / exit 1), `[fault]` tests including a 200-resize storm on Wayland and X11; `tests/rhi/lsan.supp` with a reason per entry; all 149 asserts and the log sites classified (table in the RHI guide); fixed a triangle golden test that failed instead of skipping without Vulkan; RHI suite ~15 s with a GPU, ~1 s without; 1277/1277 tests, docs 1689/1689; high-effort review (10 findings, 8 fixed, 2 declined) in the day file.

Phase 3 Day 67 (portability review, performance baseline, phase wrap-up) is done and **Phase 3 is complete**: `docs/superpowers/specs/2026-11-04-rhi-portability-review.md` (every RHI concept vs Direct3D 12 and Metal; one interface change - `DeviceCaps::buffer_texture_copy_offset/row_pitch_alignment`, 512/256, enforced by the null and Vulkan backends - and the rest filed as Phase 12/13 shims); `game/rhi_bench` baseline recorded in the day file and `docs/guide/rhi.md` (Release, RTX 3060: ~0.5-0.6 us per draw, 0.15-0.2 us per barrier, 14-18 GiB/s uploads, cube pipeline 3.6-5.8 ms; device mutex and per-draw transient bind groups are the hotspots); `docs/guide/rhi.md` is `draft`, the tutorial runs goal -> triangle -> cube -> texture -> lit; Phase 4 notes updated. Final run: 1280/1280 tests (plain, ASan, TSan, docs 1691/1691), validation and sync validation silent; high-effort review (7 findings, all fixed) in the day file. Phase 4 (Renderer core) has no day files yet, so it is not startable until they are written and committed (CLAUDE.md). Run the baseline: `cmake --build build --target rhi_bench && build/game/rhi_bench/rhi_bench` (use a Release build for meaningful numbers).

Phase 4 (Renderer core) is planned, not started: 17 day files (Days 68-84) under `plans/phase-04-renderer-core/`, indexed by `plans/phase-04-renderer-core.md`. Decisions (2026-11-04): Slang as the shader language (offline `slangc`; GLSL ported on Days 69-70), deferred lighting without MSAA, a generic render graph on the RHI, no bindless/indirect/async compute. Phase 4 can start.

Phase 4 Day 68 (renderer design spec and build skeleton) is done: `docs/superpowers/specs/2026-11-04-renderer-design.md` (R1-R11, including the G-buffer formats and the GGX lighting model), `engine/renderer/` + `tests/renderer/` (plain/ASan/TSan; `RenderLog` on `LogRender`), module `renderer` and a guide skeleton, the boundary gate extended to `tests/renderer/` (seven planted reports); docs 1694/1694; high-effort review (6 findings, 5 fixed, 1 declined) in the day file. Next: Day 69 (install `slangc`, first shader ports).

Phase 4 Day 69 (Slang toolchain and first shader ports) is done: `slangc` v2026.18.2 installed and pinned (every generated `.spv.inc` now records `// compiler: <name> <version>`); `*.slang` ports of `fullscreen.vert`, `triangle.vert/frag`, `bindings_test.frag`, `fill/saxpy/gradient.comp` with committed outputs; `-DENGINE_SHADER_LANG=glsl|slang` (default `glsl` until Day 70); flags and pitfalls in `shaders/README.md`; `tests/rhi/slang_shader_test.cpp` proves each port matches its GLSL interface; the GPU suite passes unchanged against the Slang outputs with validation and sync validation silent. Regenerate with `cmake --build build --target shaders`. High-effort review (8 findings, 7 fixed, 1 declined) in the day file. Known: `X11Window::Create opens a mapped window on the real X server` fails consistently under this session's Xwayland (unrelated). Next: Day 70.

Phase 4 Day 70 (remaining shaders ported, GLSL retired) is done: `cube`, `textured` and `lit` shaders ported to Slang; every golden image and analytic probe passed unchanged against the Slang outputs before anything was deleted; `rhi_demo --frames 1000 --validation` clean on Wayland and X11; the GLSL sources, the `glslc` path and `ENGINE_SHADER_LANG` are gone, Slang sources and `*.spv.inc` live in `shaders/` and `shaders_check` uses `slangc` only; `tests/rhi/shader_interface_test.cpp` asserts every shader's interface; HLSL/Metal emission checked (Y flip stays in the projection; Metal renames `main`, see `shaders/README.md`). PLAN.md #18, CLAUDE.md and the guides now describe only Slang. High-effort review (6 findings, all fixed) in the day file. Vsync-paced demo frame time was 50 ms (Wayland) / 17 ms (X11) vs 4.1 ms in Phase 3 in this session (compositor pacing, shader cost is negligible with `--no-vsync`): re-measure after the session restart together with the X11 test failure. Next: Day 71 (reflection).

Phase 4 Day 71 (shader reflection and the mint tool) is done: `tools/shader_mint` (replaces `spv_embed`) turns `slangc` output into committed `*.spv.inc` and `*.refl.inc`, reading `-reflection-json` with vendored nlohmann/json (tool-only; `PLAN.md` assumption #19, decided with you on 2026-11-05) and cross-checking it against the SPIR-V; `renderer::ShaderReflection` with a strict versioned blob (`ParseShaderReflection` rejects bad version, truncation, unknown types, corruption without out-of-bounds reads, ASan-tested), `BuildBindGroupLayout`/`BuildVertexLayout`/`CheckBlockSize`; generated block size/offset constants now back the `static_assert`s on `LitUniforms` and the cube `Mat4`; everything builds without `slangc`. Docs 1770/1770. High-effort review (10 findings, 9 fixed, 1 declined: the tool uses the STL because the JSON library does) in the day file. Regenerate with `cmake --build build --target shaders`. Next: Day 72.

Phase 4 Day 72 (shader permutations) is done: variants are compile-time defines (`-DOPTION=1`), not specialization constants; measurements and the portability reasoning are in the renderer spec R10.1 (pipeline creation time was not measured, and the spec says so). A shader declares `// permutation-options: ...`, a `<name>.perm` manifest lists up to 32 keys of up to 16 options, `shader_mint --permute` compiles each key itself, dedupes identical modules and commits `<name>.perm.inc`; `renderer::FindPermutation` returns the module or an error naming the shader and key, never a fallback. No RHI change. Test shader `permute_test.frag` with a GPU analytic probe per key; `shaders_check` regenerates and compares the tables. Docs 1797/1797, 11/11 samples. High-effort review (10 findings, all fixed) in the day file. Next: Day 73.

Phase 4 Day 73 (renderer foundation) is done: `renderer::Camera`, `Frustum`, bounds, `MeshData`/`GpuMesh` with cube, sphere and plane builders, `RenderScene` (generation-checked ids, lights, culling) and `FrameResources` (frame uniform plus one per-object buffer, three frame regions); the camera helpers moved out of the demo. 100k-object cull about 3 ms (recorded). Docs 1979/1979. High-effort review (8 findings, 4 fixed, 3 declined, 1 process) in the day file. Next: Day 74.

Phase 4 Day 74 (materials) is done: `renderer::MaterialSystem` with templates and materials validated against reflection (set 0 frame / 1 material / 2 objects), per-material parameter buffer with three frame regions and a batched flush, bind groups rebuilt only for the material whose texture changed, default textures, a pipeline cache keyed by (template, permutation, target, vertex layout) with hit/miss counters, and `MakeDrawSortKey` (pipeline, material, mesh). A GPU test draws three objects through three materials by `first_instance` and reads pixels back. Finding: use `SV_VulkanInstanceID` for the object index (D3D12/Metal need another route, filed). Docs 100%. High-effort review (9 findings, 7 fixed, 1 declined, 1 process) in the day file. Next: Day 75.

Phase 4 Day 75 (render graph I: declaration and compilation) is done: `renderer::RenderGraph` with virtual transient and imported resources, passes that declare reads/writes with a usage that fixes the resource state, edges derived from declaration order (raw/waw/war) plus explicit `AddDependency`, a stable topological schedule, culling of unused passes (roots: side effects and writes to imports), first/last use per resource, a text dump and Graphviz writer used as test oracles, and named compile errors. All graph storage comes from a per-frame arena (a test counts zero heap allocations). A 1000-pass random DAG matches an independent reference (0.18 ms). Docs 100%. High-effort review (8 findings, 7 fixed, 1 declined) in the day file.

Phase 4 Day 76 (render graph II: automatic barriers and transient resources) is done: `Compile` plans the barrier list (state tracking from each resource's usage sequence, merged reads, same-state write ordering, import entry and exit states such as swapchain to `Present`) and checks transient usage flags; `RenderGraph::Execute` records each pass's barriers before it and takes transients from the new `renderer::TransientPool` (reuse by description across frames and across non-overlapping lifetimes in one frame, idle destruction, eviction when full, counters). Null-device oracle tests (a deferred-shaped graph, zero errors, four frames) and a real-device four-pass test with validation and synchronization validation (zero messages) pass; mutations confirmed both bite. New shader `graph_sample.frag`. Docs 100%. High-effort review (10 findings, 6 fixed, 3 declined, 1 noted) in the day file. 

Phase 4 Day 77 (render graph III: execution, parallel recording, swapchain) is done: transients are resolved first and each scheduled pass records into its own command list, in parallel on `jobs::Scheduler` participants, submitted once in schedule order (`RenderGraph::ExecuteParallel`); `renderer::Renderer::RenderFrame` runs the frame loop (acquire/import target, build, compile, record, submit, present) with `Ok/Skipped/DeviceLost/Failed` statuses, minimize/resize/loss handling and a presented swapchain image on every acquired path. Tests: 1000 frames without growth, ordering of lists, parallel versus serial pixel equality on a real device, device loss, an allocation-failure sweep, TSan on the null parallel tests. Docs 100%. High-effort review (8 findings, 6 fixed, 1 declined, 1 process) in the day file. Next: Day 78.

Phase 4 Day 78 (mesh vertex colors & primitive topology) is done: `GraphicsPipelineDesc::primitive_restart` (Vulkan `primitiveRestartEnable`, validated: strip topologies only) with GPU strip tests under validation and sync validation; `MeshData`/`GpuMesh` carry a topology and restart flag; `ColorVertex`/`ColorVertexLayout` (UNorm8x4 sRGB color at location 4, interleaved) with `MakeColoredMesh`/`MakeUniformColoredMesh`; `MakePlaneStripMesh`, `StripToList`, `ListToStrip` (restart or degenerate joins); `MaterialSystem::GetPipeline`/`Warmup` take topology and restart and key the pipeline cache on them. Docs 100%, guide section "Vertex colors and primitive topology". High-effort review (8 findings, 3 fixed, 4 declined with reasons, 1 process) in the day file. Next: Day 79.

Phase 4 Day 79 (the G-buffer geometry pass) is done: `renderer::GBufferPass` (new library `renderer_passes`) culls, sorts by `MakeDrawSortKey` and draws the scene into albedo (`RGBA8_SRGB`), octahedral normal (`RG16_Float`), material (`RGBA8_UNorm`) and `D32_Float` depth with instanced runs; Slang shaders `gbuffer.vert`/`gbuffer.frag` (permutations normal map, alpha test, vertex color) and a `GBufferDebugView`; named rejections for unprepared meshes, missing materials and vertex-color mismatches; cached frame/object bind groups in `MaterialSystem`. Real-device tests (validation and sync validation silent) read every layer back and compare against CPU values. Docs 100%, guide section "The G-buffer geometry pass". High-effort review (10 findings, all fixed except the per-frame pipeline lookup cache and the first-message-only error, declined with reasons) in the day file. Next: Day 80.

Phase 4 Day 80 (deferred lighting: directional and ambient) is done: `LightType::Ambient`, a per-frame light storage buffer (`FrameResources::WriteLights`), `renderer::LightingPass` (HDR `RGBA16_Float` accumulation; world position rebuilt from depth; Lambert + GGX metallic-roughness in one shader function; background skipped), `renderer::TonemapPass` (exposure + ACES fit, sRGB through the target format), the CPU twins `ShadeLight`/`ShadeAmbient`/`TonemapAces`/`ReconstructWorldPosition`, and a `WorldPosition` debug layer. Real-device tests (validation and sync validation silent) match the CPU model at probes, sweep the light direction, check positions against planted geometry and compare a cube against the Phase 3 forward Lambert values. Docs 100%, guide section "Deferred lighting and the tonemap". High-effort review (9 findings, 7 fixed, 2 declined) in the day file. Next: Day 81 (not started; the request covered Days 77-80).

Phase 4 Day 83 (renderer hardening & performance) is done: failure-injection tests (`tests/renderer/renderer_fault_test.cpp`, plus real-device sweeps in `tests/rhi/vulkan_renderer_demo_test.cpp`) lose the device between frames, in the frame callback and in each pass, refuse a transient, lose a texture file, leave out an object with no pipeline and fail the G-buffer and lighting pipeline builds, each ending with a defined status, a logged cause and nothing left alive; the sweep found and fixed a null command-list crash in `demo::CreateSampledTexture`. All 118 assert/log sites in `engine/renderer` are classified (asserts = programming errors, logs = environment) and the missing `Aborts:` lines added; the guide has a failure-behavior table and performance notes. `game/renderer_bench` (run by hand: `renderer_bench [--section graph|frame|lights|pool] [--device null]`, Release build) measured 0.08 us per object (10000 objects in 0.78 ms, 16 draws), 0.05 us per graph pass, 0.1-0.2 us CPU per point light and a 99% transient pool hit rate; the profile and the budgets for the sanitizer flavors are in the day file. 1832 tests pass in plain, ASan and TSan; docs 100%. High-effort review (9 findings, 8 fixed, 1 noted) in the day file. Next: Day 84 (portability review & Phase 4 wrap-up).
Phase 4 Day 84 (portability proof & wrap-up) is done and **Phase 4 is complete**: `shaders_portability` compiles all 24 shaders and 39 permutation keys to HLSL and Metal (76 outputs; `fill.comp`/`saxpy.comp` cannot go to Metal because of `GetDimensions`, listed with the reason), `docs/superpowers/specs/2026-11-04-renderer-portability-review.md` gives a verdict per item (4 shims, 1 deferred change, no interface redesign; one cheap change made: `CheckGBufferFormats` checks the color attachment limit), `docs/guide/renderer.md` is finished and marked draft, `rhi-shaders.md` is reviewed, notes added to phases 5-9. Final run: 1839 tests pass in plain, ASan and TSan; docs 100%; both demos ran 1000 frames with validation on Wayland and on X11 with no messages and flat memory. High-effort review (9 findings, 6 fixed, 3 declined or noted) in the day file. **Next: Phase 5 (Windows port + DX12), which needs its day files written and committed before it starts.**
**Roadmap change (2026-11-05, later):** three phases were inserted after Phase 4 and before the Windows port: **5 Test scoping**, **6 Debugging facilities** (stack traces, core dumps, minidumps, assert/ensure macros) and **7 RHI reset** (any rendering setting changes at run time by re-initializing the RHI; device loss recovers through the same path). Everything from the old Phase 5 moved up by 3 (Windows is Phase 8, macOS 9, Font 10, UI 11, ECS 12, Physics 13, Audio 14, Assets 15, Scripting 16, Android + iOS 17, Consoles 18, Unreal import 19-20, Editor 21, First playable demo 22). Earlier paragraphs in this file keep the numbers they were written with. The new phases have plan files at goal level (`plans/phase-05-test-scoping.md`, `phase-06-debugging-facilities.md`, `phase-07-rhi-reset.md`) and need day files before any of them starts. The day files for Phase 5 (Days 85-90, `plans/phase-05-test-scoping/`) are written: inventory and design, phase labels (map file plus a CMake function), the RHI/renderer labels, the `scripts/test-phase.sh` runner with touched-module selection, workflow and policy docs, and measurement with the closing full run. **Next: Day 85 (nothing started; the request was to plan the days).**

**Roadmap change (2026-11-05):** the Windows and macOS ports moved from Phases 12-13 to **Phases 5 and 6**, right after the renderer core, and the later phases were renumbered (7 Font, 8 UI, 9 ECS, 10 Physics, 11 Audio, 12 Assets, 13 Scripting; 14 is now Android + iOS). Earlier paragraphs in this file keep the numbers they were written with. Phase 4 is still the active phase; Phases 5 and 6 need day files before they can start. See the 2026-11-05 entry in `PLAN.md`.


Phase 4 Day 80a (foundation `Array` and `Vector`; no raw `new[]`/`delete[]`) is done: the growable container is `Vector<T, Allocator = HeapAllocator>` (renamed from `Array`; owns and frees heap storage, `Resize`/`TryResize`/`TryResizeUninitialized`, `Data()`), `Array<T, N>` is the inline `std::array`-like type, and all 50 raw array sites in `engine/` use `Vector`. `ctest -L arrays` (`raw_array_check`, with planted-violation fixtures) fails if `new T[` / `delete[]` reappears in `engine/` or `game/`. 1741 tests pass (plain, ASan, TSan); docs 100%, guide section "Array and Vector: which one". High-effort review (8 findings, 6 fixed, 2 declined) in the day file. Next: Day 81 (not started).

Phase 4 Day 80b (`UniquePtr`, `SharedPtr` and no raw `new`/`delete`) is done: `UniquePtr<T, Deleter>` and `SharedPtr<T, Count>` (atomic or plain count) in `containers/`; `rhi::CreateDevice` returns a `rhi::DevicePtr` (the deleter waits for the GPU) and `DestroyDevice` is gone; the scheduler, Vulkan memory/command-pool/swapchain state, the null device and the async read state are owned by `UniquePtr`; the two leaked singletons are gone (`FallbackSlots` is an atomic bitmask table, `FramePool::Global()` lives in static storage). `ctest -L arrays` (`raw_new_check`) bans every raw `new`/`delete` in `engine/` and `game/`. 1754 tests pass (plain, ASan, TSan); docs 100%. High-effort review (10 findings, 8 fixed, 2 declined) in the day file. Next: Day 81 (not started).

Phase 4 Day 81 (point lights & light volumes) is done: `PointLightAttenuation` (windowed inverse square, exactly 0 at the radius), `kMaxLights` = 16384 with counted, logged overflow (`LightSummary::dropped`), frustum-culled and distance-sorted point lights in one storage buffer (`WriteLights(scene, frame_data)`), and `LightingPass` drawing one additive, front-face-culled, scissored sphere volume per light with one pipeline (the sphere is generated in the vertex shader; new `point_light.vert`/`point_light.frag`). Real-device tests (validation and sync validation silent) match the analytic model, light a camera inside a volume, sum overlapping lights, check that a culled light issues no draw, and handle a light larger than the far plane; measured at 1080p: 1 / 100 / 1000 lights = 0.6 / 1.1 / 5-6 ms wall time. 1785 tests pass; docs 100%, guide subsection "Point lights". High-effort review (8 findings, 5 fixed, 2 declined, 1 process) in the day file. Next: Day 82 (renderer demo & depth correctness).

Phase 4 Day 82 (the renderer demo & depth correctness) is done: `game/renderer_demo` (540 objects, eight materials, 300 point lights, a sun, orbit and fly cameras, `--gbuffer`/`--lights`/`--objects`/`--screenshot`/`--validation`, device-loss exit code 3) built on `renderer::Renderer` and the render graph; procedural textures written by `renderer_demo_mint` and checked byte for byte; a golden image, draw-order independence (shuffled add orders give identical pixels) and a crossing-planes depth test with analytic probes, validation silent; `ctest -L demo` covers screenshots and fault injection; README and the tutorial `hello-renderer.md`. Milestone: 1000 frames with `--validation` on Wayland and on X11, zero messages, 1.27 / 1.28 ms per frame, memory flat (details in the day file). 1807 tests pass; docs 100%. High-effort review (10 findings, all fixed) in the day file. Next: Day 83 (done below).
Phase 5 Day 85 (test inventory & scoping design) is done (planning only): `docs/superpowers/specs/2026-11-05-test-scoping-design.md` has the inventory (1839 tests; executables alone: foundation 22.7 s, jobs 4.6, docgen 4.2, renderer 5.0, rhi 49.0), the per-file phase assignment with the ambiguous cases explained, the label mechanism (Catch2 v3 has no per-file tag, so a small patch to the vendored `CatchAddTests.cmake` behind an `engine_discover_tests` wrapper), the `ctest -L` combinations (repeated `-L` is AND, `|` is OR, `-LE` excludes), the decisions (labels `phase0`..`phase4`, `phase2_5`; `tests/phases.txt`; `CURRENT_PHASE`; scoped = current phase + touched modules; always-full triggers; flavors are labels `asan`/`tsan`) and the risk table. **Next: Day 86 (phase labels).**
Phase 5 Day 86 (phase labels for every test) is done: `tests/phases.txt` is the one place a test's phase is decided; Catch2 tests get a `phaseN` label (`phase2_5` for 2.5) through `engine_discover_tests` (`cmake/EngineTestPhases.cmake`, a marked patch to the vendored `CatchAddTests.cmake`; a Catch2 test file missing from the map stops the build), other tests through `engine_label_tests`; `test_phase_labels` (with planted unlabeled/doubled fixtures and an unused-line check) enforces exactly one label. Counts: phase0 87, phase1 63, phase2 232, phase2_5 340, phase3 580, phase4 537, phase5 4 = 1843; names unchanged; the patch also made the ASan docgen tests carry their `asan` label. The rhi/renderer call sites were switched on the same day. 1843/1843 pass. High-effort review (9 findings, 6 fixed, 3 declined or process) in the day file. **Next: Day 87 (no-GPU discovery check, flavor coverage, per-phase baseline times).**
Phase 5 Day 87 (labels for the RHI and renderer, flavors) is done: discovery builds with no Vulkan driver; `flavors` lines in `tests/phases.txt` state each phase's sanitizer flavors and `test_phase_labels` enforces them (two planted fixtures); a `;`-in-test-name counting bug in the check was fixed. Baseline scoped times (wall, `-j16`): phase 3 plain 17 s / ASan 39 s, phase 4 plain 13 s / ASan 38 s, the whole suite about 65 s. 1845/1845 pass. High-effort review (9 findings, 6 fixed, 2 declined, 1 process) in the day file. **Next: Day 88 (the `scripts/test-phase.sh` runner and touched-module selection).**
Phase 5 Day 88 (the runner) is done: `CURRENT_PHASE` (5), `tests/module_map.txt`, and `scripts/test-phase.sh [N | --current | --affected | --all] [--flavor plain|asan|tsan|all] [--jobs N] [--dry-run]`, which builds, counts, runs the selected labels and ends with `ran X of Y tests (phases ...; flavors ...; display ...)`; `--affected` adds the phases of every module touched since `CURRENT_PHASE` last changed and runs everything for CMake files, `cmake/`, `third_party/`, `engine/foundation` headers, `tests/phases.txt` or a path in no map line. 29 `test_phase_script_*` cases (counts checked against `ctest -N`, planted regression verified). **`CURRENT_PHASE` is changed in the commit that starts a phase.** 1874/1874 pass. High-effort review (10 findings, 9 fixed, 1 declined) in the day file. **Next: Day 89 (workflow policy and docs).**
Phase 5 Day 89 (workflow, policy and docs) is done (documentation and rules only): `CLAUDE.md` has the scoped-test rule (run `--affected` before the review, quote the `ran X of Y tests` line, close a phase with `--all --flavor all`), `STATUS.md` has the build and test commands, `PLAN.md` the guiding principle, the guide "Test phases and scoped runs", and the Phase 6 and 7 plans a closing-run deliverable. **Next: Day 90 (measurements, planted regressions, the closing full run, Phase 5 complete).**
Phase 5 Day 90 (measurement and wrap-up) is done and **Phase 5 is complete**. Closing run: `ran 1874 of 1874 tests (phases: all; flavors: all) in 67 s`, all pass; `-j1` takes 341 s; docs 4/4; both demos 1000 frames with validation on Wayland and X11, silent. Savings: a new phase's own tests ~1.4 s, an RHI/renderer day 638 tests (26 s), a foundation day 871 tests (38 s). Planted faults: a foundation allocator overlap is caught by `--affected`; an RHI generation fault is missed by `--current` and caught by `--affected` and `--all`; two milder allocator faults were caught by no test (KNOWN-ISSUES). `CURRENT_PHASE` is now 6. **Next: Phase 6 (Debugging facilities) needs its day files written and committed before it starts.**

Phase 6 Day 91 (library selection and spikes) is done (planning and spikes only, no code): libbacktrace (commit `0b9b49cf4a`) and sentry-native 0.17.6 with the Breakpad backend and no transport are chosen (`PLAN.md` assumption #20, spec `docs/superpowers/specs/2026-11-06-debugging-design.md`); the Crashpad backend was rejected because on Linux it needs libcurl and a handler executable with about 25 shared libraries. Days 92-102 are corrected to match. `CURRENT_PHASE` is 6. **Next: Day 92 (death-test helper and the `tests/debug` suite).**

Phase 6 Day 92 (death-test helper, fault injection and the `tests/debug` suite) is done: `debug::RunInChild` re-executes the test program (no `fork`), runs a function registered with `ENGINE_DEATH_TEST_FN` and returns exit code, fault (`CrashReason` plus the native code), timeout and captured output; `debug::InjectFault(FaultKind)`; Linux backend under `engine/foundation/os/linux/debug/`; `tests/debug` in plain, ASan and TSan (67 tests, 160 repeat runs clean, also non-unity and clang); `tools/platform_neutral_check` keeps platform types out of the neutral headers and the library headers out of every file but their one implementation file. Docs: module `foundation/debug` 62/62, guide `foundation-debugging.md`. Scoped runs: `ran 894 of 1939 tests (phases: all; flavors: plain; display: included)`, asan `ran 655 of 1939`, tsan `ran 390 of 1939`, all pass. High-effort review (10 findings; 8 fixed, 1 declined, 1 bookkeeping) recorded in the day file. **Next: Day 93 (assert, ensure, verify family).**

Phase 6 Day 93 (assert, ensure, verify family and the handler chain) is done: `ENGINE_ASSERT`, `ENGINE_ENSURE` (once per call site, returns the condition), `ENGINE_VERIFY`, `ENGINE_CHECK_SLOW`, `ENGINE_UNREACHABLE`, `ENGINE_NOT_IMPLEMENTED`, `SetAssertHandler` and the default handler (`ASSERT FAILED: file:line function: condition: message`, then abort, break into a debugger, or for ensure continue); a failed assert now dies with `SIGABRT` instead of `exit(1)`; `IsDebuggerAttached`, `debug/compiler.h`; CMake `ENGINE_FORCE_DEBUG_CHECKS`/`ENGINE_FORCE_SLOW_CHECKS`; the exit sink is gone (two tests migrated); the gate bans the per-translation-unit macros in headers. 152 Phase 6 tests (also non-unity, clang, Release). Scoped runs: `ran 925 of 2024` plain (one known display failure), `ran 682 of 2024` asan, `ran 417 of 2024` tsan. High-effort review (9 findings; 6 fixed, 2 declined, 1 bookkeeping) in the day file. Release builds found a Day 92 bug (fixed) and a pre-existing jobs failure (KNOWN-ISSUES). **Next: Day 94 (vendoring libbacktrace and the stack-trace facade).**

Phase 6 Day 94 (libbacktrace and the stack-trace facade) is done: libbacktrace (commit `0b9b49cf4a`) is vendored under `third_party/libbacktrace/` with our CMake wrapper; `debug/stack_trace.h` captures, resolves (function, file, line, inlined frames, module and offset) and formats traces with no library type in sight, `debug/module_list.h` names the module of an address, and a failed `ENGINE_ASSERT` or the first failure of an `ENGINE_ENSURE` site prints its stack trace under the message in one write; the gate lets only `stack_trace_backtrace.cpp` include `backtrace.h`. 201 Phase 6 tests (also non-unity, clang, Release; the signal-handler test runs in all three flavors). Scoped runs: `ran 942 of 2073` plain (one known display failure), `ran 698 of 2073` asan, `ran 433 of 2073` tsan. High-effort review (10 findings; 9 fixed, 1 declined) in the day file; the new concurrency test found a real hang in the first init ordering (fixed). **Next: Day 95 (vendoring the crash library, sentry-native with the Breakpad backend, and the crash facade).**

Phase 6 Day 95 (sentry-native and the crash facade) is done: sentry-native 0.17.6 is vendored under `third_party/sentry_native/` (Breakpad backend, no network transport, 6.8 MB) behind `debug/crash_handler.h` (`InstallCrashHandler`, `UninstallCrashHandler`, `CrashHandlerOptions` with no address or account field); a crash leaves an envelope with a minidump of every thread that survives the next start (a local `saved/` transport; the null transport would have deleted it), the process still dies from the original fault, the dump directory is private to the user; `tools/crash_extract` pulls the minidump out of the envelope; `os::GetCrashDumpDirectory`, `os::EnsureDirectory`, `os::EnsurePrivateDirectory`; ctests check that no test binary links or imports anything network. 283 Phase 6 tests (the crash flows also in ASan and TSan; non-unity, clang, Release). Scoped runs: `ran 976 of 2155` plain (one known display failure), `ran 722 of 2155` asan, `ran 457 of 2155` tsan. High-effort review (9 findings; 8 fixed, 1 partly) in the day file; it found that dumps were deleted on relaunch. **Next: Day 96 (crash report content).**

Phase 6 Day 96 (crash report content) is done: `debug/crash_context.h` (build id from the binary's GNU note, platform, memory, command line), a lock-free 256-line log ring (`ReadRecentLogLines`, 33-48 ns per log call in Release including the append), `RegisterCrashStateProvider` (16 entries), `WriteCrashReport`/`WriteCrashContext` (fixed section order, written in small chunks, no allocation), thread names, and failed asserts now print the same context; the build id is the Sentry release. 346 Phase 6 tests. Scoped runs: `ran 997 of 2218` plain (one known display failure), `ran 743 of 2218` asan, `ran 478 of 2218` tsan. High-effort review (9 findings; 7 fixed, 1 declined, 1 deferred to Day 97) in the day file. **Next: Day 97 (crash handler integration).**

Phase 6 Day 97 (crash handling end to end) is done: a crash prints a report on standard error and to `crash-report.txt` (attached to the envelope), the library writes the minidump, and the process dies from the original signal; works for every fault kind, a stack overflow, other threads (`RegisterThreadForCrashHandling`), two threads at once, a faulting provider (`double fault`), a failed assert, and the dump opens in `lldb`. Design change: a recorder in front of Breakpad's handler writes the report, because the `on_crash` hook gets no signal information and runs on a helper thread (day file). `tools/signal_safety_check` gates the crash-path code. 395 Phase 6 tests. Scoped runs: `ran 1016 of 2267` plain (one known display failure), `ran 758 of 2267` asan, `ran 493 of 2267` tsan; 20 repeats clean. High-effort review (10 findings; 7 fixed, 2 declined, 1 deferred to Day 101) in the day file. **Next: Day 98 (core dumps).** Local commits only: the remote rejected the history rewrite (see the conversation), so nothing after `61c7ee7` is pushed.

Phase 6 Day 98 (core dumps) is done: `EnablePostMortemDumps` (neutral interface, Linux implementation: core limit, dumpable, `core_pattern`), `CrashHandlerOptions::post_mortem_dumps` with a `core dumps:` line in the crash report, `scripts/find-core.sh` and `scripts/open-core.sh` (systemd-coredump and pattern files, build-id check, gdb batch), the guide section, and a gdb end-to-end test. The log ring got a real fix (a lapped writer could corrupt a slot). 425 Phase 6 tests. Scoped runs: `ran 1026 of 2297` plain (one known display failure), `ran 768 of 2297` asan, `ran 503 of 2297` tsan. High-effort review (10 findings; 9 fixed, 1 partly) in the day file. The gdb test failed twice in loaded runs and could not be reproduced (`KNOWN-ISSUES.md`). **Next: Day 99 (dump tooling and symbols).** Commits are local only (the remote rejected the history rewrite).

Phase 6 Day 99 (dump tooling and symbols) is done: `scripts/crash-report.sh` (envelope or minidump to a trace with `lldb`, `minidump-stackwalk` when present, build-id check), `scripts/store-symbols.sh` (symbol store by build id), `scripts/split-debug.sh` and the CMake option `ENGINE_SPLIT_DEBUG_INFO`, the guide sections and the `llvm-symbolizer` recipe. 446 Phase 6 tests. Scoped runs: `ran 1033 of 2318` plain (one known display failure), `ran 775 of 2318` asan, `ran 510 of 2318` tsan. High-effort review (10 findings; 7 fixed, 2 declined, 1 for the next commit) in the day file. The Breakpad `.sym` path is documented but not exercised (`KNOWN-ISSUES.md`). **Next: Day 100 (subsystem integration).** Commits are local only (the remote rejected the history rewrite).

Phase 6 Day 100 (subsystem integration) is done: `debug::StartProgramDebugging` / `RunCrashTest` (build line, handler by flag, environment or build default, `--crash-test=segv|abort|assert|ensure`) in all four demos and benches with ctests per kind and a no-network check each; `SetLogErrorTraces`; crash state helpers (`SetCrashStateText`, `CrashStateCounter`) used for the `gpu`, `frame` and `window` lines; job workers and the I/O thread register by name; device loss and validation errors print an ensure trace; Release behavior recorded. 483 Phase 6 tests. Scoped runs: `ran 1054 of 2357` plain, `ran 784 of 2357` asan, `ran 519 of 2357` tsan. High-effort review (9 findings; 6 fixed, 2 declined, 1 partly) in the day file; it exposed and fixed a deadlock when the installing thread had too small a signal stack. **Next: Day 101 (Windows/macOS readiness).** Commits are local only (the remote rejected the history rewrite).

Phase 6 Day 101 (Windows and macOS readiness) is done: the platform audit (design spec section 10, with the known gaps and their decisions), `BACKEND.md` checklists for Windows and macOS kept in step with the headers by a new `Backend:` doc label and the ctest `debug_backend_lists`, `tests/debug/BACKEND-CONFORMANCE.md`, cross-target syntax checks for `x86_64-pc-windows-msvc` and `arm64-apple-macos13` (which found and fixed a clang-on-Windows bug in `ENGINE_NOINLINE_LAMBDA`), the porting guide `docs/guide/porting-debugging.md`, and the real remaining work in the Phase 8 and 9 files. 489 Phase 6 tests. Scoped runs: `ran 1061 of 2365` plain, `ran 785 of 2365` asan, `ran 519 of 2365` tsan. High-effort review (10 findings; 9 fixed, 1 accepted) in the day file. **Next: Day 102 (documentation sweep and closing run).** Commits are local only (the remote rejected the history rewrite).

**Phase 6 (Debugging facilities) is COMPLETE (Days 91-102, 2026-11-06).** Linux: stack traces with names, files and lines (libbacktrace), the assert/ensure/verify family, crash reports with build id, platform, engine state and recent log, minidumps through sentry-native's Breakpad backend with no upload, core dump setup and scripts, dump and symbol tooling, and every demo wired in (`--crash-handler`, `--crash-test`). Windows and macOS are ready (audit, `BACKEND.md`, conformance list, cross-target checks, porting guide). Closing run: `ran 2371 of 2371 tests (phases: all; flavors: all; display: included)`, all passing; docs 4/4; both demos 1000 frames clean on Wayland and X11; six planted regressions caught; measurements in the Day 102 file. End-of-phase high-effort review: 10 findings, 8 fixed, 2 declined. **Next: Phase 7 (RHI reset) needs its day files written and committed before it starts; `CURRENT_PHASE` stays 6 until then.**

