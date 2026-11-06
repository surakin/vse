# Phase 4 / Day 58 — Shader Workflow (GLSL → SPIR-V) & Shader Modules

**Phase:** [Phase 4 — Vulkan RHI](../phase-04-vulkan-rhi.md)
**Depends on:** Day 57.

How shaders enter the engine: GLSL compiled offline by `glslc` (`PLAN.md` assumption #18) — a build workflow, not a compiler in the engine. This revises the earlier plan of hand-written SPIR-V assembly; the long-term toolchain (DX12/Metal path) stays an open decision recorded in `PLAN.md`.

## Tasks

- [x] `shaders/` directory layout and conventions: GLSL 4.50 sources `*.vert`/`*.frag`/`*.comp` (`#version 450`, a header comment documenting the interface — locations, bindings, push constants), generated `*.spv.inc` (little-endian `u32` arrays + `constexpr usize` word count, with the `glslc` version recorded in a header comment) committed next to them.
- [x] `tools/spv_embed` (our own tiny C++ tool on the foundation, so no script-language dependency): reads a `.spv` binary and emits the `.spv.inc`; a CMake custom target `shaders` that runs `glslc` (dev-time, found via `find_program`, **optional**; fixed flags: `--target-env=vulkan1.3 -O -fshader-stage=<from extension>`) then `spirv-val` then `spv_embed` when sources changed; when the tools are absent the committed `.inc` files are used and a `shaders_check` ctest recompiles and compares embedded words against the committed ones only if the tools exist (and reports a version mismatch of `glslc` as a skip with a message, not a failure).
- [x] `CreateShader(ShaderDesc)` in the Vulkan backend: `vkCreateShaderModule` from words; stage and entry-point recorded; a minimal SPIR-V sanity pass in the RHI itself (magic number, version ≤ 1.6, word count, entry point of the declared stage present — parsed by our own small reader) so garbage fails before reaching the driver; null device validates the same. `ShaderDesc` is documented as carrying backend bytecode (SPIR-V for Vulkan) so DXIL/MSL blobs can follow (`PLAN.md` assumption #18).
- [x] Write the first shader pair: `triangle.vert` (positions + colors from vertex inputs, passes color) and `triangle.frag` (outputs interpolated color); compile with `glslc`, validate with `spirv-val`; commit sources and `.inc`.
- [x] Document the GLSL subset the engine's shaders use and the SPIR-V the RHI accepts (capability `Shader`, logical addressing, entry point `main`), with a short authoring guide for Days 59-65: vertex input and output locations, uniform block layout (`std140`) and its CPU-side matching, `set`/`binding` decorations matching the bind group layout, `push_constant` blocks, storage buffers (`std430`) and `layout(local_size_x=...)` for compute.
- [x] Tests: the SPIR-V reader rejects bad magic, truncated modules, wrong stage; accepts the committed shaders; `[gpu]`: both triangle shaders create shader modules validation-clean.
- [x] **Documentation (CLAUDE.md "Public API is documented before a day is done"):** the shader authoring guide lives in `docs/guide/rhi-shaders.md` (linked from the guide index); `CreateShader`/`ShaderDesc` documented in the reference.
- [x] **End-of-day review (CLAUDE.md):** run `/code-review high` on the
  day's diff, fix/decline each finding with a reason, record it under a
  "Post-implementation code review" section here, and mention it in
  `STATUS.md`.
- [x] Update `STATUS.md`; commit (code, tests, the regenerated `docs/reference/` and the guide page together).

**Done:** GLSL vertex/fragment shaders compile with `glslc`, validate, embed and load through `CreateShader` with sanity checks, and the workflow works with or without the dev tools installed.

## Result

`rhi::ValidateSpirv` (`engine/rhi/spirv.{h,cpp}`) is the module sanity check both backends run in `CreateShader`: magic (with a byte-swapped message), version 1.0 to 1.6, id bound, instruction lengths and an exact end of stream, capability `Shader` and not `Kernel`, `Logical` addressing with the `GLSL450` or `Vulkan` memory model, and an `OpEntryPoint` with the requested name whose execution model matches the declared stage. The Vulkan `CreateShader`/`DestroyShader` create a `VkShaderModule` held in a deferred-release `HandlePool`, with `LookupShader` for Day 59's pipeline creation and a user-leak count; `ShaderDesc::Validate` now also rejects entry names over 63 bytes. `tools/spv_embed` turns a `.spv` into `*.spv.inc` (written atomically); `shaders/` holds `triangle.vert`/`triangle.frag` GLSL sources and their committed `.spv.inc` files, the `shaders` target (`glslc --target-env=vulkan1.3 -O`, `spirv-val`, `spv_embed`) and the `shaders_check` ctest (recompile and compare, skipping with a message when `glslc` is a different version from the one recorded in each file). Tests: `tests/rhi/shader_test.cpp` (reader accepts the minimal modules and the committed shaders, rejects bad magic, byte-swap, versions, zero bound, every truncation point, zero-length instructions, wrong stage, entry names, unterminated names, capabilities, addressing/memory model; the null device checks contents; `[gpu]` module creation validation-clean, deferred destruction, forgotten shader freed). Docs 1607/1607; the guide page `docs/guide/rhi-shaders.md` has a tagged sample that compiles and runs against the committed shaders.

Deviations from the original plan: GLSL compiled by `glslc` replaced hand-written SPIR-V assembly (decided before this day, `PLAN.md` assumption #18). The check is a subset check, not a validator, and does not parse capabilities beyond `Shader`/`Kernel`; `spirv-val` stays the dev-time validator.

## Post-implementation code review

`/code-review high` returned 8 findings.

Fixed:
1. An unterminated entry-point name could be "terminated" by an interface id with a zero low byte: the reader now requires the padding after the terminator to be zero, and a regression test (which failed before the fix) covers it.
2. `check.cmake` ignored `spv_embed`'s exit code and silently skipped a `.inc` with no version line: both are now errors.
3. `cmake_language(EXIT)` needs CMake 3.29: the skip is now done with the test's `SKIP_REGULAR_EXPRESSION` instead.
4. The entry-name length rule was duplicated: `ShaderEntryNameTooLong` is shared.
5. `spv_embed` could leave a truncated committed file: it writes beside the target and renames.
6. Tests flipped the global log flag and re-enabled it unconditionally: they use a restoring `QuietLog`.
7. `std::string` without `<string>`: replaced with `strcmp`.
8. A shader module was destroyed while holding the resource lock when the handle pool was full: moved outside the lock.

Declined:
- The `spv_embed` magic-number literal stays (the tool links only the foundation, not `engine/rhi`); it is commented.
