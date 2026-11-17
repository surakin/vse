// Ogg Vorbis decoding (Phase 26 Day 432): the facade over the vendored libogg and libvorbis. It is the only file pair
// that includes the libraries' headers; the input is hostile (limits on size, pages, setup, steps and frames) and every
// failure is a `VorbisStatus`. Decoding runs on a job, never on the audio thread.
#pragma once

#include "containers/span.h"
#include "containers/unique_ptr.h"
#include "containers/vector.h"
#include "mem/heap_allocator.h"
#include "string/string.h"
#include "types.h"

namespace engine_placeholder::audio
{

/// The version of the vendored libvorbis, `major << 16 | minor << 8 | patch` (recorded in clips decoded at mint time,
/// so a decoder change is a visible re-mint).
inline constexpr u32 kVorbisLibraryVersion = 0x010307;

/// How a Vorbis call ended.
enum class VorbisStatus : u8
{
    /// The call did what it was asked.
    Ok,
    /// No bytes, or a limit that is zero.
    InvalidInput,
    /// The bytes are not an Ogg stream with a Vorbis first packet.
    NotVorbis,
    /// The headers end before the setup is complete.
    Truncated,
    /// A header is malformed.
    Corrupt,
    /// A valid stream the engine does not take: a channel count with no layout (3, 5, 7 or more than 8), a rate outside
    /// 8,000 to 384,000 Hz.
    Unsupported,
    /// A limit was exceeded: the input size, the page count, the setup size, the decoded frames or the step budget.
    LimitExceeded,
    /// The stream ended; no more frames.
    EndOfStream,
    /// The heap is exhausted.
    OutOfMemory,
};

/// The limits around the library: nothing in the stream may make a job run long or allocate without bound.
///
/// - Thread-safety: thread-compatible plain data.
struct VorbisLimits
{
    /// The most input bytes.
    u64 max_input_bytes = 256ull * 1024 * 1024;
    /// The most Ogg pages read in total.
    u32 max_pages = 4u * 1024 * 1024;
    /// The largest setup header packet (codebooks and floors) in bytes.
    u32 max_setup_bytes = 256u * 1024;
    /// The most frames decoded in total when the stream does not state its length (and the cap above the stated one).
    u64 max_frames = 1ull << 32;
    /// The most library calls one `Decode` makes (so a stream that never yields audio cannot hang a job).
    u32 max_steps = 1u << 20;
};

/// What the headers say.
///
/// - Thread-safety: thread-compatible plain data.
struct VorbisInfo
{
    /// The channel count: 1, 2, 4, 6 or 8.
    u32 channels = 0;
    /// The sample rate in Hz.
    u32 sample_rate = 0;
    /// The nominal bitrate in bits per second (0 when the stream gives none).
    u32 nominal_bitrate = 0;
    /// The length in frames from the last page's granule position, or 0 when the stream does not say.
    u64 total_frames = 0;
    /// The loop start in frames from the `LOOPSTART` comment (0 with `loop_end` when there is no valid loop).
    u64 loop_start = 0;
    /// The loop end in frames from the `LOOPEND` comment; 0 when absent or not `loop_start < loop_end <= total_frames`.
    u64 loop_end = 0;
};

/// A Vorbis decoder over bytes in memory.
///
/// The output is interleaved `f32` in the engine's channel order (`ChannelLayout`: Vorbis order is mapped by name for 6
/// and 8 channels).
///
/// - Thread-safety: thread-compatible; one thread at a time.
/// - Ownership: borrows the input bytes, which must outlive the decoder; owns the library state.
class VorbisDecoder
{
public:
    /// An empty decoder: use `Open`, which makes the usable ones (public only so `MakeUnique` can build it).
    ///
    /// - Fails: never.
    VorbisDecoder();

    /// Releases the library state.
    ///
    /// - Fails: never.
    ~VorbisDecoder();

    VorbisDecoder(const VorbisDecoder&) = delete;
    VorbisDecoder& operator=(const VorbisDecoder&) = delete;

    /// Parses the three headers and makes a decoder.
    ///
    /// - Param bytes: the whole Ogg stream; borrowed.
    /// - Param limits: the limits.
    /// - Param out: receives the decoder on `Ok`.
    /// - Returns: `Ok`, or why the stream was refused.
    /// - Fails: reported by return value; never aborts on bad bytes.
    /// - Ownership: `bytes` is borrowed and must outlive `*out`.
    static VorbisStatus Open(ByteSpan bytes, const VorbisLimits& limits, UniquePtr<VorbisDecoder>* out);

    /// What the headers said.
    ///
    /// - Returns: the info.
    const VorbisInfo& Info() const { return info_; }

    /// The comment strings as `name=value` lines (the names as the stream spells them), one per line, plus the vendor
    /// string as the first line. They come from the stream and are never trusted (at most 64 lines of 1,024 bytes are
    /// kept).
    ///
    /// - Returns: the text.
    const String<HeapAllocator>& Tags() const { return tags_; }

    /// The frames decoded so far.
    ///
    /// - Returns: the position in frames.
    u64 Position() const { return position_; }

    /// The packets the library refused (damaged data skipped) so far.
    ///
    /// - Returns: the count.
    u32 SkippedPackets() const { return skipped_; }

    /// Decodes up to `max_frames` frames into `out`.
    ///
    /// - Param out: `max_frames * channels` samples.
    /// - Param max_frames: the capacity in frames.
    /// - Param frames: receives the frames written (0 at the end).
    /// - Returns: `Ok` (with at least one frame), `EndOfStream` (none), `LimitExceeded` or `OutOfMemory`.
    /// - Fails: reported by return value.
    /// - Ownership: `out` is borrowed for the call.
    VorbisStatus Decode(f32* out, u32 max_frames, u32* frames);

    /// Moves the decode position to `frame`: the output after the seek equals what decoding from the start gives from
    /// that frame on. The first call reads the page index of the whole input (once, bounded by the page limit); a seek
    /// then bisects it and decodes at most one page plus the frames up to the target.
    ///
    /// - Param frame: the target in frames; at or past the end of the stream the decoder is left at its end.
    /// - Returns: `Ok`, `EndOfStream` (the target is at or past the end), `Corrupt` (the stream does not match its own
    /// index), `LimitExceeded` or `OutOfMemory`.
    /// - Fails: reported by return value; the position is then undefined until the next successful seek.
    VorbisStatus SeekFrame(u64 frame);

    /// Turns the loop on or off: with a loop on, `Decode` wraps from `loop_end` to `loop_start` by seeking (the frames
    /// of the codec's own lapping are decoded and discarded, so the join has no discontinuity beyond the stream's own).
    ///
    /// - Param on: whether to loop; ignored when the stream has no valid loop points.
    void SetLooping(bool on) { looping_ = on; }

    /// Whether the loop is on.
    ///
    /// - Returns: the flag.
    bool Looping() const { return looping_; }

private:
    struct State;

    VorbisInfo info_;
    String<HeapAllocator> tags_;
    UniquePtr<State> state_;
    u64 position_ = 0;
    u32 skipped_ = 0;
    bool looping_ = false;
};

}  // namespace engine_placeholder::audio
