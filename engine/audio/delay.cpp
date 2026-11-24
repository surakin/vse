#include "audio/delay.h"

#include "containers/vector.h"
#include "cstr/mem_ops.h"
#include "lang/scalar.h"
#include "lang/utility.h"

namespace vse::audio
{

namespace
{

constexpr f32 kDelayFlush = 1e-30f;
constexpr f32 kDelayMaxFeedback = 0.995f;
constexpr f32 kDelayMaxGain = 16.0f;        // the largest wet or dry gain a block may ask for
constexpr f32 kDelayMaxLineGain = 0.9999f;  // a reverb line must lose energy on every round trip

f32 DelayFlush(f32 v)
{
    return (v < kDelayFlush && v > -kDelayFlush) ? 0.0f : v;
}

f32 DelayClamp01(f32 v)
{
    return !(v > 0.0f) ? 0.0f : (v > 1.0f ? 1.0f : v);
}

// A value kept in [low, high]; NaN becomes `low`, an infinity the nearer bound (`SetParams` of every effect: a block
// that is out of range is clamped, never trusted).
f32 DelayLimit(f32 v, f32 low, f32 high)
{
    return !(v > low) ? low : (v > high ? high : v);
}

// The coefficient of the one-pole low-pass of the damping: 1 passes everything, a smaller value filters more.
f32 DampCoefficient(f32 damping)
{
    return 1.0f - 0.95f * DelayClamp01(damping);
}

// ---- The delay ----
// Parameters: v0 delay in frames, v1 feedback, v2 wet, v3 dry, v4 damping coefficient.
class DelayEffect final : public Effect
{
public:
    bool Init(u32 channels, u32 max_frames, u32 ramp_frames)
    {
        if (channels == 0 || channels > 8 || max_frames < 2)
        {
            return false;
        }
        channels_ = channels;
        max_frames_ = max_frames;
        capacity_ = max_frames + 2;
        ramp_ = ramp_frames;
        if (!buffer_.TryResizeUninitialized(static_cast<usize>(capacity_) * channels))
        {
            return false;
        }
        current_[0] = 1.0f;
        target_[0] = 1.0f;
        current_[1] = 0.0f;
        target_[1] = 0.0f;
        current_[2] = 0.0f;
        target_[2] = 0.0f;
        current_[3] = 1.0f;
        target_[3] = 1.0f;
        current_[4] = 1.0f;
        target_[4] = 1.0f;
        Reset();
        return true;
    }

    void Process(f32* samples, u32 frames) override
    {
        for (u32 n = 0; n < frames; ++n)
        {
            if (steps_left_ > 0)
            {
                steps_left_ -= 1;
                for (u32 i = 0; i < 5; ++i)
                {
                    current_[i] = steps_left_ == 0 ? target_[i] : current_[i] + delta_[i];
                }
            }
            const f32 delay = current_[0] < 1.0f
                                  ? 1.0f
                                  : (current_[0] > static_cast<f32>(max_frames_) ? static_cast<f32>(max_frames_)
                                                                                 : current_[0]);
            const u32 whole = static_cast<u32>(delay);
            const f32 fraction = delay - static_cast<f32>(whole);
            const f32 feedback = current_[1] < 0.0f
                                     ? 0.0f
                                     : (current_[1] > kDelayMaxFeedback ? kDelayMaxFeedback : current_[1]);
            f32* frame = samples + static_cast<usize>(n) * channels_;
            const u32 i0 = (write_ + capacity_ - whole) % capacity_;
            const u32 i1 = (i0 + capacity_ - 1) % capacity_;
            for (u32 c = 0; c < channels_; ++c)
            {
                const f32 a = buffer_[static_cast<usize>(i0) * channels_ + c];
                const f32 b = buffer_[static_cast<usize>(i1) * channels_ + c];
                const f32 delayed = fraction == 0.0f ? a : a + (b - a) * fraction;
                lowpass_[c] = lowpass_[c] + current_[4] * (delayed - lowpass_[c]);
                lowpass_[c] = DelayFlush(lowpass_[c]);
                const f32 x = frame[c];
                buffer_[static_cast<usize>(write_) * channels_ + c] = DelayFlush(x + feedback * lowpass_[c]);
                frame[c] = current_[3] * x + current_[2] * delayed;
            }
            write_ = write_ + 1 == capacity_ ? 0 : write_ + 1;
        }
    }

