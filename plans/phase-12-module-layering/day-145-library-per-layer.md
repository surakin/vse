# Phase 12 / Day 145 — One Library per Layer

**Phase:** [Phase 12 — Module Layering](../phase-12-module-layering.md)
**Depends on:** Day 144 (no cycle left).

Split `foundation` into its layers now that nothing points upward, and prove the split by linking layer tests against their own layer only.

## Tasks

- [x] **Libraries** per the spec: `foundation_base` (layers 0-5 and their `os/<platform>/` backend), `foundation_io` (layer 6 and `io/<platform>/`),
      `foundation_debug` (layer 7, `os/<platform>/debug/`, the only library that links libbacktrace and sentry-native), `foundation_settings` (layer 8); each links
      only the ones below it; `platform` now links `foundation_debug` (crash state) instead of `foundation`; `foundation` becomes an `INTERFACE` library linking
      all four, so every existing consumer builds unchanged.
- [x] **Flavors:** each library has the `_asan` and `_tsan` variants the old `foundation_asan`/`foundation_tsan` had, with the same flags; `foundation_asan` and
      `foundation_tsan` become aggregates the same way.
- [x] **Prove the layering by linking:** `tests/types` and the core `tests/foundation` files link `foundation_base` only; `tests/io` links `foundation_io`;
      `tests/debug` links `foundation_debug`; `tests/settings` links `foundation_settings`; the window tests link `platform`. A test that turns out to need a higher
      layer is either moved to that layer's test directory or links the higher library with a one-line reason in its `CMakeLists.txt`; record each.
- [x] **`module_deps_check` link rules** updated for the new targets (a library linking a higher one fails); a planted fixture proves it.
- [x] **Both unity modes** build (`ENGINE_UNITY_BUILD` on and off); unity batches now form per library, so re-check the conflicts the unity rule covers
      (CLAUDE.md, "Unity builds") and fix any new one with the macro pairs first.
- [x] **Measure:** full and incremental build times (touching a `string/` header, an `io/` source, a `debug/` source) before and after, recorded here.
- [x] **Documentation:** `docs/guide/building-and-testing.md` "Libraries and layers" (which library to link for what); `ARCHITECTURE.md` section 1 names the
      libraries; `STATUS.md` build notes.
- [x] **Runs (Linux):** `scripts/test-phase.sh --affected` (CMake changed: everything) plus asan and tsan, `--crashes`; quote the lines.
- [x] **End-of-day review:** `/code-review high`. Extra scrutiny: static-initialization order across the new libraries (registries, the log, the crash handler),
      symbols that resolved only because of link order in the old single library, the sanitizer variants linking the right flavor of every layer.
- [x] Update `STATUS.md`, `KNOWN-ISSUES.md`; commit and push.

**Done (2026-11-07):** the foundation builds as four layered libraries plus `platform`, each layer's tests link only that layer and those below, both unity modes and every
flavor pass, and the build times are recorded.

## Result

- Libraries: `foundation_base` (layers 0-5 and the `os/<platform>/` backend), `foundation_io`, `foundation_debug` (the only one linking libbacktrace and sentry), `foundation_settings`, each with `_asan`/`_tsan`
  variants; `foundation` (and its flavors) is an `INTERFACE` aggregate. The sanitizer flags are PUBLIC on `foundation_base_asan`/`_tsan` and reach every library above by linking. `platform` links
  `foundation_debug` (crash state) instead of `foundation`.
- Tests: `tests/types`, `tests/foundation_sanitized` and the new `foundation_base_tests` (the core files of `tests/foundation`) link `foundation_base`; `tests/io` `foundation_io`; `tests/debug`
  `foundation_debug`; `tests/settings` `foundation_settings`; `foundation_tests` (file and window tests) links `platform` and `foundation`. No test needed a higher library than its layer.
- Gate: `layer`/`target` lines for the four libraries; a planted upward link (`foundation_io` linking `foundation_debug`) reported `upward link: foundation_io (6) -> foundation_debug (7)` and was reverted;
  the existing `link` fixture covers it in the suite.
- Unity: both modes build. The non-unity build exposed three missing includes (the Day 144 `window_host_linux.cpp`, `session_state_test.cpp`, and `tests/docgen/reference_test.cpp`, the last one since Phase 8); fixed
  by explicit includes, no `SKIP_UNITY_BUILD_INCLUSION` needed.
- Build times (this machine, `-j$(nproc)`, seconds, before = HEAD of Day 144, after = Day 145): full 27 / 27; touch `string/string.h` 22 / 25; touch `io/path.cpp` 4 / 3; touch `debug/crash_state.cpp` 3 / 3.
  The split does not change build time; its value is that a layer builds and tests alone.
- Docs: `docs/guide/building-and-testing.md` "Libraries and layers"; `ARCHITECTURE.md` item 8 marked done. `ctest -L docs` passes (6 of 6).
- Runs: plain `--affected --crashes` = `ran 1527 of 3488 tests (phases: all; flavors: plain; display: included; crash tests: included)`; ASan `ran 1122 of 3488 tests (phases: all; flavors: asan; ...)`;
  TSan `ran 839 of 3488 tests (phases: all; flavors: tsan; ...)`. Only the two known Wayland/NVIDIA demo failures (plain).

## Post-implementation code review

Inline `/code-review high`. Four findings, all fixed: the three missing includes above (correctness) and a stale comment in `tests/foundation/CMakeLists.txt`. Checked and found fine: link order of the
`foundation` aggregate (higher layers first), static-initialization order (registries and the log live in one library each; the hooks are installed explicitly by `InitDebugFacilities`, not by static init),
sanitizer flavors (each `_asan`/`_tsan` library links the same flavor below it). Noted behavior: a program linking only `foundation_base` gets one-line asserts without traces (documented in the guide). Declined: none.
