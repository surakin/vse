# Assets and resources: identity, blobs, backends and the resource manager

Status: Phase 22. Built: the module skeleton and the container version constant (Day 313); resource ids, paths, types, the content hash, the blob container and the id <-> path table (Day 314). Next: the backend interface, the filesystem and memory backends and mounts (Day 315).

**Audience:** engine and game programmers who load meshes, textures, materials and levels. Design, spikes and the benchmark pass conditions:
`docs/superpowers/specs/2026-11-13-asset-pipeline-design.md`.

The module `engine/asset` (namespace `asset`) loads minted assets. Three rules shape it (the user, 2026-11-13):

- **Loading performance is paramount.** A load is a read into a pre-faulted, aligned buffer and a bounded validation; nothing is copied between the read and the GPU upload.
- **Load-time conversions are discouraged.** The loaders never repack, re-order or byte-swap; the mint writes what the GPU and the CPU want.
- **Upgrades are enforced offline.** The runtime reads the current version only (`asset::kContainerVersion`) and refuses an older asset with a message naming
  `asset_mint remint`. The upgrade chain lives in `tools/asset_mint`.

Minting is deterministic and the offline tools are called mints (`tools/asset_mint`); "cook" is Unreal's word.

## Identity: ids, paths and the resource table

A resource is named by a `asset::ResourceId`, a permanent 64-bit number (`ResourceId::None()` is the null reference). The id is **not** derived from the path: `asset_mint adopt` draws it once from the operating system's random source and writes it into the source file itself (one file per asset, no sidecar), and every minted blob carries its own id in its header. A rename or a move changes the path, never the id. The runtime refers to resources by id only.

A path is an alias. `asset::NormalizePath` gives its one canonical spelling: UTF-8 in Unicode NFC, a leading `/`, single `/` separators, no `.` or `..` segment, no trailing slash, no backslash, no control characters, at most 1024 bytes (255 per segment), case-sensitive. Anything else is an error, never repaired.

`asset::ResourceTable` is the one id <-> path map. One path names one id; one id may have many paths and one type. Each backend supplies its own table (a stored `NAMES` blob, a pak section, a server reply, or one built by scanning a folder), and the mount table merges them by priority: a path that two tables map to different ids keeps the higher-priority id, and the other is reported as shadowed, not dropped.

```cpp compile run standalone
#include "asset/resource_table.h"
#include "os/console.h"

using namespace engine_placeholder;

int main() {
    asset::ResourceTable table;
    const asset::ResourceId rock = asset::ResourceId::FromValue(0x1234);
    if (table.Add("/textures/rock.tex", rock, asset::types::kTexture).Failed()) return 1;
    if (table.Add("/textures/stone.tex", rock, asset::types::kTexture).Failed()) return 1;  // a second path

    asset::ResourceId found;
    if (!table.Resolve("/textures/stone.tex", &found)) return 1;
    Vector<StringView> paths;
    table.PathsOf(found, &paths);
    char hex[17];
    asset::FormatResourceId(found, hex);
    os::Print("%s has %u paths\n", hex, static_cast<unsigned>(paths.Size()));
    return 0;
}
```
```output
0000000000001234 has 2 paths
```

## The blob container

Every minted resource is a blob: a fixed 64-byte header (magic, container version, type, type version, the blob's own id, the target tag, the size and a 128-bit content hash), a section table, then sections that each start on a 64-byte boundary (a 4096-byte page boundary from 1 MiB), so the loader hands a span of the file buffer straight to the GPU upload. Section 1 is the standard dependency section: `(id, type, type version, role)` records sorted by id, with no optional dependencies. `asset::BlobWriter` builds it (deterministically: the same input gives the same bytes); `asset::BlobReader::Open` validates the header and every offset and size before anything is used, and reads the **current version of the type only**: any other version is refused with `UnsupportedVersion` and a message naming `asset_mint remint`. The content hash (SHA-256 truncated to 128 bits) is checked only by `BlobReader::Verify`, never on the load path.
