# Phase 0 / Day 2 — OS Interaction Wrappers (libc-backed)

**Phase:** [Phase 0 — Foundation Layer](../phase-00-foundation.md)
**Depends on:** Day 1 (build skeleton).

libc is the OS-interaction boundary for this layer (`PLAN.md`
assumption #2, resolved) — these are thin, engine-named wrappers around
libc calls, not hand-rolled raw syscalls via inline assembly. The
wrapper layer still earns its keep as the seam later OS ports
(Windows/macOS) implement against with their own native calls.

**Note on `usize`:** the day files as originally written have Day 2's
`os.h` signatures use `usize`, but that typedef isn't defined until Day
3's `types.h` — and Day 3 in turn uses Day 2's `os::Write` for its test
output, so the two days can't both go first. Resolved by having `os.h`
use the standard `std::size_t` (`<cstddef>`) for now; Day 3 introduces
`usize` as the project's alias for everything else and, as part of
that, updates `os.h`/`os.cpp` to use `usize` instead of `std::size_t`
for consistency (a one-line signature edit, not a redesign).

**Note on portability (added later, alongside Day 9):** `os::MMap`/
`os::MUnmap`/`os::MProtect` as originally implemented here were a thin
1:1 pass-through of POSIX `mmap`'s full signature (address hint, `prot`,
`flags`, `fd`, `offset`) — but every actual call site (Days 4/5's
allocators) only ever wanted anonymous, read+write memory, and Windows'
`VirtualAlloc` has no equivalent shape for the unused parameters
(`fd`/`offset` especially). This was corrected: `MMap`/`MUnmap` were
replaced with `os::ReserveMemory(size)`/`os::ReleaseMemory(addr, size)`
— exactly what callers need, portable to both `mmap` and `VirtualAlloc`
without either side emulating the other — and unused `MProtect` was
dropped (YAGNI; a real page-protection API can be designed properly
when something actually needs it). `os.cpp` also moved to
`linux/os.cpp`, with `CMakeLists.txt` selecting the platform source
conditionally, so a later `windows/os.cpp` is additive, not a redesign.
`Exit`/`Write`/`Read`/`Open`/`Close` were left as-is — their libc/POSIX
shapes are already satisfied by Windows' CRT (`_write`/`_read`/`_open`/
`_close`, `ExitProcess`), so no equivalent problem exists there.

## Tasks

- [x] Create `/engine/foundation/os.h` declaring: `os::Exit(int code)`,
  `os::Write(int fd, const void* buf, std::size_t len)`, `os::Read(int
  fd, void* buf, std::size_t len)`, `os::Open(const char* path, int
  flags)`, `os::Close(int fd)`, `os::MMap(...)`, `os::MUnmap(...)`,
  `os::MProtect(...)`.
- [x] Implement each in `/engine/foundation/os.cpp` as a direct call
  through to the equivalent libc function (`::exit`/`::_exit`,
  `::write`, `::read`, `::open`, `::close`, `::mmap`, `::munmap`,
  `::mprotect`) — no inline assembly, no raw syscall numbers. Used
  `::exit` (not `::_exit`) so `os::Exit` runs normal libc cleanup,
  consistent with linking libc normally.
- [x] Write a small verification program in `main.cpp` calling
  `os::Write` to print `"OK\n"` to fd 1.
- [x] Build and run it, confirming `"OK\n"` appears on stdout.
- [x] Commit.

**Done:** the `os::Write` wrapper prints `"OK\n"` to fd 1, calling
through to libc's `write` with no libc involved beyond that call (i.e.
no `printf`/`iostream` doing the formatting).
