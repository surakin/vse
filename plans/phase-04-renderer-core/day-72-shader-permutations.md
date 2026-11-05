# Phase 4 / Day 72 — Shader Permutations

**Phase:** [Phase 4 — Renderer Core](../phase-04-renderer-core.md)
**Depends on:** Day 71.

Let one Slang source produce the variants a material system needs (with/without normal map, alpha test, ...) without runtime compilation and without a file explosion.

## Tasks

- [ ] Evaluate the two Slang mechanisms on a real shader - specialization constants and compile-time defines/generics - and record the choice and the evidence (SPIR-V size, pipeline creation time, portability to HLSL/MSL) in the renderer spec. Pick one as the engine's permutation mechanism.
- [ ] Add specialization support to the RHI only if the chosen mechanism needs it (`ShaderDesc`/pipeline desc gain a constants array; null + Vulkan + tests + docs + a note in the portability review). If compile-time defines are chosen the RHI does not change.
- [ ] Permutation model: `PermutationKey` (a small bit set over named options declared in the shader), a mint-time manifest listing which keys to build per shader; a missing key at runtime is an error naming the shader and key, never a silent fallback.
- [ ] The mint tool builds every manifest key, dedupes identical SPIR-V, and emits one `key -> words` table per shader.
- [ ] Tests: two permutations of one shader render differently on the GPU (analytic probe), identical keys share a module, an unknown key is rejected, lookup is bounded.
- [ ] Document the size limits (max options per shader, max keys) and the build-time cost of the manifest.
- [ ] **Documentation (CLAUDE.md "Public API is documented before a day is done"):** The permutation types and the author-facing workflow (declaring options in Slang, listing keys in the manifest) are documented. `docgen lint` stays at zero findings, `cmake --build build --target docs` output is committed, `ctest -L docs` passes.
- [ ] **End-of-day review (CLAUDE.md):** run `/code-review high` on the day's diff, fix/decline each finding with a reason, record it under a "Post-implementation code review" section here, and mention it in `STATUS.md`.
- [ ] Update `STATUS.md`; commit (code, tests, regenerated `docs/reference/` and the guide pages together).

**Done:** A shader with two options builds all variants offline, the right one is chosen by key at runtime, and the mechanism decision is recorded with evidence.
