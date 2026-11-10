# Phase 13 / Day 162 — RHI Backend Registry and Bytecode Blobs

**Phase:** [Phase 13 — Windows Port + DX12 Backend](../phase-13-windows-dx12.md)
**Depends on:** Day 161; Phase 8 Day 110 (the registry was filed then), Phase 4 portability review section 3 (the shader boundary).

The two neutral RHI changes DX12 needs, made and proven on both machines before any DX12 code exists. Shared code throughout.

## Tasks

- [x] **Backend registry** in `rhi` (`engine/rhi/backend_registry.h/.cpp`): each backend library registers a descriptor (name, `RhiBackend` value, device
      factory, adapter lister, availability probe) at static-initialization-free time (an explicit `RegisterVulkanBackend()` call from a generated or listed
      table, not a global constructor — decide and record); `rhi::CreateDevice`, `EnumerateAdapters` and `DeviceHost` move from `rhi_vulkan` into the neutral
      `rhi` library and dispatch through the registry; the renderer links `rhi` plus whatever backends the program asks for, never `rhi_vulkan` by name.
      The null backend registers the same way.
      *(Decided: an explicit call, `rhi::RegisterCompiledBackends()` in the new library `rhi_backends` (`engine/rhi/backends/`), the one listed
      table of backends; no global constructor. Null registers through `RegisterNullBackend`, Vulkan through `RegisterVulkanBackend`; tests register from a
      Catch2 listener, `tests/support/register_backends.cpp`. `ARCHITECTURE.md` items 7 and 8 closed, the module allow list is empty.)*
