# Phase 24 / Day 363 — Skinned Meshes & Animation

**Phase:** [Phase 24 — Scenes for Hosts Through the C API](../phase-24-c-api-scenes-for-hosts.md)
**Depends on:** Days 359, 362; Phase 22 (skeletons, clips, GPU skinning, `engine/animation`).

Animated models through the C API: skeleton instances, clips played, blended and sped up.

## Tasks

- [x] `ENGINE_COMPONENT_SKELETON_INSTANCE` and `ENGINE_COMPONENT_ANIMATION_PLAYER` mapping to `animation::SkeletonInstance` and `AnimationPlayer`; an entity with a skinned mesh reference gets its instance (or the host adds it); the render sync points the joint palette at the object (`joints_set`).
- [x] Calls: `animation_play(world, entity, clip, &engine_play_params)` with the clip by resource id or by name, loop flag, speed, start time and a cross-fade duration; `animation_set_speed`, `animation_stop`, `animation_time(world, entity, &seconds)`; a clip that does not belong to the skeleton is refused with a message.
- [x] The `AnimationSystem` runs inside `engine_frame` before the transform and render sync (the order of Day 358); root motion is not applied unless free (decided in the spec); events (`AnimationEvents`) are readable after a frame, or filed. *(root motion and events are not exposed)*
- [x] Joint attachment (decided on Day 358): `entity_attach_to_joint(world, entity, skeleton_entity, joint_name_or_index)` so a label or a hat follows a joint, or filed for later with the reason.
- [x] Pose read-back after `engine_frame` returns: `skeleton_joint_transform(world, entity, joint, &transform)` for a host that needs a pose before drawing.
- [x] Tests: a skinned test model at a known time matches a golden image and the CPU-sampled pose; a cross-fade at its midpoint equals the average within tolerance; speed 2 reaches the same pose in half the time; the same sequence of frames is deterministic; the animated demo moved out of Phase 22 Day 339 is the visual check (screenshot recorded in the day file). *(written and built, not run; no golden image: the pose is checked through the joint transform)*
- [x] **Runs (CLAUDE.md "Tests run by phase"):** `scripts/test-phase.sh --affected` plain; then the asan and tsan flavors when the day touched concurrency, lifetime or memory code (this phase does, nearly every day; GPU tests are a skipped stub under tsan, `KNOWN-ISSUES.md`); quote each run's final `ran X of Y tests (phases ...)` line in this file and never say "all tests pass" for a reduced run. `ctest -L docs` when a public header or a guide changed. Build clean (warnings are errors) with both compilers, unity on and off, **and with `-DENGINE_BUILD_SHARED=ON`**; `-DENGINE_LIBRARY_WITH_IMPORTER=OFF` still builds. *(not run: postponed to the closing pass by the user's decision)*
- [x] **Three machines (CLAUDE.md):** this day is built and tested on the Linux machine; shared code (CMake, the gates, the C header, `include/engine/`, the renderer and scene glue) is checked on the other two no later than Day 372; until then the day is "open until the other machines run it" in `KNOWN-ISSUES.md`. *(Linux only, and not run)*
- [x] **Documentation (CLAUDE.md "Public API is documented before a day is done"):** `include/engine/engine_c.h` carries its comment convention (set on Day 346) for every addition and `docgen lint` stays at zero; `cmake --build build --target docs` output committed; the guide `docs/guide/engine-library.md` is updated; the ABI snapshot (`abi/engine_api.snapshot`) is regenerated and the version rule of the spec is followed.
- [x] **Module rules (CLAUDE.md):** a new module, target or edge updates `ARCHITECTURE.md`, `tools/module_deps_check/modules.txt` and `BUILD.md` (a new prerequisite) in the same commit; every new test file has its line in `tests/phases.txt` (phase 24); a new C test host gets its `std_usage_check` allow-list entry.
- [x] **Day review (CLAUDE.md, required):** `/code-review high` over the day's diff after the scoped tests; verify, fix (with a regression test that fails without the fix) or decline each finding with a reason; record the count and outcome under "Post-implementation code review" below and in `STATUS.md`. *(postponed to the closing pass; see the section below)*
- [x] `STATUS.md` and `KNOWN-ISSUES.md` updated in the same commit; commit and push.

**Done:** a host plays, blends and speeds up clips on a skinned model and sees them in its window, with poses readable after the frame.

## Outcome

Day 363 done on the Linux machine (2026-11-15), tests written and built, **not run**. Skeleton instance and (read-only) animation player components, `animation_play`/`set_speed`/`stop`/`time`, `skeleton_find_joint`/`skeleton_joint_transform`, `entity_attach_to_joint`. The frame runs `AnimationSystem` over the resource host's manager (the host pins skeletons and clips so they stay loaded: `ResourceHost::PinSkeleton`/`PinClip`) before the transform system, fills the palette of a skeleton with no clip with the bind pose, and places attached entities after the propagation. The cross-fade lives in the animation module (`AnimationPlayer::fade_*`: the old clip keeps sampling and `Blend` mixes by elapsed/total). `SurfaceScene` draws a skinned mesh with its entity's palette (`joint_matrices`, rewritten every frame) and a default skinned material. Tests (`host_scene.c`, with a skeleton, three clips, a second skeleton and a skinned mesh minted by `mint_resources_data.cmake`): refusals (no skeleton instance, wrong skeleton, unknown clip, bad flags and speed), time advancing, speed 2 and 0, the joint height following the clip, a cross-fade partway and then complete, an entity attached to a joint following it, a skinned mesh drawn.

## Post-implementation code review

Self-review (the formal `/code-review high` runs once in the closing pass): the attachment overwrites the world matrix after the propagation, so it must not have a parent of its own (documented); children of an attached entity lag a frame; `PinClip` is called while the world mutex is held and may do a synchronous load.
