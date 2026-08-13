# Phase 1 / Day 21 — File I/O

**Phase:** [Phase 1 — Linux Platform Layer](../phase-01-linux-platform.md)
**Depends on:** Day 13 (process lifecycle).

## Tasks

- [ ] Extend Day 2's `os` layer with `os::Lseek` (libc `lseek`), on top
  of the existing `os::Open`/`os::Read`/`os::Write`/`os::Close`.
- [ ] Implement a "read whole file into an arena buffer" utility: open,
  determine size (`lseek` to end + back, or `fstat`), allocate from a
  Day 4 arena, read the full contents in.
- [ ] Write a test that reads the engine's own executable file (via
  `argv[0]` or `/proc/self/exe`) and verifies the ELF magic number
  (`0x7F 'E' 'L' 'F'`) at the start of the buffer.
- [ ] Commit.

**Done:** reads its own executable's ELF header and verifies the magic
number.
