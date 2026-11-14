# Phase 22 / Day 337 — Settings, the No-Tools-In-The-Game Gate & the `asset_demo`

**Phase:** [Phase 22 — Asset Pipeline & Resource System](../phase-22-asset-pipeline.md)
**Depends on:** Days 313-333; Phase 5 and Phase 17 (the renderer and the scene bridge, for the demo).

Close the loops: every knob a setting, a gate that keeps tool code out of the game, and a demo that uses it all.

## Tasks

- [ ] **Animated demo (added 2026-11-13):** `asset_demo` shows a skinned character minted from text sources (a 20-joint skeleton, a skinned mesh, an idle clip and a walk clip, built by a checked-in generator script of `tools/asset_mint` fixtures so the sources are small text files), with the two clips blended by a slider, root motion applied, the joint palette budget shown in the HUD, and `--frames` mode comparing the final frame with a golden (the settings of Days 321, 322 and 320 are all reachable from `--set`).
- [ ] **Settings finished** (`assets.*`): mounts (a list of `prefix=backend:arg` entries), `strict`, `cache_dir`, `cache_bytes`, `max_inflight`, `max_queued`, `stream_budget_us`, `stream_budget_bytes`, `net.timeout_ms`, `net.max_retries`, `net.max_read_bytes`; env names `ENGINE_ASSETS_*`, flags `assets-*`; `config_access_check` stays clean (no ad hoc `getenv` or flags).
- [ ] **Gate `asset_boundary_check`** (`tools/asset_boundary_check`, ctest): no target under `game/` or `engine/` links `asset_mint`'s library or includes a header from `tools/`; the minters' encoders live in `tools/asset_mint/` and the tests of the minters may link it; the runtime library `engine/asset` contains only decoders and loaders (the BC *decoders* are runtime, the *encoders* are tools) and no upgrade code (no symbol of the upgrade chains is linked into `asset_demo`: checked on the binary's symbol table), so a game cannot convert an old blob at load. Allow list with a reason per entry, stale entry fails, planted-violation self-test.
- [ ] **`game/asset_demo`:** mints a small source tree generated at start (meshes, textures in BC1/BC7/RGBA8, materials, a level in text form) into a temporary directory, packs it, then loads the level from the loose mount, from the pak and from a `resource_server` on loopback (three runs, same scene), renders each headless to a screenshot and compares them pixel for pixel; `--list-backends`, `--simulated` (network over the simulated stream, no sockets); ctests `asset_demo_runs`, `asset_demo_simulated_runs`, `asset_demo_screenshots_match`.
- [ ] **Load-quality assertions in the demo:** each of the three runs prints `load_conversions` (must be 0), the bytes copied beyond the read and the upload (must be 0 for meshes and textures) and the load time; the ctests fail otherwise.
- [ ] **Dependency demo and gate:** `asset_demo --break` removes one dependency at each depth from the mounted project and expects a clean `MissingDependency` with the chain in the message and nothing half-loaded; `asset_boundary_check` also fails when engine or game code loads a blob by a raw `ResourceId` literal or builds a `ResourceId` from a path at run time (ids come from minted data).
- [ ] **Device reset in the demo:** `--reset-after N` resets the device mid-run and the scene returns identical.
- [ ] **Hot reload of loose files** (development): `Watch` plus the manager reloads a changed texture or material in place (handles stay valid); tested with a modified file on disk.
- [ ] **`docs/tutorials/loading-a-level.md`:** mint a project, load it from a pak and from a server; compiled sample.
- [ ] **Runs (CLAUDE.md "Tests run by phase"):** `scripts/test-phase.sh --affected` plain; then the asan and tsan flavors when the day touched concurrency, lifetime or memory code; quote each run's final `ran X of Y tests (phases ...)` line in this file and never say "all tests pass" for a reduced run. `ctest -L docs` when a public header or a guide changed. Build clean (warnings are errors) with Clang and GCC, unity on and off. New code uses the engine's wrappers and types, never `std` (`std_usage_check`); file access only through `io::` (`file_access_check`); settings only through `settings::Registry` (`config_access_check`).
- [ ] **Three machines (CLAUDE.md "Three machines"):** shared code is portable and checked on the Windows PC and the Mac no later than Day 339; until then the day is "open until the other machines run it" in `KNOWN-ISSUES.md`.
- [ ] **Documentation (CLAUDE.md "Public API is documented before a day is done"):** public headers carry `///` comments with the contract labels (`docs/DOC-CONVENTIONS.md`); `docgen lint` stays at zero; `cmake --build build --target docs` output committed; new modules are added to `docs/reference/modules.txt`; the guide page `docs/guide/assets.md` is updated and its tagged samples compile and run.
- [ ] **Module rules (CLAUDE.md "Module dependencies follow ARCHITECTURE.md"):** a new module or edge updates `ARCHITECTURE.md`, `tools/module_deps_check/modules.txt` and the CMake targets (with `_asan`/`_tsan` flavors) in the same commit; no upward edge, no cycle; every new test file has its line in `tests/phases.txt` (phase 22).
- [ ] **Day review (CLAUDE.md, required):** `/code-review high` over the day's diff after the scoped tests; verify, fix (with a regression test that fails without the fix) or decline each finding with a reason; record the count and outcome under "Post-implementation code review" below and in `STATUS.md`.
- [ ] `STATUS.md` and `KNOWN-ISSUES.md` updated in the same commit; commit and push.

**Done:** `asset_demo` renders the same level from three backends with identical pixels, survives a device reset, and the boundary gate keeps tool code out of the runtime.

## Outcome

(written when the day is done)

## Post-implementation code review

(written when the day is done)
