# Vendored yyjson

Upstream: https://github.com/ibireme/yyjson
Pinned release: **0.13.0** (tag `0.13.0`, commit `6447536015f3d600f3d65323b10976103b337ca7`), cloned 2026-11-14.
License: `LICENSE` (MIT), unmodified (sha256 `45e384d3d52c73cba3a64d6e6c25d47cd738cd8a55c30629e3201046eda62947`).
`yyjson.c` (sha256 `d2d58ef0a3b2267862dc363832c4f3185fd375aea933d5d3419d1a940d6ece2e`) and `yyjson.h` (sha256
`ef803cda5c06b8962face6dfa39c3284b3bcf3e73f4d7317664690cc779679ca`): the upstream `src/` files, unmodified.

Per `PLAN.md` assumption #19 (amended 2026-11-14) this reads the JSON of glTF files for `engine/asset_import`, whose input may be hostile and
untrusted (the host fetches files from rooms): it was chosen by a spike on Phase 22 Day 335 (the record is in
`docs/superpowers/specs/2026-11-13-asset-pipeline-design.md`, "The JSON library"). Source vendor, not a submodule or `FetchContent`, so builds stay
offline and reproducible (same pattern as SQLite, assumption #25). Only `engine/asset_import/json_view.cpp` includes `yyjson.h`; the gate
`json_boundary_check` lets no other file do it.

## How it is built (`CMakeLists.txt`)

One static C library per flavor (`yyjson`, `yyjson_asan`, `yyjson_tsan`), so the sanitizers look inside the parser. Compile definitions (public: the
header's `#if`s must agree):

| Definition | Why |
|---|---|
| `YYJSON_DISABLE_WRITER=1` | The importer only reads. |
| `YYJSON_DISABLE_INCR_READER=1` | No incremental reader. |
| `YYJSON_DISABLE_FILE=1` | **No `fopen` in the library**: all file access goes through `io::` (`file_access_check`); the importer reads from memory. |
| `YYJSON_DISABLE_UTILS=1` | No JSON pointer, patch or merge. |
| `YYJSON_DISABLE_NON_STANDARD=1` | Strict RFC 8259 only: no comments, trailing commas, `NaN` or `Infinity`, single quotes, BOM (the reader flags that allow them are not compiled). |
| `YYJSON_READER_DEPTH_LIMIT=128` | The reader refuses nesting deeper than 128 (the default is no limit). A glTF is a few levels deep. |

The reader is called with a pool allocator over a buffer of `yyjson_read_max_memory_usage(length)` bytes (`engine/asset_import/json_view.cpp`): the memory a parse can use is a
function of the input's length, whatever the input says; it never calls `malloc`.

To update the pin: replace `yyjson.c`, `yyjson.h` and `LICENSE` from the new tag, update the version, commit and checksums above, and run the
hostile-input tests of `tests/asset/gltf_json_test.cpp` under asan.
