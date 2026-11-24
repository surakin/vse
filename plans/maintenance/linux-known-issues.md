# Maintenance: known issues Linux can fix

This is not a numbered phase: it is a maintenance series of eight days, proposed on 2026-11-24 after Phase 28 closed (it does not change `CURRENT_PHASE` or any phase or day number; it starts on its own first day when the user says so).

**Origin:** `KNOWN-ISSUES.md` (774 lines) mixes three kinds of entries: what needs a person (a screen, a keyboard, a decision), what needs the Windows PC or the Mac, and what Linux can fix. This series is the third kind: the findings of Phase 28's uncapped review that were recorded and not fixed, the limits the Phase 28 days listed, and the failing and flaky tests seen on Linux. A handful of leftovers from Phases 25 to 27 that need no other machine are in Day 07. Windows- and macOS-only entries, the X11 window tests that need an unlocked session, and everything that waits for the user's look stay where they are.

**Depends on:** Phase 28 complete (it is, on Linux, 2026-11-24).

**Decisions asked of the user before the days that need them:** (1) the level format change for terrain and instanced meshes (Day 04's last task); (2) raising `ENGINE_ABI_VERSION_MINOR` (Day 08); (3) a script-visible `AudioSource` (Day 07).

**Goal:** the entries above are gone from `KNOWN-ISSUES.md`, each by a fix with a test shown to fail before it, or are rewritten with what was found.

**Definition of done:** every day's boxes are checked; the whole suite passes on Linux in every flavor; physics hashes re-baselined by Days 03 and 05 are listed for the other machines; the review's findings are resolved; `docgen lint` is at zero and `ctest -L docs` passes.

## Days

1. [Terrain correctness: tiles, seams, drop order and the height map's checks](linux-known-issues/01-terrain-correctness.md)
2. [Terrain in the C API and scripts: lifetimes, queries and the cube sky](linux-known-issues/02-terrain-in-the-c-api.md)
3. [The height-field collider: contacts, recovery and snapshots](linux-known-issues/03-height-field-physics.md)
4. [Instance sets and trees: bookkeeping, costs and the C API's missing data](linux-known-issues/04-instances-and-hierarchies.md)
5. [Sky, cube textures and the shader reflection](linux-known-issues/05-sky-cube-and-reflection.md)
6. [Failing and flaky tests seen on Linux](linux-known-issues/06-linux-test-failures.md)
7. [Audio, scripting and physics leftovers that Linux can fix](linux-known-issues/07-audio-and-scripts-leftovers.md)
8. [Closing: docs, runs, review and the entries](linux-known-issues/08-closing-docs-runs-and-review.md)
