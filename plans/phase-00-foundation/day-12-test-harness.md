# Phase 0 / Day 12 — Foundation Test Harness

**Phase:** [Phase 0 — Foundation Layer](../phase-00-foundation.md)
**Depends on:** Days 1-11 (everything this harness runs).

No external test framework (GoogleTest/Catch2) — zero third-party
dependencies applies to test tooling too.

## Tasks

- [ ] Design a minimal test-registration macro (e.g. `TEST(name) {
  ... }`) that registers each test function into a static list at
  static-init time, in `/engine/foundation/test.h`.
- [ ] Implement the runner: iterate registered tests, run each, capture
  pass/fail, print a summary via Day 8's formatter and Day 2's
  `os::Write`, and set the process exit code to reflect overall
  pass/fail.
- [ ] Wire the runner into CMake's `ctest` as a custom test executable
  CTest invokes.
- [ ] Migrate Days 3-11's ad hoc verification code into this harness's
  `TEST` macro so they run as part of the same suite (no test logic
  left outside the harness).
- [ ] Run `ctest` and confirm every test — Days 3-11's, plus the
  harness's own self-test — reports green.
- [ ] Commit.

**Done:** `ctest` builds and runs the suite, all green.
