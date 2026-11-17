// The PipeWire backend (Phase 26 Days 425-426): the sound server's own client library, loaded when the device is
// opened.
#pragma once

#include "audio/device.h"
#include "audio/device_list.h"
#include "containers/unique_ptr.h"
#include "containers/vector.h"

namespace engine_placeholder::audio
{

/// Whether `libpipewire` can be loaded on this machine (it is looked for when asked, not linked).
///
/// - Returns: true if the library loads and exports what the backend needs; false also when the build was made without
///   the PipeWire headers.
/// - Fails: never.
bool PipeWireAvailable();

/// Lists the PipeWire outputs: one entry, the server's default sink, when the library loads; none otherwise.
///
/// - Param out: receives the descriptions (cleared first).
/// - Ownership: `out` is borrowed for the call.
/// - Returns: `Ok` or `OutOfMemory`.
/// - Fails: `OutOfMemory`.
DeviceStatus EnumeratePipeWireDevices(Vector<DeviceDescription>* out);

/// Makes the PipeWire device. `Open` connects a playback stream to the server's default sink and reports `NotFound`
/// when the library or the server is missing.
///
/// - Returns: the device, or null if the heap is exhausted.
/// - Ownership: the caller owns the result.
/// - Fails: reported by a null result.
UniquePtr<AudioDevice> CreatePipeWireDevice();

}  // namespace engine_placeholder::audio
