#include "audio/vorbis.h"

#include "audio/format.h"
#include "cstr/mem_ops.h"
#include "lang/utility.h"
#include "ogg/ogg.h"
#include "vorbis/codec.h"

namespace engine_placeholder::audio
{

namespace
{

constexpr u32 kChunk = 4096;
constexpr u32 kMaxTagLines = 64;
constexpr u32 kMaxTagBytes = 1024;
constexpr u32 kMinRate = 8000;
constexpr u32 kMaxRate = 384000;
constexpr usize kTailBytes = 70000;  // a page is at most 65,307 bytes

// For each output channel, the Vorbis channel that feeds it.
constexpr u32 kOrder6[6] = {0, 2, 1, 5, 3, 4};
constexpr u32 kOrder8[8] = {0, 2, 1, 7, 5, 6, 3, 4};

}  // namespace

struct VorbisDecoder::State
{
    ogg_sync_state sync;
    ogg_stream_state stream;
    vorbis_info vi;
    vorbis_comment vc;
    vorbis_dsp_state dsp;
    vorbis_block block;
    bool sync_ready = false;
    bool stream_ready = false;
    bool info_ready = false;
    bool dsp_ready = false;
    bool block_ready = false;
    ByteSpan bytes;
    usize consumed = 0;
    u32 pages = 0;
    i32 serial = 0;
    bool eos = false;
    u64 frame_cap = 0;
    VorbisLimits limits;
    usize data_offset = 0;
    struct SeekPoint
    {
        usize offset;
        u64 start;  // the frame the first sample a decode from `offset` emits belongs to
    };
    Vector<SeekPoint> index;
    bool index_built = false;

    bool BuildIndex();

    ~State()
    {
        if (block_ready)
        {
            vorbis_block_clear(&block);
        }
        if (dsp_ready)
        {
            vorbis_dsp_clear(&dsp);
        }
        if (info_ready)
        {
            vorbis_comment_clear(&vc);
            vorbis_info_clear(&vi);
        }
        if (stream_ready)
        {
            ogg_stream_clear(&stream);
        }
        if (sync_ready)
        {
            ogg_sync_clear(&sync);
        }
    }

