#pragma once

//! The project side of `asset_mint` (Phase 22 Day 333): the project file (`assets.project`), the discovery of sources,
//! the dependency order, incremental and parallel minting, packing, verifying, listing and checking. An offline tool's
//! code: never linked into the engine; its tests compile it directly.
//!
//! The project file is line-oriented text; `#` starts a comment; fields are separated by blanks:
//!
//!     source_root src            # where the sources are (relative to the project file)
//!     output_root out            # where the blobs, resources.table, assets.manifest and the pak go
//!     pak game.pak               # the pak's file name inside output_root
//!     target 0                   # the target tag the blobs and the pak are made for
//!     exclude drafts             # a path prefix (below source_root) that is not part of the project
//!     rule .tex.src skip         # an option for one source extension (`skip`: its sources are left out)
//!
//! A source's resource path is its path below `source_root` without its extensions (`meshes/rock.mesh.src` is
//! `/meshes/rock`); its blob is `output_root/meshes/rock.mesh.blob`. A reference in a source names a resource by that
//! path (absolute, or relative to the source's folder) or by `@` and its sixteen hexadecimal digits.

#include "asset/content_hash.h"
#include "asset/resource_id.h"
#include "asset/resource_type.h"
#include "asset/status.h"
#include "target_transform.h"
#include "upgrade.h"
#include "containers/array.h"
#include "containers/unique_ptr.h"
#include "containers/vector.h"
#include "io/path.h"
#include "mem/heap_allocator.h"
#include "string/string.h"
#include "string_view.h"
#include "types.h"

namespace engine_placeholder::asset
{

/// The version of the tool as the manifest records it: a change re-mints every blob.
inline constexpr u32 kMintToolVersion = 1;

/// The messages a command collects (every error of every file, not only the first).
///
/// - Thread-safety: thread-compatible; the project calls it from one thread.
class MintDiagnostics
{
public:
    /// Adds an error; `line` 0 means the file as a whole.
    void Error(StringView file, u32 line, StringView message);
    /// Adds a note (printed with `--verbose` only).
    void Note(StringView text);
    /// The number of errors.
    usize ErrorCount() const { return errors_; }
    /// All messages in the order they were added, one per entry, errors first marked `error:`.
    const Vector<String<HeapAllocator>>& Lines() const { return lines_; }
    /// True when some entry mentions `text` (for tests).
    bool Mentions(StringView text) const;

private:
    Vector<String<HeapAllocator>> lines_;
    usize errors_ = 0;
};

/// The parsed project file.
///
/// - Thread-safety: thread-compatible plain data.
struct ProjectConfig
{
    ProjectConfig()
        : source_root(HeapAllocator::Default()), output_root(HeapAllocator::Default()), pak(HeapAllocator::Default())
    {
    }
    String<HeapAllocator> source_root;
    String<HeapAllocator> output_root;
    String<HeapAllocator> pak;
    u32 target = 0;
    Vector<String<HeapAllocator>> excludes;
    Vector<String<HeapAllocator>> skipped_extensions;
};

/// Where and why the project file was refused.
///
/// - Thread-safety: immutable value.
struct ProjectError
{
    u32 line = 0;
    Array<char, 160> message = {};
};

/// Parses a project file.
///
/// - Fails: `Invalid` with `error` (the line): an unknown record, a wrong field count, a repeated record, a missing
///   `source_root` or `output_root`, a path that is absolute or climbs out with `..`, a number that is not one.
/// - Ownership: borrows `text` for the call; `*out` and `*error` are written.
Status ParseProjectConfig(StringView text, ProjectConfig* out, ProjectError* error);

/// The kind of a source file.
enum class SourceKind : u8
{
    Skeleton,  ///< `.skel.src`
    Mesh,      ///< `.mesh.src`
    Clip,      ///< `.anim.src`
    Texture,   ///< `.tex.src`
    Material,  ///< `.mat.src`
    Level,     ///< `.level.src`
};

/// The name a kind goes by in messages and listings.
const char* SourceKindName(SourceKind kind);

/// What a mint did.
///
/// - Thread-safety: immutable value.
struct MintStats
{
    u32 sources = 0;    ///< Sources of the project.
    u32 minted = 0;     ///< Sources whose minter ran.
    u32 unchanged = 0;  ///< Sources whose inputs matched the manifest (nothing ran).
    u32 rewritten = 0;  ///< Blob files written (a minted blob equal to the file on disk is not written again).
    u32 failed = 0;     ///< Sources that failed or were skipped because something they need failed.
};

/// What a mint is told.
struct MintOptions
{
    bool force = false;  ///< Mint everything again (the files are still not rewritten when equal).
    u32 workers = 0;     ///< Participants of the job system (0: the default).
};

/// What `pack` is asked for.
struct PackOptions
{
    bool has_target = false;  ///< Pack for `target` instead of the project's own target.
    u32 target = 0;           ///< The target tag of the pak.
};

/// What `verify` is asked for.
struct VerifyOptions
{
    bool require_current = false;  ///< An old-version blob is an error (it is named, with the fix); otherwise it is
                                   ///< only checked structurally.
    bool has_target = false;       ///< Check the pak packed for `target` (the pak of that target's name).
    u32 target = 0;                ///< The target tag.
};

/// A project on disk.
///
/// - Thread-safety: thread-compatible.
/// - Ownership: owns the sources it discovered and the blobs it holds in memory.
class Project
{
public:
    struct Source;  // internal
    struct Impl;    // internal

