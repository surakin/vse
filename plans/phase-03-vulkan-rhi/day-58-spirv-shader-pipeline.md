# Phase 3 / Day 58 — Shader Workflow (GLSL → SPIR-V) & Shader Modules

**Phase:** [Phase 3 — Vulkan RHI](../phase-03-vulkan-rhi.md)
**Depends on:** Day 57.

How shaders enter the engine: GLSL compiled offline by `glslc` (`PLAN.md` assumption #18) — a build workflow, not a compiler in the engine. This revises the earlier plan of hand-written SPIR-V assembly; the long-term toolchain (DX12/Metal path) stays an open decision recorded in `PLAN.md`.

## Tasks

- [ ] `shaders/` directory layout and conventions: GLSL 4.50 sources `*.vert`/`*.frag`/`*.comp` (`#version 450`, a header comment documenting the interface — locations, bindings, push constants), generated `*.spv.inc` (little-endian `u32` arrays + `constexpr usize` word count, with the `glslc` version recorded in a header comment) committed next to them.
- [ ] `tools/spv_embed` (our own tiny C++ tool on the foundation, so no script-language dependency): reads a `.spv` binary and emits the `.spv.inc`; a CMake custom target `shaders` that runs `glslc` (dev-time, found via `find_program`, **optional**; fixed flags: `--target-env=vulkan1.3 -O -fshader-stage=<from extension>`) then `spirv-val` then `spv_embed` when sources changed; when the tools are absent the committed `.inc` files are used and a `shaders_check` ctest recompiles and compares embedded words against the committed ones only if the tools exist (and reports a version mismatch of `glslc` as a skip with a message, not a failure).
- [ ] `CreateShader(ShaderDesc)` in the Vulkan backend: `vkCreateShaderModule` from words; stage and entry-point recorded; a minimal SPIR-V sanity pass in the RHI itself (magic number, version ≤ 1.6, word count, entry point of the declared stage present — parsed by our own small reader) so garbage fails before reaching the driver; null device validates the same. `ShaderDesc` is documented as carrying backend bytecode (SPIR-V for Vulkan) so DXIL/MSL blobs can follow (`PLAN.md` assumption #18).
- [ ] Write the first shader pair: `triangle.vert` (positions + colors from vertex inputs, passes color) and `triangle.frag` (outputs interpolated color); compile with `glslc`, validate with `spirv-val`; commit sources and `.inc`.
- [ ] Document the GLSL subset the engine's shaders use and the SPIR-V the RHI accepts (capability `Shader`, logical addressing, entry point `main`), with a short authoring guide for Days 59-65: vertex input and output locations, uniform block layout (`std140`) and its CPU-side matching, `set`/`binding` decorations matching the bind group layout, `push_constant` blocks, storage buffers (`std430`) and `layout(local_size_x=...)` for compute.
- [ ] Tests: the SPIR-V reader rejects bad magic, truncated modules, wrong stage; accepts the committed shaders; `[gpu]`: both triangle shaders create shader modules validation-clean.
- [ ] **Documentation (CLAUDE.md "Public API is documented before a day is done"):** the shader authoring guide lives in `docs/guide/rhi-shaders.md` (linked from the guide index); `CreateShader`/`ShaderDesc` documented in the reference.
- [ ] **End-of-day review (CLAUDE.md):** run `/code-review high` on the
  day's diff, fix/decline each finding with a reason, record it under a
  "Post-implementation code review" section here, and mention it in
  `STATUS.md`.
- [ ] Update `STATUS.md`; commit (code, tests, the regenerated `docs/reference/` and the guide page together).

**Done:** GLSL vertex/fragment shaders compile with `glslc`, validate, embed and load through `CreateShader` with sanity checks, and the workflow works with or without the dev tools installed.
