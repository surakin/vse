#pragma once

//! The environment as a settings layer, and the command line as another.

#include "settings/layers.h"

namespace engine_placeholder::settings {

/// A copy of the process environment (or a made-up one in a test).
///
/// Thread-safety: thread-compatible.
/// Ownership: owns copies of the names and values.
class EnvironmentSnapshot {
public:
    /// One variable.
    struct Var {
        /// The variable's name.
        String<HeapAllocator> name = String<HeapAllocator>(HeapAllocator::Default());
        /// The variable's value.
        String<HeapAllocator> value = String<HeapAllocator>(HeapAllocator::Default());
    };
    /// Adds a variable (a later one with the same name replaces the earlier).
    void Add(StringView name, StringView value);
    /// Returns the variable's value, or null if it is not set. Valid until the snapshot changes.
    const String<HeapAllocator>* Find(StringView name) const;
    /// Copies the process environment.
    static EnvironmentSnapshot Capture();
    /// Returns the number of variables.
    usize Size() const { return vars_.Size(); }

private:
    Vector<Var> vars_;
};

/// The longest variable name [[settings::EnvironmentVariableFor]] writes, including the terminator.
inline constexpr usize kMaxEnvNameBytes = 128;

/// Writes the standard variable name of a key: `ENGINE_SETTING_` plus the key in upper case with each `.` replaced by `__`
/// (`renderer.present_mode` is `ENGINE_SETTING_RENDERER__PRESENT_MODE`).
///
/// Param key: a valid key.
/// Param out: destination of at least [[settings::kMaxEnvNameBytes]] bytes.
void EnvironmentVariableFor(const Key& key, char* out, usize capacity);

/// A read-only store over a list of `key -> text` entries, each with a note on where it came from. The base of the environment and command-line layers.
///
/// Thread-safety: thread-compatible.
/// Ownership: owns its entries.
class TextEntryStore : public ConfigStore {
public:
    u32 Capabilities() const override { return kCapPreservesOrder; }
    Status Load() override { return Status(); }
    /// Returns the entry's text, or `TooLong` if it is over 511 bytes (the same answer as a file store gives).
    Status Get(StringView key, StoredValue* out) override;
    Status Set(StringView, const StoredValue&) override { return Status::Of(StoreError::ReadOnly); }
    Status Remove(StringView) override { return Status::Of(StoreError::ReadOnly); }
    Status Enumerate(StringView prefix, Vector<Key>* out) override;
    Status Commit(const CommitOptions&) override { return Status::Of(StoreError::ReadOnly); }
    void Origin(StringView key, char* out, usize capacity) const override;

protected:
    /// Adds an entry, or replaces the text and origin of the entry for `key`.
    void Put(const Key& key, StringView text, StringView origin);

private:
    struct Entry {
        Key key;
        String<HeapAllocator> text = String<HeapAllocator>(HeapAllocator::Default());
        String<HeapAllocator> origin = String<HeapAllocator>(HeapAllocator::Default());
    };
    Vector<Entry> entries_;
};

/// A read-only layer over an environment snapshot. A setting is supplied by `ENGINE_SETTING_<KEY>` (see
/// [[settings::EnvironmentVariableFor]]) or by the variable its declaration names in `env` (`ENGINE_RHI_GPU`); the standard name wins when both
/// are set. Values are text; the layer stack converts them by the setting's type and reports a bad one with the variable's name.
///
/// Thread-safety: thread-compatible.
/// Ownership: copies what it needs; the registry need not outlive it.
class EnvironmentSource final : public TextEntryStore {
public:
    /// Reads the variables that apply to the registered settings.
    EnvironmentSource(const Registry& registry, const EnvironmentSnapshot& environment);

    void Describe(char* out, usize capacity) const override;
};

/// What the command line asked for.
///
/// Thread-safety: thread-compatible.
struct CommandLineResult {
    /// Arguments the settings system does not own, in order, for the application (the program name is not included).
    Vector<String<HeapAllocator>> rest;
    /// The problems found: an unknown `--set` key (with a suggestion), a missing value, a flag alias with a bad form.
    Vector<String<HeapAllocator>> errors;
    /// The files named by `--settings` (each becomes a `Custom` layer).
    Vector<String<HeapAllocator>> settings_files;
    /// `--list-settings` was given.
    bool list_settings = false;
    /// `--help` or `-h` was given.
    bool help = false;
    /// `--save-settings` was given.
    bool save_settings = false;
    /// The file of `--save-settings=FILE`, or empty for the user's file.
    String<HeapAllocator> save_path = String<HeapAllocator>(HeapAllocator::Default());
};

/// A read-only layer over command-line arguments: `--set key=value` (repeatable; a later one for a key wins; the value may contain `=` and may
/// be quoted), a setting's declared `flag` alias (`--gpu NAME` or `--gpu=NAME`; for a bool `--flag`, `--flag=false` and `--no-flag`, never
/// a separate value), and `--settings FILE`, `--list-settings`, `--save-settings[=FILE]` and `--help`, which are recorded for the application.
/// Everything else is left in [[settings::CommandLineResult::rest]]. A bare `--` ends the options: it and everything after it go to `rest` untouched.
///
/// Thread-safety: thread-compatible.
/// Ownership: copies what it needs.
class CommandLineSource final : public TextEntryStore {
public:
    /// Parses the arguments.
    ///
    /// Param registry: the settings (for key checks and flag aliases).
    /// Param argc: the argument count including the program name.
    /// Param argv: the arguments; `argv[0]` is skipped.
    CommandLineSource(const Registry& registry, int argc, const char* const* argv);

    /// Returns what the parse found.
    const CommandLineResult& Result() const { return result_; }

    void Describe(char* out, usize capacity) const override;

private:
    CommandLineResult result_;
};

} // namespace engine_placeholder::settings
