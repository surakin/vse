// The audio device interface (Phase 26 Day 419): the only per-platform seam of the audio module. The mixer renders into
// a callback; a device decides when that callback runs and where the frames go. The null device and the WAV-file device
// need no hardware, and a platform backend implements the same interface (BACKEND.md).
#pragma once

#include "audio/format.h"
#include "containers/unique_ptr.h"
#include "io/path.h"
#include "string_view.h"
#include "types.h"

namespace engine_placeholder::audio
{

/// Why a device call failed.
///
/// - Thread-safety: thread-compatible plain data.
enum class DeviceStatus : u8
{
    /// The call succeeded.
    Ok,
    /// The configuration is not valid (a rate or layout `IsValidFormat` refuses, zero or too many buffer frames).
    InvalidConfig,
    /// No device has the requested name.
    NotFound,
    /// The device exists but cannot run the configuration (a rate or layout it does not offer), or the call is not
    /// supported by this device kind (`Pump` on a hardware device).
    Unsupported,
    /// `Open` on a device that is already open.
    AlreadyOpen,
    /// A call that needs an open device on one that is not (or `Start` with no render callback).
    NotOpen,
    /// The device is in use by another program or already started.
    Busy,
    /// The device was unplugged or the system took it away; reported by the error callback too.
    Lost,
    /// Writing the output failed (the WAV-file device).
    IoError,
    /// The heap is exhausted.
    OutOfMemory,
};

/// How a device is asked to run.
///
/// - Thread-safety: thread-compatible plain data.
struct DeviceConfig
{
    /// The device name from `EnumerateDevices`; empty or `default` is the system default (or the only device).
    StringView name;
    /// The wanted sample rate in Hz; 0 takes the device's own rate. The granted rate is in `DeviceInfo`.
    u32 sample_rate = 48000;
    /// The wanted channel layout; the granted one is in `DeviceInfo`.
    ChannelLayout layout = ChannelLayout::Stereo;
    /// The wanted frames per render call, 1 to `kMaxDeviceBufferFrames`; a device may grant another size.
    u32 buffer_frames = 1024;
};

/// The most frames one render call may ask for.
inline constexpr u32 kMaxDeviceBufferFrames = 16384;

/// What a device granted.
///
/// - Thread-safety: thread-compatible plain data.
struct DeviceInfo
{
    /// The sample rate in Hz.
    u32 sample_rate = 0;
    /// The channel layout.
    ChannelLayout layout = ChannelLayout::Stereo;
    /// The frames of one render call at most (a device may call back with fewer).
    u32 buffer_frames = 0;
    /// Frames between the render call and the sound leaving the speaker (0 for devices with no output hardware).
    u32 latency_frames = 0;
};

/// The render callback: fills `frames` frames of interleaved `f32` samples in the granted layout.
///
/// It runs on the device's thread (the caller's thread for `Pump`). It must not allocate, lock or do I/O. `user` is the
/// pointer given to `AudioDevice::SetCallbacks`; `interleaved` holds `frames` times the channel count samples, to be
/// overwritten (not zeroed beforehand); `frames` is 1 to the granted buffer size.
using RenderCallback = void (*)(void* user, f32* interleaved, u32 frames);

/// The error callback: the device stopped on its own (`Lost`, `IoError`). It may run on any thread.
using DeviceErrorCallback = void (*)(void* user, DeviceStatus status);

/// An audio output device: a null clock, a file, or a platform's sound card.
///
/// Call order: `SetCallbacks`, `Open`, `Start`, `Stop`, `Close` (`Open` again after `Close`); `Stop` and `Close` are
/// safe to call twice. All calls come from one controlling thread; only the callbacks run elsewhere.
///
/// - Thread-safety: thread-compatible; the control calls must not overlap each other, and the callbacks run on the
/// device's thread.
/// - Ownership: owned by the caller through the `UniquePtr` the factories return; the callbacks' user pointer is
/// borrowed and must outlive `Stop`.
class AudioDevice
{
public:
    virtual ~AudioDevice() = default;

