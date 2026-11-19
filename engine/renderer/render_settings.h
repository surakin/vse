#pragma once

//! Rendering settings the user can change at run time, what each change costs, and how two sets of settings become the
//! cheapest sufficient action.
//!
//! A setting is declared once in a [[renderer::SettingsRegistry]] with a type, a default, a range, a
//! [[renderer::CostClass]] and the capability it needs. A [[renderer::RenderSettings]] holds one value per registered
//! setting. [[renderer::DiffSettings]] compares two of them and reports the highest cost class among the changes, which
//! is the cheapest action that is sufficient (a higher class includes the work of the lower ones). The registry is
//! open: a later subsystem registers its own settings and takes part in the diff without any change here. Settings can
//! be written to and read from a small text format.

#include "types.h"

namespace engine_placeholder::renderer
{

/// The most settings a registry holds.
inline constexpr u32 kMaxSettings = 64;
/// The most bytes (including the terminator) in a text setting.
inline constexpr u32 kSettingTextBytes = 64;
/// The most entries a [[renderer::Capabilities]] holds.
inline constexpr u32 kMaxCapabilities = 32;
/// The most problems a [[renderer::SettingsParseReport]] records of each kind.
inline constexpr u32 kMaxParseProblems = 8;
/// The version written by [[renderer::SerializeSettings]].
inline constexpr u32 kSettingsFormatVersion = 1;

/// What changing a setting costs. Ordered: a larger class includes the actions of every smaller one.
enum class CostClass : u8
{
    None = 0,         ///< Takes effect without touching the GPU or any frame.
    Dynamic,          ///< Changes a parameter that the next frame reads.
    Swapchain,        ///< Recreates the swapchain and what depends on its size or format.
    PipelineRebuild,  ///< Releases and recreates pipelines, material bind groups and passes; the device stays.
    DeviceReset,      ///< Destroys and creates the device; every device-dependent object is rebuilt.
};

/// Returns a cost class's name (`none`, `dynamic`, `swapchain`, `pipeline-rebuild`, `device-reset`).
///
/// - Param cost: any class.
/// - Returns: a static string.
/// - Fails: never.
/// - Ownership: the string is static and never freed.
const char* CostClassName(CostClass cost);

/// The kind of value a setting holds.
enum class SettingType : u8
{
    Bool = 0,  ///< True or false.
    Enum,      ///< One of a fixed list of names, held as an index.
    Int,       ///< A whole number within a range.
    Float,     ///< A finite `f32` within a range.
    Text,      ///< A short string of printable ASCII.
};

/// What setting a value outside its range does.
enum class RangePolicy : u8
{
    Clamp = 0,  ///< The value is moved to the nearest end of the range and the call reports `Clamped`.
    Reject,     ///< The value is refused, the setting is unchanged and the call reports `OutOfRange`.
};

/// How a setting is identified. Small integers handed out by the registry in registration order; 0 is never valid.
///
/// - Thread-safety: thread-compatible plain data.
struct SettingId
{
    /// The value; 0 means no setting.
    u16 value = 0;
    /// Returns whether this names a setting.
    /// - Fails: never.
    constexpr bool IsValid() const { return value != 0; }
    /// Returns whether two ids are the same.
    constexpr bool operator==(const SettingId& other) const { return value == other.value; }
    /// Returns whether two ids differ.
    constexpr bool operator!=(const SettingId& other) const { return value != other.value; }
};

/// The declaration of one setting.
///
/// - Thread-safety: thread-compatible plain data. The strings must outlive the registry (use string literals).
struct SettingDesc
{
    /// The setting's key: lowercase letters, digits and `_`, 1 to 31 characters, unique in the registry.
    const char* name = nullptr;
    /// The kind of value.
    SettingType type = SettingType::Bool;
    /// What a change costs.
    CostClass cost = CostClass::None;
    /// The capability the setting needs, or null if it always works.
    ///
    /// See [[renderer::Capabilities]]. A change to a value other than the default is unsupported while the capability
    /// is missing; putting the setting back to its default is always allowed.
    const char* capability = nullptr;
    /// True for a setting that exists only to exercise the mechanism and has no visible effect yet.
    bool mechanism_only = false;
    /// True for a setting that takes effect only when the program starts (`backend`): a change at run time is not
    /// applied but listed in
    /// [[renderer::SettingsChange::restart]], and the value is kept for the next start by whoever stores the settings.
    /// `cost` then says what a different value means for a device, not what a run-time change does.
    bool restart_required = false;
    /// What an out-of-range number does (`Int` and `Float`).
    RangePolicy range_policy = RangePolicy::Clamp;
    /// The default of a `Bool` (0 or 1), `Enum` (an index) or `Int`.
    i64 default_int = 0;
    /// The smallest `Int`.
    i64 min_int = 0;
    /// The largest `Int`.
    i64 max_int = 0;
    /// The default of a `Float`.
    f32 default_float = 0.0f;
    /// The smallest `Float`.
    f32 min_float = 0.0f;
    /// The largest `Float`.
    f32 max_float = 0.0f;
    /// The default of a `Text` (may be null for the empty string).
    const char* default_text = nullptr;
    /// The names of an `Enum`'s values, in index order.
    const char* const* enum_names = nullptr;
    /// How many names `enum_names` holds, 1 to 16.
    u32 enum_count = 0;
};

/// The settings that exist, with their types, defaults, ranges and costs.
///
/// A registry is filled at start-up and then only read. [[renderer::SettingsRegistry::Builtin]] is the process-wide one
/// that the renderer uses; a test builds its own with [[renderer::RegisterBuiltinSettings]] plus whatever it wants to
/// add.
///
/// - Thread-safety: thread-compatible. Register only before any thread reads the registry.
/// - Ownership: borrows the strings in each [[renderer::SettingDesc]].
class SettingsRegistry
{
public:
    /// Registers a setting.
    ///
    /// - Param desc: the declaration; copied (the strings are borrowed).
    /// - Returns: the new id, or an invalid id after logging the reason.
    /// - Fails: the registry is full, the name is empty, too long, uses other characters or is already taken, an `Enum`
    /// has no
    ///   or too many names or a default outside them, an `Int` or `Float` has `min` above `max` or a default outside
    ///   its range, a `Float` bound is not finite, a `Text` default is too long or has edge blanks, an `Enum` name is
    ///   empty, null, longer than 31 characters or has a blank, `=` or `#`.
    SettingId Register(const SettingDesc& desc);

