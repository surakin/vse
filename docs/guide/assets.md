# Assets and resources: identity, blobs, backends and the resource manager

Status: Phase 22. Built: animation clips, their source form and minter (Day 320); the module skeleton and the container version constant (Day 313); resource ids, paths, types, the content hash, the blob container and the id <-> path table (Day 314). The backend interface, the filesystem and host-memory backends, the load buffer pool and mounts (Day 315). The pak format, its writer, reader and backend, and `asset_mint pack` (Day 316).

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

## Skeletons

A skeleton (`asset::types::kSkeleton`, `SKEL`) is a resource of its own: the joint hierarchy, the bind pose and the inverse bind matrices that skinned meshes and animation clips refer to by id. At most 256 joints (the palette limit of the GPU skinning day), names at most 64 bytes, parents strictly before their children. The blob is a set of sections in structure-of-arrays form, each 64-byte aligned: the header (joint count, flags, the 128-bit skeleton hash, a bound of the bind pose), parents (`u16`, `0xFFFF` for a root), translations (`f32 x3`), rotations (unit quaternions `f32 x4`, w >= 0), scales (`f32 x3`), the inverse bind matrices (3x4 row-major `f32`, 48 bytes each: the exact bytes of the joint palette upload) and, optionally, the joint name hashes and names.

The skeleton hash covers the joint count, the parents and the names, not the pose: a mesh or a clip records it, and a clip made for another skeleton is refused when it is bound. Retargeting between different skeletons is not supported.

`asset::SkeletonView::Open` checks the whole blob once (current version only, every count, parent order, unit quaternions, finite values, name offsets and hashes) and then reads spans into the buffer, with no copy and no conversion. `FindJoint` goes by name hash, then name.

The text source form is `.skel.src`, minted by `asset_mint mint-skeleton SOURCE OUTPUT [--target=N]`:

```
id 00000000000000aa
joint root  -     0 1.0 0   0 0 0 1   1 1 1
joint spine root  0 0.2 0   0 0 0 1   1 1 1
ibm spine 1 0 0 0  0 1 0 -1.2  0 0 1 0
```

`joint NAME PARENT tx ty tz qx qy qz qw sx sy sz` (the parent is a name defined earlier, or `-`); the minter normalizes quaternions into the w >= 0 hemisphere and computes each inverse bind matrix in double precision from the bind pose unless an `ibm NAME` line (12 numbers, row-major 3x4) gives it. Errors name the line. The output is deterministic.

```cpp compile run standalone
#include "asset/skeleton.h"
#include "cstr/format.h"
#include "os/console.h"

using namespace engine_placeholder;

int main() {
    asset::TypeRegistry types;
    asset::SkeletonJoint joints[2];
    FormatTo(joints[0].name.Data(), joints[0].name.Size(), "root");
    FormatTo(joints[1].name.Data(), joints[1].name.Size(), "tip");
    joints[1].parent = 0;
    joints[1].translation = {{0.0f, 2.0f, 0.0f}};
    asset::ComputeInverseBindMatrices(Span<asset::SkeletonJoint>(joints, 2));

    Vector<byte> blob;
    if (asset::BuildSkeletonBlob(types, asset::ResourceId::FromValue(7), Span<const asset::SkeletonJoint>(joints, 2), true,
                                 &blob).Failed()) return 1;
    asset::SkeletonView view;
    if (view.Open(ByteSpan(blob.Data(), blob.Size()), types).Failed()) return 1;
    u32 tip = 0;
    if (!view.FindJoint("tip", &tip)) return 1;
    os::Print("%u joints, tip is %u, parent %u, inverse bind y %.1f\n", view.JointCount(), tip,
              static_cast<unsigned>(view.Parent(tip)), static_cast<double>(view.InverseBind(tip)[7]));
    return 0;
}
```
```output
2 joints, tip is 1, parent 0, inverse bind y -2.0
```

## Meshes

