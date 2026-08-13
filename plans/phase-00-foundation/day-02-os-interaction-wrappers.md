# Phase 0 / Day 2 — OS Interaction Wrappers (libc-backed)

**Phase:** [Phase 0 — Foundation Layer](../phase-00-foundation.md)
**Depends on:** Day 1 (build skeleton).

libc is the OS-interaction boundary for this layer (`PLAN.md`
assumption #2, resolved) — these are thin, engine-named wrappers around
libc calls, not hand-rolled raw syscalls via inline assembly. The
wrapper layer still earns its keep as the seam later OS ports
(Windows/macOS) implement against with their own native calls.

## Tasks

- [ ] Create `/engine/foundation/os.h` declaring: `os::Exit(int code)`,
  `os::Write(int fd, const void* buf, usize len)`, `os::Read(int fd,
  void* buf, usize len)`, `os::Open(const char* path, int flags)`,
  `os::Close(int fd)`, `os::MMap(...)`, `os::MUnmap(...)`,
  `os::MProtect(...)`.
- [ ] Implement each in `/engine/foundation/os.cpp` as a direct call
  through to the equivalent libc function (`::exit`/`::_exit`,
  `::write`, `::read`, `::open`, `::close`, `::mmap`, `::munmap`,
  `::mprotect`) — no inline assembly, no raw syscall numbers.
- [ ] Write a small verification program in `main.cpp` calling
  `os::Write` to print `"OK\n"` to fd 1.
- [ ] Build and run it, confirming `"OK\n"` appears on stdout.
- [ ] Commit.

**Done:** the `os::Write` wrapper prints `"OK\n"` to fd 1, calling
through to libc's `write` with no libc involved beyond that call (i.e.
no `printf`/`iostream` doing the formatting).
