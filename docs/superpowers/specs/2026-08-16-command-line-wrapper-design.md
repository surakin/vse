# Command-Line Wrapper — Design Spec

**Date:** 2026-08-16
**Status:** Approved, not yet implemented

## Problem

Phase 1 Day 13 (`plans/phase-01-linux-platform/day-13-process-lifecycle-argv.md`)
originally scoped the engine's entry point to receive and log raw
`argc`/`char** argv` directly. The user flagged that engine code should
never touch `argc`/`argv` directly — it needs a `CommandLine` wrapper
class, and that wrapper needs to extend cleanly to other platforms later
(Phases 15-20), the same way `PLAN.md` assumption #11 already requires
of the `Window`/input interface Day 26 defines.

Raw `argc`/`argv` isn't actually portable: Linux/console-subsystem
Windows/macOS get it from a standard C `main()`, GUI-subsystem Windows
gets a single command-line string (`GetCommandLineW`), and Android gets
launch parameters from an `Intent`, not process `argv` at all. A wrapper
is needed both for engine-code ergonomics (no raw pointer/count pair)
and to isolate that sourcing divergence behind one interface.

## Decision: portable parser class + per-platform `FromNative()` sourcing

Two concerns are kept separate:

1. **Parsing** (splitting flags/options from positional args, storing
   them) is identical logic on every platform — implemented exactly
   once, not duplicated per platform the way `os.h`'s `Write`/`Read`/
   etc. are.
2. **Sourcing** (how a platform obtains its initial argument list at
   all) is genuinely platform-specific — implemented once per platform,
   matching `os.h`'s existing interface-header/per-platform-impl split.

```cpp
namespace engine_placeholder::os {

class CommandLine {
public:
    // Portable parser. Any platform whose entry point already has an
    // argc/argv-shaped list can call this directly; also used by tests
    // to construct a CommandLine from synthetic argv with no real
    // process launch involved.
    CommandLine(Allocator& allocator, int argc, char** argv);

    // Per-platform sourcing — implemented once per platform under
    // os/<platform>/command_line.cpp.
    static CommandLine FromNative(Allocator& allocator);

    StringView ProgramName() const;      // argv[0]
    usize ArgCount() const;              // positional args only
    StringView Arg(usize index) const;

    bool HasFlag(StringView name) const;                             // --name
    bool TryGetOption(StringView name, StringView& out_value) const; // --name=value

    // Enumeration by index (added during implementation): callers that
    // don't know flag names ahead of time — e.g. Day 13's startup log,
    // which must print whatever flags were actually passed — need to
    // walk every flag/option rather than look one up by name.
    usize FlagCount() const;
    StringView FlagName(usize index) const;
    bool FlagHasValue(usize index) const;
    StringView FlagValue(usize index) const;
};

// Called unconditionally by each platform's real entry point, right
// after it receives argc/argv (main() on Linux/console-subsystem
// Windows/macOS). No-op on platforms where FromNative() sources args
// another way instead (future Windows GUI subsystem via
// GetCommandLineW/CommandLineToArgvW, Android via Intent extras).
void StashNativeCommandLine(int argc, char** argv);

} // namespace engine_placeholder::os
```

Lives in `namespace engine_placeholder::os` (not a separate top-level
type) — same rationale as `Write`/`Read`/`Open`/`Close`: it wraps a
process-launch/OS-boundary concern, not a general-purpose foundation
utility like `String`/`Array`.

### File layout

- `engine/foundation/os/command_line.h` — class declaration + the
  portable `(Allocator&, int argc, char** argv)` constructor's
  implementation (parsing logic; one copy, no per-platform variant).
- `engine/foundation/os/linux/command_line.cpp` — `StashNativeCommandLine`
  (stores into file-static `argc`/`argv`) and `FromNative()` (reads that
  stash, `ENGINE_ASSERT`s it was actually set first, forwards into the
  portable constructor).

`main.cpp` becomes:

```cpp
int main(int argc, char** argv) {
    os::StashNativeCommandLine(argc, argv);
    os::CommandLine cmd_line = os::CommandLine::FromNative(allocator);
    // ... log program name / args / flags per Day 13 ...
}
```

Future platforms plug in without touching `CommandLine` itself: a
GUI-subsystem Windows `FromNative()` would call
`GetCommandLineW`/`CommandLineToArgvW` itself and ignore the stash;
Android's launch shim would build an argument list from `Intent` extras
and either call the portable `(argc, argv)` constructor directly or
implement its own `FromNative()`. This mirrors the Day 26 `Window`/input
seam note: the interface stays generic so later backends are additive,
never a redesign.

## Parsing rules

- A token starting with `--` and longer than 2 characters is a
  flag/option: split on the *first* `=`. `--key=value` → option with a
  value; `--key` alone (no `=`) → boolean flag.
- Everything else — including `argv[0]`, single-dash tokens (`-v`), and
  a bare `--` — is **not** treated as flag syntax. `argv[0]` becomes
  `ProgramName()` and is not counted in `ArgCount()`; single-dash/bare-`--`
  tokens land as ordinary positional args. Short-flag syntax (`-v`) and
  a `--` positional-args terminator are explicitly out of scope for this
  pass — nothing in the engine needs them yet, and adding them later is
  additive, not a redesign.
- Duplicate flags: last occurrence wins.

## Storage

- Positional args: `Array<String<Allocator>, Allocator>`.
- Flags/options: a small `Array<FlagEntry, Allocator>` (`{ name,
  has_value, value }`), looked up by linear scan rather than
  `HashMap<StringView, StringView, Allocator>`. `hash_map.h` has no
  `Hash<StringView>` specialization yet (its own comment defers that to
  when it's actually needed) — command lines are realistically a
  handful of tokens, so a linear scan avoids adding that dependency for
  no measurable benefit.

## Day 13 scope change

Day 13's task list (`plans/phase-01-linux-platform/day-13-process-lifecycle-argv.md`)
is updated to implement this class rather than logging raw `argv`
directly. See that file for the current task breakdown.