    /// Opens the project file and discovers its sources: reads every id, finds the references between them and reports
    /// a source with no id (the message names `asset_mint adopt`), two sources with one id or one path, and a cycle.
    ///
    /// - Param project_file: the path of `assets.project`.
    /// - Param diagnostics: receives every problem.
    /// - Fails: `Invalid` (the project file, or any source problem), `NotFound`, `Io`.
    /// - Ownership: borrows `diagnostics` for the call.
    Status Load(const io::Path& project_file, MintDiagnostics* diagnostics);
    Project();
    ~Project();
    Project(const Project&) = delete;
    Project& operator=(const Project&) = delete;

    /// True once the project file was read and the sources were discovered (their problems are not an error here: they
    /// were reported by `Load`).
    bool IsLoaded() const;
    /// The number of sources found.
    usize SourceCount() const;
    /// The configuration read from the file.
    const ProjectConfig& Config() const { return config_; }
    /// The directory the blobs are written to.
    const io::Path& OutputRoot() const { return output_root_; }

    /// Writes a fresh `id` line at the top of every source that has none (the only command that writes an id).
    ///
    /// - Param adopted: receives the number of sources changed.
    /// - Fails: `Io`.
    Status Adopt(MintDiagnostics* diagnostics, u32* adopted);

    /// Mints every source whose inputs changed, in dependency order and in parallel where the order allows, then
    /// writes `resources.table` and `assets.manifest`. Every error is reported; a source that needs a failed one is
    /// counted failed too.
    ///
    /// - Fails: `Invalid` when any source failed.
    Status Mint(const MintOptions& options, MintDiagnostics* diagnostics, MintStats* stats);

    /// Packs the blobs under the output root into the pak (`pak` of the project file).
    ///
    /// - Fails: the failures of the pak writer (an incomplete closure lists each missing pair), `Invalid` for a blob
    ///   that is not the current version or whose hash does not match.
    Status Pack(MintDiagnostics* diagnostics, const PackOptions& options = PackOptions());

    /// Checks every blob under the output root (header, hash, current version, the closure complete and typed), the
    /// table against the blobs, and the pak when there is one.
    ///
    /// - Fails: `Invalid` when anything is wrong (every problem is reported).
    Status Verify(MintDiagnostics* diagnostics, const VerifyOptions& options = VerifyOptions());

    /// Prints what the asset references and what references it (sources, by path and id).
    ///
    /// - Param asset: a resource path (`/meshes/rock`), an id (`0000000000000b02`) or a source path below the source
    /// root.
    /// - Fails: `NotFound`.
    Status Refs(StringView asset, MintDiagnostics* diagnostics, String<HeapAllocator>* out);

    /// Changes the name of a source (its file stem; the folder stays) and rewrites the references to it in the other
    /// sources; the id does not change, so no blob of a dependent changes.
    ///
    /// - Param asset: as for `Refs`.
    /// - Param new_name: the new name without folder or extension.
    /// - Fails: `NotFound`, `DuplicateId` (the target path is taken), `Invalid`, `Io`.
    Status Rename(StringView asset, StringView new_name, MintDiagnostics* diagnostics);

    /// Moves a source to another folder below the source root (its name stays) and rewrites the references to it.
    ///
    /// - Param new_folder: a path below the source root (`props/stone`).
    /// - Fails: as `Rename`.
    Status Move(StringView asset, StringView new_folder, MintDiagnostics* diagnostics);

    /// Removes a source, refused while another source references it (the referrers are named and nothing changes).
    ///
    /// - Param with_dependents: list everything that would have to go first (the referrers, and theirs); nothing is
    ///   removed in either case while a reference exists.
    /// - Fails: `Invalid` (referenced), `NotFound`, `Io`.
    Status Delete(StringView asset, bool with_dependents, MintDiagnostics* diagnostics);

