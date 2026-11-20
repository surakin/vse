#include "audio/render_compare.h"

#include "lang/scalar.h"
#include "log/assert.h"

namespace vse::audio
{

namespace
{

// 20 / ln(10): decibels from a ratio through the natural logarithm.
constexpr f64 kDbPerNeper = 8.685889638065036;
constexpr f32 kSilenceDb = -200.0f;

// Accumulates differences: the largest absolute one and the sum of squares.
struct ErrorAccumulator
{
    f64 sum_squares = 0.0;
    u64 count = 0;
    f32 max_abs = 0.0f;
    bool has_nan = false;

    void Add(f32 a, f32 b)
    {
        const f32 diff = a - b;
        if (IsNaN(diff))
        {
            has_nan = true;
            return;
        }
        const f32 magnitude = Abs(diff);
        if (magnitude > max_abs)
        {
            max_abs = magnitude;
        }
        sum_squares += static_cast<f64>(diff) * static_cast<f64>(diff);
        count += 1;
    }

    CompareResult Finish(const RenderTolerance& tolerance) const
    {
        CompareResult result;
        result.max_abs_error = max_abs;
        f64 rms = 0.0;
        if (count > 0)
        {
            rms = Sqrt(sum_squares / static_cast<f64>(count));
        }
        result.rms_error_db = rms > 0.0 ? static_cast<f32>(kDbPerNeper * Ln(rms)) : kSilenceDb;
        result.equal = !has_nan && max_abs <= tolerance.max_abs_error && result.rms_error_db <= tolerance.rms_error_db;
        return result;
    }
};

CompareResult ShapeDiffers()
{
    CompareResult result;
    result.shape_differs = true;
    return result;
}

}  // namespace

BlockStats MeasureBlock(Span<const f32> block)
{
    BlockStats stats;
    if (block.Size() == 0)
    {
        return stats;
    }
    f64 sum_squares = 0.0;
    f32 peak = 0.0f;
    for (usize i = 0; i < block.Size(); ++i)
    {
        const f32 sample = block[i];
        if (IsNaN(sample))
        {
            stats.rms = sample;
            stats.peak = sample;
            return stats;
        }
        const f32 magnitude = Abs(sample);
        if (magnitude > peak)
        {
            peak = magnitude;
        }
        sum_squares += static_cast<f64>(sample) * static_cast<f64>(sample);
    }
    stats.rms = static_cast<f32>(Sqrt(sum_squares / static_cast<f64>(block.Size())));
    stats.peak = peak;
    return stats;
}

RenderSummary Summarize(Span<const f32> samples, usize block_samples, usize probe_count)
{
    ENGINE_ASSERT(block_samples > 0, "Summarize: block_samples must be greater than 0");
    RenderSummary summary;
    summary.sample_count = samples.Size();
    for (usize first = 0; first < samples.Size(); first += block_samples)
    {
        usize count = samples.Size() - first;
        if (count > block_samples)
        {
            count = block_samples;
        }
        summary.blocks.Push(MeasureBlock(Span<const f32>(samples.Data() + first, count)));
    }
    if (samples.Size() > 0)
    {
        for (usize i = 0; i < probe_count; ++i)
        {
            const u64 position = static_cast<u64>(i) * static_cast<u64>(samples.Size()) / static_cast<u64>(probe_count);
            summary.probes.Push(samples[static_cast<usize>(position)]);
        }
    }
    return summary;
}

CompareResult CompareSamples(Span<const f32> a, Span<const f32> b, const RenderTolerance& tolerance)
{
    if (a.Size() != b.Size())
    {
        return ShapeDiffers();
    }
    ErrorAccumulator errors;
    for (usize i = 0; i < a.Size(); ++i)
    {
        errors.Add(a[i], b[i]);
    }
    return errors.Finish(tolerance);
}

CompareResult CompareSummaries(const RenderSummary& a, const RenderSummary& b, const RenderTolerance& tolerance)
{
    if (a.blocks.Size() != b.blocks.Size() || a.probes.Size() != b.probes.Size() || a.sample_count != b.sample_count)
    {
        return ShapeDiffers();
    }
    ErrorAccumulator errors;
    for (usize i = 0; i < a.blocks.Size(); ++i)
    {
        errors.Add(a.blocks[i].rms, b.blocks[i].rms);
        errors.Add(a.blocks[i].peak, b.blocks[i].peak);
    }
    for (usize i = 0; i < a.probes.Size(); ++i)
    {
        errors.Add(a.probes[i], b.probes[i]);
    }
    return errors.Finish(tolerance);
}

}  // namespace vse::audio
