# Phase 16 / Day 211 — `mem` Operations and the `cstr` Module

**Phase:** [Phase 16 — Standard-Library Wrappers](../phase-16-std-wrappers.md)
**Depends on:** Day 208.

Wrap the C library's memory and string functions below the log layer: raw memory operations in `foundation/mem` (layer 2) and a new layer-2 module `foundation/cstr` for NUL-terminated strings, formatting and number parsing. Needs to sit below `log` (layer 3), which formats.

## Tasks

- [ ] `mem/mem_ops.h`: `MemCopy`, `MemMove`, `MemSet`, `MemCompare`, `MemFind` (`memchr`), `Allocate`/`Free`/`Reallocate` for the C heap (the heap allocator's backing), `AlignedAllocate`/`AlignedFree`; null with zero size allowed, null with non-zero size asserts (documented `Aborts:`).
- [ ] `cstr/cstr.h`: `CStrLength`, `CStrCompare`, `CStrCompareN`, `CStrFind` (`strstr`), `CStrFindChar`, `CStrCopy`/`CStrAppend` (bounded, always terminated, returning truncation), wide forms (`WStrLength`, `WStrCompare`, `WMemCopy`); `cstr/ctype.h`: `IsDigit`, `IsAlpha`, `IsSpace`, `ToLower`, `ToUpper` for ASCII.
- [ ] `cstr/format.h`: `FormatTo(char* buffer, usize capacity, const char* fmt, ...)`, `VFormatTo(..., va_list)`, wide `WFormatTo`; returns the length the text would have (as `snprintf`); attribute-checked format strings (`ENGINE_PRINTF_LIKE`); `cstr/parse.h`: `ParseI64`, `ParseU64`, `ParseF64` returning a status (the callers of `atol`/`strtoul` get an error instead of 0).
- [ ] Console formatting stays out: printing is `os::Print` (Day 214).
- [ ] Tests in `tests/cstr/` and `tests/mem/` (phase 16): every function against the C library as oracle over generated inputs (empty, 1 byte, page-crossing, overlapping for `MemMove`, embedded NUL), formatting against `snprintf` for each conversion the tree uses (list from the Day 207 inventory), truncation at every capacity 0..N, number parsing edge cases (overflow, sign, leading space, no digits).
- [ ] Docs: `///` on all headers, `docs/guide/foundation-cstr.md`, modules.txt, `ARCHITECTURE.md`, `module_deps_check` map (cstr: layers 0-1).
- [ ] **Day review (CLAUDE.md, required):** scoped tests (`scripts/test-phase.sh --affected`, plain, then asan and tsan where the day touched concurrency or memory code), then `/code-review high` on the day's diff; verify, fix or decline each finding with a reason and record the count and outcome under "Post-implementation code review"; `STATUS.md` and `KNOWN-ISSUES.md` updated; commit and push.

**Done:** both modules build, are tested against the C library, documented, and `module_deps_check` and `docgen lint` pass.
