# Assets and resources: identity, blobs, backends and the resource manager

Status: written.

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
#include "log/output.h"

using namespace vse;

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
    out::Print("%s has %u paths\n", hex, static_cast<unsigned>(paths.Size()));
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
- `asset::HostMemoryBackend` holds resources whose bytes the host application provides at run time: copied into the backend. `Add`, `Replace` and `Remove` work from any thread; an open reader keeps the bytes it opened, so a replace never tears a load. `Add` checks the blob (its own id, its content hash, and a hash the host supplies) and the byte budget (`assets.memory_backend_bytes`; past it `Add` returns `LimitExceeded`: the host owns the content, so there is back-pressure and never eviction). A resource that a loaded one depends on cannot be removed or replaced unless the host forces it (`InUse` names the dependents).
- `asset::FaultyBackend` wraps any backend with injected failures, chunked reads and latency, for tests.

`asset::MountTable` orders backends by priority (the later of two equal priorities wins), so a loose-file mount overrides a pak and host content overrides both. A load by id goes to the highest-priority backend that has the id; a failure of that backend other than `NotFound` is returned, never replaced by a lower copy. Path questions are answered from the mounts' tables: `RefreshTablesAsync` starts every mount's table read without blocking and merges each table as it arrives, `Resolve` answers `NotReady` (never `NotFound`) for a path whose mount has not delivered its table, and loads by id never use a table. `Snapshot` returns the merged table and the entries one mount shadowed in another.

`asset::LoadBufferPool` hands out the buffers a resource is read into: 64-byte aligned (page aligned from 1 MiB), pages touched when the buffer is created (a read into a pre-faulted buffer was four times faster in a spike), returned on release, and bounded by `assets.buffer_pool_bytes`.

```cpp compile run standalone
#include "asset/blob.h"
#include "asset/memory_backend.h"
#include "asset/mount_table.h"
#include "log/output.h"

using namespace vse;

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
    out::Print("%u bytes, id matches: %d\n", static_cast<unsigned>(reader->Size()), found == id ? 1 : 0);
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
#include "log/output.h"

using namespace vse;

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
    out::Print("%u entries, %u bytes\n", static_cast<unsigned>(pak->Info().entry_count), static_cast<unsigned>(reader->Size()));
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
#include "log/output.h"

using namespace vse;

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
    out::Print("%u joints, tip is %u, parent %u, inverse bind y %.1f\n", view.JointCount(), tip,
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

A coloured mesh has one more attribute of stream 0, the per-vertex colour, `UNorm8x4` at location 4 after the others (offset 24 packed, 48 full; the stride grows by 4): rgb sRGB-encoded, alpha linear, as `gbuffer.vert`'s `VERTEX_COLOR` reads them; `asset::EncodeMeshColor` turns a linear colour into those bytes, and a `.mesh.src` `v` line takes four more numbers, the linear colour (every `v` of a mesh has one or none). The version-1 reader already took any attribute, so the format version did not change. Morph targets are sections 40 and 41: up to `kMaxMorphTargets` (64) named targets, each a run of deltas (the vertex, then position, normal and tangent deltas as `f32x3`, sorted by vertex, no repeats); `MeshBuildInput::morph_targets` takes them (all-zero deltas are dropped), `MeshView::MorphTarget` and `MorphDelta` read them in place, and every delta is checked on open, since the GPU indexes vertices with them. The box grows by each target's largest position delta on both sides, so it holds the mesh at any weights from -1 to 1. A version-1 reader refused section 40, so the version did not change. In `.mesh.src`, `morph NAME` starts a target and `d VERTEX dx dy dz [nx ny nz [tx ty tz]]` lines are its deltas. A skinned mesh adds binding 1: joint indices `UInt8x4` (location 5) and weights `UNorm8x4` (location 6). The minter keeps the four largest influences of a vertex (ties by joint index), renormalizes in double precision and rounds with the largest-remainder rule so the four weights sum to exactly 255 (a shader needs no renormalization), strongest first. The skeleton is a dependency with the role `kMeshRoleSkinSkeleton`, and the blob records its hash and joint count: `MeshView::CheckSkeleton` refuses a skeleton with another id, hash or joint count (`DependencyTypeMismatch`). The bind-pose box of each joint's influence set is stored too. Morph targets are not supported (section 40 is reserved and refused).

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
#include "log/output.h"

using namespace vse;

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
    out::Print("%u vertices of %u bytes, %u indices of %u bytes, %u attributes\n", view.VertexCount(), view.VertexStride(),
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
#include "log/output.h"

using namespace vse;

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
    out::Print("x at 0.5 s: %.3f\n", half.translation[0]);
    return half.translation[0] > 0.49f && half.translation[0] < 0.51f ? 0 : 1;
}
```

## Textures

A texture (`asset::types::kTexture`, `TEXR`) stores **every mip level of every layer**: there is no "generate at load" flag, because the mip filter, the sRGB handling and the premultiplication all happen in the minter. The kinds are a 2D image, a cube map (six square layers, +X -X +Y -Y +Z -Z) and an array of 1 to 2048 layers. The formats of this day are `R8_UNorm`, `RG8_UNorm`, `RGBA8_UNorm`, `RGBA8_SRGB`, `R16_Float` and `RGBA16_Float`; `BC1_UNorm`, `BC1_SRGB`, `BC4_UNorm`, `BC5_UNorm`, `BC7_UNorm` and `BC7_SRGB` join them behind the same layout : a compressed level stores block rows, `ceil(h / 4)` of them, each `ceil(w / 4)` blocks of 8 or 16 bytes, rounded up to the target's row alignment.

The blob has a header, a table of subresources (`layer * mip_count + mip`: offset, size, row pitch) and one data section. Each subresource starts at the target's subresource alignment and its rows are `row_pitch` bytes apart, so `TextureView::Level(layer, mip)` gives spans that go to `Device::UploadTexture` unchanged (`row_pitch` is its argument). The alignments are the target API's: `TextureLayoutForTarget` gives rows of 4 and subresources of 16 for the development target 0 (Vulkan), 256 and 512 for Direct3D 12 (target 1), 16 and 16 for Metal (target 2). `asset_mint pack --target` re-lays a blob out with `RetargetTextureBlob`; minting for a target (`mint-texture ... --target=N`) gives the same bytes.

