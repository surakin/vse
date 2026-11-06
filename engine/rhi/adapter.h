#pragma once

#include "rhi/limits.h"
#include "types.h"

//! The GPUs (adapters) a backend can use, as plain data, and the rule that matches a user's request to one of them.

namespace engine_placeholder::rhi {

struct DeviceCreateInfo;

/// The most adapters listed; further ones are ignored.
inline constexpr usize kMaxAdapters = 16;
/// The size of [[rhi::AdapterInfo::id]], including the terminator.
inline constexpr usize kAdapterIdBytes = 64;
/// The size of [[rhi::AdapterInfo::name]], including the terminator.
inline constexpr usize kAdapterNameBytes = 256;

/// What kind of GPU an adapter is.
enum class AdapterKind : u8 {
    Other = 0,  ///< Unknown kind.
    Software,   ///< A software implementation running on the CPU.
    Virtual,    ///< A virtualized GPU.
    Integrated, ///< A GPU sharing memory with the CPU.
    Discrete,   ///< A dedicated GPU.
};

/// One adapter.
///
/// Thread-safety: thread-compatible plain data.
struct AdapterInfo {
    /// A stable id, null-terminated: the same string for the same GPU and driver on every run on one machine, and not an
    /// enumeration index, so a saved setting survives another GPU being added. Vulkan builds it from the vendor id, the
    /// device id and the device UUID (a driver that reports no UUID gives all zeros, so two identical cards could then share an id); the null backend uses `null-discrete` and `null-integrated`.
    char id[kAdapterIdBytes] = {};
    /// The name the driver reports, null-terminated.
    char name[kAdapterNameBytes] = {};
    /// The kind of GPU.
    AdapterKind kind = AdapterKind::Other;
    /// Device-local memory in bytes (the largest device-local heap); 0 if unknown.
    u64 dedicated_memory_bytes = 0;
    /// The driver's version text, null-terminated; may be empty.
    char driver[64] = {};
    /// Whether the adapter can present to the window given to the enumeration; always false without a window.
    bool can_present = false;
    /// Whether the adapter meets the engine's requirements (and, with a window, can present to it).
    bool suitable = false;
    /// Why `suitable` is false: a static string; null when the adapter is suitable.
    const char* unsuitable_reason = nullptr;
};

/// A fixed-capacity list of adapters.
///
/// Thread-safety: thread-compatible plain data.
struct AdapterList {
    /// The adapters; only the first `count` are valid.
    AdapterInfo items[kMaxAdapters];
    /// Number of valid entries in `items`.
    u32 count = 0;
};

/// Lists the adapters the backend of `info` can use.
///
/// The Vulkan backend creates a throwaway instance (no device), reads every physical device and whether it can present to
/// `info.window`, and destroys the instance. The null backend lists two fake adapters (discrete, then integrated), or the
/// list set by [[rhi::SetNullAdapters]]; it ignores the window.
///
/// Param info: the backend, validation settings and window to test presentability against.
/// Param out: receives the list; cleared first.
/// Returns: true if the list was read (it may be empty); false if the backend could not be started.
/// Fails: returns false with an empty list, logging the reason, if the Vulkan loader is missing or the instance cannot
///   be created.
/// Ownership: `out` belongs to the caller; nothing is retained after the call.
/// Thread-safety: thread-safe.
bool EnumerateAdapters(const DeviceCreateInfo& info, AdapterList* out);

/// Finds the adapter a request names.
///
/// `request` is tried as an id (exact, case-sensitive), then as a decimal index into the list, then as a case-insensitive
/// substring of the name; the first adapter that matches wins. A null or empty request matches nothing.
///
/// Param items: the adapters.
/// Param count: number of adapters.
/// Param request: the text to match.
/// Returns: the index of the adapter, or -1.
/// Thread-safety: thread-safe.
int MatchAdapter(const AdapterInfo* items, usize count, const char* request);

/// Replaces the adapters the null backend reports, for tests; null restores the two default fake adapters.
///
/// Param list: the list to report, copied; null for the default.
/// Thread-safety: thread-safe.
void SetNullAdapters(const AdapterList* list);

/// Returns the null backend's default list: a discrete adapter `null-discrete`, then an integrated one `null-integrated`.
///
/// Param out: receives the list.
/// Ownership: `out` belongs to the caller.
void DefaultNullAdapters(AdapterList* out);

} // namespace engine_placeholder::rhi
