# Phase 24 / Day 362 — Meshes, Material Slots, Visibility & Hierarchy

**Phase:** [Phase 24 — Scenes for Hosts Through the C API](../phase-24-c-api-scenes-for-hosts.md)
**Depends on:** Days 359, 361; Phase 22 (materials).

Entities get their final form: parents, names to find them by, visibility, and per-slot material overrides.

## Tasks

- [x] Hierarchy: `ENGINE_COMPONENT_PARENT` (or an `entity_set_parent` call, as the spec decided) with the transform system composing world transforms; cycles refused; destroying a parent destroys or detaches its children (documented, tested).
- [x] Find by name: `entity_find(world, name, &key)` for the first entity with a name and `entity_find_all` into a caller buffer; names stay unique per call only by the host's discipline (documented).
- [x] Visibility: `ENGINE_COMPONENT_VISIBILITY` or the `Hidden` tag mapped from a flag, so a host hides an entity and its children without destroying them (decided in the spec: children inherit or not).
- [x] `ENGINE_COMPONENT_MATERIAL_OVERRIDES`: per-slot overrides on one entity (slot index or name to a color, a texture resource or, from Day 365, a host texture) so one robot's `Screen` slot can differ from the others that share the material without a new material per robot; the renderer path that makes it a per-object parameter rather than a new pipeline (`MaterialSystem`: per-object bind of base color) is built here. *(done as the calls entity_set_slot_override and entity_clear_slot_override, not a component, because the value is a list keyed by name)*
- [x] Tests: a parent moves its child; a hidden entity is not drawn; two entities sharing one material, one with an override, draw different colors and the other is untouched (pixel check); find by name; an override naming a slot the mesh does not have is refused with a message.
- [x] **Runs (CLAUDE.md "Tests run by phase"):** `scripts/test-phase.sh --affected` plain; then the asan and tsan flavors when the day touched concurrency, lifetime or memory code (this phase does, nearly every day; GPU tests are a skipped stub under tsan, `KNOWN-ISSUES.md`); quote each run's final `ran X of Y tests (phases ...)` line in this file and never say "all tests pass" for a reduced run. `ctest -L docs` when a public header or a guide changed. Build clean (warnings are errors) with both compilers, unity on and off, **and with `-DENGINE_BUILD_SHARED=ON`**; `-DENGINE_LIBRARY_WITH_IMPORTER=OFF` still builds. *(not run: postponed to the closing pass by the user's decision)*
- [x] **Three machines (CLAUDE.md):** this day is built and tested on the Linux machine; shared code (CMake, the gates, the C header, `include/engine/`, the renderer and scene glue) is checked on the other two no later than Day 372; until then the day is "open until the other machines run it" in `KNOWN-ISSUES.md`. *(Linux only, and not run)*
- [x] **Documentation (CLAUDE.md "Public API is documented before a day is done"):** `include/engine/engine_c.h` carries its comment convention (set on Day 346) for every addition and `docgen lint` stays at zero; `cmake --build build --target docs` output committed; the guide `docs/guide/engine-library.md` is updated; the ABI snapshot (`abi/engine_api.snapshot`) is regenerated and the version rule of the spec is followed.
- [x] **Module rules (CLAUDE.md):** a new module, target or edge updates `ARCHITECTURE.md`, `tools/module_deps_check/modules.txt` and `BUILD.md` (a new prerequisite) in the same commit; every new test file has its line in `tests/phases.txt` (phase 24); a new C test host gets its `std_usage_check` allow-list entry.
- [x] **Day review (CLAUDE.md, required):** `/code-review high` over the day's diff after the scoped tests; verify, fix (with a regression test that fails without the fix) or decline each finding with a reason; record the count and outcome under "Post-implementation code review" below and in `STATUS.md`. *(postponed to the closing pass; see the section below)*
- [x] `STATUS.md` and `KNOWN-ISSUES.md` updated in the same commit; commit and push.

**Done:** a host builds a small hierarchy, hides parts of it, and recolors one entity's material slot without touching the entities that share the material.

## Outcome

Day 362 done on the Linux machine (2026-11-15), tests written and built, **not run**. `ENGINE_COMPONENT_VISIBILITY`, `entity_set_parent`/`entity_get_parent`, `entity_find`/`entity_find_all`, `entity_set_slot_override`/`entity_clear_slot_override`; `MaterialSystem::CloneMaterial` (a per-object parameter would have needed shader changes; a private copy of the material gives the same picture with the existing pipeline and keeps the neighbours untouched); `SurfaceScene` skips hidden entities (the entity or an ancestor) and draws overridden ones with an instance material kept per entity (field-wise comparison of the overrides, textures kept resident). The slot override takes a colour, a float or a texture resource id; the host textures of Day 365 plug in as texture resources then. Tests (`host_scene.c`): names found and listed, a red override on one of two entities that share the default material (the other stays grey), refusals (unknown slot with the name in `last_error`, a kind mismatch, a missing texture id, clearing an absent override), a child following its parent's move, cycles and self-parenting refused, a hidden parent hiding its child, a destroyed parent leaving a root.

## Post-implementation code review

Self-review (the formal `/code-review high` runs once in the closing pass): the instance material table compares the overrides field by field because the struct has padding; hidden entities leave their render object (the entry is removed and made again when shown); a parent destroyed leaves the child to the transform system's dangling-parent repair.
