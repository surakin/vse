// Repeatability (Phase 26 Day 415): a small scene rendered twice in one process gives the same bits, whatever the size
// of the pulls; the tolerance comparison flags the same scene with a gain of 1.001. The scene stands in for the mixer
// (Day 417): voices in a fixed order, a ramped gain and pan, one biquad low-pass per voice, a command applied at a
// block stamp, and nothing that depends on thread timing.
#include <catch2/catch_test_macros.hpp>

#include "audio/block_hash.h"
#include "audio/render_compare.h"
#include "containers/vector.h"
#include "lang/scalar.h"
#include "lang/utility.h"

using namespace engine_placeholder;
using namespace engine_placeholder::audio;

namespace
{

constexpr u32 kSceneBlockFrames = 256;
constexpr u32 kBlocks = 40;

struct Voice
{
    f32 phase = 0.0f;
    f32 step = 0.0f;
    f32 gain = 0.0f;
    f32 pan = 0.5f;
    f32 z1 = 0.0f;  // the low-pass state
    f32 z2 = 0.0f;
};

// Renders the scene as `kBlocks` stereo blocks of `kSceneBlockFrames`, pulled by the caller in callbacks of
// `pull_frames`, and returns the interleaved samples.
Vector<f32> RenderScene(f32 master_gain, u32 pull_frames)
{
    Vector<Voice> voices;
    for (u32 v = 0; v < 24; ++v)
    {
        Voice voice;
        voice.step = 0.01f + 0.0037f * static_cast<f32>(v);
        voice.gain = 0.02f + 0.001f * static_cast<f32>(v);
        voice.pan = static_cast<f32>(v % 5) * 0.25f;
        voices.Push(voice);
    }
    // A low-pass at 0.2 of the rate (RBJ biquad, Q 0.7).
    const f32 w0 = 6.2831853f * 0.1f;
    const f32 alpha = Sin(w0) / (2.0f * 0.7f);
    const f32 cw = Cos(w0);
    const f32 a0 = 1.0f + alpha;
    const f32 b0 = (1.0f - cw) * 0.5f / a0;
    const f32 b1 = (1.0f - cw) / a0;
    const f32 a1 = -2.0f * cw / a0;
    const f32 a2 = (1.0f - alpha) / a0;

    Vector<f32> out;
    Vector<f32> block;
    for (u32 i = 0; i < kSceneBlockFrames * 2; ++i)
    {
        block.Push(0.0f);
    }
    Vector<f32> pending;  // rendered frames not yet pulled
    for (u32 b = 0; b < kBlocks; ++b)
    {
        if (b == 17)
        {
            voices[3].gain *= 2.0f;  // a command at a block stamp
        }
        for (usize i = 0; i < block.Size(); ++i)
        {
            block[i] = 0.0f;
        }
        for (u32 frame = 0; frame < kSceneBlockFrames; ++frame)
        {
            for (usize v = 0; v < voices.Size(); ++v)  // fixed voice order
            {
                Voice& voice = voices[v];
                const f32 x = Sin(voice.phase) * voice.gain;
                voice.phase += voice.step;
                const f32 y = b0 * x + voice.z1;
                voice.z1 = b1 * x - a1 * y + voice.z2;
                voice.z2 = b0 * x - a2 * y;
                block[frame * 2] += y * (1.0f - voice.pan) * master_gain;
                block[frame * 2 + 1] += y * voice.pan * master_gain;
            }
        }
        for (usize i = 0; i < block.Size(); ++i)
        {
            pending.Push(block[i]);
        }
        // The device pulls in callbacks of its own size; whole callbacks are taken as they become available.
        while (pending.Size() >= static_cast<usize>(pull_frames) * 2)
        {
            const usize take = static_cast<usize>(pull_frames) * 2;
            for (usize i = 0; i < take; ++i)
            {
                out.Push(pending[i]);
            }
            Vector<f32> rest;
            for (usize i = take; i < pending.Size(); ++i)
            {
                rest.Push(pending[i]);
            }
            pending = Move(rest);
        }
    }
    for (usize i = 0; i < pending.Size(); ++i)
    {
        out.Push(pending[i]);
    }
    return out;
}

Span<const f32> RenderView(const Vector<f32>& v)
{
    return Span<const f32>(v.Data(), v.Size());
}

}  // namespace

TEST_CASE("repeatable render: two renders in one process are bit-identical", "[audio][repeatable]")
{
    const Vector<f32> first = RenderScene(1.0f, kSceneBlockFrames);
    const Vector<f32> second = RenderScene(1.0f, kSceneBlockFrames);
    REQUIRE(first.Size() == static_cast<usize>(kBlocks) * kSceneBlockFrames * 2);
    REQUIRE(first.Size() == second.Size());

    bool valid = false;
    const u64 hash = HashBlock(RenderView(first), &valid);
    CHECK(valid);
    CHECK(hash == HashBlock(RenderView(second), nullptr));
    CHECK(CompareSamples(RenderView(first), RenderView(second), RenderTolerance()).max_abs_error == 0.0f);
}

TEST_CASE("repeatable render: the size of the pulls does not change the bits", "[audio][repeatable]")
{
    const u64 reference = HashBlock(RenderView(RenderScene(1.0f, kSceneBlockFrames)), nullptr);
    const u32 pulls[3] = {64, 100, 1024};
    for (u32 pull : pulls)
    {
        CAPTURE(pull);
        CHECK(HashBlock(RenderView(RenderScene(1.0f, pull)), nullptr) == reference);
    }
}

TEST_CASE("repeatable render: the tolerance comparison flags a gain of 1.001", "[audio][repeatable]")
{
    const Vector<f32> reference = RenderScene(1.0f, kSceneBlockFrames);
    const Vector<f32> louder = RenderScene(1.001f, kSceneBlockFrames);
    CHECK(HashBlock(RenderView(reference), nullptr) != HashBlock(RenderView(louder), nullptr));

    const CompareResult samples = CompareSamples(RenderView(reference), RenderView(louder), RenderTolerance());
    CHECK_FALSE(samples.equal);
    CHECK(samples.max_abs_error > 1.0e-5f);

    const usize block_samples = static_cast<usize>(kSceneBlockFrames) * 2;
    const CompareResult summaries = CompareSummaries(Summarize(RenderView(reference), block_samples, 64),
                                                     Summarize(RenderView(louder), block_samples, 64),
                                                     RenderTolerance());
    CHECK_FALSE(summaries.equal);
}
