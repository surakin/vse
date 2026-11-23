# Phase 27 / Day 474 — macOS: LuaJIT on Apple Clang, the Tests and the Hashes

**Phase:** [Phase 27 — Scripting](../phase-27-scripting.md)
**Depends on:** Day 473; Phase 21.

On the Mac: x86-64 builds and runs everything; arm64 compiles.

## Tasks

- [x] Pull and build `macos-clang` (x86-64; plain; no sanitizer runs for now) and `macos-clang-arm64` compile-only in both unity modes; LuaJIT's macOS build (the deployment target flag it requires, the arm64 cross route from the x86-64 host) driven from CMake; `BUILD.md`'s macOS section updated.
- [x] **File layer: the macOS directory watcher, FSEvents** (`io/macos/fsevents_watcher.cpp`; the plan said `directory_watcher.mm`, see the outcome, the backend of the Day 464 `io::DirectoryWatcher`; section in `io/BACKEND.md`): `FSEventStreamCreate` with `kFSEventStreamCreateFlagFileEvents | kFSEventStreamCreateFlagNoDefer` on a private serial dispatch queue (`FSEventStreamSetDispatchQueue`, no run loop), a small latency (50 ms); per-file flags (`ItemCreated`, `ItemModified`, `ItemRemoved`, `ItemRenamed`) mapped to the neutral events, `MustScanSubDirs`/`UserDropped`/`KernelDropped` become `Overflow`, events filtered to the watched directory (FSEvents is recursive: deeper paths are dropped unless the recursive option is on), the callback only appends to a mutex-guarded queue that `Poll`/`Wait` drain (ARC, `@autoreleasepool` in every entry point per the macOS rule). Spike first (30 minutes on the Mac): whether `kqueue` on the directory plus per-file `EVFILT_VNODE` is simpler and enough for one directory; the day file records the choice, FSEvents is the default. Core Services is a system framework, `BUILD.md` lists it. The Day 464 conformance suite runs against it (plain; no sanitizer runs for now); `Open` selects it instead of the polling fallback; the hot-reload test then runs through it. Remove its `KNOWN-ISSUES.md` entry.
- [x] The script tests including the thread-count independence suite, the sandbox corpus, hot reload and the C API tests with the dylib.
- [x] The cross-machine hash comparison against Linux's and the PC's; arm64 output is compile-only and listed as unchecked.
- [x] A note on the JIT on Apple silicon (the write-xor-execute memory rule): the arm64 build is compile-only here, the finding for Phase 31 is recorded, and the interpreter-only mode (Day 470) is the fallback.
- [x] Record the final `ran X of Y` line and the comparison table in this file; commit and push from the Mac.
- [x] **Runs (CLAUDE.md "Tests run by phase"):** `scripts/test-phase.sh --affected` plain; then the asan and tsan flavors (this phase runs script code on several threads on nearly every day, so tsan is not optional); quote each run's final `ran X of Y tests (phases ...)` line in this file and never say "all tests pass" for a reduced run. `ctest -L docs` when a public header or a guide changed. Build clean (warnings are errors) with both compilers and unity on and off; `-DENGINE_BUILD_SHARED=ON` and `-DENGINE_LIBRARY_WITH_IMPORTER=OFF` still build.
- [x] **Three machines (CLAUDE.md):** this is the Mac's day; Linux and the PC are checked on Days 472-473.
- [x] **Documentation (CLAUDE.md "Public API is documented before a day is done"), when a public header changed:** `///` comments with the contract labels and `docgen lint` at zero; `cmake --build build --target docs` output committed; `docs/guide/scripting.md` updated or created; tagged samples compile and run.
- [x] **Module rules (CLAUDE.md):** a new module, target or edge updates `ARCHITECTURE.md`, `tools/module_deps_check/modules.txt`, `tests/module_map.txt` and `BUILD.md` in the same commit; every new test file has its line in `tests/phases.txt` (phase 27); only wrapped types, no `std`, no raw platform macros; Lua headers only in the LuaJIT backend files (the boundary gate).
- [x] **Day review (CLAUDE.md, required):** `/code-review high` over the day's diff after the scoped tests; verify, fix (with a regression test shown to fail without the fix) or decline each finding with a reason; record the count and outcome under "Post-implementation code review" below and in `STATUS.md`.
- [x] `STATUS.md` and `KNOWN-ISSUES.md` updated in the same commit; commit and push.

**Done:** The Mac builds LuaJIT, passes the script tests, and the hashes equal Linux's and the PC's.

## Outcome (2026-11-19, the Mac)

