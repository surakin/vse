# Phase 26 / Day 282 — Spike: Minting `.metallib` Files Without Xcode

**Phase:** [Phase 26 — macOS Port + Metal Backend](../phase-26-macos-metal.md)
**Depends on:** nothing in the engine (throwaway programs only).

Apple ships the offline Metal compiler (`metal`, `metallib`) only with Xcode (since Xcode 26 as the separate download `xcodebuild -downloadComponent
MetalToolchain`); the Command Line Tools do not include it. The driver's own compiler ships with macOS. A probe on 2026-11-10 (this Mac, Command Line Tools
only, a throwaway Swift program) minted a library through `MTLCompileOptions.libraryType = .dynamic`, `MTLDevice.makeDynamicLibrary(library:)` and
`MTLDynamicLibrary.serialize(to:)`; the 27 KB fat container (magic `cbfebabe`, AIR slice `__AIR_DATA` plus native `INTEL_*` sections) loaded back through
`makeLibrary(URL:)` with its vertex entry usable. Today decides whether a small `tools/metal_mint` can mint the committed `.metallib` files on any Mac,
or whether minting needs Xcode (or Apple's Metal Developer Tools for Windows). No engine code; the throwaway programs live in the scratch directory and
only their results and the commands are recorded.

## Tasks

- [x] **Inputs:** mint the Slang `-target metal` output of three real shaders (`gbuffer.vert`, a lighting fragment shader, `fill.comp` with a hand-made
      element-count constant) with the probe route, using `slangc` v2026.18.2 (install it first; kitty's `slangc` on `PATH` is not the Slang compiler).
- [ ] **Load elsewhere — this decides the spike.** Load the minted files on an Apple silicon Mac and on a macOS version other than 15.8 (a second Mac, a
      colleague's machine or a macOS VM with paravirtualized Metal; record which). If none is reachable today, record that, keep the spike open on this task
      only, and continue with the rest. If a file fails to load on either, the route is rejected.
- [x] **Strip to AIR.** Try `lipo -thin`/`-remove` on the container (the format is undocumented); otherwise parse the fat header (`cbfebabe`, big-endian
      slice table) in the throwaway program and write the AIR slice alone. The stripped file must load on this Mac and on the machines of the previous task.
- [x] **Determinism.** Mint the same source twice on this Mac and compare bytes; mint it on a second machine if one is reachable and compare. Name every
      byte range that differs (timestamps, UUIDs, paths, the install name) and whether it can be fixed (a fixed install name, zeroed fields after
      checking they are not checksummed) or not. Minting must be deterministic (`PLAN.md` principles); a route that cannot be made so is rejected.
- [x] **Dynamic against ordinary library** for the engine's shaders: exported (mangled) helper names, the install name, function constants, the
      `[[buffer(n)]]` / `[[texture(n)]]` / `[[sampler(n)]]` bindings, and the reflection the backend will read (`MTLFunction` arguments through a
      pipeline's reflection). A pipeline built from a function of the loaded dynamic library must render one triangle into an offscreen texture and read
      back the expected pixel.
- [x] **Fallback measured once:** (not possible: Xcode not installed; see the result) if Xcode is installable on this Mac (macOS 15.8 runs Xcode 26), time how long `metal`/`metallib` takes for the same
      three shaders and whether their output is deterministic, so the fallback is a known quantity. If not installed, say so.
- [x] **Decide and record:** (interim decision, pending: see the result) `metal_mint` on the driver, or Xcode (or Apple's Metal Developer Tools for Windows on the PC). Write the decision, the
      load and determinism results and the commands into the phase file ("Design decisions", item 10), `PLAN.md` assumption #18, the
      `KNOWN-ISSUES.md` line it resolves (or narrows), and the `BUILD.md` macOS prerequisites that follow (Day 283 writes the section).
- [ ] No code review (no product code). Update `STATUS.md`; commit and push.

**Done:** the decision is recorded in the phase file and in `PLAN.md` assumption #18, with the load results (which machines and macOS versions), the
strip result and the determinism comparison quoted; or, if no second machine was reachable, the spike is recorded as open on the "load elsewhere" task
only, with the rest done and the interim choice (the driver route, at risk) stated.

## Result (2026-11-10, on the Mac; throwaway programs in the session scratch directory, nothing committed)

Inputs: `slangc` v2026.18.2 for macOS x86-64 (release archive `slang-2026.18.2-macos-x86_64.tar.gz`, SHA-256
`d09a2993b4ed6d81c0c19ff8bede27944bf73437d982549bd7b2ac5a1a3202b9`), `-target metal -entry main -stage <stage> -matrix-layout-column-major` as
`shaders/portability.cmake` does, on `triangle.vert`, `triangle.frag`, `gbuffer.vert`, `lighting.frag` and `gradient.comp` (`fill.comp` needs the
`GetDimensions` change of Day 299 first). The Metal device: Intel Iris Plus 645, Metal 3 and `MTLGPUFamilyMac2` supported, `hasUnifiedMemory` 1,
`isLowPower` 1, `isRemovable` 0, **argument-buffer tier 1** (decision 9 stands), read-write texture tier 1, `recommendedMaxWorkingSetSize` 1,536 MiB,
`maxBufferLength` 2,048 MiB, dynamic libraries and ray tracing supported. Driver compiler: `metalfe-32023.622` on macOS 15.8.1.

**1. The probed route (dynamic library, `MTLDynamicLibrary serializeToURL:`) does not work for entry points: rejected.** The minted file loads
(`newLibraryWithURL:` succeeds, `functionNames` lists `main_0`), but every way of getting the function (`newFunctionWithName:`,
`newFunctionWithDescriptor:`, `newFunctionWithName:constantValues:`) stops the process with the Metal framework's assertion `validateMTLFunctionType:
type is not a valid MTLFunctionType`. A dynamic library's functions are meant to be linked into a pipeline (`preloadedLibraries`), not used as its
vertex, fragment or kernel function; the probe of the morning saw the entry point listed, not created. The file is a fat container (`cbfebabe`, two
slices): slice 1 a Mach-O with `__AIR_DATA` and LC_NOTEs, holding a complete AIR metallib (`MTLB`, 3,220 bytes for the triangle) at byte 18,048; slice 2 a
bare `MTLB` with the Intel-native code (3,252 bytes). `lipo` cannot read the container. The extracted AIR metallib and the native one load and assert the
same way; patching the header's type byte (offset 0x0A: `00` executable, `02` dynamic, compared with system metallibs) from 02 to 00 still loads as a
dynamic library (the `DYN` tag marks it too). Going further means reverse-engineering an undocumented format: stopped.

**2. The documented binary archive does not hold entry points on its own.** `MTLBinaryArchive addFunctionWithDescriptor:library:` with the vertex
function adds nothing (`serializeToURL:` answers "Nothing to serialize"): it is for visible functions. An archive of whole pipelines holds GPU binaries
for one device, not a library.

**3. Only the undocumented `-[MTLLibrary serializeToURL:error:]` of an ordinary (executable) library gives a usable file**, and on this machine it
gives exactly what the plan wants: a plain AIR-only executable `MTLB` (no fat container, no native slice; triangle vertex 3,872 bytes, fragment 3,632,
`gbuffer.vert` 10,256, `lighting.frag` 12,804, `gradient.comp` 4,316). Each loads with `newLibraryWithURL:` as type 0, `main_0` comes back with the
right function type, the vertex attributes and the compute bindings reflect (`position_1@0 normal_1@1 tangent_1@2 uv_1@3`; `img_1@0`, 1,024 threads), a
pipeline built from the triangle pair renders offscreen with `MTL_DEBUG_LAYER=1`, `MTL_DEBUG_LAYER_ERROR_MODE=assert` and `MTL_SHADER_VALIDATION=1`
silent and the expected pixels (centre 63 67 126 for an expected 64 64 128, corner clear). **Determinism on one machine: byte-identical** for all five
across separate processes. No path or timestamp is embedded; the file does embed the **driver compiler's version** (`metalfe-32023.622`) and the
target triple **`air64-apple-macosx15.8.0`**, which `MTLCompileOptions.languageVersion` (3.0, 2.4) does not change and which no compile option lowers.
So minted bytes change with each macOS update that changes the compiler, two machines agree only on the same macOS build, and whether a file marked
15.8 loads on the macOS 13 deployment target is unknown. The plan already excludes this API.

**4. Shader finding for Day 299 (independent of the route):** the Metal compiler rejects the Slang output of `gbuffer.vert` and `lighting.frag`
(`cannot reserve 'buffer' resource location at index 0`): the D3D12 `register(b0, space0)` / `register(t0, space2)` annotations added in Phase 13 also
set the Metal slots, and Metal ignores the space, so `frame` and `objects` (and `frame` and `push`) both get `[[buffer(0)]]`. `shaders/README.md`'s
"frame `[[buffer(0)]]`, objects `[[buffer(1)]]`" is from before those annotations. With the annotations removed (a throwaway copy) Slang assigns distinct
slots (`gbuffer.vert`: buffers 1 and 0; `lighting.frag`: textures 0-3, buffers 0-2) and both shaders mint and load. Day 299: emit `register()` only
in the D3D12 build. The dynamic route also exported the helper functions under mangled names (`_Z12ShadeLight_0...`); the executable route lists only
`main_0`.

**Not done:** loading on Apple silicon or another macOS version (no second machine reachable today): the deciding task stays open. Xcode is not
installed (20 GiB free on the disk), so the fallback was not timed.

**Decision (the user, 2026-11-10): interim, pending.** The documented driver route is rejected. The options were (a) Xcode 26 with its Metal
toolchain on this Mac, (b) Apple's Metal Developer Tools for Windows on the PC, (c) the undocumented `serializeToURL:` with its stated risks. The user
chose **(c) for now, until they have a new Mac**; then the route is decided again and the new Mac runs the open "load elsewhere" task. Recorded in the
phase file (decision 10, notes), `PLAN.md` assumption #18, `KNOWN-ISSUES.md` (pending) and `BUILD.md`. Day 299 builds `metal_mint` on route (c) with the
guards it needs (below).
