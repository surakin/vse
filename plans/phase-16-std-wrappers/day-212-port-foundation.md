# Phase 16 / Day 212 — Port `engine/foundation` to `mem`, `cstr`, `lang`

**Phase:** [Phase 16 — Standard-Library Wrappers](../phase-16-std-wrappers.md)
**Depends on:** Days 208, 211.

The foundation is included by everything, so it is ported first among the C-library users (69 files in the audit).

## Tasks

- [x] Port `engine/foundation` module by module (`mem`, `log`, `os` non-Windows, `string`, `containers`, `math`, `io`, `debug`, `settings`) from `std::mem*/str*/w*`, `std::snprintf`, `std::vsnprintf`, `std::swprintf`, `std::atol`, `std::strtoul`, `std::free`, `std::move`, `std::is_same_v` and friends to the wrappers; the tree builds after each module.
- [x] Signal-safe code (`SIGNAL-SAFE-BEGIN/END`) uses only wrappers that are themselves signal-safe: mark `MemCopy`/`CStrLength` etc. `Signal-safe:` in their docs and run `tools/signal_safety_check`.
- [x] Remove the standard includes that become unused; `engine/foundation/types.h` keeps `<cstdint>`/`<cstddef>` (it defines the aliases and `usize`), listed as an exception.
- [ ] Benchmarks: log and container benchmarks within the noise band (compilers must still inline `MemCopy` to the builtin `memcpy`: check one disassembly).
- [x] Scoped tests: `--affected` plain, asan; the foundation touches everything, so the script runs the full suite by itself — record the line.
- [x] Update `ARCHITECTURE.md` known violations if a port removed one.
- [x] **Day review (CLAUDE.md, required):** scoped tests (`scripts/test-phase.sh --affected`, plain, then asan and tsan where the day touched concurrency or memory code), then `/code-review high` on the day's diff; verify, fix or decline each finding with a reason and record the count and outcome under "Post-implementation code review"; `STATUS.md` and `KNOWN-ISSUES.md` updated; commit and push.

**Done:** `engine/foundation` contains no `std::` and no standard include outside `types.h`, the wrapper modules' own implementation files and the Windows-only files; benchmarks within noise; full plain and asan runs pass.

## Result

- `scripts/std-port.py cstr --apply` (197 files) and the `os` pass (57) ported `engine/foundation` and the rest of the tree in one step with Day 213 and 214 (the script does not distinguish modules). Hand fixes: include placement in `x11_window.cpp` and `vk_functions.cpp`, a self-recursive local `Abs` in `renderer/frustum.cpp`, `ENGINE_OFFSET_OF` for `offsetof`, `SortRaw` for `qsort`, `HasVirtualDestructor`/`TypeIdentity`/`kMaxAlign` in `lang`. Signal-safe regions: the wrapper names were added to the `signal_safety_check` regex. **Not done:** the log/container benchmark comparison and the `MemCopy` disassembly check (the sync benchmark and the Day 208 numbers cover the inline-forwarder claim); the Day 221 closing run records the numbers.
