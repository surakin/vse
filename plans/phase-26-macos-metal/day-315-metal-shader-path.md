# Phase 26 / Day 315 — The Shader Path for Metal: `.metallib` Blobs, Entry Names, Clip Space, Object Index

**Phase:** [Phase 26 — macOS Port + Metal Backend](../phase-26-macos-metal.md)
**Depends on:** Day 314 (and the Day 298 decision: the interim, pending route through `-[MTLLibrary serializeToURL:error:]`).

Every shader mints a third blob, an AIR-only `.metallib`, committed beside `*.spv.inc` and `*.dxil.inc`, deterministically, by the route Day 298
chose. The shader source changes that Metal needs land on all three targets.

## Tasks

- [ ] **The minting tool** (Day 298's interim route, pending until the user's new Mac): `tools/metal_mint`, an Objective-C++ tool on the driver:
      `newLibraryWithSource:` of an ordinary (executable) library, then the undocumented `serializeToURL:error:` reached through `respondsToSelector:`
      (a missing selector or a failed call is a clear error naming `KNOWN-ISSUES.md`, never a crash); the output is checked before it is written (it
      loads, its type is executable, every expected entry creates a function). The tool records the minting macOS build and the compiler version
      (`metalfe-...`) beside each blob, and the minted-output check compares bytes only on the macOS build that minted them (elsewhere it checks that the
      files load). The route is isolated in the tool so a later Xcode route replaces one file; `ENGINE_METAL_MINT` / discovery like `ENGINE_DXC`, and
      without it the build uses the committed outputs. `tools/` rules: the tool may use what the spike needs; it never links into the engine.
- [ ] **`shader_mint`** runs `slangc -target metal` with the same entry, stage and `-matrix-layout-column-major` flags, then the minting tool, and writes
      `<name>.metallib.inc`; permuted shaders (`--permute`) mint one `.metallib` per key with the same dedupe; the `.inc` format matches the other two.
- [ ] **`ShaderFormat::Msl` becomes `MetalLib`** (`engine/rhi/enums.h`, `ShaderFormatName` returns `metallib`), `ShaderBlobSet` carries it, the null device
      accepts it when asked; every reference renamed; the RHI shader docs updated.
- [ ] **Entry names:** a shader whose entry is `main` comes out as `main_0`; the blob records the entry name the library exports (read from the minted
      library's function names by the tool), and `CreateShaderFromSet` passes it; no backend guesses.
- [ ] **Metal slots (Day 298 finding):** the D3D12 `register(..., spaceN)` annotations also set Slang's Metal slots, and Metal ignores the space, so
      `gbuffer.vert` and `lighting.frag` get two parameters at `[[buffer(0)]]` and the Metal compiler rejects them; emit `register()` only in the D3D12 build
      (a macro in `target_common.slang`), keep the DXIL bytes identical (re-minted on the PC), and correct `shaders/README.md`'s slot example.
- [ ] **Clip space Y:** `ENGINE_CLIP` in the Metal build, as `shaders/target_common.slang` does for D3D12; the SPIR-V and DXIL outputs stay byte-identical
      (checked by re-minting them).
- [ ] **Per-object index:** the Metal build uses `ENGINE_OBJECT_INDEX_STREAM` (the D3D12 mechanism); `DeviceCaps::object_index_stream` will be true on
      Metal (Day 316); the SPIR-V output unchanged.
- [ ] **`fill.comp` and `saxpy.comp`:** the element count comes from a constant instead of `GetDimensions` on all targets (the RHI compute tests pass it);
      `portability.cmake`'s exception for them removed; the Vulkan and DX12 compute golden tests still pass (filed for the other machines).
- [ ] **Reflection:** the tool writes, per shader, the Metal slot of every binding (`[[buffer(n)]]`, `[[texture(n)]]`, `[[sampler(n)]]`) next to the Vulkan
      set and binding, and the compute threadgroup size, into the reflection `shader_mint` already emits; Day 318 and Day 319 read it.
- [ ] **Determinism:** mint twice and compare bytes (a ctest, like the existing minted-output checks; on the same macOS build only, see the tool task); `shaders_portability` gains the minting step.
- [ ] **Documentation:** `shaders/README.md` (Metal: the minting route, entry names, clip Y, slots), `docs/guide/rhi-shaders.md`, `BUILD.md` (the
      minting prerequisite), `PLAN.md` assumption #18 (the third format); `KNOWN-ISSUES.md` lines for the Metal Y flip, `main` rename, object index and
      `GetDimensions` closed; `docgen lint` zero; reference regenerated.
- [ ] **Runs:** `scripts/test-phase.sh --affected --flavor all` (quote); the Linux and Windows re-mint checks (SPIR-V and DXIL unchanged) and their compute
      tests filed.
- [ ] **End-of-day review:** `/code-review high`. Extra scrutiny: the guards around the undocumented call (a changed or missing API fails loudly), the check of each file before it is written, determinism,
      the `.inc` generation for permutations.
- [ ] Update `STATUS.md`, `KNOWN-ISSUES.md`; commit and push.

**Done:** every shader and permutation has a committed, deterministic `.metallib.inc` with its entry name and Metal slots in the reflection; the SPIR-V and
DXIL outputs are unchanged; `ShaderFormat::MetalLib` replaces `Msl`; the four Metal shader items in `KNOWN-ISSUES.md` are closed.
