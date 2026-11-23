# Phase 28 / Day 495 — The Instance Set

**Phase:** [Phase 28 — Extra Rendering Features](../phase-28-extra-rendering-features.md)
**Depends on:** Day 494, Phase 18 (spatial).

The data structure behind instanced meshes: many transforms, chunked, one owner.

## Tasks

- [ ] `renderer::InstanceSet` (a class owned by `UniquePtr`, not a shared unit unless the spec found two owners): instances in the layout Day 494 picked, stored in chunks of the picked size ordered by a Morton or Hilbert key of their position, each chunk with its bounding box and its first-instance index; a stable `InstanceId` (chunk index and slot, plus a generation) that survives other instances' removal.
- [ ] Operations: `Add(transform, data)`, `Remove(id)` (swap with the chunk's last element, update the id map), `SetTransform(id, t)` (moves between chunks only when it leaves the chunk's bound by more than a threshold, otherwise grows the bound; a periodic `Rebuild` re-sorts by key), `SetData(id, ...)` for the per-instance colour or parameters, `Clear`, with the chunk bounds kept exact and a version counter per chunk so the GPU upload knows which chunks changed.
- [ ] Capacity limits: at most 2^24 instances per set (the index travels as a `f32`-exact or a `u32` in the buffer, the spec says which), checked with a status, never an abort; memory per instance recorded (40 to 48 bytes plus data).
- [ ] Per-instance data: a `Vec4` colour tint and a `u32` custom parameter in a second array, optional (a set declares at creation whether it has them).
- [ ] Determinism: for the same operations in the same order the chunk layout is the same on every run; `Rebuild` sorts with a stable sort on (key, id).
- [ ] Tests: add, remove and set 100,000 random instances and compare with a brute-force model after each batch; bounds contain all their instances exactly (min and max checked); ids of survivors stay valid after removals; generation checks refuse a stale id; `Rebuild` leaves the set of instances unchanged and improves (does not worsen) the sum of chunk volumes on a random scatter.
- [ ] **Runs (CLAUDE.md "Tests run by phase"):** `scripts/test-phase.sh --start-day` at the start; `scripts/test-phase.sh --affected` plain at the end, then the asan and tsan flavors (this day touches lifetime or concurrency code; tsan GPU tests are skipped by the existing environment switch); quote the final `ran X of Y tests (phases ...)` line in this file and never say "all tests pass" for a reduced run. `ctest -L docs` when a public header or a guide changed. Build clean (warnings are errors) with both compilers and unity on and off.
- [ ] **Module rules (CLAUDE.md):** a new module, target or edge updates `ARCHITECTURE.md`, `tools/module_deps_check/modules.txt`, `tests/module_map.txt` and `BUILD.md` in the same commit; every new test file has its line in `tests/phases.txt` (phase 28); only wrapped types, no `std`, no raw platform macros (`build_defs.h`); code formatted by `scripts/format.sh <files>`.
- [ ] **Documentation (CLAUDE.md "Public API is documented before a day is done"), when a public header changed:** `///` comments with the contract labels, `docgen lint` at zero, `cmake --build build --target docs` output committed, the guide page updated; published pages name no phases or days. No tutorials before the last day.
- [ ] **Day review (CLAUDE.md, required):** `/code-review high` over the day's diff after the scoped tests; verify, fix (with a regression test shown to fail without the fix) or decline each finding with a reason; record the count and outcome under "Post-implementation code review" below and in `STATUS.md`.
- [ ] `STATUS.md` and `KNOWN-ISSUES.md` updated in the same commit; commit and push (standing permission).

**Done:** `InstanceSet` holds and edits a million instances with exact chunk bounds and stable ids, proven against a brute-force model.

## Notes

- Large worlds (Day 494 spec, the user's decision): each chunk stores an `f64` origin and its instances `f32` offsets from it; the public API takes `f32` positions and converts at the boundary; a test places a set 10,000 km from the origin and checks the offsets and bounds are exact. The record and the chunk size are the spec's: 40 bytes, 256.

- Written 2026-11-23 with the phase plan; the user's decisions are in `plans/phase-28-extra-rendering-features.md` ("Decisions").

## Post-implementation code review

(To be filled in when the day is done.)
