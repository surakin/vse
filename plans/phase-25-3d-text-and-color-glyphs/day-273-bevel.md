# Phase 25 / Day 273 — Beveled Extrusion

**Phase:** [Phase 25 — 3D Text, Large 2D Text & Colored Glyphs](../phase-25-3d-text-and-color-glyphs.md)
**Depends on:** Day 272.

Optional bevel on the front and back edges: chamfer or rounded profile, correct at concave corners and thin strokes.

## Tasks

- [ ] **Profile:** `BevelOptions { f32 depth_em; f32 inset_em; u32 segments; BevelProfile profile (Chamfer | Round); f32 miter_limit; }`; the face is inset by `inset_em` along the offset of the loops (polygon offsetting of the cleaned loops with miter joins, falling back to bevel joins past the miter limit), the profile steps connect the inset face to the wall, `segments = 1` is a chamfer, more are a quarter-circle approximation.
- [ ] **Thin features:** an inset larger than half a stroke width would invert the face: the inset is clamped per loop to the local stroke width (computed from the offset's collapse) and the clamp is counted; the face triangulation is rerun on the inset shape (Day 269 code), so holes shrink and outers shrink consistently.
- [ ] **Normals** across the bevel are smooth for `Round`, flat for `Chamfer`; the wall/bevel junction obeys `smooth_angle_deg`.
- [ ] **Tests** (`tests/text/bevel_test.cpp`): a square with a chamfer: exact vertex positions and the volume (analytic); a round bevel's volume converges as `segments` grows; watertight and outward-facing for every glyph of the shipped font with a default bevel (the Day 272 checks reused); an inset past the stroke width of 'I' and '.' is clamped, still watertight, and counted; a concave corner ('L', 'V') with the miter limit exceeded gets a bevel join (no spike longer than limit * inset); triangle count versus `segments` recorded.
- [ ] **Runs (CLAUDE.md "Tests run by phase"):** `scripts/test-phase.sh --affected` plain; then the asan and tsan flavors (pure CPU); quote each run's final `ran X of Y tests (phases ...)` line in this file. `ctest -L docs` when a public header or a guide changed. Build clean (warnings are errors) with Clang and GCC, unity on and off. New code uses the engine's wrappers and types, never `std` (`std_usage_check`).
- [ ] **Two machines (CLAUDE.md "Windows is a supported platform"):** shared code is built and tested on the Windows PC no later than Day 281; until then the day files "open until the other machine runs it" in `KNOWN-ISSUES.md`.
- [ ] **Documentation (CLAUDE.md "Public API is documented before a day is done"):** public headers carry `///` comments with the contract labels (`docs/DOC-CONVENTIONS.md`); `docgen lint` stays at zero; `cmake --build build --target docs` output committed; `docs/guide/text.md` "3D text": bevel options and limits.
- [ ] **Day review (CLAUDE.md, required):** `/code-review high` over the day's diff after the scoped tests; verify, fix (with a regression test that fails without the fix) or decline each finding with a reason; record the count and outcome under "Post-implementation code review" below and in `STATUS.md`.
- [ ] `STATUS.md` and `KNOWN-ISSUES.md` updated in the same commit; commit and push.

**Done:** beveled solids are watertight and outward-facing for the whole font, thin features degrade by a counted clamp rather than inverting, and chamfer and round volumes are checked against analytic values.

## Notes

- (filled in as the day is worked)

## Post-implementation code review

(Filled in at the end of the day.)
