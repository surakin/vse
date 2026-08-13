# Phase 0 / Day 12 — Foundation Test Harness

**Phase:** [Phase 0 — Foundation Layer](../phase-00-foundation.md)
**Depends on:** Days 1-11 (everything this harness runs).

Uses Catch2 v3, vendored into the repo — see `PLAN.md` assumption #16
for why this supersedes the originally-planned hand-rolled runner.

## Tasks

- [ ] Vendor Catch2 v3's source (a specific pinned release tag, not a
  floating branch) into `third_party/catch2/`, committed directly to
  the repo — no CMake `FetchContent`, no submodule.
- [ ] Add `third_party/catch2` as a CMake subdirectory from the root
  `CMakeLists.txt` (`add_subdirectory(third_party/catch2)`);
  `enable_testing()`.
- [ ] Create a `foundation_tests` executable target with sources under
  `/tests/foundation` (per `PLAN.md`'s repo layout), linking
  `foundation` and `Catch2::Catch2WithMain`.
- [ ] Scope `-fno-exceptions` to the `foundation` library target only,
  not the `foundation_tests` executable — Catch2's assertion macros
  need exceptions; production code under test still builds and runs
  exception-free. Check empirically whether `-fno-rtti` also needs
  dropping for the test target, or whether Catch2 builds fine without
  it.
- [ ] Wire `catch_discover_tests(foundation_tests)` (from Catch2's
  CMake integration module, `Catch.cmake`) so `ctest` finds and runs
  each `TEST_CASE` as its own individual CTest entry.
- [ ] Migrate Days 3-11's ad hoc `Expect()`-based verification code
  (currently inline in `engine/foundation/main.cpp`) into Catch2
  `TEST_CASE`/`SECTION`s, one test file per subsystem under
  `/tests/foundation` (e.g. `mem_test.cpp`, `arena_allocator_test.cpp`)
  — no test logic left in `main.cpp`, which reverts to being just the
  engine's real entry point (drops the `Expect()` helper and all
  `Test*()` functions it currently calls).
- [ ] Run `ctest`; confirm every migrated test — Days 3-11's, plus
  whatever Day 12 itself adds — reports green individually.
- [ ] Commit.

**Done:** `ctest` builds and runs the full suite through Catch2, all
green, with each original test case visible as its own discovered
CTest entry (not one monolithic pass/fail).

## Notes / open questions

- `-fno-rtti` carve-out for the test target is unverified — resolve
  empirically when this day is implemented, not speculatively here.
- Catch2 version pin (which v3.x release tag) is a Day-12-time decision,
  not fixed by this plan.
