# Phase 3 / Day 43 — Lint, Coverage & Build Integration

**Phase:** [Phase 3 — Documentation Tooling](../phase-03-docs-tooling.md)
**Depends on:** Days 40-42.

Turns the convention into an enforced rule.

## Tasks

- [x] Coverage report: documented / total public symbols per module and overall; `docgen lint --baseline FILE` fails if coverage drops below the committed baseline or any new undocumented public symbol appears.
- [x] Lint rules, configurable per module in the module map: summary required; `Thread-safety:` required on public types in modules marked thread-aware (jobs, async I/O); `Ownership:` required on functions returning raw pointers or taking ownership; `Fails:`/`Aborts:` required where the body asserts or returns failure (heuristic: declared via label presence on `bool`/pointer returns in modules marked strict).
- [x] CMake: a `docs` target that builds `docgen` and runs `reference`, `check`, `links`, `samples`, `lint`; ctest entries `docs_check`, `docs_links`, `docs_samples`, `docs_lint`.
- [x] Make `docgen` timing sane (full run over `engine/` under a few seconds); document how to run it in `docs/guide/building-and-testing.md`.
- [x] Commit the initial lint baseline (expected to be low until Day 44 retrofits the headers).
- [x] **End-of-day review (CLAUDE.md):** run `/code-review high` on the
  day's diff, fix/decline each finding with a reason, record it under a
  "Post-implementation code review" section here, and mention it in
  `STATUS.md`.
- [x] Update `STATUS.md`; commit.

**Done:** `ctest` runs the four docs checks, they pass on the current tree, and introducing an undocumented public symbol, a broken link or a non-compiling sample makes the matching check fail.

## Result

New: `tools/docgen/src/lint.{h,cpp}` (`RunLint`, baseline parse/render/
compare), the `docgen lint` command, `tests/docgen/lint_test.cpp` (14 cases;
suite is 166, plain and under ASan/UBSan), `docs/samples-prelude.h`, the
`docs` target and the ctest entries `docs_check`, `docs_links`,
`docs_samples`, `docs_lint` (label `docs`) in `tools/docgen/CMakeLists.txt`,
a "Documentation checks" section with a built-and-run sample in
`docs/guide/building-and-testing.md`, and the first committed output:
`docs/reference/` (generated pages, `symbols.txt`, index replacing the
hand-written skeleton README) and `docs/reference/lint-baseline.txt`
(690 known findings, 0% documented: nothing in the engine carries `///` yet).

Timing: `reference` over the whole engine ~15 ms, `check` ~15 ms, `links`
~15 ms, `lint` ~20 ms; `samples` ~0.4 s for the one sample (compile+link+run).
Breaking things by hand fails the right check: a broken link or a
non-compiling sample fails `docs_links`/`docs_samples`, a new undocumented
header symbol fails `docs_check` and `docs_lint`.

Decisions: deleted/defaulted members are exempt from the count (documented in
DESIGN.md section 8; Day 44 revisits if the renderer should say "deleted"
instead of "_Undocumented._"); the baseline is a ratchet in both directions
(fixed entries must be removed); the generated reference is committed now so
`docs_check` is meaningful, which means every header change that affects a
signature or a comment needs `cmake --build build --target docs`.

## Post-implementation code review (2026-11-04, `/code-review high`)

Fixed:
- Stale baseline entries were only warnings (and `--quiet` hid them), so the
  baseline never had to shrink and a regressed symbol stayed tolerated →
  errors. Removed/renamed/new modules and an overall ratio drop are now
  reported too.
- `--update-baseline` could bake new findings in → refuses unless
  `--allow-growth`; the file is written via a temporary file and `rename`.
- `Aborts:` did not satisfy the failure rule (the plan says `Fails:`/
  `Aborts:`) → it does.
- Inherited-only documentation ran label checks on a partial comment →
  skipped unless the symbol has its own summary (in a clean index a
  summary-less comment is already a `missing summary` error).
- `ReturnType` treated the whole signature as the return type when it could
  not find the name → judges nothing; return type is computed once per
  function; `T* const` parameters count as raw pointers.
- Link flags for the sample build came from variables of another directory's
  scope → asked of pkg-config in `tools/docgen/CMakeLists.txt`.
- `lint.h` comment referred to a non-existent object.

Declined:
- "Ratio gate false-alarms when documented symbols are deleted": the day's
  task asks for exactly this gate, and deleting documented API below the
  floor is a real coverage change; the remedy (`--update-baseline`, which
  refuses to lower a ratio without `--allow-growth`) leaves a reviewable diff.
- "`docs_samples` is not tied to building `jobs`/`foundation`": same
  convention as every other ctest entry (`ctest` after `cmake --build`; both
  libraries are part of `all`).
