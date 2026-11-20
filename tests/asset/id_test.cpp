// Resource ids, resource types and virtual paths (Phase 22 Day 314).

#include <catch2/catch_test_macros.hpp>

#include "asset/resource_id.h"
#include "asset/resource_type.h"
#include "support.h"

namespace vse::asset
{

namespace
{

bool Normalizes(const char* in, const char* expected)
{
    String<HeapAllocator> out(HeapAllocator::Default());
    return NormalizePath(StringView(in, CStrLength(in)), &out).Ok() &&
           out.View() == StringView(expected, CStrLength(expected));
}

ErrorCode PathError(const char* in)
{
    return NormalizePath(StringView(in, CStrLength(in)), nullptr).Code();
}

}  // namespace

TEST_CASE("The null id is explicit and generated ids are never null", "[asset][id]")
{
    CHECK(ResourceId::None().IsNone());
    CHECK(ResourceId() == ResourceId::None());
    ResourceId a = GenerateResourceId();
    ResourceId b = GenerateResourceId();
    CHECK(!a.IsNone());
    CHECK(!b.IsNone());
    CHECK(a != b);  // 64 random bits: a repeat would be a defect
    CHECK(ResourceId::FromValue(1) < ResourceId::FromValue(2));
}

TEST_CASE("An id formats as 16 hex digits and parses back", "[asset][id]")
{
    char hex[17];
    FormatResourceId(ResourceId::FromValue(0x0123456789abcdefull), hex);
    CHECK(CStrCompare(hex, "0123456789abcdef") == 0);
    ResourceId parsed;
    CHECK(ParseResourceId(StringView(hex, 16), &parsed).Ok());
    CHECK(parsed.Value() == 0x0123456789abcdefull);
    CHECK(ParseResourceId(StringView("0123456789ABCDEF", 16), &parsed).Ok());
    CHECK(parsed.Value() == 0x0123456789abcdefull);
}

TEST_CASE("Malformed ids are refused and leave the output untouched", "[asset][id]")
{
    ResourceId out = ResourceId::FromValue(5);
    CHECK(ParseResourceId(StringView("", 0), &out).Is(ErrorCode::Invalid));
    CHECK(ParseResourceId(StringView("0123456789abcde", 15), &out).Is(ErrorCode::Invalid));
    CHECK(ParseResourceId(StringView("0123456789abcdef0", 17), &out).Is(ErrorCode::Invalid));
    CHECK(ParseResourceId(StringView("0123456789abcdeg", 16), &out).Is(ErrorCode::Invalid));
    CHECK(ParseResourceId(StringView("0000000000000000", 16), &out)
              .Is(ErrorCode::Invalid));  // the null id is never recorded
    CHECK(out.Value() == 5);
}

TEST_CASE("Valid paths normalize to themselves", "[asset][path]")
{
    CHECK(Normalizes("/a", "/a"));
    CHECK(Normalizes("/textures/rock.tex", "/textures/rock.tex"));
    CHECK(Normalizes("/a.b/c..d/.hidden/e.", "/a.b/c..d/.hidden/e."));
    CHECK(Normalizes("/Case/Matters", "/Case/Matters"));
}

TEST_CASE("Paths are normalized to Unicode NFC", "[asset][path]")
{
    // "e" + combining acute accent becomes the precomposed U+00E9.
    CHECK(Normalizes("/caf\x65\xcc\x81", "/caf\xc3\xa9"));
    CHECK(Normalizes("/caf\xc3\xa9", "/caf\xc3\xa9"));
}

TEST_CASE("Invalid paths are refused with a reason", "[asset][path]")
{
    CHECK(PathError("") == ErrorCode::Invalid);
    CHECK(PathError("/") == ErrorCode::Invalid);
    CHECK(PathError("relative/path") == ErrorCode::Invalid);
    CHECK(PathError("/a/") == ErrorCode::Invalid);
    CHECK(PathError("/a//b") == ErrorCode::Invalid);
    CHECK(PathError("/a/./b") == ErrorCode::Invalid);
    CHECK(PathError("/a/../b") == ErrorCode::Invalid);
    CHECK(PathError("/..") == ErrorCode::Invalid);
    CHECK(PathError("/a\\b") == ErrorCode::Invalid);
    CHECK(PathError("/a\tb") == ErrorCode::Invalid);
    CHECK(PathError("/a\x7f") == ErrorCode::Invalid);
    CHECK(PathError("/a\xff") == ErrorCode::Invalid);  // not UTF-8
}

TEST_CASE("Path length limits are enforced", "[asset][path]")
{
    String<HeapAllocator> segment(HeapAllocator::Default());
    for (usize i = 0; i < kMaxPathSegmentBytes; ++i)
    {
        segment.Append('a');
    }
    String<HeapAllocator> ok(HeapAllocator::Default(), "/");
    ok.Append(segment.View());
    CHECK(NormalizePath(ok.View(), nullptr).Ok());
    segment.Append('a');
    String<HeapAllocator> long_segment(HeapAllocator::Default(), "/");
    long_segment.Append(segment.View());
    CHECK(NormalizePath(long_segment.View(), nullptr).Is(ErrorCode::TooLarge));

    String<HeapAllocator> long_path(HeapAllocator::Default());
    while (long_path.Length() <= kMaxPathBytes)
    {
        long_path.Append("/aaaaaaaaaa");
    }
    CHECK(NormalizePath(long_path.View(), nullptr).Is(ErrorCode::TooLarge));
}

TEST_CASE("Resource types spell as four characters and register once", "[asset][type]")
{
    char spelled[5];
    types::kMesh.Spell(spelled);
    CHECK(CStrCompare(spelled, "MESH") == 0);
    CHECK(ResourceType().IsNone());
    CHECK(types::kMesh != types::kTexture);

    TypeRegistry registry;
    TypeInfo info;
    CHECK(registry.Find(types::kTexture, &info));
    CHECK(info.current_version == 1);
    CHECK(!registry.Find(ResourceType::FromChars('X', 'X', 'X', 'X'), nullptr));
    CHECK(registry.Register(types::kMesh, 2, "again").Is(ErrorCode::DuplicateId));
    CHECK(registry.Register(ResourceType(), 1, "none").Is(ErrorCode::Invalid));
    CHECK(registry.Register(ResourceType::FromChars('T', 'E', 'S', 'T'), 0, "v0").Is(ErrorCode::Invalid));
    CHECK(registry.Register(ResourceType::FromChars('T', 'E', 'S', 'T'), 3, "test").Ok());
    CHECK(registry.Find(ResourceType::FromChars('T', 'E', 'S', 'T'), &info));
    CHECK(info.current_version == 3);
}

}  // namespace vse::asset