    void SetParams(const EffectParams& params) override
    {
        // The ranges: the delay inside the buffer, the feedback below one, the gains finite, the damping coefficient in
        // the range `DampCoefficient` makes.
        target_[0] = DelayLimit(params.v[0], 0.0f, static_cast<f32>(max_frames_));
        target_[1] = DelayLimit(params.v[1], 0.0f, kDelayMaxFeedback);
        target_[2] = DelayLimit(params.v[2], 0.0f, kDelayMaxGain);
        target_[3] = DelayLimit(params.v[3], 0.0f, kDelayMaxGain);
        target_[4] = DelayLimit(params.v[4], 0.05f, 1.0f);
        if (ramp_ == 0)
        {
            for (u32 i = 0; i < 5; ++i)
            {
                current_[i] = target_[i];
            }
            steps_left_ = 0;
        }
        else
        {
            for (u32 i = 0; i < 5; ++i)
            {
                delta_[i] = (target_[i] - current_[i]) / static_cast<f32>(ramp_);
            }
            steps_left_ = ramp_;
        }
    }

    void Reset() override
    {
        MemSet(buffer_.Data(), 0, buffer_.Size() * sizeof(f32));
        for (u32 c = 0; c < 8; ++c)
        {
            lowpass_[c] = 0.0f;
        }
        write_ = 0;
    }

    u32 TailFrames(const EffectParams& params) const override
    {
        const f64 delay = params.v[0];
        const f64 feedback = params.v[1];
        if (!(params.v[2] > 0.0f))
        {
            return 0;
        }
        if (!(feedback > 0.0))
        {
            return static_cast<u32>(delay) + 1;
        }
        const f64 fb = feedback > kDelayMaxFeedback ? kDelayMaxFeedback : feedback;
        // The repeats until they fall below 1e-30.
        const f64 repeats = 69.0 / (-Ln(fb)) + 1.0;
        const f64 frames = (repeats + 1.0) * delay;
        return frames > 4294967000.0 ? 0xFFFFFFF0u : static_cast<u32>(frames);
    }

    u32 Channels() const override { return channels_; }

    void SaveState(StateOut* out) const override
    {
        out->U32(steps_left_);
        out->U32(write_);
        out->F32s(current_, 5);
        out->F32s(target_, 5);
        out->F32s(delta_, 5);
        out->F32s(lowpass_, 8);
        out->Floats(buffer_);
    }

    void LoadState(StateIn* in) override
    {
        steps_left_ = in->U32();
        write_ = in->U32();
        in->F32s(current_, 5);
        in->F32s(target_, 5);
        in->F32s(delta_, 5);
        in->F32s(lowpass_, 8);
        in->Floats(&buffer_);
        if (steps_left_ > ramp_ || write_ >= capacity_)
        {
            in->Fail();
        }
    }

private:
    u32 channels_ = 0;
    u32 max_frames_ = 0;
    u32 capacity_ = 0;
    u32 ramp_ = 0;
    u32 steps_left_ = 0;
    u32 write_ = 0;
    f32 current_[5] = {};
    f32 target_[5] = {};
    f32 delta_[5] = {};
    f32 lowpass_[8] = {};
    Vector<f32> buffer_;
};

// ---- The reverb ----
// Parameters: v0..v7 the gain of each line, v8 wet, v9 dry, v10 damping coefficient, v11 pre-delay in frames.
constexpr u32 kLines = 8;
constexpr u32 kBaseLengths[kLines] = {1327, 1523, 1759, 1949, 2153, 2347, 2539, 2749};

bool DelayIsPrime(u32 n)
{
    if (n < 2)
    {
        return false;
    }
    for (u32 d = 2; d * d <= n; ++d)
    {
        if (n % d == 0)
        {
            return false;
        }
    }
    return true;
}

class ReverbEffect final : public Effect
{
public:
    bool Init(u32 channels, const ReverbDesc& desc)
    {
        if (channels == 0 || channels > 8 || desc.sample_rate == 0)
        {
            return false;
        }
        channels_ = channels;
        ReverbLineLengths(desc.sample_rate, desc.size, lengths_);
        u32 total = 0;
        for (u32 i = 0; i < kLines; ++i)
        {
            offsets_[i] = total;
            total += lengths_[i];
        }
        predelay_capacity_ = desc.sample_rate / 4 + 2;
        if (!lines_.TryResizeUninitialized(total) || !predelay_.TryResizeUninitialized(predelay_capacity_))
        {
            return false;
        }
        // Output weights: line i feeds channel (i mod channels), alternating in sign for a decorrelated field.
        u32 per_channel = (kLines + channels - 1) / channels;
        out_scale_ = 1.0f / Sqrt(static_cast<f32>(per_channel));
        const EffectParams params = MakeReverbParams(desc);
        SetParams(params);
        for (u32 i = 0; i < 12; ++i)
        {
            current_[i] = target_[i];
        }
        steps_left_ = 0;
        Reset();
        return true;
    }

