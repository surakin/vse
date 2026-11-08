# Phase 14 / Day 182 — Hello World in the Demos, Reset and Loss, Golden Images

**Phase:** [Phase 14 — Font & Text Rendering](../phase-14-font-text-rendering.md)
**Depends on:** Day 181.

Show it, lock it down with golden images, and prove it survives everything the renderer survives.

## Tasks

- [ ] **`renderer_demo`:** draws "Hello, world!" and a status line (frame time, GPU name, backend) with the shipped font; a key (F11) toggles the overlay text, shown in the demo's key list; `--text "..."` (a launch-only
      option, listed in `config_access_check`'s allow list entry for the demo) replaces the greeting; the string "Héllo wörld — 你好 🙂" is drawn when the font lacks glyphs, showing `.notdef` boxes without a crash.
- [ ] **Golden images** for the text pass on Vulkan (Linux, Windows) and DX12 (Windows): "Hello, world!" at 16 and 48 px, a wrapped paragraph, a line with kerned pairs; stored under `tests/renderer/golden/`
      with the repo's comparison tolerance (the Phase 4 golden machinery); regenerated and reviewed by eye on both machines before being committed.
- [ ] **Reset and loss:** `Renderer::ApplySettings` changes of every cost class with text on screen leave the picture identical (a golden compare after each); a forced device loss recovers text (the Phase 8
      loss tests extended); the memory checks (`device_loss_recovery_test.cpp`) still pass with the atlas present.
- [ ] **Unicode checks by eye** (a person looks and records it in this file): accents, a combining mark, an emoji ZWJ sequence, the CJK string; the screenshot names are kept in the scratchpad notes of the day.
- [ ] **Settings audit:** if the spec made any text option a setting (default font path, UI scale), it is declared with `settings::Registry` and read through `Setting<T>` (`config_access_check` clean); otherwise the
      day says "none" and why.
- [ ] **Tests:** the golden tests above, the reset test through `reset_rig.h`, a demo script test (`renderer_demo --script`) that types the F11 toggle, plus phase-label entries (`tests/phases.txt`).
- [ ] **Runs (CLAUDE.md "Tests run by phase"):** `scripts/test-phase.sh --affected` plain on Linux and Windows, asan on both; then the asan and tsan flavors if the day touched lifetime, memory or concurrency code;
      quote each run's final `ran X of Y tests (phases ...)` line in this file. `ctest -L docs` when a public header or a guide changed. Build clean (warnings are errors) with
      Clang and GCC, unity on and off.
- [ ] **Two machines (CLAUDE.md "Windows is a supported platform"):** shared code is built and tested on the Windows PC before the day closes; a day done on one machine files
      "open until the other machine runs it" in `KNOWN-ISSUES.md` and the closing day clears it.
- [ ] **Documentation:** public headers carry `///` comments with the contract labels (`docs/DOC-CONVENTIONS.md`); `docgen lint` stays at zero; `cmake --build build --target docs` output committed.
- [ ] **End-of-day review:** `/code-review high` over the day's diff; verify, fix (with a regression test that fails without the fix) or decline each finding with a reason; record the outcome under
      "Post-implementation code review" below and in `STATUS.md`.
- [ ] `STATUS.md` and `KNOWN-ISSUES.md` updated in the same commit; commit and push.

**Done:** the demo shows "Hello, world!" on Wayland, X11 and (Windows) Win32 with Vulkan and DX12; golden images pass on every backend present; reset and loss keep the text; the by-eye checks are recorded.

## Notes

(filled in as the day goes)

## Post-implementation code review

(filled in at the end of the day)