    // Feeds the next chunk of the input to the sync state; false when the input is used up or the memory is.
    bool Feed(bool* out_of_memory)
    {
        if (consumed >= bytes.Size())
        {
            return false;
        }
        const usize n = bytes.Size() - consumed < kChunk ? bytes.Size() - consumed : kChunk;
        char* buffer = ogg_sync_buffer(&sync, static_cast<long>(n));
        if (buffer == nullptr)
        {
            *out_of_memory = true;
            return false;
        }
        MemCopy(buffer, bytes.Data() + consumed, n);
        ogg_sync_wrote(&sync, static_cast<long>(n));
        consumed += n;
        return true;
    }
};

VorbisDecoder::VorbisDecoder() : tags_(HeapAllocator::Default()) {}

VorbisDecoder::~VorbisDecoder() = default;

namespace
{

// The granule position of the last page of the first logical stream with this serial, read from the tail of the input
// through the library's own page parser (so the CRC is checked); 0 when none.
u64 LastGranule(ByteSpan bytes, i32 serial)
{
    const usize start = bytes.Size() > kTailBytes ? bytes.Size() - kTailBytes : 0;
    ogg_sync_state sync;
    ogg_sync_init(&sync);
    u64 granule = 0;
    usize at = start;
    while (at < bytes.Size())
    {
        const usize n = bytes.Size() - at < kChunk ? bytes.Size() - at : kChunk;
        char* buffer = ogg_sync_buffer(&sync, static_cast<long>(n));
        if (buffer == nullptr)
        {
            break;
        }
        MemCopy(buffer, bytes.Data() + at, n);
        ogg_sync_wrote(&sync, static_cast<long>(n));
        at += n;
        ogg_page page;
        while (true)
        {
            const int r = ogg_sync_pageout(&sync, &page);
            if (r == 0)
            {
                break;
            }
            if (r < 0)
            {
                continue;  // a hole: the library resynchronises
            }
            const ogg_int64_t g = ogg_page_granulepos(&page);
            if (ogg_page_serialno(&page) == serial && g > 0)
            {
                granule = static_cast<u64>(g);
            }
        }
    }
    ogg_sync_clear(&sync);
    return granule;
}

}  // namespace

namespace
{

bool NameIs(const char* text, u32 length, const char* name, u32* value_at)
{
    u32 n = 0;
    while (name[n] != 0)
    {
        if (n >= length)
        {
            return false;
        }
        char a = text[n];
        a = (a >= 'a' && a <= 'z') ? static_cast<char>(a - 'a' + 'A') : a;
        if (a != name[n])
        {
            return false;
        }
        n += 1;
    }
    if (n >= length || text[n] != '=')
    {
        return false;
    }
    *value_at = n + 1;
    return true;
}

// LOOPSTART=n and LOOPEND=n (decimal frames); anything else is ignored.
void ReadLoopTag(const char* text, u32 length, VorbisInfo* info)
{
    u32 at = 0;
    u64* target = nullptr;
    if (NameIs(text, length, "LOOPSTART", &at))
    {
        target = &info->loop_start;
    }
    else if (NameIs(text, length, "LOOPEND", &at))
    {
        target = &info->loop_end;
    }
    if (target == nullptr || at >= length)
    {
        return;
    }
    u64 value = 0;
    for (u32 i = at; i < length; ++i)
    {
        if (text[i] < '0' || text[i] > '9' || value > (1ull << 40))
        {
            return;
        }
        value = value * 10 + static_cast<u64>(text[i] - '0');
    }
    *target = value;
}

}  // namespace

VorbisStatus VorbisDecoder::Open(ByteSpan bytes, const VorbisLimits& limits, UniquePtr<VorbisDecoder>* out)
{
    if (bytes.Size() == 0 || bytes.Data() == nullptr || limits.max_steps == 0 || limits.max_pages == 0)
    {
        return VorbisStatus::InvalidInput;
    }
    if (bytes.Size() > limits.max_input_bytes)
    {
        return VorbisStatus::LimitExceeded;
    }
    UniquePtr<VorbisDecoder> decoder = MakeUnique<VorbisDecoder>();
    if (decoder == nullptr)
    {
        return VorbisStatus::OutOfMemory;
    }
    decoder->state_ = MakeUnique<State>();
    if (decoder->state_ == nullptr)
    {
        return VorbisStatus::OutOfMemory;
    }
    State& s = *decoder->state_;
    s.bytes = bytes;
    s.limits = limits;
    ogg_sync_init(&s.sync);
    s.sync_ready = true;
    vorbis_info_init(&s.vi);
    vorbis_comment_init(&s.vc);
    s.info_ready = true;

    // The three header packets of the first logical stream.
    u32 headers = 0;
    bool saw_page = false;
    bool oom = false;
    u32 steps = 0;
    while (headers < 3)
    {
        ogg_page page;
        const int r = ogg_sync_pageout(&s.sync, &page);
        if (r == 0)
        {
            steps += 1;
            if (steps > limits.max_steps || !s.Feed(&oom))
            {
                if (oom)
                {
                    return VorbisStatus::OutOfMemory;
                }
                return saw_page ? VorbisStatus::Truncated : VorbisStatus::NotVorbis;
            }
            continue;
        }
        if (r < 0)
        {
            steps += 1;
            if (steps > limits.max_steps)
            {
                return VorbisStatus::LimitExceeded;
            }
            continue;  // skipped bytes: the library resynchronises on the next page
        }
        saw_page = true;
        s.pages += 1;
        if (s.pages > limits.max_pages)
        {
            return VorbisStatus::LimitExceeded;
        }
        if (!s.stream_ready)
        {
            if (!ogg_page_bos(&page))
            {
                return VorbisStatus::NotVorbis;
            }
            s.serial = ogg_page_serialno(&page);
            if (ogg_stream_init(&s.stream, s.serial) != 0)
            {
                return VorbisStatus::OutOfMemory;
            }
            s.stream_ready = true;
        }
        else if (ogg_page_serialno(&page) != s.serial)
        {
            continue;  // another logical stream (multiplexed): skipped
        }
        if (ogg_stream_pagein(&s.stream, &page) < 0)
        {
            return VorbisStatus::Corrupt;
        }
        ogg_packet packet;
        while (headers < 3)
        {
            const int p = ogg_stream_packetout(&s.stream, &packet);
            if (p == 0)
            {
                break;
            }
            if (p < 0)
            {
                return VorbisStatus::Corrupt;
            }
            if (headers == 2 && packet.bytes > static_cast<long>(limits.max_setup_bytes))
            {
                return VorbisStatus::LimitExceeded;
            }
            // The comment header allocates per comment (about seven times its size): it has the setup's bound.
            if (headers == 1 && packet.bytes > static_cast<long>(limits.max_setup_bytes))
            {
                return VorbisStatus::LimitExceeded;
            }
            const int h = vorbis_synthesis_headerin(&s.vi, &s.vc, &packet);
            if (h == OV_ENOTVORBIS)
            {
                return VorbisStatus::NotVorbis;
            }
            if (h != 0)
            {
                return VorbisStatus::Corrupt;
            }
            headers += 1;
        }
    }

    if (s.vi.channels < 1 || s.vi.channels > 8 || s.vi.channels == 3 || s.vi.channels == 5 || s.vi.channels == 7 ||
        s.vi.rate < static_cast<long>(kMinRate) || s.vi.rate > static_cast<long>(kMaxRate))
    {
        return VorbisStatus::Unsupported;
    }
    if (vorbis_synthesis_init(&s.dsp, &s.vi) != 0)
    {
        return VorbisStatus::Corrupt;
    }
    s.dsp_ready = true;
    if (vorbis_block_init(&s.dsp, &s.block) != 0)
    {
        return VorbisStatus::OutOfMemory;
    }
    s.block_ready = true;

    VorbisInfo& info = decoder->info_;
    info.channels = static_cast<u32>(s.vi.channels);
    info.sample_rate = static_cast<u32>(s.vi.rate);
    info.nominal_bitrate = s.vi.bitrate_nominal > 0 ? static_cast<u32>(s.vi.bitrate_nominal) : 0u;
    info.total_frames = LastGranule(bytes, s.serial);
    s.frame_cap = info.total_frames != 0 && info.total_frames < limits.max_frames ? info.total_frames
                                                                                  : limits.max_frames;

    // The audio starts on the page after the one that ended the setup header.
    s.data_offset = s.consumed - static_cast<usize>(s.sync.fill - s.sync.returned);

    // The tags: the vendor, then the comments; bounded and never trusted.
    if (s.vc.vendor != nullptr)
    {
        u32 n = 0;
        while (s.vc.vendor[n] != 0 && n < kMaxTagBytes)
        {
            n += 1;
        }
        decoder->tags_.Append(StringView(s.vc.vendor, n));
    }
    for (int i = 0; i < s.vc.comments && static_cast<u32>(i) < kMaxTagLines; ++i)
    {
        const int length = s.vc.comment_lengths[i];
        const u32 n = length < 0 ? 0u
                                 : (static_cast<u32>(length) > kMaxTagBytes ? kMaxTagBytes : static_cast<u32>(length));
        decoder->tags_.Append(StringView("\n", 1));
        decoder->tags_.Append(StringView(s.vc.user_comments[i], n));
    }
    for (int i = 0; i < s.vc.comments; ++i)
    {
        const int length = s.vc.comment_lengths[i];
        if (length > 0)
        {
            ReadLoopTag(s.vc.user_comments[i], static_cast<u32>(length), &decoder->info_);
        }
    }
    if (!(decoder->info_.loop_start < decoder->info_.loop_end && decoder->info_.loop_end <= info.total_frames))
    {
        decoder->info_.loop_start = 0;
        decoder->info_.loop_end = 0;
    }
    *out = Move(decoder);
    return VorbisStatus::Ok;
}

VorbisStatus VorbisDecoder::Decode(f32* out, u32 max_frames, u32* frames)
{
    *frames = 0;
    if (out == nullptr || max_frames == 0)
    {
        return VorbisStatus::InvalidInput;
    }
    State& s = *state_;
    const u32 channels = info_.channels;
    u32 steps = 0;
    u32 written = 0;
    const bool loops = looping_ && info_.loop_end > 0;
    while (written < max_frames)
    {
        if (loops && position_ >= info_.loop_end)
        {
            const VorbisStatus wrapped = SeekFrame(info_.loop_start);
            if (wrapped != VorbisStatus::Ok)
            {
                *frames = written;
                return written > 0 ? VorbisStatus::Ok : wrapped;
            }
        }
        if (position_ >= s.frame_cap)
        {
            break;
        }
        // Frames the synthesis already has.
        float** pcm = nullptr;
        const int available = vorbis_synthesis_pcmout(&s.dsp, &pcm);
        if (available > 0)
        {
            u64 take = static_cast<u64>(available);
            if (take > max_frames - written)
            {
                take = max_frames - written;
            }
            if (take > s.frame_cap - position_)
            {
                take = s.frame_cap - position_;
            }
            if (loops && position_ < info_.loop_end && take > info_.loop_end - position_)
            {
                take = info_.loop_end - position_;
            }
            for (u32 n = 0; n < take; ++n)
            {
                f32* frame = out + static_cast<usize>(written + n) * channels;
                for (u32 c = 0; c < channels; ++c)
                {
                    const u32 source = channels == 6 ? kOrder6[c] : (channels == 8 ? kOrder8[c] : c);
                    frame[c] = pcm[source][n];
                }
            }
            vorbis_synthesis_read(&s.dsp, static_cast<int>(take));
            written += static_cast<u32>(take);
            position_ += take;
            continue;
        }
        steps += 1;
        if (steps > s.limits.max_steps)
        {
            *frames = written;
            return written > 0 ? VorbisStatus::Ok : VorbisStatus::LimitExceeded;
        }
        // The next packet of the stream.
        ogg_packet packet;
        const int p = ogg_stream_packetout(&s.stream, &packet);
        if (p > 0)
        {
            if (packet.e_o_s)
            {
                s.eos = true;
            }
            if (vorbis_synthesis(&s.block, &packet) == 0)
            {
                vorbis_synthesis_blockin(&s.dsp, &s.block);
            }
            else
            {
                skipped_ += 1;
            }
            continue;
        }
        if (p < 0)
        {
            skipped_ += 1;  // a hole in the stream: the library resynchronises
            continue;
        }
        // The next page.
        if (s.eos)
        {
            break;
        }
        ogg_page page;
        const int r = ogg_sync_pageout(&s.sync, &page);
        if (r > 0)
        {
            s.pages += 1;
            if (s.pages > s.limits.max_pages)
            {
                *frames = written;
                return written > 0 ? VorbisStatus::Ok : VorbisStatus::LimitExceeded;
            }
            if (ogg_page_serialno(&page) == s.serial)
            {
                ogg_stream_pagein(&s.stream, &page);
            }
            continue;
        }
        if (r < 0)
        {
            continue;
        }
        bool oom = false;
        if (!s.Feed(&oom))
        {
            if (oom)
            {
                *frames = written;
                return VorbisStatus::OutOfMemory;
            }
            break;  // the input is used up
        }
    }
    *frames = written;
    return written > 0 ? VorbisStatus::Ok : VorbisStatus::EndOfStream;
}

// Reads the page index of the whole input once: every page of the stream that starts a packet, has a granule position
// and is not the last, with the granule of the page before it.
bool VorbisDecoder::State::BuildIndex()
{
    State& s = *this;
    ogg_sync_state index_sync;
    ogg_sync_init(&index_sync);
    ogg_stream_state st;
    if (ogg_stream_init(&st, s.serial) != 0)
    {
        ogg_sync_clear(&index_sync);
        return false;
    }
    usize fed = s.data_offset;
    u64 previous = 0;
    long previous_blocksize = 0;
    u32 index_pages = 0;
    bool ok = true;
    while (ok && fed < s.bytes.Size())
    {
        const usize n = s.bytes.Size() - fed < kChunk ? s.bytes.Size() - fed : kChunk;
        char* buffer = ogg_sync_buffer(&index_sync, static_cast<long>(n));
        if (buffer == nullptr)
        {
            ok = false;
            break;
        }
        MemCopy(buffer, s.bytes.Data() + fed, n);
        ogg_sync_wrote(&index_sync, static_cast<long>(n));
        fed += n;
        ogg_page page;
        while (true)
        {
            const int r = ogg_sync_pageout(&index_sync, &page);
            if (r == 0)
            {
                break;
            }
            if (r < 0)
            {
                continue;
            }
            index_pages += 1;
            if (index_pages > s.limits.max_pages)
            {
                ok = false;
                break;
            }
            if (ogg_page_serialno(&page) != s.serial)
            {
                continue;
            }
            const usize end = fed - static_cast<usize>(index_sync.fill - index_sync.returned);
            const usize start = end - static_cast<usize>(page.header_len) - static_cast<usize>(page.body_len);
            const bool starts_packet = !ogg_page_continued(&page);
            const ogg_int64_t granule = ogg_page_granulepos(&page);
            if (ogg_stream_pagein(&st, &page) < 0)
            {
                continue;
            }
            long first_blocksize = 0;
            long last_blocksize = 0;
            ogg_packet packet;
            while (ogg_stream_packetout(&st, &packet) > 0)
            {
                const long size = vorbis_packet_blocksize(&s.vi, &packet);
                if (size <= 0)
                {
                    continue;
                }
                if (first_blocksize == 0)
                {
                    first_blocksize = size;
                }
                last_blocksize = size;
            }
            // A page the decode can restart on: it starts a packet, it is not the first (whose first packet makes no
            // sound) nor the last (whose granule position is trimmed), and a packet before it gives the lapping.
            if (starts_packet && !ogg_page_eos(&page) && granule > 0 && start != s.data_offset && first_blocksize > 0 &&
                previous_blocksize > 0)
            {
                if (!s.index.TryReserve(s.index.Size() + 1))
                {
                    ok = false;
                    break;
                }
                SeekPoint point;
                point.offset = start;
                point.start = previous + static_cast<u64>(previous_blocksize / 4) +
                              static_cast<u64>(first_blocksize / 4);
                s.index.Push(point);
            }
            if (last_blocksize > 0)
            {
                previous_blocksize = last_blocksize;
            }
            if (granule > 0)
            {
                previous = static_cast<u64>(granule);
            }
        }
    }
    ogg_stream_clear(&st);
    ogg_sync_clear(&index_sync);
    return ok;
}

VorbisStatus VorbisDecoder::SeekFrame(u64 frame)
{
    State& s = *state_;
    if (frame >= s.frame_cap)
    {
        position_ = s.frame_cap;
        return VorbisStatus::EndOfStream;
    }
    if (!s.index_built)
    {
        if (!s.BuildIndex())
        {
            return VorbisStatus::LimitExceeded;
        }
        s.index_built = true;
    }
    // The last restart page whose first emitted frame is at or before the target.
    usize lo = 0;
    usize hi = s.index.Size();
    while (lo < hi)
    {
        const usize mid = lo + (hi - lo) / 2;
        if (s.index[mid].start <= frame)
        {
            lo = mid + 1;
        }
        else
        {
            hi = mid;
        }
    }
    ogg_sync_reset(&s.sync);
    ogg_stream_reset(&s.stream);
    vorbis_synthesis_restart(&s.dsp);
    s.eos = false;
    s.pages = 0;  // the page limit is per pass: an endlessly looping stream must not run into it
    if (lo == 0)
    {
        s.consumed = s.data_offset;
        position_ = 0;
    }
    else
    {
        // The first packet of the page only primes the overlap; the decode from here emits from `start` on.
        s.consumed = s.index[lo - 1].offset;
        position_ = s.index[lo - 1].start;
    }
    // The rest of the way is decoded and thrown away.
    Vector<f32> scratch;
    if (!scratch.TryResizeUninitialized(static_cast<usize>(2048) * info_.channels))
    {
        return VorbisStatus::OutOfMemory;
    }
    const bool was_looping = looping_;
    looping_ = false;
    VorbisStatus status = VorbisStatus::Ok;
    while (position_ < frame)
    {
        const u64 want = frame - position_ < 2048 ? frame - position_ : 2048;
        u32 got = 0;
        status = Decode(scratch.Data(), static_cast<u32>(want), &got);
        if (status != VorbisStatus::Ok)
        {
            break;
        }
    }
    looping_ = was_looping;
    return status == VorbisStatus::EndOfStream ? VorbisStatus::Corrupt : status;
}

}  // namespace engine_placeholder::audio
