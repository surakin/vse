# Phase 23 / Day 355 — macOS: the dylib & Embedding into an `NSView`

**Phase:** [Phase 23 — The Engine as a Loadable Library](../phase-23-engine-as-library.md)
**Depends on:** Days 344-353 and 354's lessons; Phase 21. **Done on the Mac.**

The shared build and the host-window embedding on macOS with Metal.

## Tasks

- [ ] `ENGINE_BUILD_SHARED` on `macos-clang` (x86-64) and the arm64 compile check: `libengine.dylib`, install name and `@rpath`, hidden visibility, `abi_check` through `nm -gU`, asan and tsan flavors, the hardened-runtime and library-validation notes in the guide (a host that is hardened loads only signed libraries).
- [ ] The loader on macOS (`dlopen`), the reload cycles (Objective-C runtime state does not unload: the documented limit and what the test tolerates), `@autoreleasepool` around every C API entry that touches AppKit or Metal.
- [ ] Foreign surface: Metal on a host `NSView` or `CAMetalLayer`, resize and scale, the display link driving `engine_frame`, device and drawable loss, readback; the Cocoa C host (an Objective-C file calling the C API) with its own window forwards input and implements the cursor and clipboard callbacks (window tests need the console unlocked).
- [ ] Run the whole C host suite on the Mac; the Mac's side-phase rule: results are quoted in this file and `KNOWN-ISSUES.md`; note iOS: static only, the headers compile for iOS (a compile check).
- [ ] **Runs (CLAUDE.md "Tests run by phase"):** `scripts/test-phase.sh --affected` plain; then the asan and tsan flavors when the day touched concurrency, lifetime or memory code (this phase does, nearly every day); quote each run's final `ran X of Y tests (phases ...)` line in this file and never say "all tests pass" for a reduced run. `ctest -L docs` when a public header or a guide changed. Build clean (warnings are errors) with both compilers, unity on and off, **and with `-DENGINE_BUILD_SHARED=ON`** from Day 343 on.
- [ ] **Three machines (CLAUDE.md):** this day is built and tested on the Mac; shared code (CMake, the gates, the C header, the loader) is checked on the other two no later than Day 357; until then the day is "open until the other machines run it" in `KNOWN-ISSUES.md`.
- [ ] **Documentation (CLAUDE.md "Public API is documented before a day is done"):** headers carry `///` comments with the contract labels (`engine_c.h` has its own comment convention, set on Day 346, checked by `docgen` or its own gate); `docgen lint` at zero; `cmake --build build --target docs` output committed; the guide `docs/guide/engine-library.md` is updated.
- [ ] **Module rules (CLAUDE.md):** a new module, target or edge updates `ARCHITECTURE.md`, `tools/module_deps_check/modules.txt` and `BUILD.md` (a new prerequisite) in the same commit; every new test file has its line in `tests/phases.txt` (phase 23).
- [ ] **Day review (CLAUDE.md, required):** `/code-review high` over the day's diff after the scoped tests; verify, fix (with a regression test that fails without the fix) or decline each finding with a reason; record the count and outcome under "Post-implementation code review" below and in `STATUS.md`.
- [ ] `STATUS.md` and `KNOWN-ISSUES.md` updated in the same commit; commit and push.

**Done:** the dylib builds and loads, the reload cycles and the C hosts pass on the Mac with Metal, including the embedded `NSView`.

## Outcome

(written when the day is done)

## Post-implementation code review

(written when the day is done)
