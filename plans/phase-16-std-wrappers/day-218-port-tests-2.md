# Phase 16 / Day 218 — Port the Tests II: platform, rhi, renderer, text, ui, docgen, shader_mint

**Phase:** [Phase 16 — Standard-Library Wrappers](../phase-16-std-wrappers.md)
**Depends on:** Day 217.

The remaining test directories, including the old allow-listed ones (`tests/docgen/` 331 lines, `tests/rhi/` 221, `tests/renderer/` 151) and the shared `tests/support/`.

## Tasks

- [ ] Port `tests/platform` (Linux), `tests/rhi` (including `image_compare.*`, shared by the golden tests; they convert together), `tests/renderer`, `tests/text`, `tests/ui`, `tests/ui_window`, `tests/game` if present, `tests/tools` and `tests/docgen` (including `fs_fixture.h`, shared by 7 files; fixtures that are *input text* for docgen are data and excluded by name from the gate).
- [ ] `tests/support/catch_engine_types.h` and `tests/shader_mint/reflect_json_test.cpp` stay as boundaries (Catch2 returns `std::string`; the JSON library); `tests/support/` helpers otherwise use engine types.
- [ ] Tests that only exist for Windows (`tests/windows/`) wait for Day 219.
- [ ] Remove the matching `std_container_check` allow-list entries (`tests/docgen/`, `tests/renderer/`, `tests/rhi/`).
- [ ] Goldens still match bit for bit; plain and asan runs for these directories, tsan where they use threads.
- [ ] **Day review (CLAUDE.md, required):** scoped tests (`scripts/test-phase.sh --affected`, plain, then asan and tsan where the day touched concurrency or memory code), then `/code-review high` on the day's diff; verify, fix or decline each finding with a reason and record the count and outcome under "Post-implementation code review"; `STATUS.md` and `KNOWN-ISSUES.md` updated; commit and push.

**Done:** every test directory except `tests/windows/` and the documented boundaries is free of `std::` and standard includes; goldens unchanged; the allow list holds no `tests/` directory entry other than the boundaries.
