# Phase 12 / Day 143 — Log Configuration, OS Primitives, Placement and `engine_main`

**Phase:** [Phase 12 — Module Layering](../phase-12-module-layering.md)
**Depends on:** Day 142.

Remove the remaining upward edges inside the foundation (`ARCHITECTURE.md` section 2, items 2, 4 and 6, and the two placement notes), after which the
foundation's include graph has no cycle except through the window code that Day 144 moves out.

## Tasks

- [x] **Log configuration:** find every caller of `log::LoadConfig` (and of the `<Channel>.verbosity/enabled` file form); since Phase 11 made the channels
      settings, either retire the file form (callers use `--set log.*` or a settings file) or move its file reading into `foundation_settings`; `log/log.cpp` no
      longer includes `io/`. Any user-visible change (a flag or file that stops working) is listed in the day file and the guide, with the replacement.
- [x] **`StringView` to the base layer (spec D3):** new `string_view.h` in the foundation root, `string/string.h` includes it; `log/log.h`, `os/os.h`,
      `int128.h`, `containers/span.h` include it instead of `string/string.h`; `int128.cpp` includes `log/assert.h`, not `log/log.h`. Removes items 11, 12, 13.
- [x] **OS primitives below strings:** `os/os.h` stops including `string/string.h` and `mem/heap_allocator.h`: paths are passed as `StringView` moved to the base
      layer (or as pointer and length — the spec's choice), `EnvironmentEntry` writes into a caller buffer and reports the needed size; every caller (the settings
      environment source, the log, the debug facilities, tests) is ported; the signal-safe callers are unchanged in behavior (`signal_safety_check` passes).
- [x] **Placement:** `algorithm.h` is mapped to layer 4 (or moved under `containers/`, with a forwarding header only if the spec allows one); `int128.h`'s
      `FromText` either moves to the string module or `int128.h` is mapped to layer 4 — whichever keeps the base layer free of strings; record the choice.
- [x] **`engine_main`:** moved to `game/hello_window/` (its CMake target, the window test that runs it, docs) or retired if the demos cover everything it shows
      (decide with a list of what it does that the demos do not); `engine/foundation/` then holds no program.
- [x] **Allow list:** entries for items 2, 4 and 6 deleted; `module_deps_check` passes; the gate now reports no cycle inside the foundation except through the
      window files (item 5, Day 144).
- [x] **Tests:** every test unchanged except for signature ports; new tests for `EnvironmentEntry`'s caller-buffer contract (too small, exact, empty) and for the
      log-configuration replacement if the file form was retired.
- [x] **Documentation:** `os.h` (the new signatures, `Fails:` for a short buffer), the log guide (configuration), `docs/guide/platform-os.md`; reference
      regenerated; `docgen lint` zero; `ctest -L docs`.
- [x] **Runs (Linux):** `scripts/test-phase.sh --affected` plus asan and tsan (`os.h` and the log are under everything); quote the lines.
- [x] **End-of-day review:** `/code-review high`. Extra scrutiny: buffer-size contracts at every ported caller, the signal-safe paths that used `String`, any
      configuration a user relied on that changed.
- [x] Update `STATUS.md`, `KNOWN-ISSUES.md`; commit and push.

**Done:** the foundation's only remaining cycle runs through the window code, `os.h` depends on nothing above the base layer, no program lives in a library
directory, and three more allow-list entries are gone.

## Result (2026-11-07)

- `string_view.h` (base layer) holds `StringView`; `string/string.h` includes it. `log/log.h`, `os/os.h`, `int128.h` and `containers/span.h` include it instead of
  `string/string.h`. (`int128.cpp` already included only `log/assert.h`, which the gate maps to the base layer.) Two files that had relied on the transitive include
  (`rhi/vulkan/vk_instance.cpp`, `tests/types/span_test.cpp`) include `containers/vector.h` / `string/string.h` themselves.
- `os::EnvironmentEntry(index, buffer, capacity, &length)` writes into the caller's buffer and reports the full length (`Fails:` false when out of range; a short buffer is
  not a failure, the caller retries with `length + 1`). `os.h` includes no `mem/` or `string/` header. `settings/env_source.cpp` is the only caller (stack buffer, heap
  retry). `tests/foundation/os_timing_test.cpp` tests exact fit, one byte short, capacity 0 and out of range.
- **`log::LoadConfig` retired** (user-visible): its only callers were its own test; nothing loaded a file. The replacement is the settings system (`--set
  log.<channel>.verbosity=...`, a settings file, `ENGINE_SETTING_<KEY>`); `docs/guide/settings.md` and the logging guide say so. `log.cpp` no longer includes `io/`.
- `algorithm.h` is mapped to layer 4 in the gate (it uses `Span` and the heap allocator), no move needed.
- `engine_main` moved with `git mv` to `game/hello_window/` (targets `hello_window`, `hello_window_asan`); no test ran it, the demos do not show the input readout, so it was
  kept. It still includes the per-platform header (allow-list entry moved to Day 144).
- Allow list: `log -> io`, `os -> string`, `os -> mem`, `log -> string`, `containers -> string`, `base -> string` and the `engine_main` entry deleted (`hello_window` entry added, Day 144).
  The foundation's only remaining cycle ingredient is the window code (Day 144).
- Runs: plain `--affected --crashes` = `ran 1527 of 3488 tests (phases: all; flavors: plain; display: included; crash tests: included)`, ASan `ran 965 of 3488`, TSan `ran 682 of 3488`;
  the only failures are the two known Wayland/NVIDIA demo tests. `ctest -L docs` passes (reference regenerated, `string_view.h` added to the strings module).
  Not run: a `-DENGINE_UNITY_BUILD=OFF` build (Day 145 does both modes).

## Post-implementation code review

Inline high-effort review. Found and fixed: a local variable named `small` in `env_source.cpp` (a macro in the Windows headers) renamed. Checked: every ported `EnvironmentEntry` caller handles a
short buffer; the signal-safe code does not use the function. Declined: none.