    void Process(f32* samples, u32 frames) override
    {
        const f32 matrix_scale = 0.35355339059327373f;  // 1 / sqrt(8)
        const f32 mono_scale = 1.0f / static_cast<f32>(channels_);
        for (u32 n = 0; n < frames; ++n)
        {
            if (steps_left_ > 0)
            {
                steps_left_ -= 1;
                for (u32 i = 0; i < 12; ++i)
                {
                    current_[i] = steps_left_ == 0 ? target_[i] : current_[i] + delta_[i];
                }
            }
            f32* frame = samples + static_cast<usize>(n) * channels_;
            f32 mono = 0.0f;
            for (u32 c = 0; c < channels_; ++c)
            {
                mono += frame[c];
            }
            mono *= mono_scale;
            // The pre-delay.
            u32 pre = static_cast<u32>(current_[11] < 0.0f ? 0.0f : current_[11]);
            if (pre >= predelay_capacity_)
            {
                pre = predelay_capacity_ - 1;
            }
            predelay_[predelay_write_] = mono;
            const f32 input = predelay_[(predelay_write_ + predelay_capacity_ - pre) % predelay_capacity_];
            predelay_write_ = predelay_write_ + 1 == predelay_capacity_ ? 0 : predelay_write_ + 1;
            // Read each line's output, damp it, apply its gain.
            f32 taps[kLines];
            f32 s[kLines];
            for (u32 i = 0; i < kLines; ++i)
            {
                taps[i] = lines_[offsets_[i] + positions_[i]];
                damped_[i] = DelayFlush(damped_[i] + current_[10] * (taps[i] - damped_[i]));
                s[i] = damped_[i] * current_[i];
            }
            // The Hadamard matrix by three butterfly stages: adds and subtracts only.
            for (u32 stride = 1; stride < kLines; stride <<= 1)
            {
                for (u32 i = 0; i < kLines; i += stride << 1)
                {
                    for (u32 j = i; j < i + stride; ++j)
                    {
                        const f32 a = s[j];
                        const f32 b = s[j + stride];
                        s[j] = a + b;
                        s[j + stride] = a - b;
                    }
                }
            }
            for (u32 i = 0; i < kLines; ++i)
            {
                lines_[offsets_[i] + positions_[i]] = DelayFlush(input + s[i] * matrix_scale);
                positions_[i] = positions_[i] + 1 == lengths_[i] ? 0 : positions_[i] + 1;
            }
            // The output: line i goes to channel (i mod channels), odd lines inverted.
            f32 wet[8] = {};
            for (u32 i = 0; i < kLines; ++i)
            {
                const u32 c = i % channels_;
                wet[c] += ((i / channels_) & 1u) != 0u ? -taps[i] : taps[i];
            }
            for (u32 c = 0; c < channels_; ++c)
            {
                frame[c] = current_[9] * frame[c] + current_[8] * out_scale_ * wet[c];
            }
        }
    }

