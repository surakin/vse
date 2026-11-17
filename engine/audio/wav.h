// The WAV parser, decoder and frame reader (Phase 26 Day 417). WAV files come from outside the engine: every size is
// checked against the bytes that remain before it is used, counts are capped, and nothing aborts on bad bytes (failure
// is a `WavStatus`).
#pragma once

#include "audio/clip.h"
#include "audio/format.h"
#include "containers/span.h"
#include "containers/vector.h"
#include "string/string.h"
#include "types.h"

namespace engine_placeholder::audio
{

/// Why a WAV file was refused.
///
/// - Thread-safety: thread-compatible plain data.
enum class WavStatus : u8
{
    /// The file parsed.
    Ok,
    /// Fewer bytes than a RIFF header.
    TooShort,
    /// The file does not start with `RIFF` (or `RF64`, which is refused as `Unsupported`).
    NotRiff,
    /// The RIFF form is not `WAVE`.
    NotWave,
    /// There is no `fmt ` chunk before the end of the file.
    NoFmt,
    /// There is no `data` chunk.
    NoData,
    /// The `fmt ` chunk is too short or inconsistent (block align, bytes per second, bits).
    BadFmt,
    /// A valid WAV the module does not play: another format tag, a bit depth, a channel count with no layout, a rate
    /// out of range, RF64.
    Unsupported,
    /// A chunk that must fit in the file does not.
    BadChunk,
    /// The `data` chunk has no whole frame, or more frames than a clip may have.
    BadFrameCount,
    /// The decoded samples contain a NaN or an infinity, or the clip could not be made.
    BadSamples,
    /// The heap is exhausted.
    OutOfMemory,
};

/// How the samples are stored in the file (`SampleFormat` has no 64-bit float).
///
/// - Thread-safety: thread-compatible plain data.
enum class WavEncoding : u8
{
    /// Unsigned 8-bit PCM.
    U8,
    /// Signed 16-bit PCM.
    S16,
    /// Signed 24-bit PCM, packed.
    S24,
    /// Signed 32-bit PCM.
    S32,
    /// 32-bit IEEE float.
    F32,
    /// 64-bit IEEE float.
    F64,
};

/// A cue marker.
///
/// - Thread-safety: thread-compatible plain data.
struct WavCue
{
    /// The cue's identifier.
    u32 id = 0;
    /// Its position in frames.
    u32 frame = 0;
};

/// The most cue points a [[audio::WavInfo]] keeps (more are counted and dropped).
inline constexpr u32 kMaxWavCues = 256;
/// The most frames a WAV may hold (the clip limit).
inline constexpr u32 kMaxWavFrames = 1u << 29;

/// What the parser found.
///
/// - Thread-safety: thread-compatible.
/// - Ownership: owns the cue list; borrows nothing.
struct WavInfo
{
    /// How the samples are stored.
    WavEncoding encoding = WavEncoding::S16;
    /// The rate and layout; `format.format` is the nearest `SampleFormat` (`F32` for a 64-bit float file).
    AudioFormat format;
    /// The channel mask of an extensible file, 0 otherwise.
    u32 channel_mask = 0;
    /// Where the first frame is in the file.
    usize data_offset = 0;
    /// The bytes of whole frames the file has (a truncated `data` chunk gives the bytes present).
    usize data_bytes = 0;
    /// The bytes of one frame.
    u32 frame_bytes = 0;
    /// The whole frames in the file.
    u64 frame_count = 0;
    /// True when the `data` chunk claimed more bytes than the file has (a truncated recording); the bytes present are
    /// used.
    bool truncated = false;
    /// True when a `smpl` loop was read.
    bool has_loop = false;
    /// The loop start in frames.
    u32 loop_start = 0;
    /// The loop end in frames, exclusive (the file's end point is inclusive).
    u32 loop_end = 0;
    /// The cue markers, up to `kMaxWavCues`.
    Vector<WavCue> cues;
    /// True when a loop or cue was ignored because it pointed outside the data or the chunk was malformed.
    bool ignored_metadata = false;
};

/// Parses a RIFF/WAVE file: `fmt ` (PCM 8, 16, 24, 32-bit; float 32 and 64-bit; extensible with the channel mask),
/// `data`, and the optional `smpl` loop and `cue ` markers. Chunks are padded to even sizes; unknown chunks are
/// skipped.
///
/// - Param bytes: the whole file.
/// - Param info: receives what was found on success; left unchanged otherwise.
/// - Returns: `Ok` or why not.
/// - Fails: reported by return value; never aborts on any input.
/// - Thread-safety: thread-safe.
/// - Ownership: `info` owns copies; `bytes` is read during the call only.
WavStatus ParseWav(ByteSpan bytes, WavInfo* info);

/// Parses and decodes a whole file into a clip of `f32` frames.
///
/// - Param bytes: the whole file.
/// - Param name: the clip's name.
/// - Param clip: receives the clip on success.
/// - Param info: receives the parse result; may be null.
/// - Returns: `Ok` or why not.
/// - Fails: reported by return value.
/// - Thread-safety: thread-safe.
/// - Ownership: the clip owns its decoded samples; `bytes` is read during the call only.
WavStatus DecodeWav(ByteSpan bytes, StringView name, AudioClipPtr* clip, WavInfo* info);

/// Reads frames of a parsed WAV in order and by position, converting each to `f32`: the reader of streaming sources
/// (Day 435).
///
/// - Thread-safety: thread-compatible; one reader belongs to one thread.
/// - Ownership: borrows the file's bytes; they must outlive the reader.
class WavReader
{
public:
    /// Parses `bytes` and positions at frame 0.
    ///
    /// - Param bytes: the whole file; borrowed.
    /// - Returns: `Ok` or why not.
    /// - Fails: reported by return value.
    WavStatus Open(ByteSpan bytes);

    /// What the file holds.
    const WavInfo& Info() const { return info_; }

    /// The next frame to read.
    u64 Position() const { return position_; }

    /// Moves to a frame.
    ///
    /// - Param frame: at most `Info().frame_count` (the end).
    /// - Returns: false (and does not move) if past the end.
    bool Seek(u64 frame);

    /// Reads up to `max_frames` frames as interleaved `f32`.
    ///
    /// - Param out: receives frames; at least `max_frames * channels` samples.
    /// - Param max_frames: the most to read.
    /// - Returns: the frames read; 0 at the end or if `out` is too small. Allocates nothing.
    /// - Fails: reported by return value.
    usize ReadFrames(Span<f32> out, usize max_frames);

private:
    ByteSpan bytes_;
    WavInfo info_;
    u64 position_ = 0;
};

}  // namespace engine_placeholder::audio