    /// Returns the number of registered settings.
    /// - Fails: never.
    u32 Count() const { return count_; }

    /// Returns a setting's declaration.
    ///
    /// - Param id: any id.
    /// - Returns: the declaration, or null if `id` is not registered.
    /// - Fails: never.
    /// - Ownership: borrowed; valid for the registry's lifetime.
    const SettingDesc* Find(SettingId id) const;

    /// Looks a setting up by key.
    ///
    /// - Param name: the key; may be null.
    /// - Returns: the id, or an invalid id if there is none.
    /// - Fails: never.
    SettingId FindByName(const char* name) const;

    /// Returns the process-wide registry, with the built-in settings registered on first use.
    /// - Fails: never.
    /// - Thread-safety: thread-safe to obtain; see the class for registering into it.
    static SettingsRegistry& Builtin();

private:
    SettingDesc descs_[kMaxSettings];
    u32 count_ = 0;
};

/// The built-in settings. Their ids are fixed: registration order, starting at 1.
namespace settings
{
/// Which backend renders (`null`, `vulkan`). `DeviceReset`.
inline constexpr SettingId kBackend{1};
/// The adapter's stable id; empty means the best one. `DeviceReset`.
inline constexpr SettingId kAdapter{2};
/// Validation layers (`auto`, `off`, `on`). `DeviceReset`.
inline constexpr SettingId kValidation{3};
/// Synchronization validation. `DeviceReset`.
inline constexpr SettingId kSyncValidation{4};
/// Frames the CPU may run ahead, 1 to 3. `DeviceReset`.
inline constexpr SettingId kFramesInFlight{5};
/// Present mode (`fifo`, `mailbox`, `immediate`). `Swapchain`.
inline constexpr SettingId kPresentMode{6};
/// Swapchain width in pixels; 0 follows the window. `Swapchain`.
inline constexpr SettingId kWidth{7};
/// Swapchain height in pixels; 0 follows the window. `Swapchain`.
inline constexpr SettingId kHeight{8};
/// Fullscreen; needs the `fullscreen` capability. `Swapchain`.
inline constexpr SettingId kFullscreen{9};
/// Swapchain format (`auto`, `srgb8`, `rgba16f`, `rgb10a2`). `Swapchain`.
inline constexpr SettingId kSwapchainFormat{10};
/// High dynamic range output (`off`, `scrgb`, `hdr10`); needs the `hdr` capability. `Swapchain`.
inline constexpr SettingId kHdr{11};
/// Shader quality (`low`, `medium`, `high`). `PipelineRebuild`; mechanism only until shaders read it.
inline constexpr SettingId kShaderQuality{12};
/// The G-buffer debug layer (`none`, `albedo`, `normal`, `material`, `depth`, `worldpos`, `emissive`). `Dynamic`.
inline constexpr SettingId kGBufferDebugLayer{13};
/// Render scale, 0.25 to 2. `Dynamic`; mechanism only until the targets read it.
inline constexpr SettingId kRenderScale{14};
/// The joint palette's room per frame in bytes (default 16 MiB, about 350,000 joints; 48 to 768 MiB): read when the
/// program makes its `FrameResources`, so a change takes effect at the next start. `DeviceReset`, restart required.
inline constexpr SettingId kJointPaletteBytes{15};
}  // namespace settings

/// Registers the built-in settings (the ids in the `settings` namespace) into an empty registry, in id order.
///
/// - Param registry: must be empty.
/// - Aborts: `registry` is null or not empty.
/// - Ownership: `registry` is borrowed for the call.
void RegisterBuiltinSettings(SettingsRegistry* registry);

/// What a call that sets a value reports.
enum class SetStatus : u8
{
    Ok = 0,      ///< The value was stored as given.
    Clamped,     ///< The value was stored after moving it into its range.
    OutOfRange,  ///< The value is outside its range or not finite, and the setting is unchanged.
    WrongType,   ///< The call does not match the setting's type; unchanged.
    UnknownId,   ///< No such setting; unchanged.
    BadText,     ///< The text is too long or has characters outside printable ASCII; unchanged.
};

/// One value per registered setting.
///
/// A plain value type (about 5 KiB, no heap): copy it freely. Equality ignores `Generation`. A setting registered after
/// this object was made reads as its default until it is set.
///
/// - Thread-safety: thread-compatible.
/// - Ownership: borrows the registry, which must outlive it.
class RenderSettings
{
public:
    /// Creates settings with every value at its default.
    ///
    /// - Param registry: the registry to follow; defaults to [[renderer::SettingsRegistry::Builtin]].
    explicit RenderSettings(const SettingsRegistry& registry = SettingsRegistry::Builtin());

