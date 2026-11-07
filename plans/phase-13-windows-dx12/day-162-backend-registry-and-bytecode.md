# Phase 13 / Day 162 — RHI Backend Registry and Bytecode Blobs

**Phase:** [Phase 13 — Windows Port + DX12 Backend](../phase-13-windows-dx12.md)
**Depends on:** Day 161; Phase 8 Day 110 (the registry was filed then), Phase 4 portability review section 3 (the shader boundary).

The two neutral RHI changes DX12 needs, made and proven on both machines before any DX12 code exists. Shared code throughout.

## Tasks

- [ ] **Backend registry** in `rhi` (`engine/rhi/backend_registry.h/.cpp`): each backend library registers a descriptor (name, `RhiBackend` value, device
      factory, adapter lister, availability probe) at static-initialization-free time (an explicit `RegisterVulkanBackend()` call from a generated or listed
      table, not a global constructor — decide and record); `rhi::CreateDevice`, `EnumerateAdapters` and `DeviceHost` move from `rhi_vulkan` into the neutral
      `rhi` library and dispatch through the registry; the renderer links `rhi` plus whatever backends the program asks for, never `rhi_vulkan` by name.
      The null backend registers the same way.
- [ ] **The `backend` setting** gains `dx12` (and lists only the backends compiled in and available, `auto` picking DX12 on Windows once Day 164 registers it,
      Vulkan elsewhere — the spec's rule); `--list-settings` shows the available names; an unavailable backend is an error with its reason.
- [ ] **`MatchAdapter`** accepts the second id form (a LUID string) as `adapter.h` was told in Phase 8 Day 105; tests on the null backend's fake adapters.
- [ ] **`ShaderDesc` as a bytecode blob:** `format` (`ShaderFormat::Spirv`, `Dxil`; `Msl` reserved for Phase 14), `bytes`, `size` replace `words`/`word_count`;
      `Validate` checks size and alignment per format; `ValidateSpirv` is called only for SPIR-V; the Vulkan backend refuses DXIL with a clear error and vice
      versa. Every caller (tests, `shader_mint`'s generated `.inc` headers, the renderer's shader tables, the demos, guide samples) is ported; the committed
      `.spv.inc` files are re-minted only if their format must change (then byte-identical on both machines).
- [ ] **Shader sets:** a material or pass names its shader once and gets the blob for the device's format (a small `ShaderBlobSet { spirv, dxil }` or the
      reflection table extended — decide); a backend without a blob for its format fails at pipeline creation with the shader's name.
- [ ] **Tests:** the registry (two fake backends, ordering, unavailable backends, `auto` selection), `ShaderDesc` validation per format, the renderer and demos
      unchanged on Vulkan (golden images byte-identical before and after on Linux, within tolerance on Windows), `rhi_boundary_check` still clean.
- [ ] **Documentation:** `rhi.h`, `desc.h`, `adapter.h` comments; `docs/guide/rhi.md` "Backends and the registry"; `docs/guide/rhi-shaders.md` "Bytecode
      formats"; `BACKEND-LIFECYCLE.md` notes where `CreateDevice`/`EnumerateAdapters` live now; reference regenerated; `docgen lint` zero; `ctest -L docs`.
- [ ] **Runs:** Windows and Linux `--affected` (RHI headers changed: the RHI, renderer and demo selections) plus the sanitizer flavors of each machine; quote all.
- [ ] **End-of-day review:** `/code-review high`. Extra scrutiny: initialization order of the registry in tests and demos (a backend missing from a test binary
      should fail loudly), lifetimes of shader blobs referenced by `ShaderDesc`.
- [ ] Update `STATUS.md`, `KNOWN-ISSUES.md` (the Phase 8 registry deferral removed); commit and push.

**Done:** the renderer and `DeviceHost` live in the backend-neutral library and reach backends through a registry, `ShaderDesc` carries a format-tagged blob,
and both machines pass with the Vulkan output unchanged.