    /// Copies a source to a new resource path with a fresh id.
    ///
    /// - Param new_path: the resource path of the copy (`/props/rock2`).
    /// - Fails: `NotFound`, `DuplicateId` (the new path is taken), `Invalid`, `Io`.
    Status Copy(StringView asset, StringView new_path, MintDiagnostics* diagnostics);

    /// Gives a source a fresh id (the fix for two sources that share one). A source other sources reference by id is
    /// refused unless `update_references` rewrites those references or the id is shared.
    ///
    /// - Param asset: the source's path below the source root when the id is shared, else as for `Refs`.
    /// - Fails: `NotFound`, `Invalid`, `Io`.
    Status Reid(StringView asset, bool update_references, MintDiagnostics* diagnostics);

    /// Prints a minted level or material back as source text (from its blob in the output).
    ///
    /// - Fails: `NotFound`, `Invalid` (another kind of resource), and the formatter's failures.
    Status Unmint(StringView asset, MintDiagnostics* diagnostics, String<HeapAllocator>* out);

    /// Checks the sources without minting: ids, paths, references and their kinds, cycles.
    ///
    /// - Fails: `Invalid`.
    Status Check(MintDiagnostics* diagnostics);

    /// Prints the resources of the output root (id, type, size, path), or with `skeletons` each skeleton's joint count,
    /// hash and the meshes and clips that use it.
    ///
    /// - Param out: receives the text.
    Status List(bool skeletons, MintDiagnostics* diagnostics, String<HeapAllocator>* out);

private:
    ProjectConfig config_;
    io::Path project_file_;
    io::Path source_root_;
    io::Path output_root_;
    UniquePtr<Impl> impl_;
};

/// What `PackDirectory` is asked for.
struct PackRequest
{
    u32 blob_target = 0;                           ///< The target the blobs of the directory are laid out for.
    u32 pak_target = 0;                            ///< The target of the pak; blobs are transformed when it differs.
    bool names = true;                             ///< Include the resource paths in the pak.
    const TargetTransforms* transforms = nullptr;  ///< The transforms (null: the built-in set).
    bool (*source_of)(void* user, ResourceId id,
                      Vector<byte>* text) = nullptr;  ///< Source text for a transform, or null.
    void* source_user = nullptr;                      ///< Passed to `source_of`.
};

/// Packs the blobs of a directory into a pak for `request.pak_target`, transforming each blob that is laid out for
/// another target (`TransformBlob`) and checking the result.
///
/// - Fails: see `Project::Pack`; `Invalid` when a transform breaks a rule.
Status PackDirectory(const io::Path& directory, const io::Path& output, const PackRequest& request,
                     MintDiagnostics* diagnostics);

/// Checks the blobs, the closure, the table and the pak of an output folder (what `Project::Verify` does).
///
/// - Param pak_path: the pak to check, or an empty path for none.
/// - Param pak_target: the target the pak must have been packed for.
/// - Param types: the resource types (tests pass their own).
/// - Fails: `Invalid` when anything is wrong (every problem is reported).
Status VerifyDirectory(const io::Path& root, const io::Path& pak_path, u32 pak_target, const TypeRegistry& types,
                       const VerifyOptions& options, MintDiagnostics* diagnostics);

/// What `RemintDirectory` did.
///
/// - Thread-safety: immutable value.
struct RemintStats
{
    u32 blobs = 0;     ///< Blobs found.
    u32 current = 0;   ///< Of those, already the current version.
    u32 upgraded = 0;  ///< Rewritten to the current version.
    u32 failed = 0;    ///< Left alone: no step, or a step failed.
};

/// Rewrites every old-version blob under `root` to the current version through the upgrade chains, in place (a file is
/// replaced whole, never partly written). The result is checked: it opens at the current version and its hash holds.
///
/// - Fails: `Invalid` when some blob could not be upgraded (each is named).
Status RemintDirectory(const io::Path& root, const TypeRegistry& types, const UpgradeChains& chains,
                       MintDiagnostics* diagnostics, RemintStats* stats);

/// Packs the blobs of a directory into a pak: the engine of `asset_mint pack DIRECTORY OUTPUT.pak`.
///
/// - Param names: include the resource paths in the pak.
/// - Fails: see `Project::Pack`.
Status PackDirectory(const io::Path& directory, const io::Path& output, u32 target, bool names,
                     MintDiagnostics* diagnostics);

}  // namespace engine_placeholder::asset
