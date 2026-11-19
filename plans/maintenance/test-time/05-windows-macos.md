# Shorter test runs / Day 5: the PC and the Mac (the user's machines)

**Series:** [shorter test runs](../test-time.md)
**Depends on:** Days 1 to 4.

## Tasks

- [ ] **The PC** (Git Bash, `build-msvc`):
  - `scripts/test-phase.sh --start-day`, then `--affected`, then `--soak`;
  - the per-flavor build targets work with the MSVC tree's flavors (plain and asan; no tsan);
  - the parallel build flag reaches MSBuild or Ninja, whichever the preset uses;
  - record the before and after times of an `--affected` run.
- [ ] **The Mac** (`/bin/bash` 3.2, `build-macos`):
  - the same three runs;
  - the script uses no bash 4 feature: the script tests run there;
  - per the standing rule, unity trees only and plain flavor only.
- [ ] **The short forms' counts:** on each machine, check that no ordinary test is over the 30 s bar, and record any that is. A slower
  machine may need its own count: a setting, not a different test.
- [ ] **Standing rows:**
  - the `ran X of Y` lines on each machine;
  - `STATUS.md`;
  - `KNOWN-ISSUES.md`: remove the "not run on the PC and the Mac" entries this series added;
  - commit and push.

**Done when** the new script, the `soak` label and the per-flavor build run on the PC and the Mac, and their before and after times are
recorded.

## Notes
