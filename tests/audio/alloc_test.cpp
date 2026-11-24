// The audio thread allocates nothing: a counter on every heap allocation of this executable proves that a warm mixer's
// `RenderBlock` and `Render` (voices at every quality, virtual voices, loops, a fade, buses with sends, ducking,
// meters, the three effects and the limiter, commands applied in the block) make none. The ThreadSanitizer runtime
// defines its own operator new (a second definition fails to link), so that flavor skips the counting.
#include "sync/atomic.h"
#include "build_defs.h"
#include "lang/new.h"

#include <catch2/catch_test_macros.hpp>

#include "audio/delay.h"
#include "audio/filter.h"
#include "audio/mixer.h"
#include "containers/vector.h"
#include "cstr/mem_ops.h"
#include "lang/scalar.h"

using namespace vse;  // the allocation hooks below are global and name the engine types

#if ENGINE_SANITIZER_THREAD
#define AUDIO_ALLOC_TEST_NO_NEW_COUNTER 1
#endif

// Counted per thread: other tests' threads (a device's render thread, the job workers) may allocate meanwhile.
static thread_local unsigned long t_audio_heap_allocations = 0;

#ifndef AUDIO_ALLOC_TEST_NO_NEW_COUNTER
// Every form is replaced so allocation and release always pair up. A block that needs more than the 16 bytes malloc
// gives is over-allocated: its start is rounded up to `align` and the pointer of the underlying allocation is kept just
// before it, which the aligned delete forms read back.
static constexpr usize kAudioMallocAlign = 16;
static void* AudioCountedAlloc(usize size, usize align)
{
    t_audio_heap_allocations += 1;
    if (size == 0)
    {
        size = 1;
    }
    if (align <= kAudioMallocAlign)
    {
        return MemAllocate(size);
    }
    void* raw = MemAllocate(size + align + sizeof(void*));
    if (raw == nullptr)
    {
        return nullptr;
    }
    const usize start = (reinterpret_cast<usize>(raw) + sizeof(void*) + align - 1) / align * align;
    void* block = reinterpret_cast<void*>(start);
    reinterpret_cast<void**>(block)[-1] = raw;
    return block;
}
static void AudioAlignedFree(void* p, usize align) noexcept
{
    if (align <= kAudioMallocAlign)
    {
        MemFree(p);
    }
    else if (p != nullptr)
    {
        MemFree(reinterpret_cast<void**>(p)[-1]);
    }
}
void* operator new(usize n)
{
    if (void* p = AudioCountedAlloc(n, 0))
    {
        return p;
    }
    throw BadAlloc();
}
void* operator new[](usize n)
{
    if (void* p = AudioCountedAlloc(n, 0))
    {
        return p;
    }
    throw BadAlloc();
}
void* operator new(usize n, const NoThrowTag&) noexcept
{
    return AudioCountedAlloc(n, 0);
}
void* operator new[](usize n, const NoThrowTag&) noexcept
{
    return AudioCountedAlloc(n, 0);
}
void* operator new(usize n, AlignVal a)
{
    if (void* p = AudioCountedAlloc(n, static_cast<usize>(a)))
    {
        return p;
    }
    throw BadAlloc();
}
void* operator new[](usize n, AlignVal a)
{
    if (void* p = AudioCountedAlloc(n, static_cast<usize>(a)))
    {
        return p;
    }
    throw BadAlloc();
}
void* operator new(usize n, AlignVal a, const NoThrowTag&) noexcept
{
    return AudioCountedAlloc(n, static_cast<usize>(a));
}
void* operator new[](usize n, AlignVal a, const NoThrowTag&) noexcept
{
    return AudioCountedAlloc(n, static_cast<usize>(a));
}
void operator delete(void* p) noexcept
{
    MemFree(p);
}
void operator delete[](void* p) noexcept
{
    MemFree(p);
}
void operator delete(void* p, usize) noexcept
{
    MemFree(p);
}
void operator delete[](void* p, usize) noexcept
{
    MemFree(p);
}
void operator delete(void* p, const NoThrowTag&) noexcept
{
    MemFree(p);
}
void operator delete[](void* p, const NoThrowTag&) noexcept
{
    MemFree(p);
}
void operator delete(void* p, AlignVal a) noexcept
{
    AudioAlignedFree(p, static_cast<usize>(a));
}
void operator delete[](void* p, AlignVal a) noexcept
{
    AudioAlignedFree(p, static_cast<usize>(a));
}
void operator delete(void* p, AlignVal a, const NoThrowTag&) noexcept
{
    AudioAlignedFree(p, static_cast<usize>(a));
}
void operator delete[](void* p, AlignVal a, const NoThrowTag&) noexcept
{
    AudioAlignedFree(p, static_cast<usize>(a));
}
void operator delete(void* p, usize, AlignVal a) noexcept
{
    AudioAlignedFree(p, static_cast<usize>(a));
}
void operator delete[](void* p, usize, AlignVal a) noexcept
{
    AudioAlignedFree(p, static_cast<usize>(a));
}
#endif

