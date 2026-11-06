# Phase 7 / Day 99 — Dump Tooling & Symbols

**Phase:** [Phase 7 — Debugging Facilities](../phase-07-debugging-facilities.md)
**Depends on:** Days 95-98.

A developer who finds a `.dmp` must be able to turn it into a readable trace, including for a stripped release build. Use
existing tools; write only the glue.

## Tasks

- [x] Script `scripts/crash-report.sh <dump-or-envelope> [<executable-or-symbol-dir>]`: if given a sentry `.envelope` it first runs `crash_extract` (Day 95) to get the `.dmp`; then prints the faulting thread's trace, all threads, and the module list with build ids, using `lldb -b -c` (installed) and, when present, `minidump-stackwalk` (rust-minidump, `cargo install minidump-stackwalk`) for a symbolized trace from Breakpad `.sym` files; checks that the dump's main-module build id matches the executable passed and says so when it does not (a wrong binary gives a wrong trace); exits non-zero with an instruction when no tool is available.
- [x] Symbol workflow for release builds, documented and scripted: keep the unstripped binary or its separate debug file per build id (`objcopy --only-keep-debug`, `--add-gnu-debuglink`, or `eu-strip -f`), a `scripts/store-symbols.sh <binary>` that copies it to `symbols/<name>/<build-id>/` (the layout lldb's and Breakpad's tools expect), and the CMake option that builds Release with `-g` and splits it (`ENGINE_SPLIT_DEBUG_INFO`). Test with a small program: build, split, strip, crash, and resolve the trace from the symbol store.
- [x] `tools/symbolize` is not written: the existing `llvm-symbolizer` (installed) and `addr2line` are the offline resolvers for a raw `module+offset` taken from a report; a three-line recipe goes in the guide.
- [x] Automated tests: `crash-report.sh` on a dump from a crashing child names the faulting function and the thread; with a stripped binary and its symbol store it still does; with a wrong binary it reports the build id mismatch; with no tools available (simulated by an empty `PATH`) it exits non-zero with the instruction. Skips with a reason where `lldb` is missing. Run in the style of `tests/test_phase_script`.
- [x] If `minidump-stackwalk` and `dump_syms` (Breakpad) are not available, the Breakpad symbol path is documented but not exercised; file it in `KNOWN-ISSUES.md` as a check not yet done, naming the exact commands, rather than guessing that it works.
- [x] **Documentation:** the guide gets "Turning a dump into a trace" and "Keeping symbols for release builds"; `docs/guide/building-and-testing.md` mentions the scripts; `docgen links` and `lint` pass; `ctest -L docs` passes.
- [x] Run the day's scoped tests (`--affected`, asan and tsan flavors) and quote the `ran X of Y tests` lines.
- [x] **End-of-day review (CLAUDE.md):** `/code-review high` on the day's diff (scripts and CMake); record it here and in `STATUS.md`.
- [x] Update `STATUS.md`; commit.

**Done:** one script turns a dump (or the envelope a crash leaves) into a readable trace with the build-id check, release builds keep their symbols in a store the tools find, the three test cases pass, and anything that needs a tool not installed here is filed in `KNOWN-ISSUES.md`.


## Result

Phase 7 has 446 tests (21 new this day). Scoped runs: `ran 1033 of 2318 tests (phases: all; flavors: plain; display: included)` (one
known display failure), `ran 775 of 2318 ... asan`, `ran 510 of 2318 ... tsan`; also the non-unity, clang, Release and
Release-with-checks builds, 10 and 5 repeats of the phase, and `ctest -L docs`; `docgen lint` at 2676/2676. The symbol-store tests
skip with a printed reason in a build without debug information (Release without `-g`).

What exists: `scripts/crash-report.sh` (envelope or `.dmp`; `lldb` trace of the faulting thread, every thread and the modules;
`minidump-stackwalk` when installed; build-id check with exit 3; exit 66 with an instruction when no tool exists; exit 2 when lldb
cannot read the file), `scripts/store-symbols.sh` (`<store>/<name>/<build-id>/<name>.debug` plus relative links under
`.build-id/`), `scripts/split-debug.sh` (split, strip, debuglink; idempotent), the CMake option `ENGINE_SPLIT_DEBUG_INFO` with
`engine_split_debug_info(<target>)` on the demos and benches (checked by configuring a Release build and splitting `rhi_bench`:
1.4 MB stripped, 8.3 MB `.debug`, same build id), and the two guide sections including the `llvm-symbolizer`/`addr2line` recipe
(both checked against a known function). `tools/symbolize` is not written, as planned.

Pages touched: `docs/guide/foundation-debugging.md` ("Turning a dump into a trace", "Keeping symbols for release builds"),
`docs/guide/building-and-testing.md`.

## Post-implementation code review

`/code-review high` found 10 issues.

1. The `.build-id` links were absolute and broke when the store moved. **Fixed** (relative links; a test moves the store).
2. The build-id check ran only with lldb. **Declined as a check, fixed as behavior**: reading the id from the dump needs a tool; without lldb the script now warns that it did not compare, and the guide says so.
3. Paths with spaces broke the lldb startup file. **Fixed** (quoted and escaped).
4. lldb failures exited 0. **Fixed**: no thread listing or a bad exit status gives exit 2 (test added).
5. The Breakpad recipe would file the module under the wrong name. **Fixed in the recipe** (`dump_syms -n`, and the script says the `breakpad` directory is the user's to create); still not exercised (`KNOWN-ISSUES.md`).
6. The POST_BUILD split strips in place with no opt-out. **Declined**: the option is off by default and per target; the guide says to run an unstripped copy under valgrind.
7. The first listed module was assumed to be the program. **Fixed**: the module whose file name is the program's is chosen, falling back to the first.
8. `split-debug.sh` could overwrite a real `.debug` file with an empty one. **Fixed** (idempotent; test).
9. Bookkeeping files missing from the diff. They are part of this commit.
10. The tests did not show the child's output, and the name check was weak. **Fixed**: output in the failure message, a frame must print as a call `InjectFault(`, the `nm` check compares the exact count.

Found by the day's own matrix: the symbol-store tests failed in Release builds (no debug information to store); they now skip there with a reason.
