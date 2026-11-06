# Phase 5 / Day 69 — Slang Toolchain & First Shader Ports

**Phase:** [Phase 5 — Renderer Core](../phase-05-renderer-core.md)
**Depends on:** Day 68.

Replace the dev-time shader compiler with `slangc` without changing a pixel: wire it into the build, learn which flags make Slang match the engine's conventions, and port the simplest shaders first.

## Tasks

- [x] Install and pin `slangc` (document the version, where it comes from on Arch/Windows/macOS, and that it is an optional dev tool: a checkout without it builds from the committed outputs). `slangc` v2026.18.2 (Arch `shader-slang`) was installed on the dev machine on this day. Record `slangc -v` in each generated file like Phase 4 did with `glslc`.
- [x] Extend `shaders/CMakeLists.txt` with `.slang` support beside the GLSL sources: `find_program(SLANGC slangc)`, a helper running `slangc -target spirv -profile spirv_1_5 -entry <name> -stage <stage>`, `spirv-val` after, `spv_embed` for the `.inc`; the `shaders` target and `shaders_check` ctest handle both languages during the migration.
- [x] Find and document the flags that make Slang match the engine: column-major matrix layout (`-matrix-layout-column-major` or the equivalent in source), Vulkan clip-space handling (`-fvk-invert-y` stays OFF: the projection already flips Y), a std140-compatible buffer layout so `Mat4` can still be copied into uniform blocks, entry-point naming, descriptor set/binding mapping from `[[vk::binding(b, s)]]`, the push-constant form. Write each finding in `shaders/README.md` with the failure it prevents.
- [x] Port `fullscreen.vert`, `triangle.vert`, `triangle.frag`, `bindings_test.frag`, `fill.comp`, `saxpy.comp` and `gradient.comp` to `.slang`. Keep the GLSL files until Day 70.
- [x] Prove equivalence without trusting the compiler: switch the includes behind one CMake option `ENGINE_SHADER_LANG=slang|glsl` (default `glsl` until Day 70); the triangle golden, compute and bindings tests pass unchanged against the Slang-built `.inc` files on the real device with validation and sync validation silent.
- [x] Add a test that every ported shader's SPIR-V passes `rhi::ValidateSpirv` and has the entry name, stage and (for compute) local size the GLSL one had.
- [x] **Documentation (CLAUDE.md "Public API is documented before a day is done"):** `docs/guide/rhi-shaders.md` describes the Slang workflow (the GLSL workflow stays documented until Day 70) and the pitfalls hit. `docgen lint` stays at zero findings, `cmake --build build --target docs` output is committed, `ctest -L docs` passes.
- [x] **End-of-day review (CLAUDE.md):** run `/code-review high` on the day's diff, fix/decline each finding with a reason, record it under a "Post-implementation code review" section here, and mention it in `STATUS.md`.
- [x] Update `STATUS.md`; commit (code, tests, regenerated `docs/reference/` and the guide pages together).

**Done:** Seven shaders compile from Slang, the existing GPU tests pass unchanged against them with validation clean, and the flags and pitfalls are written down.

## Result

- **Toolchain:** `slangc` v2026.18.2 pinned; every generated file now has a `// compiler: <name> <version>` line (`spv_embed` writes it; the 13 GLSL outputs were relabelled `glslc 2026.4`, SPIR-V byte-identical). `shaders/CMakeLists.txt` builds both languages (`shaders/<name>.spv.inc` from GLSL, `shaders/slang/<name>.spv.inc` from Slang) behind one `shaders` target; `shaders_check` checks both and skips only what it cannot compare. The Slang arguments live in `slang_compile_args()` (`shaders/names.cmake`).
- **`ENGINE_SHADER_LANG=glsl|slang`** (default `glsl` until Day 70) sets `ENGINE_SHADER_INCLUDE_DIRS`, used by the RHI and renderer tests, `rhi_demo` and `rhi_bench`; shaders without a Slang port fall back to GLSL.
- **Ported:** `fullscreen.vert`, `triangle.vert`, `triangle.frag`, `bindings_test.frag`, `fill.comp`, `saxpy.comp`, `gradient.comp` (`shaders/slang/*.slang`; the GLSL files stay until Day 70).
- **Flags and pitfalls:** written in `shaders/README.md` with the failure each prevents. Surprises: `-matrix-layout-column-major` is already the SPIR-V default and Slang names layouts opposite to GLSL; `SV_VertexID` silently needs `DrawParameters` (use `SV_VulkanVertexID`); a storage image must be `WTexture2D` + `.Store` to be `NonReadable` like the GLSL `writeonly` one.
- **Equivalence:** `tests/rhi/slang_shader_test.cpp` compares each port with its GLSL original (entry, stage, local size, (set, binding) pairs, locations, member offsets, `NonReadable`/`NonWritable`, storage-class counts, image formats; capabilities limited to Matrix/Shader/ImageQuery) and runs `ValidateSpirv`. With `-DENGINE_SHADER_LANG=slang`, the full GPU suite (triangle golden, compute, bindings, cube/textured/lit goldens that use the GLSL fallback) passes unchanged, `ENGINE_RHI_VALIDATION=1 ENGINE_RHI_VALIDATION_SYNC=1` silent (`ENGINE_RHI_VALIDATION_FATAL=1` aborts only in `vulkan_instance_test.cpp:99`, which tests that switch itself).
- **Docs:** `docs/guide/rhi-shaders.md` has a Slang workflow section; `docgen lint` at zero new findings, docs 1694/1694, `ctest -L docs` passes.
- Full suite in both modes: 1295/1295 except `X11Window::Create opens a mapped window on the real X server` (`x11_window_test.cpp:45`, waits for `map_state == IsViewable` under Xwayland), which fails consistently in this session; foundation code and that test are untouched by this day, so it is recorded as an environment problem to look at separately.

## Post-implementation code review

`/code-review high` returned 8 findings.

Fixed: (1) `gradient.comp.slang` used `RWTexture2D`, losing GLSL's `NonReadable`: now `WTexture2D` + `Store`; regression test confirmed failing on the old port (`a.non_readable == b.non_readable`). (2) The interface test ignored push-constant blocks, `NonReadable`, image formats, offsets and capabilities: all compared now (and `DrawParameters` is rejected). (3) Generated outputs did not depend on `names.cmake` (the flags): added to `DEPENDS`. (4) `cmake -E true` was given the `.spv` path when `spirv-val` is missing: the no-op is now built per shader without arguments. (5) The `shaders` subdirectory ordering and the stray blank line in the root `CMakeLists.txt`: commented as an ordering requirement. (6) `STATUS.md`, checkboxes and this section: done. (8) A version mismatch in one language marked the whole `shaders_check` skipped: it now prints `SKIP:` only if nothing was compared at all (checked by faking a mismatch: the test passes with a note).

Declined: (7) The test includes generated `.inc` files inside namespaces and relies on `types.h` being included first: it is included explicitly at the top, and any break is a loud compile error, not a silent one; a wrapper header per language would be more machinery than the problem warrants. Also declined from (2): running the Slang outputs on the GPU in the default build. The default stays `glsl` until Day 70 flips it; the Slang GPU run is the manual `-DENGINE_SHADER_LANG=slang` pass recorded above. `tools/docgen` keeps its hard-coded `-I shaders`: guide samples compile against the GLSL outputs, which have the same symbols.
