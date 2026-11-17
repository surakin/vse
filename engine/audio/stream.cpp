#include "audio/stream.h"

#include "audio/convert.h"
#include "audio/vorbis.h"
#include "cstr/mem_ops.h"
#include "jobs/counter.h"
#include "jobs/job.h"
#include "jobs/scheduler.h"
#include "lang/scalar.h"
#include "lang/utility.h"

namespace engine_placeholder::audio
{

namespace
{

bool ChannelsOk(u32 channels)
{
    return channels == 1 || channels == 2 || channels == 4 || channels == 6 || channels == 8;
}

ChannelLayout StreamLayoutOf(u32 channels)
{
    switch (channels)
    {
    case 1: return ChannelLayout::Mono;
    case 2: return ChannelLayout::Stereo;
    case 4: return ChannelLayout::Quad;
    case 6: return ChannelLayout::Surround51;
    default: return ChannelLayout::Surround71;
    }
}

// ---- Memory ---------------------------------------------------------------------------------------------------------

class MemoryStream final : public StreamSource
{
public:
    explicit MemoryStream(AudioClipConstPtr clip) : clip_(Move(clip)) {}

    u32 Channels() const override { return clip_->View().Channels(); }
    u32 SampleRate() const override { return clip_->Format().sample_rate; }
    u64 Length() const override { return clip_->Frames(); }
    u64 LoopStart() const override { return clip_->View().HasLoop() ? clip_->View().desc.loop_start : 0; }
    u64 LoopEnd() const override { return clip_->View().HasLoop() ? clip_->View().desc.loop_end : 0; }

    u32 Read(f32* out, u32 frames) override
    {
        const ClipView view = clip_->View();
        const u64 left = view.Frames() - position_;
        const u32 n = left < frames ? static_cast<u32>(left) : frames;
        MemCopy(out, view.samples.Data() + static_cast<usize>(position_) * view.Channels(),
                static_cast<usize>(n) * view.Channels() * sizeof(f32));
        position_ += n;
        return n;
    }

    bool Seek(u64 frame) override
    {
        position_ = frame < clip_->Frames() ? frame : clip_->Frames();
        return true;
    }

private:
    AudioClipConstPtr clip_;
    u64 position_ = 0;
};

// ---- Ogg Vorbis -----------------------------------------------------------------------------------------------------

class VorbisStream final : public StreamSource
{
public:
    VorbisStream(SharedBytesPtr bytes, UniquePtr<VorbisDecoder> decoder) : bytes_(Move(bytes)), decoder_(Move(decoder))
    {
    }

    u32 Channels() const override { return decoder_->Info().channels; }
    u32 SampleRate() const override { return decoder_->Info().sample_rate; }
    u64 Length() const override { return decoder_->Info().total_frames; }
    u64 LoopStart() const override { return decoder_->Info().loop_start; }
    u64 LoopEnd() const override { return decoder_->Info().loop_end; }

    u32 Read(f32* out, u32 frames) override
    {
        u32 got = 0;
        return decoder_->Decode(out, frames, &got) == VorbisStatus::Ok ? got : 0;
    }

    bool Seek(u64 frame) override
    {
        const VorbisStatus status = decoder_->SeekFrame(frame);
        return status == VorbisStatus::Ok || status == VorbisStatus::EndOfStream;
    }

private:
    SharedBytesPtr bytes_;
    UniquePtr<VorbisDecoder> decoder_;
};

// ---- A WAV file -----------------------------------------------------------------------------------------------------

u32 ReadLe16(const byte* p)
{
    return static_cast<u32>(p[0]) | (static_cast<u32>(p[1]) << 8);
}

u32 ReadLe32(const byte* p)
{
    return ReadLe16(p) | (ReadLe16(p + 2) << 16);
}

class WavFileStream final : public StreamSource
{
public:
    WavFileStream(io::FilePtr file, u32 channels, u32 rate, bool is_float, u64 data_offset, u64 frames)
        : file_(Move(file)), channels_(channels), rate_(rate), is_float_(is_float), data_offset_(data_offset),
          frames_(frames)
    {
    }

    u32 Channels() const override { return channels_; }
    u32 SampleRate() const override { return rate_; }
    u64 Length() const override { return frames_; }
    u64 LoopStart() const override { return 0; }
    u64 LoopEnd() const override { return 0; }

