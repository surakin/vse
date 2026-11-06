# Phase 6 / Day 100 — Minidump Reader & Dump Tool

**Phase:** [Phase 6 — Debugging Facilities](../phase-06-debugging-facilities.md)
**Depends on:** Day 99.

A reader for the dumps we write, used by the tests to verify them and by a dev tool that prints a readable report from
a `.dmp` — the same output a developer would get from a tool like `minidump-stackwalk`, for the crashing thread.

## Tasks

- [ ] `engine/foundation/debug/minidump_reader.h`: `MinidumpReader` over a memory-mapped file (or a caller's buffer), with checked accessors for the header, directory, each stream, strings, and the stack memory; every offset and size is validated against the file length before use; a damaged or malicious file returns an error and never reads out of bounds. Allowed to allocate (it is not part of the crash path), but follows the foundation rules (no STL).
- [ ] The reader accepts dumps from any platform: unknown stream types are skipped, both CodeView record kinds (PDB70 and the ELF build-id convention) and both CPU architectures are parsed, and the extra streams `MiniDumpWriteDump` adds (handle data, misc info, thread info, memory info) are ignored without error. Add a test with a hand-built dump in the shape Windows writes (extra streams, `MINIDUMP_THREAD_EX`-style records), and note in the Phase 8 file that the first real Windows dump becomes a committed fixture.
- [ ] Reader features: list streams; system info; modules (with build ids); the exception and the crashing thread's context; stack words; and `WalkStack(reader) -> StackTrace` that reproduces the Day 94 walk over the dump's saved stack bytes (frame-pointer chain inside the saved range) so a dump can be symbolized without the process.
- [ ] Dev tool `tools/minidump_dump/`: prints streams, the exception and the symbolized trace (function names via Day 95's reader on the module files named in the dump, `file:line` via `tools/symbolize`); `--raw` prints addresses only; exits non-zero on a damaged file. Same rules as the other dev tools (not linked into the engine).
- [ ] Fuzz-style robustness tests: flip every byte of a small valid dump one at a time and truncate it at every length (under ASan): the reader returns an error or valid data and never crashes or reads out of range; random garbage; a directory entry pointing past the end; a string without a terminator; a stream count of 2^32-1.
- [ ] Round-trip tests with the Day 99 writer: write a dump of a synthetic `CrashInfo` in-process (no crash needed), read it back, compare every field; write one from a real crashing child and check the symbolized trace through the dump contains the faulting function.
- [ ] External-reader check with `lldb` (installed): open a dump from a crashing child with `lldb -c <dump>` and confirm `thread list`, `register read`, `image list` (module names and build ids) and `bt` (the faulting function, with the module's symbols via `target modules add` if needed) agree with `tools/minidump_dump`. Make it an automated ctest that skips with a printed reason when `lldb` is missing (`lldb -b -c <dump> -o 'bt'` and a check of the output for the faulting function name), and record the exact command lines in the guide. `minidump-stackwalk` (`cargo install minidump-stackwalk`) is an optional second reader; if it was not tried, say so in `KNOWN-ISSUES.md`.
- [ ] **Documentation:** `///` comments; the guide gets "Reading a minidump" and the tool's usage; reference regenerated and committed; `docgen lint` at zero; `ctest -L docs` passes.
- [ ] Run the day's scoped tests (`--affected`, asan and tsan flavors) and quote the `ran X of Y tests` lines.
- [ ] **End-of-day review (CLAUDE.md):** `/code-review high` on the day's diff, with extra attention to integer overflow in offset arithmetic; record it here and in `STATUS.md`.
- [ ] Update `STATUS.md`; commit.

**Done:** the reader parses every dump the writer produces and rejects every damaged one without out-of-bounds reads (byte-flip and truncation tests under ASan); `tools/minidump_dump` prints a symbolized trace for a crashed child's dump; `lldb` opens a crashed child's dump in an automated test (or the test skips with a reason where `lldb` is missing).