    /// Sets the callbacks; call before `Start`, not while running.
    ///
    /// - Param render: the render callback; not null for `Start` to succeed.
    /// - Param error: called when the device stops on its own; may be null.
    /// - Param user: passed back to both.
    /// - Ownership: `user` is borrowed and must outlive `Stop`.
    /// - Fails: never.
    virtual void SetCallbacks(RenderCallback render, DeviceErrorCallback error, void* user) = 0;

    /// Opens the device with `config`; the granted values are in `Info`.
    ///
    /// - Param config: the wanted rate, layout and buffer size.
    /// - Returns: `Ok` or why not.
    /// - Fails: `InvalidConfig`, `NotFound`, `Unsupported`, `AlreadyOpen`, `Busy`, `IoError`, `OutOfMemory`.
    /// - Backend: yes.
    virtual DeviceStatus Open(const DeviceConfig& config) = 0;

    /// Starts calling the render callback (a hardware device from its own thread; null and file devices wait for
    /// `Pump`).
    ///
    /// - Returns: `Ok` or why not.
    /// - Fails: `NotOpen` (not open, or no render callback), `Busy` (already started), `Lost`.
    /// - Backend: yes.
    virtual DeviceStatus Start() = 0;

    /// Stops the render calls and returns when the last one has finished. Safe to call twice or when not started.
    ///
    /// - Fails: never.
    /// - Backend: yes.
    virtual void Stop() = 0;

    /// Stops if needed and releases the device. Safe to call twice or when not open.
    ///
    /// - Returns: `Ok`, or `IoError` when the file device could not write its output.
    /// - Fails: `IoError`.
    /// - Backend: yes.
    virtual DeviceStatus Close() = 0;

    /// Returns what the device granted; valid after a successful `Open`.
    virtual DeviceInfo Info() const = 0;

    /// Whether the device is open.
    ///
    /// - Returns: true after a successful `Open` and before `Close`.
    /// - Fails: never.
    virtual bool IsOpen() const = 0;

    /// Whether `Start` succeeded and `Stop` has not been called.
    ///
    /// - Returns: true while the device renders.
    /// - Fails: never.
    virtual bool IsRunning() const = 0;

    /// Renders `frames` frames on the caller's thread through the callback (null and file devices; the offline and
    /// test route). Large counts are split into calls of at most the granted buffer size.
    ///
    /// - Param frames: the frames to render.
    /// - Returns: `Ok` or why not.
    /// - Fails: `NotOpen` (not started), `Unsupported` (a hardware device runs from its own thread), `IoError`,
    /// `OutOfMemory`.
    virtual DeviceStatus Pump(u64 frames) = 0;

    /// The frames rendered since `Open`: the clock of an on-demand device (a hardware device counts what it has asked
    /// for).
    ///
    /// - Returns: the frame count.
    /// - Fails: never.
    virtual u64 FramesRendered() const = 0;
};

/// The kinds of device `CreateDevice` makes.
enum class DeviceKind : u8
{
    /// Renders on demand and discards the samples; advances a clock the test reads.
    Null,
    /// Renders on demand and writes the samples to a WAV file (see `CreateWavFileDevice`).
    WavFile,
    /// The platform's sound output (PipeWire or ALSA, WASAPI, CoreAudio); the Null device on a platform with no
    /// backend yet.
    Platform,
};

/// Makes a device of `kind`. A `WavFile` device made here has no path and `Open` fails with `InvalidConfig`: use
/// `CreateWavFileDevice`.
///
/// - Param kind: which device.
/// - Returns: the device.
/// - Ownership: the caller owns the result.
/// - Fails: never.
UniquePtr<AudioDevice> CreateDevice(DeviceKind kind);

/// Makes a device that renders on demand and writes a 32-bit float WAV file to `path` on `Close`.
///
/// - Param path: the file to create or overwrite.
/// - Returns: the device.
/// - Ownership: the caller owns the result.
/// - Fails: never; `Close` reports `IoError` when the file cannot be written (a destroyed device writes on its own).
UniquePtr<AudioDevice> CreateWavFileDevice(const io::Path& path);

/// Whether `config` names a rate (or 0) and layout the module accepts and a buffer size in range.
///
/// - Param config: the configuration.
/// - Returns: true when `Open` need not refuse it as `InvalidConfig`.
/// - Fails: never.
bool IsValidDeviceConfig(const DeviceConfig& config);

}  // namespace engine_placeholder::audio