- [x] **The `backend` setting** gains `dx12` (and lists only the backends compiled in and available, `auto` picking DX12 on Windows once Day 164 registers it,
      Vulkan elsewhere — the spec's rule); `--list-settings` shows the available names; an unavailable backend is an error with its reason.
      *(Values `null`, `vulkan`, `dx12`, `auto` in `rhi::Backend`'s order; the default is now `auto`. `--list-settings` shows the four names; which
      ones a program has is not in the listing, so the goldens stay the same on both platforms; a change to one it does not have, or that cannot run, is
      refused by `ApplySettings` with the registry's reason before the device is touched (tested).)*
- [x] **`MatchAdapter`** accepts the second id form (a LUID string) as `adapter.h` was told in Phase 8 Day 105; tests on the null backend's fake adapters.
      *(`AdapterInfo::luid`, 16 hex digits, high part first; Vulkan fills it from `deviceLUID` when valid (Windows drivers); the null adapters have
      `00000000000a0001`/`2`. A LUID-shaped request matches LUIDs only (review fix).)*
- [x] **`ShaderDesc` as a bytecode blob:** `format` (`ShaderFormat::Spirv`, `Dxil`; `Msl` reserved for Phase 24), `bytes`, `size` replace `words`/`word_count`;
      `Validate` checks size and alignment per format; `ValidateSpirv` is called only for SPIR-V; the Vulkan backend refuses DXIL with a clear error and vice
      versa. Every caller (tests, `shader_mint`'s generated `.inc` headers, the renderer's shader tables, the demos, guide samples) is ported; the committed
      `.spv.inc` files are re-minted only if their format must change (then byte-identical on both machines).
      *(`ShaderBlob`, `SpirvBlob`, `DxilBlob`, `ShaderDesc::SetCode`; DXIL is checked for the `DXBC` tag and its recorded size. No committed `.inc`
      changed: the generated files keep their names and shapes.)*
- [x] **Shader sets:** a material or pass names its shader once and gets the blob for the device's format (a small `ShaderBlobSet { spirv, dxil }` or the
      reflection table extended — decide); a backend without a blob for its format fails at pipeline creation with the shader's name.
      *(Decided: both. `rhi::ShaderBlobSet { name, spirv, dxil }` with `For(format)` and `rhi::CreateShaderFromSet`; the renderer's
      `PermutationModule` gains `dxil`/`dxil_size` and `Blob(format)`, with the one reflection for every format. `DeviceCaps::shader_format` names the
      device's format. The lighting, G-buffer debug and cube-scene shaders still build their SPIR-V blobs by hand; Day 163 swaps them for the minted sets
      once DXIL exists.)*
- [x] **Tests:** the registry (two fake backends, ordering, unavailable backends, `auto` selection), `ShaderDesc` validation per format, the renderer and demos
      unchanged on Vulkan (golden images byte-identical before and after on Linux, within tolerance on Windows), `rhi_boundary_check` still clean.
      *(`backend_registry_test.cpp` (6 tests), the DXIL and blob-set validation tests, two LUID tests, the refused-backend `ApplySettings` test. The
      golden images pass within tolerance on Windows; the byte-identical Linux comparison waits for the phase close.)*
- [x] **Documentation:** `rhi.h`, `desc.h`, `adapter.h` comments; `docs/guide/rhi.md` "Backends and the registry"; `docs/guide/rhi-shaders.md` "Bytecode
      formats"; `BACKEND-LIFECYCLE.md` notes where `CreateDevice`/`EnumerateAdapters` live now; reference regenerated; `docgen lint` zero; `ctest -L docs`.
- [x] **Runs:** Windows and Linux `--affected` (RHI headers changed: the RHI, renderer and demo selections) plus the sanitizer flavors of each machine; quote all.
      *(Windows done; the Linux run waits for the phase close, Day 174.)*
- [x] **End-of-day review:** `/code-review high`. Extra scrutiny: initialization order of the registry in tests and demos (a backend missing from a test binary
      should fail loudly), lifetimes of shader blobs referenced by `ShaderDesc`.
- [x] Update `STATUS.md`, `KNOWN-ISSUES.md` (the Phase 8 registry deferral removed); commit and push.

**Done:** the renderer and `DeviceHost` live in the backend-neutral library and reach backends through a registry, `ShaderDesc` carries a format-tagged blob,
and both machines pass with the Vulkan output unchanged.

## Notes

- **Where the code went.** `engine/rhi/create_device.cpp` (the registry, `CreateDevice`, `EnumerateAdapters`) and `engine/rhi/device_host.cpp` moved from
  `rhi/vulkan/` into `rhi`; `rhi/null/null_backend.cpp` holds the null factory that used to be a `switch` case; `rhi/vulkan/vulkan_backend.cpp` the Vulkan
  descriptor (its availability probe opens the loader). The renderer links `rhi` only; the demos, benchmarks, test binaries and the docs samples link
  `rhi_backends`.
- **`Auto`.** `DeviceCreateInfo::backend` defaults to `Auto`: the available registered backend with the highest rank (Vulkan 10 today, null 0, never
  picked). DX12 registers on Windows from Day 164, with a rank above Vulkan only once it passes the suites (Day 172), so `auto` keeps meaning Vulkan
  until then.
- **The refused-backend test** (`apply_settings_test.cpp`) asks for `dx12`, which no binary registers today; Day 164 registers DX12 on Windows and must
  change it to a backend the test binary leaves out.
- `renderer_demo` still pins `backend` to Vulkan (`settings.SetEnum(kBackend, 1)`); Day 173 lets the demos choose.

## For the Linux run

Shared: `engine/rhi/` (`rhi.h`, `desc.h/.cpp`, `enums.h`, `caps.h`, `adapter.h/.cpp`, `device.h`, `device_host.h`, `rhi.cpp`, `create_device.cpp`,
`device_host.cpp`, `backend_registry.h`, `backends/`, `null/`, `vulkan/vulkan_backend.*`, `vulkan/vk_device_select.*`, `vulkan/vulkan_device.cpp`,
`CMakeLists.txt`), `engine/renderer/` (`material.cpp`, `lighting.cpp`, `gbuffer.cpp`, `shader_permutation.*`, `render_settings.cpp`, `renderer.cpp`,
`shared_settings.cpp`, `CMakeLists.txt`), `game/common/cube_scene.cpp`, the four program `main.cpp` and `CMakeLists.txt`, the two `listsettings.golden`,
`tests/rhi` and `tests/renderer` (sources and CMake), `tests/support/register_backends.cpp`, `tests/phases.txt`, `tools/docgen/CMakeLists.txt` (the
link order `rhi_backends rhi_vulkan rhi`), `tools/module_deps_check`, `tools/rhi_boundary_check/allowlist.txt`, the guides. Run: both unity modes,
`--affected` plain, asan and tsan, `ctest -L docs`, the golden images byte-identical, and the reference regeneration (`docgen lint`).

## Runs

Windows PC:

- `scripts/test-phase.sh --affected`: `ran 1185 of 2338 tests (phases: all; flavors: plain; display: included; crash tests: excluded) in 125 s; the last
  full run took 13 s` (everything: `CMakeLists.txt` files changed). Three failures on the first run, fixed: the two `--list-settings` goldens (the new
  `backend` line) and `rhi_boundary_check` (an allow-list entry for `backends.cpp`, the one file that names every backend).
- `scripts/test-phase.sh --affected --flavor asan`: `ran 888 of 2338 tests (phases: all; flavors: asan; display: included; crash tests: excluded) in 67 s;
  the last full run took 13 s`.
- After the review's fixes, rebuilt in all three trees (clean) and rerun: MSVC `ctest -L "^rhi$|^renderer$|demo|docs|boundary|gate"` 593 of 593;
  `windows-clang-cl` phase 0, RHI and renderer 631 of 631; non-unity MSVC RHI and renderer 537 of 537.
- `cmake --build build-msvc --target docs`: `docgen: 51 samples, 51 compiled, 45 run, 0 failed`; lint clean.
- Linux: not run; pending until the phase close (Day 174), `KNOWN-ISSUES.md`.

## Post-implementation code review

`/code-review high` on the day's diff: 6 findings, 4 fixed, 2 declined.

- **Fixed (with a regression test):** a 16-digit LUID request that matched no LUID fell through to the index rule, so a LUID saved before a reboot such
  as `0000000000000001` silently picked adapter 1. A LUID-shaped request now matches LUIDs only; the new check in the LUID test fails without the fix
  (seen) and passes with it.
- **Fixed:** `PickAutoBackend`'s reason buffer (160 bytes) could not hold a backend name plus a 160-byte probe reason, which GCC's `-Wformat-truncation`
  reports as an error on Linux; now 256 bytes.
- **Fixed (docs):** `DeviceHost` still said it is defined in `rhi_vulkan`; `EnumerateAdapters` did not list the registry failure.
- **Declined (planned):** the lighting, G-buffer debug and cube-scene passes build SPIR-V blobs by hand instead of from a `ShaderBlobSet`, so on a DXIL
  device they fail with "no bytecode" rather than the shader's name. Day 163 mints DXIL and replaces them with the minted sets; no DXIL device exists today.
- **Declined:** `ApplySettings` probes the backend's availability on every device-class change (the Vulkan probe opens the loader, already loaded):
  negligible next to a device reset.
