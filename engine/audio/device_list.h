// The device list (Phase 26 Day 419): what `EnumerateDevices` reports for each device kind.
#pragma once

#include "audio/device.h"
#include "containers/vector.h"

namespace vse::audio
{

/// One sound output a backend can open.
///
/// - Thread-safety: thread-compatible plain data.
struct DeviceDescription
{
    /// The name `DeviceConfig::name` takes.
    char name[96] = {};
    /// Whether this is the system's default output.
    bool is_default = false;
    /// The most channels the device plays.
    u32 max_channels = 2;
    /// The lowest and highest sample rate it accepts, in Hz.
    u32 min_rate = kMinSampleRate;
    /// See `min_rate`.
    u32 max_rate = kMaxSampleRate;
};

/// Lists the outputs of a device kind.
///
/// The Null and WAV-file kinds report one fake device each, named `null` and `wav-file`, both the default.
///
/// - Param kind: which kind of device to list.
/// - Param out: receives the descriptions (cleared first).
/// - Ownership: `out` is borrowed for the call.
/// - Returns: `Ok`, or `OutOfMemory`; `Unsupported` is never returned (a kind with no backend lists the null device).
/// - Fails: `OutOfMemory`.
DeviceStatus EnumerateDevices(DeviceKind kind, Vector<DeviceDescription>* out);

}  // namespace vse::audio
