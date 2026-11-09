# Phase 16 / Day 220 — The `std_usage_check` Gate and the Rule

**Phase:** [Phase 16 — Standard-Library Wrappers](../phase-16-std-wrappers.md)
**Depends on:** Days 207-219.

Replace `std_container_check` with a gate for every standard header, so the rule cannot erode.

## Tasks

- [ ] `tools/std_usage_check` (ctest `std_usage_check`): fails on any `#include <...>` of a standard header, any `std::` token, and any unqualified C-library name from the Day 207 list (`strlen`, `memcpy`, `printf`, ...) in `engine/`, `game/`, `tools/`, `tests/`, outside its allow list; comments and string literals ignored; `third_party/` and docgen input fixtures excluded by path.
- [ ] The allow list is **per module and header**: `engine/foundation/lang/** : <utility>,<type_traits>,<limits>,<new>,<initializer_list>,<compare>,<concepts>,<cstddef>,<climits>`, `sync/** : <atomic>,<mutex>,<condition_variable>,<thread>`, `mem/** : <cstring>,<cstdlib>`, `cstr/** : <cstring>,<cwchar>,<cstdio>,<cstdarg>,<cctype>,<cstdlib>`, `os/** : <cmath>…` and so on, as built on Days 208-215; every entry has a reason, a stale entry fails, and a file may include only the headers of its own module's list.
- [ ] Documented boundaries: `engine/foundation/types.h` (`<cstdint>`), `tools/shader_mint/reflect_json.*` and its test, `tests/support/catch_engine_types.h`, oracle test files for the wrappers (listed by name).
- [ ] Self-tests with planted violations (each category, an unqualified C name, a header in the wrong module, a stale entry, a missing reason) and a clean fixture; the gate runs in under a few seconds.
- [ ] Remove `tools/std_container_check` and its allow list (its checks are included: the container names are in the new gate's list); keep a pointer in `docs/guide/building-and-testing.md`.
- [ ] Update `CLAUDE.md`: the "Zero third-party convenience dependencies" bullet no longer lists concurrency primitives, `<cmath>`, `<chrono>`, the C library as allowed, the container bullet's "Allowed:" sentence becomes the module table with the wrapper names, and a new section "The standard library is used only through the wrapper modules (required)"; `PLAN.md` assumption #2 and #23 final text; memory note updated.
- [ ] **Day review (CLAUDE.md, required):** scoped tests (`scripts/test-phase.sh --affected`, plain, then asan and tsan where the day touched concurrency or memory code), then `/code-review high` on the day's diff; verify, fix or decline each finding with a reason and record the count and outcome under "Post-implementation code review"; `STATUS.md` and `KNOWN-ISSUES.md` updated; commit and push.

**Done:** the gate passes with a per-module allow list and no `tests/` directory entry, its self-tests pass, `CLAUDE.md` and `PLAN.md` state the new rule, and `std_container_check` is gone.
