# Phase 23 / Day 346 — The C API Core: Version, Function Table, Create & Destroy

**Phase:** [Phase 23 — The Engine as a Loadable Library](../phase-23-engine-as-library.md)
**Depends on:** Days 344, 345.

`engine_c.h` v1 core: negotiation, the table, the engine object, settings, logging, errors.

## Tasks

- [ ] Write `engine/engine_c/engine_c.h` (valid C11): `ENGINE_ABI_VERSION`, `engine_status` codes mapped from `asset::ErrorCode` and the other `Status` types (one table, tested for completeness), `engine_string` (pointer and length), `engine_buffer` with explicit ownership and a release function, opaque handles with generations (`engine_handle`), `size`-prefixed structs (`struct_size` first) for every struct that can grow, callbacks with a user pointer; the convention comment at its top.
- [ ] **Remove the Day 343/345 probes** (`engine_shared_probe`, `engine_shared_state_probe`, `engine/engine_c/engine_state_probe.h`) and move the `tests/engine_library` hosts to the table: `abi_check` counts a removed symbol as a major-version change, so either bump to 2.0 in the same commit (nothing was released) or reset the list and the version header to 1.0 with the C header, and say which in the day file.
- [ ] The one exported entry point `engine_get_api(u32 wanted_major, u32 wanted_minor, usize table_size_wanted, const engine_api** out)` returning a table of function pointers (the table's own size says which functions exist); the old versions' tables stay valid by construction.
- [ ] `engine_create(const engine_create_info*, engine** out)` / `engine_destroy`: settings from a host-provided list of `key=value` entries and the command line the host passes (through the settings system, never `getenv` in the C API), a log callback (level, channel, message) with the thread rules documented, an allocator hook is **not** offered (decision on Day 342), `engine_last_error`.
- [ ] `engine/engine_c/` implementation in C++ (module `engine_c`, layer above everything it wraps, exported with `ENGINE_API` only here), no exception or C++ type crossing; every function checks its handle and its `struct_size`; a handle used after destroy fails cleanly.
- [ ] Tests: `tests/engine_library/host_c.c` compiled by the C compiler, linked statically against the C API first (the dynamic loading is Day 347): create with settings, run, destroy; error paths (null, wrong size, stale handle, a too-new version) each return the documented status.
- [ ] Docs: `docs/guide/engine-library.md` (the first sections: the C API's conventions, versioning rules, ownership).
- [ ] **Runs (CLAUDE.md "Tests run by phase"):** `scripts/test-phase.sh --affected` plain; then the asan and tsan flavors when the day touched concurrency, lifetime or memory code (this phase does, nearly every day); quote each run's final `ran X of Y tests (phases ...)` line in this file and never say "all tests pass" for a reduced run. `ctest -L docs` when a public header or a guide changed. Build clean (warnings are errors) with both compilers, unity on and off, **and with `-DENGINE_BUILD_SHARED=ON`** from Day 343 on.
- [ ] **Three machines (CLAUDE.md):** this day is built and tested on the Linux machine; shared code (CMake, the gates, the C header, the loader) is checked on the other two no later than Day 357; until then the day is "open until the other machines run it" in `KNOWN-ISSUES.md`.
- [ ] **Documentation (CLAUDE.md "Public API is documented before a day is done"):** headers carry `///` comments with the contract labels (`engine_c.h` has its own comment convention, set on Day 346, checked by `docgen` or its own gate); `docgen lint` at zero; `cmake --build build --target docs` output committed; the guide `docs/guide/engine-library.md` is updated.
- [ ] **Module rules (CLAUDE.md):** a new module, target or edge updates `ARCHITECTURE.md`, `tools/module_deps_check/modules.txt` and `BUILD.md` (a new prerequisite) in the same commit; every new test file has its line in `tests/phases.txt` (phase 23).
- [ ] **Day review (CLAUDE.md, required):** `/code-review high` over the day's diff after the scoped tests; verify, fix (with a regression test that fails without the fix) or decline each finding with a reason; record the count and outcome under "Post-implementation code review" below and in `STATUS.md`.
- [ ] `STATUS.md` and `KNOWN-ISSUES.md` updated in the same commit; commit and push.

**Done:** a C program creates an engine with settings and a log callback, queries the version, destroys it, and every bad call fails with a documented code.

## Outcome

(written when the day is done)

## Post-implementation code review

(written when the day is done)
