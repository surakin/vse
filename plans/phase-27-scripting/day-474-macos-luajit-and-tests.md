# Phase 27 / Day 474 — macOS: LuaJIT on Apple Clang, the Tests and the Hashes

**Phase:** [Phase 27 — Scripting](../phase-27-scripting.md)
**Depends on:** Day 473; Phase 21.

On the Mac: x86-64 builds and runs everything; arm64 compiles.

## Tasks

- [ ] Pull and build `macos-clang` (x86-64; plain; no sanitizer runs for now) and `macos-clang-arm64` compile-only in both unity modes; LuaJIT's macOS build (the deployment target flag it requires, the arm64 cross route from the x86-64 host) driven from CMake; `BUILD.md`'s macOS section updated.
- [ ] **File layer: the macOS directory watcher, FSEvents** (`io/macos/directory_watcher.mm`, the backend of the Day 464 `io::DirectoryWatcher`; section in `io/BACKEND.md`): `FSEventStreamCreate` with `kFSEventStreamCreateFlagFileEvents | kFSEventStreamCreateFlagNoDefer` on a private serial dispatch queue (`FSEventStreamSetDispatchQueue`, no run loop), a small latency (50 ms); per-file flags (`ItemCreated`, `ItemModified`, `ItemRemoved`, `ItemRenamed`) mapped to the neutral events, `MustScanSubDirs`/`UserDropped`/`KernelDropped` become `Overflow`, events filtered to the watched directory (FSEvents is recursive: deeper paths are dropped unless the recursive option is on), the callback only appends to a mutex-guarded queue that `Poll`/`Wait` drain (ARC, `@autoreleasepool` in every entry point per the macOS rule). Spike first (30 minutes on the Mac): whether `kqueue` on the directory plus per-file `EVFILT_VNODE` is simpler and enough for one directory; the day file records the choice, FSEvents is the default. Core Services is a system framework, `BUILD.md` lists it. The Day 464 conformance suite runs against it (plain; no sanitizer runs for now); `Open` selects it instead of the polling fallback; the hot-reload test then runs through it. Remove its `KNOWN-ISSUES.md` entry.
- [ ] The script tests including the thread-count independence suite, the sandbox corpus, hot reload and the C API tests with the dylib.
- [ ] The cross-machine hash comparison against Linux's and the PC's; arm64 output is compile-only and listed as unchecked.
- [ ] A note on the JIT on Apple silicon (the write-xor-execute memory rule): the arm64 build is compile-only here, the finding for Phase 29 is recorded, and the interpreter-only mode (Day 470) is the fallback.
- [ ] Record the final `ran X of Y` line and the comparison table in this file; commit and push from the Mac.
- [ ] **Runs (CLAUDE.md "Tests run by phase"):** `scripts/test-phase.sh --affected` plain; then the asan and tsan flavors (this phase runs script code on several threads on nearly every day, so tsan is not optional); quote each run's final `ran X of Y tests (phases ...)` line in this file and never say "all tests pass" for a reduced run. `ctest -L docs` when a public header or a guide changed. Build clean (warnings are errors) with both compilers and unity on and off; `-DENGINE_BUILD_SHARED=ON` and `-DENGINE_LIBRARY_WITH_IMPORTER=OFF` still build.
- [ ] **Three machines (CLAUDE.md):** this is the Mac's day; Linux and the PC are checked on Days 472-473.
- [ ] **Documentation (CLAUDE.md "Public API is documented before a day is done"), when a public header changed:** `///` comments with the contract labels and `docgen lint` at zero; `cmake --build build --target docs` output committed; `docs/guide/scripting.md` updated or created; tagged samples compile and run.
- [ ] **Module rules (CLAUDE.md):** a new module, target or edge updates `ARCHITECTURE.md`, `tools/module_deps_check/modules.txt`, `tests/module_map.txt` and `BUILD.md` in the same commit; every new test file has its line in `tests/phases.txt` (phase 27); only wrapped types, no `std`, no raw platform macros; Lua headers only in the LuaJIT backend files (the boundary gate).
- [ ] **Day review (CLAUDE.md, required):** `/code-review high` over the day's diff after the scoped tests; verify, fix (with a regression test shown to fail without the fix) or decline each finding with a reason; record the count and outcome under "Post-implementation code review" below and in `STATUS.md`.
- [ ] `STATUS.md` and `KNOWN-ISSUES.md` updated in the same commit; commit and push.

**Done:** The Mac builds LuaJIT, passes the script tests, and the hashes equal Linux's and the PC's.
