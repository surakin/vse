// The audio clip (Phase 26 Day 416): decoded, immutable samples shared by every voice that plays them.
#pragma once

#include "audio/format.h"
#include "containers/shared_bytes.h"
#include "containers/shared_ptr.h"
#include "containers/span.h"
#include "string/string.h"
#include "types.h"

namespace vse::audio
{

class AudioClip;

/// A shared, mutable-by-its-owner handle to a decoded or minted clip; every voice that plays the clip holds one.
using AudioClipPtr = SharedPtr<AudioClip>;
/// A shared read-only handle to a clip.
using AudioClipConstPtr = SharedPtr<const AudioClip>;

/// Why a clip could not be made.
///
/// - Thread-safety: thread-compatible plain data.
enum class ClipStatus : u8
{
    /// The clip is valid.
    Ok,
    /// The format is not valid (rate or layout), or its sample format is not `F32`: a clip holds `f32`.
    BadFormat,
    /// The clip has no frames, or more than the module accepts.
    BadFrameCount,
    /// The bytes are too few for the frames, or the offset is past their end.
    TooShort,
    /// The samples do not start on a 4-byte boundary.
    Misaligned,
    /// The loop points are not `0 <= start < end <= frames`.
    BadLoop,
    /// A sample is a NaN or an infinity.
    NotFinite,
    /// The heap is exhausted.
    OutOfMemory,
};

/// The most frames a clip may have (a bound so that frame counts times channels fit comfortably: about 3.6 hours at 48
/// kHz).
inline constexpr u64 kMaxClipFrames = 1ull << 29;

/// What a clip is, apart from its samples.
///
/// - Thread-safety: thread-compatible plain data.
struct ClipDesc
{
    /// Rate and layout; `format.format` must be `F32`.
    AudioFormat format;
    /// The number of frames, at least 1.
    u64 frame_count = 0;
    /// Where a looping voice jumps back to, in frames; with `loop_end` 0 the clip has no loop.
    u64 loop_start = 0;
    /// Where a looping voice jumps from, in frames: `loop_start < loop_end <= frame_count`; 0 means the clip does not
    /// loop (a voice may still loop the whole clip).
    u64 loop_end = 0;
};

/// A validated, zero-copy view of interleaved `f32` frames in someone else's bytes: what the mixer reads and what a
/// minted blob maps to.
///
/// - Thread-safety: thread-compatible; reads only.
/// - Ownership: borrows the bytes; the owner (an [[audio::AudioClip]], a blob buffer) must outlive the view.
struct ClipView
{
    /// The description the view was validated against.
    ClipDesc desc;
    /// The samples: `frame_count * ChannelCount` of them, interleaved.
    Span<const f32> samples;

    /// The number of frames.
    u64 Frames() const { return desc.frame_count; }

    /// The number of channels.
    u32 Channels() const { return ChannelCount(desc.format.layout); }

    /// The samples of one frame.
    ///
    /// - Param frame: less than `Frames()`.
    /// - Returns: a span of `Channels()` samples.
    /// - Aborts: `frame` is not less than `Frames()` (the span's bounds check).
    Span<const f32> Frame(u64 frame) const
    {
        return samples.Subspan(static_cast<usize>(frame) * Channels(), Channels());
    }

    /// The length in seconds.
    ///
    /// - Returns: `Frames() / sample_rate`.
    f64 Duration() const { return static_cast<f64>(desc.frame_count) / static_cast<f64>(desc.format.sample_rate); }

