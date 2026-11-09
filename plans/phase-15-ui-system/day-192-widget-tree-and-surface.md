# Phase 15 / Day 192 — Widget Tree, Surface and the Offscreen Test Harness

**Phase:** [Phase 15 — UI System](../phase-15-ui-system.md)
**Depends on:** Days 189, 191.

The retained tree, the surface that owns it, and the harness every widget test uses - an offscreen surface from the first widget (the phase file's design constraint).

## Tasks

- [ ] **`ui/widget.h`:** `Widget` base class with `UniquePtr` children, a parent pointer, an id (hash of the parent id and a name or index), `name`, visibility, enabled state, `state bits` (hover, pressed, focused, disabled, checked), dirty flags (`kLayoutDirty`, `kDrawDirty`) that propagate to the root, `AddChild`/`RemoveChild`/`FindById`/`FindByName`, `Draw(Canvas&)` (the abstract canvas of Day 189; a widget never names `DrawList`) and `Measure`/`Arrange` hooks (filled on Day 193), `OnEvent(Event&)`, `Update(dt)`. Tree mutation during event dispatch is deferred to the end of the dispatch (a widget may remove itself in its own click handler).
- [ ] **`ui/surface.h`:** `Surface` owns the root widget, size in logical units, `scale` factor, the clock (`SetTime(seconds)`, `dt` derived), the theme pointer, the font provider, the focus owner, the pointer capture owner and the popup layer; **no `os::Window` anywhere**: a window feeds a surface (Day 194's adapter), a test or an in-world screen feeds it directly.
- [ ] **`ui/event.h`:** `ui::Event` (pointer move/down/up/enter/leave with position in the receiving widget's local space, wheel, key down/up with repeat, text, focus gained/lost, `Tick`), `ui::Key` as an alias of `os::Key` (Day 184 amendment: no second enum), a `handled` flag, timestamps from the surface clock.
- [ ] **Frame cycle:** `Surface::Frame(dt)` = apply queued events -> `Update` of animating widgets (hover fades are not planned; only caret blink, tooltips and scroll inertia need time) -> layout if dirty -> `BuildDrawList(DrawList*)` (a `DrawListCanvas` over the list is made here and handed to the widgets as a `Canvas&`) (draw only if dirty or the list is empty; `needs_redraw` is what an on-demand offscreen surface reads on Day 202).
- [ ] **Hit testing and order:** children draw in order and are hit-tested in reverse; a widget can mark itself `pass_through` (does not take hits, its children do); clip is respected by hit testing (a widget scrolled out of its view is not hit).
- [ ] **`ui/testing/harness.h`:** `Harness` builds an offscreen `Surface` of a given size and scale, a built-in test font provider (Liberation Sans through the Phase 14 module), helpers to send pointer/key/text events, advance the clock, run `Frame`, find a widget by name, read its rectangle, and render the draw list to a CPU image through a small software rasterizer (rects, rounded rects approximated, text from the atlas bitmaps) so most widget tests compare pixels or rectangles without a GPU.
- [ ] **Memory and lifetime:** destroying a widget with focus, capture or hover clears those references on the surface before it dies; a test removes each while it holds the state; no raw owning pointers (`UniquePtr`), `std_container_check` clean.
- [ ] **Tests (`tests/ui/widget_tree_test.cpp`, `surface_test.cpp`):** id stability across rebuilds with the same names; add/remove during dispatch; dirty propagation; hit-test order and clipping; surface without any window object compiles and runs (the offscreen test is the proof); focus/capture cleared on removal; the software rasterizer draws a rect at the right pixels (its own test, so the harness can be trusted).
- [ ] **Tests are written with the code:** every public behavior above has a test; GPU tests carry the `gpu` tag, use `FOR_EACH_GPU_BACKEND` and skip a backend that has no shaders (Vulkan only until Day 205 mints DXIL); every new test file has its line in `tests/phases.txt` (phase 15).
- [ ] **Runs (CLAUDE.md "Tests run by phase"):** `scripts/test-phase.sh --affected` plain on Linux; then the asan and tsan flavors when the day touched lifetime, memory or concurrency code; quote each run's final `ran X of Y tests (phases ...)` line in this file. `ctest -L docs` when a public header or a guide changed. Build clean (warnings are errors) with Clang and GCC, unity on and off.
- [ ] **Two machines (CLAUDE.md "Windows is a supported platform"):** shared code is built and tested on the Windows PC before the phase closes (Day 205); a day done on one machine files "open until the other machine runs it" in `KNOWN-ISSUES.md`.
- [ ] **Documentation:** public headers carry `///` comments with the contract labels (`docs/DOC-CONVENTIONS.md`); `docgen lint` stays at zero; `cmake --build build --target docs` output committed; the guide page `docs/guide/ui.md` gains the day's section (tagged samples compile and run); new modules are in `docs/reference/modules.txt`.
- [ ] **Module rules:** `ARCHITECTURE.md`, `tools/module_deps_check/modules.txt` and `BUILD.md` updated in the same commit when a module, an edge or a prerequisite changed; `module_deps_check`, `platform_neutral_check`, `std_container_check`, `file_access_check` and `config_access_check` pass.
- [ ] **End-of-day review:** `/code-review high` over the day's diff; verify, fix (with a regression test that fails without the fix) or decline each finding with a reason; record the outcome under "Post-implementation code review" below and in `STATUS.md`.
- [ ] `STATUS.md` and `KNOWN-ISSUES.md` updated in the same commit; commit and push.

**Done:** a tree of widgets lives in a surface that needs no window, frames run from a test, and a harness can drive input and read rectangles and pixels without a GPU.

## Notes

(filled in as the day goes)

## Post-implementation code review

(filled in at the end of the day)
