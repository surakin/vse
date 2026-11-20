#pragma once

//! The glTF importer: glTF 2.0 files become the engine's resources, in memory.
//!
//! Phase 22 Days 335 to 338. Bytes go in, minted blobs come out; [[asset::ImportIntoBackend]] adds them to a
//! [[asset::HostMemoryBackend]] that the rest of the engine reads like any other backend. This is a library a program
//! opts into: no runtime engine module depends on it (the gate `asset_boundary_check`), and `asset_mint import-gltf` is
//! a thin command over it.
//!
//! The input may be hostile (a file fetched from a room): every failure is a [[asset::Status]] and a
//! [[asset::GltfError]] that names the glTF object, nothing aborts, and every count and size is checked against
//! [[asset::ImportLimits]] before anything is allocated. External files (`.bin` and image URIs) are read through a
//! [[asset::GltfSource]], so the host decides where bytes come from.
//!
//! Built so far (Day 335): the container (`.gltf`, `.glb`, base64 `data:` URIs), the accessors, the limits, the id
//! sources and the import manifest, and `ImportIntoBackend`. [[asset::ImportGltf]] reads and checks the container and
//! reports what it holds; it imports resources from Day 336 on (meshes), Day 337 (skeletons and animation) and Day 338
//! (images, textures, materials).

#include "asset/memory_backend.h"
#include "asset/resource_id.h"
#include "asset/resource_type.h"
#include "asset/status.h"
#include "asset_import/gltf_error.h"
#include "containers/span.h"
#include "containers/vector.h"
#include "mem/heap_allocator.h"
#include "string/string.h"
#include "string_view.h"
#include "sync/atomic.h"
#include "types.h"

namespace vse::asset
{

/// The version of what the glTF importer writes: raised by every change that can make a re-import of the same file
/// give other blobs (a fixed bug, a new field, other ids or paths, a changed default), and never otherwise. A host that
/// keeps imported blobs (a disk cache) keys them by this and by the engine's format fingerprint
/// (`engine_api.import_format_version`, which also folds in the blob container and type versions). 2: glTF coverage
/// (PBR maps, colours, morphs, lights); 3: skins in the space of the node that uses them (2026-11-20); 4:
/// `KHR_materials_pbrSpecularGlossiness` converted to metallic-roughness, no longer ignored when only used
/// (2026-11-20); 5: collision (glTF colliders: `MCOL`s,
/// `static_collider`, `-colonly` meshes not drawn; 2026-11-20).
inline constexpr u32 kGltfImporterOutputVersion = 5;

/// The steps an import goes through (container, textures and materials, skeletons, meshes, clips);
/// `ImportOptions::steps_done` counts them.
inline constexpr u32 kImportSteps = 5;

/// The bounds an import is held to.
///
/// Every count and size of a file is checked against them before memory is allocated for
/// it (they become settings, `assets.import.*`, on Day 339).
///
/// - Thread-safety: thread-compatible plain data.
struct ImportLimits
{
    u64 max_file_bytes = u64{1} << 30;         ///< The `.gltf` or `.glb` file.
    u64 max_json_bytes = u64{64} << 20;        ///< The JSON text (the parse memory is a multiple of it).
    u64 max_buffer_bytes = u64{1} << 30;       ///< All buffers together (external, `data:` and the `.glb` chunk).
    u32 max_buffers = 1024;                    ///< `buffers`.
    u32 max_buffer_views = 200000;             ///< `bufferViews`.
    u32 max_accessors = 200000;                ///< `accessors`.
    u64 max_accessor_elements = u64{1} << 26;  ///< The `count` of one accessor (an accessor with no buffer view is all
                                               ///< zeros: its size is not bounded by a buffer).
    u32 max_meshes = 20000;                    ///< `meshes`.
    u32 max_primitives_per_mesh = 1024;        ///< `primitives` of one mesh.
    u32 max_nodes = 200000;                    ///< `nodes`.
    u32 max_images = 4096;                     ///< `images`.
    u32 max_textures = 4096;                   ///< `textures`.
    u32 max_materials = 20000;                 ///< `materials`.
    u32 max_skins = 4096;                      ///< `skins`.
    u32 max_animations = 4096;                 ///< `animations`.
    u64 max_pixels = u64{1} << 26;             ///< The pixels of one decoded image (Day 338).
    u64 max_output_bytes = u64{2} << 30;       ///< All blobs of one import.
    u64 max_collision_triangles = u64{1} << 22;  ///< The triangles of every collider of one import together (glTF
                                                 ///< colliders Day 3).
};

/// Reads the files a glTF names by URI.
///
/// The `.bin` of a `.gltf`, the images. The tool's implementation reads files inside
/// the glTF's directory; a host's reads from wherever it fetched the glTF (a room's media, a cache).
///
/// - Thread-safety: thread-compatible; one import calls it from one thread at a time.
class GltfSource
{
public:
    virtual ~GltfSource() = default;

