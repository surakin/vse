# Maintenance: double precision, Day 11 — Demos, benchmarks, docs and closing

**Series:** [double precision](../double-precision.md)

## Tasks

- [ ] The demos (`renderer_demo`, `physics_demo`, `model_viewer`, `render_features_demo`, `script_demo`, `ui_demo` where it draws a world) build and run; each gains a `--origin-offset` option (or a setting) that places the whole scene far from the origin, and its screenshot test compares the result with the near-origin picture.
- [ ] Benchmarks: `physics_bench`, `renderer_bench`, `ecs_bench` and the instancing benchmark before and after; the numbers in the day file and the guides.
- [ ] Tutorial and mental-model page (the last day writes them): "Big worlds: what is `f64`, what is `f32`, and why", with compiling samples.
- [ ] Windows and macOS: the days that check the shared changes there are the user's (a list of what to run, in the day file and `KNOWN-ISSUES.md`).
- [ ] The closing run: `scripts/test-phase.sh --all --flavor all`, `ctest -L docs`, the demos; `/code-review high --max-findings all` over the whole series' diff; the count and outcome recorded.
- [ ] `KNOWN-ISSUES.md`: the entry "Large world coordinates" is removed; `STATUS.md` and `PLAN.md` updated.
- [ ] **Runs and review (the series' rules):** scoped tests plain, asan and tsan as the day's code needs; docs gates; `/code-review high`; `STATUS.md` and `KNOWN-ISSUES.md`; commit and push.

**Done:** Everything that holds a world position is double precision where the spec says, the full suite passes in every flavor on Linux, and the Windows and macOS runs are listed for the user.

## Notes

- Written 2026-11-23.

## Post-implementation code review

(To be filled in when the day is done.)
