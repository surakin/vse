#include "audio/filter.h"

#include "containers/vector.h"
#include "lang/scalar.h"
#include "lang/utility.h"

namespace engine_placeholder::audio
{

namespace
{

constexpr f64 kFilterPi = 3.14159265358979323846;
// A state smaller than this (-400 dB) is flushed to zero, so a decaying filter rings out to exact silence and never
// meets denormal numbers. A float biquad near the unit circle can otherwise stall at a rounding floor around 1e-28.
constexpr f32 kFilterFlush = 1e-20f;

// The parameter layout of a biquad: b0 b1 b2 a1 a2 (normalised by a0).
enum
{
    kB0,
    kB1,
    kB2,
    kA1,
    kA2,
    kCount
};

u32 TailOf(f64 radius)
{
    if (!(radius < 0.999999))
    {
        return 1u << 20;
    }
    const f64 frames = 70.0 / (1.0 - radius) + 1.0;
    return frames > 1048576.0 ? (1u << 20) : static_cast<u32>(frames);
}

class BiquadEffect final : public Effect
{
public:
    bool Init(u32 channels, u32 ramp_frames)
    {
        if (channels == 0 || channels > 8)
        {
            return false;
        }
        channels_ = channels;
        ramp_ = ramp_frames;
        const EffectParams identity = IdentityFilterParams();
        for (u32 i = 0; i < kCount; ++i)
        {
            current_[i] = identity.v[i];
            target_[i] = identity.v[i];
        }
        Reset();
        return true;
    }

    void Process(f32* samples, u32 frames) override
    {
        if (steps_left_ == 0 && IsIdentity(current_))
        {
            return;  // bit-transparent
        }
        for (u32 n = 0; n < frames; ++n)
        {
            if (steps_left_ > 0)
            {
                steps_left_ -= 1;
                for (u32 i = 0; i < kCount; ++i)
                {
                    current_[i] = steps_left_ == 0 ? target_[i] : current_[i] + delta_[i];
                }
            }
            f32* frame = samples + static_cast<usize>(n) * channels_;
            for (u32 c = 0; c < channels_; ++c)
            {
                const f32 x = frame[c];
                const f32 y = current_[kB0] * x + z1_[c];
                f32 z1 = current_[kB1] * x - current_[kA1] * y + z2_[c];
                f32 z2 = current_[kB2] * x - current_[kA2] * y;
                // Both states are flushed together: clearing one alone would restart the ring-out from a new state.
                const bool tiny = z1 < kFilterFlush && z1 > -kFilterFlush && z2 < kFilterFlush && z2 > -kFilterFlush;
                z1_[c] = tiny ? 0.0f : z1;
                z2_[c] = tiny ? 0.0f : z2;
                frame[c] = y;
            }
        }
        if (steps_left_ == 0 && IsIdentity(current_))
        {
            for (u32 c = 0; c < 8; ++c)
            {
                z1_[c] = 0.0f;
                z2_[c] = 0.0f;
            }
        }
    }

    void SetParams(const EffectParams& params) override
    {
        for (u32 i = 0; i < kCount; ++i)
        {
            target_[i] = params.v[i];
        }
        if (ramp_ == 0)
        {
            for (u32 i = 0; i < kCount; ++i)
            {
                current_[i] = target_[i];
            }
            steps_left_ = 0;
        }
        else
        {
            const f32 inverse = 1.0f / static_cast<f32>(ramp_);
            for (u32 i = 0; i < kCount; ++i)
            {
                delta_[i] = (target_[i] - current_[i]) * inverse;
            }
            steps_left_ = ramp_;
        }
    }

    void Reset() override
    {
        for (u32 c = 0; c < 8; ++c)
        {
            z1_[c] = 0.0f;
            z2_[c] = 0.0f;
        }
    }

