# Phase 15 / Day 206 — Guide, Benchmark, Closing Runs, Phase Review

**Phase:** [Phase 15 — UI System](../phase-15-ui-system.md)
**Depends on:** Days 184-205.

Document, measure, run everything on both machines, review the whole phase, close it.

## Tasks

- [x] **Guide `docs/guide/ui.md` complete:** the model (surface, tree, event flow, frame cycle, coordinates and scale), a complete "hello UI" sample (tagged, compiles and runs on an offscreen surface with the harness), each widget family with a sample, theming (data, variants, overrides, swap), layout, input routing and focus, popups and modality, the viewport widget and mouse look, UI surfaces and the ray-cast input recipe, the settings screen, the debug overlay, reset and loss, the limits (no docking, no IME, no accessibility tree, no file dialogs, no clipboard history or clipboard-manager persistence); `docs/guide/README.md`, `renderer.md`, `building-and-testing.md` and `platform` guides updated where they mention input or the new tests.
- [x] **Reference:** `docgen lint` zero; reference regenerated and committed (identical on both machines); `docs/reference/modules.txt` covers `ui`, `ui_window` and the new renderer and platform headers.
- [x] **Benchmark** `game/renderer_bench` section `ui`: draw-list build for 1,000 / 5,000 widgets, layout time for the same trees, UI renderer record cost, list box with 100,000 items per frame, steady-state allocation count; recorded in the guide and this file (no thresholds in tests).
- [x] **Project documents:** `PLAN.md` (the roadmap row, a progress-log entry, the assumption list if the platform libraries or the pointer-lock choices need one), `CLAUDE.md` if a rule changed (the `ui` module line, the platform event stream, theme rule "no literal colors in widgets" if it became a gate), `BUILD.md` (any new library), `ARCHITECTURE.md` (the measured graph), `STATUS.md`.
- [x] **Known-issues sweep:** every Phase 15 deferral filed (no docking, no IME, no clipboard-manager persistence, no drag images on X11 without a compositor, no accessibility, no file dialogs, no scroll inertia, tree view deferred to Phase 31 if the editor needs it, DX12 and Win32 items as they stand after Day 205); entries this phase fixed removed.
- [x] **Closing full runs (CLAUDE.md, required for a phase's last day):** Linux `scripts/test-phase.sh --all --flavor all`, `ctest -L docs`, `ui_demo` and the other demos 1000 frames with validation on Wayland and X11; Windows `scripts/test-phase.sh --all --flavor all` (plain and asan), `ctest -L docs`, the demos 1000 frames on DX12 and Vulkan; record both final `ran X of Y tests (phases ...)` lines here; nothing left out. **Linux half done (see Notes and Runs); the Windows half done on 2026-11-12 (below).**
- [x] **End-of-phase review (CLAUDE.md, uncapped):** `/code-review high --max-findings all` over the whole phase's diff (the commit before Day 184 to the working tree); verify, fix or decline each finding with a reason; record the count and outcome under "Post-implementation code review".
- [x] `plans/phase-15-ui-system.md` (status complete), `PLAN.md`, `STATUS.md` updated; `CURRENT_PHASE` stays 15 until Phase 17 has its day files and starts; commit and push.

**Done:** the guide and reference are current and identical on both machines, the closing full-run lines for Linux and Windows are recorded, the uncapped review is recorded, and Phase 15 is marked complete.

## Notes

**Guide.** `docs/guide/ui.md` gained a runnable "hello UI" (`cpp compile run standalone`, with its `output` block; `docgen` now links `ui` for samples on both platforms), "Reset and loss", "Cost" (the benchmark table) and "Limits"; `docs/guide/building-and-testing.md` gained "Testing a UI". `ctest -L docs` 7 of 7, reference regenerated (6603 symbols, 100%).

**Benchmark** (`renderer_bench --section ui`, Release, AMD Ryzen 9 7950X3D, Linux, CPU only, nothing asserted). Rows of ten buttons in a vertical box, a surface as tall as the tree:

| | 1,000 widgets | 5,000 widgets |
|---|---|---|
| build the tree | 0.5 ms | 1.6-2.1 ms |
| first frame (measure, arrange, draw, glyphs into the atlas) | 7.6 ms | 32-34 ms |
| relayout and repaint | 0.49 ms | 2.4 ms |
| repaint only | 0.42 ms | 2.1 ms |
| draw list | 32,000 vertices / 48,000 indices | 160,000 / 240,000 |
| allocations, 20 idle frames / 20 repaint frames | 0 / 0 | 0 / 0 |

List box over 100,000 items: 3.7 us per frame scrolling inside laid-out rows (0 allocations over 200 frames), 28 us per frame with three new rows per frame. `UiRenderer` graph build + execute over the 1,000-button list on the null device: 25-28 us (no driver cost). The Debug build is about 4x slower (first frame 19 ms for 1,000 widgets).

**Sentry warnings (user report).** 16 `'_Generic' is a C11 extension` warnings from sentry-native's C99 build against glibc's `memchr`/`strchr` macros; fixed by building sentry as C11 around its `add_subdirectory` (commit b1ebebe).

**Windows half.** The user did Day 205 on the Windows PC; this day built and tested those commits on Linux (four builds clean, scoped runs plain `ran 1927 of 4928`, asan `ran 1411 of 4928`, no failures). **The closing full run on Windows (`--all --flavor all` plain and asan, `ctest -L docs`, the demos on DX12 and Vulkan) was not run after this day's changes**; the Windows lines are Day 205's (plain `ran 1746 of 3398`, asan `ran 1385 of 3398`). It is listed in `KNOWN-ISSUES.md`.

**Windows half, run on 2026-11-12 (Windows PC):** four trees clean (after three unrelated fixes, `KNOWN-ISSUES.md`); `ran 4551 of 4551 tests (phases: all; flavors: all (plain and asan, no tsan on Windows); display: included; crash tests: included) in 413 s`;
`ctest -L docs` 7 of 7; `rhi_demo`, `renderer_demo` and `ui_demo` `--frames 1000 --validation` on DX12 and Vulkan, all exit 0 with no validation
message and no memory growth. Twelve display tests (five clipboard and drag-and-drop tests and the Alt key test, plain and asan) failed in the run and in
three reruns while the console session was locked; they wait for a rerun on an unlocked desktop (`KNOWN-ISSUES.md`), so the task stays open.
**Done later on 2026-11-12:** with the desktop unlocked, all twelve passed (`ctest -R "Windows clipboard|Windows drag and drop|the left Alt is Alt"`
in `build-msvc`: 26 of 26, plain and asan, the other clipboard and drag-and-drop tests included), and so did the whole scoped run of the Windows catch-up
after the Mac's Phase 26 Day 311 (`ran 4807 of 5084 tests (phases: all; flavors: all (plain and asan, no tsan on Windows); display: included; crash tests: excluded) in 379 s`, no failure). The Windows half of the closing run is complete.

## Runs

- Linux closing run, before the review fixes: `ran 4928 of 4928 tests (phases: all; flavors: all; display: included; crash tests: included) in 138 s`, 100% passed. `ctest -L docs` 7 of 7. Demos `ui_demo`, `renderer_demo`, `rhi_demo`, 1000 frames, `--validation --no-vsync`, Wayland and X11: exit 0, no warnings or errors.
- After the review fixes, the closing run on the final tree: `ran 4952 of 4952 tests (phases: all; flavors: all; display: included; crash tests: included) in 147 s`, 100% passed; `ctest -L docs` 7 of 7.

## Post-implementation code review

The uncapped end-of-phase review (`/code-review high --max-findings all`, CLAUDE.md) covered the whole Phase 15 diff (68ff250..HEAD, 303 files, +74,270 lines; 180 files in `engine`, `game`, `tools`). It was run as five independent reviewers by area (platform; `ui` core; `ui` widgets; renderer/RHI/text/shaders; demos, foundation, gates and tests), each with the high-effort recipe and told to report only what it verified by reading: about 66 candidate findings. Each was checked against the code. **Fixed (with a test that fails without the fix where practical):** `SettingsView::Rebuild` hung during a dispatch (test confirmed to hang without the fix); unclamped `snprintf` lengths in `DebugOverlay::Run` (two places), `ui_script.cpp` and five places of `ui_demo_app.cpp`; `TabView::ShowPage` kept a `Tab&` across the page factory; a drop-down never scrolled to its selection when selected before layout (test); `ThemeSet::SetTheme` ignored style padding and font changes (test); `PlacePopup` ran off the far edge when neither side fit (test); `SurfaceInput::Text` mis-split a stray continuation byte (test); `ReadLogLinesSince` could pass a line whose writer had not claimed its slot (reasoned, not testable without a hook); a text field deleted its selection on an empty paste and took control characters (test); a spin box swallowed the wheel at its limit; a `WindowFrame` and a `Splitter` kept drag state when the capture was lost; the demo reported a refused change as applied, forgot the rendering dialog's choices (test) and ran past an unfinished script silently (now a warning); Wayland clipboard deadlines used a stale clock and `start_drag` used the wrong serial; X11 swallowed key releases during a drag; the Windows drop leaked a non-HGLOBAL `STGMEDIUM` (**not built or run here**); `(void)` workarounds against the CLAUDE.md rule; stale DXIL comments; an orphaned doc comment. **Declined with a reason:** the background of `UiSurfaceTarget` is correct as straight alpha (`ui.frag` multiplies; a transparent-white test passes both ways; the test now uses transparent white); the claim that a focused text field makes a dialog's Escape unreachable (`dialog_test` expects the opposite and passes); `DebugOverlay::Attach` leaving the overlay attached when the chord is taken is its documented contract; the editable combo box focus claim could not be reproduced in a test and the edit was reverted; one UI pass per frame per renderer cannot be asserted (no per-frame identity: graphs share an address across frames). **Filed in `KNOWN-ISSUES.md` instead of fixed** (each needs a design change or hardware I do not have here): closed popups stay hit-testable for later events of the same batch; layout containers re-measure outside a subtree theme override; focus after a modal opened from a menu; stray `PointerUp` after a cancelled drag; stale widget ids after a removal; the platform state findings (a new keymap or lost seat capability forgets held keys silently, Win32 capture loss without button-up, Wayland `ClearImage` and pointer-lock cursor, X11 oversized property and lock motion, Win32 clipboard set order, Wayland offer eviction and data-device version); `unsigned char` in the X11 `.inc` files (the gate does not scan `.inc`); the new `tests/foundation` window tests that still spell `std::vector`/`unsigned char` (the gate counts per directory only in prose); gate `ui_widget_style_check` gaps; and smaller items (list box selection cleared on item-count change, scroll bar fade end, menu rows after item changes, text field `scroll_x_`, drop-move atomicity, `Callback::Invoke` allocation, `SortLayers` worst case, `SurfaceUpdater` drift). The list is in `KNOWN-ISSUES.md` under Phase 15 Day 206.

Result: 66 candidates, 21 groups fixed, 5 declined, the rest filed. After the fixes: Clang and GCC, unity on and off, build clean (0 warnings); scoped plain `ran 1935 of 4952 tests (phases: all; flavors: plain; display: included; crash tests: excluded)` (one failure, `X11Window translates key press/release into Key via the server's keymap`, which fails about 2 runs in 11 alone even with this day's X11 change reverted), asan `ran 1419 of 4952 tests (phases: all; flavors: asan; display: included; crash tests: excluded)`, tsan `ran 1101 of 4952 tests (phases: all; flavors: tsan; display: included; crash tests: excluded)`, all passed.