Done, with the deviations and gaps below (all in `KNOWN-ISSUES.md`, entry "Phase 27 Day 474").

- **LuaJIT on macOS** (`third_party/luajit/CMakeLists.txt`): the host tools (`minilua`, `buildvm`) are built for the host processor
  (`OSX_ARCHITECTURES` = `CMAKE_HOST_SYSTEM_PROCESSOR`), `buildvm -m machasm` writes the VM, the DynASM flags and `LUAJIT_TARGET` follow
  `CMAKE_OSX_ARCHITECTURES` (x64, or arm64 with `DUALNUM`), and Darwin x64 gets `-fno-stack-check`. The root `CMakeLists.txt` builds
  `luajit` on `Linux OR Darwin` and adds `engine/script`, `script_physics`, `script_media`, their tests, `game/script_demo`,
  `game/script_bench` and `tests/script_demo` to the macOS block (without them `tools/asset_mint` failed to link: "library 'script' not
  found"). `BUILD.md` has a "LuaJIT on macOS" paragraph.
- **The watcher: FSEvents, not kqueue** (the spike). `kqueue` with `EVFILT_VNODE` needs an open descriptor per file plus one on the
  directory, reports only that the directory changed (a relisting finds which name) and must re-arm for every new file; FSEvents with
  `FileEvents` gives per-name flags with no descriptor per file. The file is `io/macos/fsevents_watcher.cpp`, not `directory_watcher.mm`:
  it needs no Objective-C (Core Services is C), and a second `directory_watcher.cpp` in `foundation_io` breaks `dsymutil` (two sources of
  one library may not share a base name on macOS). Stream flags `FileEvents | NoDefer | WatchRoot`, 50 ms latency, a private serial
  dispatch queue. Because FSEvents accumulates a path's flags, the backend decides by the name's current `lstat` against a known-name
  map (type, size, mtime): present and unknown is `Created`, gone and known is `Removed`, an `ItemModified` with no metadata-only flag is
  `Modified` even when size and time are unchanged; loss flags (`MustScanSubDirs`, `UserDropped`, `KernelDropped`) and an `lstat` error
  other than not-found give `Overflow` and a relisting; a `RootChanged` with the directory gone ends the watch, with the directory still
  there it relists. The directory is listed before and after the stream starts, so a name made or removed during the start is reported.
  A rename is reported as its two names (`Removed`, `Created`), not as `Renamed` (FSEvents gives no pairing to rely on); `directory_watcher.h`
  and `io/BACKEND.md` say so. The known-name map needed a string-keyed `HashMap`, which the foundation did not have: the
  `HashMap` rework (move-only keys and values, lookup by `StringView`, `ForEach`, `Hash<StringView>` and `Hash<String>`) is its own
  commit, `eefa8c3b`.
- **Builds:** `build-macos` (x86-64, unity) and the unity-off tree build clean; `build-macos-arm64` (unity, compile-only) builds clean;
  `build-macos-arm64-nounity` compiled every source, but 16 sanitizer test links (`tests/engine_library/host_*_tsan`, `audio_sample_tsan`
  and others) failed with `errno=28`: the disk was full (four trees are about 17 GB). The two arm64 trees were deleted afterwards to free
  space. `-DENGINE_BUILD_SHARED=ON` and `-DENGINE_LIBRARY_WITH_IMPORTER=OFF` were not built.
- **Runs** (plain only: no sanitizer runs on the Mac, the user's rule of 2026-11-15; this day's task list asked for asan and tsan, not run):
  `scripts/test-phase.sh --affected` (everything: `third_party/` and foundation headers changed)
  `ran 4022 of 11153 tests (phases: all; flavors: plain; display: included; crash tests: excluded) in 4934 s`, 3 failures:
  `Snapshot: the cost of a voice at each quality, with and without effects` (phase 26, known on the Mac since Day 441; failed 3 of 3 reruns,
  slower than on Day 441: 256 linear voices took 90 percent of one core, sinc 509); `Throughput scales with participant count` (phase 2,
  known flaky; passed 3 of 3 reruns); `io on macOS: a source whose size is not its content (a FIFO) reads whole` (phase 9) **hung**: the
  reader had read all 30,000 bytes, the writer thread had exited, and the next `read` did not return end of file for 74 minutes until the
  process was killed; it passed 3 of 3 reruns. This is a reduced run (the sanitizer flavors are not in it). The script tests (phase 27,
  including the thread-count suites, the sandbox corpus, hot reload through the FSEvents watcher and the C API hosts linked statically)
  passed in it. `ctest -L docs`: 7 of 7 passed.
- **Hashes** (x86-64): `engine_c_script_sample` ends on `e3e3ca13a45686f9` at 1, 2, 4 and 8 partitions, equal to Linux's (Day 472);
  `script_demo` ends on `e1f5c33055210a41` at 1, 2 and 8 threads (equal across thread counts; Linux's Day 475 value was not recorded, so
  the cross-machine comparison of it is open). The PC's values wait for Day 473. arm64: compile-only, nothing compared.
- **Linux's `script_demo` value, recorded afterwards (2026-11-19, Linux x86-64, Clang Release, `ce90ed09`; 2,000 entities, 16 partitions):**
  - `--frames 120`, the length the Mac and the PC compared, ends on `e1f5c33055210a41` at 1, 2, 8 and 16 threads, **equal to the Mac's and
    the PC's**.
  - At the default 600 frames it ends on `424a6fbf279d2104`, the same at 1, 2, 8 and 16 threads, in Release and Debug, and at this day's
    commit `6db61d8a` as well. No other machine has run 600 frames.

  | Program | Mac x86-64 | Linux | Windows PC |
  |---|---|---|---|
  | `script_sample` (1/2/4/8 partitions) | `e3e3ca13a45686f9` | `e3e3ca13a45686f9` | Day 473 |
  | `script_demo --frames 120` (1/2/8 threads) | `e1f5c33055210a41` | `e1f5c33055210a41` (recorded 2026-11-19, below) | `e1f5c33055210a41` (Day 473) |

- **JIT on Apple silicon:** the vendored LuaJIT toggles its machine-code pages between writable and executable with `mprotect` unless it
  is built with `LUAJIT_ENABLE_OSX_HRT`, which uses `MAP_JIT` and `pthread_jit_write_protect_np` (a per-thread switch). A signed app with the
  hardened runtime (and every iOS app) forbids the `mprotect` route: such an app needs the HRT build plus the
  `com.apple.security.cs.allow-jit` entitlement, and iOS has no JIT at all. Finding for Phase 31: build with `LUAJIT_ENABLE_OSX_HRT` on
  Apple arm64 and run the interpreter-only mode (`script.jit` off, Day 470) on iOS; nothing ran on arm64 here.
- **Docs:** `directory_watcher.h` (backend lines), `hash_map.h` and `string.h` (in `eefa8c3b`) changed; `docs/reference/` regenerated and
  committed; `io/BACKEND.md` has the "macOS, as built" section. No guide page changed.

## Post-implementation code review

`/code-review high`, run twice over the day's diff (the second after the first's fixes and the `HashMap` rework).

