# Phase 0 / Day 15 — Foundation Test Harness

**Phase:** [Phase 0 — Foundation Layer](../phase-00-foundation.md)
**Depends on:** Days 1-14 (everything this harness runs).

Uses Catch2 v3, vendored into the repo — see `PLAN.md` assumption #16
for why this supersedes the originally-planned hand-rolled runner.

## Tasks

- [x] Vendor Catch2 v3's source (a specific pinned release tag, not a
  floating branch) into `third_party/catch2/`, committed directly to
  the repo — no CMake `FetchContent`, no submodule. Pinned to **v3.15.3**
  (latest release at implementation time); the upstream tarball was
  pruned down to the `CMakeLists.txt`/`src/`/`CMake/`/`extras/` files
  actually needed to build `Catch2`/`Catch2WithMain` as a CMake
  subproject and wire `catch_discover_tests` (docs/tests/examples/
  benchmarks/fuzzing/packaging files dropped — see
  `third_party/catch2/VENDORED.md` for the exact list and why each
  piece was kept or cut).
- [x] Add `third_party/catch2` as a CMake subdirectory from the root
  `CMakeLists.txt` (`add_subdirectory(third_party/catch2)`);
  `enable_testing()`.
- [x] Create a `foundation_tests` executable target with sources under
  `/tests/foundation` (per `PLAN.md`'s repo layout), linking
  `foundation` and `Catch2::Catch2WithMain`.
- [x] Scope `-fno-exceptions` to the `foundation` library target only,
  not the `foundation_tests` executable — Catch2's assertion macros
  need exceptions; production code under test still builds and runs
  exception-free. Check empirically whether `-fno-rtti` also needs
  dropping for the test target, or whether Catch2 builds fine without
  it.
  **Resolved:** the root `CMakeLists.txt`'s `add_compile_options()` was
  build-wide, which leaked `-fno-exceptions`/`-fno-rtti` into every
  consumer of `foundation` — including `foundation_tests`, since CMake
  `PUBLIC` compile options are a usage requirement that propagates to
  linking targets regardless of the consumer's own link keyword. Fixed
  by making `ENGINE_PRODUCTION_COMPILE_OPTIONS` a `PRIVATE` compile
  option on `foundation`/`foundation_asan` and applying it explicitly
  on `engine_main`/`engine_main_asan` instead of relying on
  propagation; `foundation_tests` never gets it, so it builds with
  exceptions and RTTI both enabled (default) — no separate `-fno-rtti`
  carve-out was needed since neither flag reaches the test target now.
- [x] Wire `catch_discover_tests(foundation_tests)` (from Catch2's
  CMake integration module, `Catch.cmake`) so `ctest` finds and runs
  each `TEST_CASE` as its own individual CTest entry.
- [x] Migrate Days 3-14's ad hoc `Expect()`-based verification code
  (currently inline in `engine/foundation/main.cpp`) into Catch2
  `TEST_CASE`/`SECTION`s, one test file per subsystem under
  `/tests/foundation` (e.g. `mem_test.cpp`, `arena_allocator_test.cpp`)
  — no test logic left in `main.cpp`, which reverts to being just the
  engine's real entry point (drops the `Expect()` helper and all
  `Test*()` functions it currently calls).
  Migrated 1:1 into 15 files (`mem_primitives_test.cpp`,
  `arena_allocator_test.cpp`, `pool_allocator_test.cpp`,
  `stack_allocator_test.cpp`, `array_test.cpp`, `hash_map_test.cpp`,
  `utf8_test.cpp`, `grapheme_test.cpp`, `normalize_test.cpp`,
  `string_test.cpp`, `vector_test.cpp`, `matrix_test.cpp`,
  `quaternion_test.cpp`, `transform_test.cpp`, `log_test.cpp`), plus a
  shared `test_helpers.h` for the float/vector/quaternion near-equality
  and byte-buffer-equality helpers several files need. The old
  setjmp/longjmp trick `TestAssertFailure` used to unwind past a
  simulated `ENGINE_ASSERT` failure (needed because `main.cpp` built
  without exceptions) was replaced with a real `throw`/`catch` in
  `log_test.cpp`, now that exceptions are available in the test binary.
- [x] Run `ctest`; confirm every migrated test — Days 3-14's, plus
  whatever Day 15 itself adds — reports green individually.
  All 45 original `Test*()` functions became 45 discovered `ctest`
  entries (some via `SECTION`s inside one `TEST_CASE`, e.g. `memcpy`/
  `memset`/`memmove`), all green. One test's original name
  (`"\r\n" is one grapheme cluster, not two`) had to be reworded —
  literal double-quotes and a comma inside a Catch2 test name break its
  `ctest`-CLI-filter round trip (`catch_discover_tests` reconstructs a
  `-- <name>` command-line argument per test, and both characters
  collide with Catch2's own filter-string syntax) — renamed to `CRLF is
  one grapheme cluster not two`.
- [x] Commit.

**Done:** `ctest` builds and runs the full suite through Catch2, all
green, with each original test case visible as its own discovered
CTest entry (not one monolithic pass/fail). Confirmed via a fresh
`cmake -B build && cmake --build build` followed by `ctest` in
`build/`: 45/45 tests pass individually; `engine_main`/`engine_main_asan`
still build and run (exit 0) unaffected by the compile-options fix.

## Notes / open questions

- `-fno-rtti` carve-out for the test target is unverified — resolve
  empirically when this day is implemented, not speculatively here.
  **Resolved** — see the `-fno-exceptions` task above; not needed as a
  separate carve-out.
- Catch2 version pin (which v3.x release tag) is a Day-12-time decision,
  not fixed by this plan. **Resolved:** v3.15.3.