    /// Returns the registry this follows.
    /// - Fails: never.
    const SettingsRegistry& Registry() const { return *registry_; }

    /// Returns a `Bool` setting.
    /// - Param id: the setting.
    /// - Returns: its value.
    /// - Fails: a wrong type or unknown id returns false.
    bool GetBool(SettingId id) const;
    /// Returns an `Int` setting.
    /// - Param id: the setting.
    /// - Returns: its value.
    /// - Fails: a wrong type or unknown id returns 0.
    i64 GetInt(SettingId id) const;
    /// Returns an `Enum` setting's index.
    /// - Param id: the setting.
    /// - Returns: the index into the setting's names.
    /// - Fails: a wrong type or unknown id returns 0.
    u32 GetEnum(SettingId id) const;
    /// Returns a `Float` setting.
    /// - Param id: the setting.
    /// - Returns: its value.
    /// - Fails: a wrong type or unknown id returns 0.
    f32 GetFloat(SettingId id) const;
    /// Returns a `Text` setting.
    /// - Param id: the setting.
    /// - Returns: its value, never null.
    /// - Fails: a wrong type or unknown id returns the empty string.
    /// - Ownership: borrowed; valid until the settings are changed or destroyed.
    const char* GetText(SettingId id) const;

    /// Sets a `Bool` setting.
    /// - Param id: the setting.
    /// - Param value: the new value.
    /// - Returns: `Ok`, or `WrongType` / `UnknownId`.
    SetStatus SetBool(SettingId id, bool value);
    /// Sets an `Int` setting, applying its range policy.
    /// - Param id: the setting.
    /// - Param value: the new value.
    /// - Returns: `Ok`, `Clamped`, `OutOfRange`, `WrongType` or `UnknownId`.
    SetStatus SetInt(SettingId id, i64 value);
    /// Sets an `Enum` setting by index.
    /// - Param id: the setting.
    /// - Param index: the index; it must be below the setting's name count (an enum never clamps).
    /// - Returns: `Ok`, `OutOfRange`, `WrongType` or `UnknownId`.
    SetStatus SetEnum(SettingId id, u32 index);
    /// Sets a `Float` setting, applying its range policy.
    /// - Param id: the setting.
    /// - Param value: the new value; NaN and infinities are `OutOfRange` whatever the policy.
    /// - Returns: `Ok`, `Clamped`, `OutOfRange`, `WrongType` or `UnknownId`.
    SetStatus SetFloat(SettingId id, f32 value);
    /// Sets a `Text` setting.
    /// - Param id: the setting.
    /// - Param value: printable ASCII (32 to 126) without a leading or trailing blank, fewer than
    /// [[renderer::kSettingTextBytes]]
    ///   characters; null means empty.
    /// - Returns: `Ok`, `BadText`, `WrongType` or `UnknownId`.
    SetStatus SetText(SettingId id, const char* value);