    u32 TailFrames(const EffectParams& params) const override
    {
        const f64 a1 = params.v[kA1];
        const f64 a2 = params.v[kA2];
        if (a1 == 0.0 && a2 == 0.0 && params.v[kB1] == 0.0 && params.v[kB2] == 0.0)
        {
            return 0;
        }
        const f64 disc = a1 * a1 - 4.0 * a2;
        const f64 radius = disc < 0.0 ? Sqrt(a2 < 0.0 ? -a2 : a2) : ((a1 < 0.0 ? -a1 : a1) + Sqrt(disc)) * 0.5;
        return TailOf(radius);
    }

    u32 Channels() const override { return channels_; }

    void SaveState(StateOut* out) const override
    {
        out->U32(steps_left_);
        out->F32s(current_, kCount);
        out->F32s(target_, kCount);
        out->F32s(delta_, kCount);
        out->F32s(z1_, 8);
        out->F32s(z2_, 8);
    }

    void LoadState(StateIn* in) override
    {
        steps_left_ = in->U32();
        in->F32s(current_, kCount);
        in->F32s(target_, kCount);
        in->F32s(delta_, kCount);
        in->F32s(z1_, 8);
        in->F32s(z2_, 8);
        if (steps_left_ > ramp_)
        {
            in->Fail();
        }
    }

private:
    static bool IsIdentity(const f32* c)
    {
        return c[kB0] == 1.0f && c[kB1] == 0.0f && c[kB2] == 0.0f && c[kA1] == 0.0f && c[kA2] == 0.0f;
    }

    u32 channels_ = 0;
    u32 ramp_ = 0;
    u32 steps_left_ = 0;
    f32 current_[kCount] = {};
    f32 target_[kCount] = {};
    f32 delta_[kCount] = {};
    f32 z1_[8] = {};
    f32 z2_[8] = {};
};

class OnePoleEffect final : public Effect
{
public:
    bool Init(u32 channels, u32 ramp_frames)
    {
        if (channels == 0 || channels > 8)
        {
            return false;
        }
        channels_ = channels;
        ramp_ = ramp_frames;
        Reset();
        return true;
    }

    void Process(f32* samples, u32 frames) override
    {
        if (steps_left_ == 0 && current_ >= 1.0f)
        {
            return;  // open: bit-transparent
        }
        for (u32 n = 0; n < frames; ++n)
        {
            if (steps_left_ > 0)
            {
                steps_left_ -= 1;
                current_ = steps_left_ == 0 ? target_ : current_ + delta_;
            }
            f32* frame = samples + static_cast<usize>(n) * channels_;
            for (u32 c = 0; c < channels_; ++c)
            {
                f32 y = state_[c] + current_ * (frame[c] - state_[c]);
                y = (y < kFilterFlush && y > -kFilterFlush) ? 0.0f : y;
                state_[c] = y;
                frame[c] = y;
            }
        }
    }

    void SetParams(const EffectParams& params) override
    {
        target_ = params.v[0] < 0.0f ? 0.0f : (params.v[0] > 1.0f ? 1.0f : params.v[0]);
        if (ramp_ == 0)
        {
            current_ = target_;
            steps_left_ = 0;
        }
        else
        {
            delta_ = (target_ - current_) / static_cast<f32>(ramp_);
            steps_left_ = ramp_;
        }
    }

    void Reset() override
    {
        for (u32 c = 0; c < 8; ++c)
        {
            state_[c] = 0.0f;
        }
    }

    u32 TailFrames(const EffectParams& params) const override
    {
        const f64 a = params.v[0];
        if (a >= 1.0)
        {
            return 0;
        }
        return a <= 0.0 ? (1u << 20) : TailOf(1.0 - a);
    }

    u32 Channels() const override { return channels_; }

    void SaveState(StateOut* out) const override
    {
        out->U32(steps_left_);
        out->F32(current_);
        out->F32(target_);
        out->F32(delta_);
        out->F32s(state_, 8);
    }

