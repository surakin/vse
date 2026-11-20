// Streaming sources (Phase 26 Day 435): long clips played without loading them. A `StreamSource` gives frames in order
// (memory, Ogg Vorbis, a WAV file read through the file layer), a `StreamBuffer` keeps a ring of them filled by a job
// (never the audio thread) and a mixer voice reads the ring. The audio thread never waits: a shortfall plays silence
// and is counted as an underrun.
#pragma once

#include "audio/clip.h"
#include "containers/shared_bytes.h"
#include "containers/shared_ptr.h"
#include "containers/unique_ptr.h"
#include "containers/vector.h"
#include "io/file.h"
#include "sync/atomic.h"
#include "types.h"

namespace vse::jobs
{
class Scheduler;
class Counter;
}  // namespace vse::jobs

namespace vse::audio
{

/// How a stream call ended.
enum class StreamStatus : u8
{
    /// The call did what it was asked.
    Ok,
    /// An argument is not valid (no source, a rate or channel count the engine does not take, a bad capacity).
    Invalid,
    /// A valid stream the engine does not stream (a WAV encoding other than 16-bit PCM or 32-bit float).
    Unsupported,
    /// The file could not be read or is not what it says.
    Io,
    /// The heap is exhausted.
    OutOfMemory,
};

/// Frames in order, from somewhere. A source is read by one thread at a time (the refill job).
///
/// - Thread-safety: thread-compatible; one thread at a time.
/// - Ownership: owned by the [[audio::StreamBuffer]] it feeds.
class StreamSource
{
public:
    virtual ~StreamSource() = default;

    /// The channel count: 1, 2, 4, 6 or 8.
    ///
    /// - Returns: the count.
    virtual u32 Channels() const = 0;

    /// The sample rate in Hz.
    ///
    /// - Returns: the rate.
    virtual u32 SampleRate() const = 0;

    /// The length in frames, or 0 when not known.
    ///
    /// - Returns: the frames.
    virtual u64 Length() const = 0;

    /// The loop start in frames (a loop exists when `LoopEnd` is above it), else 0.
    ///
    /// - Returns: the frame.
    virtual u64 LoopStart() const = 0;

    /// The loop end in frames, 0 when the stream has no loop points.
    ///
    /// - Returns: the frame.
    virtual u64 LoopEnd() const = 0;

    /// Reads up to `frames` interleaved `f32` frames.
    ///
    /// - Param out: `frames * Channels()` samples.
    /// - Param frames: the capacity in frames.
    /// - Returns: the frames written; 0 at the end of the stream (or on an unreadable one).
    /// - Fails: reported by returning 0.
    /// - Ownership: `out` is borrowed for the call.
    virtual u32 Read(f32* out, u32 frames) = 0;

