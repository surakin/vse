# Phase 12 / Day 145 — One Library per Layer

**Phase:** [Phase 12 — Module Layering](../phase-12-module-layering.md)
**Depends on:** Day 144 (no cycle left).

Split `foundation` into its layers now that nothing points upward, and prove the split by linking layer tests against their own layer only.

## Tasks

- [ ] **Libraries** per the spec: `foundation_base` (layers 0-5 and their `os/<platform>/` backend), `foundation_io` (layer 6 and `io/<platform>/`),
      `foundation_debug` (layer 7, `os/<platform>/debug/`, the only library that links libbacktrace and sentry-native), `foundation_settings` (layer 8); each links
      only the ones below it; `platform` now links `foundation_debug` (crash state) instead of `foundation`; `foundation` becomes an `INTERFACE` library linking
      all four, so every existing consumer builds unchanged.
- [ ] **Flavors:** each library has the `_asan` and `_tsan` variants the old `foundation_asan`/`foundation_tsan` had, with the same flags; `foundation_asan` and
      `foundation_tsan` become aggregates the same way.
- [ ] **Prove the layering by linking:** `tests/types` and the core `tests/foundation` files link `foundation_base` only; `tests/io` links `foundation_io`;
      `tests/debug` links `foundation_debug`; `tests/settings` links `foundation_settings`; the window tests link `platform`. A test that turns out to need a higher
      layer is either moved to that layer's test directory or links the higher library with a one-line reason in its `CMakeLists.txt`; record each.
- [ ] **`module_deps_check` link rules** updated for the new targets (a library linking a higher one fails); a planted fixture proves it.
- [ ] **Both unity modes** build (`ENGINE_UNITY_BUILD` on and off); unity batches now form per library, so re-check the conflicts the unity rule covers
      (CLAUDE.md, "Unity builds") and fix any new one with the macro pairs first.
- [ ] **Measure:** full and incremental build times (touching a `string/` header, an `io/` source, a `debug/` source) before and after, recorded here.
- [ ] **Documentation:** `docs/guide/building-and-testing.md` "Libraries and layers" (which library to link for what); `ARCHITECTURE.md` section 1 names the
      libraries; `STATUS.md` build notes.
- [ ] **Runs (Linux):** `scripts/test-phase.sh --affected` (CMake changed: everything) plus asan and tsan, `--crashes`; quote the lines.
- [ ] **End-of-day review:** `/code-review high`. Extra scrutiny: static-initialization order across the new libraries (registries, the log, the crash handler),
      symbols that resolved only because of link order in the old single library, the sanitizer variants linking the right flavor of every layer.
- [ ] Update `STATUS.md`, `KNOWN-ISSUES.md`; commit and push.

**Done:** the foundation builds as four layered libraries plus `platform`, each layer's tests link only that layer and those below, both unity modes and every
flavor pass, and the build times are recorded.
