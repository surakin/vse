#include "audio/block_hash.h"

#include "cstr/mem_ops.h"
#include "lang/scalar.h"

namespace engine_placeholder::audio
{

void BlockHasher::AddSample(f32 value)
{
    if (!IsFinite(value))
    {
        valid_ = false;
    }
    if (value == 0.0f)
    {
        value = 0.0f;  // -0.0 compares equal to 0.0 and is replaced by the positive zero
    }
    u32 bits = 0;
    MemCopy(&bits, &value, sizeof(bits));
    for (u32 shift = 0; shift < 32; shift += 8)
    {
        hash_ = (hash_ ^ static_cast<u64>((bits >> shift) & 0xFFu)) * kPrime;
    }
}

void BlockHasher::AddBlock(Span<const f32> samples)
{
    for (usize i = 0; i < samples.Size(); ++i)
    {
        AddSample(samples[i]);
    }
}

u64 HashBlock(Span<const f32> samples, bool* valid)
{
    BlockHasher hasher;
    hasher.AddBlock(samples);
    if (valid != nullptr)
    {
        *valid = hasher.Valid();
    }
    return hasher.Value();
}

}  // namespace engine_placeholder::audio