using namespace vse::audio;

namespace
{

#ifndef AUDIO_ALLOC_TEST_NO_NEW_COUNTER
AudioClipPtr AlClip(const char* name, u32 frames, ChannelLayout layout, bool loop_points)
{
    const u32 channels = ChannelCount(layout);
    Vector<f32> samples;
    for (u32 i = 0; i < frames * channels; ++i)
    {
        samples.Push(0.3f * Sin(0.021f * static_cast<f32>(i)));
    }
    ClipDesc desc;
    desc.format.sample_rate = 44100;  // resampled to the mixer's 48 kHz
    desc.format.layout = layout;
    desc.format.format = SampleFormat::F32;
    desc.frame_count = frames;
    if (loop_points)
    {
        desc.loop_start = 100;
        desc.loop_end = frames - 100;
    }
    AudioClipPtr clip;
    REQUIRE(AudioClip::CreateCopy(desc, Span<const f32>(samples.Data(), samples.Size()), name, &clip) ==
            ClipStatus::Ok);
    return clip;
}
#endif

}  // namespace

TEST_CASE("Audio thread: a warm mixer renders, applies commands and runs its effects with no heap allocation",
          "[audio][mixer][alloc]")
{
#ifdef AUDIO_ALLOC_TEST_NO_NEW_COUNTER
    SKIP("this flavor's sanitizer runtime owns operator new, so allocations cannot be counted");
#else
    const ResamplerQuality qualities[] = {ResamplerQuality::Linear, ResamplerQuality::Hermite, ResamplerQuality::Sinc};
    for (ResamplerQuality quality : qualities)
    {
        MixerConfig config;
        config.layout = ChannelLayout::Stereo;
        config.quality = quality;
        config.max_voices = 16;
        config.lead_blocks = 0;
        config.limiter = true;
        Mixer mixer;
        REQUIRE(mixer.Init(config) == MixerStatus::Ok);
        BusId music;
        BusId fx;
        REQUIRE(mixer.CreateBus("music", BusId{0}, &music) == MixerStatus::Ok);
        REQUIRE(mixer.CreateBus("fx", BusId{0}, &fx) == MixerStatus::Ok);
        REQUIRE(mixer.SetBusSend(music, 0, fx, 0.4f, false) == MixerStatus::Ok);
        REQUIRE(mixer.SetDuck(fx, music, 0.5f, 0.05f, 1000.0f) == MixerStatus::Ok);
        REQUIRE(mixer.SetMeter(music, true) == MixerStatus::Ok);
        DelayDesc delay;
        delay.delay_seconds = 0.05f;
        delay.feedback = 0.4f;
        REQUIRE(mixer.SetInsert(music, 0, MakeDelayEffect(2, 48000, 0.2f, 64)) == MixerStatus::Ok);
        REQUIRE(mixer.SetInsertParams(music, 0, MakeDelayParams(48000, delay)) == MixerStatus::Ok);
        ReverbDesc reverb;
        REQUIRE(mixer.SetInsert(fx, 0, MakeReverbEffect(2, reverb)) == MixerStatus::Ok);
        REQUIRE(mixer.SetInsert(fx, 1, MakeFilterEffect(2, 64)) == MixerStatus::Ok);
        const AudioClipPtr mono = AlClip("mono", 20000, ChannelLayout::Mono, true);
        const AudioClipPtr stereo = AlClip("stereo", 9000, ChannelLayout::Stereo, false);
        Vector<VoiceId> ids;
        for (u32 v = 0; v < 10; ++v)
        {
            VoiceDesc desc;
            desc.clip = (v & 1) ? stereo : mono;
            desc.loop = v != 9;
            desc.gain = 0.2f;
            desc.pitch = 0.7f + 0.11f * static_cast<f32>(v);
            desc.pan = -0.8f + 0.17f * static_cast<f32>(v);
            desc.bus = v < 5 ? music : fx;
            desc.send_bus = v == 2 ? fx : BusId();
            desc.send_gain = v == 2 ? 0.3f : 0.0f;
            desc.directional = v == 4;
            desc.is_virtual = v == 6;
            desc.fade_in = v == 7;
            VoiceId id;
            REQUIRE(mixer.Play(desc, &id) == MixerStatus::Ok);
            ids.Push(id);
        }
        Vector<f32> block;
        block.Resize(static_cast<usize>(kBlockFrames) * 2);
        Vector<f32> device;
        device.Resize(2000 * 2);
        for (u32 b = 0; b < 8; ++b)  // warm: every command so far applied, every buffer touched
        {
            mixer.RenderBlock(Span<f32>(block.Data(), block.Size()));
        }
        // Commands for the blocks inside the counted run: they are posted (and allocated, on the control side) now.
        const u64 next = mixer.NextBlock();
        REQUIRE(mixer.Fade(ids[1], 0.05f, 3000, next + 2) == MixerStatus::Ok);
        REQUIRE(mixer.SetPitch(ids[3], 1.4f, next + 3) == MixerStatus::Ok);
        REQUIRE(mixer.Pause(ids[5], next + 4) == MixerStatus::Ok);
        REQUIRE(mixer.Resume(ids[5], next + 9) == MixerStatus::Ok);
        REQUIRE(mixer.SetVirtual(ids[8], true, next + 5) == MixerStatus::Ok);
        REQUIRE(mixer.SetDirection(ids[4], 0.3f, 0.0f, -0.9f, 0.1f, next + 6) == MixerStatus::Ok);
        REQUIRE(mixer.Stop(ids[0], 600, next + 7) == MixerStatus::Ok);
        REQUIRE(mixer.SetBusGain(music, 0.6f, next + 8) == MixerStatus::Ok);
        EffectParams params;
        params.v[0] = 0.5f;
        REQUIRE(mixer.SetInsertParams(fx, 1, params, next + 8) == MixerStatus::Ok);
        const unsigned long before = t_audio_heap_allocations;
        for (u32 b = 0; b < 60; ++b)
        {
            mixer.RenderBlock(Span<f32>(block.Data(), block.Size()));
        }
        const u32 sizes[] = {1, 37, 256, 300, 1999};
        for (u32 frames : sizes)
        {
            mixer.Render(device.Data(), frames);
        }
        const unsigned long during = t_audio_heap_allocations - before;
        INFO("quality " << static_cast<u32>(quality));
        CHECK(during == 0);
        // The commands did their work in the counted blocks.
        CHECK(mixer.Stats().commands_applied >= 9);
    }
#endif
}

TEST_CASE("Audio thread: the allocation counter sees an allocation on this thread", "[audio][alloc]")
{
#ifdef AUDIO_ALLOC_TEST_NO_NEW_COUNTER
    SKIP("this flavor's sanitizer runtime owns operator new, so allocations cannot be counted");
#else
    const unsigned long before = t_audio_heap_allocations;
    Vector<f32> grow;
    grow.Resize(1000);
    CHECK(t_audio_heap_allocations > before);
#endif
}
