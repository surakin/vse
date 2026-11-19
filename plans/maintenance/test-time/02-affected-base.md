# Shorter test runs / Day 2: the `--affected` base

**Series:** [shorter test runs](../test-time.md)
**Depends on:** Day 1.

## Why

`--affected` compares with the last commit that changed `CURRENT_PHASE`. That is right in the middle of a phase. But a long stretch of
maintenance work never moves `CURRENT_PHASE`, so the diff keeps growing until some earlier edit to the root `CMakeLists.txt` or a foundation
header makes every run cover everything (seen 2026-11-19).

## Tasks

- [ ] **Record the day's start:**
  - `scripts/test-phase.sh --start-day [REF]` writes `REF` (default `HEAD`) to `.git/test-phase-base`, prints it and runs nothing;
  - the file is in `.git/`, so it is per checkout and never committed;
  - the Mac, the PC and a worktree each keep their own;
  - `git rev-parse --git-path` finds it in a worktree.
- [ ] **The base `--affected` uses, in order:**
  1. `--since REF`;
  2. the recorded day start, if it is an ancestor of `HEAD` (after a rebase onto upstream that drops it, it is ignored with a message);
  3. the merge-base of `HEAD` and `@{upstream}`, if there is an upstream;
  4. the last commit that changed `CURRENT_PHASE` (today's rule).
  - The first line the script prints names the base and which rule chose it.
  - The working tree's changes and untracked files are always included, as today.
- [ ] **A recorded day start that is older than the phase's start is not used** (the `CURRENT_PHASE` commit wins), so a stale file from an
  earlier phase cannot narrow a run.
- [ ] **Tests** (`test_phase_script_*`, through `--diff-from-file` where git is not needed, and a scratch repository where it is):
  - each base rule chooses as listed;
  - a recorded day start that is not an ancestor is ignored with the message;
  - a recorded day start older than the phase's start loses to the `CURRENT_PHASE` commit;
  - `--since` overrides all of them;
  - the dry run (`--dry-run`) prints the base.
- [ ] **The rules that run everything are unchanged:** cmake/, third_party/, a foundation header, and edited lines in a `CMakeLists.txt` or
  the maps. They now apply to the day's diff, not the phase's.
- [ ] **CLAUDE.md** ("Tests run by phase"): one sentence that a day starts with `scripts/test-phase.sh --start-day`, and that the base is
  printed. `docs/guide/building-and-testing.md` gets the same.
- [ ] **Standing rows:**
  - `--affected` (plain), with its `ran X of Y` line;
  - the script tests;
  - `/code-review high`;
  - `STATUS.md`, `KNOWN-ISSUES.md`;
  - commit and push.

**Done when** `--affected` on the unlit change, with the day start at `258cc983^`, selects only the phases of the renderer, asset_import,
scene and shaders (not "all"), and the base rules are tested.

## Notes
