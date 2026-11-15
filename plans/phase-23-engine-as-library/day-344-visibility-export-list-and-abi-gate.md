# Phase 23 / Day 344 — Symbol Visibility, the Export List & the `abi_check` Gate

**Phase:** [Phase 23 — The Engine as a Loadable Library](../phase-23-engine-as-library.md)
**Depends on:** Day 343.

Hidden by default; one `ENGINE_API` macro; a committed export list; a gate that fails when it changes without a version bump.

## Tasks

- [ ] Add `ENGINE_API` (and `ENGINE_API_CALL`) to `build_defs.h`: `__attribute__((visibility("default")))` on GCC/Clang, `__declspec(dllexport/dllimport)` on MSVC and clang-cl chosen by `ENGINE_BUILDING_LIBRARY`, empty in a static build; a `build_defs` test.
- [ ] `-fvisibility=hidden -fvisibility-inlines-hidden` for every module in the shared build (and `/GL`-independent equivalents for Windows, written now, built on Day 354); verify with `nm -D` that nothing but the C API is exported.
- [ ] `abi/engine.exports` (the committed list of exported symbols, one per line, with the ABI version it belongs to) and a Linux version script generated from it; `tools/abi_check` (ctest `abi_check`): builds nothing, reads the built library's dynamic symbol table (a CMake script over `nm`/`objdump`; on Windows `dumpbin`, on macOS `nm -gU`) and fails on a symbol that is not in the list, a listed symbol that is missing, or a list change without a bump of `ENGINE_ABI_VERSION` in the same diff (compared with the last commit's list through git).
- [ ] A second check in the gate: `engine_c.h` (Day 346) is valid C (compiled by the C compiler with `-std=c11 -pedantic -Werror`) and contains no C++ token (`class`, `namespace`, `template`, `::`, `std`, `bool` without `stdbool.h`), with planted-violation fixtures like the other gates.
- [ ] Document how to add an export: the macro, the list line, the version rule.
- [ ] **Runs (CLAUDE.md "Tests run by phase"):** `scripts/test-phase.sh --affected` plain; then the asan and tsan flavors when the day touched concurrency, lifetime or memory code (this phase does, nearly every day); quote each run's final `ran X of Y tests (phases ...)` line in this file and never say "all tests pass" for a reduced run. `ctest -L docs` when a public header or a guide changed. Build clean (warnings are errors) with both compilers, unity on and off, **and with `-DENGINE_BUILD_SHARED=ON`** from Day 343 on.
- [ ] **Three machines (CLAUDE.md):** this day is built and tested on the Linux machine; shared code (CMake, the gates, the C header, the loader) is checked on the other two no later than Day 357; until then the day is "open until the other machines run it" in `KNOWN-ISSUES.md`.
- [ ] **Documentation (CLAUDE.md "Public API is documented before a day is done"):** headers carry `///` comments with the contract labels (`engine_c.h` has its own comment convention, set on Day 346, checked by `docgen` or its own gate); `docgen lint` at zero; `cmake --build build --target docs` output committed; the guide `docs/guide/engine-library.md` is updated.
- [ ] **Module rules (CLAUDE.md):** a new module, target or edge updates `ARCHITECTURE.md`, `tools/module_deps_check/modules.txt` and `BUILD.md` (a new prerequisite) in the same commit; every new test file has its line in `tests/phases.txt` (phase 23).
- [ ] **Day review (CLAUDE.md, required):** `/code-review high` over the day's diff after the scoped tests; verify, fix (with a regression test that fails without the fix) or decline each finding with a reason; record the count and outcome under "Post-implementation code review" below and in `STATUS.md`.
- [ ] `STATUS.md` and `KNOWN-ISSUES.md` updated in the same commit; commit and push.

**Done:** the shared library exports exactly the list, the gate fails on every planted change, and the static build is unaffected.

## Outcome

(written when the day is done)

## Post-implementation code review

(written when the day is done)
