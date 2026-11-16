# Phase 25: closing runs on the Windows PC and the Mac

Written 2026-11-17. The Linux closing is done (Day 401, `71a99fb5`). These runs are owed "no later than the phase's close" (CLAUDE.md, "Three machines") and are listed in `KNOWN-ISSUES.md` ("Phase 25 closing", item 1). Record each machine's final `ran X of Y` line in the Day 401 file, "Outcome", and remove the entry from `KNOWN-ISSUES.md` when both are done.

Files changed since the Day 398/399 runs and not built on either machine (Day 401 and Day 400): `engine/physics/{sync,joint_sync,character,query}.cpp`, `engine/physics/character.h`, `engine/physics/input_log.h` (comment only), `engine/spatial/{pair_tracker.cpp,pair_tracker.h,spatial_set.h}`, `engine/physics/{step.cpp,world.h}` (step timers), `tests/phases.txt`, `game/physics_bench/*`, `game/CMakeLists.txt`, `tests/physics/{stress_test,perf_smoke_test,character_test,sync_test,events_test,query_test}.cpp`, `tests/spatial/pair_test.cpp`, `tests/engine_library/{physics_fuzz.c,CMakeLists.txt}`, `tools/{module_deps_check/modules.txt,std_usage_check/allowlist.txt}`.

## Both machines

1. `git pull`, then a clean configure and build (warnings are errors): plain, and unity off (`-DENGINE_UNITY_BUILD=OFF`).
2. **Hashes must not move.** The golden hashes (`tests/physics/golden_test.cpp`, the 600-step scene, the C sample's checksum `0x532bfea0`) are unchanged by Days 400-401; any difference from the committed values is a finding, not a baseline update.
3. `ctest -L docs`.
4. The demos the phase ships run (`engine_c_physics_sample` through ctest; the `physics_sample` program prints the checksum).
5. Quote the final line of each run in the Day 401 file.

## Windows PC (plain and asan; no tsan on Windows)

```bash
scripts/test-phase.sh --all --flavor all
```

(Git Bash; `--flavor all` runs plain and asan there.) Also build `windows-clang-cl` plain and run `scripts/test-phase.sh --affected` in it.

Things to look at on this machine:
- The new Release-only ctest `physics_bench_large` registers only with `CMAKE_BUILD_TYPE` Release or RelWithDebInfo; the Windows presets build multi-config or Debug, so it may not register. If it does not, run `physics_bench --scene large --threads 1` from a Release build once and record the time (Linux: 194 s) and `large: ok`.
- `physics_bench` should print the same `large piled` line shape; note the ms per step of the 50,000-body pile (Linux Release, one thread: 719 ms) and the long run's time (Linux: about 25 s per million steps).
- The five Day 401 review fixes have tests: `Character: a ceiling ends the rise of a jump`, `Character: a skin width too small...`, `Sync: a changed group or mask reaches the body`, `Joint component: a body lost while its entity lives...`, `ShapeCast: a distance beyond the world...`.

## Mac (plain; no sanitizer runs for now)

```bash
scripts/test-phase.sh --all --flavor plain
```

(the system's bash 3.2; the console must be unlocked for the window tests.) Also, because Day 401 changed shared code: build `macos-clang-arm64` (compile-only) in both unity modes, and `scripts/format.sh --check` with MacPorts' `clang-format` 23.x.

## If something fails

Record it in `KNOWN-ISSUES.md` with the machine, the test name and the observed output (no guess at causes), and a flaky test with its phase label. Known Linux observations that are not physics and may show up elsewhere: `engine_c_surface` fails in a Release tree on Linux (not investigated); the three `engine_c_import_shared*` tests need the importer.
