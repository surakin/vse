# Container audit / Day 13: The gate, the after numbers, the closing runs and the series review

**Series:** [the container audit fixes](../container-audit.md)
**Depends on:** Days 1-12. If Day 12 has not run on both machines, this day closes on Linux and lists what waits in `KNOWN-ISSUES.md`.

## Tasks

- [ ] **The gate** `container_use_check` (`tools/container_use_check`, ctest `container_use_check`, same shape as the other gates):
  - it fails on `.Remove(0)` and on the FNV constants (`1099511628211`, `0x100000001b3`, `14695981039346656037`, `0xcbf29ce484222325`) outside the foundation hash;
  - it has an allow list (`path|reason`; a stale entry fails), with `tools/shader_mint` and anything Day 4 kept;
  - planted self-tests;
  - a line in `tests/phases.txt`, plus `docs/guide/building-and-testing.md` and the `CLAUDE.md` gate list if the series adds a rule there. Ask the user before adding a `CLAUDE.md` rule.
- [ ] **The after numbers:**
  - every Day 1 benchmark is re-run in the same conditions, and the table of before, after and growth goes here and in the audit file's summary;
  - a path still superlinear is listed in `KNOWN-ISSUES.md` with its number;
  - regression bounds (a generous multiple of the after number) go into the benches' `check` modes where a bench has one, `script_bench_check` style.
- [ ] **The audit checklist:** every P1, P2 and P3 item of [the audit](../container-audit-2026-11-19.md) is marked fixed (with its day), declined (with the reason and
  the number), or moved to `KNOWN-ISSUES.md`.
- [ ] **Closing runs:**
  - `scripts/test-phase.sh --all --flavor all`;
  - `ctest -L docs`;
  - the Release benches' checks (`script_bench_check`, `physics_bench_large`, `audio_bench_large`);
  - the nojit tree;
  - `-DENGINE_BUILD_SHARED=ON` and `-DENGINE_LIBRARY_WITH_IMPORTER=OFF`.

  Record each final line here. The Windows and Mac closing runs are their own (Day 12), or listed as waiting.
- [ ] **The series review:** `/code-review high --max-findings all` over the whole series' diff, from the commit before Day 1 to the working tree. Verify, fix or decline each
  finding, and record the count and outcome below.
- [ ] **Docs:** the "Choosing a container" table in `docs/guide/foundation-types.md` is final, with the measured costs from this day. It holds facts and numbers in
  reference form, since this is not a phase.
- [ ] `STATUS.md` (the series done), `KNOWN-ISSUES.md`, `PLAN.md` progress log; commit and push.

## Runs

## After numbers

## Post-implementation code review

**Done:**
- The gate passes and catches its planted cases.
- Every audit item is fixed, declined with a number, or recorded.
- The full suite passes in every flavor on Linux.
- The uncapped series review is recorded.
- The before/after table is published in this file and in the audit.
