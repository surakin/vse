# Phase 4 / Day 69 — Slang Toolchain & First Shader Ports

**Phase:** [Phase 4 — Renderer Core](../phase-04-renderer-core.md)
**Depends on:** Day 68.

Replace the dev-time shader compiler with `slangc` without changing a pixel: wire it into the build, learn which flags make Slang match the engine's conventions, and port the simplest shaders first.

## Tasks

- [ ] Install and pin `slangc` (document the version, where it comes from on Arch/Windows/macOS, and that it is an optional dev tool: a checkout without it builds from the committed outputs). `slangc` is not installed on the dev machine today; installing it is the first step. Record `slangc -v` in each generated file like Phase 3 did with `glslc`.
- [ ] Extend `shaders/CMakeLists.txt` with `.slang` support beside the GLSL sources: `find_program(SLANGC slangc)`, a helper running `slangc -target spirv -profile spirv_1_5 -entry <name> -stage <stage>`, `spirv-val` after, `spv_embed` for the `.inc`; the `shaders` target and `shaders_check` ctest handle both languages during the migration.
- [ ] Find and document the flags that make Slang match the engine: column-major matrix layout (`-matrix-layout-column-major` or the equivalent in source), Vulkan clip-space handling (`-fvk-invert-y` stays OFF: the projection already flips Y), a std140-compatible buffer layout so `Mat4` can still be copied into uniform blocks, entry-point naming, descriptor set/binding mapping from `[[vk::binding(b, s)]]`, the push-constant form. Write each finding in `shaders/README.md` with the failure it prevents.
- [ ] Port `fullscreen.vert`, `triangle.vert`, `triangle.frag`, `bindings_test.frag`, `fill.comp`, `saxpy.comp` and `gradient.comp` to `.slang`. Keep the GLSL files until Day 70.
- [ ] Prove equivalence without trusting the compiler: switch the includes behind one CMake option `ENGINE_SHADER_LANG=slang|glsl` (default `glsl` until Day 70); the triangle golden, compute and bindings tests pass unchanged against the Slang-built `.inc` files on the real device with validation and sync validation silent.
- [ ] Add a test that every ported shader's SPIR-V passes `rhi::ValidateSpirv` and has the entry name, stage and (for compute) local size the GLSL one had.
- [ ] **Documentation (CLAUDE.md "Public API is documented before a day is done"):** `docs/guide/rhi-shaders.md` describes the Slang workflow (the GLSL workflow stays documented until Day 70) and the pitfalls hit. `docgen lint` stays at zero findings, `cmake --build build --target docs` output is committed, `ctest -L docs` passes.
- [ ] **End-of-day review (CLAUDE.md):** run `/code-review high` on the day's diff, fix/decline each finding with a reason, record it under a "Post-implementation code review" section here, and mention it in `STATUS.md`.
- [ ] Update `STATUS.md`; commit (code, tests, regenerated `docs/reference/` and the guide pages together).

**Done:** Seven shaders compile from Slang, the existing GPU tests pass unchanged against them with validation clean, and the flags and pitfalls are written down.
