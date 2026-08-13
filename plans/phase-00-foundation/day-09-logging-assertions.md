# Phase 0 / Day 9 — Logging & Assertions

**Phase:** [Phase 0 — Foundation Layer](../phase-00-foundation.md)
**Depends on:** Day 8 (string type & builder).

Designed against Unreal's log-spam problem from the start (`PLAN.md`
assumption #10): named channels/categories, independent per-channel
verbosity, per-user config overridable without recompiling.

## Tasks

- [x] Define `LogChannel` and a static registry of named channels
  (e.g. `LogCore`, `LogRender`) in `/engine/foundation/log.h`, each
  carrying its own verbosity level (Error/Warn/Info/Debug/Trace) and
  enable flag, defaulting to Warning+ enabled. Registry is an intrusive
  singly-linked list (each `LogChannel` links itself in at construction
  — no `HashMap`/allocator needed for something this small and
  needed-before-any-arena-exists).
- [x] Implement the log call (`Log(channel, level, message)`, a
  function, not a macro — no `__FILE__`/`__LINE__` capture needed for
  plain log calls, unlike the assert) routing through a fixed 512-byte
  stack buffer (not Day 8's `StringBuilder`, deliberately — logging
  shouldn't need an allocator/arena to exist first) and Day 2's
  `os::Write`, gated by the channel's current verbosity/enable state
  (checked *before* any formatting work happens).
- [x] Implement a minimal hand-rolled `key=value` config file reader
  (via Day 2's `os::Open`/`os::Read`) that overrides per-channel
  verbosity/enable at startup — `ChannelName.verbosity=Level` /
  `ChannelName.enabled=true|false` per line, unknown
  channels/attributes/values silently skipped. Extended Day 8's
  `StringView` with `IndexOf(char)`/`Find(StringView)`/`Contains`
  (additive, needed for line/key splitting and the test's output
  assertions) and Day 2's `os::Open` with an optional `mode` parameter
  (needed for `O_CREAT`, default value keeps existing call sites
  unchanged).
- [x] Implement the assert macro: on failure, format file/line/message
  into the same fixed-buffer scheme as `Log`, write it, then call
  Day 2's `os::Exit(1)` — not libc's `abort()`, to get a clean,
  controlled failure exit instead of `abort()`'s `SIGABRT`/core-dump
  behavior.
- [x] Write a test that intentionally fails an assert and verifies both
  the printed message and the process exit code. **Design note:** an
  assert failure calls `os::Exit`, which would kill the whole test
  process if triggered directly — rather than adding subprocess
  machinery (fork/pipe/dup2) to `os.h` just for this, `Log`/`AssertFail`
  route their output/exit through swappable function-pointer sinks
  (`SetWriteSinkForTesting`/`SetExitSinkForTesting`, defaulting to the
  real `os::Write`/`os::Exit`). The test installs a capturing write sink
  and an exit sink that `longjmp`s back to a `setjmp` in the test
  instead of actually exiting — fully in-process, no new OS-level
  primitives needed.
- [x] Write a test that toggles a test channel's enable flag and
  verbosity via the config file and verifies what does/doesn't reach
  the write sink, for both a suppressed and an emitted message — plus a
  default-state test confirming Error emits and Info is suppressed on
  `LogCore` before any config is loaded (the "Warning+ enabled" default
  from `PLAN.md` assumption #10).
- [x] Commit.

**Done:** an intentionally-failing assert prints the right message and
process exit code; toggling a test channel's enable flag and verbosity
via config changes what reaches the write sink, verified for both a
suppressed and an emitted message. Verified: `engine_main` runs all
tests through Day 9, exit code 0; also clean under a one-off
`-fsanitize=address,undefined` build.
