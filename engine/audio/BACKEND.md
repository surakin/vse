# Audio: what a platform backend implements

The audio module (`engine/audio/`) renders on its own; a platform backend only moves the rendered frames to a sound card. `audio::AudioDevice`
(`engine/audio/device.h`) is the seam. This list is kept in step with the header by the test `audio_backend_lists`: every function whose
documentation carries a `Backend:` label has a `###` section here, and every section names such a function. The Null and WAV-file devices
(`engine/audio/device.cpp`) implement the same interface with no hardware and are the model for the call order.

## Rules every backend follows

- **The render callback never allocates, locks or does I/O**, and neither does the backend's own thread around it: buffers are allocated in `Open`.
- **`Open` grants, it does not refuse needlessly.** Ask the system for the wanted rate, layout and buffer size; report what it gave in `Info()`
  (the mixer adapts). Refuse with `Unsupported` only when the layout cannot be played at all. A rate of 0 means the device's own rate.
- **`Stop` returns after the last render call.** The mixer's state may be destroyed right after.
- **A lost device calls the error callback once** (`Lost`) from the backend's thread and then stops calling render; `Stop` and `Close` stay safe.
- **`Pump` returns `Unsupported`:** hardware devices run from their own thread.
- **Wrapped types only** (no `std`), platform calls only under the backend's directory (`engine/audio/<platform>/`), platform macros only through
  `build_defs.h`. A backend registers itself in `CreateDevice(DeviceKind::Platform)` and `EnumerateDevices`.

## Functions

### `Open`
Open the sound output with the wanted rate, layout and buffer size; fill the info the mixer reads (granted rate, layout, buffer frames, latency).

### `Start`
Start the backend's thread or callback; the first render call may come before `Start` returns.

### `Stop`
Stop the callbacks and wait for the last one to finish. Safe when not started.

### `Close`
Stop if needed and release the system's device. Safe when not open.
