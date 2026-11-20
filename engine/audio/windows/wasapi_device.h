// The WASAPI backend (Phase 26 Day 440): Windows' sound output in shared mode, event-driven, through the system's COM
// interfaces (IMMDeviceEnumerator, IAudioClient, IAudioRenderClient). Exclusive mode is not built.
#pragma once

#include "audio/device.h"
#include "audio/device_list.h"
#include "containers/unique_ptr.h"
#include "containers/vector.h"

namespace vse::audio
{

/// Lists the active render endpoints; each name is the endpoint's id, and the system's default output (the console
/// role) is marked.
///
/// - Param out: receives the descriptions (cleared first); empty on a machine with no sound output.
/// - Ownership: `out` is borrowed for the call.
/// - Returns: `Ok` (also for an empty list, or when COM cannot start) or `OutOfMemory`.
/// - Fails: `OutOfMemory`.
DeviceStatus EnumerateWasapiDevices(Vector<DeviceDescription>* out);

/// Makes the WASAPI device. `Open` takes an endpoint id from `EnumerateWasapiDevices` or the default (empty or
/// `default`), grants the endpoint's mix rate and layout, and plays through the endpoint's own format.
///
/// - Returns: the device, or null if the heap is exhausted.
/// - Ownership: the caller owns the result.
/// - Fails: reported by a null result.
UniquePtr<AudioDevice> CreateWasapiDevice();

}  // namespace vse::audio
