#include "audio/limiter.h"

#include "cstr/mem_ops.h"
#include "lang/scalar.h"

namespace engine_placeholder::audio
{

f32 DbToGain(f32 db)
{
    if (!(db > -1000.0f))
    {
        return 0.0f;
    }
    return static_cast<f32>(Pow(10.0, static_cast<f64>(db) / 20.0));
}

f32 GainToDb(f32 gain)
{
    if (!(gain > 0.0f))
    {
        return -1000.0f;
    }
    return static_cast<f32>(20.0 * Ln(static_cast<f64>(gain)) / 2.302585092994046);
}

bool Limiter::Init(u32 channels, const LimiterConfig& config)
{
    if (channels == 0 || channels > 8 || !(config.threshold > 0.0f) || config.lookahead_frames > 4096 ||
        !(config.release_frames >= 0.0f))
    {
        return false;
    }
    config_ = config;
    channels_ = channels;
    const u32 length = config.lookahead_frames + 1;
    delay_.Reset();
    peaks_.Reset();
    gains_.Reset();
    if (!delay_.TryResizeUninitialized(static_cast<usize>(config.lookahead_frames) * channels) ||
        !peaks_.TryResizeUninitialized(length) || !gains_.TryResizeUninitialized(length))
    {
        return false;
    }
    // The release moves the gain a fraction of the way back to 1 each frame: 1 - exp(-1 / frames) without libm in the
    // hot path (it is computed once, here).
    release_coef_ = config.release_frames > 0.0f
                        ? static_cast<f32>(1.0 - Exp(-1.0 / static_cast<f64>(config.release_frames)))
                        : 1.0f;
    Reset();
    return true;
}

void Limiter::Reset()
{
    MemSet(delay_.Data(), 0, delay_.Size() * sizeof(f32));
    for (u32 i = 0; i < peaks_.Size(); ++i)
    {
        peaks_[i] = 0.0f;
        gains_[i] = 1.0f;
    }
    position_ = 0;
    delay_position_ = 0;
    gain_ = 1.0f;
    min_gain_ = 1.0f;
}

void Limiter::Process(f32* samples, u32 frames)
{
    const u32 length = config_.lookahead_frames + 1;
    const f32 inverse_length = 1.0f / static_cast<f32>(length);
    f32 lowest = 1.0f;
    for (u32 n = 0; n < frames; ++n)
    {
        f32* frame = samples + static_cast<usize>(n) * channels_;
        f32 peak = 0.0f;
        for (u32 c = 0; c < channels_; ++c)
        {
            const f32 value = frame[c];
            const f32 magnitude = value < 0.0f ? -value : value;
            if (magnitude > peak)
            {
                peak = magnitude;
            }
        }
        if (config_.lookahead_frames > 0)
        {
            // The slot holds the frame from `lookahead` frames ago: it is the one to output now. Swap them.
            f32* slot = delay_.Data() + static_cast<usize>(delay_position_) * channels_;
            for (u32 c = 0; c < channels_; ++c)
            {
                const f32 old = slot[c];
                slot[c] = frame[c];
                frame[c] = old;
            }
            delay_position_ = delay_position_ + 1 == config_.lookahead_frames ? 0 : delay_position_ + 1;
        }
        peaks_[position_] = peak;
        f32 window = 0.0f;
        for (u32 i = 0; i < length; ++i)
        {
            if (peaks_[i] > window)
            {
                window = peaks_[i];
            }
        }
        gains_[position_] = window > config_.threshold ? config_.threshold / window : 1.0f;
        f32 sum = 0.0f;
        for (u32 i = 0; i < length; ++i)
        {
            sum += gains_[i];
        }
        const f32 mean = sum * inverse_length;
        // Down at once (the mean is already a ramp), up exponentially.
        gain_ = mean < gain_ ? mean : gain_ + (mean - gain_) * release_coef_;
        if (gain_ < 1.0f)
        {
            for (u32 c = 0; c < channels_; ++c)
            {
                frame[c] *= gain_;
            }
        }
        if (gain_ < lowest)
        {
            lowest = gain_;
        }
        position_ = position_ + 1 == length ? 0 : position_ + 1;
    }
    min_gain_ = lowest;
}

}  // namespace engine_placeholder::audio
