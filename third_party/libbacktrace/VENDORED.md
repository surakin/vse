# Vendored libbacktrace

Upstream: https://github.com/ianlancetaylor/libbacktrace
Pinned commit: **`0b9b49cf4a2c9229fc052d6716e1528b2f23e91a`** (2026-09-03; the project makes no releases)
Downloaded as the GitHub archive of that commit, SHA-256 `fabcb6868b3d09734d4c537637dacac0bf8c9aa28e07486de5c6ce2eb749d413`.
License: `LICENSE` (BSD-3-Clause, unmodified).

Per `PLAN.md` assumption #20 this is a source vendor (not a submodule, not `FetchContent`) so builds stay offline and
reproducible. It provides stack unwinding (through the compiler's unwinder, no frame pointers needed), function names, and
file and line from DWARF, for ELF (Linux, Android), Mach-O (macOS, iOS) and PE/COFF (Windows). Its resolving calls are
documented async-signal-safe once the first call has been made (the first call walks the loaded objects with
`dl_iterate_phdr`); measured on Phase 7 Day 91 at zero `malloc` calls inside a signal handler. It reads **DWARF only**, not
PDB: Windows resolves through DbgHelp instead and does not build this library (Phase 13 Day 154).

## Kept from the upstream archive

`alloc.c atomic.c backtrace.c dwarf.c elf.c fileline.c macho.c mmap.c mmapio.c nounwind.c pecoff.c posix.c print.c read.c
simple.c sort.c state.c unknown.c`, the headers `backtrace.h filenames.h internal.h`, the template
`backtrace-supported.h.in`, `LICENSE` and `README.md`. All unmodified.

Dropped: `xcoff.c` (AIX), every test program (`btest.c`, `stest.c`, `ztest.c`, ...), the autotools machinery (`configure*`,
`Makefile*`, `libtool`, `config.h.in` of autoconf, ...), and the scripts and data the tests use.

## Added by this repository

- `CMakeLists.txt`: replaces autotools. The feature checks (`dl_iterate_phdr`, `_Unwind_GetIPInfo`, the `__atomic` and `__sync`
  builtins, `readlink`, `lstat`, `fcntl`, `strnlen`, `getpagesize`) fill the `config.h` that `config.h.in` (ours, with only
  the macros the sources test) turns into a header, and `backtrace-supported.h` is made from upstream's template. Only the
  ELF/Linux configuration exists: the Linux-only `FATAL_ERROR` names Phases 8 and 9 for the Windows and macOS branches
  (`pecoff.c` + `alloc.c` + `read.c`; `macho.c`).
- `config.h.in` (see above).

No upstream source file was edited. There is no `[engine patch]`.

## Build

One static library `backtrace`, plain flags (no sanitizer, no `-Werror`, `UNITY_BUILD OFF` because the files share static
helper names), linked into the plain, ASan and TSan flavors of `foundation` alike (an instrumented program calling the plain
library worked in all three on Day 91). libbacktrace decompresses compressed DWARF sections itself (zlib, zstd, lzma), so it
needs no system library.

## Updating the pin

Download the archive of the new commit into an empty directory, copy the kept files over the old ones, re-check
`config.h.in` against the macros the new sources test (`grep -ho 'HAVE_[A-Z_0-9]*' *.c`), rebuild, run the Phase 6 tests,
and update the commit and checksum above.
