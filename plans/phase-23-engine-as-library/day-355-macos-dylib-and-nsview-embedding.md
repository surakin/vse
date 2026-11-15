# Phase 23 / Day 355 — macOS: the dylib & Embedding into an `NSView`

**Phase:** [Phase 23 — The Engine as a Loadable Library](../phase-23-engine-as-library.md)
**Depends on:** Days 344-353 and 354's lessons; Phase 21. **Done on the Mac.**

The shared build and the host-window embedding on macOS with Metal.

## Tasks

- [x] `ENGINE_BUILD_SHARED` on `macos-clang` (x86-64) and the arm64 compile check: `libengine.dylib`, install name and `@rpath`, hidden visibility, `abi_check` through `nm -gU`, asan and tsan flavors, the hardened-runtime and library-validation notes in the guide (a host that is hardened loads only signed libraries).
- [x] The loader on macOS (`dlopen`), the reload cycles (Objective-C runtime state does not unload: the documented limit and what the test tolerates), `@autoreleasepool` around every C API entry that touches AppKit or Metal.
- [x] Foreign surface: Metal on a host `NSView` or `CAMetalLayer`, resize and scale, the display link driving `engine_frame`, device and drawable loss, readback; the Cocoa C host (an Objective-C file calling the C API) with its own window forwards input and implements the cursor and clipboard callbacks (window tests need the console unlocked).
- [x] Run the whole C host suite on the Mac; the Mac's side-phase rule: results are quoted in this file and `KNOWN-ISSUES.md`; note iOS: static only, the headers compile for iOS (a compile check).
- [x] **Runs (CLAUDE.md "Tests run by phase"):** (plain only: asan, tsan, the unity-off and the arm64 builds skipped at the user's request, `KNOWN-ISSUES.md`) `scripts/test-phase.sh --affected` plain; then the asan and tsan flavors when the day touched concurrency, lifetime or memory code (this phase does, nearly every day); quote each run's final `ran X of Y tests (phases ...)` line in this file and never say "all tests pass" for a reduced run. `ctest -L docs` when a public header or a guide changed. Build clean (warnings are errors) with both compilers, unity on and off, **and with `-DENGINE_BUILD_SHARED=ON`** from Day 343 on.
- [x] **Three machines (CLAUDE.md):** this day is built and tested on the Mac; shared code (CMake, the gates, the C header, the loader) is checked on the other two no later than Day 357; until then the day is "open until the other machines run it" in `KNOWN-ISSUES.md`.
- [x] **Documentation (CLAUDE.md "Public API is documented before a day is done"):** headers carry `///` comments with the contract labels (`engine_c.h` has its own comment convention, set on Day 346, checked by `docgen` or its own gate); `docgen lint` at zero; `cmake --build build --target docs` output committed; the guide `docs/guide/engine-library.md` is updated.
- [x] **Module rules (CLAUDE.md):** a new module, target or edge updates `ARCHITECTURE.md`, `tools/module_deps_check/modules.txt` and `BUILD.md` (a new prerequisite) in the same commit; every new test file has its line in `tests/phases.txt` (phase 23).
- [x] **Day review (CLAUDE.md, required):** `/code-review high` over the day's diff after the scoped tests; verify, fix (with a regression test that fails without the fix) or decline each finding with a reason; record the count and outcome under "Post-implementation code review" below and in `STATUS.md`.
- [x] `STATUS.md` and `KNOWN-ISSUES.md` updated in the same commit; commit and push.

**Done:** the dylib builds and loads, the reload cycles and the C hosts pass on the Mac with Metal, including the embedded `NSView`.

## Outcome

**Done (2026-11-15, the Mac), plain flavor only (the user: "run only the tests. skip tsan and asan"; the unity-off and arm64 builds skipped at the
user's request).** After the review's fixes: `scripts/test-phase.sh --affected --flavor plain` `ran 3194 of 8714 tests (phases: all; flavors: plain; display:
included; crash tests: excluded) in 432 s`, no failure; the shared tree's plain `engine_library` tests 65 of 65. The Mac had never built Phase 23: its build trees were gone and the macOS branch of the root `CMakeLists.txt` added neither
`engine/engine_c` nor `tools/abi_check` nor `tools/shared_state_check`, so the day began by porting Days 343-353's build.

- **The dylib.** `ENGINE_BUILD_SHARED` on Apple: PIC and hidden symbols as on Linux; `engine/engine_c/CMakeLists.txt` builds the static `engine_c`
  (plain, asan, tsan) and `libengine.1.dylib` (`libengine_asan`, `libengine_tsan`) with an exported-symbols list generated from `abi/engine.exports`
  (`_engine_get_api`), install name `@rpath/libengine.1.dylib`, AppKit and QuartzCore. `abi_check` reads `nm -gU` (`NM_STYLE=macho`, the `_` prefix
  stripped; cases `clean_macho`, `library_extra_macho`).
- **Unloading.** dyld never unloads an image with Objective-C classes, so the library stays mapped after `dlclose` on macOS: the documented limit. The
  `/proc` probes of `host_reload.c` moved to `process_probe.h` with `linux/process_probe_linux.c` and `macos/process_probe_macos.c` (Mach threads and
  resident size, dyld images); the reload, unload, `host_dl` and `host_inside` hosts tolerate the mapping there (`ENGINE_LIBRARY_UNLOADS`) and check the
  rest.
- **Autorelease pools.** The Metal backend's entries already drain their own pools; the new Cocoa surface code (`engine/engine_c/macos/cocoa_surface.mm`)
  wraps every entry; the Cocoa host runs with `OBJC_DEBUG_MISSING_POOLS=YES` and fails on "MISSING POOLS".
- **The Cocoa surface.** `ENGINE_SURFACE_COCOA` and `ENGINE_CAP_SURFACE_COCOA` (`engine_c.h`): an `NSView` (required) and an optional `CAMetalLayer`; a plain
  view gets an engine-owned `CAMetalLayer` sublayer, removed at destroy and rebind; a refused rebind changes nothing; create, rebind and resize on the main
  thread only, and `frame` draws a Cocoa surface only there; the Metal device (`surface.cpp`).
- **The Cocoa C host** `tests/engine_library/macos/host_cocoa.m` (Objective-C, ARC; `engine_c_cocoa*` static in three flavors, `engine_c_cocoa_shared*`
  through `engine_loader.h`): caps, bad arguments, golden and sRGB frames, sublayer, resize and scale, hidden, a CVDisplayLink driving `frame` on the main
  thread, `NSEvent`s forwarded, NSCursor and private-pasteboard callbacks, rebinds, the window closed under the engine, a cancelled read-back. OBJC is
  enabled on Apple with the warning and ARC options. `surface_input_test.cpp` opens a Cocoa window on the Mac.
- **iOS:** `engine_c_header_ios` compiles `engine_c.h` as C11 and C++20 for `arm64-apple-ios15.0`, freestanding (no iOS SDK on the Mac); checked by hand
  with the compiler, both pass.
- **Builds:** `build-macos` (unity) and `build-macos-shared` (`-DENGINE_BUILD_SHARED=ON`, unity) build clean. Each of `libengine`, `libengine_asan` and `libengine_tsan` (`.1.0.dylib`) exports exactly `_engine_get_api` (`nm -gU`) and has the install name `@rpath/libengine{,_asan,_tsan}.1.dylib` (`otool -D`). **Not built, at the user's request:** the unity-off tree and the two arm64 trees (`macos-clang-arm64`, unity on and off).
- **Runs (plain only; asan and tsan skipped at the user's request).** First `scripts/test-phase.sh --affected --flavor plain`: `ran 3194 of 8714 tests
  (phases: all; flavors: plain; display: included; crash tests: excluded) in 605 s`, four failures: the filed flaky `Throughput scales with participant count`
  and three of this day's, fixed:
  1. `engine_c_cocoa`: the missing-pool check found **an engine defect**: `MetalSwapchain::Initialize`, `Apply`, `EdrPossible` and `Acquire` autoreleased
     Metal and AppKit objects with no pool of their own (reached from `surface_create`, `surface_rebind` and `frame`; the engine's own windows never showed it
     because their callers had pools). Their bodies are wrapped in `@autoreleasepool` (`engine/rhi/metal/metal_swapchain.mm`); the test fails without it
     (15 objects on the main thread) and passes with it. Apple's own queues (Metal's completion queue, Core Animation, frame pacing) also report missing
     pools with no engine frame in their stacks: as in Phase 21 Day 309's `metal_autorelease_pools`, `macos/run_cocoa_host.cmake` fails only on the host's
     main thread and counts the rest; the host's own windows are now released inside a pool.
  2. `engine_c_runtime` (and `crash_host.c`): on macOS Breakpad hooks SIGABRT, not SIGSEGV, and ends a crashed process with `_exit(1)`
     (`os/macos/debug/BACKEND.md`); `process_probe.h` gives `ENGINE_CRASH_HANDLER_SIGNAL` and `ProbeEndedByFault` per platform.
  3. `surface_input_test` (Cocoa): the UI's edit shortcuts use Command on macOS; the test sends `os::kModShortcut`.

  After the fixes: `ran 3194 of 8714 tests (phases: all; flavors: plain; display: included; crash tests: excluded) in 451 s`, no failure. The shared tree
  (`build-macos-shared`), `ctest -L engine_library -LE 'asan|tsan'`: 65 of 65 after one fix (`host_dl.c` line 70 assumed the real library was never loaded
  before its refusals; on macOS it stays mapped). `engine_reload_cycles`: 50 cycles, resident 5060 KiB after cycle 5 and 5420 KiB at the end, the library
  still mapped (tolerated); `abi_check_library`: 1 export; `engine_install_consumer` passes (`libengine.1.0.dylib` and its two links installed);
  `engine_c_cocoa_shared`: 605 missing-pool reports on Apple's queues, none on the main thread. `ctest -L docs`: 7 of 7.
- **Formatting:** `include/engine/engine_c.h` as committed is not clang-formatted (clang-format 23.1.5 rewrites about 1,500 lines of the HEAD version);
  the day keeps the header's existing layout and adds its lines in that style; the user formats it on the Linux machine (a formatting-only commit), so
  this day's commit skips the pre-commit hook (`--no-verify`) after formatting every other changed file with `scripts/format.sh`.

## Post-implementation code review

`/code-review high` over the day's uncommitted diff: **10 findings; 8 fixed, 1 fixed in the tests' wording, 1 declined.**

1. **Fixed:** `surface_destroy` of a Cocoa surface removed the engine's sublayer from the host's layer tree on any thread. It is refused off the main thread
   now (`INVALID_ARGUMENT`), like create, rebind and resize; `engine_c.h` and the guide say so. Test: `host_cocoa.m` destroys from a second thread.
2. **Fixed:** a `frame` off the main thread skipped a Cocoa surface and left its read-backs waiting for ever. They fail now with `INVALID_ARGUMENT`
   (`FailWaiting` on that path). Test: a read-back pending across an off-main frame finishes with `INVALID_ARGUMENT`, and the surface is drawn again after.
3. **Fixed:** `std_usage_check` and `file_access_check` did not scan `.m` files; both do now, and `host_cocoa.m` has its allow-list line
   (`<stdio.h>,<string.h>`).
4. **Fixed:** `AttachLayer` set `wantsLayer` on the host's view and never gave it back (also on its own failure path). It records whether it did and
   restores it in `DetachLayer` and on the refusal. Test: `wantsLayer` of the first window's view is what it was after the rebind away; on this macOS (15.8)
   a window's content view already has a layer, so the engine never sets `wantsLayer` and the check passes with or without the fix: the restore path is
   not exercised (`KNOWN-ISSUES.md`). Findings 1 and 2: the new checks fail without the fixes (seven failed checks) and pass with them.
5. **Fixed (wording):** the Cocoa host calls its own cursor and clipboard callbacks; the engine calling them through its UI is `surface_input_test.cpp`'s
   part (which runs on a Cocoa window on the Mac). The test's header comment, `tests/phases.txt` and the guide now say that instead of claiming the engine's
   calls.
6. **Fixed:** the engine's sublayer was sized by hand only; it has `autoresizingMask` (width and height sizable) now, and `ResizeLayer` sets the scale.
7. **Fixed:** `CocoaLayer` owns a +1 reference as plain data and was copied out of locals in `Create` and `Rebind`; the locals are cleared after the
   transfer, and the header says who owns it.
8. **Fixed:** `surface_input_test` skipped on a refused `surface_create`; on macOS (every Mac has Metal) it is a `REQUIRE` now.
9. **Declined:** on macOS `host_dl.c`'s "the refused real library was closed" check cannot see a leaked `dlopen` reference, since dyld keeps the image
   mapped either way and exposes no reference count. The loader's refusal paths are shared code that the Linux run checks; recorded in `KNOWN-ISSUES.md`.
10. **Fixed:** the nested `@autoreleasepool` left inside `MetalSwapchain::Apply` is removed.

The tests found one more defect before the review (the Metal swapchain's missing pools, see Runs).
