// The tolerance comparison (Phase 26 Day 415): equal renders, a render off by a rounding, a render with a gain of 1.001
// and very different renders.
#include <catch2/catch_test_macros.hpp>

#include "audio/render_compare.h"
#include "containers/vector.h"
#include "lang/limits.h"
#include "lang/scalar.h"

using namespace engine_placeholder;
using namespace engine_placeholder::audio;

namespace
{

// A stereo tone with a slow envelope: 2,048 frames.
Vector<f32> Tone(f32 gain)
{
    Vector<f32> out;
    for (u32 frame = 0; frame < 2048; ++frame)
    {
        const f32 t = static_cast<f32>(frame);
        const f32 envelope = 0.5f + 0.4f * Sin(t * 0.003f);
        out.Push(gain * envelope * Sin(t * 0.11f));
        out.Push(gain * envelope * Sin(t * 0.07f + 1.0f));
    }
    return out;
}

Span<const f32> CompareView(const Vector<f32>& v)
{
    return Span<const f32>(v.Data(), v.Size());
}

}  // namespace

TEST_CASE("CompareSamples: identical renders are equal with an exact zero error", "[audio][compare]")
{
    const Vector<f32> a = Tone(1.0f);
    const Vector<f32> b = Tone(1.0f);
    const CompareResult result = CompareSamples(CompareView(a), CompareView(b), RenderTolerance());
    CHECK(result.equal);
    CHECK_FALSE(result.shape_differs);
    CHECK(result.max_abs_error == 0.0f);
    CHECK(result.rms_error_db == -200.0f);
}

TEST_CASE("CompareSamples: a difference of a few last bits is within the tolerance", "[audio][compare]")
{
    const Vector<f32> a = Tone(1.0f);
    Vector<f32> b = Tone(1.0f);
    for (usize i = 0; i < b.Size(); i += 3)
    {
        b[i] += 3.0e-7f;  // about the size of an FMA difference (spec section 4)
    }
    const CompareResult result = CompareSamples(CompareView(a), CompareView(b), RenderTolerance());
    CHECK(result.equal);
    CHECK(result.max_abs_error > 0.0f);
    CHECK(result.max_abs_error < 1.0e-5f);
    CHECK(result.rms_error_db < -100.0f);
}

TEST_CASE("CompareSamples: a gain of 1.001 is flagged", "[audio][compare]")
{
    const Vector<f32> a = Tone(1.0f);
    const Vector<f32> b = Tone(1.001f);
    const CompareResult result = CompareSamples(CompareView(a), CompareView(b), RenderTolerance());
    CHECK_FALSE(result.equal);
    CHECK_FALSE(result.shape_differs);
    CHECK(result.max_abs_error > 1.0e-5f);
    CHECK(result.rms_error_db > -100.0f);
}

TEST_CASE("CompareSamples: each limit alone flags a render", "[audio][compare]")
{
    const Vector<f32> a = Tone(1.0f);
    Vector<f32> one_spike = Tone(1.0f);
    one_spike[1000] += 2.0e-5f;  // over the absolute limit, a tiny RMS
    CompareResult result = CompareSamples(CompareView(a), CompareView(one_spike), RenderTolerance());
    CHECK_FALSE(result.equal);
    CHECK(result.rms_error_db < -100.0f);

    // The RMS of a difference never exceeds its largest value, so with the defaults (1e-5 is -100 dB) the absolute
    // limit decides; the RMS limit counts when the absolute limit is looser.
    Vector<f32> everywhere = Tone(1.0f);
    for (usize i = 0; i < everywhere.Size(); ++i)
    {
        everywhere[i] += 5.0e-4f;
    }
    RenderTolerance loose_abs;
    loose_abs.max_abs_error = 1.0e-3f;
    result = CompareSamples(CompareView(a), CompareView(everywhere), loose_abs);
    CHECK_FALSE(result.equal);
    CHECK(result.max_abs_error < 1.0e-3f);
    CHECK(result.rms_error_db > -100.0f);

    loose_abs.rms_error_db = -60.0f;
    CHECK(CompareSamples(CompareView(a), CompareView(everywhere), loose_abs).equal);
}

