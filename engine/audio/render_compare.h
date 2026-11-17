// Comparing two renders within a tolerance (Phase 26 Day 415): the cross-machine check of the audio module
// (docs/superpowers/specs/2026-11-17-audio-design.md, section 4). One build renders the same bits run to run
// (`audio/block_hash.h` checks that); two machines may differ in the last bits of libm, so a render is summarised by
// per-block RMS and peak and a table of sample probes, and two summaries are equal when every difference is within the
// tolerance.
#pragma once

#include "containers/span.h"
#include "containers/vector.h"
#include "types.h"

namespace engine_placeholder::audio
{

/// How far two renders may differ and still count as equal.
///
/// - Thread-safety: thread-compatible plain data.
struct RenderTolerance
{
    /// The largest allowed absolute difference of one sample, block statistic or probe (1e-5 is -100 dBFS).
    f32 max_abs_error = 1e-5f;
    /// The largest allowed RMS of the difference, in dB relative to full scale (-100 dB).
    f32 rms_error_db = -100.0f;
};

/// The RMS and peak of one block.
///
/// - Thread-safety: thread-compatible plain data.
struct BlockStats
{
    /// The root mean square of the block's samples; 0 for an empty block.
    f32 rms = 0.0f;
    /// The largest absolute sample of the block.
    f32 peak = 0.0f;
};

/// What one render is reduced to for a cross-machine comparison: the statistics of each block and sample probes at
/// evenly spaced positions.
///
/// - Thread-safety: thread-compatible.
/// - Ownership: owns its tables.
struct RenderSummary
{
    /// The statistics of each block, in order.
    Vector<BlockStats> blocks;
    /// The samples at the probe positions, in order.
    Vector<f32> probes;
    /// The number of samples the render had.
    u64 sample_count = 0;
};

/// What a comparison found.
///
/// - Thread-safety: thread-compatible plain data.
struct CompareResult
{
    /// True when the renders have the same shape (block count, probe count, sample count) and every error is within the
    /// tolerance.
    bool equal = false;
    /// True when the two had different shapes, so nothing else was compared.
    bool shape_differs = false;
    /// The largest absolute difference found (samples, block RMS and peak, or probes, according to the function).
    f32 max_abs_error = 0.0f;
    /// The RMS of the difference in dB relative to full scale; -200 when the difference is exactly zero.
    f32 rms_error_db = -200.0f;
};

/// The RMS and peak of a block.
///
/// - Param block: the samples.
/// - Returns: the statistics; both 0 for an empty block. A NaN sample gives a NaN statistic.
/// - Thread-safety: thread-safe; pure.
BlockStats MeasureBlock(Span<const f32> block);

/// Reduces a render to a summary.
///
/// - Param samples: the whole render, interleaved.
/// - Param block_samples: how many samples one block holds (frames times channels); the last block may be shorter. Must
/// be greater than 0.
/// - Param probe_count: how many probes to take, at positions `i * samples.Size() / probe_count` (the table of the
/// committed references uses 64).
/// - Returns: the summary.
/// - Aborts: `block_samples` is 0.
/// - Thread-safety: thread-safe; pure.
RenderSummary Summarize(Span<const f32> samples, usize block_samples, usize probe_count);

/// Compares two renders sample by sample.
///
/// - Param a: the first render.
/// - Param b: the second render.
/// - Param tolerance: the limits.
/// - Returns: the largest absolute difference, the RMS of the difference in dB, and whether both are within the
/// tolerance. A NaN in either render is never
///   equal. Renders of different lengths have `shape_differs` set and are not equal.
/// - Thread-safety: thread-safe; pure.
CompareResult CompareSamples(Span<const f32> a, Span<const f32> b, const RenderTolerance& tolerance);

/// Compares two summaries, for a render that was reduced on another machine and committed as a table.
///
/// - Param a: the first summary.
/// - Param b: the second summary.
/// - Param tolerance: the limits; the error is measured on the block RMS and peak values and the probes.
/// - Returns: the largest absolute difference over those values, the RMS of all those differences in dB, and whether
/// both are within the tolerance.
///   Summaries of different shapes have `shape_differs` set and are not equal.
/// - Thread-safety: thread-safe; pure.
CompareResult CompareSummaries(const RenderSummary& a, const RenderSummary& b, const RenderTolerance& tolerance);

}  // namespace engine_placeholder::audio
