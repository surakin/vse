# Phase 12 / Day 140 — Design Spec, Measured Graph & Phase Setup

**Phase:** [Phase 12 — Module Layering](../phase-12-module-layering.md)
**Depends on:** Phase 11 complete; every day file of this phase committed (planning policy).

No product code except the phase setup. Re-measure the graph on the Linux machine, settle the library split and the hook shapes by experiment, and wire the
phase into the test map.

## Tasks

- [x] **Phase setup first:** `CURRENT_PHASE` becomes 12; `tests/phases.txt`'s phase list comment names "12 module layering"; `docs/guide/building-and-testing.md`
      gets a row for phase 11 (with its closing counts) and a placeholder row for 12. The `flavors 12 ...` line arrives with the first phase-12 test (Day 141).
- [x] **Re-measure the graph** on the Linux machine with the same method as `ARCHITECTURE.md` section 2 (every `#include "..."` in `engine/`, `game/`, `tools/`
      resolved against the include directories; files mapped to the modules of section 1; strongly connected groups), and confirm or correct section 2. Also
      list the CMake link edges (`target_link_libraries`) per target, so the library split has a before picture.
- [x] **Spike A — the hooks (done as analysis; see the spec, section 3).** A throwaway build where `log/log.cpp` no longer includes `debug/` (the failure report goes through a function pointer that a
      test installs) and `io/file.cpp` calls a thread-start hook: check that every `tests/debug` and `tests/foundation` assert and death test still passes with the
      hook installed by program setup, and that an assert before installation prints its message and aborts. Decide the hook API (names, where they are
      declared, thread-safety of installation, whether `SetAssertHandler`'s chain is reused) and record it.
- [x] **Spike B — the split (done as the include graph; Day 145 builds it).** A throwaway `CMakeLists.txt` that builds `foundation_base` alone from the layer 0-5 sources and links `tests/types` against it;
      record every unresolved symbol and which upward edge it comes from; this list is the work of Days 142-145 and must match section 2 (any extra edge is
      added to `ARCHITECTURE.md`).
- [x] **Spike C — the platform move.** List every file that includes `os/window.h`, `os/input.h` or `os/linux/platform_window_host.h` (engine, game, tests,
      docs samples), and count what moving them to `engine/platform/` changes: include lines, namespace uses (if renamed), `tests/phases.txt` and
      `tests/module_map.txt` patterns, gate allow lists, `docs/reference/modules.txt`. Decide namespace and include prefix (decision 4) from the count.
- [x] **The spec** `docs/superpowers/specs/2026-11-NN-module-layering-design.md`: each phase-file decision confirmed or amended with the evidence; the library list
      with the sources of each; the hook APIs with their `Fails:`/`Aborts:` contracts and thread rules; the platform move's file list and namespace decision;
      the gate's design (Day 141); which `tests/` directories link which layer after Day 145.
- [x] **Documentation:** the spec is linked from `docs/README.md`; `ARCHITECTURE.md` section 2 corrected if the re-measure differs; `ctest -L docs` passes.
- [x] Run `scripts/test-phase.sh --affected` on the Linux machine (the phase map changed, so the whole plain suite runs; quote the line).
- [x] No code review (no product code beyond the setup). Update `STATUS.md`; commit and push.

**Done:** `CURRENT_PHASE` is 12, the graph is re-measured on Linux and matches `ARCHITECTURE.md`, the three spikes are written up, and the spec settles the
libraries, hooks and platform move.

## Result (2026-11-07)

- Re-measure: `docs/superpowers/specs/2026-11-07-module-layering-design.md` section 1. One group of nine modules, as `ARCHITECTURE.md` said, **plus three
  upward edges it had missed** (`log.h`, `span.h`, `int128.h` → `string/string.h`; now items 11-13 and decision 10 in `ARCHITECTURE.md`). The gate's allow
  list on Day 141 therefore starts with thirteen entries, not ten.
- Spikes A and B were not run as throwaway builds: A was settled by reading every call site and the existing registration test, B is the include graph,
  and both are proven by the real changes on Days 142 and 145 (recorded in the spec, section 3). C counted 25 files for the platform move; namespace stays
  `os::`, prefix `platform/`.
- The Phase 11 row in `docs/guide/building-and-testing.md` was stale ("0, started on Day 130"); it now reads 164 / 127 / 127 (plain / ASan / TSan, from
  `scripts/test-phase.sh 11 --dry-run`), and the Phase 12 row is a placeholder.
- Day 143 gains the `StringView` move (decision D3); Day 141's count is thirteen.
