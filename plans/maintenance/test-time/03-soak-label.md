# Shorter test runs / Day 3: the `soak` label

**Series:** [shorter test runs](../test-time.md)
**Depends on:** Day 2.

## Tasks

- [ ] **Measure:** from a full `--all --flavor all` run's ctest output, list every test over 30 s in each flavor. Record the list (name, flavor,
  seconds) in the Notes. This list is the input to this day and to Day 4.
- [ ] **The label:**
  - `soak`, set with the existing labelling helper (`engine_label_tests(... SOAK)` or a `set_tests_properties` beside the test);
  - one place lists how a test gets it (`tests/phases.txt` keeps the phase; the label is not a phase);
  - `test_phase_labels` accepts it.
- [ ] **The script:**
  - `soak` is left out of `--current`, `--affected` and a single phase, like `crash`;
  - `--soak` includes it, and `--all` includes it by default;
  - the summary line gains `soak: included|excluded`, beside `crash tests:`;
  - `.last-full-run` is written only by a run with nothing left out, as today.
- [ ] **Label the benches and stress runs:** `audio_bench_large`, `physics_bench_large`, `script_stress_large`, and any other
  bench or stress test from the measured list that has no shorter form. A test whose length is its point is labelled whole.
- [ ] **A warning for new long tests:**
  - after a run, the script reads ctest's timings (`Testing/Temporary/CTestCostData.txt`, or `--output-junit`);
  - it prints every test without `soak` that took over 30 s;
  - it is a warning, not a failure: timing depends on the load;
  - the Notes and `docs/guide/building-and-testing.md` say what to do: shorten the test, or give it a short form and a soak form (Day 4).
- [ ] **Tests** (`test_phase_script_*`):
  - a soak-labelled fixture test is left out of `--affected` and included with `--soak` and `--all`;
  - the summary line says which;
  - the warning lists an unlabelled long fixture test (driven by a cost-data fixture file, not by a real slow test).
- [ ] **CLAUDE.md** ("Tests run by phase"): a phase's last day's `--all --flavor all` includes `soak`; a day that changes a module with soak
  tests runs `--affected --soak` before its review. Same in the guide.
- [ ] **Standing rows:**
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