    void SetParams(const EffectParams& params) override
    {
        // The line gains below one (a line at one or more never decays), the mix gains finite, the damping coefficient
        // and the pre-delay inside their ranges.
        for (u32 i = 0; i < kLines; ++i)
        {
            target_[i] = DelayLimit(params.v[i], 0.0f, kDelayMaxLineGain);
        }
        target_[8] = DelayLimit(params.v[8], 0.0f, kDelayMaxGain);
        target_[9] = DelayLimit(params.v[9], 0.0f, kDelayMaxGain);
        target_[10] = DelayLimit(params.v[10], 0.05f, 1.0f);
        target_[11] = DelayLimit(params.v[11], 0.0f, static_cast<f32>(predelay_capacity_ - 1));
        if (ramp_ == 0)
        {
            for (u32 i = 0; i < 12; ++i)
            {
                current_[i] = target_[i];
            }
            steps_left_ = 0;
        }
        else
        {
            for (u32 i = 0; i < 12; ++i)
            {
                delta_[i] = (target_[i] - current_[i]) / static_cast<f32>(ramp_);
            }
            steps_left_ = ramp_;
        }
    }

    void Reset() override
    {
        MemSet(lines_.Data(), 0, lines_.Size() * sizeof(f32));
        MemSet(predelay_.Data(), 0, predelay_.Size() * sizeof(f32));
        for (u32 i = 0; i < kLines; ++i)
        {
            positions_[i] = 0;
            damped_[i] = 0.0f;
        }
        predelay_write_ = 0;
    }

    u32 TailFrames(const EffectParams& params) const override
    {
        const f64 g = params.v[0];
        if (!(params.v[8] > 0.0f))
        {
            return 0;
        }
        if (!(g > 0.0) || !(g < 1.0))
        {
            return g <= 0.0 ? lengths_[0] : (1u << 24);
        }
        // Line 0 loses -60 dB (a factor 1000) over decay frames; the tail runs until 1e-30, i.e. 10 times the decay.
        const f64 decay = -3.0 * static_cast<f64>(lengths_[0]) / (Ln(g) / 2.302585092994046);
        const f64 frames = params.v[11] + decay * 10.0 + 1.0;
        return frames > 16777216.0 ? (1u << 24) : static_cast<u32>(frames);
    }

    u32 Channels() const override { return channels_; }

    void SaveState(StateOut* out) const override
    {
        out->U32(steps_left_);
        out->U32(predelay_write_);
        for (u32 i = 0; i < kLines; ++i)
        {
            out->U32(positions_[i]);
        }
        out->F32s(damped_, kLines);
        out->F32s(current_, 12);
        out->F32s(target_, 12);
        out->F32s(delta_, 12);
        out->Floats(lines_);
        out->Floats(predelay_);
    }