TEST_CASE("CompareSamples: very different renders, other lengths and NaN", "[audio][compare]")
{
    const Vector<f32> a = Tone(1.0f);
    Vector<f32> silence;
    for (usize i = 0; i < a.Size(); ++i)
    {
        silence.Push(0.0f);
    }
    CompareResult result = CompareSamples(CompareView(a), CompareView(silence), RenderTolerance());
    CHECK_FALSE(result.equal);
    CHECK(result.max_abs_error > 0.5f);
    CHECK(result.rms_error_db > -20.0f);

    Vector<f32> shorter;
    for (usize i = 0; i + 2 < a.Size(); ++i)
    {
        shorter.Push(a[i]);
    }
    result = CompareSamples(CompareView(a), CompareView(shorter), RenderTolerance());
    CHECK_FALSE(result.equal);
    CHECK(result.shape_differs);

    Vector<f32> with_nan = Tone(1.0f);
    with_nan[10] = Limits<f32>::QuietNaN();
    result = CompareSamples(CompareView(a), CompareView(with_nan), RenderTolerance());
    CHECK_FALSE(result.equal);
}

TEST_CASE("MeasureBlock: RMS and peak", "[audio][compare]")
{
    const f32 block[4] = {0.5f, -0.5f, 0.5f, -1.0f};
    const BlockStats stats = MeasureBlock(Span<const f32>(block, 4));
    CHECK(stats.peak == 1.0f);
    CHECK(stats.rms == Sqrt(0.4375f));  // (0.25 * 3 + 1) / 4 = 0.4375

    const BlockStats empty = MeasureBlock(Span<const f32>());
    CHECK(empty.rms == 0.0f);
    CHECK(empty.peak == 0.0f);
}

TEST_CASE("Summarize: blocks and probes", "[audio][compare]")
{
    const Vector<f32> a = Tone(1.0f);  // 4,096 samples
    const RenderSummary summary = Summarize(CompareView(a), 512, 64);
    CHECK(summary.blocks.Size() == 8);
    CHECK(summary.probes.Size() == 64);
    CHECK(summary.sample_count == 4096);
    CHECK(summary.probes[0] == a[0]);
    CHECK(summary.probes[1] == a[64]);
    CHECK(summary.probes[63] == a[63 * 64]);

    const RenderSummary ragged = Summarize(CompareView(a), 1000, 4);  // the last block holds 96 samples
    CHECK(ragged.blocks.Size() == 5);
    CHECK(ragged.probes.Size() == 4);

    const RenderSummary none = Summarize(Span<const f32>(), 256, 64);
    CHECK(none.blocks.Size() == 0);
    CHECK(none.probes.Size() == 0);
}

TEST_CASE("CompareSummaries: equal, perturbed and differently shaped", "[audio][compare]")
{
    const Vector<f32> a = Tone(1.0f);
    const Vector<f32> same = Tone(1.0f);
    const Vector<f32> louder = Tone(1.001f);
    const RenderSummary sa = Summarize(CompareView(a), 512, 64);

    CHECK(CompareSummaries(sa, Summarize(CompareView(same), 512, 64), RenderTolerance()).equal);

    const CompareResult flagged = CompareSummaries(sa, Summarize(CompareView(louder), 512, 64), RenderTolerance());
    CHECK_FALSE(flagged.equal);
    CHECK(flagged.max_abs_error > 1.0e-5f);

    CHECK(CompareSummaries(sa, Summarize(CompareView(a), 256, 64), RenderTolerance()).shape_differs);
    CHECK(CompareSummaries(sa, Summarize(CompareView(a), 512, 32), RenderTolerance()).shape_differs);
}
