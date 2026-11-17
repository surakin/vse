# Guide

Concept pages, one per subsystem. Each explains the mental model, shows working code and lists the rules and the common mistakes. If you are new, read
the pages in the order below; each builds on the ones before it. The generated [reference](../reference/README.md) lists every symbol; the
[tutorials](../tutorials/README.md) are step-by-step walkthroughs.

## Start here

1. [getting-started](getting-started.md): build, run, test, find your way around, the house rules.

## The foundation (used in every file)

2. [foundation-types](foundation-types.md): the engine's own names for integers, bytes, spans and the standard facilities.
3. [foundation-memory](foundation-memory.md): allocators and how to choose one.
4. [foundation-containers](foundation-containers.md): `Array`, `Vector`, `HashMap`, smart pointers, `Span`.
5. [foundation-strings](foundation-strings.md): `StringView`, `String`, `StringBuilder`, UTF-8.
6. [foundation-logging-and-assert](foundation-logging-and-assert.md): logs, assertions, how failures are reported.
7. [foundation-math](foundation-math.md): vectors, matrices, quaternions.
8. [foundation-sync](foundation-sync.md) and [foundation-cstr](foundation-cstr.md): threads, atomics, and C-library replacements.
9. [io](io.md): files, paths and directories. [settings](settings.md): everything a user can configure.

## Working on the engine

10. [building-and-testing](building-and-testing.md): scoped test runs, sanitizers, the gates, writing a test.
11. [foundation-debugging](foundation-debugging.md): stack traces, crash dumps, death tests. [porting-debugging](porting-debugging.md): for a new platform.
12. [windows](windows.md): building and running on Windows. [macos](macos.md): building and running on macOS. [engine-library](engine-library.md): the engine as a loadable library and what it exports.

## Running a program

13. [platform-os](platform-os.md): operating-system services. [platform-windowing-and-input](platform-windowing-and-input.md): windows, keyboard, mouse.
14. [jobs](jobs.md): the job system that runs everything in parallel.

## Drawing

15. [rhi](rhi.md) and [rhi-shaders](rhi-shaders.md): the graphics abstraction and shaders.
16. [renderer](renderer.md) and [render-settings](render-settings.md): the render graph, materials, and what can change at run time.
17. [text](text.md): fonts and text layout. [ui](ui.md): the widget toolkit.

## Game objects

18. [ecs](ecs.md): entities, components, archetypes and the world.
19. [spatial](spatial.md): what is near what: shapes, bounding-volume hierarchies, spatial sets (Phase 18).
20. [database](database.md): the database interface, SqlText, values and the conformance suite (Phase 19).
21. [storage](storage.md): the component registry and the canonical encoding (Phase 19).
22. [net](net.md): addresses, endpoints and network errors (Phase 20).
23. [assets](assets.md): asset identity, blobs, backends and the resource manager (Phase 22).
24. [animation](animation.md): poses, the clip sampler, skinning matrices and the animation system (Phase 22).
25. [physics](physics.md): rigid bodies that agree to the bit: the determinism rules, the probe and the state hash (Phase 25).
26. [audio](audio.md): the repeatability rules, the block hash and the tolerance comparison (Phase 26).
