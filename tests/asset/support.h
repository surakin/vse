#pragma once

// Helpers shared by the asset tests (Phase 22 Day 314).

#include <catch2/catch_test_macros.hpp>

#include "asset/blob.h"
#include "containers/span.h"
#include "containers/vector.h"
#include "cstr/cstr.h"
#include "io/file_system.h"
#include "io/path.h"
#include "os/console.h"
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

/// A StringView of a C string.
inline StringView Vp(const char* text)
{
    return StringView(text, CStrLength(text));
}

/// A resource id from a number.
inline ResourceId Rid(u64 value)
{
    return ResourceId::FromValue(value);
}

/// The bytes of a vector as a span.
inline ByteSpan AsBytes(const Vector<byte>& bytes)
{
    return ByteSpan(bytes.Data(), bytes.Size());
}

/// The type the backend tests mint their blobs with.
inline constexpr ResourceType kBackendTestType = ResourceType::FromChars('B', 'K', 'N', 'D');

/// A registry that knows the built-in types and `kBackendTestType`.
inline TypeRegistry MakeBackendRegistry()
{
    TypeRegistry registry;
    REQUIRE(registry.Register(kBackendTestType, 1, "backend test").Ok());
    return registry;
}

/// A blob of `id` whose one section holds `payload_bytes` pseudo-random bytes (seeded by `seed`).
inline Vector<byte> MakeTestBlob(const TypeRegistry& registry, ResourceId id, usize payload_bytes, u32 seed)
{
    Rng rng(seed);
    Vector<byte> payload;
    for (usize i = 0; i < payload_bytes; ++i)
    {
        payload.Push(static_cast<byte>(rng.Below(256)));
    }
    BlobWriter writer(registry, kBackendTestType, id);
    REQUIRE(writer.AddSection(16, payload).Ok());
    Vector<byte> blob;
    REQUIRE(writer.Finish(&blob).Ok());
    return blob;
}

/// A directory made for one test and removed with everything in it afterwards.
class TempDir
{
public:
    TempDir()
    {
        io::Path tmp;
        if (io::TempDirectory(&tmp).Failed() || io::CreateTempDirectory(tmp, "re_asset_test", &path_).Failed())
        {
            os::Abort();
        }
    }
    ~TempDir() { io::RemoveAll(path_); }
    TempDir(const TempDir&) = delete;
    TempDir& operator=(const TempDir&) = delete;

    const io::Path& Path() const { return path_; }

private:
    io::Path path_;
};

}  // namespace engine_placeholder::asset::test
