# Maintenance: script handles (a resource id a Lua number cannot hold)

Not a numbered phase, no day files. Answers `script-resource-ids.md` (the user chose handles over paths, 2026-11-24): a clip id from a seeded import is a full
64-bit number, and `animation.clip` failed with "an integer outside +-2^53 cannot be represented as a Lua number". Looking a clip up by path on every call is
also slow. A script now holds a **handle**: a value the engine made, that names something by a 64-bit id the script can neither read nor forge.

## Design

- `script::Value` gets a kind `Handle`: a type code (`HandleType`, `Resource` first; room for others) and a `u64` id. Entities stay as they are (the user,
  2026-11-24: resource ids only, with a solution general enough to take more types later).
- In LuaJIT a handle is a userdata of 8 bytes with the metatable `engine.resource` (one metatable per type): `__eq` (same id), `__tostring`
  (`resource(<hex>)`), `__metatable = false`. The id is never a Lua number. `type(h)` is `"userdata"`.
- Bindings take `script::ResourceHandle` (a codec of `bindings.h`): the registry checks the kind and the type and names the expected type in the error.
- Handles are copied through events, partitions and stored script state like any value. The stored record keeps the type and the id (the encoder cannot refuse a
  value, and a resource id is stable, so the record round-trips). A handle that leaves through the C API (an event payload, a host function argument) arrives as nil
  (the writer has no failure path): no ABI change.
- Applied: `animation.clip` returns a handle (nil when there is none); `animation.play` takes a handle or a clip path; `mesh.set_slot` takes a texture
  handle for a texture field (besides a host texture name); `environment.set_sky_cube` takes a texture handle. A raw integer id is no longer accepted by
  these (it could never exceed 2^53 from Lua). New `resource.find(path)` returns a handle for any mounted resource, or nil.
- Not done: equality as a table key (handles are not interned); entity handles.

## Done

- [x] `Value` kind `Handle` (value, check, equals, hash, copy, codec)
- [x] LuaJIT userdata and the binding codec
- [x] `animation.clip/play`, `mesh.set_slot`, `environment.set_sky_cube`, `resource.find`
- [x] C API test (a clip id above 2^53 played by handle and by path; errors)
- [x] guide text
- [ ] checked on Windows and macOS (see `KNOWN-ISSUES.md`)

## Post-implementation code review

`/code-review high` on the diff: 3 findings, all nits, none fixed on purpose. (1) A handle that reaches the C API arrives as nil (the writer has no failure
path): declined, recorded in `KNOWN-ISSUES.md`. (2) Handles are not interned, so two equal handles are two table keys: declined, in the guide and
`KNOWN-ISSUES.md`. (3) Raw integer ids are refused by `animation.play`, `mesh.set_slot` and `environment.set_sky_cube`: the intended change.
Also fixed on the way: `file_access_check` failed at HEAD because `host_script_scene.c` reads blobs with `fopen`; its allow-list line was added.
Scoped runs: `ran 1341 of 12484 tests (phases: 3 6 22 23 27 28; flavors: plain; ...)` and `ran 1144 of 12484 tests (phases: 3 6 22 23 27 28; flavors: asan; ...)`, both passing; `ctest -L docs` passes; built clean in unity and `-DENGINE_UNITY_BUILD=OFF` with clang, and with gcc.
