#pragma once

//! [[asset::PakBackend]]: a pak file served through [[asset::ResourceBackend]] (read-only).

#include "asset/backend.h"
#include "asset/pak.h"
#include "containers/unique_ptr.h"
#include "io/path.h"

namespace vse::asset
{

/// Serves the entries of one pak by id.
///
/// A patch pak names its base pak by identity (the base's index hash); `Stat`, `Open` and `ReadTable` of a patch pak
/// fail with `MissingDependency` until the base's backend is linked with `LinkBase`. `Watch` reports nothing.
/// - Thread-safety: thread-safe once opened; `LinkBase` belongs to setup, before the first read.
/// - Ownership: owns the reader; readers it hands out must not outlive it.
class PakBackend : public ResourceBackend
{
public:
    /// Opens the pak at `path` for a program of target `target`.
    ///
    /// - Fails: as [[asset::PakReader::Open]], `NotFound` for a missing file.
    /// - Ownership: `*out` receives the backend; the caller owns it.
    static Status Open(const io::Path& path, const TypeRegistry& types, u32 target, UniquePtr<PakBackend>* out);

    /// As `Open`, over a file that is already open.
    ///
    /// - Ownership: takes `file`; `*out` receives the backend.
    static Status FromFile(io::FilePtr file, const TypeRegistry& types, u32 target, UniquePtr<PakBackend>* out);

    /// Wraps an opened reader; use `Open` or `FromFile`.
    PakBackend(PakReader reader, const TypeRegistry& types);

    /// Says which backend holds the base pak of a patch pak.
    ///
    /// - Ownership: borrows `base`, which must outlive this backend.
    void LinkBase(const PakBackend* base) { base_ = base; }

    /// What the pak says about itself.
    const PakInfo& Info() const { return reader_.Info(); }

    /// The underlying reader (for tools).
    const PakReader& Reader() const { return reader_; }

    /// A short static name.
    ///
    /// - Fails: never.
    /// - Ownership: returns a static string; the caller does not free it.
    const char* Name() const override { return "pak"; }
    /// Ranged reads, and a stored table when the pak holds its names.
    u32 Capabilities() const override
    {
        return kBackendRanged | (reader_.Info().has_names ? u32(kBackendStoredTable) : 0u);
    }
    /// Describes an entry from the index.
    ///
    /// - Fails: `NotFound`; `MissingDependency` (a patch pak without its base).
    /// - Ownership: `*out` is written on success only.
    Status Stat(ResourceId id, ResourceInfo* out) override;
    /// Opens an entry.
    ///
    /// - Fails: `NotFound`; `MissingDependency`; `Corrupt` (the blob's own id differs from the index's); `Io`.
    /// - Ownership: `*out` receives the reader on success; the caller owns it.
    Status Open(ResourceId id, UniquePtr<ResourceReader>* out) override;
    /// Reads the names section (an empty table when the pak has none: it then reports ids only).
    ///
    /// - Fails: `MissingDependency`; `Corrupt`; `Io`.
    /// - Ownership: adds to `*out`, which must be empty.
    Status ReadTable(ResourceTable* out) override;

private:
    Status CheckBase() const;

    PakReader reader_;
    const TypeRegistry* types_;
    const PakBackend* base_ = nullptr;
};

}  // namespace vse::asset