    /// Sets a setting from its text form, the one the file format uses: `true`/`false`, an enum name, a number, or the
    /// text.
    /// - Param id: the setting.
    /// - Param text: the value text, trimmed by the caller.
    /// - Returns: as the typed setter for the setting's type; `OutOfRange` for text that is not a valid value of the
    /// type.
    SetStatus SetFromString(SettingId id, const char* text);

    /// Writes a setting's value in its text form.
    /// - Param id: the setting.
    /// - Param out: the buffer.
    /// - Param capacity: its size in bytes.
    /// - Returns: the length the text needs (not counting the terminator); the output is truncated and terminated when
    /// it does
    ///   not fit, and 0 with an empty output for an unknown id.
    /// - Ownership: `out` is borrowed for the call.
    usize ValueToString(SettingId id, char* out, usize capacity) const;

    /// Returns how many times these settings were applied by the renderer. Not part of equality.
    /// - Fails: never.
    u64 Generation() const { return generation_; }
    /// Sets the generation; the renderer calls it after a successful apply.
    /// - Param generation: the new value.
    void SetGeneration(u64 generation) { generation_ = generation; }

    /// Returns whether every setting has the same value in both.
    /// - Param other: the settings to compare with; they must follow the same registry.
    /// - Returns: true if equal. `Generation` is ignored.
    /// - Aborts: `other` follows a different registry.
    bool operator==(const RenderSettings& other) const;
    /// Returns whether any setting differs.
    /// - Param other: the settings to compare with.
    bool operator!=(const RenderSettings& other) const { return !(*this == other); }

private:
    struct Value
    {
        i64 integer = 0;
        f32 real = 0.0f;
        char text[kSettingTextBytes] = {};
    };
    Value Get(SettingId id, const SettingDesc* desc) const;
    void Store(SettingId id, const Value& value);

    const SettingsRegistry* registry_;
    Value values_[kMaxSettings];
    u32 count_ = 0;  // values_[0..count_) were written; the rest read as defaults
    u64 generation_ = 0;
};

/// What the device and the window can do, by name.
///
/// Filled by the layer that knows (the adapter list, the swapchain and window queries). The names are the ones settings
/// declare in [[renderer::SettingDesc::capability]], for example `fullscreen` and `hdr`.
///
/// - Thread-safety: thread-compatible.
class Capabilities
{
public:
    /// Adds a capability.
    /// - Param name: a static string; duplicates are ignored.
    /// - Returns: false if the list is full.
    bool Add(const char* name);
    /// Returns whether a capability is present.
    /// - Param name: the capability; null means none needed and is always present.
    /// - Returns: true if present.
    bool Has(const char* name) const;

    /// Records how many suitable adapters the machine has (from [[rhi::EnumerateAdapters]]).
    ///
    /// With two or more the `multi_adapter` capability is present, which a settings screen can use to offer a choice.
    /// - Param count: the number of suitable adapters.
    void SetAdapterCount(u32 count);
    /// Returns the count given to `SetAdapterCount`; 0 until then.
    u32 AdapterCount() const { return adapter_count_; }

private:
    const char* names_[kMaxCapabilities] = {};
    u32 count_ = 0;
    u32 adapter_count_ = 0;
};

/// A requested change that cannot be done.
///
/// - Thread-safety: thread-compatible plain data.
struct UnsupportedSetting
{
    /// The setting.
    SettingId id;
    /// The capability that is missing (static string).
    const char* capability = nullptr;
};

/// What changing from one set of settings to another involves.
///
/// - Thread-safety: thread-compatible plain data (about 1.2 KiB).
struct SettingsChange
{
    /// The highest cost class among the supported changes; `None` when nothing (or only `None`-class settings) changed.
    CostClass cost = CostClass::None;
    /// How many settings changed and are supported.
    u32 changed_count = 0;
    /// The changed, supported settings, in id order.
    SettingId changed[kMaxSettings];
    /// How many changes cannot be done.
    u32 unsupported_count = 0;
    /// The changes that need a missing capability; they are not in `changed` and do not raise `cost`.
    UnsupportedSetting unsupported[kMaxSettings];
    /// How many changes wait for the next start.
    u32 restart_count = 0;
    /// The changes to settings that take effect only at start ([[renderer::SettingDesc::restart_required]]); they are
    /// not in `changed` and do not raise `cost`. A request that keeps asking for the new value (the settings system
    /// stores it for the next start) lists it again every time, until the program starts with it.
    SettingId restart[kMaxSettings];

