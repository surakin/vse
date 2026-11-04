# Phase 3 / Day 58 — SPIR-V Shader Workflow & Shader Modules

**Phase:** [Phase 3 — Vulkan RHI](../phase-03-vulkan-rhi.md)
**Depends on:** Day 57.

How hand-authored shaders enter the engine (assumption #4) — a workflow, not a compiler.

## Tasks

- [ ] `shaders/` directory layout and conventions: `*.spvasm` sources (SPIR-V assembly text with a header comment documenting the interface — locations, bindings, push constants), generated `*.spv.inc` (little-endian `u32` arrays + `constexpr usize` word count) committed next to them.
- [ ] `tools/spv_embed` (our own tiny C++ tool on the foundation, so no script-language dependency): reads a `.spv` binary and emits the `.spv.inc`; a CMake custom target `shaders` that runs `spirv-as` (dev-time, found via `find_program`, **optional**) then `spirv-val` then `spv_embed` when sources changed; when the tools are absent the committed `.inc` files are used and a `shaders_check` ctest compares embedded words against a fresh assembly only if tools exist.
- [ ] `CreateShader(ShaderDesc)` in the Vulkan backend: `vkCreateShaderModule` from words; stage and entry-point recorded; a minimal SPIR-V sanity pass in the RHI itself (magic number, version ≤ 1.6, word count, entry point of the declared stage present — parsed by our own small reader) so garbage fails before reaching the driver; null device validates the same.
- [ ] Write the first shader pair by hand: `triangle.vert.spvasm` (positions + colors from vertex inputs, passes color) and `triangle.frag.spvasm` (outputs interpolated color); validate with `spirv-val`; commit sources and `.inc`.
- [ ] Document the SPIR-V subset used (capabilities `Shader`, logical addressing, GLSL450 memory model) and a short authoring guide with the idioms needed for Days 59-65 (uniform block layout decorations, descriptor set/binding decorations, push constant block, `OpExtInst GLSL.std.450` for normalize/dot/pow).
- [ ] Tests: the SPIR-V reader rejects bad magic, truncated modules, wrong stage; accepts the committed shaders; `[gpu]`: both triangle shaders create shader modules validation-clean.
- [ ] **Documentation (CLAUDE.md "Public API is documented before a day is done"):** the shader authoring guide lives in `docs/guide/rhi-shaders.md` (linked from the guide index); `CreateShader`/`ShaderDesc` documented in the reference.
- [ ] **End-of-day review (CLAUDE.md):** run `/code-review high` on the
  day's diff, fix/decline each finding with a reason, record it under a
  "Post-implementation code review" section here, and mention it in
  `STATUS.md`.
- [ ] Update `STATUS.md`; commit (code, tests, the regenerated `docs/reference/` and the guide page together).

**Done:** hand-authored vertex/fragment SPIR-V assembles, validates, embeds and loads through `CreateShader` with sanity checks, and the workflow works with or without the dev tools installed.
