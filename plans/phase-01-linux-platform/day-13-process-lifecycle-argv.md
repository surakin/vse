# Phase 1 / Day 13 — Process Lifecycle & Argv

**Phase:** [Phase 1 — Linux Platform Layer](../phase-01-linux-platform.md)
**Depends on:** Phase 0 complete (links libc normally — see `PLAN.md`
assumption #2).

**Resolved (previously flagged in Phase 0's notes):** this uses libc's
standard `main()` entry rather than a hand-rolled `_start`. Since Phase 0
already links libc normally, skipping libc's own CRT startup would risk
skipping initialization later libc calls implicitly depend on (TLS,
stdio buffering, etc.) for no benefit — argc/argv/envp arrive at `main()`
already parsed correctly per the System V ABI, no reason to re-parse the
raw stack ourselves.

**Scope amendment (2026-08-16):** engine code must not handle raw
`argc`/`argv` directly — it goes through an `os::CommandLine` wrapper
that also has to extend cleanly to future platforms (Windows/macOS/
Android/iOS/consoles, Phases 6-7 and 22-24), the same seam treatment Day 26 gives
the `Window`/input interface. See
`docs/superpowers/specs/2026-08-16-command-line-wrapper-design.md` for
the full design (class shape, parsing rules, per-platform sourcing via
`FromNative()`/`StashNativeCommandLine()`).

## Tasks

- [x] Confirm the engine's entry point is a standard `int main(int argc,
  char** argv)` (Linux/glibc also exposes `envp` via `extern char**
  environ` if needed later — not required for this day).
- [x] Implement `os::CommandLine` per the design spec:
  `engine/foundation/os/command_line.h` (class + portable
  `(Allocator&, int argc, char** argv)` parsing constructor,
  `ProgramName`/`ArgCount`/`Arg`/`HasFlag`/`TryGetOption`, plus
  `FlagCount`/`FlagName`/`FlagHasValue`/`FlagValue` enumeration added
  during implementation — needed so a caller that doesn't know flag
  names ahead of time, e.g. this day's own startup log, can walk every
  flag/option passed) and `engine/foundation/os/linux/command_line.cpp`
  (`StashNativeCommandLine` + `Detail_NativeCommandLineArgs`). TDD:
  `tests/foundation/command_line_test.cpp` written first against the
  not-yet-existing header (confirmed RED via a CMake configure failure
  — the implementation files were moved aside first), then the
  implementation brought back in for GREEN — 9 tests, all passing.
- [x] Wire `main.cpp`: call `os::StashNativeCommandLine(argc, argv)`,
  construct via `os::CommandLine::FromNative(allocator)`.
- [x] Log the program name, each positional arg, and each recognized
  flag/option via Day 8's formatter and Day 2's `os::Write`.
- [x] Build and run with varied inputs: zero extra args, several
  positional args, an arg containing a space (shell-quoted), a boolean
  `--flag`, and a `--key=value` option — confirm each is categorized
  (positional vs. flag vs. option) and logged correctly, including the
  space-containing arg not being split. Verified manually with
  `engine_main` — see commit for output.
- [x] Commit.

**Done:** `os::CommandLine::FromNative()` correctly parses the engine's
received command-line arguments — positional args (including one
containing a space), a boolean flag, and a key/value option — for zero,
one, and multiple arguments, and the engine logs them correctly.
