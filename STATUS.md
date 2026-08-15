# STATUS.md

Current progress snapshot for this project. This file changes often —
update it as part of the same commit whenever the plan advances (a day
file's tasks get checked off, a phase starts or completes, a build/test
command changes). See `CLAUDE.md` for the durable rules that don't change
with progress.

## Phase 0 (Foundation layer)

**In progress — Days 1-14 of 15 are complete and committed.** Real code
exists under `/engine/foundation`, built via CMake:
`cmake -B build && cmake --build build` produces `engine_main`
(`build/engine/foundation/engine_main`), which runs the current ad hoc
test suite (`Expect()`-style checks inline in `main.cpp`) and exits 0 on
success. A sanitized build exists as a separate target:
`cmake --build build --target engine_main_asan`, then run
`build/engine/foundation/engine_main_asan`. There is no `ctest`/Catch2
yet — Day 15 (not yet started) migrates the ad hoc tests to Catch2 v3
(`PLAN.md` assumption #16) and wires up real `ctest` integration; don't
invent or assume `ctest` commands before that lands. Days 11-12 (grapheme
clusters, NFC/NFD normalization) built out an offline Unicode-data table
generator (`/tools/unicode_data_gen/`, pinned to Unicode 17.0.0 — see
`/UNICODE_VERSION`) that emits the committed tables under
`/engine/foundation/generated/` (grapheme-break, decomposition,
combining-class, composition-exclusions). `Array<T, Allocator>` and
`String<Allocator>` also gained move construction/assignment as part of
Day 12 — needed to return `String<Allocator>` by value from
`NormalizeNFC`/`NormalizeNFD`, previously missing since only copy had
been (deliberately) disabled. Day 13 added `Vec2`/`Vec3`/`Vec4` (`/engine/foundation/vector.h`) and
`Mat3`/`Mat4` (`/engine/foundation/matrix.h`) — column-major storage,
column-vector convention (`v' = M * v`), documented in `matrix.h` since
it's load-bearing for later renderer/transform code. Day 14 added
`Quaternion` (`/engine/foundation/quaternion.h`: construction, Hamilton
product, conjugate/inverse, normalize, slerp, and both directions of
quaternion<->`Mat3` conversion) and `Transform`
(`/engine/foundation/transform.h`: position/rotation/scale, `Compose`,
`ToMatrix`). **Scope amendment made during Day 14** (PLAN.md assumption
#2, `CLAUDE.md`): scalar math functions (`sqrt`/`sin`/`cos`/`acos`/etc.)
now call `<cmath>` directly instead of being hand-rolled — solved
problems with no control/performance/learning upside to reimplementing.
`math_util.h`'s `Sqrt` was accordingly changed from Day 13's hand-rolled
Newton-Raphson to a thin `<cmath>` wrapper; the vector/matrix/quaternion
*types* themselves remain hand-rolled. Day 15 (not yet started) migrates
the ad hoc tests to Catch2 v3 — see
`docs/superpowers/specs/2026-08-14-unicode-string-support-design.md` for
the Days 10-12 Unicode design.

Every phase after Phase 0 is still unimplemented — check the relevant
`plans/phase-NN-name/day-NN-*.md` files for what's actually done before
assuming a subsystem exists.