    /// Moves to `frame` (at or past the end is the end).
    ///
    /// - Param frame: the position in frames.
    /// - Returns: false when the source could not move.
    virtual bool Seek(u64 frame) = 0;
};

/// Makes a source over a clip's samples (a test double and the way to stream what is already in memory).
///
/// - Param clip: the clip; shared.
/// - Param out: receives the source.
/// - Returns: `Ok` or `Invalid`.
/// - Fails: reported by return value.
/// - Ownership: the source shares the clip.
StreamStatus MakeMemoryStream(const AudioClipConstPtr& clip, UniquePtr<StreamSource>* out);

/// Makes a source decoding Ogg Vorbis bytes (the facade of `audio/vorbis.h`); the stream's loop tags are its loop
/// points.
///
/// - Param bytes: the whole Ogg stream; shared.
/// - Param out: receives the source.
/// - Returns: `Ok`, `Invalid` (not a stream the facade takes) or `OutOfMemory`.
/// - Fails: reported by return value.
/// - Ownership: the source shares `bytes`.
StreamStatus MakeVorbisStream(const SharedBytesPtr& bytes, UniquePtr<StreamSource>* out);

/// Makes a source reading a RIFF/WAVE file through the file layer: 16-bit PCM or 32-bit float, any engine channel
/// count; only the header is read now, the samples as the refill job asks.
///
/// - Param file: an open, readable file; moved into the source.
/// - Param out: receives the source.
/// - Returns: `Ok`, `Invalid` (not a WAV), `Unsupported` (another encoding), `Io` or `OutOfMemory`.
/// - Fails: reported by return value.
/// - Ownership: the source owns `file`.
StreamStatus MakeWavFileStream(io::FilePtr file, UniquePtr<StreamSource>* out);

/// What a stream buffer is made of.
///
/// - Thread-safety: thread-compatible plain data.
struct StreamConfig
{
    /// The rate the buffer delivers (the mixer's); a source of another rate is resampled by the refill job.
    u32 sample_rate = 48000;
    /// The ring's size in frames.
    u32 capacity_frames = 8192;
    /// The most frames one read of the source asks for.
    u32 chunk_frames = 1024;
    /// A buffer with fewer frames than this wants a refill.
    u32 low_water_frames = 4096;
    /// A number that names the stream's clip (`stream<id>`) so a snapshot can tell streams apart; 0 names it `stream`.
    u32 id = 0;
};

/// What a push stream is made of (Maintenance 2026-11-18, live voices): a stream the host feeds with `Push` as its
/// audio arrives, with no source object and no refill job in between.
///
/// - Thread-safety: thread-compatible plain data.
struct PushStreamConfig
{
    /// The rate of the frames pushed: the mixer's (there is no resampling).
    u32 sample_rate = 48000;
    /// The channel count: 1, 2, 4, 6 or 8.
    u32 channels = 1;
    /// The most frames kept: a push past it makes the oldest frames go (counted). 64 to 480000.
    u32 capacity_frames = 9600;
    /// Frames to collect before the voice plays, and again after it ran dry: a jitter buffer. 0 plays what is there;
    /// at most `capacity_frames`. The delay a voice adds to the audio is about this many frames.
    u32 prefill_frames = 0;
    /// A number that names the stream's clip, as `StreamConfig::id`.
    u32 id = 0;
};

/// What a push stream counted.
///
/// - Thread-safety: thread-compatible plain data.
struct PushStreamStats
{
    u64 frames_pushed = 0;   ///< Frames accepted by `Push`.
    u64 frames_dropped = 0;  ///< Frames lost to a full ring (the oldest) or to a second concurrent pusher.
    u64 underruns = 0;       ///< Mixer blocks that found fewer frames than they needed.
    u32 buffered = 0;        ///< Frames waiting now.
};

class StreamBuffer;
/// A shared handle to a stream buffer: the mixer voice, the pump and the caller hold one.
using StreamBufferPtr = SharedPtr<StreamBuffer>;

/// A ring of decoded frames between a refill job and one mixer voice.
///
/// The consumer (the audio thread) calls `Pop`, `Ended`, `Flush` and `SetLoop`; any one thread at a time calls `Refill`
/// (a second caller returns at once). A flush bumps an epoch: frames written for an older epoch are never read. The
/// samples a stream produces depend only on the source and the flushes, never on when `Refill` ran; only whether a
/// block found them in time does (an underrun).
///
/// - Thread-safety: thread-compatible; one consumer thread and one producer at a time, as above.
/// - Ownership: owns the source and the ring.
class StreamBuffer
{
public:
    /// Makes a buffer; nothing is read yet (call `Refill` before the voice starts).
    ///
    /// - Param source: the source; moved in.
    /// - Param config: the sizes and the delivered rate.
    /// - Param out: receives the buffer.
    /// - Returns: `Ok`, `Invalid` or `OutOfMemory`.
    /// - Fails: reported by return value.
    /// - Ownership: the buffer owns `source`.
    static StreamStatus Create(UniquePtr<StreamSource> source, const StreamConfig& config, StreamBufferPtr* out);

    /// Makes a push stream: frames arrive through `Push` from any one thread at a time and are read by the mixer
    /// voice, with no job in between. The ring keeps the newest `capacity_frames`; the voice never finishes.
    ///
    /// - Param config: the sizes.
    /// - Param out: receives the buffer.
    /// - Returns: `Ok`, `Invalid` (a rate, channel count, capacity or prefill out of range) or `OutOfMemory`.
    /// - Fails: reported by return value.
    /// - Ownership: the buffer owns its ring; nothing is borrowed.
    static StreamStatus CreatePush(const PushStreamConfig& config, StreamBufferPtr* out);

