# Phase 12 / Day 143 — Log Configuration, OS Primitives, Placement and `engine_main`

**Phase:** [Phase 12 — Module Layering](../phase-12-module-layering.md)
**Depends on:** Day 142.

Remove the remaining upward edges inside the foundation (`ARCHITECTURE.md` section 2, items 2, 4 and 6, and the two placement notes), after which the
foundation's include graph has no cycle except through the window code that Day 144 moves out.

## Tasks

- [ ] **Log configuration:** find every caller of `log::LoadConfig` (and of the `<Channel>.verbosity/enabled` file form); since Phase 11 made the channels
      settings, either retire the file form (callers use `--set log.*` or a settings file) or move its file reading into `foundation_settings`; `log/log.cpp` no
      longer includes `io/`. Any user-visible change (a flag or file that stops working) is listed in the day file and the guide, with the replacement.
- [ ] **`StringView` to the base layer (spec D3):** new `string_view.h` in the foundation root, `string/string.h` includes it; `log/log.h`, `os/os.h`,
      `int128.h`, `containers/span.h` include it instead of `string/string.h`; `int128.cpp` includes `log/assert.h`, not `log/log.h`. Removes items 11, 12, 13.
- [ ] **OS primitives below strings:** `os/os.h` stops including `string/string.h` and `mem/heap_allocator.h`: paths are passed as `StringView` moved to the base
      layer (or as pointer and length — the spec's choice), `EnvironmentEntry` writes into a caller buffer and reports the needed size; every caller (the settings
      environment source, the log, the debug facilities, tests) is ported; the signal-safe callers are unchanged in behavior (`signal_safety_check` passes).
- [ ] **Placement:** `algorithm.h` is mapped to layer 4 (or moved under `containers/`, with a forwarding header only if the spec allows one); `int128.h`'s
      `FromText` either moves to the string module or `int128.h` is mapped to layer 4 — whichever keeps the base layer free of strings; record the choice.
- [ ] **`engine_main`:** moved to `game/hello_window/` (its CMake target, the window test that runs it, docs) or retired if the demos cover everything it shows
      (decide with a list of what it does that the demos do not); `engine/foundation/` then holds no program.
- [ ] **Allow list:** entries for items 2, 4 and 6 deleted; `module_deps_check` passes; the gate now reports no cycle inside the foundation except through the
      window files (item 5, Day 144).
- [ ] **Tests:** every test unchanged except for signature ports; new tests for `EnvironmentEntry`'s caller-buffer contract (too small, exact, empty) and for the
      log-configuration replacement if the file form was retired.
- [ ] **Documentation:** `os.h` (the new signatures, `Fails:` for a short buffer), the log guide (configuration), `docs/guide/platform-os.md`; reference
      regenerated; `docgen lint` zero; `ctest -L docs`.
- [ ] **Runs (Linux):** `scripts/test-phase.sh --affected` plus asan and tsan (`os.h` and the log are under everything); quote the lines.
- [ ] **End-of-day review:** `/code-review high`. Extra scrutiny: buffer-size contracts at every ported caller, the signal-safe paths that used `String`, any
      configuration a user relied on that changed.
- [ ] Update `STATUS.md`, `KNOWN-ISSUES.md`; commit and push.

**Done:** the foundation's only remaining cycle runs through the window code, `os.h` depends on nothing above the base layer, no program lives in a library
directory, and three more allow-list entries are gone.
