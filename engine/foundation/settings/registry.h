#pragma once

//! The registry of settings: what exists, with its type, default, range and metadata, and the current value of each.
//!
//! A registry is filled at start-up ([[settings::Registry::Register]]), frozen ([[settings::Registry::Freeze]]) and then only read; the
//! current values change through [[settings::Registry::Publish]] (the settings service does this) and are read by typed handles
//! ([[settings::Setting]]) at the cost of one relaxed atomic load.

#include <atomic>
#include <mutex>

#include "containers/unique_ptr.h"
#include "containers/vector.h"
#include "settings/desc.h"
#include "settings/key.h"
#include "types.h"

namespace engine_placeholder::settings {

/// How a setting is identified: small integers handed out in registration order; 0 is never valid.
///
/// Thread-safety: thread-compatible plain data.
struct SettingId {
    /// The value; 0 means no setting.
    u16 value = 0;
    /// Returns whether this names a setting.
    constexpr bool IsValid() const { return value != 0; }
    /// Returns whether two ids are the same.
    constexpr bool operator==(const SettingId& other) const { return value == other.value; }
    /// Returns whether two ids differ.
    constexpr bool operator!=(const SettingId& other) const { return value != other.value; }
};

/// The most settings a registry holds.
inline constexpr u32 kMaxRegisteredSettings = 65535;

/// Why a registration was refused.
enum class RegisterError : u8 {
    None = 0,     ///< Registered.
    BadKey,       ///< The key (or a deprecated key) is not a valid [[settings::Key]], is null, or is `version` or ends in `.schema` (reserved for version stamps).
    Duplicate,    ///< The key, or a deprecated key, is already taken.
    BadDefault,   ///< The default has another type than the declaration, is outside the range or the enum, or is not finite.
    BadRange,     ///< `min` is above `max`, or a float bound is not finite.
    BadEnum,      ///< An enum with no names, more than 32, or a name that is null, empty, longer than 31 bytes, or holds a blank, `=`, `#`, `;` or `"`, or twice.
    BadDeprecated,///< More than 8 deprecated keys, or a null entry.
    Full,         ///< The registry holds 65535 settings.
};

/// Returns a short name for a registration error (`none`, `bad_key`, `duplicate`, `bad_default`, `bad_range`, `bad_enum`, `bad_deprecated`, `full`).
///
/// Returns: a static string.
/// Fails: never.
/// Ownership: the string is static and never freed.
const char* RegisterErrorName(RegisterError error);

/// The settings that exist and the current value of each.
///
/// Thread-safety: thread-compatible. `Register` and `Freeze` belong before any other thread uses the registry; every other
///   function may then be called from any thread, and `Publish` and typed reads are safe together.
/// Ownership: owns copies of the declarations (the strings in them are borrowed and must outlive the registry).
class Registry {
public:
    /// One setting's storage; public so a handle can point at it.
    struct Cell {
        /// The current value of a bool, int, float or enum, encoded (see `Setting`). Text values live in the registry under a lock.
        std::atomic<u64> bits{0};
    };

    /// Creates an empty registry.
    Registry();
    /// Destroys the registry; no handle made from it may be used afterwards.
    ~Registry();
    Registry(const Registry&) = delete;
    Registry& operator=(const Registry&) = delete;

    /// Returns the process-wide registry that engine modules register their settings with.
    ///
    /// Returns: a registry that lives for the whole process.
    /// Fails: never.
    /// Thread-safety: thread-safe.
    static Registry& Global();

    /// Registers a setting; its current value starts at the default.
    ///
    /// Param desc: the declaration (copied; its strings are borrowed).
    /// Param error: receives the reason on failure (may be null).
    /// Returns: the new id, or an invalid id on failure.
    /// Fails: see [[settings::RegisterError]]; the registry is unchanged.
    /// Aborts: the registry is frozen.
    SettingId Register(const SettingDesc& desc, RegisterError* error = nullptr);

    /// Ends registration; later `Register` calls abort. Calling it twice does nothing.
    void Freeze();
    /// Returns whether [[settings::Registry::Freeze]] was called.
    bool IsFrozen() const { return frozen_.load(std::memory_order_relaxed); }
    /// Returns the number of registered settings.
    u32 Count() const { return static_cast<u32>(slots_.Size()); }

    /// Finds a setting by its key or by one of its deprecated keys.
    ///
    /// Param key: the key text.
    /// Returns: its id, or an invalid id.
    /// Fails: reports absence by returning an invalid id.
    SettingId Find(StringView key) const;
    /// Returns whether `key` is one of the setting's *deprecated* keys rather than its current one.
    bool IsDeprecatedKey(StringView key) const;

    /// Declares the schema version of a section: the number of times the meaning of its keys has changed, starting at 1. Files record the version
    /// they were written with (`<section>.schema`), and migrations upgrade older ones. A section without a declared version is version 1 and has
    /// no migrations.
    ///
    /// Aborts: the registry is frozen, `section` is not a valid key, or `version` is 0.
    void SetSchemaVersion(StringView section, u32 version);
    /// Returns the declared schema version of `section`, or 0 if none was declared.
    u32 SchemaVersion(StringView section) const;
    /// Returns the number of sections with a declared schema version.
    u32 SchemaCount() const { return static_cast<u32>(schemas_.Size()); }
    /// Returns the `index`th declared section name and sets `*version`; `index` must be less than `SchemaCount()`.
    StringView SchemaAt(u32 index, u32* version) const;

    /// Returns a setting's declaration, or null if `id` is not registered.
    const SettingDesc* Desc(SettingId id) const;
    /// Returns a setting's key, or the invalid key if `id` is not registered.
    const Key& KeyOf(SettingId id) const;
    /// Returns the id of the `index`th setting in key order (bytewise); `index` must be less than `Count()`.
    ///
    /// Aborts: `index` is not less than `Count()`.
    SettingId SortedAt(u32 index) const;

    /// Returns a setting's current value.
    ///
    /// Aborts: `id` is not registered.
    Value Current(SettingId id) const;
    /// Makes `value` the setting's current value: later reads through a handle see it. The value is not validated against the range
    /// (the settings service does that); it must have the declared type.
    ///
    /// Aborts: `id` is not registered, the value has another type than the declaration, or an enum index is outside the names.
    void Publish(SettingId id, const Value& value);
    /// Returns a setting's storage, for [[settings::Setting]]; null if `id` is not registered.
    const Cell* CellOf(SettingId id) const;
    /// Copies a text setting's current value into `out`.
    ///
    /// Param out: destination of the text and a NUL.
    /// Param capacity: size of `out`.
    /// Returns: the length of the text; if it does not fit (`capacity` too small) nothing is written and the needed length is returned.
    /// Aborts: `id` is not registered or is not a text setting.
    usize CurrentText(SettingId id, char* out, usize capacity) const;

private:
    struct Slot;
    Vector<UniquePtr<Slot>> slots_;
    Vector<u16> sorted_; // ids ordered by key
    struct Schema {
        Key section;
        u32 version = 0;
    };
    Vector<Schema> schemas_;
    Vector<u16> aliases_sorted_; // ids of settings that have deprecated keys, searched linearly (rare)
    mutable std::mutex text_mutex_;
    std::atomic<bool> frozen_{false};
    usize LowerBound(StringView key) const;
};

} // namespace engine_placeholder::settings
