# Shorter test runs / Day 3: the `soak` label

**Series:** [shorter test runs](../test-time.md)
**Depends on:** Day 2.

## Tasks

- [x] **Measure:** from a full `--all --flavor all` run's ctest output, list every test over 30 s in each flavor. Record the list (name, flavor,
  seconds) in the Notes. This list is the input to this day and to Day 4.
- [x] **The label:**
  - `soak`, set with the existing labelling helper (`engine_label_tests(... SOAK)` or a `set_tests_properties` beside the test);
  - one place lists how a test gets it (`tests/phases.txt` keeps the phase; the label is not a phase);
  - `test_phase_labels` accepts it.
- [x] **The script:**
  - `soak` is left out of `--current`, `--affected` and a single phase, like `crash`;
  - `--soak` includes it, and `--all` includes it by default;
  - the summary line gains `soak: included|excluded`, beside `crash tests:`;
  - `.last-full-run` is written only by a run with nothing left out, as today.
- [x] **Label the benches and stress runs:** `audio_bench_large`, `physics_bench_large`, `script_stress_large`, and any other
  bench or stress test from the measured list that has no shorter form. A test whose length is its point is labelled whole.
- [x] **A warning for new long tests:**
  - after a run, the script reads ctest's timings (`Testing/Temporary/CTestCostData.txt`, or `--output-junit`);
  - it prints every test without `soak` that took over 30 s;
  - it is a warning, not a failure: timing depends on the load;
  - the Notes and `docs/guide/building-and-testing.md` say what to do: shorten the test, or give it a short form and a soak form (Day 4).
- [x] **Tests** (`test_phase_script_*`):
  - a soak-labelled fixture test is left out of `--affected` and included with `--soak` and `--all`;
  - the summary line says which;
  - the warning lists an unlabelled long fixture test (driven by a cost-data fixture file, not by a real slow test).
- [x] **CLAUDE.md** ("Tests run by phase"): a phase's last day's `--all --flavor all` includes `soak`; a day that changes a module with soak
  tests runs `--affected --soak` before its review. Same in the guide.
- [x] **Standing rows:**
  - `--affected` (plain), with its `ran X of Y` line;
  - `--all --soak --dry-run`, whose count must equal today's total;
  - `/code-review high`;
  - `STATUS.md`, `KNOWN-ISSUES.md`;
  - commit and push.

**Done when:**
- the three benches and the other whole-soak tests carry `soak`;
- a scoped run leaves them out and says so;
- `--all` still counts every test;
- the long-test warning is tested.

## Notes

### Measured (the input to this day and Day 4)

From the unlit change's run (2026-11-19, `--affected --flavor all`, every phase, the three flavors at once, so these times include the load
of the others), the tests over 30 s:

| Seconds | Test |
|---|---|
| 449.7 | `audio_bench_large` |
| 276.6 | `physics_bench_large` |
| 261.5 | `script_stress_large` |
| 177.4 / 70.9 | Resets in a loop on a real device (plain / asan) |
| 99.5 / 88.4 | `engine_c_script_sample_tsan` / `_asan` |
| 91.4 / 76.8 | Device allocation failures at any point (plain / asan) |
| 78.6 / 64.0 | The pipeline cache is written at shutdown, reused, and ignored when damaged (plain / asan) |
| 75.7 | `test_phase_labels` |
| 64.1 / 53.1 | Repeated losses on a real device (plain / asan) |
| 61.0 / 60.4 | Hostile scripts for a minute (asan / tsan) |
| 59.4 / 56.4 | Vulkan devices are destroyed and recreated (asan / plain) |
| 53.9 / 42.9 | `script_demo_window_x11` / `_wayland` |
| 51.6 | `platform_neutral_check` |
| 43.1 | A pak with offsets above 4 GiB reads back [tsan] |
| 40.6 | HashGrid: 100,000 random operations [asan] |
| 40.0 | `engine_c_scene` |
| 36.8 | Script demo: the state hash at 1, 2 and 16 threads [tsan] |
| 36.6 | Spatial sync: the cost for 100,000 entities [tsan] |
| 36.2 / 34.7 | `shaders_check` / `shaders_portability` |
| 35.8 | Golden: every scene reproduces its committed hashes [tsan] |
| 32.6 / 32.0 | Losing the real device in the deferred pipeline (plain / asan) |
| 31.7 | `file_access_check` |

