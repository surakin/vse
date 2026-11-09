# Phase 15 / Day 184 — Design Spec, Spikes and the `ui` Module Skeleton

**Phase:** [Phase 15 — UI System](../phase-15-ui-system.md)
**Depends on:** Phase 14 complete on Linux (Days 175-183).

Settle the questions the phase file leaves open, by short spikes, before any widget exists. This day starts the phase: `CURRENT_PHASE` becomes 15 in its commit.

## Tasks

- [ ] **Write `docs/superpowers/specs/2026-11-NN-ui-design.md`** (date = the day it is written) with the decisions below, each with its reason and the spike that backed it. The phase index (`plans/phase-15-ui-system.md`, "Decisions made when planning") has the starting positions; this day confirms or changes them, and an amendment goes into the index and the later day files in the same commit.
- [ ] **Spike A, rounded rectangles and borders:** build a throwaway program (in the scratchpad, not committed) that draws 2,000 rounded rects with borders both ways: tessellated corners (8 segments) and one quad with a signed-distance function in the fragment shader. Compare vertex count, picture quality at 1x and 2x scale and 1-pixel borders, and CPU time to build 2,000 of them. Record the numbers and the choice (planned: SDF quad, one pipeline).
- [ ] **Spike B, draw-list shape:** measure building a 2,000-widget frame (quads plus 6,000 glyph quads) into one `Vector<Vertex>`/`Vector<u32>` with 32-bit indices and per-command clip rectangles; decide the vertex layout (position, uv, color, rect/radius/border parameters) and the command structure (clip rect, texture id, index range) from the numbers.
- [ ] **Spike C, text field model:** take `text/cursor.h` and write a throwaway console test that types, deletes by cluster, moves with arrows and selects with shift over a string containing a combining mark and a flag; list which operations the cursor API lacks (word movement is the known one) and decide what the text field implements itself (planned: word movement by whitespace and punctuation boundaries inside `ui`, not in `text`).
- [ ] **Decide and record:** (1) coordinates are logical units times the surface's scale factor, the draw list is in physical pixels; (2) widgets are classes with `UniquePtr` children, ids are 32-bit hashes of the parent id and a name or index, state lives in the widget; (3) one `ui::Event` type, own `ui::Key` values equal to `os::Key` (checked by a `static_assert` table); (4) frame order: events, `Update(dt)`, layout, draw-list build, then the renderer pass; (5) the OS clipboard and OS drag and drop are in this phase (platform Days 187-188, toolkit Day 200), asynchronous with timeouts, with an in-process clipboard kept for offscreen surfaces and tests; (6) UI is main-thread only; (7) a surface owns its root, theme, size, scale, clock and focus, and widgets never name `os::Window`; (8) the library `ui` is CPU-only and names no RHI type: images are `ui::ImageId` (opaque 64-bit) that the renderer maps to textures.
- [ ] **Create the module `engine/ui`** (library `ui`, namespace `engine_placeholder::ui`): `CMakeLists.txt` (unity-aware like `text`, `_asan`/`_tsan` flavors if `text` has them), one placeholder header `ui/version.h` with `kUiLayoutVersion`, and `tests/ui/CMakeLists.txt` with one trivial test; `tools/module_deps_check/modules.txt` maps the files and targets; `ARCHITECTURE.md` gets the row (planned: layer 11 beside the RHI, depends on `text`, `platform` input headers only, foundation) and the new edges (the renderer will link `ui` on Day 190); `docs/reference/modules.txt` lists `ui`.
- [ ] **Platform headers without the display libraries:** decide how `ui` includes `platform/input.h` without linking the Wayland/X11 libraries (planned: a header-only CMake target `platform_input` holding `input.h` and, from Day 185, `input_event.h`, which `platform` and `ui` both use); make it so and keep `module_deps_check` green.
- [ ] **Test directory and phase map:** `tests/phases.txt` gets the `tests/ui/` entries (phase 15); `CURRENT_PHASE` set to 15 in this commit; `scripts/test-phase.sh --dry-run` shows phase 15 selected.
- [ ] **Guide stub:** `docs/guide/ui.md` (status "draft, Phase 15") with the model section from the spec; listed in `docs/guide/README.md`.
- [ ] **Runs (CLAUDE.md "Tests run by phase"):** `scripts/test-phase.sh --affected` plain on Linux; then the asan and tsan flavors when the day touched lifetime, memory or concurrency code; quote each run's final `ran X of Y tests (phases ...)` line in this file. `ctest -L docs` when a public header or a guide changed. Build clean (warnings are errors) with Clang and GCC, unity on and off.
- [ ] **Two machines (CLAUDE.md "Windows is a supported platform"):** shared code is built and tested on the Windows PC before the phase closes (Day 205); a day done on one machine files "open until the other machine runs it" in `KNOWN-ISSUES.md`.
- [ ] **Documentation:** public headers carry `///` comments with the contract labels (`docs/DOC-CONVENTIONS.md`); `docgen lint` stays at zero; `cmake --build build --target docs` output committed; the guide page `docs/guide/ui.md` gains the day's section (tagged samples compile and run); new modules are in `docs/reference/modules.txt`.
- [ ] **Module rules:** `ARCHITECTURE.md`, `tools/module_deps_check/modules.txt` and `BUILD.md` updated in the same commit when a module, an edge or a prerequisite changed; `module_deps_check`, `platform_neutral_check`, `std_container_check`, `file_access_check` and `config_access_check` pass.
- [ ] **End-of-day review:** `/code-review high` over the day's diff; verify, fix (with a regression test that fails without the fix) or decline each finding with a reason; record the outcome under "Post-implementation code review" below and in `STATUS.md`.
- [ ] `STATUS.md` and `KNOWN-ISSUES.md` updated in the same commit; commit and push.

**Done:** the spec is committed with the spike numbers, the `ui` library builds and is mapped in the architecture gate, `CURRENT_PHASE` is 15, and the decisions the later days depend on are written down.

## Notes

(filled in as the day goes)

## Post-implementation code review

(filled in at the end of the day)