    /// Returns whether anything supported changed.
    /// - Fails: never.
    bool Any() const { return changed_count != 0; }
};

/// Compares two sets of settings.
///
/// - Param from: the current settings.
/// - Param to: the requested settings.
/// - Param capabilities: what the device and window can do.
/// - Returns: the changed settings, the cheapest sufficient cost class, the changes that need a missing capability, and
/// the changes that
///   wait for the next start.
/// - Aborts: the two follow different registries.
SettingsChange DiffSettings(const RenderSettings& from, const RenderSettings& to, const Capabilities& capabilities);

/// Returns `requested` with every unsupported change, and every change to a setting that takes effect only at start,
/// put back to its value in `current`.
///
/// - Param current: the settings now in force.
/// - Param requested: what was asked for.
/// - Param capabilities: what is possible.
/// - Returns: the settings that can be applied.
/// - Aborts: the two follow different registries.
RenderSettings ResolveSettings(const RenderSettings& current, const RenderSettings& requested,
                               const Capabilities& capabilities);

/// Writes settings in the text format.
///
/// The output is a comment line, `version = 1`, then one `key = value` line per registered setting in id order. The
/// same settings always give the same bytes.
///
/// - Param settings: the settings.
/// - Param out: the buffer, or null to only measure.
/// - Param capacity: its size in bytes.
/// - Returns: the length needed, not counting the terminator; when `out` is too small the output is truncated and
/// terminated.
/// - Ownership: `out` is borrowed for the call.
usize SerializeSettings(const RenderSettings& settings, char* out, usize capacity);

/// One thing found wrong in a settings text.
///
/// - Thread-safety: thread-compatible plain data.
struct SettingsProblem
{
    /// The 1-based line.
    u32 line = 0;
    /// A description (the key, then the reason), null-terminated.
    char message[96] = {};
};

/// What [[renderer::ParseSettings]] found.
///
/// - Thread-safety: thread-compatible plain data.
struct SettingsParseReport
{
    /// The version the text declared (0 if it declared none).
    u32 version = 0;
    /// How many errors were found (the first [[renderer::kMaxParseProblems]] are recorded).
    u32 error_count = 0;
    /// The first errors.
    SettingsProblem errors[kMaxParseProblems];
    /// How many warnings were found (for example unknown keys).
    u32 warning_count = 0;
    /// The first warnings.
    SettingsProblem warnings[kMaxParseProblems];
};

/// Renames a key written by an older format version.
///
/// Called with the version the text declared and the key found; returns the current key to use instead, or null to keep
/// the key.
using SettingsKeyMigrationFn = const char* (*)(u32 file_version, const char* key);

/// Reads settings from text onto `settings`.
///
/// Lines are `key = value`; blank lines and lines starting with `#` are skipped; `version = N` declares the format.
/// Keys that are not registered are warnings and skipped. A malformed line, a bad value or a repeated key is an error.
/// When there is any error `settings` is left unchanged, so a half-read file is never applied.
///
/// - Param text: the text.
/// - Param length: its length in bytes.
/// - Param settings: the settings to change; keys the text omits keep their value.
/// - Param report: receives the findings; may be null.
/// - Param migrate: renames keys of older versions, or null.
/// - Returns: true if there were no errors.
/// - Fails: returns false and records the line and reason in `report`; a version newer than
/// [[renderer::kSettingsFormatVersion]] is an error.
/// - Ownership: `text`, `settings`, `report` and `migrate` are borrowed for the call.
bool ParseSettings(const char* text, usize length, RenderSettings* settings, SettingsParseReport* report,
                   SettingsKeyMigrationFn migrate = nullptr);

}  // namespace engine_placeholder::renderer
