#pragma once

//! The settings file store: a human-edited text file that keeps what the human wrote.

#include "io/path.h"
#include "settings/config_store.h"
#include "settings/config_text.h"

namespace vse::settings
{

/// Options of a [[settings::FileConfigStore]].
struct FileStoreOptions
{
    /// Permission bits of a file the store creates (`0600` for a user's file, `0644` for a project's).
    u32 permissions = 0600;
    /// Create the file's missing parent directories on `Commit`.
    bool create_directories = false;
    /// Copy the previous content to `<name>.bak` before each `Commit` replaces it.
    bool keep_backup = true;
    /// The largest file `Load` reads, in bytes; a larger one is `TooLong`.
    u64 max_bytes = u64{8} << 20;
    /// Never write: `Set`, `Remove` and `Commit` say `ReadOnly` (a layer of shipped defaults or a project file the
    /// program must not change).
    bool read_only = false;
};

/// A [[settings::ConfigStore]] over one text file (see [[settings::ConfigDocument]] for the format).
///
/// The store reads the file whole on `Load` (a missing file is an empty store). `Set` and `Remove` edit the in-memory
/// document, so a change rewrites only the bytes it touches. `Commit` writes the document with `io::WriteFileAtomic`
/// after copying the previous file to `.bak`.
///
/// **A file that cannot be read is never overwritten by accident.** If `Load` finds lines it cannot understand, the
/// store is *damaged*: the lines it could read are available, the rest is kept verbatim, and `Commit` refuses with
/// `Damaged` unless `CommitOptions::replace_damaged` is set (the damaged text is then in the `.bak` file).
///
/// If a `Load` could not read the file at all (permission, size cap, I/O), `Commit` refuses with that error until a
/// `Load` succeeds.
///
/// Two stores over one file do not merge: the last `Commit` wins as a whole, and a reader never sees a torn file.
///
/// - Thread-safety: thread-compatible.
/// - Ownership: owns its document; the file belongs to the file system.
class FileConfigStore final : public ConfigStore
{
public:
    /// Makes a store over `path`; nothing is read until `Load`.
    ///
    /// - Param path: the settings file.
    /// - Param options: see [[settings::FileStoreOptions]].
    explicit FileConfigStore(const io::Path& path, const FileStoreOptions& options = FileStoreOptions());

    u32 Capabilities() const override;
    void Describe(char* out, usize capacity) const override;
    Status Load() override;
    Status Get(StringView key, StoredValue* out) override;
    Status Set(StringView key, const StoredValue& value) override;
    Status Remove(StringView key) override;
    Status Enumerate(StringView prefix, Vector<Key>* out) override;
    Status Commit(const CommitOptions& options) override;
    void Origin(StringView key, char* out, usize capacity) const override;
    bool IsPresent() const override { return exists_; }

    /// Returns the problems the last `Load` found (the errors make the store damaged).
    const ConfigReport& Report() const { return report_; }
    /// Returns whether the last `Load` found lines it could not understand.
    bool Damaged() const { return report_.error_count != 0; }

private:
    io::Path path_;
    FileStoreOptions options_;
    ConfigDocument doc_;
    ConfigReport report_;
    Vector<char> on_disk_;         // the bytes the file held at the last Load or Commit (the next .bak)
    bool exists_ = false;          // the file existed then
    bool backup_written_ = false;  // .bak already holds the file as it was found (set by the first Commit that replaced
                                   // it)
    u32 staged_ = 0;
    StoreError load_error_ = StoreError::None;  // why the last Load could not read the file
};

}  // namespace vse::settings
