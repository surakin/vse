# Phase 5 / Day 72 — Shader Permutations

**Phase:** [Phase 5 — Renderer Core](../phase-05-renderer-core.md)
**Depends on:** Day 71.

Let one Slang source produce the variants a material system needs (with/without normal map, alpha test, ...) without runtime compilation and without a file explosion.

## Tasks

- [x] Evaluate the two Slang mechanisms on a real shader - specialization constants and compile-time defines/generics - and record the choice and the evidence (SPIR-V size, pipeline creation time, portability to HLSL/MSL) in the renderer spec. Pick one as the engine's permutation mechanism.
- [x] Add specialization support to the RHI only if the chosen mechanism needs it (`ShaderDesc`/pipeline desc gain a constants array; null + Vulkan + tests + docs + a note in the portability review). If compile-time defines are chosen the RHI does not change.
- [x] Permutation model: `PermutationKey` (a small bit set over named options declared in the shader), a mint-time manifest listing which keys to build per shader; a missing key at runtime is an error naming the shader and key, never a silent fallback.
- [x] The mint tool builds every manifest key, dedupes identical SPIR-V, and emits one `key -> words` table per shader.
- [x] Tests: two permutations of one shader render differently on the GPU (analytic probe), identical keys share a module, an unknown key is rejected, lookup is bounded.
- [x] Document the size limits (max options per shader, max keys) and the build-time cost of the manifest.
- [x] **Documentation (CLAUDE.md "Public API is documented before a day is done"):** The permutation types and the author-facing workflow (declaring options in Slang, listing keys in the manifest) are documented. `docgen lint` stays at zero findings, `cmake --build build --target docs` output is committed, `ctest -L docs` passes.
- [x] **End-of-day review (CLAUDE.md):** run `/code-review high` on the day's diff, fix/decline each finding with a reason, record it under a "Post-implementation code review" section here, and mention it in `STATUS.md`.
- [x] Update `STATUS.md`; commit (code, tests, regenerated `docs/reference/` and the guide pages together).

**Done:** A shader with two options builds all variants offline, the right one is chosen by key at runtime, and the mechanism decision is recorded with evidence.

## Result

- **Mechanism: compile-time defines** (decided with measurements in the renderer spec, new section R10.1). On the real `lit.frag` with two options the four defines variants were 1820 / 796 / 552 / 552 bytes (3168 shipped once the two identical ones are shared) against one 2248-byte specialization-constant module; Slang's HLSL for a specialization constant is a constant-buffer field and a runtime `if`, so Direct3D 12 would lose the optimization; per-variant reflection is exact only with separate modules. **Not measured:** pipeline creation time (it needs the RHI plumbing this decision avoids); the spec says so and names specialization constants as the Vulkan-only escape hatch if profiling ever asks for it. The portability review notes the decision.
- **No RHI change.**
- **Model:** a shader declares `// permutation-options: A B C` (first 60 lines, upper-case, unique, at most 16, each used in code), a `<name>.perm` manifest lists the keys (`key -` required, at most 32, unique as sets). `renderer::PermutationKey`, `ShaderPermutations` (committed `<name>.perm.inc`), `ValidateShaderPermutations`, `MakePermutationKey`, `FindPermutation`, `DescribePermutationKey` in `engine/renderer/shader_permutation.{h,cpp}`. A key that was not built returns null with `shader 'x' has no permutation {A, B} (add the key to its .perm manifest)`; a corrupt table says so instead.
- **Mint tool:** `shader_mint --permute` runs `slangc` itself once per key (`-DA=1`, `posix_spawnp`, Linux only for now), checks each with `spirv-val`, mints each module like a plain shader, dedupes identical modules, and writes the plain pair (base key) and `.perm.inc` as one all-or-nothing set. `shaders/CMakeLists.txt` and `check.cmake` detect a `.perm` file; `shaders_check` regenerates and byte-compares the table (a doctored `perm.inc` fails it; regeneration twice is byte-identical). Cost: about 1 second for the 7-key test shader.
- **Test shader:** `shaders/permute_test.frag` (INVERT, HALF, TINT, DEBUG; 7 keys, 6 distinct modules because `DEBUG TINT` shares `DEBUG`'s module).
- **Tests:** `tests/shader_mint` (option declaration and manifest parsing with every rejection, limits of 16 and 32, dedupe, emitter format, deterministic output) and `tests/renderer/shader_permutation_test.cpp` (table validity, lookup, shared module, unknown key error text, bounded lookup, malformed tables, and a GPU analytic probe: base, INVERT, HALF, INVERT+HALF, TINT, DEBUG and the shared key all draw the expected pixel with validation and sync validation silent).
- **Docs:** `///` on every symbol (renderer 106/106, total 1797/1797, lint baseline untouched), guide section "Permutations: one source, several modules" in `docs/guide/rhi-shaders.md` with a tagged sample that compiles and runs (11/11 samples), `docs/guide/renderer.md`, `shaders/README.md` workflow, `docs/reference/` regenerated.
- **Finding while writing the guide:** I had planned to document a pitfall that permutation code must sit entirely inside `#if` blocks. It does not reproduce: Slang reports a resource a variant does not use as unused (including behind a branch on a constant derived from the option), and the layout builders leave it out. The guide states what was verified instead.
- Full suite 1370/1370 except the Xwayland `X11Window::Create opens a mapped window on the real X server` (known environment problem).

## Post-implementation code review

`/code-review high` returned 10 findings; all fixed.

(1) The "option declared but never used" check counted comments, so the shader's own header comment hid a typo: comments are now stripped before counting, with tests (a note mentioning the option, a block comment, and a typo beside a correct comment); confirmed the new test fails with stripping disabled. (2) The plain pair was committed before the `.perm.inc` write, so a failure left a new pair beside an old table: all three files are written to temporaries and renamed together (checked by pointing `--perm-out` at a missing directory: the old files stay, no temporaries). (3) Dedupe and the base-module assumption lived untested in `main()`: moved to `DedupeModules` with tests (equal words but different reflection stay distinct). (4) `posix_spawn` did not search `PATH`, `waitpid` was not retried on `EINTR`, and a signal death looked like a failure: `posix_spawnp`, retry loop, and the message says exit status or signal. (5) `Appendf` and the array emission were duplicated: shared `Appendf`, `AppendWordArray`, `AppendByteArray` (the committed `.inc` files stayed byte-identical). (6) `FindPermutation` blamed the manifest for a corrupt table and trusted null arrays: it now reports corruption separately and never dereferences null arrays. (7) `ValidateShaderPermutations` accepted null, empty and duplicate option names: rejected, with tests. (8) The tool printed a summary line on every successful run: removed (silent on success). (9) A misspelled or indented declaration produced a misleading "no line in the first 60 lines": leading blanks and blanks after `//` are tolerated, with tests. (10) `STATUS.md` and this section were missing from the diff: added.

