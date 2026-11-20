// Hostile input (Phase 19 Day 257): deterministic mutation of valid encodings of every field kind. Every result is a
// Status or a valid value; a decode that succeeds consumes exactly the bytes its re-encoding produces (the encoding is
// canonical); nothing reads outside the buffer (AddressSanitizer) or allocates above the limits.

#include <catch2/catch_test_macros.hpp>

#include "build_defs.h"
#include "cstr/mem_ops.h"
#include "storage/component_registry.h"

namespace vse::storage
{
namespace
{

using FuzzText = String<HeapAllocator>;

enum class FuzzMode : u8
{
    A = 0,
    B = 1,
    C = 2
};

class FuzzRng
{
public:
    explicit FuzzRng(u64 seed) : state_(seed != 0 ? seed : 0x9e3779b97f4a7c15ull) {}
    u64 Next()
    {
        state_ ^= state_ >> 12;
        state_ ^= state_ << 25;
        state_ ^= state_ >> 27;
        return state_ * 0x2545f4914f6cdd1dull;
    }
    u64 Below(u64 n) { return n == 0 ? 0 : Next() % n; }

private:
    u64 state_;
};

f32 FuzzF32(FuzzRng& r)
{
    const u32 bits = static_cast<u32>(r.Next());
    f32 v = 0;
    MemCopy(&v, &bits, sizeof(v));
    return v;
}
f64 FuzzF64(FuzzRng& r)
{
    const u64 bits = r.Next();
    f64 v = 0;
    MemCopy(&v, &bits, sizeof(v));
    return v;
}

template <class V> void FuzzSample(FuzzRng& r, V* out);
template <class V>
    requires(IsIntegral<V> && !IsSame<V, bool>)
void FuzzSample(FuzzRng& r, V* out)
{
    *out = static_cast<V>(r.Next());
}
void FuzzSample(FuzzRng& r, bool* out)
{
    *out = (r.Next() & 1) != 0;
}
void FuzzSample(FuzzRng& r, f32* out)
{
    *out = FuzzF32(r);
}
void FuzzSample(FuzzRng& r, f64* out)
{
    *out = FuzzF64(r);
}
void FuzzSample(FuzzRng& r, Vec2* out)
{
    *out = Vec2(FuzzF32(r), FuzzF32(r));
}
void FuzzSample(FuzzRng& r, Vec3* out)
{
    *out = Vec3(FuzzF32(r), FuzzF32(r), FuzzF32(r));
}
void FuzzSample(FuzzRng& r, Vec4* out)
{
    *out = Vec4(FuzzF32(r), FuzzF32(r), FuzzF32(r), FuzzF32(r));
}
void FuzzSample(FuzzRng& r, Quaternion* out)
{
    *out = Quaternion(FuzzF32(r), FuzzF32(r), FuzzF32(r), FuzzF32(r));
}
void FuzzSample(FuzzRng& r, Mat4* out)
{
    for (usize c = 0; c < 4; ++c)
    {
        out->col[c] = Vec4(FuzzF32(r), FuzzF32(r), FuzzF32(r), FuzzF32(r));
    }
}
void FuzzSample(FuzzRng& r, Aabb* out)
{
    Vec3 a;
    Vec3 b;
    FuzzSample(r, &a);
    FuzzSample(r, &b);
    out->min = a;
    out->max = b;
}
void FuzzSample(FuzzRng& r, FuzzMode* out)
{
    *out = static_cast<FuzzMode>(r.Below(3));
}
void FuzzSample(FuzzRng& r, EntityRef* out)
{
    out->key.high = r.Next();
    out->key.low = r.Next();
}
void FuzzSample(FuzzRng& r, FuzzText* out)
{
    static const char* const pieces[] = {"a", "Z", "0", " ", "\xc3\xa9", "\xe2\x82\xac", "\xf0\x9f\x98\x80"};
    FuzzText t(HeapAllocator::Default());
    const u64 count = r.Below(12);
    for (u64 i = 0; i < count; ++i)
    {
        t.Append(StringView(pieces[r.Below(7)]));
    }
    *out = Move(t);
}
void FuzzSample(FuzzRng& r, Array<u16, 4>* out)
{
    for (usize i = 0; i < 4; ++i)
    {
        (*out)[i] = static_cast<u16>(r.Next());
    }
}

using FuzzIntArray = Array<u16, 4>;

template <class V> struct FuzzOne
{
    V value{};
};
template <> struct FuzzOne<FuzzText>
{
    FuzzText value{HeapAllocator::Default()};
};

CodecLimits FuzzLimits()
{
    CodecLimits l;
    l.max_string_bytes = 24;
    l.max_array_elements = 4;
    l.max_record_bytes = 256;
    return l;
}

#if ENGINE_SANITIZER_THREAD
constexpr u32 kFuzzCases = 10000;  // ThreadSanitizer runs the same code about ten times slower; the plain and
                                   // AddressSanitizer runs do the full count
#else
constexpr u32 kFuzzCases = 100000;
#endif

// Applies one mutation to `bytes` (a copy of a valid encoding).
void FuzzMutate(FuzzRng& r, Vector<byte>* bytes)
{
    switch (r.Below(5))
    {
    case 0:
    {  // flip one to three bits
        if (bytes->Size() == 0)
        {
            break;
        }
        const u64 flips = 1 + r.Below(3);
        for (u64 i = 0; i < flips; ++i)
        {
            const usize at = static_cast<usize>(r.Below(bytes->Size()));
            (*bytes)[at] = static_cast<byte>(static_cast<u8>((*bytes)[at]) ^ (1u << r.Below(8)));
        }
        break;
    }
    case 1:
    {  // truncate
        bytes->Resize(static_cast<usize>(r.Below(bytes->Size() + 1)));
        break;
    }
    case 2:
    {  // overwrite a byte with a value that makes a long or oversized length
        if (bytes->Size() == 0)
        {
            break;
        }
        static const u8 interesting[] = {0x00, 0x7f, 0x80, 0xff, 0x01, 0xfe};
        (*bytes)[static_cast<usize>(r.Below(bytes->Size()))] = static_cast<byte>(interesting[r.Below(6)]);
        break;
    }
    case 3:
    {  // duplicate a slice of itself at the end
        if (bytes->Size() == 0)
        {
            break;
        }
        const usize from = static_cast<usize>(r.Below(bytes->Size()));
        const usize count = static_cast<usize>(r.Below(bytes->Size() - from) + 1);
        for (usize i = 0; i < count; ++i)
        {
            bytes->Push((*bytes)[from + i]);
        }
        break;
    }
    default:
    {  // replace everything by random bytes of a random length
        const usize count = static_cast<usize>(r.Below(40));
        bytes->Resize(count);
        for (usize i = 0; i < count; ++i)
        {
            (*bytes)[i] = static_cast<byte>(r.Next());
        }
        break;
    }
    }
}

template <class T> void FuzzKind(const char* name, u32 id)
{
    ComponentRegistry registry;
    REQUIRE(
        registry.Register<T>({.id = id, .name = name, .fields = {Field<&T::value>("value", FieldOptions::Enum(0, 2))}})
            .Ok());
    const RegisteredComponent* c = registry.FindById(id);
    const CodecLimits limits = FuzzLimits();
    FuzzRng rng(id * 7919ull);
    Vector<Vector<byte>> seeds;
    for (u32 i = 0; i < 16; ++i)
    {
        T value;
        FuzzSample(rng, &value.value);
        bytes::ByteWriter w;
        const Status s = c->Encode(&value, w, limits);
        if (s.Failed())
        {  // a random sample the encoder refuses (a string over the limit): not a seed
            continue;
        }
        Vector<byte> v;
        for (usize b = 0; b < w.Size(); ++b)
        {
            v.Push(w.Bytes()[b]);
        }
        seeds.Push(Move(v));
    }
    REQUIRE(seeds.Size() > 0);
    u32 accepted = 0;
    u32 rejected = 0;
    for (u32 n = 0; n < kFuzzCases; ++n)
    {
        Vector<byte> input;
        const Vector<byte>& seed = seeds[static_cast<usize>(rng.Below(seeds.Size()))];
        for (usize i = 0; i < seed.Size(); ++i)
        {
            input.Push(seed[i]);
        }
        if (n >= seeds.Size())
        {  // the first pass checks the unmutated seeds
            FuzzMutate(rng, &input);
        }
        T out;
        bytes::ByteReader in(ByteSpan(input.Data(), input.Size()));
        const Status s = c->Decode(in, &out, limits);
        if (s.Ok())
        {
            ++accepted;
            // Canonical: re-encoding the value gives back exactly the bytes that were consumed.
            bytes::ByteWriter again;
            const Status e = c->Encode(&out, again, limits);
            if (e.Failed() || again.Size() != in.Position() ||
                MemCompare(again.Bytes().Data(), input.Data(), in.Position()) != 0)
            {
                FAIL("a decoded value did not re-encode to the consumed bytes, kind " << name << ", case " << n);
            }
        }
        else
        {
            ++rejected;
            if (!s.Is(ErrorCode::Corrupt) && !s.Is(ErrorCode::LimitExceeded))
            {
                FAIL("an unexpected error code on hostile input, kind " << name << ": " << ErrorCodeName(s.Code()));
            }
            if (in.Position() > input.Size())
            {
                FAIL("the reader passed the end of its input, kind " << name);
            }
        }
    }
    CHECK(accepted > 0);
    CHECK(rejected > 0);
}

// NOLINTBEGIN(bugprone-macro-parentheses): TYPE is a template argument
#define FUZZ_KIND(TYPE, NAME, ID)                                                                                      \
    TEST_CASE("Fuzz: " NAME " survives hostile input", "[storage][fuzz]")                                              \
    {                                                                                                                  \
        FuzzKind<FuzzOne<TYPE>>(NAME, ID);                                                                             \
    }
// NOLINTEND(bugprone-macro-parentheses)

FUZZ_KIND(u8, "u8", 4001)
FUZZ_KIND(u16, "u16", 4002)
FUZZ_KIND(u32, "u32", 4003)
FUZZ_KIND(u64, "u64", 4004)
FUZZ_KIND(i8, "i8", 4005)
FUZZ_KIND(i16, "i16", 4006)
FUZZ_KIND(i32, "i32", 4007)
FUZZ_KIND(i64, "i64", 4008)
FUZZ_KIND(f32, "f32", 4009)
FUZZ_KIND(f64, "f64", 4010)
FUZZ_KIND(bool, "bool", 4011)
FUZZ_KIND(Vec2, "vec2", 4012)
FUZZ_KIND(Vec3, "vec3", 4013)
FUZZ_KIND(Vec4, "vec4", 4014)
FUZZ_KIND(Quaternion, "quat", 4015)
FUZZ_KIND(Mat4, "mat4", 4016)
FUZZ_KIND(Aabb, "aabb", 4017)
FUZZ_KIND(FuzzMode, "enum", 4018)
FUZZ_KIND(FuzzText, "string", 4019)
FUZZ_KIND(FuzzIntArray, "array", 4020)
FUZZ_KIND(EntityRef, "entity_ref", 4021)

}  // namespace
}  // namespace vse::storage
