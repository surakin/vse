// The CoreAudio backend (Phase 26 Day 441): macOS's sound output through an AUHAL output unit
// (kAudioUnitSubType_HALOutput) bound to one device, with a render callback on CoreAudio's I/O thread. Plain C APIs
// (AudioToolbox, CoreAudio, CoreFoundation, libdispatch): no Objective-C.
#pragma once

#include "audio/device.h"
#include "audio/device_list.h"
#include "containers/unique_ptr.h"
#include "containers/vector.h"

namespace engine_placeholder::audio
{

/// Lists the devices that have output channels; each name is the device's UID (`kAudioDevicePropertyDeviceUID`), and
/// the system's default output is marked.
///
/// - Param out: receives the descriptions (cleared first); empty on a machine with no sound output.
/// - Ownership: `out` is borrowed for the call.
/// - Returns: `Ok` (also for an empty list, or when the system will not list its devices) or `OutOfMemory`.
/// - Fails: `OutOfMemory`.
DeviceStatus EnumerateCoreAudioDevices(Vector<DeviceDescription>* out);

/// Makes the CoreAudio device. `Open` takes a UID from `EnumerateCoreAudioDevices` or the default output (empty or
/// `default`), grants the device's nominal rate and its own channel count (stereo or mono for a count with no layout),
/// and asks the device for the wanted buffer size within its range.
///
/// - Returns: the device, or null if the heap is exhausted.
/// - Ownership: the caller owns the result.
/// - Fails: reported by a null result.
UniquePtr<AudioDevice> CreateCoreAudioDevice();

}  // namespace engine_placeholder::audio
