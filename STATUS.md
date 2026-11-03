# STATUS.md

Current progress snapshot for this project. This file changes often —
update it as part of the same commit whenever the plan advances (a day
file's tasks get checked off, a phase starts or completes, a build/test
command changes). See `CLAUDE.md` for the durable rules that don't change
with progress.

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
`mem/` (`mem`, arena/pool/stack allocators), `containers/` (`array.h`,
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

**Phase 1 Days 13-21 done** (...; Wayland event loop, Wayland keyboard
— human visual/interactive check pending on Days 19-21); Days 22-26
(pointer input, hello-window milestone, X11 fallback, backend selection)
not yet started.
