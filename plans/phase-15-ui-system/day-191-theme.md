# Phase 15 / Day 191 — Themes: Data, Resolution, Variants, Run-Time Swap

**Phase:** [Phase 15 — UI System](../phase-15-ui-system.md)
**Depends on:** Day 189 (colors and metrics are drawn through the list); `docs/superpowers/specs` from Day 184.

The first widget is written against the theme API, so the theme comes before any widget.

## Tasks

- [x] **`ui/theme.h`:** a `Theme` is plain data: named colors (`text`, `text_disabled`, `window_background`, `panel`, `accent`, `danger`, ...), metrics (padding, spacing, border width, corner radius, scrollbar thickness, minimum hit size, icon size), fonts (`FontRef` = family key + size in logical units, resolved by the surface's font provider), and **style entries**: per widget kind and **variant name** (`button/primary`, `label/heading`) a `Style` with a state table (normal, hover, pressed, focused, disabled, checked) of background, border, text color, radius and padding.
- [x] **Resolution:** `ResolvedStyle Resolve(Theme chain, WidgetKind, Variant, StateBits)` walks the widget's theme chain (a widget's own override -> the nearest ancestor's override -> the surface's theme -> the built-in default theme); a value left unset falls to the next; unknown variant falls back to the kind's default style and counts a `missing_style` diagnostic (logged once per name). Resolution is pure and cached per (theme revision, kind, variant, state).
- [x] **Scale:** all metrics and font sizes are logical units; `Resolve` multiplies by the surface's scale factor and rounds borders and paddings to whole physical pixels (so a 1-unit border is 1 pixel at 1x and 2 at 2x, never 1.5).
- [x] **Run-time swap:** `Surface::SetTheme(const Theme*)` (the surface does not own themes; the application does) bumps a revision; widgets re-resolve lazily on the next draw, with no rebuild of the tree and no layout change unless a metric changed (metrics changing marks layout dirty).
- [x] **Built-ins:** `DefaultLightTheme()` and `DefaultDarkTheme()` built in code (contrast ratio of text on background checked >= 4.5:1 in a test, so the two themes are not accidentally unreadable); both are ordinary `Theme` values, no special mode.
- [x] **Custom widgets read the same mechanism:** `WidgetKind` is an open registry (`RegisterWidgetKind(name)` returns a stable id) so a game's own widget gets its style entries from the theme data; a theme can be built from code now and from a data file later (Phase 21 owns the format; the `Theme` struct has no loader here, only `ThemeBuilder`).
- [x] **Tests (`tests/ui/theme_test.cpp`):** fallback order through all four levels; variant lookup and the missing-variant diagnostic; scale rounding at 1x, 1.25x, 2x; swap revision invalidates the cache; contrast of the built-ins; two themes give different resolved colors for the same widget state; registry stability of kind ids.
- [x] **Tests are written with the code:** every public behavior above has a test; GPU tests carry the `gpu` tag, use `FOR_EACH_GPU_BACKEND` and skip a backend that has no shaders (Vulkan only until Day 205 mints DXIL); every new test file has its line in `tests/phases.txt` (phase 15).
- [x] **Runs (CLAUDE.md "Tests run by phase"):** `scripts/test-phase.sh --affected` plain on Linux; then the asan and tsan flavors when the day touched lifetime, memory or concurrency code; quote each run's final `ran X of Y tests (phases ...)` line in this file. `ctest -L docs` when a public header or a guide changed. Build clean (warnings are errors) with Clang and GCC, unity on and off.
- [x] **Two machines (CLAUDE.md "Windows is a supported platform"):** shared code is built and tested on the Windows PC before the phase closes (Day 205); a day done on one machine files "open until the other machine runs it" in `KNOWN-ISSUES.md`.
- [x] **Documentation:** public headers carry `///` comments with the contract labels (`docs/DOC-CONVENTIONS.md`); `docgen lint` stays at zero; `cmake --build build --target docs` output committed; the guide page `docs/guide/ui.md` gains the day's section (tagged samples compile and run); new modules are in `docs/reference/modules.txt`.
- [x] **Module rules:** `ARCHITECTURE.md`, `tools/module_deps_check/modules.txt` and `BUILD.md` updated in the same commit when a module, an edge or a prerequisite changed; `module_deps_check`, `platform_neutral_check`, `std_container_check`, `file_access_check` and `config_access_check` pass.
- [x] **End-of-day review:** `/code-review high` over the day's diff; verify, fix (with a regression test that fails without the fix) or decline each finding with a reason; record the outcome under "Post-implementation code review" below and in `STATUS.md`.
- [x] `STATUS.md` and `KNOWN-ISSUES.md` updated in the same commit; commit and push.

**Done:** a widget can ask the theme for its look by kind, variant and state at any scale, a subtree can override it, and swapping the surface's theme restyles everything on the next draw.

## Notes

- **Where:** `engine/ui/theme.{h,cpp}` (`Theme`, `ThemeBuilder`, `ThemeSet`, `ResolvedStyle`, the kind/variant registries, `DefaultLightTheme`/`DefaultDarkTheme`, `ContrastRatio`), `tests/ui/theme_test.cpp` (9 cases, 83 assertions), the "Themes" section of `docs/guide/ui.md`.
- **Calls made:** there is no `Surface` yet (Day 192), so `ThemeSet` is the piece a surface will hold (`SetTheme`, `Revision`, cache); `Surface::SetTheme` will forward to it. Variants are ids (`RegisterVariant`) like kinds, so a style entry is trivially copyable. `Resolve` returns a `ResolvedStyle` whose sizes are physical; the radius is scaled but not rounded. A style with overrides is not cached (the override can change without a revision); the surface-only chain is. `SetTheme` returns whether any metric or font changed so the caller marks layout dirty.
- **Runs (reduced, session locked):** plain `ran 1597 of 3988 tests (phases: all; flavors: plain; display: included; crash tests: excluded)` (the two known locked-session X11 failures), asan `ran 1103 of 3988 tests (...asan...)`, tsan `ran 794 of 3988 tests (...tsan...)`; `ctest -L docs` passes; Clang, GCC, unity and non-unity clean. `raw_new_check` caught a leaked-singleton `new` in the first version; the registries and default themes are function-local statics now.

## Post-implementation code review

Run with `/code-review high`: 3 findings.

- **Fixed:** the name registries never forget a name, so varying text grows them; the header now says to register a fixed set of names.
- **Declined:** styles with overrides are not cached (an override can change without a revision; Day 192's widget tree can cache per widget).
- **Declined:** `SetTheme` reports metric/font changes of the surface theme only; the owner of an override marks layout dirty itself.
