# Assets and resources: identity, blobs, backends and the resource manager

Status: Phase 22. Built: the module skeleton and the container version constant (Day 313); resource ids, paths, types, the content hash, the blob container and the id <-> path table (Day 314). The backend interface, the filesystem and host-memory backends, the load buffer pool and mounts (Day 315). The pak format, its writer, reader and backend, and `asset_mint pack` (Day 316).

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

## Backends, mounts and host content

A source of resources implements `asset::ResourceBackend`: `Stat` and `Open` by id (an `asset::ResourceReader` reads any range, synchronously or with `ReadAtAsync`), `ReadTable` / `ReadTableAsync` for the backend's own id <-> path table, `List` as a bounded view over it, and `Watch` for the changes since the last call. The table is complete or an error, never silently partial. `ReadTableAsync` returns an `asset::TableTicket` at once: `Finish` waits for the whole table, `TakePage` consumes what has arrived (every page holds complete entries), `Cancel` leaves no table, and a failure surfaces from `Finish` with the backend's status. A backend that does not read natively gets a default that runs `ReadTable` on the I/O executor.

- `asset::FilesystemBackend` serves a directory of loose blobs. It builds its index once: from `resources.table` (a minted `NAMES` blob) when the directory has one, otherwise by walking the directory and reading each file's 64-byte header (the path is the file's location, the id the blob's own; never an id from a file name). A stored table is a cache that is never trusted over the files: `Stat` and `Open` check the file's own id, and a mismatch is `Corrupt`. Two files that claim one id fail the index with `DuplicateId`. `Watch` rescans and reports added, replaced and removed ids.
- `asset::HostMemoryBackend` holds resources whose bytes the host application provides at run time: copied into the backend, or borrowed with a release function that runs exactly once after the resource is replaced or removed and its last reader closed. `Add`, `Replace` and `Remove` work from any thread; an open reader keeps the bytes it opened, so a replace never tears a load. `Add` checks the blob (its own id, its content hash, and a hash the host supplies) and the byte budget (`assets.memory_backend_bytes`; past it `Add` returns `LimitExceeded`: the host owns the content, so there is back-pressure and never eviction). A resource that a loaded one depends on cannot be removed or replaced unless the host forces it (`InUse` names the dependents).
- `asset::FaultyBackend` wraps any backend with injected failures, chunked reads and latency, for tests.

`asset::MountTable` orders backends by priority (the later of two equal priorities wins), so a loose-file mount overrides a pak and host content overrides both. A load by id goes to the highest-priority backend that has the id; a failure of that backend other than `NotFound` is returned, never replaced by a lower copy. Path questions are answered from the mounts' tables: `RefreshTablesAsync` starts every mount's table read without blocking and merges each table as it arrives, `Resolve` answers `NotReady` (never `NotFound`) for a path whose mount has not delivered its table, and loads by id never use a table. `Snapshot` returns the merged table and the entries one mount shadowed in another.

`asset::LoadBufferPool` hands out the buffers a resource is read into: 64-byte aligned (page aligned from 1 MiB), pages touched when the buffer is created (a read into a pre-faulted buffer was four times faster in the Day 313 spike), returned on release, and bounded by `assets.buffer_pool_bytes`.

```cpp compile run standalone
#include "asset/blob.h"
#include "asset/memory_backend.h"
#include "asset/mount_table.h"
#include "os/console.h"

using namespace engine_placeholder;

int main() {
    asset::TypeRegistry types;
    const asset::ResourceId id = asset::ResourceId::FromValue(0x42);

    asset::BlobWriter writer(types, asset::types::kLevel, id);
    const char payload[] = "hello";
    if (writer.AddSection(16, ByteSpan(reinterpret_cast<const byte*>(payload), sizeof(payload))).Failed()) return 1;
    Vector<byte> blob;
    if (writer.Finish(&blob).Failed()) return 1;

    // The host provides the bytes; the backend copies them and reports the path.
    asset::HostMemoryBackend host(types, 1 << 20);
    const StringView path = "/levels/start.level";
    asset::MemoryAddOptions options;
    options.paths = Span<const StringView>(&path, 1);
    if (host.Add(id, ByteSpan(blob.Data(), blob.Size()), options).Failed()) return 1;

    asset::MountTable mounts;
    asset::MountId mount = 0;
    if (mounts.Mount("/", &host, 100, &mount).Failed()) return 1;
    if (mounts.RefreshTables().Failed()) return 1;

    asset::ResourceId found;
    if (mounts.Resolve("/levels/start.level", &found).Failed()) return 1;
    UniquePtr<asset::ResourceReader> reader;
    if (mounts.Open(found, &reader).Failed()) return 1;
    os::Print("%u bytes, id matches: %d\n", static_cast<unsigned>(reader->Size()), found == id ? 1 : 0);
    return 0;
}
```
```output
256 bytes, id matches: 1
```