`renderer::GpuTexture::CreateFromSharedLevels(device, view.Desc(usage), bytes, levels, &registry, name)` is the zero-copy entry: each `TextureLevelSpan` names a level by offset and size in the blob and the staging copy into device memory is the only copy. The texture shares the blob and keeps the offsets, so a device reset uploads from the same bytes again whoever else lets go of them. A cube is created as a 6-layer array (the cube view is the material system's).

**Mip generation** (`asset/texture_mips.h`) uses exact integer arithmetic only, so the bytes are the same on every compiler, machine and thread count. Destination texel `i` of an axis covers the source range `[i * s / d, (i + 1) * s / d)`; the box filter averages it, the Kaiser filter is the fixed 6-tap kernel `-5 24 109 109 24 -5` over the texels `lo - 2 .. lo + 3` (clamped at the edges, the result clamped to the value range). sRGB data is filtered in linear light through a fixed 16-bit table. Alpha is `straight` (the colors are weighted by alpha while filtering, so a transparent texel's color does not bleed in; stored straight), `premultiply` (the minter multiplies level 0 by alpha, in linear light for sRGB, and all levels stay premultiplied) or `ignore`. Half-float texels are exact multiples of 2^-24 and are filtered as integers; `straight` is refused for them. A cube's faces are filtered independently (no filtering across the seams).

The source form `.tex.src` (`asset_mint mint-texture SOURCE OUTPUT [--target=N]`; the images are raw files next to it):

```
id 00000000000000dd
kind cube              # 2d (default), cube or array
source px.raw          # one per layer: 1 for 2d, 6 for a cube
format rgba8           # r8 rg8 rgba8 r16f rgba16f bc1 bc4 bc5 bc7 bc7-alpha
srgb true              # rgba8: store sRGB; half floats: the source is sRGB
mips full              # or a count
filter box             # or kaiser
alpha straight         # or premultiply, ignore
input rgba8            # or rgba16f: the raw files hold half-float RGBA (needs format rgba16f)
projection faces       # or equirect (a cube from one latitude-longitude image; needs input rgba16f)
face_size 512          # with projection equirect only: texels across a face, 1 to 4096
environment yes        # cube, input rgba16f only: mips from 1 are the GGX specular chain (level i: roughness i / (mips - 1))
```

A raw image is `RAW1`, width, height, an id field and a reserved `u32` (24 bytes, little-endian), then `width * height * 4` bytes of RGBA8: no PNG. With the magic `RAWH` the pixels are `width * height * 8` bytes of half-float RGBA (`input rgba16f`): a high-dynamic-range image, kept as it is (values above 1 stay above 1) and filtered like the 8-bit ones. Errors name the line; a missing `id` line names `asset_mint adopt`.

**Cube maps.** A cube is six square faces in the order +X -X +Y -Y +Z -Z; `TextureView::Desc` makes a `Cube` texture of it, which a shader reads with a direction. The face directions are those of Vulkan, Direct3D 12 and Metal (`asset::CubeFaceDirection`). `projection equirect` makes the faces from one latitude-longitude image (`asset::EquirectangularToCubeFaces`): its center column looks along -Z, its columns run toward +X, its top row is straight up; the conversion is bilinear and uses only `+ - * /`, the square root and a fixed polynomial for the angles, so the faces are the same bytes on every machine. `environment yes` (a half-float cube) replaces the levels from 1 with a specular chain for reflections: each level is the box-filtered level convolved with a GGX lobe of roughness `i / (mips - 1)` by 32 fixed importance samples (`asset::PrefilterEnvironmentChains`, deterministic, only `+ - * /` and the square root with polynomial sine and cosine), so a shader reads a rougher reflection from a higher level; level 0 stays the sharp image the sky draws. A texture that is missing from the project loads the 2D fallback, not a cube: code that needs a cube checks the loaded texture's dimension.

### Block compression

`asset/block_compress.h` holds the decoders (`DecodeBlock`, `DecodeImage` for BC1, BC2, BC3, BC4, BC5 and BC7) and the encoders (`EncodeBlock`, `EncodeImage` for BC1, BC4, BC5 and BC7). A block is 4x4 texels in 8 bytes (BC1, BC4) or 16 (BC5); decoding gives RGBA8 (BC4 in red, BC5 in red and green, the rest 0 and alpha 255). Everything is integer arithmetic: the same texels give the same blocks on every compiler, machine and worker count, and a partial block at the right or bottom edge repeats its last column and row.

- **BC1:** the endpoints come from a principal-axis fit (power iteration on the covariance in fixed point), tried at the extremes and pulled in by 1/8 and 1/4, then up to two (`Normal`) or four (`High`) least-squares refinements that are kept only when the error drops. The 4-color form is used for opaque blocks; with `punchthrough_alpha` (the minter turns it on unless `alpha ignore`), texels with alpha below 128 select the 3-color form's transparent code. Ties go to the lower code.
- **BC4 and BC5:** the endpoints start at the block's minimum and maximum, both orders are tried (the 8-step form and the 6-step form that adds 0 and 255), and least-squares refinement follows. BC5 is two BC4 blocks, red then green.
- **BC7:** the decoder reads all eight modes (partitions, anchors, rotations, the two index sets of modes 4 and 5, p-bits); its partition tables were read back from an independent decoder and 192 random blocks, 24 per mode, are checked against that decoder's output. The encoder writes **mode 6 only** (one subset, RGBA, 7-bit endpoints each with a p-bit, 4-bit indices): the range fit as for BC1 in four channels (extremes and 1/16 and 1/8 insets), all four p-bit combinations, then up to two (`Normal`) or six (`High`) least-squares refinements. An opaque block keeps both p-bits at 1 so alpha 255 is exact. A flat color is exact when its channels share a parity and within one step otherwise (the p-bit is shared by an endpoint's channels). The partitioned modes are not built. `format bc7` encodes alpha as 255 (`alpha` defaults to `ignore`); `bc7-alpha` keeps it (`straight`); `srgb true` gives `BC7_SRGB`.
- **On the GPU:** the engine's decoders are the reference, not a GPU's. `tests/rhi/bc_gpu_test.cpp` samples BC1, BC4, BC5 and BC7 textures on every GPU backend and compares them with `DecodeImage`: BC7 matches exactly, and BC1, BC4 and BC5 may differ by up to two steps per channel (a Radeon 860M, D3D12 and Vulkan alike), because the decoder truncates the interpolated values and a GPU computes them at its own precision.
- **Parallel:** `EncodeImage(..., scheduler, &blocks)` spreads block rows over a `jobs::Scheduler` (null: the calling thread); every block writes its own bytes, so the output does not depend on the worker count.

The measured errors of the tests (a 64x64 image, mean squared error per channel): a solid color 4.3 (the 5:6:5 grid), a gradient 8.8 in BC1 and 0.2 in BC4, a block-aligned checker 3.8, a normal map 25 in BC5; uniform noise has no line to fit and gives 2843 in BC1 (13.6 dB), 62 in BC4 and 61 in BC5.

In `.tex.src`, `format bc1` (with `srgb true` giving `BC1_SRGB`), `bc4` (red) and `bc5` (red and green) take the image's channels, filter the chain as RGBA8 (in linear light for sRGB) and compress each stored level; the blob keeps the compressed levels and `TextureView::Level` returns them as they are. `alpha` defaults to `straight` for bc1 and `ignore` for bc4 and bc5. `MintTexture` takes an optional scheduler for the compression; `asset_mint mint-texture` passes its own.

```cpp compile run standalone
#include "asset/texture.h"
#include "log/output.h"

using namespace vse;

int main() {
    asset::TypeRegistry types;
    Vector<byte> level0;  // a 4 x 4 R8 gradient
    for (u32 i = 0; i < 16; ++i) level0.Push(static_cast<byte>(i * 16));
    Vector<byte> chain;
    asset::MipOptions options;
    if (asset::GenerateMipChain(rhi::Format::R8_UNorm, 4, 4, ByteSpan(level0.Data(), level0.Size()), options, &chain)
            .Failed()) return 1;
    Vector<ByteSpan> levels;  // 4x4, 2x2, 1x1
    levels.Push(ByteSpan(chain.Data(), 16));
    levels.Push(ByteSpan(chain.Data() + 16, 4));
    levels.Push(ByteSpan(chain.Data() + 20, 1));
    asset::TextureBuildInput input;
    input.format = rhi::Format::R8_UNorm;
    input.width = 4;
    input.height = 4;
    input.mip_count = 3;
    input.subresources = Span<const ByteSpan>(levels.Data(), levels.Size());
    Vector<byte> blob;
    if (asset::BuildTextureBlob(types, asset::ResourceId::FromValue(7), input, &blob, 1).Failed()) return 1;
    asset::TextureView view;
    if (view.Open(ByteSpan(blob.Data(), blob.Size()), types).Failed()) return 1;
    const asset::TextureLevel top = view.Level(0, 0);
    out::Print("%u mips, row pitch %u, level 1 texel %u\n", view.MipCount(), top.row_pitch,
              static_cast<unsigned>(view.Level(0, 1).bytes[0]));
    return view.MipCount() == 3 && top.row_pitch == 256 ? 0 : 1;
}
```

## Height maps (`HGHT`)

A height map (`asset::types::kHeightMap`) is the data of a terrain: 16-bit samples over a height range, their mip chain and a pyramid of block minima and maxima. `asset::BuildHeightMapBlob` writes one from `width * height` little-endian `u16` samples (each side 2 to 8192), a height range (`range_min` for sample 0, `range_max` for 65535, minimum not above maximum), and a cell size in metres; the result is deterministic. Three sections follow the header: the levels packed from level 0 (each half the one before, rounded down to at least 1, a sample being the rounded mean of the four above it with the edge repeated), and, for each block of 8 by 8 cells of level 0, the smallest and largest sample the block touches, edges included.

`asset::HeightMapView` reads a blob in place (`Open` checks the sizes, the level count and that no block has its minimum above its maximum; the samples themselves are not scanned). `Sample`, `MipOffset` and `MipWidth` give the data to a GPU texture (`rhi::Format::R16_UNorm`) or a collider; `HeightAt(x, z)` is the height by the two triangles of a cell, each cell cut from its minimum-x, minimum-z corner to its maximum-x, maximum-z corner, with the point clamped to the map and only `+ - * /` in the arithmetic, so a collider and a CPU query agree to the bit on every machine; `NormalAt` is the surface normal by central differences one sample either side, as the terrain shader derives it.

`asset_mint mint-heightmap SOURCE.hgt.src OUTPUT.blob` mints a map from a text source (`id`, `source` naming a raw file of `width * height` 16-bit little-endian samples with no header, `width`, `height`, `range_min`, `range_max`, `cell_size`); the same source and tool give the same bytes.

## Materials

A material (`asset::types::kMaterial`, `MATL`) names a built-in template, the fragment permutation options it enables, a parameter block, texture bindings, sampler descriptions and render-state overrides. `asset::BuildMaterialBlob` writes it and `asset::LoadMaterialBlob` reads it back into an `asset::MaterialAssetData` (the GPU-backed material loader turns that into a `renderer::MaterialId`). The parameter block is stored in the template's std140 layout, so creating the material copies one block. The header's `kMaterialSkinnable` flag is set from the template (`gbuffer_skinned` and `gbuffer_skinned_color`), so a loader knows a material may be drawn on a skinned mesh without opening the template.

Each texture binding is a resource id and also a dependency of the blob (type `TEXR`, role = the first slot that uses it), so a texture used in two slots is one dependency and the closure loader follows the references without reading the sections. The loader checks that the dependency list matches the bindings.

The source is `.mat.src`, read by `asset_mint mint-material SOURCE.mat.src OUTPUT.blob`:

```text
id 00000000000000a1
template gbuffer
option NORMAL_MAP
param roughness float 0.25
param base_color color 1 0.5 0.25 1
texture albedo_tex albedo.texr
texture normal_tex normal.texr
sampler linear_sampler filter nearest address clamp anisotropy 8
state cull none
```

The minter checks the source against the template: a parameter must be a member of the shader's set 1 uniform block and have its type (an unknown or mistyped one lists the expected members), a texture slot must exist for the chosen options, and the texture's blob must have the slot's shape (2D, cube, array) and color space (the albedo slot takes sRGB data, the normal slot linear data). A texture path that does not resolve is a mint error with the source line. Parameters the source leaves out keep the template's defaults. A blob of another type version is refused with `UnsupportedVersion`, whose message names `asset_mint remint`.

## Levels

A level (`asset::types::kLevel`, `LEVL`) is the same data as a save: one `storage::EntityRecord` per entity, in key order, whose bytes are the entity container of `storage::WorldStorage` (a count, then framed component records encoded by the `storage::ComponentRegistry`). Saves and levels share one set of field codecs; a component registered for storage can be in a level with no further work. What a level adds is its references. A field that names a resource is declared with `storage::FieldOptions::Reference(storage::ResourceTag('M', 'E', 'S', 'H'), nullable)` (a `u64` member) or is a `storage::ResourceRef`: the stored bytes are the same eight as for a `u64`, the level minter types the reference against the declared resource type, and the blob lists every non-null use in a *reference table* (entity, component, field, declared type, id) and every distinct resource in the standard dependency section (role bit `kLevelRoleReference`; a level named by an `include` line has `kLevelRoleInclude`). The manager follows only that list, so a level's closure is known before any entity is made.

`asset::BuildLevelBlob` writes the blob and `asset::ParseLevelBlob` reads it back into an `asset::LevelData`; the parser recomputes the references from the records and refuses a table or dependency list that disagrees. `asset::LoadLevelBlob` creates the entities in a world through `storage::WorldStorage::Load`, so the world is untouched when any record is bad. A component record is read only at its registered version: a level written with an older one is refused with `UnsupportedVersion` and fixed by re-minting from the source, never converted. Derived state (transforms, bounds) is not stored; run the systems once after a load. `asset::MeshReference` (a mesh and a material by id, id 50) and `asset::EntityName` (the name the text form gives an entity, id 51) are the level components; `RegisterLevelComponents` registers them beside the builtin and animation components.

```cpp compile run standalone
#include "asset/level.h"
#include "ecs/transform.h"
#include "log/output.h"
#include "storage/builtin_components.h"
#include "storage/component_record.h"

using namespace vse;

int main() {
    asset::TypeRegistry types;
    storage::ComponentRegistry registry;
    if (storage::RegisterBuiltinComponents(&registry).Failed()) return 1;
    if (asset::RegisterLevelComponents(&registry).Failed()) return 1;

    // One entity with a mesh: its record is the encoding of the component, framed.
    asset::MeshReference mesh;
    mesh.mesh.id = 0x201;
    bytes::ByteWriter fields;
    const storage::CodecLimits limits;
    if (registry.FindById(asset::kMeshReferenceId)->Encode(&mesh, fields, limits).Failed()) return 1;
    bytes::ByteWriter container;
    container.WriteU32(1);
    storage::ComponentRecord record;
    record.id = asset::kMeshReferenceId;
    record.version = 1;
    record.bytes = fields.Bytes();
    if (storage::WriteComponentRecord(record, limits, container).Failed()) return 1;

    asset::LevelData data;
    storage::EntityKey key;
    key.low = 1;
    data.entities.Push(storage::EntityRecord::Make(key, container.Bytes()));
    asset::LevelResource resource;
    resource.id = asset::ResourceId::FromValue(0x201);
    resource.type = asset::types::kMesh;
    resource.type_version = 1;
    resource.roles = asset::kLevelRoleReference;
    data.resources.Push(resource);

    Vector<byte> blob;
    if (asset::BuildLevelBlob(types, registry, asset::ResourceId::FromValue(7), data, &blob).Failed()) return 1;

    ecs::World world;
    storage::KeyIndex index;
    asset::LevelData loaded;
    if (asset::LoadLevelBlob(ByteSpan(blob.Data(), blob.Size()), types, registry, world, index, &loaded).Failed()) return 1;
    const asset::MeshReference* got = world.Peek<asset::MeshReference>(index.Find(key));
    out::Print("%u entity, %u reference, mesh %llx\n", static_cast<unsigned>(loaded.entities.Size()),
              static_cast<unsigned>(loaded.references.Size()), static_cast<unsigned long long>(got->mesh.id));
    return got->mesh.id == 0x201 ? 0 : 1;
}
```
```output
1 entity, 1 reference, mesh 201
```

The source is `.level.src`, read by `asset_mint mint-level SOURCE.level.src OUTPUT.blob [--root=DIR]` (the references are resolved against the blobs under `--root`, by the paths their folders give them) and printed back by `asset_mint unmint-level LEVEL.blob OUTPUT.level.src`:

```text
id 00000000000000d0
include /levels/sky
entity ground
  component local_transform
    position = 0 -1 0
entity hero
  parent ground
  component mesh_reference
    mesh = /meshes/hero
    material = /materials/skin
  skeleton /skeletons/hero
  animation /clips/walk loop speed 1.5
entity lamp key @0000000000000000000000000000002a
  component local_transform
    position = 3 4 5
```

`id` comes first (`asset_mint adopt` writes it). `entity NAME` starts an entity; its key is derived from the level's id and the name (`asset::LevelEntityKey`), so the same text always gives the same keys and two levels may use one name, and `key @HEX32` pins one. `component TYPE` starts a record named as the registry names it, and `FIELD = VALUE` lines set its fields: the names and kinds come from the registry, so a new component needs no change to the parser, and a field left out keeps the value of a default-constructed component. `parent NAME` links the entity to its parent. `skeleton PATH` and `animation PATH [loop|once] [speed X]` are shorthand for the `skeleton_instance` and `animation_player` components; the printer always writes the long form. Includes come before the first entity and may not form a cycle.

The minter refuses with the line and the entity or field: an unknown component or field (listing the registry's names), a value that does not fit its field, a path that does not exist, a resource of another type than the field declares (a material where a mesh is wanted), a null in a field that is not nullable, a `parent` that is nowhere, an include that is not a level or that includes this level again, and a clip made for another skeleton than the entity's. It then loads the result into a scratch world the way the loader will. The printer writes entities in name order (unnamed ones last, by key) and the components in the order they are stored, so a level printed, minted and printed again is byte-identical, and `asset::FormatWorld` prints a loaded world as the same text.

## The resource manager

`asset::ResourceManager` is how a program asks for a resource: by id, getting a handle, never knowing which backend served it. It owns the mount table, the load buffer pool and a cache of loaded resources. `Mount` borrows a backend, `MountPath` opens a directory of loose blobs or a pak file and keeps it, and `MountFromSettings` mounts every entry of `assets.mounts` (`PREFIX=PATH` separated by `;`, later entries win). `Load<T>(id, &handle)` reads the blob into one aligned buffer from the pool, checks its container and version, loads its **closure first** and then calls the loader registered for the type; `LoadPath<T>` resolves a path through the mounts' tables first.

The closure follows only the dependencies the blob declares, each of the type and version the declaration says. The handle is published only when every one of them loaded, so a resource is never half loaded: a missing member is `MissingDependency` (the `LoadFailure` names the chain, `/levels/a -> /materials/m -> /textures/t`, with paths where the mounts know them), a member of another type or version is `DependencyTypeMismatch` (this is what a loose file shadowing a pak entry with the wrong content looks like), a cycle in hostile content is `DependencyCycle`, and a blob that is not the current version is `UnsupportedVersion`, naming `asset_mint remint`, counted in `Stats().rejected_old_version`. A loaded resource holds a reference on each of its dependencies, so one cannot go while a dependent is loaded: `Unload` answers `InUse` and names the dependents, and `Unload(id, true)` takes them with it (their handles go stale). The same answer is what `HostMemoryBackend` asks for through `InUseQuery()`, so it refuses to replace or remove a resource something loaded depends on.

A handle is an owning, reference-counted reference with a generation: a copy adds one, the last one frees the resource unless it is pinned (`Pin`, `Unpin`), and a handle whose resource was unloaded reads as invalid, never as the resource that took its slot. `handle.Get()` is the loaded value; it stays valid while a handle exists and until `PollChanges` reloads the resource. `PollChanges` takes the changes the mounts report (a watched directory, the memory backend) and reloads each changed resource **with its dependents, atomically**: the new objects are made first, the closure rules are checked again, and they replace the old ones only when all of that succeeded; otherwise the old version stays loaded and the result says why.

There is no silent fallback. A missing or invalid resource returns the status and no handle. Behind the development setting `assets.allow_fallbacks` (default off), a type with a registered fallback blob (`RegisterFallback`) gets it instead of a failure, logged and counted in `Stats().fallbacks_used`; a fallback never stands in for a dependency, so a closure with a missing member fails whole. `Stats()` also counts loads, cache hits, resident bytes, reads per mount, and `load_conversions`, the copies a loader made beyond the one read into the aligned buffer: it is zero for minted data, and a test asserts it. `assets.verify_on_load` (default off) hashes every blob as it loads, `assets.cache_bytes` bounds the resident bytes (a load past it is `LimitExceeded`; nothing is evicted behind a handle's back).

A loader turns a blob into an object of its C++ type (`RegisterLoader<T>(function)`, `ResourceTraits<T>` names the resource type). `asset::RegisterCpuLoaders` registers the ones that need no GPU: skeletons and clips and meshes and textures as their views over the manager's buffer (so a load copies nothing), materials as their description, levels as their summary. A clip is published only when the skeleton it was made for is loaded and its hash and joint count match, and a skinned mesh likewise (`DependencyTypeMismatch`); a later registration for a type replaces the earlier one, which is how the GPU-backed loaders take over.

```cpp compile run standalone
#include "asset/cpu_loaders.h"
#include "asset/level.h"
#include "asset/memory_backend.h"
#include "asset/resource_manager.h"
#include "log/output.h"
#include "storage/builtin_components.h"

using namespace vse;

int main() {
    asset::TypeRegistry types;
    storage::ComponentRegistry registry;
    if (storage::RegisterBuiltinComponents(&registry).Failed()) return 1;

    // A level with no entities, made in memory, in a backend.
    Vector<byte> blob;
    if (asset::BuildLevelBlob(types, registry, asset::ResourceId::FromValue(7), asset::LevelData(), &blob).Failed()) return 1;
    asset::HostMemoryBackend backend(types, u64{1} << 20);
    if (backend.Add(asset::ResourceId::FromValue(7), ByteSpan(blob.Data(), blob.Size())).Failed()) return 1;

    asset::ManagerConfig config;
    config.buffer_pool_bytes = u64{8} << 20;
    asset::ResourceManager manager(types, config);
    asset::RegisterCpuLoaders(&manager);
    if (manager.Mount(StringView("/"), &backend, 0).Failed()) return 1;

    asset::Handle<asset::LevelSummary> level;
    if (manager.Load<asset::LevelSummary>(asset::ResourceId::FromValue(7), &level).Failed()) return 1;
    const asset::ResourceStats stats = manager.Stats();
    out::Print("%u entities, %u loaded, %u conversions\n", level.Get()->entity_count, static_cast<unsigned>(stats.resident),
              static_cast<unsigned>(stats.load_conversions));
    return stats.resident == 1 && stats.load_conversions == 0 ? 0 : 1;
}
```
```output
0 entities, 1 loaded, 0 conversions
```

### GPU-backed resources and the level spawner

### Loading without stalling the frame

`asset::AsyncLoader` (`asset/async_loader.h`) loads on the job scheduler's workers and builds on the owner thread. `LoadAsync<T>(id, priority, cancel)` returns an `asset::LoadTicket`; the blob and the blobs of its closure are read into the manager's buffer pool on the workers, and `Update(max_microseconds, max_bytes)`, once per frame on the thread that owns the GPU objects, builds the objects (the loaders, GPU uploads among them) until the budget is spent, so a frame's cost is bounded; the leftovers wait for the next call and `Stats().ready` is the backlog. At most `assets.max_inflight` requests read at once, the others wait by priority (`Immediate`, `Normal`, `Background`; earlier first among equals), and when `assets.max_queued` are waiting the next request is a ticket that is already done with `LimitExceeded`. Two requests for one resource share one load (the more urgent priority wins), a request for a resource that is loaded is answered at once, and a failed load is not remembered: the next request is a new load. A ticket can be polled (`IsDone`, `Progress`), waited for through the loader (`Wait` runs `Update` without a budget), finished into a handle (`Finish`) or cancelled (`Cancel`, a `storage::CancelToken`, or destroying the ticket); a load nobody waits for is cancelled before it starts, while it reads (the check is between blobs) or before it is built, and nothing leaks. `LoadPathAsync<T>(path, ...)` resolves through the mounts' tables and stays pending while a table has not arrived. `LoadLevelAsync(id)` reads the level, then the levels it includes, then the rest in the order the level lists them, and `Progress` reports blobs read of blobs known; the level and everything it names are built together, as `Load` builds a closure, so the level is active when its ticket is done. Destroying the loader cancels every load and waits for the reading jobs. A device reset between a read and its build is harmless; point `GpuAssetContext::device` at the new device before the build.

```cpp compile run standalone
#include "asset/async_loader.h"
#include "asset/cpu_loaders.h"
#include "asset/level.h"
#include "asset/memory_backend.h"
#include "jobs/scheduler.h"
#include "log/output.h"
#include "storage/builtin_components.h"

using namespace vse;

int main() {
    asset::TypeRegistry types;
    storage::ComponentRegistry registry;
    if (storage::RegisterBuiltinComponents(&registry).Failed()) return 1;

    Vector<byte> blob;
    if (asset::BuildLevelBlob(types, registry, asset::ResourceId::FromValue(7), asset::LevelData(), &blob).Failed()) return 1;
    asset::HostMemoryBackend backend(types, u64{1} << 20);
    if (backend.Add(asset::ResourceId::FromValue(7), ByteSpan(blob.Data(), blob.Size())).Failed()) return 1;

    asset::ManagerConfig config;
    config.buffer_pool_bytes = u64{8} << 20;
    asset::ResourceManager manager(types, config);
    asset::RegisterCpuLoaders(&manager);
    if (manager.Mount(StringView("/"), &backend, 0).Failed()) return 1;

    jobs::Scheduler::Config workers;
    workers.participants = 2;  // this thread and one worker
    workers.pin = false;
    jobs::Scheduler scheduler(workers);
    asset::AsyncLoader loader(manager, scheduler);
    asset::LoadTicket ticket = loader.LoadAsync<asset::LevelSummary>(asset::ResourceId::FromValue(7), asset::LoadPriority::Normal);
    while (!ticket.IsDone()) {
        loader.Update(2000, u64{1} << 20);  // a frame: at most 2 ms of building and 1 MiB of blobs
    }
    asset::Handle<asset::LevelSummary> level;
    if (ticket.Finish(&level).Failed()) return 1;
    out::Print("%u entities, %u loaded, %u in the backlog\n", level.Get()->entity_count,
              static_cast<unsigned>(manager.Stats().resident), static_cast<unsigned>(loader.Stats().ready));
    return level.Get()->entity_count == 0 && loader.Stats().ready == 0 ? 0 : 1;
}
```
```output
0 entities, 1 loaded, 0 in the backlog
```

### Serving resources over the network

`asset::ResourceServer` (`asset/resource_server.h`) serves any `ResourceBackend` (a directory, a pak, host memory) to many clients over a `net::Transport` (the TCP transport in practice), and `tools/resource_server` is the command line around it: `resource_server --root DIR --listen ADDR:PORT` or `--pak FILE`, with `--seconds N` to stop after a while; its options are its own command line, not engine settings. The server is read-only (the protocol has no request that writes), polled (`Update()` takes the transport's events and answers) and has no encryption or authentication: use it on a network you trust. The transport's own limits (connections, rate limits, queues) apply as they are.

The protocol (`asset/resource_protocol.h`, version 1) is one transport message per request or response, encoded with `net::NetWriter`; a message is a type byte, a request id that the response repeats (so responses may interleave) and the fields below. A client says `Hello` first; a server closes a connection that sends anything else first, a second `Hello`, a message it cannot decode, a response where a request belongs, or an unknown type.

| Message | Direction | Fields |
|---|---|---|
| `Hello` | client to server | version (u16), the largest response the client takes (u32) |
| `HelloAck` | server to client | version (u16), the largest read the server serves to this client (u32), entries per page (u32), requests per poll (u32) |
| `Stat` / `StatOk` | both | id (u64) / size in bytes (u64) |
| `Table` | client to server | the generation the client has (u64, 0 for none), page (u32) |
| `TableOk` | server to client | generation (u64), unchanged (bool), page (u32), page count (u32); unless unchanged: a count, then per entry the id (u64), the type tag (u32) and the path |
| `List` / `ListOk` | both | prefix (string), page (u32) / the table generation (u64), page (u32), more (bool), a count and the ids (u64) |
| `Read` / `ReadOk` | both | id (u64), offset (u64), length (u32) / offset (u64), the bytes |
| `Changes` / `ChangesOk` | both | the epoch the client has (u64) / the server's epoch (u64), reset (bool), a count and per change its kind (u8: added, replaced, removed) and id (u64) |
| `Error` | server to client | a code: `NotFound`, `Range` (a read starts past the end), `TooLarge` (a read above the limit, or a response that would not fit a message), `Busy`, `Invalid` (a page past the last), `Unsupported`, `Other` |

Limits: a path or prefix is at most 1,024 bytes and a page at most 1,024 entries (`assets.net.page_entries`, default 256); a read at most `assets.net.max_read_bytes` (default 256 KiB, and never more than the transport's message holds) or what the client said it takes; a connection has at most `assets.net.max_inflight` requests answered per `Update` (default 16) and the rest are answered `Busy`. Every length is checked against its limit and the bytes that remain before anything is allocated, and every value has one encoding. A read that runs past the end returns the bytes that are there (the last range is short); a read at the end returns none. The table has a generation, a hash of its entries (the same table has the same generation after a restart, and a table that changes gets another): a client that sends the generation it has gets `unchanged` and no entries, and a list reply carries it too so that a change between two pages shows. New bytes under the same ids and paths do not change it. The server asks the backend what changed only when a client asks for the table or a list. A version other than 1 in `Hello` is answered `Unsupported` and the connection is closed. If a client's send queue is full, the answer is replaced by `Busy`.

```cpp compile run standalone
#include "asset/resource_protocol.h"
#include "log/output.h"

using namespace vse;

int main() {
    asset::ProtoData request;
    request.type = asset::ProtoMessage::Read;
    request.request = 7;
    request.id = 0x1234;
    request.offset = 100;
    request.length = 4096;

    byte buffer[128];
    usize size = 0;
    if (asset::EncodeProto(request, MutableByteSpan(buffer, sizeof(buffer)), &size) != asset::ProtoError::None) return 1;
    asset::ProtoData decoded;
    if (asset::DecodeProto(ByteSpan(buffer, size), 4096, &decoded) != asset::ProtoError::None) return 1;
    out::Print("read of id %llx, %llu bytes from %llu, in a message of %u bytes\n",
              static_cast<unsigned long long>(decoded.id), static_cast<unsigned long long>(decoded.length),
              static_cast<unsigned long long>(decoded.offset), static_cast<unsigned>(size));
    return decoded.request == 7 && decoded.length == 4096 ? 0 : 1;
}
```
```output
read of id 1234, 4096 bytes from 100, in a message of 25 bytes
```

`asset::RegisterGpuLoaders(&manager, &context)` registers loaders for the types that live on the GPU, over the CPU loaders of the same resource types (a later registration replaces the earlier one, and the C++ type of the loaded object tells the two apart: `Handle<asset::GpuMeshAsset>`, `Handle<asset::GpuTextureAsset>`, `Handle<asset::GpuMaterialAsset>`). The context names the device, the `renderer::DeviceResources` the objects register with, the material system and the G-buffer templates.

The blob's bytes are shared, not pointed at. The manager reads a blob into one `asset::BlobBuffer` (a `SharedBytes`), and the GPU objects hold a share of it: a `renderer::MeshGeometry` names the vertex, index and skin sections by offset, and `GpuTexture::CreateFromSharedLevels` takes the levels as offsets too. A mesh uploads from where the bytes are (nothing is copied; `Stats().load_conversions` stays 0), and after a device reset it uploads again from the same shared bytes, even when the manager has unloaded or reloaded the resource meanwhile: the last holder of the mesh or texture is what returns the buffer to the pool. A material holds a share of the textures and samplers it binds. Rule for new code: classes that need the same data share a `SharedPtr` to a small class that holds it, and never keep a raw pointer into each other's storage.

A skinned mesh is refused unless the skeleton it was made for is loaded (the closure rule). Fallbacks (`asset::RegisterGpuFallbacks`: a unit cube, a magenta and black checker, a magenta material) stand in for a missing or invalid mesh, texture or material only with `assets.allow_fallbacks` on, each use logged and counted in `Stats().fallbacks_used`; every test that loads minted data asserts the count is 0.

`scene::SpawnLevel(manager, level, registry, world, index, &spawned)` loads a level and its closure, spawns the levels it includes first, creates the entities with `asset::LoadLevelBlob`, adds the world transform the transform system fills (a level stores no derived state) and gives every `mesh_reference` a `scene::MeshRenderer` that shares the manager's mesh (`MeshRenderer::mesh`, a `renderer::GpuMeshPtr`) and names the material system's slot of the referenced material. The `SpawnedLevel` holds the level and its materials loaded; `scene::DestroyLevel` removes the entities and lets them go. If anything fails, the entities made so far are destroyed. `animation::ManagerResolver` is the animation system's resolver over the manager: it finds skeletons and clips by id among the loaded resources and takes no references (the level that names them keeps them loaded). A mesh the manager loaded is not ready to draw until its upload barrier is recorded (`GpuMesh::RecordPrepare`, then `MarkReady`), as for every mesh.

### Reading from a server: the network backend, the cache and offline mode

`asset::NetworkBackend` (`asset/network_backend.h`) is a `ResourceBackend` whose resources live on a resource server. Give it a `net::Transport`, the server's endpoint and a clock (`NetworkBackendOptions`); mount it like any backend. It starts a thread of its own that polls the transport (turn that off with `background_thread = false` and call `Poll()`, or let the blocking calls poll while they wait, with an `idle` function between polls; the deterministic tests do this over simulated streams).

- **Reading.** A resource of at most `assets.net.prefetch_bytes` (1 MiB) is fetched whole: the backend reads its 64-byte header first and asks the cache whether the hash the header records is held. A held blob is served from disk and not fetched; any other is fetched, checked against its recorded hash (a mismatch is `Corrupt` and nothing is cached) and stored. A larger resource, or one with no header, is read by range, a request per `ReadAt`. `Stat`, `ReadTable`, `List` and `Watch` are requests too; the table is read in pages with the generation the backend holds, so an unchanged table costs one small message, and `ReadTableAsync` is native: it publishes each page to the ticket as it arrives.
- **Retry.** A request that fails to connect, is lost with its connection or gets no answer within `assets.net.timeout_ms` is tried again after `assets.net.backoff_ms`, doubled each time up to a limit, with a random share added from a generator seeded by the config (so a test replays a run). A connection that does not answer is closed and a new one made. After `assets.net.max_retries` retries the state is `Offline`. A server's `Busy` counts as a failure; its other errors (`NotFound`, `Range`, ...) are returned at once and not retried.
- **Offline.** While `Offline`, `Stat` and `Open` are answered from the cache for an id it knows (the hash and size it last saw, with the blob held), `ReadTable` returns the last table read, and everything else is `ErrorCode::Offline`. The backend reconnects every `reconnect_nanos`; the first greeted connection makes it `Online` again. A reader opened before the server went away keeps working if the resource was fetched whole; a range reader fails with `Offline`.
- **Watch.** The first `Watch` learns the server's epoch and reports nothing; later ones send the epoch and get the changes the server's own `Watch` saw since (`Added`, `Replaced`, `Removed`), or a reset when the server cannot say (the client is too far behind), in which case the backend reads the table again and reports the ids that came and went.
- **The cache.** `asset::BlobCache` is a directory of `<hash>.blob` files (written to a temporary and renamed, so a crash leaves a whole file or none) and a text index of which id had which hash. It is bounded by `assets.net.cache_bytes` with least-recently-used eviction (the name of an evicted blob stays in the index so `Stat` still answers offline), and opening it hashes every file and drops the ones that do not match their name. `assets.cache_dir` names the directory (empty: no disk cache); `asset::NetworkCacheFromSettings` opens it. Blobs are never trusted by name alone.
- **Settings.** `asset::RegisterNetworkSettings` declares `assets.net.timeout_ms`, `assets.net.max_retries`, `assets.net.backoff_ms`, `assets.net.prefetch_bytes`, `assets.net.cache_bytes` and `assets.cache_dir`; `asset::NetworkBackendConfigFromSettings` reads them.

The tests run the conformance suite over real loopback sockets (`asset_tests_sockets`, label `network`) and, with simulated streams and a manual clock, the cache hit, miss and eviction, a server that disappears and returns, offline serving, `Watch`, the asynchronous table read and the `Changes` request.

## The `asset_mint` project

`asset_mint COMMAND PROJECT.project [--force] [--workers=N] [--quiet] [--verbose]` works on a whole source tree (`tools/asset_mint/project.h`; the one-file commands `mint-skeleton`, `mint-mesh` and the others stay). The project file is text:

```
source_root src         # where the sources are (relative to the project file)
output_root out         # blobs, resources.table, assets.manifest and the pak
pak game.pak
target 0
exclude drafts          # a path prefix below source_root that is not part of the project
rule .tex.src skip      # the only rule option so far: leave a kind of source out
```

A source is a file named `NAME.KIND.src` (`.skel.src`, `.mesh.src`, `.anim.src`, `.tex.src`, `.mat.src`, `.level.src`) whose first record is `id HEX16`. Its resource path is its path below `source_root` without the extensions (`meshes/rock.mesh.src` is `/meshes/rock`, normalized to NFC) and its blob is `meshes/rock.mesh.blob` below `output_root`. A reference in a source is a resource path (absolute, or relative to the source's folder) or the id as `@HEX16`; the tool finds them by reading every source for `@HEX16`, a bare id after `skeleton`, a word that starts with `/` and a material's `texture SLOT PATH` path, and matching them against the project's paths and ids (any other word is not a reference). The blobs of a mint are kept in memory only while a source that needs them is still to be minted.

- `adopt` writes a fresh id into every source that has none (the only command that writes an id; running it again changes nothing).
- `mint` mints every source whose inputs changed. The order follows the references (a wave per depth, the sources of a wave in parallel on the job system, textures one at a time because a block-compressed one fans out over the workers itself). The result does not depend on the worker count or on the file system's listing order. Every error is reported with its file and line, a source that needs a failed one is reported as not minted, and the others are still minted; the exit code is 1 when anything failed. A project with a source problem (a missing id, two sources with one id, a cycle, or a `skeleton`, `include`, `animation` or `texture` line that names no source) mints nothing; a reference elsewhere in a level that names nothing is the minter's error, reported with its line.
- The manifest `assets.manifest` records for each blob the hash of its inputs (the source text, a texture's image files, the hash and id of every blob the source references, directly or through others, the target, the type's version and the tool's version) and the blob's hash. A source whose inputs match and whose file is intact is not minted; a minted blob equal to the file on disk is not written again (so the file's time does not move); `--force` mints everything. A source removed by hand makes the next `mint` name every blob that referenced it and mint none of them; a blob no source maps to any more (a removed, renamed or moved source) is deleted once a mint succeeds.
- `pack` packs the blobs of the output root into the pak (it refuses a blob that is not the current version or whose hash does not hold, and an incomplete closure).
- `verify` checks every blob (header, current version, hash, one blob per id), the closure (every reference is a blob of the output, of the declared type), `resources.table` against the blobs (both ways) and the pak.
- `list` prints id, type, size and path of the output; `list --skeletons` prints each skeleton's joint count, its hash and the meshes and clips made for it.
- `check` checks the sources without minting.

The sample project of the tests is `tests/asset/mint_sample`.

### Changing a project without breaking a reference, packing for a target, and versions

More commands of `asset_mint COMMAND PROJECT.project ...`:

- `refs ASSET` prints what the asset references and what references it. `ASSET` is a resource path (`/meshes/rock`), an id or a source path below `source_root`.
- `rename ASSET NEWNAME` changes the name of a source (its folder stays), `move ASSET NEWFOLDER` moves it to another folder. The id does not change, and every other source that names the old path is rewritten to the new one, so the next `mint` gives every dependent the very same blob bytes (blobs hold ids only); the blob of the moved asset is written at its new place and the old file is removed. A path that is taken is refused.
- `delete ASSET` is refused while another source references the asset: the referrers are named, nothing is removed and the exit code is 1. `--with-dependents` also lists what references the referrers; it still removes nothing.
- `copy ASSET NEWPATH` copies a source to a new resource path with a fresh id. `reid ASSET` gives a source a fresh id: it fixes two sources that share one (name the source by its path below the source root), and refuses an id other sources reference by id unless `--update-references` rewrites those references.
- `unmint ASSET [--out FILE]` writes a minted level or material back as source text; minting the text gives the same blob.
- `pack --target N` packs for another target (`sample.t1.pak`): each blob is converted by the transform of its type (`tools/asset_mint/target_transform.h`): textures are laid out again for the target's row pitch and slice alignment, every other type is retagged (the target tag in the header and the hash change, nothing else). The result is checked (the same type, id and references, a valid hash); a transform that adds a reference or ignores the target is refused, and a target no layout is known for fails naming the texture. Two packs for one target are the same bytes, and a pak for one target is refused by a program that runs another (`WrongTarget`). `verify --target N` checks that pak.
- `verify --require-current` fails, naming every blob that is not the current version of its type, with the fix. Without it an old blob is only checked by its header and hash. `pack` refuses an old blob. `remint PROJECT.project` mints every source again and then requires the output to be current; `remint DIRECTORY` rewrites old-version blobs of a folder through the upgrade chains (`tools/asset_mint/upgrade.h`: one function per type and version). The level type is at version 2 (positions in double precision) and the others at version 1; no older blob has an upgrade chain, so none is registered (a level is re-minted from its source); the manifest includes each type's version in the hash of every source, so a bump makes the next `mint` rebuild that type's blobs.

## Convex hulls (`HULL`)

`HULL` (`asset/hull.h`) is the convex collision shape of the physics module: `BuildHullGeometry` takes up to 4096 points, welds them, builds the hull with quickhull in double precision, merges coplanar triangles into polygon faces (at most 255 vertices, 512 faces, 64 vertices per face; more is refused with a sentence) and computes the volume, centroid and inertia tensor about the centroid from the face fans. `BuildHullBlob` writes sections 16-20 behind an 80-byte header (counts, volume, centroid, inertia `xx yy zz xy xz yz`, extent); `HullView::Open` reads it in place. The minter is `asset_mint mint-hull FILE.hull.src --out=FILE.blob`, whose source is `id <hex>` and `v x y z` lines. Minting is deterministic (the same points give the same bytes). The blob is not loaded through the `ResourceManager` yet (`KNOWN-ISSUES.md`): a program builds a `physics::ConvexHull` from a `HullView` and adds it to the world's `HullLibrary`.

## Mesh colliders (`MCOL`)

`MCOL` (`asset/mesh_collider.h`) is the triangle mesh of a static physics collider: `BuildMeshColliderBlob` takes positions, triangle indices and a material index per triangle (up to 1,048,576 vertices and triangles), refuses a non-finite position, an index outside the vertices and a triangle with no area, and writes a 32-byte header (counts and bounds) and three sections; `MeshColliderView::Open` reads it in place from a `SharedBytesPtr` and checks every number again. The bounding-volume hierarchy is not stored: the physics module builds it from these bytes when the mesh is added (deterministically). `asset_mint mint-mcol FILE.mcol.src --out=FILE.blob` reads `id <hex>`, `v x y z` and `t a b c [material]` lines. **One area rule**: `asset::MeshColliderTriangleHasArea` is physics' own test (`physics::TriangleMesh::Create`: the `f32` cross product of two edges, its squared length finite and above `1e-20`), and both the builder and `MeshColliderView::Open` use it, so an `MCOL` that opens always makes a physics mesh. **Loading:** `RegisterCpuLoaders` registers a loader, so an `MCOL` loads through the resource manager as a `MeshColliderView` (`ResourceTraits<MeshColliderView>`). **In a level:** the stored component `static_collider` (`asset::StaticCollider`, id 53) gives an entity one static collider: a shape (`0` mesh, `1` box, `2` sphere, `3` capsule), the `MCOL` of a mesh shape (part of the level's closure), the unscaled sizes (`half_extents`, `radius`, `half_height`), `friction` and `restitution`; whoever makes the physics body applies the entity's world scale then. Not built yet (`KNOWN-ISSUES.md`): minting a collider from a glTF mesh, and the baked hierarchy.

## Scripts (`SCRP`)

`SCRP` (`asset/script_blob.h`) is a script as an asset: the source text with the facts the minter found in it, so a load never parses to learn them. `BuildScriptBlob` writes five sections (a 32-byte header with the source size, the entry count and a hash of the source; the source, UTF-8 with `\n` line ends; the language name and version; the entry points, the names of the behaviors the source defines; and an optional bytecode chunk behind a key) and the container's dependency section lists the scripts it requires, by id, strictly increasing. `ScriptView::Open` reads it in place from a `SharedBytesPtr`: `Source()` is a range of the loaded buffer, never a copy, and `BytecodeFor(key)` returns the chunk only when the key is exactly the loader's (the chunk is never required and never used under another key). The asset module knows no language; the script loader hands the source to the backend.

The minter is `asset_mint mint-script FILE.script.src OUTPUT.blob [--search=DIR] [--bytecode] [--no-run] [--target=N]`. The source form is `id <hex>`, `lua <file>` (relative to the source) and one `dep <module> <hex id>` line per script it requires. It does, in this order:

- normalizes the text (a byte-order mark is dropped, `\r\n` and `\r` become `\n`) and refuses a NUL byte, text that is not UTF-8, and more than 1 MiB;
- scans the text without running it (comments and strings skipped): a global named `io`, `os`, `debug`, `package`, `ffi`, `jit`, `load`, `loadstring`, `loadfile`, `dofile`, `setfenv`, `getfenv`, `module` or `newproxy`, or `string.dump`, is refused with its line (a field of that name, `t.io`, is fine), and so is a `require` whose argument is not one literal name;
- rewrites each `require("assets.weapon")` to `__require("<hex id>")` through the `dep` lines, keeping every line number: a script depends on ids, never on file paths. A `require` with no `dep` line, and a `dep` that is never required, are errors;
- walks the dependencies (each module `a.b` is `a/b.script.src` under `--search`, by default the source's directory, and its `id` must be the one the `dep` line gives) and refuses a missing one or a cycle, printing the chain (`player -> assets.weapon -> assets.player`);
- compiles the rewritten text through the backend (a syntax error is reported as `file:line: message`);
- unless `--no-run`, runs the source once in a scratch state with a stub `__require` and a recording `behavior`, to list the behaviors it defines and to check that every lifecycle callback (`on_create`, `on_enable`, `on_disable`, `on_start`, `on_fixed_update`, `on_update`, `on_destroy`) is a function and no behavior is defined twice. The run has no engine bindings, so top-level code that calls one (`time.dt()`) fails here: define behaviors at the top level and do the work in the callbacks, or mint with `--no-run`;
- with `--bytecode`, adds the compiled chunk under the key `<language> <version> <processor>`.

Minting is deterministic: the same inputs and tool version give the same bytes, with and without the bytecode (`tests/script/script_asset_test.cpp` mints twice and compares). The tool needs the LuaJIT backend, which builds on Linux x86-64 only so far; elsewhere `mint-script` says so and fails.

## Importing glTF

`engine/asset_import` (`asset_import/gltf_import.h`) imports glTF 2.0 files (`.gltf` and `.glb`) into the engine's own resources, in memory: bytes in, blobs out. It is a library a program opts into (a host that fetches `.glb` files at run time, and `asset_mint import-gltf`, which is a thin command over it); no runtime engine module includes it, and the runtime still reads only its own formats.

```cpp
asset::NoGltfSource none;                       // a .glb has everything inside it
asset::ImportOptions options;                   // limits, an id source, the manifest of an earlier import
asset::ImportResult result;
asset::GltfError error;
const asset::Status status = asset::ImportGltf(file_bytes, none, options, &result, &error);
if (status.Ok()) { asset::ImportIntoBackend(host_memory_backend, result); }   // then mount the backend
```

**Built so far:** the container, the accessors, the limits, the id sources, the import manifest, `ImportIntoBackend`, images, textures, materials, meshes, skeletons and clips, the asynchronous entry `ImportGltfAsync`, and `asset_mint import-gltf`. A glTF scene (`--emit-level`) is not imported yet (`KNOWN-ISSUES.md`).

- **Hostile input.** A file may come from the network. Nothing aborts: every failure is a `Status` and a `GltfError` sentence that names the glTF object (`accessors[3]: does not lie inside its buffer view`). Every count and size is checked against `ImportLimits` before memory is allocated for it (the settings `assets.import.*` are declared with the importer); every offset and count is computed with checked arithmetic, so no accessor can read outside its buffer.
- **The container.** `.glb` (the 12-byte header, the JSON chunk, an optional BIN chunk, lengths and padding checked) and `.gltf` (JSON with external files and base64 `data:` URIs). Only glTF 2.0 is read; `extensionsRequired` is checked against the supported list (`KHR_lights_punctual`, `KHR_materials_emissive_strength`, `KHR_materials_pbrSpecularGlossiness`, `KHR_materials_unlit`, `KHR_texture_transform`) and anything else is refused naming it.
- **External files.** A URI is turned into a clean relative name (percent-escapes decoded; no `..`, no absolute path, no scheme, no backslash, no empty segment) and handed to a `GltfSource`, which the host implements: the tool reads files inside the glTF's folder, a host reads from wherever it fetched the glTF. `NoGltfSource` is for a `.glb`.
- **JSON.** The parser is yyjson, vendored (`third_party/yyjson`), reached only through `json_view.*` (the gate `json_boundary_check`): strict RFC 8259, nesting at most 128 deep, memory bounded by the length of the input; a repeated key is refused.
- **Accessors.** `ReadAccessorFloats` and `ReadAccessorUnsigned` read every component type, `normalized` values, `byteStride`, matrices (columns padded to four bytes), accessors with no buffer view (zeros) and sparse overrides (indices strictly increasing and below the count); a float that is not finite is refused.
- **Ids.** A new resource gets an id from an `IdSource` (`RandomIdSource` by default, `SeededIdSource` for byte-identical imports). `ImportManifest` records the ids an import gave, as text (`Format`/`Parse`), so a re-import of the same file keeps them: an object matches by kind and name when the name is unique, else by kind and index.
- **Into the engine.** `ImportIntoBackend(backend, result)` adds every resource to a `HostMemoryBackend` under its path, atomically: when one is refused, the ones added before are removed again.
- **Meshes.** Each glTF mesh becomes one mesh resource at `<prefix>/meshes/<name>` (`/gltf/meshes/<name>` by default; a name's `/`, `\` and control characters become `_`, a repeated or empty name is made distinct, a nameless mesh is `mesh<N>`), and each primitive becomes a material slot named after its material (`primitive<N>` when it has none, or the name is taken; cut to 31 bytes). The blob is built by `BuildMeshBlob` from a `MeshBuildInput`, so an imported mesh is exactly what a `.mesh.src` of the same data mints. `TRIANGLES`, `TRIANGLE_STRIP` and `TRIANGLE_FAN` become triangle lists (the degenerate triangles of a strip are dropped); points and lines are refused. glTF and the engine share the convention (right-handed, Y up, counter-clockwise front faces), so nothing is flipped; `ImportOptions::scale` multiplies the positions (glTF is in metres).
- **What the file lacks is made, deterministically.** No `NORMAL`: smooth normals, the sum of the area-weighted face normals around each vertex (`ImportOptions::flat_normals` gives every triangle its own vertices and its face normal instead). No `TANGENT`: per-triangle tangents from the texture-coordinate derivatives, summed per vertex and made perpendicular to the normal, the bitangent sign the handedness of the mapping (this is **not** MikkTSpace, and is recorded in `KNOWN-ISSUES.md`). No `TEXCOORD_0`: zeros, counted as a warning. `COLOR_0` (`VEC3` or `VEC4`, float or normalized integers, linear) becomes the mesh's colour stream, encoded to sRGB 8-bit by `EncodeMeshColor`; a mesh with it on any primitive has colours on all (white where a primitive has none), and its materials are on the colour template: a material that coloured and uncoloured meshes share gets a twin `<name>_color` (`gbuffer_color`, or `gbuffer_skinned_color`; manifest key `kGltfColorTwinKey` plus its index), which the coloured meshes' level slots name. `COLOR_1` and up, `TEXCOORD_1` and up, and custom attributes are dropped, each counted as a warning in `ImportReport` (`warnings`, `notes`). Morph targets are imported: a primitive's `targets` (`POSITION`, `NORMAL` and `TANGENT` deltas; sparse accessors are read like the others; positions scaled with the mesh) become the mesh's targets, named from `mesh.extras.targetNames` (else `target<N>`). Every primitive of a mesh must have the same number (refused otherwise, naming the primitive), and more than 64 is refused; made normals get no deltas. `ImportOptions::drop_morph_targets` still imports the base shape only (a warning). The level gives the node's entity the default weights, `node.weights` or else `mesh.weights`, as `animation::MorphWeights`.
- **Skin.** `JOINTS_n`/`WEIGHTS_n` become the mesh's skin stream (the four largest influences, weights quantized to sum to 255); more than four influences keep the largest four with a warning. The skeleton they refer to comes from a `MeshSkeletonBinding` per mesh, which `ImportGltf` fills from the nodes that carry both the mesh and a skin (a mesh used with two different skins is refused); `JOINTS_n` values are remapped from the skin's joint order to the skeleton's. A skinned primitive of a mesh with no skeleton (`ImportMeshes` called alone) is imported unskinned, with a warning.
- **The in-memory loop.** `ImportIntoBackend` puts the blobs in a `HostMemoryBackend`; `ResourceManager::Mount` mounts it, and `LoadPath<MeshView>("/gltf/meshes/tri")`, `Load<MeshView>(id)` and the asynchronous `AsyncLoader` load them like any other resource. Importing the same file again with the `ImportManifest` of the first import keeps every id (matched by name, so reordering or inserting meshes keeps them); `HostMemoryBackend::Replace` of the new blobs makes the manager's `PollChanges` reload the loaded meshes.

```cpp
asset::HostMemoryBackend backend(types, 64u << 20);
asset::ImportGltf(glb, none, options, &result, &error);            // meshes: /gltf/meshes/<name>
asset::ImportIntoBackend(backend, result);
manager.Mount("/", &backend, 0);                                   // the rest of the engine reads from it
manager.Mounts().RefreshTables();
asset::Handle<asset::MeshView> mesh;
manager.LoadPath<asset::MeshView>("/gltf/meshes/tri", &mesh);
```
- **Skeletons.** Each glTF skin becomes one skeleton at `<prefix>/skeletons/<name>` (`skin<N>` when unnamed). Joints are parent first: the skin's own order when it already is one, otherwise a stable order by node index (`JOINTS_n` values and clips are remapped). A node between two joints that is no joint is folded into the child's bind-local transform; the transform above a root joint stays in the root, made relative to the world transform of the node that uses the skin (the first by index), so the mesh entity's own transform puts it back once: glTF places a skinned mesh by its joints alone, and the engine draws the entity's world transform times the palette (before 2026-11-20 the skeleton was in the scene's space, and a file whose root node is scaled or turned had that transform twice). A skin no node uses stays in the scene's space; other nodes that use the skin at another place are a counted warning. A bind transform with a shear (translation, rotation and scale cannot hold it) is refused naming the joint and the node. A joint is named after its node (`joint_<node>` when it has no name; a repeat gets `_<n>`; a name over 64 bytes is cut and ends in a hash), and every rename is a line in `ImportReport::notes`. The inverse bind matrices of the skin replace the computed ones (a stored matrix that differs from the bind pose by more than 1e-4 is a counted warning; one that is not affine, or the wrong count, is refused); more than 256 joints is refused. `ImportOptions::scale` scales the joints' translations and the translation of the inverse bind matrices with the mesh positions, so a skinned mesh keeps its shape.
- **Clips.** Each glTF animation yields one clip per skeleton it animates, at `<prefix>/clips/<animation>_<skin>`. `translation`, `rotation` and `scale` channels on joint nodes become tracks; a channel on a node that is no joint of any skin is dropped with a counted warning. A `weights` channel becomes a clip's weight track: in the clip of the node's own skin when the animation has one (a skinned, morphed mesh plays both from one player), otherwise in a clip of the weights alone, `<animation>_<node>`, which has no skeleton (manifest key `0x80000000 + animation * 32768 + node`). `LINEAR` keys are used as they are, `STEP` gets a key one tick before each change, and `CUBICSPLINE` keeps each key's value and drops the tangents (an approximation). `LINEAR` keys map to keys; `STEP` becomes a pair of keys (the old value to the step, the new one a tick, `ImportOptions::clip_rate`, after it); `CUBICSPLINE` is resampled to linear keys at `clip_rate` (30 Hz by default) and the largest error that made is recorded in `ImportReport::resampling_translation_error` and `resampling_rotation_degrees`; rotations are kept in one hemisphere from key to key. `BuildClipBlob` then reduces and quantizes the keys, so the declared bounds are those of the final blob. The duration is the last key time over all channels; `ImportOptions::looping_clips` names the animations (or clips) that loop, since glTF has no loop flag; no events, no root motion. A joint with non-joint nodes folded into its bind pose (the armature node of an exporter) keeps its clip right: for such an ancestor that is a rotation, a translation and one scale, the keys are carried through it exactly (translation `T_a + R_a (s T)`, rotation `R_a R`, scale `s S`); an ancestor with a shear or an uneven scale cannot follow a moving rotation or scale, and those channels are dropped with a counted warning. `scale` also scales the translation keys. A mesh that nodes use with two different skins is refused.
- **Images.** A glTF image is a URI (a `data:` URI, or a file the `GltfSource` reads), or a buffer view. PNG (libpng over zlib) and JPEG (stb_image's JPEG decoder) decode to RGBA8 through `DecodePixels` in `asset_import/image_decode.h`: palette, gray, gray-alpha, transparency chunks and interlacing expand, 16 bits per channel reduce to 8 with a counted warning, and the dimensions (at most 16384 a side) and the pixel count (`ImportLimits::max_pixels`) are checked before any pixel is decoded. A decoder fails by return value; a hostile image never aborts. A baseline or progressive 8-bit JPEG decodes; 12-bit and arithmetic-coded ones do not, and some valid high-quality progressive files fail (`KNOWN-ISSUES.md`).
- **Textures.** Each glTF texture becomes one texture resource per way a material uses it: `<prefix>/textures/<name>` for the base color (BC7, sRGB, alpha kept when the image has any) `<name>_normal` for the normal map (BC7, linear), `<name>_mr` for the metallic-roughness map and `<name>_occlusion` for an occlusion map (BC7, linear; an ORM texture, the same glTF texture as both, is made once as `_mr` and bound to both slots), and `<name>_emissive` for the emissive map (BC7, sRGB): the pixels go through `MintTextureFromPixels`, the same core as `asset_mint mint-texture`, so the mip chain and the block compression are the deterministic ones.
- **Materials.** Each glTF material becomes one material of the standard G-buffer template (`gbuffer`, or `gbuffer_skinned` when a skinned mesh uses it) at `<prefix>/materials/<name>`: `baseColorFactor`, `metallicFactor`, `roughnessFactor`, `alphaCutoff`, the emissive colour (`emissiveFactor` as it is, with `KHR_materials_emissive_strength` as its strength), `albedo_tex` and `normal_tex` with the sampler of the first texture, `MASK` as the `ALPHA_TEST` option, `BLEND` as alpha blending without depth writes, `doubleSided` as no culling. The metallic-roughness, occlusion and emissive textures become `metallic_roughness_tex`, `occlusion_tex` (with `occlusionTexture.strength` as `occlusion_strength` when it is not 1) and `emissive_tex`; an unlit material binds none of them. A map on `TEXCOORD_1` or above is dropped with a counted warning, as before; a texture transform that is not the identity is refused. The material is minted from generated `.mat.src` text by the tool's own minter, which now lives in the library. `KHR_materials_unlit` sets the unlit bit of `flags` (`renderer::kMaterialFlagUnlit`; the base colour factor and texture as usual, the material's other properties ignored, as the extension says); only an unlit material gets a `flags` parameter.
- **Specular-glossiness materials.** `KHR_materials_pbrSpecularGlossiness` (the older model; it wins over `pbrMetallicRoughness` in the same material) is converted to metallic-roughness when it is read, with the Khronos tools' formula (`asset_import/spec_gloss.h`): the metallic value solved from the perceived brightness of the diffuse and specular colours, the base colour blended from the colour each implies, the roughness one minus the glossiness. With textures the conversion is per texel: the material gets its own pair, `<prefix>/textures/<material>_base_color` (BC7, sRGB, the diffuse alpha as opacity) and `<material>_metal_rough` (BC7, linear, the glTF packing), as large as the larger of `diffuseTexture` and `specularGlossinessTexture` (a smaller one is resampled bilinearly, with a warning), and its factors are 1. Without a specular-glossiness texture and with a specular colour below the dielectric 4%, or with no texture at all, the conversion goes into the factors and a diffuse texture is used as it is. An unlit material gets the base colour only.
- **Collision** (glTF colliders, 2026-11-20; `asset_import/gltf_collision.h`). Read only when the import makes a level (`emit_level`), for the nodes under the default scene. A node whose name ends in `-col` is drawn and is a static triangle-mesh collider; `-colonly` is a collider only (no mesh reference, and its glTF mesh gets no render mesh unless another node draws it); `-convcol` and `-convcolonly` ask for a convex hull and get a triangle mesh with a warning. The suffix is case-sensitive and may be followed by Blender's duplicate number (`wall-col.001`). Each glTF mesh a collider uses becomes one `MCOL` at `<prefix>/colliders/<mesh name>` (manifest kind `mesh_collider`, keyed by the mesh index; shared by every node that uses the mesh), from its `TRIANGLES` primitives' base positions with the import's `scale` (other modes, morph targets and skins warn); a triangle with no area is dropped and counted, a mesh over the `MCOL` limits or with no triangle left gives no collider (a warning), a non-finite position or an index outside the vertices refuses the import. The node's entity gets a `static_collider` naming the `MCOL`. A marked node without a mesh warns; a world transform with a shear warns (it will get no body). All colliders of one import together hold at most `assets.import.max_collision_triangles` (default 4,194,304; `engine_import_options.max_collision_triangles`) triangles, or the import fails with `TooLarge`. The summary's `"collision"` member counts them. Without a level nothing is read and every mesh is made as before.
- **Collision from `OMI_physics_body` / `OMI_physics_shape`** (the OMI group's Stage 1 proposal, written by a Blender add-on, read by Godot). A node's `OMI_physics_body.collider.shape` names one of the document's `OMI_physics_shape.shapes`: `box` (`size`, default 1 m on each side), `sphere` (`radius`, default 0.5), `capsule` (`height` is the distance between the caps' centres, default 1; `radiusTop`/`radiusBottom`, default 0.5: different radii use the larger, with a warning) and `trimesh` (`mesh`: an `MCOL`, as for a `-col` node) become static colliders; `convex` is a triangle mesh with a warning; `cylinder` and unknown types give none (a warning). A node's own mesh stays drawn. A collider with a `motion` other than `static` on itself or above it is imported as static, with a warning; a `trigger` is not imported (a warning); an OMI collider wins over a collider name on the same node (a warning). The collider's `physicsMaterial` (in `OMI_physics_body.physicsMaterials`) gives `dynamicFriction` and `restitution` (out-of-range values are clamped, with a warning); without one the physics defaults apply. A shape index out of range, a size that is not above 0 or over 100,000 m, a `mesh` or `physicsMaterial` index out of range, or a collider in a file with no shapes refuses the import naming the JSON path. Both extensions may be in `extensionsRequired`.

**Making a solid world in Blender** (no add-on): name the objects that should be solid. A floor that is also seen: `floor-col`. An invisible wall or a
simplified collision mesh for a detailed statue: `statue-colonly` (keep it simple: every triangle is tested). Blender's duplicates (`floor-col.001`) keep
working. Export as glTF with object names (the default). Import with `emit_level`, and the summary's `"collision"` member says what became solid; the
warnings name what did not (a marked empty, a `LINES` primitive, a shear in a transform). **With the OMI add-on**, give objects an `OMI_physics_body`
collider with a box, sphere or capsule where a primitive fits better than triangles (crates, pillars). Only static colliders come in: moving bodies and
triggers are reported and left out.
- **Lights.** `KHR_lights_punctual`: a node that names a light gets a `scene::LightSource` on its entity (with its mesh, when it has one), so the level holds it; a program that loads such a level registers `scene::RegisterSceneComponents` (a level with an unknown component is refused). `directional`, `point` and `spot` map to `LightType::Directional`, `Point` and `Spot`; `color` (default white) is kept, `direction` is `+Z` in the entity's space (glTF lights shine along the node's `-Z`), and a spot's `innerConeAngle` and `outerConeAngle` (defaults 0 and pi/4) become `inner_cone` and `outer_cone`. glTF gives candela and lux, the engine a plain scale: a point or spot light's intensity is multiplied by `ImportOptions::light_intensity_scale` (1), a directional light's by `sun_intensity_scale` (1/10000). `range` (times `scale`) is the radius; without one, the radius is where `intensity / (d^2 + 1)` falls to `light_cutoff` (1/256). A light with an unknown type, a negative or non-finite intensity, a range not above 0 or cones not `0 <= inner < outer <= pi/2` is refused, naming the light, and so is a node that names a light the file does not have. The model viewer uses a file's lights, and its own sun only when the file has none.
- **The whole import, in order.** Textures, materials, skeletons, meshes, clips: every resource comes after the ones it names, so `ImportIntoBackend` can add the result in order. `ImportGltfAsync(scheduler, file, source, options)` runs `ImportGltf` as a job and returns an `ImportTicket`: `IsDone`, `Progress` (steps finished of five), `Cancel` (the import stops at its next step and ends `Cancelled`), `Finish` (the result); the destructor cancels and waits. Imports running at once share nothing.
- **The command.** `asset_mint import-gltf FILE --out=DIRECTORY [--manifest=FILE] [--prefix=/PATH] [--scale=X] [--clip-rate=HZ] [--flat-normals] [--drop-morph-targets] [--loop=NAME]...` reads the files the glTF names from its own folder, writes every blob under `DIRECTORY/<resource path>.blob` (ready for `asset_mint pack`) and the manifest next to the glTF (`FILE.import` unless `--manifest` says where), prints each warning and exits 1 on a refusal, naming the glTF object.
- **Limits are settings.** `RegisterImportSettings(registry)` declares `assets.import.max_file_bytes`, `max_json_bytes`, `max_buffer_bytes`, `max_nodes`, `max_accessors`, `max_images`, `max_pixels` and `max_output_bytes` (flags `--assets-import-*`, environment `ENGINE_ASSETS_IMPORT_*`), and `ImportLimitsFromSettings(registry)` reads them into the `ImportLimits` of an import. The light units are settings too: `assets.import.light_intensity_scale`, `sun_intensity_scale` and `light_cutoff`, read into an `ImportOptions` by `ApplyImportLightSettings(registry, &options)`.
- **The boundary.** The runtime never reaches the importer: the gate `asset_boundary_check` fails when a file under `engine/` (outside `engine/asset_import/`) includes `asset_import/...` or a tool's header or links `asset_import` or `asset_mint`, when a program under `game/` does so without an entry in `tools/asset_boundary_check/allowlist.txt` (with its reason), or when anything but `image_decode.cpp` includes a decoder header (`png.h`, `zlib.h`, `stb_image.h`).
- **The demo.** `asset_demo --import FILE [--async]` is a host's run-time import in one headless program: import from memory, add to a `HostMemoryBackend`, mount, load every mesh, skeleton and clip through the manager by path, by id and through the `AsyncLoader`, open the textures and materials as blobs, and fail when anything does not verify or a load converted data.

- **The viewer.** `model_viewer FILE.glb|FILE.gltf` shows a file in a window: the import with `ImportOptions::emit_level` (a level of the default scene), `ImportIntoBackend`, a manager with the CPU and GPU loaders, `scene::SpawnLevel`, then the transform system and the render sync into a `RenderScene` drawn by `SceneFrame`. Meshes loaded through the GPU loaders still need their upload barriers recorded once (`GpuMesh::RecordPrepare` in a submission, then `MarkReady`), as `game/model_viewer/viewer_scene.cpp` does after the spawn. A skinned entity (the level gives it a `SkeletonInstance`) plays a clip with an `animation::AnimationPlayer`, posed by `animation::AnimationSystem` over a `ManagerResolver` while the clips are held loaded. An orbit camera frames the render objects' bounds; the mouse orbits, zooms and strafes (`game/model_viewer/README.md`).

### Keeping an import's blobs (the host cache)

`ImportResult::summary` is UTF-8 JSON written by the level import (`asset_import/gltf_summary.h`): the level's nodes (the names `level_spawn` gives and their world positions times `scale`), the box of the meshes' `POSITION` ranges, the material names, the animation names with their durations, and the level's punctual lights (name, glTF type, the entity each is on, world position). It exists for a host that adds the blobs of an earlier import back from its own disk cache and has no other way to learn those facts (the C API reads none of them from a loaded level). The supported way to cache is: store, per resource, the id, the type, the path and the blob (`HostMemoryBackend::Open` / the C function `resource_read`), plus the level's id and the summary; key by the file's hash and by the format fingerprint (`engine_api.import_format_version`: `kGltfImporterOutputVersion`, `kContainerVersion` and every type's version); add them back with `HostMemoryBackend::AddMany` (the C function `resource_add_many`), which adds all or none. **Change what the importer writes and you change `kGltfImporterOutputVersion`** (a fixed bug, a new field, other ids or paths): that is what invalidates the caches of imports that are valid but different. See the engine-library guide ("Keeping what an import made").

## Adding a resource type or a backend

A **resource type** is a four-character tag registered with `TypeRegistry::Register(type, version, name)`; its blob is written with `BlobWriter` (sections from 16, dependencies for every id the blob names) and read in place by a view that opens it with `BlobReader::Open` and keeps spans into the bytes, as `MeshView` and `SkeletonView` do. It gets a loader (`ResourceManager::RegisterLoader<T>` with `ResourceTraits<T>`), a minter (a text source form or a library function that builds the blob deterministically, with a test that mints twice and compares the bytes) and an upgrade chain when its version changes (`UpgradeChains`). A **backend** implements `ResourceBackend` (`ReadTable`, `Read`, `ReadRange`, the tickets) and passes `backend_conformance.h`; the host memory backend, the loose-file backend, the pak and the network backend are the four examples.

## Security notes

A blob, a pak, a table, a protocol message or a glTF file may come from the network: every reader checks every count and offset before it allocates or reads (`tests/asset/hostile_blob_test.cpp` mutates and truncates every blob type, the importer's tests mutate files and images, all under AddressSanitizer), the importer reads external files only through a `GltfSource` with clean relative names, and the decoders fail by return value. There is no encryption: a transport for trusted networks only (`KNOWN-ISSUES.md`).
