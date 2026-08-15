# Phase 1 / Day 16 — File I/O

**Phase:** [Phase 1 — Linux Platform Layer](../phase-01-linux-platform.md)
**Depends on:** Day 13 (process lifecycle & argv).

## Tasks

- [x] Extend Day 2's `os` layer with `os::Lseek` (libc `lseek`), on top
  of the existing `os::Open`/`os::Read`/`os::Write`/`os::Close`. Uses
  `SEEK_SET`/`SEEK_END` (ISO C `<cstdio>` constants, portable — not
  POSIX-specific) rather than raw integers.
- [x] Implement a "read whole file into an arena buffer" utility:
  `os::ReadFileToArena` (`engine/foundation/os/file_util.h`/`.cpp`) —
  open, `lseek` to end + back for size, allocate from an `ArenaAllocator`,
  read the full contents in. Portable (built purely from `os.h`'s
  primitives), so — unlike `os.h`/`os/linux/` — it's declared and
  defined once with no per-platform split, added to `FOUNDATION_SOURCES`
  unconditionally rather than `FOUNDATION_PLATFORM_SOURCES`.
- [x] Write a test that reads the engine's own executable file (via
  `argv[0]` or `/proc/self/exe`) and verifies the ELF magic number
  (`0x7F 'E' 'L' 'F'`) at the start of the buffer. Used `/proc/self/exe`
  (works identically whichever binary is running — `foundation_tests`
  here, `engine_main` in production — without depending on how the
  caller was invoked). TDD: `tests/foundation/file_io_test.cpp` written
  first against the undeclared `Lseek`/`ReadFileToArena`, confirmed RED
  via a compile failure, then the implementation added for GREEN — this
  also caught a real test-fixture bug (an 8MB arena was too small for
  the statically-linked-Catch2 `foundation_tests` binary itself, ~13.8MB;
  fixed by sizing the arena generously at 64MB rather than shrinking
  what's read).
- [x] Commit.

**Done:** reads its own executable's ELF header and verifies the magic
number.
