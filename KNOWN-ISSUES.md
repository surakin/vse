# Known issues

Failing or flaky tests and deferred items, in one place. `STATUS.md` says where the plan is; this file says what is
known to be wrong or unfinished. Remove an entry when it is fixed (the commit that fixes it removes it), and add one
whenever a day leaves something broken, unmeasured or deferred. Entries state facts that were observed, with where.

## Failing tests

| Test | Since | Cause | Status |
|---|---|---|---|
| `X11Window::Create opens a mapped window on the real X server` | Phase 4 Day 69 session | Environmental: the session's Xwayland never maps windows. Reproduced with a plain Xlib program, so it is not engine code. | Fails on every run. Owner will fix the environment in a later pass over failing tests. |

## Flaky tests

Seen failing once or intermittently, passing on rerun. Cause not investigated.

- X11 key-translation tests.
- Wayland resize-storm test (once, under load).

## Unverified by a human

Automated checks pass; nobody has looked at the real thing yet (from `STATUS.md`, Phases 1-2).

- Visual confirmation of the Phase 1 windows, live keyboard and mouse updates, and the real close-button exit on Wayland and X11 (Days 19-26).
- `fr`/`de` keyboard layouts through a live X server (xkbcommon tests cover them).
- Phase 4 Day 70: vsync-paced `rhi_demo` frame time was 50 ms on Wayland and 17 ms on X11 against 4.1 ms in Phase 3 in that session. The cause is believed to be compositor pacing (negligible shader cost with `--no-vsync`) but it was not re-measured after a session restart.

## Deferred items

| Item | Needed by | Where recorded |
|---|---|---|
| Pipeline creation time for permuted shaders was not measured; specialization constants are the Vulkan-only escape hatch if profiling asks for it | Only if profiling shows a problem | `docs/superpowers/specs/2026-11-04-renderer-design.md` R10.1 |
| `shader_mint --permute` starts `slangc` with `posix_spawnp` (Linux only) | Phase 12 (Windows) | `shaders/README.md`, Day 72 day file |
| Clip-space Y flip in the shader path (Slang does not apply it) | Phases 12-13 | `shaders/README.md`, RHI portability review |
| Metal renames an entry point called `main` | Phase 13 | `shaders/README.md` |
| Other items filed per backend (typeless depth, descriptor copies, `ClampToBorder`, ...) | Phases 12-13 | RHI portability review, section 5 |
| Epic's UE EULA terms need review | Before Phase 16 starts | `PLAN.md` (open risk) |
