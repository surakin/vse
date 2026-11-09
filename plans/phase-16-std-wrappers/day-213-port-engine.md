# Phase 16 / Day 213 — Port the Rest of `engine/` (platform, rhi, renderer, text, ui)

**Phase:** [Phase 16 — Standard-Library Wrappers](../phase-16-std-wrappers.md)
**Depends on:** Day 212.

The same port for the remaining engine modules: `platform` 12 files, `rhi` 45, `renderer` 25, `text` 3, `ui` 30, `ui_window`, `jobs` non-coroutine uses (coroutines are Day 215). Windows-only files wait for Day 219.

## Tasks

- [x] Port `engine/platform` (Linux backends, common), `engine/rhi` (neutral, null, Vulkan), `engine/renderer`, `engine/text`, `engine/ui`, `engine/ui_window`, `engine/jobs`: C-string, memory, formatting, language-support uses.
- [x] Vulkan and Wayland/X11 C API boundaries keep their own C headers (`<vulkan/vulkan.h>`, `<wayland-client.h>`, `<X11/Xlib.h>` are not the standard library); only `std::` and standard includes move.
- [x] Replace `std::getenv`/`std::abort`/`std::signal` uses found here with the Day 214 names only after Day 214; this day leaves them and lists them in the day file result.
- [ ] Goldens and demos: `rhi_demo`, `renderer_demo`, `ui_demo` 1000 frames with `--validation` on Wayland and X11 (the ports touch formatting of shader and debug names).
- [x] Scoped tests plain and asan; `ctest -L docs` (public headers change).
- [x] **Day review (CLAUDE.md, required):** scoped tests (`scripts/test-phase.sh --affected`, plain, then asan and tsan where the day touched concurrency or memory code), then `/code-review high` on the day's diff; verify, fix or decline each finding with a reason and record the count and outcome under "Post-implementation code review"; `STATUS.md` and `KNOWN-ISSUES.md` updated; commit and push.

**Done:** these modules contain no `std::` and no standard include except `<cmath>`/`<chrono>`/process/signal uses recorded for Day 214 and the Windows-only files; demos run clean on both display backends.

## Result

- Ported with Day 212 (one script run). `std::getenv`/`abort`/`signal` were ported on Day 214 (below). **Not done:** the three demos at 1000 frames with validation on Wayland and X11, deferred to the Day 221 closing run (which runs the demos the phase ships).
