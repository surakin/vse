// The Linux sound output (Phase 26 Days 425-426): PipeWire first, the raw ALSA devices when it is absent.
#pragma once

#include "audio/device.h"
#include "audio/device_list.h"
#include "containers/unique_ptr.h"
#include "containers/vector.h"

namespace vse::audio
{

/// Makes the device `DeviceKind::Platform` means on Linux: `Open` tries PipeWire (for the default or the name
/// `pipewire`), then ALSA (for the default or a `hw:<card>,<device>` name), and reports the most telling failure.
///
/// - Returns: the device, or null if the heap is exhausted.
/// - Ownership: the caller owns the result.
/// - Fails: reported by a null result.
UniquePtr<AudioDevice> CreateLinuxPlatformDevice();

/// Lists the Linux outputs: PipeWire's default (if the library loads), then the ALSA playback devices.
///
/// - Param out: receives the descriptions (cleared first).
/// - Ownership: `out` is borrowed for the call.
/// - Returns: `Ok` (an empty list is not an error) or `OutOfMemory`.
/// - Fails: `OutOfMemory`.
DeviceStatus EnumerateLinuxDevices(Vector<DeviceDescription>* out);

}  // namespace vse::audio