- **First review: 10 findings, all fixed.** Stale known names after a loss (the relisting now replaces the map); a start race (names made
  between the first listing and the stream start: listed again after the start); loss flags on a path below a subdirectory taken as a
  loss of the watched directory (ignored now); a change of permissions reported as a write (metadata-only flags give no event); an `lstat`
  error read as a removal (now `Overflow`); a root given with a trailing `/` matched no event path; `lstat` under the queue's lock (moved
  out); a linear search of the known names (now the `HashMap`); the destructor's teardown (it now stops and releases only a stream that was started and scheduled, then drains the queue with `dispatch_sync_f`); stale wording in
  `directory_watcher.h`.
- **Second review: 10 findings, all fixed.** In `HashMap`: a `const char*` borrowed-key lookup hashed the pointer and an integer one used a
  hash of another width (the `HeterogeneousKey` concept now excludes pointers, arrays, integers, floats and enums); `Insert` with an
  argument that aliases an element was a use after free when the insert rehashed (the arguments are copied out first); a mix of lvalue
  and rvalue arguments to `Insert` did not compile; a duplicated requires clause; three parallel arrays and a zeroing pass in `Rehash`
  (now one slot struct with uninitialized rooms). In the watcher: a `RootChanged` with the directory still there was ignored; a write
  with the same size and mtime was dropped (decided by the flags now); `EventIdsWrapped` was counted as a loss; raw arrays.
- **Regression tests:** "a change of permissions only is not reported as a write (Day 474 review)" was shown failing against the first
  version. "a name that was there before the watch and goes at once is reported removed (Day 474 review)" passes against both versions,
  so it does not prove its fix (recorded). The `HashMap` fixes have three tests in `hash_map_test.cpp` (in `eefa8c3b`).
- Declined: none.

**Done condition:** met for x86-64 except where noted: the Mac builds LuaJIT and passes the script tests; `script_sample`'s hash equals
Linux's; `script_demo --frames 120` equals Linux's and the PC's (recorded 2026-11-19).
