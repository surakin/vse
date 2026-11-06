# Phase 2 / Day 35 — Stress & Throughput Test Suite

**Phase:** [Phase 2 — Job System & Coroutines](../phase-02-job-system.md)
**Depends on:** Days 27-34 (the whole job system) and Phase 0 Day 15
(test harness).

## Tasks

- [x] Randomized dependency-graph stress test: thousands of jobs in
  random fan-out/join shapes built from `Counter` (seeded PRNG; the seed
  is printed and a failing seed can be replayed), each job bumping its
  own execution counter. Asserts exactly-once execution per job, no
  deadlock (a watchdog timeout fails the test with a diagnostic instead
  of hanging `ctest`), and frame/`FramePool` stats balanced at the end.
  Run under `jobs_tests_tsan`, and repeat enough times (e.g. 200 seeds in
  the normal build, fewer under TSan) to be meaningful.
- [x] Work-stealing correctness+throughput test: submit heavily skewed
  load (every job spawned from one participant's deque). **Deterministic
  assertions:** per-participant executed-job counters show every
  participant other than the producer executed work, and total executed
  equals total submitted. **Timing:** CPU-bound jobs of fixed work
  (≥ ~50µs each so scheduling overhead doesn't dominate), best-of-5 wall
  time at `N = 1, 2, 4, default` (only configurations ≤ the allowed
  logical CPU count), logged as a table of speedup vs. `N = 1`.
- [x] Define the speedup assertion so it is not flaky: assert speedup at
  `N` ≥ `0.5 × min(N, physical-parallelism available)` only when the
  allowed logical CPU count is ≥ 4 and the build is not a sanitizer
  build; otherwise log the table and skip the assertion (with `SKIP`, so
  ctest shows it was skipped rather than silently passed).
- [x] Nested `co_await` ordering test: a chain of 5 dependent async
  steps (each step's job reads the previous step's output slot and may
  itself fan out and join), run 10,000 times, asserting strict order
  every time.
- [x] Revisit `FramePool` size classes with real data: log the observed
  frame-size histogram from these tests and either confirm the Day 27
  classes or adjust them (record the numbers in the day file); fallback
  count must be zero in these runs.
- [x] Register everything with the harness (normal `ctest`; the heavy
  TSan variants under the `tsan` label), and run `ctest` plus
  `ctest -L tsan`; all green.
- [x] Commit.

**Done:** `ctest` (and the `tsan`-labelled set) is green for the
dependency-graph stress, skewed-load stealing, and 10,000-run nested
`co_await` ordering tests; the stealing test proves other participants
executed stolen work, and the logged speedup table shows scaling with
participant count (asserted on machines with ≥ 4 allowed CPUs outside
sanitizer builds).

## Post-implementation code review (2026-11-03, `/code-review high`)

Fixed: the watchdog now covers every wait (graphs, skewed load,
pipelines, and through them the throughput runs) via `HelpOrAbort`, which
prints a diagnostic with the replay seed and aborts that test case
(the first version's timeout path returned while jobs still referenced the
caller's stack — a use-after-return); the skewed-load test only runs
configurations with N <= allowed CPUs (oversubscribed pins make "every
participant ran" scheduling-dependent) and SKIPs below 2 CPUs; the
throughput test SKIPs immediately under sanitizers (no pointless 5-rep
runs) and re-measures up to 3 rounds before declaring the 0.5×N speedup
bound failed (noisy shared machine); the graph generator now draws
per-seed max fan-out (1-6), max depth (2-41) and node cap (20-4000), so
shapes range from long chains to bushy trees (the first version hit the
4000-node cap on almost every seed).
Declined: asserting speedup at N = all logical CPUs (logged only — SMT
siblings make 0.5×N unreliable); SKIP when < 4 allowed CPUs as the plan
says. Known pre-existing flake: `X11Window maps event state bits to
engine modifiers` can fail under `ctest -j8` when the 32 cores are
saturated by the sanitizer jobs tests (passes alone and at `-j2`); it is
a timing issue in the Xvfb test, not the job system.
Frame-size histogram (FramePool size-class check): across the dependency
graphs (1.5M frames), the skewed-load run and 400k co_await-chain frames,
EVERY coroutine frame fit the two smallest classes — NodeJob frames land
in the ≤240 B class (256 B block), all other jobs in ≤112 B (128 B
block); nothing needed the 512/1024/2048 classes and the fallback count
was 0 in every run. Decision: keep the Day 27 classes unchanged (the
unused classes cost only reserved address space); revisit when ECS
(Phase 12) / asset streaming (Phase 15) bring larger frames.
Numbers (this 32-logical-CPU box, plain build, 3000 × ~60 µs jobs, best
of 5): N=1 0.163 s; N=2 0.083 s (1.96×); N=4 0.042 s (3.87×); N=32
0.0069 s (23.6×). Deterministic stealing assertions (every other
participant executed work, steals > 0, resumes == jobs + 1) hold at N=2
and N=4. Verified: 200 graph seeds ×2 configs plain (1.5M jobs; 60/20
seeds under ASan/TSan), extra seed bases 5000/9000, 10,000 nested
co_await runs per configuration, clean under ASan/TSan.