    void LoadState(StateIn* in) override
    {
        steps_left_ = in->U32();
        predelay_write_ = in->U32();
        for (u32 i = 0; i < kLines; ++i)
        {
            positions_[i] = in->U32();
            if (positions_[i] >= lengths_[i])
            {
                in->Fail();
            }
        }
        in->F32s(damped_, kLines);
        in->F32s(current_, 12);
        in->F32s(target_, 12);
        in->F32s(delta_, 12);
        in->Floats(&lines_);
        in->Floats(&predelay_);
        if (steps_left_ > ramp_ || predelay_write_ >= predelay_capacity_)
        {
            in->Fail();
        }
    }

private:
    u32 channels_ = 0;
    u32 ramp_ = 0;
    u32 steps_left_ = 0;
    u32 lengths_[kLines] = {};
    u32 offsets_[kLines] = {};
    u32 positions_[kLines] = {};
    f32 damped_[kLines] = {};
    u32 predelay_capacity_ = 0;
    u32 predelay_write_ = 0;
    f32 out_scale_ = 1.0f;
    f32 current_[12] = {};
    f32 target_[12] = {};
    f32 delta_[12] = {};
    Vector<f32> lines_;
    Vector<f32> predelay_;
};

}  // namespace

EffectParams MakeDelayParams(u32 sample_rate, const DelayDesc& desc)
{
    EffectParams params;
    const f32 seconds = !(desc.delay_seconds > 0.0f) ? 0.0f : desc.delay_seconds;
    params.v[0] = seconds * static_cast<f32>(sample_rate);
    params.v[1] = !(desc.feedback > 0.0f) ? 0.0f
                                          : (desc.feedback > kDelayMaxFeedback ? kDelayMaxFeedback : desc.feedback);
    params.v[2] = !(desc.wet > 0.0f) ? 0.0f : desc.wet;
    params.v[3] = !(desc.dry > 0.0f) ? 0.0f : desc.dry;
    params.v[4] = DampCoefficient(desc.damping);
    return params;
}

UniquePtr<Effect> MakeDelayEffect(u32 channels, u32 sample_rate, f32 max_delay_seconds, u32 ramp_frames)
{
    if (sample_rate == 0 || !(max_delay_seconds >= 0.001f) || max_delay_seconds > 10.0f)
    {
        return nullptr;
    }
    UniquePtr<DelayEffect> effect = MakeUnique<DelayEffect>();
    const u32 frames = static_cast<u32>(max_delay_seconds * static_cast<f32>(sample_rate)) + 1;
    if (effect == nullptr || !effect->Init(channels, frames, ramp_frames))
    {
        return nullptr;
    }
    return UniquePtr<Effect>(Move(effect));
}

ReverbDesc ReverbPresetDesc(ReverbPreset preset, u32 sample_rate)
{
    ReverbDesc desc;
    desc.sample_rate = sample_rate;
    switch (preset)
    {
    case ReverbPreset::SmallRoom:
        desc.size = 0.4f;
        desc.decay_seconds = 0.5f;
        desc.damping = 0.5f;
        desc.wet = 0.25f;
        desc.predelay_seconds = 0.002f;
        break;
    case ReverbPreset::Hall:
        desc.size = 2.0f;
        desc.decay_seconds = 2.2f;
        desc.damping = 0.35f;
        desc.wet = 0.3f;
        desc.predelay_seconds = 0.02f;
        break;
    case ReverbPreset::Cave:
        desc.size = 3.0f;
        desc.decay_seconds = 4.5f;
        desc.damping = 0.7f;
        desc.wet = 0.4f;
        desc.predelay_seconds = 0.04f;
        break;
    }
    return desc;
}

void ReverbLineLengths(u32 sample_rate, f32 size, u32 lengths[8])
{
    const f32 scale = (!(size > 0.05f) ? 0.05f : (size > 10.0f ? 10.0f : size)) * static_cast<f32>(sample_rate) /
                      48000.0f;
    u32 previous = 0;
    for (u32 i = 0; i < kLines; ++i)
    {
        u32 n = static_cast<u32>(static_cast<f32>(kBaseLengths[i]) * scale);
        n = n < 3 ? 3 : n;
        n = n <= previous ? previous + 1 : n;
        while (!DelayIsPrime(n))
        {
            n += 1;
        }
        lengths[i] = n;
        previous = n;
    }
}

EffectParams MakeReverbParams(const ReverbDesc& desc)
{
    EffectParams params;
    u32 lengths[kLines];
    ReverbLineLengths(desc.sample_rate, desc.size, lengths);
    const f64 decay = !(desc.decay_seconds > 0.05f) ? 0.05 : desc.decay_seconds;
    for (u32 i = 0; i < kLines; ++i)
    {
        // The line loses 60 dB (a factor 1000) in `decay` seconds: 10^(-3 length / (decay rate)) per round trip.
        params.v[i] = static_cast<f32>(
            Pow(10.0, -3.0 * static_cast<f64>(lengths[i]) / (decay * static_cast<f64>(desc.sample_rate))));
    }
    params.v[8] = !(desc.wet > 0.0f) ? 0.0f : desc.wet;
    params.v[9] = !(desc.dry > 0.0f) ? 0.0f : desc.dry;
    params.v[10] = DampCoefficient(desc.damping);
    const f32 pre = !(desc.predelay_seconds > 0.0f) ? 0.0f
                                                    : (desc.predelay_seconds > 0.25f ? 0.25f : desc.predelay_seconds);
    params.v[11] = pre * static_cast<f32>(desc.sample_rate);
    return params;
}

UniquePtr<Effect> MakeReverbEffect(u32 channels, const ReverbDesc& desc)
{
    UniquePtr<ReverbEffect> effect = MakeUnique<ReverbEffect>();
    if (effect == nullptr || !effect->Init(channels, desc))
    {
        return nullptr;
    }
    return UniquePtr<Effect>(Move(effect));
}

}  // namespace vse::audio
