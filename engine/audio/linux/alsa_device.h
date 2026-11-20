// The raw ALSA backend (Phase 26 Days 425-426): the kernel's PCM devices through ioctl on /dev/snd, no library.
#pragma once

#include "audio/device.h"
#include "audio/device_list.h"
#include "audio/pcm_negotiate.h"
#include "containers/unique_ptr.h"
#include "containers/vector.h"

namespace vse::audio
{

/// Lists the playback devices under /dev/snd (`hw:<card>,<device>`), the first being the default.
///
/// - Param out: receives the descriptions (cleared first); empty on a machine with no sound device.
/// - Ownership: `out` is borrowed for the call.
/// - Returns: `Ok` (also for an empty list) or `OutOfMemory`.
/// - Fails: `OutOfMemory`.
DeviceStatus EnumerateAlsaDevices(Vector<DeviceDescription>* out);

/// Makes the ALSA device. `Open` takes `hw:<card>,<device>` or the default (the first playback device that opens).
///
/// - Returns: the device, or null if the heap is exhausted.
/// - Ownership: the caller owns the result.
/// - Fails: reported by a null result.
UniquePtr<AudioDevice> CreateAlsaDevice();

}  // namespace vse::audio
