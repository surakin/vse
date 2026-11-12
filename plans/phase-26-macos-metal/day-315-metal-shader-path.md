# Phase 26 / Day 315 — The Shader Path for Metal: `.metallib` Blobs, Entry Names, Clip Space, Object Index

**Phase:** [Phase 26 — macOS Port + Metal Backend](../phase-26-macos-metal.md)
**Depends on:** Day 314 (and the Day 298 decision: the interim, pending route through `-[MTLLibrary serializeToURL:error:]`).

Every shader mints a third blob, an AIR-only `.metallib`, committed beside `*.spv.inc` and `*.dxil.inc`, deterministically, by the route Day 298
chose. The shader source changes that Metal needs land on all three targets.

## Tasks

- [x] **The minting tool** (Day 298's interim route, pending until the user's new Mac): `tools/metal_mint`, an Objective-C++ tool on the driver:
      `newLibraryWithSource:` of an ordinary (executable) library, then the undocumented `serializeToURL:error:` reached through `respondsToSelector:`
      (a missing selector or a failed call is a clear error naming `KNOWN-ISSUES.md`, never a crash); the output is checked before it is written (it
      loads, its type is executable, every expected entry creates a function). The tool records the minting macOS build and the compiler version
      (`metalfe-...`) beside each blob, and the minted-output check compares bytes only on the macOS build that minted them (elsewhere it checks that the
      files load). The route is isolated in the tool so a later Xcode route replaces one file; `ENGINE_METAL_MINT` / discovery like `ENGINE_DXC`, and
      without it the build uses the committed outputs. `tools/` rules: the tool may use what the spike needs; it never links into the engine.
- [x] **`shader_mint`** runs `slangc -target metal` with the same entry, stage and `-matrix-layout-column-major` flags, then the minting tool, and writes
      `<name>.metallib.inc`; permuted shaders (`--permute`) mint one `.metallib` per key with the same dedupe; the `.inc` format matches the other two.
- [x] **`ShaderFormat::Msl` becomes `MetalLib`** (`engine/rhi/enums.h`, `ShaderFormatName` returns `metallib`), `ShaderBlobSet` carries it, the null device
      accepts it when asked; every reference renamed; the RHI shader docs updated.
