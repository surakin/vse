# Maintenance: C API the host needs to cache imported (minted) assets in its own disk cache

Not a numbered phase. Request from the the host project (2026-11-18), written against the engine at `ed6d8327`. Done 2026-11-18 (see Result).

## Why

the host shows user-made worlds and avatars as `.glb` files. Today it hands the bytes to `import_gltf` on every load (a large world can take a second or
more). the host wants to keep what the import made (the minted blobs) in its own disk cache, keyed by the `.glb`'s SHA-256 and the engine's format
version, and on the next load add the blobs back with `resource_add` and skip the import.

The write half exists: `resource_add` takes a complete blob (`ENGINE_RESOURCE_CHECK_HASH` verifies it, `UNSUPPORTED_VERSION`/`CORRUPT` refuse a stale or damaged one,
`import_gltf` returns the ids, types, paths, warnings and manifest, `id_seed` and `manifest` make ids repeatable). What is missing is getting the bytes back out
and a few things around it. The memory backend holds the blobs internally (`asset::HostMemoryBackend::Open`), but no C function exposes them.

## Missing, in the order the host needs them

1. **Read a blob back (required).** `resource_read(engine, id, engine_buffer* out)`: the complete blob of a resource in the memory backend, byte for byte as
   `resource_add` takes it (container, type, id, content hash). The host releases the buffer with `engine_buffer_release`. Fails with STALE_HANDLE,
   INVALID_ARGUMENT, NOT_FOUND (not in the memory backend, for instance a pak resource), TOO_LARGE if the host sets a limit. `resource_mounted` already reports
   size and `content_hash`, so the host can size and check before and after. Today nothing can return the bytes (`resource_mounted`/`resource_status` give
   only size, type, id and hash).

2. **Add many blobs atomically (required).** `resource_add_many(engine, infos, count, engine_buffer* failed_ids)`: all added or none, in the given order,
   with the same checks and flags as `resource_add`. `import_gltf` is atomic ("when one resource is refused none is added"); a cache load made of
   `count` separate `resource_add` calls is not, and a failure half way (budget `assets.memory_backend_bytes`, a stale blob, `ALREADY_EXISTS`) leaves a partial
   world in the backend that the host must undo with `resource_remove`. To check when writing it: whether `resource_add` requires dependencies to be present
   already (the load reports MISSING_DEPENDENCY, the add may not), so the order `import_result.ids` uses ("dependencies first") is enough.

3. **A format fingerprint for the cache key (required).** A value that changes whenever a re-import of the same `.glb` could give different blobs: the importer
   version and the blob container/type versions the importer writes. Something like `engine_api.import_format_version` (a `uint32_t`/string readable without an
   engine instance, so the host can name the cache directory before creating the engine). `abi_major`/`abi_minor` do not serve: they track the API, not the
   importer's output. Without it the host can only invalidate by the library's file hash or an `UNSUPPORTED_VERSION`/`CORRUPT` on add, which misses output that
   is valid but different (a fixed importer bug).

4. **Facts about the imported file that are not in the blobs (required unless they are already readable from the loaded level; to be checked).** The the host
   specs use these after an import: for a world, the node named `Spawn` (position) and the scene's bounding box on x/z; for an avatar, its height, whether a
   material named `Screen` exists, whether animations `Walk` and `Idle` exist, and the node named `Eye`. `engine_import_result` has none of them. If a loaded and
   spawned level gives them (`entity_name_get`, `query`, transforms, mesh slot names), nothing is missing and the host can read them after loading from the
   cache. If not, add a summary buffer to `engine_import_result` (UTF-8 text/JSON: node names with their transforms, the bounds, the mesh slot names, the animation
   names and durations) that the host stores beside the blobs. Check first; do not add both.

