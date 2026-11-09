# Phase 15 / Day 189 — Themes: Data, Resolution, Variants, Run-Time Swap

**Phase:** [Phase 15 — UI System](../phase-15-ui-system.md)
**Depends on:** Day 187 (colors and metrics are drawn through the list); `docs/superpowers/specs` from Day 184.

The first widget is written against the theme API, so the theme comes before any widget.

## Tasks

- [ ] **`ui/theme.h`:** a `Theme` is plain data: named colors (`text`, `text_disabled`, `window_background`, `panel`, `accent`, `danger`, ...), metrics (padding, spacing, border width, corner radius, scrollbar thickness, minimum hit size, icon size), fonts (`FontRef` = family key + size in logical units, resolved by the surface's font provider), and **style entries**: per widget kind and **variant name** (`button/primary`, `label/heading`) a `Style` with a state table (normal, hover, pressed, focused, disabled, checked) of background, border, text color, radius and padding.
- [ ] **Resolution:** `ResolvedStyle Resolve(Theme chain, WidgetKind, Variant, StateBits)` walks the widget's theme chain (a widget's own override -> the nearest ancestor's override -> the surface's theme -> the built-in default theme); a value left unset falls to the next; unknown variant falls back to the kind's default style and counts a `missing_style` diagnostic (logged once per name). Resolution is pure and cached per (theme revision, kind, variant, state).
- [ ] **Scale:** all metrics and font sizes are logical units; `Resolve` multiplies by the surface's scale factor and rounds borders and paddings to whole physical pixels (so a 1-unit border is 1 pixel at 1x and 2 at 2x, never 1.5).
- [ ] **Run-time swap:** `Surface::SetTheme(const Theme*)` (the surface does not own themes; the application does) bumps a revision; widgets re-resolve lazily on the next draw, with no rebuild of the tree and no layout change unless a metric changed (metrics changing marks layout dirty).
- [ ] **Built-ins:** `DefaultLightTheme()` and `DefaultDarkTheme()` built in code (contrast ratio of text on background checked >= 4.5:1 in a test, so the two themes are not accidentally unreadable); both are ordinary `Theme` values, no special mode.
- [ ] **Custom widgets read the same mechanism:** `WidgetKind` is an open registry (`RegisterWidgetKind(name)` returns a stable id) so a game's own widget gets its style entries from the theme data; a theme can be built from code now and from a data file later (Phase 20 owns the format; the `Theme` struct has no loader here, only `ThemeBuilder`).
- [ ] **Tests (`tests/ui/theme_test.cpp`):** fallback order through all four levels; variant lookup and the missing-variant diagnostic; scale rounding at 1x, 1.25x, 2x; swap revision invalidates the cache; contrast of the built-ins; two themes give different resolved colors for the same widget state; registry stability of kind ids.
- [ ] **Tests are written with the code:** every public behavior above has a test; GPU tests carry the `gpu` tag, use `FOR_EACH_GPU_BACKEND` and skip a backend that has no shaders (Vulkan only until Day 202 mints DXIL); every new test file has its line in `tests/phases.txt` (phase 15).
- [ ] **Runs (CLAUDE.md "Tests run by phase"):** `scripts/test-phase.sh --affected` plain on Linux; then the asan and tsan flavors when the day touched lifetime, memory or concurrency code; quote each run's final `ran X of Y tests (phases ...)` line in this file. `ctest -L docs` when a public header or a guide changed. Build clean (warnings are errors) with Clang and GCC, unity on and off.
- [ ] **Two machines (CLAUDE.md "Windows is a supported platform"):** shared code is built and tested on the Windows PC before the phase closes (Day 202); a day done on one machine files "open until the other machine runs it" in `KNOWN-ISSUES.md`.
- [ ] **Documentation:** public headers carry `///` comments with the contract labels (`docs/DOC-CONVENTIONS.md`); `docgen lint` stays at zero; `cmake --build build --target docs` output committed; the guide page `docs/guide/ui.md` gains the day's section (tagged samples compile and run); new modules are in `docs/reference/modules.txt`.
- [ ] **Module rules:** `ARCHITECTURE.md`, `tools/module_deps_check/modules.txt` and `BUILD.md` updated in the same commit when a module, an edge or a prerequisite changed; `module_deps_check`, `platform_neutral_check`, `std_container_check`, `file_access_check` and `config_access_check` pass.
- [ ] **End-of-day review:** `/code-review high` over the day's diff; verify, fix (with a regression test that fails without the fix) or decline each finding with a reason; record the outcome under "Post-implementation code review" below and in `STATUS.md`.
- [ ] `STATUS.md` and `KNOWN-ISSUES.md` updated in the same commit; commit and push.

**Done:** a widget can ask the theme for its look by kind, variant and state at any scale, a subtree can override it, and swapping the surface's theme restyles everything on the next draw.

## Notes

(filled in as the day goes)

## Post-implementation code review

(filled in at the end of the day)
