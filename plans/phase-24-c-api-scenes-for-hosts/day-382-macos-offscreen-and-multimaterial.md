# Phase 24 / Day 382 — macOS: Off-Screen Surfaces and Multi-Material Meshes (Metal)

**Phase:** [Phase 24 — Scenes for Hosts Through the C API](../phase-24-c-api-scenes-for-hosts.md)
**Depends on:** Days 373-380 built on Linux; Phase 21; Day 381. **Done on the Mac.**

Both features on Metal.

## Tasks

- [ ] Pull and build `macos-clang` (x86-64; plain tests only for now) and `macos-clang-arm64` compile-only in both unity modes; Metal code only under `engine/rhi/metal/` and `*/macos/`.
- [ ] Metal: the off-screen colour target, the blit into the ring (shared-storage buffers, a completion handler per slot), the row layout, `@autoreleasepool` at every entry point; validation on (`MTL_DEBUG_LAYER=1`...).
- [ ] Golden images on Metal under the tolerance of Day 358; the Metal-versus-Vulkan differences recorded.
- [ ] The Cocoa C host's walk-around sample draws its off-screen mode.
- [ ] Record in `KNOWN-ISSUES.md` what could not be done; the Mac console must be unlocked for window tests.
- [ ] **Day review (CLAUDE.md, required):** `/code-review high` over the day's diff after the scoped tests; verify, fix (with a regression test shown to fail without the fix) or decline each finding with a reason; record the count and outcome under "Post-implementation code review" below and in `STATUS.md`.
- [ ] `STATUS.md` and `KNOWN-ISSUES.md` updated in the same commit; commit and push.

**Done:** The Day 374-380 tests pass on the Mac on Metal, or each failure is recorded with its observed behavior.

## Outcome

Skipped on the Linux machine by the user (2026-11-17): the user runs the Mac later. Nothing here was done; see `KNOWN-ISSUES.md`.