## The pak

A pak holds many blobs in one file and finds them by id. The layout (`engine/asset/pak.h`): a 128-byte header (magic, format version, target tag, counts, the base pak's identity for a patch pak), a type table, the optional names section (the id <-> path table as a `NAMES` blob), the blobs in id order each 64-byte aligned (4096 from 1 MiB), a sorted 48-byte-per-entry index and a footer. Offsets are 64-bit. The pak's identity is its index hash.

`asset::PakWriter` is an enforcement point. It refuses a blob that is not the current version of its type, whose own id differs from the id it is added under, whose content hash does not verify or that was laid out for another target, and `Finish` refuses a pak whose closure is incomplete: every dependency must be an entry of the pak, or of the base pak named with `SetBase` (`Problems()` lists each missing or wrongly typed pair). The output is deterministic: the same inputs give the same bytes.

`asset::PakReader::Open` validates the header, the footer and the whole index against the file size and builds a hash table over the ids; it does not hash anything (`Verify` checks the index hash and every blob, a tool's job). It refuses a pak of another format version or a resource type version that is not current (`UnsupportedVersion`, naming `asset_mint remint`) and a pak packed for another target (`WrongTarget`; `Detail()` names both tags). `asset::PakBackend` serves a pak through the backend interface; a patch pak answers `MissingDependency` until `LinkBase` names the backend of the pak it was packed against, whose identity must match.

`asset_mint pack DIRECTORY OUTPUT.pak [--target=N] [--no-names]` packs a directory of loose blobs.

```cpp compile run standalone
#include "asset/blob.h"
#include "asset/pak.h"
#include "asset/pak_backend.h"
#include "io/file_system.h"
#include "os/console.h"

using namespace engine_placeholder;

int main() {
    asset::TypeRegistry types;
    const asset::ResourceId id = asset::ResourceId::FromValue(0x42);
    asset::BlobWriter blob_writer(types, asset::types::kLevel, id);
    const char payload[] = "hello";
    if (blob_writer.AddSection(16, ByteSpan(reinterpret_cast<const byte*>(payload), sizeof(payload))).Failed()) return 1;
    Vector<byte> blob;
    if (blob_writer.Finish(&blob).Failed()) return 1;

    io::Path tmp, dir;
    if (io::TempDirectory(&tmp).Failed() || io::CreateTempDirectory(tmp, "re_pak_sample", &dir).Failed()) return 1;
    const io::Path file_path = dir.Join("sample.pak");

    {
        io::FilePtr file;
        if (io::File::Open(file_path, io::OpenMode::Write, 0, &file).Failed()) return 1;
        asset::PakWriter writer(types, asset::PakWriterOptions());
        if (writer.Add(id, ByteSpan(blob.Data(), blob.Size()), "/levels/start.level").Failed()) return 1;
        if (writer.Finish(*file).Failed()) return 1;
        if (file->Close().Failed()) return 1;
    }

    UniquePtr<asset::PakBackend> pak;
    if (asset::PakBackend::Open(file_path, types, 0, &pak).Failed()) return 1;
    UniquePtr<asset::ResourceReader> reader;
    if (pak->Open(id, &reader).Failed()) return 1;
    os::Print("%u entries, %u bytes\n", static_cast<unsigned>(pak->Info().entry_count), static_cast<unsigned>(reader->Size()));
    io::RemoveAll(dir);
    return 0;
}
```
```output
1 entries, 256 bytes
```