- [x] **Entry names:** a shader whose entry is `main` comes out as `main_0`; the blob records the entry name the library exports (read from the minted
      library's function names by the tool), and `CreateShaderFromSet` passes it; no backend guesses.
- [x] **Metal slots (Day 298 finding):** the D3D12 `register(..., spaceN)` annotations also set Slang's Metal slots, and Metal ignores the space, so
      `gbuffer.vert` and `lighting.frag` get two parameters at `[[buffer(0)]]` and the Metal compiler rejects them; emit `register()` only in the D3D12 build
      (a macro in `target_common.slang`), keep the DXIL bytes identical (re-minted on the PC), and correct `shaders/README.md`'s slot example.
- [x] **Clip space Y:** `ENGINE_CLIP` in the Metal build, as `shaders/target_common.slang` does for D3D12; the SPIR-V and DXIL outputs stay byte-identical
      (checked by re-minting them).
- [x] **Per-object index:** the Metal build uses `ENGINE_OBJECT_INDEX_STREAM` (the D3D12 mechanism); `DeviceCaps::object_index_stream` will be true on
      Metal (Day 316); the SPIR-V output unchanged.
- [x] **`fill.comp` and `saxpy.comp`:** the element count comes from a constant instead of `GetDimensions` on all targets (the RHI compute tests pass it);
      `portability.cmake`'s exception for them removed; the Vulkan and DX12 compute golden tests still pass (filed for the other machines).
- [x] **Reflection:** the tool writes, per shader, the Metal slot of every binding (`[[buffer(n)]]`, `[[texture(n)]]`, `[[sampler(n)]]`) next to the Vulkan
      set and binding, and the compute threadgroup size, into the reflection `shader_mint` already emits; Day 318 and Day 319 read it.
- [x] **Determinism:** mint twice and compare bytes (a ctest, like the existing minted-output checks; on the same macOS build only, see the tool task); `shaders_portability` gains the minting step.
- [x] **Documentation:** `shaders/README.md` (Metal: the minting route, entry names, clip Y, slots), `docs/guide/rhi-shaders.md`, `BUILD.md` (the
      minting prerequisite), `PLAN.md` assumption #18 (the third format); `KNOWN-ISSUES.md` lines for the Metal Y flip, `main` rename, object index and
      `GetDimensions` closed; `docgen lint` zero; reference regenerated.
- [x] **Runs:** deferred to the end of Day 325 (the user's choice for Days 315-325, 2026-11-12: build each day, test at the end); the Linux and
      Windows re-mint checks (SPIR-V and DXIL unchanged) and their compute tests filed.
- [x] **End-of-day review:** `/code-review high`. Extra scrutiny: the guards around the undocumented call (a changed or missing API fails loudly), the check of each file before it is written, determinism,
      the `.inc` generation for permutations.
- [x] Update `STATUS.md`, `KNOWN-ISSUES.md`; commit and push.

**Done:** every shader and permutation has a committed, deterministic `.metallib.inc` with its entry name and Metal slots in the reflection; the SPIR-V and
DXIL outputs are unchanged; `ShaderFormat::MetalLib` replaces `Msl`; the four Metal shader items in `KNOWN-ISSUES.md` are closed.

## As built (2026-11-12, the Mac)

- **`tools/metal_mint/macos/metal_mint.mm`** (macOS only, dev-time): `newLibraryWithSource:` (MSL 3.0, executable), the undocumented
  `serializeToURL:error:` reached through `respondsToSelector:` and the method's own implementation (a missing selector or a failed call is an
  error naming `KNOWN-ISSUES.md`), then the checks before writing: the `MTLB` tag, an executable library, it loads through `newLibraryWithData:`,
  it exports exactly one function and that function has the stage's type. It writes the library and an info file (`entry`, `compiler`
  `metalfe-...`, the macOS build) with `io::WriteFileAtomic`.
- **`shader_mint`:** `--metal-reflection` (plain) and `--metal-arg` (permute) read the Metal build's reflection on every platform and put each
  binding's slot into the reflection (`ApplyMetalSlots`: matched by name, the class from the binding type, the push-constant block named by the
  SPIR-V build, every Metal parameter one the SPIR-V build declares, no two resources in one slot; filled in a copy, committed only on success);
  `--metallib`/`--metal-mint` and `--metallib-out` write `<name>.metallib.inc` (bytes, `<Base>MetallibEntry`, `<Base>MetallibSpirvFnv`, the
  compiler line with the macOS build). Without a library the committed `.metallib.inc` is kept when it has the modules, else a placeholder (null
  blobs) is written, so a machine without a Mac still builds. The `.dxil.inc` includes the `.metallib.inc` and its blob set holds
  `rhi::MetalLibBlob`; the `.perm.inc` lists each module's library. The committed `.dxil.inc` files were edited to that form here (no `dxc` on
  the Mac); the PC's re-mint must write the same.
