# Log everything / Day 3: tools (`tools/`)

**Series:** [log everything](../log-everything.md)
**Depends on:** Day 1.

## Tasks

- [x] The same rename in the 12 files of `tools/` (266 calls: `asset_mint` 109, `shader_mint` 41, `crash_extract` 36, `docgen` 31, `net_probe`, `script_api_doc`,
  `resource_server`, `audio_probe`).
- [x] A tool whose standard output is a product other programs read (`docgen` reports, `asset_mint` results, `crash_extract` tables) keeps every byte:
  it prints through `out::` and nothing else changes. Note for each tool whether its standard error carries diagnostics (they stay on fd 2).
- [x] The tools that run before a log core exists in a host's process (none today) are listed if any.
- [x] Every tool builds clean (Clang unity).

**Done:** no `os::Print*` call is left in `tools/`; the tree builds.

## Notes (2026-11-20, Linux)

- Same rename in `tools/` (nothing else changed: the tools keep every byte they print, and standard error stays standard error). All tools build in the Clang and GCC trees.
