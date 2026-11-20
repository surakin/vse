// Sample formats, channel layouts and audio formats (Phase 26 Day 416): the vocabulary every later day of the audio
// module uses.
#pragma once

#include "types.h"

namespace vse::audio
{

/// The lowest sample rate an [[audio::AudioFormat]] may have, in Hz.
inline constexpr u32 kMinSampleRate = 8000;
/// The highest sample rate an [[audio::AudioFormat]] may have, in Hz.
inline constexpr u32 kMaxSampleRate = 384000;
/// The most channels any layout has.
inline constexpr u32 kMaxChannels = 8;

/// How one sample is stored.
///
/// - Thread-safety: thread-compatible plain data.
enum class SampleFormat : u8
{
    /// Unsigned 8-bit, 128 is silence.
    U8,
    /// Signed 16-bit, little-endian.
    S16,
    /// Signed 24-bit, little-endian, packed in three bytes.
    S24,
    /// Signed 32-bit, little-endian.
    S32,
    /// 32-bit float, nominally in [-1, 1].
    F32,
};

/// The speaker a channel feeds.
///
/// - Thread-safety: thread-compatible plain data.
enum class Speaker : u8
{
    /// Front left.
    FrontLeft,
    /// Front right.
    FrontRight,
    /// Front center (the only channel of a mono layout).
    FrontCenter,
    /// Low-frequency effects.
    Lfe,
    /// Back left.
    BackLeft,
    /// Back right.
    BackRight,
    /// Side left.
    SideLeft,
    /// Side right.
    SideRight,
};

/// Which channels a frame has and in which order (the order of a WAV file's channel mask).
///
/// - Thread-safety: thread-compatible plain data.
enum class ChannelLayout : u8
{
    /// One channel: front center.
    Mono,
    /// Front left, front right.
    Stereo,
    /// Front left, front right, back left, back right.
    Quad,
    /// Front left, front right, front center, LFE, back left, back right.
    Surround51,
    /// Front left, front right, front center, LFE, back left, back right, side left, side right.
    Surround71,
};

/// What a stream of samples is: the rate, the layout and how a sample is stored.
///
/// - Thread-safety: thread-compatible plain data.
struct AudioFormat
{
    /// Frames per second.
    u32 sample_rate = 48000;
    /// The channels of a frame.
    ChannelLayout layout = ChannelLayout::Stereo;
    /// How one sample is stored.
    SampleFormat format = SampleFormat::F32;
};

/// The bytes one sample takes.
///
/// - Param format: the sample format.
/// - Returns: 1, 2, 3, 4 or 4.
/// - Thread-safety: thread-safe; pure.
u32 BytesPerSample(SampleFormat format);

/// The number of channels of a layout.
///
/// - Param layout: the layout.
/// - Returns: 1, 2, 4, 6 or 8.
/// - Thread-safety: thread-safe; pure.
u32 ChannelCount(ChannelLayout layout);

/// The speaker of one channel of a layout.
///
/// - Param layout: the layout.
/// - Param channel: the channel index, less than `ChannelCount(layout)`.
/// - Returns: the speaker.
/// - Aborts: `channel` is out of range.
/// - Thread-safety: thread-safe; pure.
Speaker SpeakerOf(ChannelLayout layout, u32 channel);

/// The short name of a speaker: `FL`, `FR`, `FC`, `LFE`, `BL`, `BR`, `SL`, `SR`.
///
/// - Param speaker: the speaker.
/// - Returns: a static string.
/// - Thread-safety: thread-safe; pure.
/// - Ownership: static; the caller does not free it.
const char* SpeakerName(Speaker speaker);

/// The short name of one channel of a layout (`SpeakerName` of its speaker).
///
/// - Param layout: the layout.
/// - Param channel: the channel index, less than `ChannelCount(layout)`.
/// - Returns: a static string.
/// - Aborts: `channel` is out of range.
/// - Thread-safety: thread-safe; pure.
/// - Ownership: static; the caller does not free it.
const char* ChannelName(ChannelLayout layout, u32 channel);

/// The name of a layout: `mono`, `stereo`, `quad`, `5.1`, `7.1`.
///
/// - Param layout: the layout.
/// - Returns: a static string.
/// - Thread-safety: thread-safe; pure.
/// - Ownership: static; the caller does not free it.
const char* LayoutName(ChannelLayout layout);

/// The bytes one frame (one sample of every channel) takes.
///
/// - Param format: the format.
/// - Returns: `BytesPerSample * ChannelCount`.
/// - Thread-safety: thread-safe; pure.
u32 FrameBytes(const AudioFormat& format);

/// Whether a format is one the module accepts: a rate from `kMinSampleRate` to `kMaxSampleRate` and a known layout and
/// sample format.
///
/// - Param format: the format.
/// - Returns: true if valid.
/// - Fails: never; reports by return value.
/// - Thread-safety: thread-safe; pure.
bool IsValid(const AudioFormat& format);

}  // namespace vse::audio
