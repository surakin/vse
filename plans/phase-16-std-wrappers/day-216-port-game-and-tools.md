# Phase 16 / Day 216 — Port `game/` and `tools/`

**Phase:** [Phase 16 — Standard-Library Wrappers](../phase-16-std-wrappers.md)
**Depends on:** Days 210, 212, 214.

The demos, benchmarks and offline tools (47 files in `tools/`, 16 in `game/`). Offline tools follow the same rule (decided: everything); the only exception is the nlohmann/json boundary.

## Tasks

- [x] Port `game/` (demos, benchmarks): console output, formatting, memory, math, time, threads, environment.
- [x] Port `tools/` (docgen, shader_mint, the checkers, minters): replace `std::` and standard includes; the checkers' own sources are scanned too (a checker that parses text uses `StringView`, `Vector`, `HashMap` from Phase 10).
- [x] `tools/shader_mint/reflect_json.cpp` keeps `std::string`/`std::vector` and the `<string>`, `<vector>` includes as the single documented boundary; it exposes only engine types to the rest of the tool, converted at its edge (the file and its test are on the gate's list).
- [x] The five `tools/` files still using `std::string`/`std::vector` (audit) are converted or listed with a reason.
- [x] Run every tool's tests, the minters' determinism tests (byte-identical outputs), and the demos 1000 frames on both display backends with `--validation`.
- [x] **Day review (CLAUDE.md, required):** scoped tests (`scripts/test-phase.sh --affected`, plain, then asan and tsan where the day touched concurrency or memory code), then `/code-review high` on the day's diff; verify, fix or decline each finding with a reason and record the count and outcome under "Post-implementation code review"; `STATUS.md` and `KNOWN-ISSUES.md` updated; commit and push.

**Done:** `game/` and `tools/` contain no `std::` or standard include except the shader_mint JSON boundary; determinism tests and demos pass.

## Result

- `game/` and `tools/` were ported by the Day 212-214 passes; they contain no `std::` and no standard include except the shader_mint JSON boundary (`reflect_json.cpp`) and the Windows-only files (`tools/crash_extract/windows/resolve.cpp`, `tools/shader_mint/windows/run_process_windows.cpp`, Day 219). The "five `tools/` files with `std::string`" of the audit were already on `Vector`/`String` (`tools/shader_mint/text.h`) after Phase 10; only `reflect_json.cpp` remains and is the documented boundary. Shader-mint determinism tests run in the scoped runs; demos 1000 frames with `--validation` on Wayland and X11, exit 0 (plain build).
- Review: no code change this day beyond Days 211-214 (reviewed there).