    /// Whether this is a push stream.
    ///
    /// - Returns: the flag.
    bool IsPush() const { return push_; }

    /// Appends interleaved frames to a push stream. When the ring is full the oldest frames are dropped and counted
    /// (a block the mixer is reading right now makes the newest of the call be dropped instead, also counted). A
    /// second concurrent call returns 0 at once and counts the frames as dropped.
    ///
    /// - Param data: `frames * Channels()` finite samples (a non-finite one is stored as silence).
    /// - Param frames: the frame count.
    /// - Returns: the frames accepted.
    /// - Thread-safety: thread-compatible; any thread, one at a time; it allocates nothing and does not wait for the
    /// mixer.
    /// - Ownership: `data` is borrowed for the call.
    u32 Push(const f32* data, u32 frames);

    /// Like `Push`, with 16-bit samples (scaled by 1/32768).
    ///
    /// - Param data: `frames * Channels()` samples.
    /// - Param frames: the frame count.
    /// - Returns: the frames accepted.
    /// - Thread-safety: thread-compatible; as `Push`.
    /// - Ownership: `data` is borrowed for the call.
    u32 PushI16(const i16* data, u32 frames);

    /// Closes a push stream: later pushes are refused and the voice that plays it ends (as a stream that reached its
    /// end does). Other streams are unchanged.
    ///
    /// - Thread-safety: thread-safe.
    void Close() { push_closed_.Store(true, MemoryOrder::Release); }

    /// The counters of a push stream.
    ///
    /// - Returns: the counts (zero for another stream).
    /// - Thread-safety: thread-safe.
    PushStreamStats PushStats() const;

    /// An empty buffer: use `Create` (public only so `MakeShared` can build it).
    ///
    /// - Fails: never.
    StreamBuffer() = default;

    /// Releases the ring and the source.
    ///
    /// - Fails: never.
    ~StreamBuffer();
    StreamBuffer(const StreamBuffer&) = delete;
    StreamBuffer& operator=(const StreamBuffer&) = delete;

    /// The clip a mixer voice names (one frame in the delivered format): `VoiceDesc::clip` is this and
    /// `VoiceDesc::stream` the buffer.
    ///
    /// - Returns: the stand-in clip.
    AudioClipConstPtr Clip() const { return clip_; }

    /// The channel count of the frames delivered.
    ///
    /// - Returns: the count.
    u32 Channels() const { return channels_; }

    /// The length in delivered frames, 0 when the source does not know.
    ///
    /// - Returns: the frames.
    u64 Length() const { return length_; }

    /// Takes up to `frames` frames, interleaved.
    ///
    /// - Param out: `frames * Channels()` samples.
    /// - Param frames: the capacity in frames.
    /// - Returns: the frames written; fewer than asked when the ring is empty, which counts one underrun unless the
    /// stream has ended.
    /// - Thread-safety: thread-compatible; the consumer thread only, and it allocates nothing and locks nothing.
    /// - Ownership: `out` is borrowed for the call.
    u32 Pop(f32* out, u32 frames);

    /// Whether the stream has ended and every frame has been taken.
    ///
    /// - Returns: the flag.
    /// - Thread-safety: thread-compatible; the consumer thread only.
    bool Ended() const;

    /// Drops everything buffered and asks the refill to continue from `frame` (delivered frames).
    ///
    /// - Param frame: the position.
    /// - Thread-safety: thread-compatible; the consumer thread only.
    void Flush(u64 frame);

    /// Turns the loop on or off: at its loop points (or the end) the source moves back to its loop start.
    ///
    /// - Param on: whether to loop.
    void SetLoop(bool on) { looping_.Store(on, MemoryOrder::Release); }

    /// Fills the ring from the source up to the free space; callable from a job. If another call is running, returns 0.
    ///
    /// - Returns: the frames added.
    /// - Thread-safety: thread-compatible; any thread, one at a time.
    u32 Refill();

