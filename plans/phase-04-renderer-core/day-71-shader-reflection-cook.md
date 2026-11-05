# Phase 4 / Day 71 — Shader Reflection & the Cook Tool

**Phase:** [Phase 4 — Renderer Core](../phase-04-renderer-core.md)
**Depends on:** Day 70.

Make shaders describe themselves so materials and bind group layouts are generated from them, not written twice. This is the first piece of the Phase 10 cooker, scoped to shaders.

## Tasks

- [ ] Add `tools/shader_cook/` (a C++ tool using the foundation): runs `slangc` with `-reflection-json`, parses the JSON with nlohmann/json (vendored under `third_party/nlohmann_json/`, tool-only, `PLAN.md` assumption #19; decided 2026-11-05 instead of a hand-written reader), writes the SPIR-V words as `*.spv.inc` and a compact reflection blob as `*.refl.inc`. `spv_embed` is folded into it.
- [ ] Define `renderer::ShaderReflection` (`engine/renderer/shader_reflection.h`): entry/stage, bindings (set, binding, `rhi::BindingType`, array size, stages, name), uniform block layouts (member names, offsets, sizes, types), push-constant range, vertex inputs (location, `VertexFormat`, name), compute local size. Plain data.
- [ ] Round-trip tests: reflect each existing shader and compare with hand-written expectations (the lit shader's block: 176 bytes and member offsets; its three bindings; the cube vertex layout); a C++ struct whose size disagrees with the reflected block fails a generated size check.
- [ ] Builders `MakeBindGroupLayoutDesc(const ShaderReflection&, set)` and vertex attribute builders, tested on the null and real devices; merging a vertex+fragment pair into one layout (stage masks OR-ed, conflicting types reported with names).
- [ ] Reflection output is committed like the `.spv.inc` files, `shaders_check` verifies it is current, and the whole path still builds without `slangc` installed.
- [ ] Failure tests: a reflection blob with a bad version, truncated data or an unknown type is rejected with a message and never read out of bounds (random-truncation test, run under ASan).
- [ ] **Documentation (CLAUDE.md "Public API is documented before a day is done"):** `ShaderReflection`, the builders and the cook tool are documented in headers and in the shaders guide (what is generated, what is committed, how to regenerate). `docgen lint` stays at zero findings, `cmake --build build --target docs` output is committed, `ctest -L docs` passes.
- [ ] **End-of-day review (CLAUDE.md):** run `/code-review high` on the day's diff, fix/decline each finding with a reason, record it under a "Post-implementation code review" section here, and mention it in `STATUS.md`.
- [ ] Update `STATUS.md`; commit (code, tests, regenerated `docs/reference/` and the guide pages together).

**Done:** Every engine shader has committed reflection data, layouts and vertex inputs are built from it, and malformed blobs are rejected safely.