    u32 Read(f32* out, u32 frames) override
    {
        const u64 left = frames_ - position_;
        const u32 n = left < frames ? static_cast<u32>(left) : frames;
        if (n == 0)
        {
            return 0;
        }
        const u32 width = is_float_ ? 4u : 2u;
        const usize bytes = static_cast<usize>(n) * channels_ * width;
        if (scratch_.Size() < bytes && !scratch_.TryResizeUninitialized(bytes))
        {
            return 0;
        }
        usize got = 0;
        if (file_->Read(scratch_.Data(), bytes, data_offset_ + position_ * channels_ * width, &got).Failed() ||
            got < bytes)
        {
            return 0;
        }
        if (is_float_)
        {
            MemCopy(out, scratch_.Data(), bytes);
        }
        else if (!ConvertToF32(SampleFormat::S16, ByteSpan(scratch_.Data(), bytes),
                               Span<f32>(out, static_cast<usize>(n) * channels_)))
        {
            return 0;
        }
        position_ += n;
        return n;
    }

    bool Seek(u64 frame) override
    {
        position_ = frame < frames_ ? frame : frames_;
        return true;
    }

private:
    io::FilePtr file_;
    u32 channels_;
    u32 rate_;
    bool is_float_;
    u64 data_offset_;
    u64 frames_;
    u64 position_ = 0;
    Vector<byte> scratch_;
};

// ---- Resampling (on the refill job, never on the audio thread) ------------------------------------------------------

class ResampledStream final : public StreamSource
{
public:
    ResampledStream(UniquePtr<StreamSource> in, u32 out_rate)
        : in_(Move(in)), channels_(in_->Channels()), in_rate_(in_->SampleRate()), out_rate_(out_rate),
          step_(static_cast<f64>(in_->SampleRate()) / static_cast<f64>(out_rate))
    {
    }

    u32 Channels() const override { return channels_; }
    u32 SampleRate() const override { return out_rate_; }
    u64 Length() const override { return ToOut(in_->Length()); }
    u64 LoopStart() const override { return ToOut(in_->LoopStart()); }
    u64 LoopEnd() const override { return ToOut(in_->LoopEnd()); }

    u32 Read(f32* out, u32 frames) override
    {
        u32 written = 0;
        while (written < frames)
        {
            const u64 i = static_cast<u64>(pos_);
            Ensure(i + 3);
            if (i >= start_ + count_)
            {
                break;  // past the last input frame
            }
            const f32 t = static_cast<f32>(pos_ - static_cast<f64>(i));
            for (u32 c = 0; c < channels_; ++c)
            {
                const f32 p0 = Tap(i == 0 ? 0 : i - 1, c);
                const f32 p1 = Tap(i, c);
                const f32 p2 = Tap(i + 1, c);
                const f32 p3 = Tap(i + 2, c);
                const f32 a = -0.5f * p0 + 1.5f * p1 - 1.5f * p2 + 0.5f * p3;
                const f32 b = p0 - 2.5f * p1 + 2.0f * p2 - 0.5f * p3;
                const f32 cc = -0.5f * p0 + 0.5f * p2;
                out[static_cast<usize>(written) * channels_ + c] = ((a * t + b) * t + cc) * t + p1;
            }
            pos_ += step_;
            written += 1;
        }
        Compact();
        return written;
    }

    bool Seek(u64 frame) override
    {
        pos_ = static_cast<f64>(frame) * step_;
        start_ = static_cast<u64>(pos_) > 0 ? static_cast<u64>(pos_) - 1 : 0;
        count_ = 0;
        in_end_ = false;
        return in_->Seek(start_);
    }

private:
    u64 ToOut(u64 in_frames) const
    {
        return static_cast<u64>(static_cast<f64>(in_frames) * static_cast<f64>(out_rate_) / static_cast<f64>(in_rate_));
    }

    // Reads input until frame `limit` (exclusive) is in the window or the input ends.
    void Ensure(u64 limit)
    {
        while (start_ + count_ < limit && !in_end_)
        {
            const usize need = (static_cast<usize>(count_) + 1024) * channels_;
            if (window_.Size() < need && !window_.TryResizeUninitialized(need))
            {
                in_end_ = true;
                return;
            }
            const u32 got = in_->Read(window_.Data() + static_cast<usize>(count_) * channels_, 1024);
            if (got == 0)
            {
                in_end_ = true;
            }
            count_ += got;
        }
    }

    f32 Tap(u64 index, u32 channel) const
    {
        if (count_ == 0)
        {
            return 0.0f;
        }
        u64 i = index < start_ ? start_ : index;
        if (i >= start_ + count_)
        {
            i = start_ + count_ - 1;
        }
        return window_[static_cast<usize>(i - start_) * channels_ + channel];
    }

