# Phase 0 / Day 9 — Logging & Assertions

**Phase:** [Phase 0 — Foundation Layer](../phase-00-foundation.md)
**Depends on:** Day 8 (string type & builder).

Designed against Unreal's log-spam problem from the start (`PLAN.md`
assumption #10): named channels/categories, independent per-channel
verbosity, per-user config overridable without recompiling.

## Tasks

- [ ] Define `LogChannel` and a static registry of named channels
  (e.g. `LogCore`, `LogRender`) in `/engine/foundation/log.h`, each
  carrying its own verbosity level (Error/Warn/Info/Debug/Trace) and
  enable flag, defaulting to Warning+ enabled.
- [ ] Implement the log call (macro or function) routing through Day
  8's formatter and Day 2's `os::Write`, gated by the channel's current
  verbosity/enable state.
- [ ] Implement a minimal hand-rolled `key=value` config file reader
  (via Day 2's `os::Open`/`os::Read`) that overrides per-channel
  verbosity/enable at startup — the format this phase's open questions
  flagged as the leading candidate, kept intentionally dumb.
- [ ] Implement the assert macro: on failure, format file/line/message
  via Day 8, write it via `os::Write`, then call Day 2's `os::Exit(1)`
  — not libc's `abort()`, to get a clean, controlled failure exit
  instead of `abort()`'s `SIGABRT`/core-dump behavior.
- [ ] Write a test that intentionally fails an assert and verifies both
  the printed message and the process exit code.
- [ ] Write a test that toggles a test channel's enable flag and
  verbosity via the config file and verifies what does/doesn't reach
  `os::Write`, for both a suppressed and an emitted message.
- [ ] Commit.

**Done:** an intentionally-failing assert prints the right message and
process exit code; toggling a test channel's enable flag and verbosity
via config changes what reaches `os::Write`, verified for both a
suppressed and an emitted message.