    /// Reads one file.
    ///
    /// - Param name: a clean relative name: segments joined by `/`, no `..`, no empty segment, no scheme,
    /// percent-escapes
    ///   decoded (the importer refuses anything else before calling).
    /// - Param max_bytes: the most bytes the importer will use: a longer file is read as its first `max_bytes` (never
    /// more
    ///   than this is allocated).
    /// - Param out: replaced by the bytes.
    /// - Fails: `NotFound` (there is no such file), or what the host reports (`Offline`, `Io`).
    /// - Ownership: borrows its arguments for the call only; nothing is retained.
    virtual Status Read(StringView name, u64 max_bytes, Vector<byte>* out) = 0;
};

/// A source for a file that has none: every read fails with `NotFound` (a `.glb` whose buffers and images are inside
/// it).
/// - Thread-safety: thread-safe.
class NoGltfSource final : public GltfSource
{
public:
    /// Always `NotFound`.
    /// - Ownership: borrows its arguments for the call only; nothing is retained.
    Status Read(StringView, u64, Vector<byte>* out) override;
};

/// Where the ids of new resources come from.
///
/// - Thread-safety: thread-compatible.
class IdSource
{
public:
    virtual ~IdSource() = default;
    /// The next id: never the null id.
    virtual ResourceId Next() = 0;
};

/// Ids from the operating system's random source ([[asset::GenerateResourceId]]): what a program uses.
/// - Thread-safety: thread-safe.
class RandomIdSource final : public IdSource
{
public:
    /// A fresh random id.
    ResourceId Next() override;
};

/// Ids from a seeded generator: the same seed gives the same ids (tests, and imports that must be byte-identical).
///
/// - Thread-safety: thread-compatible.
class SeededIdSource final : public IdSource
{
public:
    /// Starts the sequence of `seed`.
    explicit SeededIdSource(u64 seed) : state_(seed) {}
    /// The next id of the sequence.
    ResourceId Next() override;

private:
    u64 state_;
};

/// The kinds of resource an import makes.
enum class ImportKind : u8
{
    Image,         ///< A decoded glTF image (Day 338).
    Texture,       ///< A glTF texture (Day 338).
    Material,      ///< A glTF material (Day 338).
    Skeleton,      ///< A glTF skin (Day 337).
    Mesh,          ///< A glTF mesh (Day 336).
    Clip,          ///< A glTF animation (Day 337).
    Level,         ///< The default scene (Day 338).
    MeshCollider,  ///< The `MCOL` of a glTF mesh a collider uses, keyed by the mesh index (glTF colliders Day 3).
};

/// The name of a kind (`mesh`, `texture`, ...), as the manifest text writes it.
///
/// - Ownership: the string is static.
/// - Fails: never.
const char* ImportKindName(ImportKind kind);

/// One imported object and the id it was given.
///
/// - Thread-safety: thread-compatible.
/// - Ownership: owns its name.
struct ImportManifestEntry
{
    /// An empty entry.
    ImportManifestEntry() : name(HeapAllocator::Default()) {}
    ImportKind kind = ImportKind::Mesh;  ///< What it is.
    u32 index = 0;                       ///< Its index in the glTF's array of that kind.
    String<HeapAllocator> name;          ///< Its name in the glTF (may be empty).
    ResourceId id;                       ///< The id the engine resource has.
};

/// An object of a new import that wants the id an earlier import gave it.
///
/// - Thread-safety: thread-compatible plain data.
struct ManifestObject
{
    u32 index = 0;    ///< Its index in the glTF's array of its kind.
    StringView name;  ///< Its name in the glTF (may be empty); borrowed.
};

/// The ids an import gave, kept by the caller for the next import.
///
/// The tool writes it next to the glTF file, a host keeps it or not, (the tool writes it next to the glTF file, a host
/// keeps it or not) so that a re-import of the same file, even with its objects reordered or renamed, keeps them: an
/// object matches by kind and name when the name is unique among that kind in the manifest, else by kind and index.
///
/// - Thread-safety: thread-compatible.
/// - Ownership: owns its entries.
class ImportManifest
{
public:
    /// Finds the ids an earlier import gave a set of objects of one kind.
    ///
    /// First by name (an object whose name is the
    /// name of exactly one entry, and no other object has it), then, for what is left, by index (the entry at that
    /// index, when its name is the object's or is the name of none of the objects: the object was renamed). No two
    /// objects get one id. The matching is made for all the objects at once, so that the result does not depend on
    /// their order.
    ///
    /// - Param kind: the kind of every object.
    /// - Param objects: the objects of the new import: their index in the glTF and their name (may be empty).
    /// - Param ids: replaced by one id per object, in order; the null id where no entry matches (the caller draws a new
    /// id).
    /// - Ownership: borrows its arguments for the call only; nothing is retained.
    void Match(ImportKind kind, Span<const ManifestObject> objects, Vector<ResourceId>* ids) const;

