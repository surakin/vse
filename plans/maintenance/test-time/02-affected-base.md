# Shorter test runs / Day 2: the `--affected` base

**Series:** [shorter test runs](../test-time.md)
**Depends on:** Day 1.

## Why

`--affected` compares with the last commit that changed `CURRENT_PHASE`. That is right in the middle of a phase. But a long stretch of
maintenance work never moves `CURRENT_PHASE`, so the diff keeps growing until some earlier edit to the root `CMakeLists.txt` or a foundation
header makes every run cover everything (seen 2026-11-19).

## Tasks

- [x] **Record the day's start:**
  - `scripts/test-phase.sh --start-day [REF]` writes `REF` (default `HEAD`) to `.git/test-phase-base`, prints it and runs nothing;
  - the file is in `.git/`, so it is per checkout and never committed;
  - the Mac, the PC and a worktree each keep their own;
  - `git rev-parse --git-path` finds it in a worktree (relative paths made absolute by the script, so git 2.31 is not needed).
- [x] **The base `--affected` uses, in order:**
  1. `--since REF`;
  2. the recorded day start, if it is an ancestor of `HEAD` (after a rebase onto upstream that drops it, it is ignored with a message);
  3. the merge-base of `HEAD` and `@{upstream}`, if there is an upstream;
  4. the last commit that changed `CURRENT_PHASE` (today's rule).
  - The first line the script prints names the base and which rule chose it.
  - The working tree's changes and untracked files are always included, as today.
- [x] **A recorded day start that is older than the phase's start is not used** (the `CURRENT_PHASE` commit wins), so a stale file from an
  earlier phase cannot narrow a run.
- [x] **Tests** (`test_phase_script_*`, through `--diff-from-file` where git is not needed, and a scratch repository where it is):
  - each base rule chooses as listed;
  - a recorded day start that is not an ancestor is ignored with the message;
  - a recorded day start older than the phase's start loses to the `CURRENT_PHASE` commit;
  - `--since` overrides all of them;
  - the dry run (`--dry-run`) prints the base.
- [x] **The rules that run everything are unchanged:** cmake/, third_party/, a foundation header, and edited lines in a `CMakeLists.txt` or
  the maps. They now apply to the day's diff, not the phase's.
- [x] **CLAUDE.md** ("Tests run by phase"): one sentence that a day starts with `scripts/test-phase.sh --start-day`, and that the base is
  printed. `docs/guide/building-and-testing.md` gets the same.
- [x] **Standing rows:**
  - `--affected` (plain), with its `ran X of Y` line;
  - the script tests;
  - `/code-review high`;
  - `STATUS.md`, `KNOWN-ISSUES.md`;
  - commit and push.

**Done when** `--affected` on the unlit change, with the day start at `258cc983^`, selects only the phases of the renderer, asset_import,
scene and shaders (not "all"), and the base rules are tested.

## Notes

### What was built (2026-11-19)

- **`scripts/test-phase.sh`:**
  - `--start-day [REF]` writes the commit to `test-phase-base` in the checkout's own git directory and prints it with its subject;
  - `base_commit` chooses the base in the order above, and `--affected` prints `compares with <sha>: <rule>` first;
  - a record that is not an ancestor, or is older than the phase's start, is ignored with a message;
  - one addition from the review: when the upstream base is `HEAD` itself (everything pushed, no record), the script warns that the
    day's pushed commits are left out;
  - `--repo-root DIR` (for tests) runs git in another checkout, and every git call uses it;
  - `changed_files` and `only_additions` share the one base (before, `changed_files` computed its own).
- **CLAUDE.md** ("Tests run by phase"): a day starts with `--start-day`. The guide's "Test phases and scoped runs" describes the
  four rules and the measurement below.
- **Test:** `test_phase_script_base`, a scratch repository (in the build tree) with a phase commit, a side branch and an upstream. It
  checks every rule, the two ignored records, `--since`, a worktree's own record, the pushed-HEAD warning and a bad `--start-day` ref.
- **Not changed:** `scripts/tidy.sh --changed` keeps its own "since the phase started" rule; it is a different tool.

### The Done measurement

The unlit change (`258cc983`), checked out in a temporary worktree with the day start recorded at `258cc983^`:
- `--affected --dry-run` selects phases 3 4 5 17 18 22 27, 1,651 tests plain (4,430 in every flavor);
- the old base ran every phase: 10,928 tests in every flavor, 4,264 plain.
- Phase 3 is the docs checks, 4 and 5 are the RHI and the renderer, 17 and 18 are the scene, 22 is the importer, and 27 is the current
  phase.

### Runs

- With the day start recorded at Day 1's commit (`84c2c705`), `scripts/test-phase.sh --affected`: `ran 562 of 11462 tests (phases: 3 6 27;
  flavors: plain; display: excluded; crash tests: excluded) in 164 s`, with no failures. Day 1's run of the same kind was 4,264 tests in
  397 s.
- After the review's fixes: phase 6, 63 of 63; `ctest -L docs` 9 of 9; `scripts/format.sh --check` clean. No C++ changed, so no compiler
  builds beyond the run's.

### Post-implementation code review

`/code-review high`: 5 findings, all fixed.
1. The test's scratch repository took its branch from `init.defaultBranch` (`git init` then `checkout -b master`); it now sets
   `HEAD` with `symbolic-ref`.
2. Scratch commits ran the user's global hooks and templates; the test now passes an empty `core.hooksPath` and `init.templateDir`.
3. `rev-parse --path-format=absolute` needs git 2.31; on an older git the record vanished silently. The script now makes the
   `--git-path` result absolute itself.
4. With HEAD pushed and no record, the upstream base drops the day's pushed commits; the script now says so.
5. Nothing tested the record inside a worktree. A case now checks that it lands in the worktree's git directory and leaves the main
   checkout's base alone; pointing the record at the shared directory fails it (checked, then restored).