    /// Whether the ring holds fewer frames than the low-water mark and the stream has not ended.
    ///
    /// - Returns: the flag.
    bool NeedsRefill() const;

    /// The frames in the ring.
    ///
    /// - Returns: the count.
    u32 Buffered() const;

    /// The blocks that found the ring short.
    ///
    /// - Returns: the count.
    u64 Underruns() const { return underruns_.Load(MemoryOrder::Relaxed); }

private:
    static constexpr u64 kCountMask = (u64{1} << 48) - 1;

    UniquePtr<StreamSource> source_;
    AudioClipConstPtr clip_;
    Vector<f32> ring_;
    u32 channels_ = 0;
    u32 capacity_ = 0;
    u32 chunk_ = 0;
    u32 low_water_ = 0;
    u64 length_ = 0;
    u64 loop_start_ = 0;
    u64 loop_end_ = 0;
    // The producer's own: the epoch it has applied and the source position in delivered frames.
    u64 seen_epoch_ = 0;
    u64 position_ = 0;
    bool ended_local_ = false;
    Atomic<bool> busy_{false};
    // epoch << 48 | frames written in that epoch; only the producer advances the count, only the consumer the epoch.
    Atomic<u64> state_{0};
    Atomic<u64> read_count_{0};
    Atomic<u64> seek_target_{0};
    // epoch + 1 once the producer reached the end in that epoch, else 0.
    Atomic<u64> ended_epoch_{0};
    Atomic<bool> looping_{false};
    Atomic<u64> underruns_{0};

    // Push mode: a ring of `push_ring_` frames (the newest `push_limit_` are kept, the rest is room for a push in
    // flight). `written` and `claim` are what the pusher and the mixer tell each other: the mixer announces where its
    // current read starts, the pusher never writes over it.
    static constexpr u64 kNoClaim = ~u64{0};
    u32 PopPush(f32* out, u32 frames);
    bool push_ = false;
    u32 push_limit_ = 0;
    u32 push_ring_ = 0;
    u32 push_chunk_ = 0;
    u32 push_prefill_ = 0;
    bool push_primed_ = false;  // the mixer's own
    Atomic<bool> push_busy_{false};
    Atomic<bool> push_closed_{false};
    Atomic<u64> push_written_{0};
    Atomic<u64> push_read_{0};
    Atomic<u64> push_claim_{kNoClaim};
    Atomic<u64> push_pushed_{0};
    Atomic<u64> push_dropped_{0};
};

/// Keeps stream buffers filled from jobs on the scheduler.
///
/// - Thread-safety: owner-thread-only (the main thread, outside any job).
/// - Ownership: holds shares of the buffers it was given.
class StreamPump
{
public:
    /// Makes a pump on a scheduler.
    ///
    /// - Param scheduler: the scheduler; must outlive the pump.
    explicit StreamPump(jobs::Scheduler& scheduler);

    /// Waits for the refill jobs in flight, then releases the pump.
    ///
    /// - Fails: never.
    ~StreamPump();
    StreamPump(const StreamPump&) = delete;
    StreamPump& operator=(const StreamPump&) = delete;

    /// Adds a buffer to keep filled.
    ///
    /// - Param buffer: the buffer; shared.
    void Add(const StreamBufferPtr& buffer);

    /// Stops keeping a buffer filled and waits for the refill jobs in flight, so the buffer's source is not read after
    /// the call returns.
    ///
    /// - Param buffer: the buffer given to `Add`; one that was never added is ignored.
    /// - Thread-safety: owner-thread-only, as `Add`.
    void Remove(const StreamBufferPtr& buffer);

    /// Starts a refill job for every buffer below its low-water mark and not being refilled; returns at once.
    ///
    /// - Returns: the jobs started.
    u32 Update();

    /// Runs `Update` and waits for the jobs it started (the offline mode: nothing the audio thread reads is late).
    ///
    /// - Returns: the jobs run.
    u32 Run();

private:
    jobs::Scheduler* scheduler_;
    Vector<StreamBufferPtr> buffers_;
    UniquePtr<jobs::Counter> counter_;
};

}  // namespace vse::audio