A mesh (`asset::types::kMesh`, `MESH`) is laid out for the GPU: the vertex section is the exact bytes of vertex buffer binding 0 and the index section the exact bytes of the index buffer (16-bit up to 65,536 vertices, otherwise 32-bit, always a triangle list), each 64-byte aligned, so a loader hands a span of the blob to the upload and nothing is converted. Sections: the header (counts, bounds, the texture-coordinate decode, the measured packing error, the skeleton hash of a skinned mesh), the attribute list, the vertex stream, the indices, the LODs (a range of indices and a screen size, non-increasing) and the material slots (a name and a range of indices). `asset::MeshView::Open` checks the sizes, counts and ranges once and keeps spans into the buffer; it does not scan the indices (`CheckIndices` does, for tools).

The default layout (`quality packed`, 24 bytes) is position `Float3`, normal and tangent `SNorm8x4` (the bitangent sign is the tangent's w) and texture coordinates `UNorm16x2` decoded as `offset + scale * value` with a per-mesh offset and scale from the header; the minter records the largest error it measured on the texture coordinates and on the normals and tangents. `quality full` (48 bytes) keeps `Float3`, `Float3`, `Float4`, `Float2`.

A skinned mesh adds binding 1: joint indices `UInt8x4` (location 5) and weights `UNorm8x4` (location 6). The minter keeps the four largest influences of a vertex (ties by joint index), renormalizes in double precision and rounds with the largest-remainder rule so the four weights sum to exactly 255 (a shader needs no renormalization), strongest first. The skeleton is a dependency with the role `kMeshRoleSkinSkeleton`, and the blob records its hash and joint count: `MeshView::CheckSkeleton` refuses a skeleton with another id, hash or joint count (`DependencyTypeMismatch`). The bind-pose box of each joint's influence set is stored too. Morph targets are not supported (section 40 is reserved and refused).

The source form `.mesh.src` (`asset_mint mint-mesh SOURCE OUTPUT [--skeleton=SKELETON.blob] [--target=N]`):

```
id 00000000000000bb
skeleton 00000000000000aa
v 0 0 0   0 0 1   1 0 0 1   0 0
v 1 0 0   0 0 1   1 0 0 1   1 0
v 0 1 0   0 0 1   1 0 0 1   0 1
i 0 1 2
slot body 0 3
skin 0 0 0.5 1 0.5
skin 1 1 1
skin 2 0 1
```

`v` is position, normal, tangent xyz and the bitangent sign (+1 or -1), uv. All `v` records come before `i` and `skin`. Errors name the line; a vertex of a skinned mesh with no weight above 0 is reported at its `v` line.

```cpp compile run standalone
#include "asset/mesh.h"
#include "os/console.h"

using namespace engine_placeholder;

int main() {
    asset::TypeRegistry types;
    asset::MeshVertex vertices[3];
    vertices[1].position = {{1.0f, 0.0f, 0.0f}};
    vertices[1].uv = {{1.0f, 0.0f}};
    vertices[2].position = {{0.0f, 1.0f, 0.0f}};
    vertices[2].uv = {{0.0f, 1.0f}};
    const u32 indices[3] = {0, 1, 2};
    asset::MeshBuildInput input;
    input.vertices = Span<const asset::MeshVertex>(vertices, 3);
    input.indices = Span<const u32>(indices, 3);

    Vector<byte> blob;
    if (asset::BuildMeshBlob(types, asset::ResourceId::FromValue(9), input, &blob).Failed()) return 1;
    asset::MeshView view;
    if (view.Open(ByteSpan(blob.Data(), blob.Size()), types).Failed()) return 1;
    os::Print("%u vertices of %u bytes, %u indices of %u bytes, %u attributes\n", view.VertexCount(), view.VertexStride(),
              view.IndexCount(), view.IndexBytesPerIndex(), view.AttributeCount());
    return 0;
}
```
```output
3 vertices of 24 bytes, 3 indices of 2 bytes, 4 attributes
```

## Animation clips

A clip (`asset::types::kAnimation`, `ANIM`) is the keyed motion of some of a skeleton's joints. It depends on its skeleton (the role `kClipRoleSkeleton`) and records the skeleton's id, hash and joint count: `asset::ClipView::Bind` refuses a skeleton that differs in any of them, so a clip is never silently applied to another rig. Retargeting is not supported.

The minter shrinks the clip offline and the loader does no conversion. Per joint there is a track with three channels (translation, rotation, scale). A channel whose keys are all equal is *constant*: its value sits in the track table and it has no keys. A keyed channel holds at least 2 keys, as `u16` times (ticks of 1 / sample rate when every key lies on that grid, so keys sampled at the authoring rate are exact; otherwise the duration mapped to 0..65535, a step that a long clip of fast motion may not be able to afford: the minter's measurement then refuses it) and 6-byte values: rotations as *smallest three* (the largest component is dropped, its index takes 2 bits, the other three are stored as 14/16/16-bit signed fixed point in +/-1/sqrt(2)), translations and scales as `UNorm16 x3` between a per-channel minimum and extent. Keys are reduced by a deterministic greedy split (the dropped key with the largest error is kept, the lowest index on a tie) until every dropped key is within 60% of the rotation bound or 50% of the translation bound (a fraction of the channel's range); the rest of the budget is left to quantization.

The bounds are `ClipLimits` (settings `assets.anim_rotation_error_degrees`, default 0.05, and `assets.anim_translation_error`, default 1e-4 of the channel range, with a floor of 1e-3 units). The minter measures the real error of the finished blob against the source keys, at every source key and at 8 points inside each segment, writes the measured values to the header (`ClipView::RotationError`, `TranslationError`) and refuses the clip with `LimitExceeded` when a bound is missed.

`ClipView::Open` checks the sizes, counts, joint order, key times, offsets and every constant value once; `Sample(track, time)` then reads the keys in place (time clamped to the duration; rotations interpolate with a normalized lerp on the shorter arc). The events are a time, a name hash (`JointNameHash`) and a payload, in time order.

The source form `.anim.src` (`asset_mint mint-clip SOURCE OUTPUT --skeleton=SKELETON.blob [--target=N]`):

```
id 00000000000000cc
skeleton 00000000000000bb
duration 2
rate 30
flags loop
bounds -1 -1 -1 1 3 1
track arm
r 0 0 0 0 1
r 2 0 0 0.3826834 0.9238795
track root
t 0 0 0 0
t 2 1 0 0
event 0.5 footstep 7
```

`track` names a joint; `t`, `r` and `s` add translation, rotation (a unit quaternion x y z w) and scale keys to the current track, times strictly increasing within [0, duration]. Errors name the line.

```cpp compile run standalone
#include "asset/clip.h"
#include "asset/skeleton.h"
#include "os/console.h"

using namespace engine_placeholder;

int main() {
    asset::TypeRegistry types;
    asset::SkeletonJoint joint;
    MemSet(&joint, 0, sizeof(joint));
    joint.name[0] = 'a';
    joint.parent = asset::kNoParent;
    joint.rotation = {{0, 0, 0, 1}};
    joint.scale = {{1, 1, 1}};
    asset::ComputeInverseBindMatrices(Span<asset::SkeletonJoint>(&joint, 1));
    Vector<byte> skeleton_blob;
    if (asset::BuildSkeletonBlob(types, asset::ResourceId::FromValue(5), Span<const asset::SkeletonJoint>(&joint, 1),
                                 true, &skeleton_blob).Failed()) return 1;
    asset::SkeletonView skeleton;
    if (skeleton.Open(ByteSpan(skeleton_blob.Data(), skeleton_blob.Size()), types).Failed()) return 1;

    asset::ClipTrackSource track;
    track.joint = 0;
    asset::ClipKey3 k0;
    k0.time = 0;
    asset::ClipKey3 k1;
    k1.time = 1;
    k1.value = {{1, 0, 0}};
    track.translation.Push(k0);
    track.translation.Push(k1);
    asset::ClipBuildInput input;
    input.duration = 1;
    input.skeleton = skeleton.Id();
    input.skeleton_hash = skeleton.Hash();
    input.joint_count = skeleton.JointCount();
    input.tracks = Span<const asset::ClipTrackSource>(&track, 1);
    Vector<byte> blob;
    if (asset::BuildClipBlob(types, asset::ResourceId::FromValue(6), input, &blob).Failed()) return 1;
    asset::ClipView view;
    if (view.Open(ByteSpan(blob.Data(), blob.Size()), types).Failed()) return 1;
    if (view.Bind(skeleton).Failed()) return 1;
    const asset::ClipSample half = view.Sample(0, 0.5f);
    os::Print("x at 0.5 s: %.3f\n", half.translation[0]);
    return half.translation[0] > 0.49f && half.translation[0] < 0.51f ? 0 : 1;
}
```
