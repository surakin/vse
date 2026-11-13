#pragma once

// Helpers shared by the asset tests (Phase 22 Day 314).

#include "containers/span.h"
#include "containers/vector.h"
#include "cstr/cstr.h"
#include "string_view.h"
#include "types.h"

namespace engine_placeholder::asset::test
{

/// The bytes of a C string, without the terminator.
inline ByteSpan BytesOf(const char* text)
{
    return ByteSpan(reinterpret_cast<const byte*>(text), CStrLength(text));
}

/// A deterministic pseudo-random generator (splitmix64) for fuzz tests.
class Rng
{
public:
    explicit Rng(u64 seed) : state_(seed) {}
    u64 Next()
    {
        state_ += 0x9e3779b97f4a7c15ull;
        u64 z = state_;
        z = (z ^ (z >> 30)) * 0xbf58476d1ce4e5b9ull;
        z = (z ^ (z >> 27)) * 0x94d049bb133111ebull;
        return z ^ (z >> 31);
    }
    u32 Below(u32 bound) { return static_cast<u32>(Next() % bound); }

private:
    u64 state_;
};

/// True when `message` contains `needle`.
inline bool Contains(const char* message, const char* needle)
{
    const StringView haystack(message, CStrLength(message));
    const StringView n(needle, CStrLength(needle));
    return haystack.Find(n) != haystack.Length();
}

}  // namespace engine_placeholder::asset::test