**Labelled `soak`:** only the three `*_large` runs, whose length is their point. Every other test on the list is long because of a count or a
scan, so it gets a short form (Day 4) rather than the label. A sanitized test under 60 s is not over its bar.

### What was built (2026-11-19)

- `engine_soak_tests(<name>...)` (`cmake/EngineTestPhases.cmake`) adds the label; the three benches' `CMakeLists.txt` call it.
- **`scripts/test-phase.sh`:**
  - `--soak` includes the soak tests; they are left out of every run but `--all`;
  - the summary line ends `; soak: included|excluded`;
  - after a run, a warning lists every test without the label over 30 s (60 s for a sanitizer test), from the run's own JUnit file
    (`--output-junit`, `<build>/Testing/test-phase-<pid>.xml`, removed after);
  - `--report-long FILE` prints the warning for a JUnit file (for tests).
- **Script tests:**
  - the label filters of every case gain `soak`, and `check.cmake` reads the new summary field;
  - `phase_without_soak` and `phase_with_soak` (phase 27);
  - `test_phase_script_long_warning` checks each line of the warning in order, against a JUnit fixture: a long plain test, a short one,
    a 45 s sanitizer test (under its bar), a 75 s one, a name with escaped characters, and `script_stress_large` (skipped where this build
    has it as soak);
  - `_long_quiet` (no warning for a file with no long test), and `_long_warning_bwk_awk` (the same under the one-true-awk, below).
- **Docs:** `CLAUDE.md` ("Tests run by phase": the label, `--affected --soak` on days that change such a module, the warning) and the
  guide ("Long tests: the `soak` label").

### Runs

- `scripts/test-phase.sh --affected` (every phase, since `cmake/` changed): `ran 4270 of 11466 tests (phases: all; flavors: plain;
  display: included; crash tests: excluded; soak: excluded) in 117 s`. The same kind of run took 397 s on Day 1.
  - Failures, all known: `engine_c_surface`, the Job-destroyed test, the voice-cost snapshot.
  - The warning listed 13 tests, Day 4's work: the reset, allocation-failure, pipeline-cache, device-loss, Vulkan recreate, lifecycle
    and bind-group tests, `test_phase_labels`, `platform_neutral_check`, `engine_c_scene`, and the two script-demo window tests.
- `--soak` (the day changed the three benches' CMake): `script_stress_large` 169.8 s, `physics_bench_large` 206.1 s,
  `audio_bench_large` 367.0 s; all pass.
- After the review's fixes:
  - phase 6: 68 of 68, and the warning shows end to end in a phase-6 run (`test_phase_labels`, 60.0 s);
  - `--all --flavor all --dry-run` counts 11,467 of 11,467 (the new awk test included), `soak: included`;
  - `ctest -L docs` 9 of 9; `scripts/format.sh --check` clean.

### Post-implementation code review

`/code-review high`: 3 findings, all fixed.
1. The soak names reached awk through `-v`, a multi-line value. The one-true-awk (macOS's) refuses that: `nawk` here prints "newline in
   string", and under `set -e` that ends the run after ctest, before the `ran X of Y` line. The names now go to awk in a file.
   - `test_phase_script_long_warning_bwk_awk` runs the warning under `nawk` where it is installed;
   - with the `-v` form put back, it fails (checked, then restored).
2. The JUnit file had a fixed path and was never removed, so a run that wrote none could report an earlier run's tests.
3. Two runs on one tree shared that path. Each run now writes its own file (by process id), and removes it.