    /// Whether the clip carries loop points.
    ///
    /// - Fails: never.
    bool HasLoop() const { return desc.loop_end != 0; }
};

/// Validates `desc` and `bytes` and makes a view over them; nothing is copied.
///
/// - Param desc: the description; `format.format` must be `F32`.
/// - Param bytes: the little-endian `f32` samples, interleaved, at least `frame_count * FrameBytes` long; the first
/// `frame_count * FrameBytes` bytes are used.
///   Their address must be 4-byte aligned.
/// - Param out: receives the view on success.
/// - Returns: `Ok`, or the first thing wrong.
/// - Fails: reported by return value; `out` is not written then.
/// - Thread-safety: thread-safe; pure. Scans every sample once for NaN and infinity.
/// - Ownership: the view borrows `bytes`.
ClipStatus MakeClipView(const ClipDesc& desc, ByteSpan bytes, ClipView* out);

/// A clip: the samples and what they are, held as `f32` so the mixer converts nothing at play time.
///
/// A 3-minute stereo clip at 48 kHz is 69 MB as `f32` (35 MB as 16-bit); the spec chose `f32` and long clips stream
/// instead (Day 435), so a clip above `kMaxClipFrames` is refused. The samples live in a [[SharedBytes]] and are named
/// by an offset, never by a pointer, so a clip over a blob shares the blob's storage. Clips are immutable once made.
///
/// - Thread-safety: thread-safe to read once made.
/// - Ownership: shares its bytes (`SharedBytesPtr`); shared by `AudioClipPtr`.
class AudioClip
{
public:
    /// Makes a clip over `bytes`, starting `offset` bytes in.
    ///
    /// - Param desc: the description; `format.format` must be `F32`.
    /// - Param bytes: the shared bytes; kept alive by the clip.
    /// - Param offset: where the samples start; the address `bytes + offset` must be 4-byte aligned.
    /// - Param name: a name for logs and tools; may be empty.
    /// - Param out: receives the clip on success.
    /// - Returns: `Ok`, or what is wrong.
    /// - Fails: reported by return value; `out` is not written then.
    /// - Thread-safety: thread-safe.
    /// - Ownership: `out` shares the new clip, which shares `bytes`.
    static ClipStatus Create(const ClipDesc& desc, SharedBytesPtr bytes, usize offset, StringView name,
                             AudioClipPtr* out);

    /// Makes a clip from a copy of `samples`.
    ///
    /// - Param desc: the description; `format.format` must be `F32`.
    /// - Param samples: `frame_count * ChannelCount` interleaved samples; read during the call only.
    /// - Param name: a name; may be empty.
    /// - Param out: receives the clip on success.
    /// - Returns: `Ok`, or what is wrong; `TooShort` if `samples` has fewer than the frames need.
    /// - Fails: reported by return value; `out` is not written then.
    /// - Thread-safety: thread-safe.
    /// - Ownership: the clip owns its copy.
    static ClipStatus CreateCopy(const ClipDesc& desc, Span<const f32> samples, StringView name, AudioClipPtr* out);

    /// The view the mixer reads, rebuilt from the shared bytes and the offset (no validation, no copy).
    ///
    /// - Returns: a view that stays valid while this clip is.
    /// - Ownership: borrowed from this clip.
    ClipView View() const;

    /// The description.
    const ClipDesc& Desc() const { return desc_; }

    /// The format: the rate and layout of the description with the sample format `F32`.
    AudioFormat Format() const { return desc_.format; }

    /// The number of frames.
    u64 Frames() const { return desc_.frame_count; }

    /// The length in seconds.
    f64 Duration() const { return static_cast<f64>(desc_.frame_count) / static_cast<f64>(desc_.format.sample_rate); }

    /// The bytes the samples take.
    u64 ByteSize() const { return desc_.frame_count * FrameBytes(desc_.format); }

    /// The name given at creation.
    StringView Name() const { return name_.View(); }

    /// The shared bytes (so a loader can tell whether two clips share storage).
    ///
    /// - Ownership: borrowed from this clip.
    const SharedBytesPtr& Bytes() const { return bytes_; }

    /// Where the samples start in `Bytes()`.
    usize Offset() const { return offset_; }

    AudioClip(const AudioClip&) = delete;
    AudioClip& operator=(const AudioClip&) = delete;

    /// Used by `MakeShared`; call [[audio::AudioClip::Create]] instead.
    AudioClip(const ClipDesc& desc, SharedBytesPtr bytes, usize offset, StringView name);

private:
    ClipDesc desc_;
    SharedBytesPtr bytes_;
    usize offset_;
    String<HeapAllocator> name_;
};

}  // namespace vse::audio