    /// Adds an entry.
    void Add(ImportKind kind, u32 index, StringView name, ResourceId id);

    /// The entries, in the order they were added.
    const Vector<ImportManifestEntry>& Entries() const { return entries_; }

    /// The text form: one line per entry, `kind index id name` with the name percent-escaped, after a `gltf-import 1`
    /// line.
    ///
    /// - Param out: replaced by the text.
    /// - Ownership: borrows its arguments for the call only; nothing is retained.
    void Format(String<HeapAllocator>* out) const;

    /// Reads the text form.
    ///
    /// - Param text: what `Format` wrote.
    /// - Fails: `Invalid` with `error` naming the line (a wrong version, an unknown kind, a bad id, a repeated id).
    /// - Ownership: borrows its arguments for the call only; nothing is retained.
    Status Parse(StringView text, GltfError* error);

private:
    Vector<ImportManifestEntry> entries_;
};

/// What an import was asked.
///
/// - Thread-safety: thread-compatible.
/// - Ownership: borrows the pointers, which must outlive the call.
struct ImportOptions
{
    ImportLimits limits;                       ///< The bounds.
    IdSource* ids = nullptr;                   ///< Where new ids come from (null: a `RandomIdSource`).
    const ImportManifest* manifest = nullptr;  ///< The manifest of an earlier import of this file, or null.
    StringView path_prefix;                    ///< The resource paths of the result start with this (default: `/gltf`).
    bool flat_normals = false;        ///< A mesh without normals gets flat ones (default: smooth, area-weighted).
    bool drop_morph_targets = false;  ///< Import the base shape of a primitive with morph targets (a warning).
    f32 clip_rate = 30.0f;            ///< The rate cubic-spline channels are resampled at, and the clips' rate hint.
    Span<const StringView> looping_clips;  ///< Animations (or clips, `<animation>_<skin>`) with these names loop.
    const Atomic<bool>*
        cancel = nullptr;  ///< When set to true the import stops between steps and fails `Cancelled` (null: never).
    Atomic<u32>* steps_done = nullptr;  ///< Receives the number of the import's steps finished (of `kImportSteps`), for
                                        ///< progress.
    f32 scale = 1.0f;                   ///< Positions are multiplied by it (glTF is in metres).
    /// `KHR_lights_punctual` point and spot intensities (candela) are multiplied by it (glTF coverage Day 12;
    /// `assets.import.light_intensity_scale`).
    f32 light_intensity_scale = 1.0f;
    /// Directional intensities (lux) are multiplied by it (`assets.import.sun_intensity_scale`: 10,000 lux of
    /// daylight becomes 1).
    f32 sun_intensity_scale = 1e-4f;
    /// A point or spot light without a `range` reaches as far as its scaled intensity over `d^2 + 1` stays at or above
    /// this (`assets.import.light_cutoff`).
    f32 light_cutoff = 1.0f / 256.0f;
    bool
        emit_level = false;  ///< Also make a level of the default scene's node tree ([[asset::ImportLevel]], Phase 24).
};

/// One resource an import made.
///
/// - Thread-safety: thread-compatible.
/// - Ownership: owns its bytes and path.
struct ImportedResource
{
    /// An empty resource.
    ImportedResource() : path(HeapAllocator::Default()) {}
    ResourceId id;               ///< Its id.
    ResourceType type;           ///< Its type.
    String<HeapAllocator> path;  ///< The resource path it is added under.
    Vector<byte> blob;           ///< The blob.
};

/// What an import found and did.
///
/// - Thread-safety: thread-compatible plain data.
struct ImportReport
{
    u32 meshes = 0;                        ///< `meshes` in the file.
    u32 materials = 0;                     ///< `materials`.
    u32 textures = 0;                      ///< `textures`.
    u32 images = 0;                        ///< `images`.
    u32 skins = 0;                         ///< `skins`.
    u32 animations = 0;                    ///< `animations`.
    u32 nodes = 0;                         ///< `nodes`.
    u32 accessors = 0;                     ///< `accessors`.
    f32 resampling_translation_error = 0;  ///< The largest error resampling a cubic-spline translation or scale channel
                                           ///< made.
    f32 resampling_rotation_degrees = 0;   ///< The largest error (degrees) resampling a cubic-spline rotation channel
                                           ///< made.
    u64 output_bytes = 0;  ///< The bytes of the blobs made so far (checked against `ImportLimits::max_output_bytes`).
    u32 colliders = 0;     ///< Level nodes that got a static collider (glTF colliders Day 3).
    u32 collision_meshes = 0;             ///< `MCOL` resources made.
    u64 collision_triangles = 0;          ///< Their triangles together.
    u32 collision_primitives = 0;         ///< Colliders that are a box, a sphere or a capsule.
    u32 collision_dropped = 0;            ///< Nodes marked as colliders that got none (each with a warning).
    u32 warnings = 0;                     ///< Features dropped or approximated, each described in `notes`.
    Vector<String<HeapAllocator>> notes;  ///< One line per warning, naming the glTF object.
};

/// The result of an import.
///
/// - Thread-safety: thread-compatible.
/// - Ownership: owns everything in it.
struct ImportResult
{
    Vector<ImportedResource> resources;  ///< The resources made, dependencies before the resources that name them.
    ImportManifest manifest;             ///< The ids of this import, to keep for the next one.
    ImportReport report;                 ///< The counts and warnings.
    String<HeapAllocator> summary{HeapAllocator::Default()};  ///< UTF-8 JSON facts about the file (gltf_summary.h).
};

/// Imports a glTF file.
///
/// Day 335: reads and checks the container (the JSON, the buffers, the accessors' ranges), fills `result->report` with
/// what the file holds, and fails with `Unsupported` because no resource kind is imported yet; Days 336 to 338 import
/// meshes, skeletons and clips, and images, textures and materials.
///
/// - Param file: the `.gltf` or `.glb` bytes.
/// - Param source: reads the external files the glTF names (use [[asset::NoGltfSource]] for a `.glb`).
/// - Param options: limits, id source, manifest.
/// - Param result: receives the resources, the manifest and the report (replaced).
/// - Param error: receives the sentence naming the glTF object a refusal is about.
/// - Fails: `Invalid` (the file is not a glTF 2.0 or breaks a rule), `TooLarge` (above a limit), `Unsupported` (a
/// feature
///   the importer does not import, naming it), `NotFound` (a named file the source does not have), `LimitExceeded`
///   (memory).
/// - Thread-safety: thread-safe for separate calls with separate arguments.
/// - Ownership: borrows its arguments for the call only; `*result` owns what it holds.
Status ImportGltf(ByteSpan file, GltfSource& source, const ImportOptions& options, ImportResult* result,
                  GltfError* error);

/// Adds every resource of an import to a host memory backend under its path, atomically: when one is refused, the ones
/// already added are removed again.
///
/// - Param backend: the backend to add to.
/// - Param result: an import's result.
/// - Fails: what [[asset::HostMemoryBackend::Add]] reports (`DuplicateId`, `LimitExceeded`, `Corrupt`).
/// - Ownership: the backend copies the bytes; `result` is not changed.
Status ImportIntoBackend(HostMemoryBackend& backend, const ImportResult& result);

}  // namespace vse::asset