5. **An ids-and-paths list to store beside the blobs (nothing to add, a note for the docs).** `engine_import_result` already gives `ids`, `types`, `paths`, `warnings`
   and `manifest`; the cache stores them in its own index and passes each id and path back in `engine_resource_add_info`. State in `assets.md`/`engine-library.md`
   that this is the supported way, and which of the three the host must store to load the level again (the level id, `ENGINE_IMPORT_EMIT_LEVEL`).

6. **Blob size and count limits for reads (small).** Say in the doc comment of `resource_read` what the largest blob is (the importer's `max_output_bytes`) so the
   host can bound its disk cache per file.

7. **Add without the importer (small, for builds).** `ENGINE_LIBRARY_WITH_IMPORTER=OFF` builds must still support `resource_add`/`resource_read`/`resource_add_many`; the
   capability bit near `engine_c.h:202` covers only `import_gltf`. Add a separate bit if the resource functions are not always present. (`KNOWN-ISSUES.md` records that
   the `engine_c_import_shared*` tests fail in that configuration; a cache-only host would use it.)

8. **Optional: an asynchronous add.** `resource_add` is synchronous on the calling thread (the host's frame thread). A big cached world may take a noticeable time to
   check and copy; a ticket form like `import_gltf_async` (`resource_add_many_async`, `poll`/`finish`) would keep the frame loop running. Only if measuring shows the
   synchronous add is slow; the cache exists to make loading fast.

## Not missing (already enough)

- Presence check before adding: `resource_mounted(id)`. Removing a world's resources when leaving it: `resource_remove` per id (`ENGINE_RESOURCE_FORCE` for dependents).
- Integrity: `ENGINE_RESOURCE_CHECK_HASH` with `expected_hash`; the host also keeps its own SHA-256 of the `.glb`.
- Stable ids: `id_seed` (derive it from the `.glb` hash) or the returned `manifest`. Cached blobs carry their ids, so two different cached files cannot clash unless
  ids collide; two participants with the same avatar share one set (the host adds it once).

## Tests this needs

- Round trip: `import_gltf` a sample, `resource_read` every id, destroy the engine, create another, `resource_add_many` the blobs, `resource_load` the level and render:
  the picture equals the one from a direct import (golden, `engine_c_acceptance_scene`).
- Atomicity: a `resource_add_many` whose last blob is corrupt or over budget leaves the backend as it was (`resource_mounted` NOT_FOUND for all).
- Fingerprint: the value is stable across runs and builds of the same source, and changes when the importer's output format version is bumped.
- Pak or file resources: `resource_read` of one that is not in the memory backend gives NOT_FOUND.
- The shared library without the importer: add, read and load work.

## Result

Done 2026-11-18 (Linux), all eight items, one commit. Runs: plain `ran 3955 of 10622 tests (phases: all; flavors: plain; display: included; crash tests: excluded)` in 631 s, asan `ran 3250 of 10622`, tsan `ran 2905 of 10622` (taken before the gate fixes of the review; the plain run repeated after); the only failures are the three already in `KNOWN-ISSUES.md` (`engine_c_surface`, the job-destroyed test, the audio voice-cost test); `ctest -L docs` 9 of 9; GCC and Clang unity-off builds of `engine_c`, `asset_tests`, `host_import`, `host_cpp`, `host_import_off` clean, their tests passed.

1. **`resource_read(engine, id, max_size, out)`** (one parameter more than the plan: `max_size`, 0 = no limit, refuses `TOO_LARGE` before copying). Reads the blob from the memory backend into a buffer made by `MakeBlankBuffer` (one copy). A pak or file resource, an unknown id: `NOT_FOUND`. `out` is zeroed on failure.
2. **`resource_add_many(engine, infos, count, size_t* failed_index)`**, atomic in the backend: new `asset::HostMemoryBackend::AddMany` checks every blob, then every id and path (also among the items), then the sum against the budget, and only then installs, all under the change lock. `failed_index` instead of the plan's `engine_buffer* failed_ids`: with all-or-none exactly one item can be the reason (or `count` for the budget). The bytes are copied (`BORROW` is `INVALID_ARGUMENT`), because a rollback of a lent entry would call its release function, which the contract says a failed add never does. **Checked: `resource_add` does not look at dependencies** (neither does `AddMany`); `resource_load` reports `MISSING_DEPENDENCY`, so `import_result.ids` order is enough but not required.
3. **`import_format_version()`**: `uint64_t`, a function of the table (callable before any engine). FNV-1a over a layout number, `asset::kGltfImporterOutputVersion` (new, in `gltf_import.h`: raise it with every change of what the importer writes), `kContainerVersion` and every registered type's tag and version (`TypeRegistry::At` added). The golden is `EXPECTED_FORMAT_VERSION` in `host_import.c`.
4. **Import facts: not readable from a loaded level, so `engine_import_result.summary` was added** (and only that): the C API has entity names and transforms but no bounds of a level, no material by name and no clip length. The importer writes JSON (`asset_import/gltf_summary.{h,cpp}`, called from `ImportLevel`): level nodes with names (as `entity_find` sees them) and world positions, bounds of the meshes' `POSITION` ranges (accessor `min`/`max`, else computed), material names, animation names with durations. A host built against the first `engine_import_result` (no `summary`) is still served.
5. **Docs note**: `docs/guide/engine-library.md` ("Keeping what an import made") and `docs/guide/assets.md` say which of `ids`, `types`, `paths`, the blob, the level id and `summary` the host stores and how to load again.
6. **Blob limits** are in the doc comment of `resource_read` (the budget `assets.memory_backend_bytes`, `assets.import.max_output_bytes` for one import) and in the guide.
7. **`ENGINE_CAP_RESOURCE_CACHE`** (0x200), set in every build; `host_import_off.c` runs add, read, load and the async form in the importer-less library (with the sample blobs minted by `asset_mint`).
8. **Asynchronous add** (the plan said to do it only if measured slow; done on the user's request, 2026-11-18, without a measurement): `resource_add_many_async`, `resource_add_wait`, `resource_add_finish`, `resource_add_destroy` over a new handle kind. The engine reads the host's bytes in place until the ticket ends (no second copy); the arguments are checked before the call returns.

C++ layer: `Engine::ReadResource`, `AddResources`, `AddResourcesAsync`, `ImportFormatVersion`, class `AddTicket`. ABI: seven functions appended to `engine_api` (1072 to 1128 bytes) and `summary` to `engine_import_result` (176 to 208), snapshot regenerated; the version stays 1.0 (`status unreleased`).

Tests: `host_import.c` (round trip through two engines, atomicity: corrupt last blob, repeated id, live ids, budget; `resource_read` limits; fingerprint; summary; async; an old-size `engine_import_result`), `host_import_off.c`, `host_cpp.cpp`, `tests/asset/memory_backend_test.cpp` (`AddMany`), `tests/asset/gltf_mesh_test.cpp` (summary). Not done from the plan's test list: the golden-image round trip (`engine_c_acceptance_scene`, needs a GPU window; the blobs of two engines are compared byte for byte and the level spawns instead) and a fingerprint test that changes with a bump (the golden does that).

### Post-implementation code review

`/code-review high` on the diff: 4 findings. **Fixed:** (1) the summary re-read a mesh's `POSITION` accessor for every node that uses the mesh (a hostile file with many nodes on one mesh without `min`/`max` made that quadratic): the box is now computed once per mesh. (2) the summary test had only root nodes: a node under a translated parent is now checked. **Declined, recorded:** (3) the fingerprint does not fold in the component versions inside level blobs, so a level-component bump needs `kGltfImporterOutputVersion` raised by hand (`KNOWN-ISSUES.md`, item 4 of the cache entry); (4) the asynchronous add borrows the host's bytes until the ticket ends: documented in `engine_c.h` and the guide, the price of not copying twice.

