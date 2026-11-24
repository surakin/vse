// The audio system (Phase 26 Day 427): a mixer, the device that plays it, and the changes that happen at run time: a
// setting applied live, a device switched, a device lost and found again.
#pragma once

#include "audio/audio_settings.h"
#include "audio/device.h"
#include "audio/mixer.h"
#include "containers/unique_ptr.h"
#include "sync/atomic.h"

namespace vse::audio
{

/// What applying new settings had to do, from cheapest to dearest.
enum class ApplyCost : u8
{
    /// Nothing changed.
    None,
    /// Only live values changed (the master volume).
    Free,
    /// The rate or the channel layout changed: the mixer was re-planned and the device reopened.
    Replan,
    /// The device or its buffer size changed: the device was closed and another opened.
    Reopen,
};

/// Where the output goes now.
enum class OutputState : u8
{
    /// Nothing is open (before `Init`, after `Shutdown`).
    Closed,
    /// The wanted device is playing.
    Device,
    /// The device was lost (or could not be opened) and the null device keeps the clock and the voices running
    /// silently.
    Fallback,
};

/// Makes the device for a kind (a seam for tests; null means `CreateDevice`): called with the pointer in
/// `AudioSystemConfig` and the kind, it returns the device.
using DeviceFactory = UniquePtr<AudioDevice> (*)(void* user, DeviceKind kind);

/// How the system starts.
///
/// - Thread-safety: thread-compatible plain data.
struct AudioSystemConfig
{
    /// The settings.
    AudioSettings settings;
    /// The device kind (`Platform` for the sound card).
    DeviceKind kind = DeviceKind::Platform;
    /// How often, in nanoseconds, a lost or missing device is tried again.
    u64 retry_nanos = 2000000000ull;
    /// Makes devices; null uses `CreateDevice`.
    DeviceFactory factory = nullptr;
    /// Passed to `factory`.
    void* factory_user = nullptr;
};

/// A mixer with the device that plays it.
///
/// The render state (voices, positions, buses) belongs to the mixer and survives every change of device. A lost device
/// moves the output to the null device, which the control thread keeps pumping, and the system tries the wanted device
/// again on a timer; the voices go on from where they were.
///
/// - Thread-safety: thread-compatible; the control calls (`Init`, `ApplySettings`, `Update`, `Shutdown` and the mixer's
///   control side) come from one thread, and the device's thread renders.
/// - Ownership: owns the mixer and the device.
class AudioSystem
{
public:
    /// Shuts the system down (stops and closes the device).
    ///
    /// - Fails: never.
    ~AudioSystem();

    /// Opens the device and starts the mixer.
    ///
    /// If the wanted device cannot be opened the system starts on the null device (the state is `Fallback`) and
    /// retries.
    ///
    /// - Param config: the start-up configuration.
    /// - Param now_nanos: the current time for the retry timer (`os::MonotonicTimeNanos`).
    /// - Returns: `Ok`; `InvalidConfig` or `OutOfMemory` from the mixer.
    /// - Fails: reported by return value.
    DeviceStatus Init(const AudioSystemConfig& config, u64 now_nanos);

    /// Stops the device and closes it.
    ///
    /// - Fails: never; safe twice.
    void Shutdown();

    /// Applies new settings without a restart, doing the cheapest thing that works.
    ///
    /// The master volume is a bus gain; a changed rate or channel count re-plans the mixer (voices keep their place;
    /// effects are bypassed on a rate or channel change, see `Mixer::Reconfigure`) and reopens the device; a changed
    /// device name or buffer size reopens the device. The voice limit and the resampler quality need a new mixer and
    /// are not applied live. If the reopened device fails the system falls back to the null device.
    ///
    /// - Param settings: the new settings.
    /// - Param now_nanos: the current time.
    /// - Param cost: receives what was done.
    /// - Returns: `Ok`, or why the wanted device could not be opened (the system is then in `Fallback`).
    /// - Fails: reported by return value.
    /// - Ownership: copies the settings; `cost` is borrowed for the call.
    DeviceStatus ApplySettings(const AudioSettings& settings, u64 now_nanos, ApplyCost* cost);

    /// The control thread's tick: notices a lost device, pumps the null device in `Fallback` for the time that passed
    /// and retries the wanted device when the timer says so.
    ///
    /// - Param now_nanos: the current time.
    /// - Fails: never.
    void Update(u64 now_nanos);

    /// The mixer: control calls (`Play`, `SetBusGain`, ...) go to it.
    ///
    /// - Returns: the mixer, valid from `Init` to `Shutdown`.
    Mixer& GetMixer() { return mixer_; }

    /// Where the output goes.
    ///
    /// - Returns: the state.
    OutputState State() const { return state_; }

    /// The device now open (the null device in `Fallback`).
    ///
    /// - Returns: the device, or null when closed.
    /// - Ownership: borrowed from the system; valid until the next switch.
    const AudioDevice* CurrentDevice() const { return device_.Get(); }

    /// The times a device has been opened or closed to switch (a leak check: it must not grow without a switch).
    ///
    /// - Returns: the count of switches.
    u32 Switches() const { return switches_; }

    /// The transitions logged so far (opened, lost, fell back, recovered), each once.
    ///
    /// - Returns: the count.
    u32 Transitions() const { return transitions_; }

private:
    static void RenderThunk(void* user, f32* out, u32 frames);
    static void ErrorThunk(void* user, DeviceStatus status);
    DeviceStatus OpenWanted(u64 now_nanos);
    void OpenFallback(u64 now_nanos);
    UniquePtr<AudioDevice> Make(DeviceKind kind);
    void LogTransition(const char* what);

    AudioSystemConfig config_;
    AudioSettings settings_;
    Mixer mixer_;
    UniquePtr<AudioDevice> device_;
    OutputState state_ = OutputState::Closed;
    bool mixer_ready_ = false;
    Atomic<bool> lost_{false};
    u64 last_update_ = 0;
    u64 next_retry_ = 0;
    u32 switches_ = 0;
    u32 transitions_ = 0;
};

}  // namespace vse::audio
