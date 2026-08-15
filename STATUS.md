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
with 30x randomized-order stress runs, 0 failures (64 total tests, all
green). Day 16 (file I/O) not yet started.