    void Compact()
    {
        const u64 keep_from = static_cast<u64>(pos_) > 1 ? static_cast<u64>(pos_) - 1 : 0;
        if (keep_from > start_ + 2048 && keep_from <= start_ + count_)
        {
            const u64 drop = keep_from - start_;
            MemMove(window_.Data(), window_.Data() + static_cast<usize>(drop) * channels_,
                    static_cast<usize>(count_ - drop) * channels_ * sizeof(f32));
            start_ += drop;
            count_ -= static_cast<u32>(drop);
        }
    }

    UniquePtr<StreamSource> in_;
    u32 channels_;
    u32 in_rate_;
    u32 out_rate_;
    f64 step_;
    f64 pos_ = 0.0;
    Vector<f32> window_;
    u64 start_ = 0;
    u32 count_ = 0;
    bool in_end_ = false;
};

}  // namespace

StreamStatus MakeMemoryStream(const AudioClipConstPtr& clip, UniquePtr<StreamSource>* out)
{
    if (!clip || out == nullptr)
    {
        return StreamStatus::Invalid;
    }
    UniquePtr<StreamSource> source = MakeUnique<MemoryStream>(clip);
    if (!source)
    {
        return StreamStatus::OutOfMemory;
    }
    *out = Move(source);
    return StreamStatus::Ok;
}

StreamStatus MakeVorbisStream(const SharedBytesPtr& bytes, UniquePtr<StreamSource>* out)
{
    if (!bytes || out == nullptr)
    {
        return StreamStatus::Invalid;
    }
    UniquePtr<VorbisDecoder> decoder;
    const VorbisStatus status = VorbisDecoder::Open(bytes->Bytes(), VorbisLimits(), &decoder);
    if (status == VorbisStatus::OutOfMemory)
    {
        return StreamStatus::OutOfMemory;
    }
    if (status != VorbisStatus::Ok)
    {
        return StreamStatus::Invalid;
    }
    UniquePtr<StreamSource> source = MakeUnique<VorbisStream>(bytes, Move(decoder));
    if (!source)
    {
        return StreamStatus::OutOfMemory;
    }
    *out = Move(source);
    return StreamStatus::Ok;
}

StreamStatus MakeWavFileStream(io::FilePtr file, UniquePtr<StreamSource>* out)
{
    if (!file || out == nullptr)
    {
        return StreamStatus::Invalid;
    }
    u64 size = 0;
    byte head[64];
    usize got = 0;
    if (file->Size(&size).Failed() || file->Read(head, 12, 0, &got).Failed() || got < 12)
    {
        return StreamStatus::Io;
    }
    if (MemCompare(head, "RIFF", 4) != 0 || MemCompare(head + 8, "WAVE", 4) != 0)
    {
        return StreamStatus::Invalid;
    }
    u64 at = 12;
    bool have_fmt = false;
    u32 tag = 0;
    u32 channels = 0;
    u32 rate = 0;
    u32 bits = 0;
    u64 data_offset = 0;
    u64 data_bytes = 0;
    bool have_data = false;
    for (u32 chunks = 0; chunks < 64 && at + 8 <= size && !(have_fmt && have_data); ++chunks)
    {
        if (file->Read(head, 8, at, &got).Failed() || got < 8)
        {
            return StreamStatus::Io;
        }
        const u64 length = ReadLe32(head + 4);
        if (MemCompare(head, "fmt ", 4) == 0)
        {
            byte fmt[40];
            const usize want = length < sizeof(fmt) ? static_cast<usize>(length) : sizeof(fmt);
            if (want < 16 || file->Read(fmt, want, at + 8, &got).Failed() || got < want)
            {
                return StreamStatus::Invalid;
            }
            tag = ReadLe16(fmt);
            channels = ReadLe16(fmt + 2);
            rate = ReadLe32(fmt + 4);
            bits = ReadLe16(fmt + 14);
            if (tag == 0xFFFE && want >= 26)
            {
                tag = ReadLe16(fmt + 24);
            }
            have_fmt = true;
        }
        else if (MemCompare(head, "data", 4) == 0)
        {
            data_offset = at + 8;
            data_bytes = length < size - data_offset ? length : size - data_offset;
            have_data = true;
        }
        at += 8 + length + (length & 1);
    }
    if (!have_fmt || !have_data || !ChannelsOk(channels) || rate < 8000 || rate > 384000)
    {
        return StreamStatus::Invalid;
    }
    const bool is_float = tag == 3 && bits == 32;
    if (!is_float && !(tag == 1 && bits == 16))
    {
        return StreamStatus::Unsupported;
    }
    const u64 frame_bytes = static_cast<u64>(channels) * (is_float ? 4 : 2);
    const u64 frames = data_bytes / frame_bytes;
    if (frames == 0 || frames > kMaxClipFrames)
    {
        return StreamStatus::Invalid;
    }
    UniquePtr<StreamSource> source = MakeUnique<WavFileStream>(Move(file), channels, rate, is_float, data_offset,
                                                               frames);
    if (!source)
    {
        return StreamStatus::OutOfMemory;
    }
    *out = Move(source);
    return StreamStatus::Ok;
}

// ---- The ring
// ---------------------------------------------------------------------------------------------------------

StreamStatus StreamBuffer::Create(UniquePtr<StreamSource> source, const StreamConfig& config, StreamBufferPtr* out)
{
    if (!source || out == nullptr || config.sample_rate < 8000 || config.sample_rate > 384000 ||
        config.chunk_frames == 0 || config.capacity_frames < config.chunk_frames || config.low_water_frames == 0 ||
        config.low_water_frames > config.capacity_frames || !ChannelsOk(source->Channels()) ||
        source->SampleRate() < 8000 || source->SampleRate() > 384000)
    {
        return StreamStatus::Invalid;
    }
    if (source->SampleRate() != config.sample_rate)
    {
        UniquePtr<StreamSource> wrapped = MakeUnique<ResampledStream>(Move(source), config.sample_rate);
        if (!wrapped)
        {
            return StreamStatus::OutOfMemory;
        }
        source = Move(wrapped);
    }
    StreamBufferPtr buffer = MakeShared<StreamBuffer>();
    if (!buffer)
    {
        return StreamStatus::OutOfMemory;
    }
    buffer->channels_ = source->Channels();
    buffer->capacity_ = config.capacity_frames;
    buffer->chunk_ = config.chunk_frames;
    buffer->low_water_ = config.low_water_frames;
    buffer->length_ = source->Length();
    buffer->loop_start_ = source->LoopStart();
    buffer->loop_end_ = source->LoopEnd() > source->LoopStart() ? source->LoopEnd() : 0;
    if (!buffer->ring_.TryResizeUninitialized(static_cast<usize>(config.capacity_frames) * buffer->channels_))
    {
        return StreamStatus::OutOfMemory;
    }
    ClipDesc desc;
    desc.format.sample_rate = config.sample_rate;
    desc.format.layout = StreamLayoutOf(buffer->channels_);
    desc.format.format = SampleFormat::F32;
    desc.frame_count = 1;
    Vector<f32> silence;
    silence.Resize(buffer->channels_);
    AudioClipPtr clip;
    if (AudioClip::CreateCopy(desc, Span<const f32>(silence.Data(), silence.Size()), StringView("stream"), &clip) !=
        ClipStatus::Ok)
    {
        return StreamStatus::OutOfMemory;
    }
    buffer->clip_ = clip;
    buffer->source_ = Move(source);
    *out = Move(buffer);
    return StreamStatus::Ok;
}

StreamBuffer::~StreamBuffer() = default;

u32 StreamBuffer::Pop(f32* out, u32 frames)
{
    const u64 state = state_.Load(MemoryOrder::Acquire);
    const u64 epoch = state >> 48;
    const u64 written = state & kCountMask;
    const u64 read = read_count_.Load(MemoryOrder::Relaxed);
    const u64 available = written > read ? written - read : 0;
    const u32 take = available < frames ? static_cast<u32>(available) : frames;
    u32 done = 0;
    while (done < take)
    {
        const u32 slot = static_cast<u32>((read + done) % capacity_);
        u32 run = capacity_ - slot;
        if (run > take - done)
        {
            run = take - done;
        }
        MemCopy(out + static_cast<usize>(done) * channels_, ring_.Data() + static_cast<usize>(slot) * channels_,
                static_cast<usize>(run) * channels_ * sizeof(f32));
        done += run;
    }
    read_count_.Store(read + take, MemoryOrder::Release);
    if (take < frames && ended_epoch_.Load(MemoryOrder::Acquire) != epoch + 1)
    {
        underruns_.FetchAdd(1, MemoryOrder::Relaxed);
    }
    return take;
}

bool StreamBuffer::Ended() const
{
    const u64 state = state_.Load(MemoryOrder::Acquire);
    return ended_epoch_.Load(MemoryOrder::Acquire) == (state >> 48) + 1 &&
           (state & kCountMask) <= read_count_.Load(MemoryOrder::Relaxed);
}

void StreamBuffer::Flush(u64 frame)
{
    const u64 state = state_.Load(MemoryOrder::Acquire);
    const u64 epoch = ((state >> 48) + 1) & 0xFFFFull;
    seek_target_.Store(frame, MemoryOrder::Relaxed);
    read_count_.Store(0, MemoryOrder::Release);
    state_.Store(epoch << 48, MemoryOrder::Release);
}

u32 StreamBuffer::Buffered() const
{
    const u64 written = state_.Load(MemoryOrder::Acquire) & kCountMask;
    const u64 read = read_count_.Load(MemoryOrder::Acquire);
    return written > read ? static_cast<u32>(written - read) : 0u;
}

bool StreamBuffer::NeedsRefill() const
{
    const u64 state = state_.Load(MemoryOrder::Acquire);
    return Buffered() < low_water_ && ended_epoch_.Load(MemoryOrder::Acquire) != (state >> 48) + 1;
}

u32 StreamBuffer::Refill()
{
    if (busy_.Exchange(true, MemoryOrder::Acquire))
    {
        return 0;
    }
    u32 total = 0;
    u32 empty_wraps = 0;
    while (true)
    {
        const u64 state = state_.Load(MemoryOrder::Acquire);
        const u64 epoch = state >> 48;
        if (epoch != seen_epoch_)
        {
            // The consumer flushed: continue from its target (read again until the epoch is steady).
            u64 target = 0;
            u64 before = 0;
            u64 after = 0;
            do
            {
                before = state_.Load(MemoryOrder::Acquire) >> 48;
                target = seek_target_.Load(MemoryOrder::Acquire);
                after = state_.Load(MemoryOrder::Acquire) >> 48;
            } while (before != after);
            source_->Seek(target);
            position_ = target;
            seen_epoch_ = after;
            ended_local_ = false;
            continue;
        }
        if (ended_local_)
        {
            break;
        }
        const u64 written = state & kCountMask;
        const u64 read = read_count_.Load(MemoryOrder::Acquire);
        u64 used = written > read ? written - read : 0;
        used = used > capacity_ ? capacity_ : used;
        const u32 free_frames = capacity_ - static_cast<u32>(used);
        if (free_frames == 0)
        {
            break;
        }
        const bool looping = looping_.Load(MemoryOrder::Acquire);
        u32 want = free_frames < chunk_ ? free_frames : chunk_;
        const u32 slot = static_cast<u32>(written % capacity_);
        if (want > capacity_ - slot)
        {
            want = capacity_ - slot;
        }
        if (looping && loop_end_ > 0 && position_ < loop_end_ && want > loop_end_ - position_)
        {
            want = static_cast<u32>(loop_end_ - position_);
        }
        const u32 got = source_->Read(ring_.Data() + static_cast<usize>(slot) * channels_, want);
        if (got == 0)
        {
            if (looping && empty_wraps < 2)
            {
                empty_wraps += 1;
                source_->Seek(loop_start_);
                position_ = loop_start_;
                continue;
            }
            ended_epoch_.Store(epoch + 1, MemoryOrder::Release);
            ended_local_ = true;
            continue;
        }
        empty_wraps = 0;
        u64 expected = state;
        if (!state_.CompareExchangeStrong(expected, (epoch << 48) | (written + got), MemoryOrder::Release,
                                          MemoryOrder::Acquire))
        {
            continue;  // flushed while reading: the frames belong to an older epoch
        }
        position_ += got;
        total += got;
        if (looping && loop_end_ > 0 && position_ >= loop_end_)
        {
            source_->Seek(loop_start_);
            position_ = loop_start_;
        }
    }
    busy_.Store(false, MemoryOrder::Release);
    return total;
}

// ---- The pump
// -----------------------------------------------------------------------------------------------------------

namespace
{

jobs::Job RefillJob(StreamBufferPtr buffer)
{
    buffer->Refill();
    co_return;
}

}  // namespace

StreamPump::StreamPump(jobs::Scheduler& scheduler)
    : scheduler_(&scheduler), counter_(MakeUnique<jobs::Counter>(scheduler))
{
}

StreamPump::~StreamPump()
{
    if (counter_)
    {
        scheduler_->HelpUntil(*counter_);
    }
}

void StreamPump::Add(const StreamBufferPtr& buffer)
{
    buffers_.Push(buffer);
}

u32 StreamPump::Update()
{
    u32 started = 0;
    for (usize i = 0; i < buffers_.Size(); ++i)
    {
        if (buffers_[i]->NeedsRefill())
        {
            jobs::Spawn(*counter_, RefillJob(buffers_[i]));
            started += 1;
        }
    }
    return started;
}

u32 StreamPump::Run()
{
    const u32 started = Update();
    scheduler_->HelpUntil(*counter_);
    return started;
}

}  // namespace engine_placeholder::audio
