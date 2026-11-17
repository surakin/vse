# Phase 24 / Day 381 — Windows: Off-Screen Surfaces and Multi-Material Meshes (Vulkan and Direct3D 12)

**Phase:** [Phase 24 — Scenes for Hosts Through the C API](../phase-24-c-api-scenes-for-hosts.md)
**Depends on:** Days 373-380 built on Linux; Phases 13, 23 Day 354. **Done on the Windows PC.**

Both features on both Windows backends.

## Tasks

- [ ] Build and run every test of Days 374-380 on `windows-msvc` (plain and asan) and `windows-clang-cl` (plain), shared and static; fix what differs.
- [ ] Direct3D 12: the off-screen target, the copy into the readback ring (a readback heap, fences per slot, row pitch alignment 256 bytes converted to the stride the API promises), the device-removed path; Vulkan: the same through the Linux code.
- [ ] Golden images of the multi-material scene and the off-screen acceptance scene on both backends under the tolerance of Day 358; differences between the backends are measured and recorded, not hidden by a larger tolerance.
- [ ] The Win32 C host's walk-around sample draws its off-screen mode (Day 383 compiles here).
- [ ] Record in `KNOWN-ISSUES.md` what could not be done and anything that moved from Linux.
- [ ] **Runs (CLAUDE.md "Tests run by phase"):** `scripts/test-phase.sh --affected` plain; then the asan and tsan flavors when the day touched concurrency, lifetime or memory code (GPU tests are a skipped stub under tsan, `KNOWN-ISSUES.md`); quote each run's final `ran X of Y tests (phases ...)` line in this file and never say "all tests pass" for a reduced run. `ctest -L docs` when a public header or a guide changed. Build clean (warnings are errors) with both compilers and unity on and off; `-DENGINE_BUILD_SHARED=ON` and `-DENGINE_LIBRARY_WITH_IMPORTER=OFF` still build.
- [ ] **Day review (CLAUDE.md, required):** `/code-review high` over the day's diff after the scoped tests; verify, fix (with a regression test shown to fail without the fix) or decline each finding with a reason; record the count and outcome under "Post-implementation code review" below and in `STATUS.md`.
- [ ] `STATUS.md` and `KNOWN-ISSUES.md` updated in the same commit; commit and push.

**Done:** The Day 374-380 tests pass on the Windows PC on both backends, or each failure is recorded with its observed behavior.
