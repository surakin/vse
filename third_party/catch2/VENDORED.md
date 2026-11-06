# Vendored Catch2

Upstream: https://github.com/catchorg/Catch2
Pinned release: **v3.15.3** (tag `v3.15.3`)
License: `LICENSE.txt` (Boost Software License 1.0), unmodified.

Per `PLAN.md` assumption #16, this is a source vendor (not a submodule,
not CMake `FetchContent`) so builds stay fully offline/reproducible.

## Pruned from the upstream release tarball

Only what's needed to build `Catch2`/`Catch2WithMain` as a CMake
subproject and use `catch_discover_tests` is kept:

- `CMakeLists.txt`, `LICENSE.txt` (root, unmodified)
- `src/` (full, unmodified — the library itself)
- `CMake/CatchConfigOptions.cmake`, `CMake/CatchMiscFunctions.cmake`
  (included by the root/`src` `CMakeLists.txt`)
- `extras/Catch.cmake`, `extras/CatchAddTests.cmake` (the
  `catch_discover_tests()` CMake integration)

Dropped: `docs/`, `tests/`, `examples/`, `benchmarks/`, `fuzzing/`,
`data/`, `tools/`, packaging files (`CMakePresets.json`, `meson.build`,
`BUILD.bazel`, `MODULE.bazel`, `conanfile.py`), CI config, and the
coverage-only `CMake/Find{codecov,Gcov,Lcov}.cmake` — none of these are
needed to consume Catch2 as a subproject the way this repo does. No
source file under the kept paths was modified.

`extras/Catch.cmake` and `extras/CatchAddTests.cmake` carry a small engine patch (marked `[engine patch]`; Phase 5
Day 86): `catch_discover_tests` takes `PHASE_MAP`/`PHASE_ROOT`, labels each test `phaseN` from its source file via
`cmake/EnginePhaseMap.cmake`, and `LABELS` accepts several labels (every element up to the next upper-case property
name). Re-apply it when updating the pin.

To update the pin: repeat this pruning against a newer release tarball
and update the version above.
