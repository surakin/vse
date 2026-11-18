# Maintenance: C API the host needs to cache imported (minted) assets in its own disk cache

Not a numbered phase. Request from the the host project (2026-11-18), written against the engine at `ed6d8327`. Nothing here is started.

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

(Not started.)
