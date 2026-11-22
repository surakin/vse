# Maintenance: the published documentation never names phases or days

This is not a numbered phase: it is a one-day maintenance task, ordered by the user on 2026-11-22 (no phase or day number changes).

**Origin:** the phases and days only guided development, but the guides, the doc comments of the headers and the generated reference named them
(about 1,150 mentions: "Phase 21 Day 299", "glTF coverage Day 9", "the Phase 4 portability review", ...). The user's decision: **the documentation does not
list phases at all.** Scope chosen by the user: the guide pages, the generated reference (edited at its source, the headers' `///` comments, and the two
binding descriptions in `engine/script` that feed `script-api.md`) and `docs/DOC-CONVENTIONS.md`. `plans/`, `PLAN.md`, `STATUS.md`, `KNOWN-ISSUES.md`,
`docs/superpowers/` and ordinary `//` comments keep their references.

## Tasks

- [x] A mechanical pass removes the parenthetical forms ("(Phase 21 Day 299)", "(glTF coverage Day 9)", a sentence ending in "Phase 22 Day 321.").
- [x] The rest is reworded by hand, keeping each fact and dropping each date: 89 headers, 29 guide pages.
- [x] `docs/reference/` regenerated (`cmake --build build --target docs`), `docgen lint` at zero new findings.
- [x] The gate `docs_phase_check` (`tools/docs_phase_check`, ctest `docs_phase_check*`, label `docs_phase`, phase 3 with the other docs tests) fails on `Phase N`,
  `Day N` and a wrapped `Phase`/`Day` in the guides, tutorials, reference, `README.md` and the headers' doc comments; planted fixtures prove it.
- [x] CLAUDE.md states the rule ("Published documentation never names phases or days").

**Done:** `docs_phase_check` passes on the tree (742 files), `ctest -L docs` passes, the build is clean.

## Post-implementation code review

Not run: the day changes comments, two string literals and CMake registration, no behaviour (the end-of-day review applies to implementation days).
