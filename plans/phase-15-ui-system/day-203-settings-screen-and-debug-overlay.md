# Phase 15 / Day 203 — Settings Binding and the Debug Overlay

**Phase:** [Phase 15 — UI System](../phase-15-ui-system.md)
**Depends on:** Days 195-199; Phase 11 (`settings::Registry`, `Settings`); Phase 14 (`TextRenderer`).

Two consumers of the toolkit that prove it on real content: a settings screen generated from the registry, and a debug overlay.

## Tasks

- [x] **Settings binding (`ui/settings_view.h`):** `BuildSettingsPanel(Settings&, Registry&, filter)` creates the widgets from registry metadata: group headings (`group`), a labelled row per setting (`label`, `description` as tooltip), the widget by type (bool -> `CheckBox`, int/float with a range -> `Slider` + `SpinBox`, enum -> `ComboBox` over the enum names, string -> `TextField`), with ranges and `RangePolicy` from the metadata; `kHidden` is skipped, `kAdvanced` collapsed under an Advanced section, `kSecret` shown as a password field, `kRestartRequired` flagged with an icon/text and collected for a restart prompt.
- [x] **Writes and rollback:** a change goes through `Settings::Apply`; a rejected change rolls the widget back and shows the reason as an inline message; a value pinned by the environment or command line (`Settings::Provenance`) is shown disabled with the source as a tooltip; the widgets refresh from `Settings` change listeners and `OnChange`, never by polling (a test counts reads over 100 idle frames: zero).
- [x] **Live example (moved to Day 204, see Notes):** the `renderer_demo` rendering settings (present mode, render scale, shader quality) change from the panel at run time through `Renderer::ApplySettings`, with the cost class (dynamic, pipeline rebuild, swapchain, device reset) displayed next to the control from the Phase 8 classification.
- [x] **Debug overlay (`ui/debug_overlay.h`):** a small tree of widgets (frame time graph from a ring of samples, FPS, job stats from the scheduler's counters, draw/pass counts from the renderer's `Stats`, GPU memory) toggled by a key, drawn in the top layer; a **log console** widget (scrolling `ListBox` over a ring of recent log lines from a log sink, level filter, a command line `TextField` that calls a registered command table) - the immediate-mode-style milestone of the phase file, built on the same retained widgets (the phase file's rule: fold, do not duplicate).
- [x] **Log sink:** `ui::LogCapture` registers a sink with the log core, copies lines into a bounded ring without allocating on the logging thread beyond the ring (documented), and the console reads it on the main thread; no UI call from other threads (UI is main-thread only).
- [x] **Tests (`tests/ui/settings_view_test.cpp`, `debug_overlay_test.cpp`):** a test registry with every type and flag - widgets by type, hidden skipped, advanced collapsed, secret masked, restart flagged; applying a change calls `Apply` once; a rejected value rolls back and shows the message; provenance disables; an external change refreshes the widget without polling; log console receives lines from another thread (thread-safe ring, tsan clean), filter, command execution; the frame-time graph ring.
- [x] **Tests are written with the code:** every public behavior above has a test; GPU tests carry the `gpu` tag, use `FOR_EACH_GPU_BACKEND` and skip a backend that has no shaders (Vulkan only until Day 205 mints DXIL); every new test file has its line in `tests/phases.txt` (phase 15).
- [x] **Runs (CLAUDE.md "Tests run by phase"):** `scripts/test-phase.sh --affected` plain on Linux; then the asan and tsan flavors when the day touched lifetime, memory or concurrency code; quote each run's final `ran X of Y tests (phases ...)` line in this file. `ctest -L docs` when a public header or a guide changed. Build clean (warnings are errors) with Clang and GCC, unity on and off.
- [x] **Two machines (CLAUDE.md "Windows is a supported platform"):** shared code is built and tested on the Windows PC before the phase closes (Day 205); a day done on one machine files "open until the other machine runs it" in `KNOWN-ISSUES.md`.
- [x] **Documentation:** public headers carry `///` comments with the contract labels (`docs/DOC-CONVENTIONS.md`); `docgen lint` stays at zero; `cmake --build build --target docs` output committed; the guide page `docs/guide/ui.md` gains the day's section (tagged samples compile and run); new modules are in `docs/reference/modules.txt`.
- [x] **Module rules:** `ARCHITECTURE.md`, `tools/module_deps_check/modules.txt` and `BUILD.md` updated in the same commit when a module, an edge or a prerequisite changed; `module_deps_check`, `platform_neutral_check`, `std_container_check`, `file_access_check` and `config_access_check` pass.
- [x] **End-of-day review:** `/code-review high` over the day's diff; verify, fix (with a regression test that fails without the fix) or decline each finding with a reason; record the outcome under "Post-implementation code review" below and in `STATUS.md`.
- [x] `STATUS.md` and `KNOWN-ISSUES.md` updated in the same commit; commit and push.

**Done:** a settings screen is generated from registry metadata and changes rendering settings live with rollback and provenance, and a debug overlay with a log console runs on the same widgets.

## Notes

- **`ui::SettingsView` (a class, not the planned `BuildSettingsPanel` function).** A widget that owns its rows can rebuild and refresh itself, hold the listener and answer `RowOf/ControlOf/SpinOf/RestartNoteOf`; a free function would have left that state with the caller. `description` is the row's tooltip; a pinned row's tooltip says "Set by <layer> <origin>" instead.
- **Settings listener prefix.** `Settings::AddListener` matches a key or the keys *below* it, so the prefix `ui.` never matched anything; the view now strips the closing dot for the listener. The test "A change made elsewhere shows up without polling" failed before that fix (the spin box stayed at 1280).
- **Log console: a reader, not a sink (deviation from the task text).** The log core already keeps the last 256 lines in a lock-free seqlock ring that every thread writes. A second sink copying into a second ring would duplicate it, so `ReadLogLinesSince(next, callback, context)` was added to `foundation/log` (thread-safe, async-signal-safe like `ReadRecentLogLines`, which now shares its ring-reading helper) and `ui::LogCapture` polls it on the UI thread into a bounded ring (512 lines). Nothing on the logging thread changed, so there is no allocation to document there. Lines that pass a channel's threshold are the only ones in the ring.
- **Filter is a text filter, not a level menu.** The lines carry their level name, so typing `WARN` filters by level; a level drop-down is not built.
- **Live `renderer_demo` example not done on Day 203.** The settings panel changing present mode, render scale and shader quality through `Renderer::ApplySettings` needs a windowed demo with the cost class from Phase 8 shown next to each control; it moves to Day 204's `ui_demo` (recorded in `KNOWN-ISSUES.md`). The view takes the apply function as a callback so that glue is a few lines there.
- **Overlay placement.** `DebugOverlay::Attach` adds the panel to the surface root and places itself at the top right in `Arrange`; it is not in a separate top layer object, which draws last among the root's children, but a popup or tooltip still draws above it.
- **Unity builds:** the file-local helper names in `debug_overlay.cpp` are distinct from `settings_view.cpp`'s (`OwnedText`, `TrimmedText`, `HasText`).

## Runs

- Plain: `ran 1890 of 4855 tests (phases: all; flavors: plain; display: included; crash tests: excluded) in 76 s; the last full run took 105 s`
- ASan: `ran 1392 of 4855 tests (phases: all; flavors: asan; display: included; crash tests: excluded) in 53 s; the last full run took 105 s`
- TSan: `ran 1076 of 4855 tests (phases: all; flavors: tsan; display: included; crash tests: excluded) in 11 s; the last full run took 105 s`
- `ctest -L docs` passes (docgen lint zero); gates `ui_widget_style_check`, `raw_new_check`, `std_container_check`, `module_deps_check`, `platform_neutral_check`, `file_access_check`, `config_access_check`, `test_phase_labels` pass; Clang and GCC, unity on and off, clean.
- The live `renderer_demo` settings example and the Windows build are not done (see Notes and `KNOWN-ISSUES.md`); their boxes above are checked for the parts that were done.

## Post-implementation code review

`/code-review high`: 6 findings, 5 fixed, 1 declined.

- **Fixed:** a text row's unsubmitted edit was overwritten by every refresh and lost on focus loss (now kept while focused, committed when the focus leaves; test "A text edit in progress survives..."). The console jumped to the newest line on every refresh (now follows only while the user is at the bottom; test "The console keeps its place...", confirmed failing without the fix). `LogCapture` found the end of the log with an undocumented `~0` (now `LogLinesWritten()`). `Refresh` took a snapshot per row (one per refresh). The command echo was cut at 256 bytes (now 1024).
- **Declined:** `LogCapture::Clear` can leave a line a writer is copying at that instant; it appears after the clear. Recorded in `KNOWN-ISSUES.md`.
- **Found before the review, by the tests:** the log reader skipped a line a writer had claimed but not finished and moved past it, losing lines under concurrent logging (`ReadLogLinesSince` now stops at a pending line; test "The log capture follows a thread..." failed 80 of 80 runs without the fix, 0 of 200 with it). The settings listener prefix `ui.` never matched (`AddListener` takes a key without the dot).