    void LoadState(StateIn* in) override
    {
        steps_left_ = in->U32();
        current_ = in->F32();
        target_ = in->F32();
        delta_ = in->F32();
        in->F32s(state_, 8);
        if (steps_left_ > ramp_)
        {
            in->Fail();
        }
    }

private:
    u32 channels_ = 0;
    u32 ramp_ = 0;
    u32 steps_left_ = 0;
    f32 current_ = 1.0f;
    f32 target_ = 1.0f;
    f32 delta_ = 0.0f;
    f32 state_[8] = {};
};

}  // namespace

EffectParams IdentityFilterParams()
{
    EffectParams params;
    params.v[kB0] = 1.0f;
    return params;
}

FilterDesign DesignFilter(FilterType type, u32 sample_rate, f32 cutoff_hz, f32 q, f32 gain_db)
{
    FilterDesign design;
    design.params = IdentityFilterParams();
    if (sample_rate == 0 || !(cutoff_hz == cutoff_hz) || !(q == q) || !(gain_db == gain_db))
    {
        design.clamped = true;
        return design;
    }
    const f64 rate = static_cast<f64>(sample_rate);
    f64 fc = cutoff_hz;
    f64 qq = q;
    f64 db = gain_db;
    const f64 top = 0.49 * rate;
    if (fc < 10.0 || fc > top || qq < 0.1 || qq > 20.0 || db < -40.0 || db > 40.0)
    {
        design.clamped = true;
        fc = fc < 10.0 ? 10.0 : (fc > top ? top : fc);
        qq = qq < 0.1 ? 0.1 : (qq > 20.0 ? 20.0 : qq);
        db = db < -40.0 ? -40.0 : (db > 40.0 ? 40.0 : db);
    }
    const f64 w0 = 2.0 * kFilterPi * fc / rate;
    const f64 cw = Cos(w0);
    const f64 alpha = Sin(w0) / (2.0 * qq);
    const f64 amp = Pow(10.0, db / 40.0);
    const f64 root = Sqrt(amp);
    f64 b0 = 1.0, b1 = 0.0, b2 = 0.0, a0 = 1.0, a1 = 0.0, a2 = 0.0;
    switch (type)
    {
    case FilterType::LowPass:
        b0 = (1.0 - cw) * 0.5;
        b1 = 1.0 - cw;
        b2 = b0;
        a0 = 1.0 + alpha;
        a1 = -2.0 * cw;
        a2 = 1.0 - alpha;
        break;
    case FilterType::HighPass:
        b0 = (1.0 + cw) * 0.5;
        b1 = -(1.0 + cw);
        b2 = b0;
        a0 = 1.0 + alpha;
        a1 = -2.0 * cw;
        a2 = 1.0 - alpha;
        break;
    case FilterType::BandPass:
        b0 = alpha;
        b1 = 0.0;
        b2 = -alpha;
        a0 = 1.0 + alpha;
        a1 = -2.0 * cw;
        a2 = 1.0 - alpha;
        break;
    case FilterType::Notch:
        b0 = 1.0;
        b1 = -2.0 * cw;
        b2 = 1.0;
        a0 = 1.0 + alpha;
        a1 = -2.0 * cw;
        a2 = 1.0 - alpha;
        break;
    case FilterType::Peaking:
        b0 = 1.0 + alpha * amp;
        b1 = -2.0 * cw;
        b2 = 1.0 - alpha * amp;
        a0 = 1.0 + alpha / amp;
        a1 = -2.0 * cw;
        a2 = 1.0 - alpha / amp;
        break;
    case FilterType::LowShelf:
        b0 = amp * ((amp + 1.0) - (amp - 1.0) * cw + 2.0 * root * alpha);
        b1 = 2.0 * amp * ((amp - 1.0) - (amp + 1.0) * cw);
        b2 = amp * ((amp + 1.0) - (amp - 1.0) * cw - 2.0 * root * alpha);
        a0 = (amp + 1.0) + (amp - 1.0) * cw + 2.0 * root * alpha;
        a1 = -2.0 * ((amp - 1.0) + (amp + 1.0) * cw);
        a2 = (amp + 1.0) + (amp - 1.0) * cw - 2.0 * root * alpha;
        break;
    case FilterType::HighShelf:
        b0 = amp * ((amp + 1.0) + (amp - 1.0) * cw + 2.0 * root * alpha);
        b1 = -2.0 * amp * ((amp - 1.0) + (amp + 1.0) * cw);
        b2 = amp * ((amp + 1.0) + (amp - 1.0) * cw - 2.0 * root * alpha);
        a0 = (amp + 1.0) - (amp - 1.0) * cw + 2.0 * root * alpha;
        a1 = 2.0 * ((amp - 1.0) - (amp + 1.0) * cw);
        a2 = (amp + 1.0) - (amp - 1.0) * cw - 2.0 * root * alpha;
        break;
    }
    const f64 n1 = a1 / a0;
    const f64 n2 = a2 / a0;
    // Poles inside the unit circle: |a2| < 1 and |a1| < 1 + a2.
    const bool stable = n2 > -1.0 && n2 < 1.0 && (n1 < 0.0 ? -n1 : n1) < 1.0 + n2;
    if (!stable || !(a0 == a0))
    {
        design.clamped = true;
        return design;
    }
    design.params.v[kB0] = static_cast<f32>(b0 / a0);
    design.params.v[kB1] = static_cast<f32>(b1 / a0);
    design.params.v[kB2] = static_cast<f32>(b2 / a0);
    design.params.v[kA1] = static_cast<f32>(n1);
    design.params.v[kA2] = static_cast<f32>(n2);
    return design;
}

f32 FilterMagnitude(const EffectParams& params, u32 sample_rate, f32 frequency_hz)
{
    const f64 w = 2.0 * kFilterPi * static_cast<f64>(frequency_hz) / static_cast<f64>(sample_rate);
    const f64 c1 = Cos(w);
    const f64 s1 = Sin(w);
    const f64 c2 = Cos(2.0 * w);
    const f64 s2 = Sin(2.0 * w);
    const f64 nr = params.v[kB0] + params.v[kB1] * c1 + params.v[kB2] * c2;
    const f64 ni = -(params.v[kB1] * s1 + params.v[kB2] * s2);
    const f64 dr = 1.0 + params.v[kA1] * c1 + params.v[kA2] * c2;
    const f64 di = -(params.v[kA1] * s1 + params.v[kA2] * s2);
    return static_cast<f32>(Sqrt((nr * nr + ni * ni) / (dr * dr + di * di)));
}

UniquePtr<Effect> MakeFilterEffect(u32 channels, u32 ramp_frames)
{
    UniquePtr<BiquadEffect> effect = MakeUnique<BiquadEffect>();
    if (effect == nullptr || !effect->Init(channels, ramp_frames))
    {
        return nullptr;
    }
    return UniquePtr<Effect>(Move(effect));
}

EffectParams OnePoleParams(u32 sample_rate, f32 cutoff_hz)
{
    EffectParams params;
    params.v[0] = 1.0f;
    if (sample_rate == 0 || !(cutoff_hz == cutoff_hz))
    {
        return params;
    }
    const f64 fc = cutoff_hz < 1.0f ? 1.0 : cutoff_hz;
    if (fc >= 0.49 * static_cast<f64>(sample_rate))
    {
        return params;
    }
    params.v[0] = static_cast<f32>(1.0 - Exp(-2.0 * kFilterPi * fc / static_cast<f64>(sample_rate)));
    return params;
}

UniquePtr<Effect> MakeOnePoleEffect(u32 channels, u32 ramp_frames)
{
    UniquePtr<OnePoleEffect> effect = MakeUnique<OnePoleEffect>();
    if (effect == nullptr || !effect->Init(channels, ramp_frames))
    {
        return nullptr;
    }
    return UniquePtr<Effect>(Move(effect));
}

}  // namespace engine_placeholder::audio