- **Reflection format 3:** `ReflectedBinding::metal_slot`, `ShaderReflection::has_metal_slots` and `push_constant_metal_slot`, validated (a slot for
  every binding and the push block, none shared within an argument table, within Metal's 31 buffers, 128 textures and 16 samplers).
- **RHI:** `ShaderFormat::MetalLib` (`metallib`), `ShaderBlob::entry`, `ShaderBlobSet::metallib` (`For` returns the stored blob with its format
  set), `rhi::MetalLibBlob`, `ShaderDesc::SetCode` sets the entry (the blob's, else `main`), and `Validate` checks a Metal library's tag, its type
  (executable) and the size its header records (a u64 at offset 16, found today).
- **Shaders:** `ENGINE_TARGET_METAL` shares D3D12's `ENGINE_CLIP`, `ENGINE_VERTEX_ID` and `ENGINE_OBJECT_INDEX_STREAM`; `ENGINE_REGISTER(r, s)` is
  `: register(r, s)` for D3D12 only (24 sources; 15 gained the include of `target_common.slang`). Re-minted on the Mac: every `.spv.inc` and
  `.refl.inc` byte-identical with the macro (before the format change), then `fill.comp` and `saxpy.comp` changed (`{k, count}`, `{a, count}`,
  bounded by the buffers' lengths where the target can ask).
- **Minted:** 29 `.metallib.inc` files (and the permutation modules), `metalfe-32023.622` on macOS 15.8.1 (24H32); a second mint of all 120 output
  files was byte-identical (twice: before and after the review's changes). `shaders_check` mints each library twice (permuted shaders: two runs
  into two directories) and compares, and compares the committed library only on the same macOS build; `shaders_portability` builds Metal with the
  Metal define, has no known failure left, and on a Mac compiles every key with the driver. A reflection minted without `metal_mint` was checked
  byte-identical to the Mac's, and the placeholder path was tried by hand.
- **Tests added** (run on Day 325): `shader_metallib_test.cpp` (every library's SPIR-V hash, entry and validation; the blob sets), the MetalLib
  cases in `types_test.cpp`, the Metal-slot rules and round trip in `shader_reflection_test.cpp`, `ApplyMetalSlots` in
  `tests/shader_mint/d3d12_conventions_test.cpp`; the compute tests push 8 bytes.
- **Accident, recovered:** a `scripts/format.sh` call was given every changed file, and clang-format rewrote the day's Markdown, `.txt`, `.cmake`
  and `CMakeLists.txt` files as C++. They were restored from `HEAD` and the day's edits replayed; the replayed build scripts were compared with the
  damaged copies (the same code, whitespace and comments aside) and the rest checked line by line against `HEAD`.
- **Runs:** deferred to Day 325. Built: both unity modes on macOS with no warning, the reference regenerated and `docgen lint` passing.

## Post-implementation code review

`/code-review high` on the day's diff: 10 findings, all fixed.

1. **The new `shader_metallib_test` checked the permutation modules' DXIL as Metal libraries** (the rows were copied from the DXIL test). Fixed:
   the 14 module rows name each module's `metallib`, `metallib_size` and `metallib_entry`.
2. **`tools/metal_mint` had no line in the module map** (the `module_deps_check` gate would fail). Fixed: `modules.txt` (module and target) and
   `ARCHITECTURE.md`.
3. **A shader added (or a permutation changed) off a Mac would name Metal symbols no file defines**, breaking the build on Linux and Windows.
   Fixed: without a library, `shader_mint` keeps the committed `.metallib.inc` when it has the modules and otherwise writes a placeholder (null
   blobs); checked by hand (and its reflection came out byte-identical to the Mac's).
4. **`fill.comp` and `saxpy.comp` trusted the push-constant count alone.** Fixed: bounded by the buffers' lengths where the target can ask
   (SPIR-V, DXIL); on Metal the caller's count is the bound, documented.
5. **`metal_mint` could raise on a nil `NSError` description** (appending nil). Fixed: `Reason(error)`.
6. **`ApplyMetalSlots` left the slots it had set when it failed.** Fixed: it fills a copy and commits only on success; tested.
7. **The Metal-slot validation and `ApplyMetalSlots` had no tests.** Fixed: a reflection case for every rule and the round trip
   (`shader_reflection_test`), and `ApplyMetalSlots` with a good reflection and four bad ones (`d3d12_conventions_test`).
8. **The permuted shaders' libraries were minted once in `shaders_check`.** Fixed: two runs into two directories, compared.
9. **The D3D12 wrong-format error still said "MSL".** Fixed: `ShaderFormatName`.
10. **`ShaderBlobSet::For` rebuilt each blob field by field**, dropping new fields. Fixed: it returns the stored blob with its format set.
