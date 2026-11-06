# Phase 6 / Day 99 — Dump Tooling & Symbols

**Phase:** [Phase 6 — Debugging Facilities](../phase-06-debugging-facilities.md)
**Depends on:** Days 95-98.

A developer who finds a `.dmp` must be able to turn it into a readable trace, including for a stripped release build. Use
existing tools; write only the glue.

## Tasks

- [ ] Script `scripts/crash-report.sh <dump-or-envelope> [<executable-or-symbol-dir>]`: if given a sentry `.envelope` it first runs `crash_extract` (Day 95) to get the `.dmp`; then prints the faulting thread's trace, all threads, and the module list with build ids, using `lldb -b -c` (installed) and, when present, `minidump-stackwalk` (rust-minidump, `cargo install minidump-stackwalk`) for a symbolized trace from Breakpad `.sym` files; checks that the dump's main-module build id matches the executable passed and says so when it does not (a wrong binary gives a wrong trace); exits non-zero with an instruction when no tool is available.
- [ ] Symbol workflow for release builds, documented and scripted: keep the unstripped binary or its separate debug file per build id (`objcopy --only-keep-debug`, `--add-gnu-debuglink`, or `eu-strip -f`), a `scripts/store-symbols.sh <binary>` that copies it to `symbols/<name>/<build-id>/` (the layout lldb's and Breakpad's tools expect), and the CMake option that builds Release with `-g` and splits it (`ENGINE_SPLIT_DEBUG_INFO`). Test with a small program: build, split, strip, crash, and resolve the trace from the symbol store.
- [ ] `tools/symbolize` is not written: the existing `llvm-symbolizer` (installed) and `addr2line` are the offline resolvers for a raw `module+offset` taken from a report; a three-line recipe goes in the guide.
- [ ] Automated tests: `crash-report.sh` on a dump from a crashing child names the faulting function and the thread; with a stripped binary and its symbol store it still does; with a wrong binary it reports the build id mismatch; with no tools available (simulated by an empty `PATH`) it exits non-zero with the instruction. Skips with a reason where `lldb` is missing. Run in the style of `tests/test_phase_script`.
- [ ] If `minidump-stackwalk` and `dump_syms` (Breakpad) are not available, the Breakpad symbol path is documented but not exercised; file it in `KNOWN-ISSUES.md` as a check not yet done, naming the exact commands, rather than guessing that it works.
- [ ] **Documentation:** the guide gets "Turning a dump into a trace" and "Keeping symbols for release builds"; `docs/guide/building-and-testing.md` mentions the scripts; `docgen links` and `lint` pass; `ctest -L docs` passes.
- [ ] Run the day's scoped tests (`--affected`, asan and tsan flavors) and quote the `ran X of Y tests` lines.
- [ ] **End-of-day review (CLAUDE.md):** `/code-review high` on the day's diff (scripts and CMake); record it here and in `STATUS.md`.
- [ ] Update `STATUS.md`; commit.

**Done:** one script turns a dump (or the envelope a crash leaves) into a readable trace with the build-id check, release builds keep their symbols in a store the tools find, the three test cases pass, and anything that needs a tool not installed here is filed in `KNOWN-ISSUES.md`.
