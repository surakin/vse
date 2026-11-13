#include "asset/content_hash.h"

#include "cstr/mem_ops.h"
#include "log/assert.h"

namespace engine_placeholder::asset
{

namespace
{

constexpr u32 kRoundConstants[64] = {
    0x428a2f98, 0x71374491, 0xb5c0fbcf, 0xe9b5dba5, 0x3956c25b, 0x59f111f1, 0x923f82a4, 0xab1c5ed5,
    0xd807aa98, 0x12835b01, 0x243185be, 0x550c7dc3, 0x72be5d74, 0x80deb1fe, 0x9bdc06a7, 0xc19bf174,
    0xe49b69c1, 0xefbe4786, 0x0fc19dc6, 0x240ca1cc, 0x2de92c6f, 0x4a7484aa, 0x5cb0a9dc, 0x76f988da,
    0x983e5152, 0xa831c66d, 0xb00327c8, 0xbf597fc7, 0xc6e00bf3, 0xd5a79147, 0x06ca6351, 0x14292967,
    0x27b70a85, 0x2e1b2138, 0x4d2c6dfc, 0x53380d13, 0x650a7354, 0x766a0abb, 0x81c2c92e, 0x92722c85,
    0xa2bfe8a1, 0xa81a664b, 0xc24b8b70, 0xc76c51a3, 0xd192e819, 0xd6990624, 0xf40e3585, 0x106aa070,
    0x19a4c116, 0x1e376c08, 0x2748774c, 0x34b0bcb5, 0x391c0cb3, 0x4ed8aa4a, 0x5b9cca4f, 0x682e6ff3,
    0x748f82ee, 0x78a5636f, 0x84c87814, 0x8cc70208, 0x90befffa, 0xa4506ceb, 0xbef9a3f7, 0xc67178f2};

constexpr u32 RotateRight(u32 x, u32 n)
{
    return (x >> n) | (x << (32 - n));
}

}  // namespace

bool ContentHash::IsZero() const
{
    for (u32 i = 0; i < 16; ++i)
    {
        if (bytes[i] != 0)
        {
            return false;
        }
    }
    return true;
}

bool operator==(const ContentHash& a, const ContentHash& b)
{
    return MemCompare(a.bytes, b.bytes, sizeof(a.bytes)) == 0;
}

void FormatContentHash(const ContentHash& hash, char (&out)[33])
{
    static const char digits[] = "0123456789abcdef";
    for (u32 i = 0; i < 16; ++i)
    {
        out[2 * i] = digits[hash.bytes[i] >> 4];
        out[2 * i + 1] = digits[hash.bytes[i] & 0xf];
    }
    out[32] = '\0';
}

ContentHasher::ContentHasher()
{
    Reset();
}

void ContentHasher::Reset()
{
    state_[0] = 0x6a09e667;
    state_[1] = 0xbb67ae85;
    state_[2] = 0x3c6ef372;
    state_[3] = 0xa54ff53a;
    state_[4] = 0x510e527f;
    state_[5] = 0x9b05688c;
    state_[6] = 0x1f83d9ab;
    state_[7] = 0x5be0cd19;
    total_ = 0;
    buffered_ = 0;
    finished_ = false;
}

void ContentHasher::Block(const u8* block)
{
    u32 w[64];
    for (u32 i = 0; i < 16; ++i)
    {
        w[i] = (static_cast<u32>(block[4 * i]) << 24) | (static_cast<u32>(block[4 * i + 1]) << 16) |
               (static_cast<u32>(block[4 * i + 2]) << 8) | static_cast<u32>(block[4 * i + 3]);
    }
    for (u32 i = 16; i < 64; ++i)
    {
        const u32 s0 = RotateRight(w[i - 15], 7) ^ RotateRight(w[i - 15], 18) ^ (w[i - 15] >> 3);
        const u32 s1 = RotateRight(w[i - 2], 17) ^ RotateRight(w[i - 2], 19) ^ (w[i - 2] >> 10);
        w[i] = w[i - 16] + s0 + w[i - 7] + s1;
    }
    u32 a = state_[0];
    u32 b = state_[1];
    u32 c = state_[2];
    u32 d = state_[3];
    u32 e = state_[4];
    u32 f = state_[5];
    u32 g = state_[6];
    u32 h = state_[7];
    for (u32 i = 0; i < 64; ++i)
    {
        const u32 big_s1 = RotateRight(e, 6) ^ RotateRight(e, 11) ^ RotateRight(e, 25);
        const u32 choose = (e & f) ^ (~e & g);
        const u32 t1 = h + big_s1 + choose + kRoundConstants[i] + w[i];
        const u32 big_s0 = RotateRight(a, 2) ^ RotateRight(a, 13) ^ RotateRight(a, 22);
        const u32 majority = (a & b) ^ (a & c) ^ (b & c);
        const u32 t2 = big_s0 + majority;
        h = g;
        g = f;
        f = e;
        e = d + t1;
        d = c;
        c = b;
        b = a;
        a = t1 + t2;
    }
    state_[0] += a;
    state_[1] += b;
    state_[2] += c;
    state_[3] += d;
    state_[4] += e;
    state_[5] += f;
    state_[6] += g;
    state_[7] += h;
}

void ContentHasher::Update(ByteSpan bytes)
{
    ENGINE_ASSERT(!finished_, "ContentHasher::Update: the hash is finished");
    const u8* data = reinterpret_cast<const u8*>(bytes.Data());
    usize count = bytes.Size();
    total_ += count;
    if (buffered_ != 0)
    {
        while (count != 0 && buffered_ < 64)
        {
            buffer_[buffered_++] = *data++;
            --count;
        }
        if (buffered_ == 64)
        {
            Block(buffer_);
            buffered_ = 0;
        }
    }
    while (count >= 64)
    {
        Block(data);
        data += 64;
        count -= 64;
    }
    while (count != 0)
    {
        buffer_[buffered_++] = *data++;
        --count;
    }
}

ContentHash ContentHasher::Finish()
{
    ENGINE_ASSERT(!finished_, "ContentHasher::Finish: the hash is finished already");
    finished_ = true;
    const u64 bit_count = total_ * 8;
    buffer_[buffered_++] = 0x80;
    if (buffered_ > 56)
    {
        while (buffered_ < 64)
        {
            buffer_[buffered_++] = 0;
        }
        Block(buffer_);
        buffered_ = 0;
    }
    while (buffered_ < 56)
    {
        buffer_[buffered_++] = 0;
    }
    for (u32 i = 0; i < 8; ++i)
    {
        buffer_[56 + i] = static_cast<u8>((bit_count >> (56 - 8 * i)) & 0xff);
    }
    Block(buffer_);
    ContentHash hash;
    for (u32 i = 0; i < 4; ++i)
    {
        hash.bytes[4 * i] = static_cast<u8>(state_[i] >> 24);
        hash.bytes[4 * i + 1] = static_cast<u8>(state_[i] >> 16);
        hash.bytes[4 * i + 2] = static_cast<u8>(state_[i] >> 8);
        hash.bytes[4 * i + 3] = static_cast<u8>(state_[i]);
    }
    return hash;
}

ContentHash HashBytes(ByteSpan bytes)
{
    ContentHasher hasher;
    hasher.Update(bytes);
    return hasher.Finish();
}

}  // namespace engine_placeholder::asset
