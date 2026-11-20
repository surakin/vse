// Resource references in components and the record walker (Phase 22 Day 327): the `ResourceRef` field kind, the
// `FieldOptions::Reference` option on a `u64`, `SkipField` and `ReadReferences`.

#include <catch2/catch_test_macros.hpp>

#include "storage/component_registry.h"
#include "storage/field_walk.h"

namespace vse::storage
{
namespace
{

constexpr u32 kFwMeshTag = ResourceTag('M', 'E', 'S', 'H');

enum class FwMode : u8
{
    A,
    B,
    C
};

struct FwProp
{
    ResourceRef mesh;  // required
    u64 material = 0;  // by option, optional
    String<HeapAllocator> label = String<HeapAllocator>(HeapAllocator::Default());
    Array<Vec3, 2> offsets = {};
    Array<FwMode, 3> modes = {};
    EntityRef owner;
    u64 plain = 0;      // an ordinary u64, not a reference
    ResourceRef extra;  // after the variable-length fields
};

struct FwBad
{
    f32 value = 0;
};

void FillRegistry(ComponentRegistry& registry)
{
    REQUIRE(
        registry
            .Register<FwProp>(
                {.id = 2001,
                 .name = "prop",
                 .fields = {Field<&FwProp::mesh>("mesh", FieldOptions::Reference(kFwMeshTag, false)),
                            Field<&FwProp::material>("material", FieldOptions::Reference(0, true)),
                            Field<&FwProp::label>("label", FieldOptions::Text(16)), Field<&FwProp::offsets>("offsets"),
                            Field<&FwProp::modes>("modes", FieldOptions::Enum(0, 2)), Field<&FwProp::owner>("owner"),
                            Field<&FwProp::plain>("plain"), Field<&FwProp::extra>("extra")}})
            .Ok());
}

Vector<byte> FwEncoded(const ComponentRegistry& registry, const FwProp& prop)
{
    bytes::ByteWriter out;
    REQUIRE(registry.Find<FwProp>()->Encode(&prop, out, CodecLimits()).Ok());
    Vector<byte> bytes;
    for (usize i = 0; i < out.Size(); ++i)
    {
        bytes.Push(out.Bytes()[i]);
    }
    return bytes;
}

}  // namespace

TEST_CASE("A resource reference is eight bytes like a u64, and the kind says it is a reference", "[storage][reference]")
{
    ComponentRegistry registry;
    FillRegistry(registry);
    const RegisteredComponent* prop = registry.Find<FwProp>();
    REQUIRE(prop != nullptr);
    const Span<const FieldDesc> fields = prop->Fields();
    CHECK(fields[0].kind == FieldKind::ResourceRef);
    CHECK(fields[0].options.is_reference);
    CHECK(fields[0].options.ref_type == kFwMeshTag);
    CHECK_FALSE(fields[0].options.nullable);
    CHECK(fields[1].kind == FieldKind::ResourceRef);  // a u64 member, declared a reference
    CHECK(fields[1].options.nullable);
    CHECK(fields[6].kind == FieldKind::U64);  // an ordinary u64 stays one
    CHECK_FALSE(fields[6].options.is_reference);
    CHECK(fields[7].kind == FieldKind::ResourceRef);  // a ResourceRef member needs no option
    CHECK(fields[7].options.nullable);

    FwProp prop_value;
    prop_value.mesh.id = 0x0102030405060708ull;
    const Vector<byte> bytes = FwEncoded(registry, prop_value);
    REQUIRE(bytes.Size() > 8);
    const byte expected[8] = {0x08, 0x07, 0x06, 0x05, 0x04, 0x03, 0x02, 0x01};
    for (usize i = 0; i < 8; ++i)
    {
        CHECK(bytes[i] == expected[i]);
    }

    FwProp decoded;
    REQUIRE(prop->DecodeRecord(1, ByteSpan(bytes.Data(), bytes.Size()), &decoded, CodecLimits()).Ok());
    CHECK(decoded.mesh.id == 0x0102030405060708ull);
}

TEST_CASE("The reference option applies only to a u64 or a ResourceRef", "[storage][reference]")
{
    ComponentRegistry registry;
    const Status s = registry.Register<FwBad>(
        {.id = 2002, .name = "bad", .fields = {Field<&FwBad::value>("value", FieldOptions::Reference(kFwMeshTag))}});
    CHECK(s.Is(ErrorCode::Invalid));
}

TEST_CASE("An array of resource references is refused at registration", "[storage][reference]")
{
    struct FwList
    {
        Array<ResourceRef, 2> meshes = {};
    };
    ComponentRegistry registry;
    CHECK(registry.Register<FwList>({.id = 2003, .name = "list", .fields = {Field<&FwList::meshes>("meshes")}})
              .Is(ErrorCode::Invalid));
}

TEST_CASE("ReadReferences lists the references of a record and walks every other kind", "[storage][reference]")
{
    ComponentRegistry registry;
    FillRegistry(registry);
    const RegisteredComponent& prop = *registry.Find<FwProp>();
    FwProp value;
    value.mesh.id = 7;
    value.material = 0;  // null
    value.label = String<HeapAllocator>(HeapAllocator::Default(), StringView("a label"));
    value.modes[1] = FwMode::C;
    value.plain = 99;
    value.extra.id = 11;
    const Vector<byte> bytes = FwEncoded(registry, value);

    Vector<ReferenceValue> refs;
    REQUIRE(ReadReferences(prop, ByteSpan(bytes.Data(), bytes.Size()), CodecLimits(), &refs).Ok());
    REQUIRE(refs.Size() == 3);
    CHECK(refs[0].field_index == 0);
    CHECK(refs[0].id == 7);
    CHECK(refs[0].field->options.ref_type == kFwMeshTag);
    CHECK(refs[1].field_index == 1);
    CHECK(refs[1].id == 0);
    CHECK(refs[2].field_index == 7);  // found after a string, two arrays, an entity reference and a u64
    CHECK(refs[2].id == 11);
}

TEST_CASE("ReadReferences refuses a record that does not fit its fields", "[storage][reference]")
{
    ComponentRegistry registry;
    FillRegistry(registry);
    const RegisteredComponent& prop = *registry.Find<FwProp>();
    FwProp value;
    value.label = String<HeapAllocator>(HeapAllocator::Default(), StringView("abc"));
    const Vector<byte> bytes = FwEncoded(registry, value);
    Vector<ReferenceValue> refs;

    // Every truncation fails.
    for (usize cut = 0; cut < bytes.Size(); ++cut)
    {
        refs.Clear();
        CHECK(ReadReferences(prop, ByteSpan(bytes.Data(), cut), CodecLimits(), &refs).Failed());
    }
    // A byte too many fails.
    Vector<byte> longer;
    for (usize i = 0; i < bytes.Size(); ++i)
    {
        longer.Push(bytes[i]);
    }
    longer.Push(0);
    refs.Clear();
    CHECK(ReadReferences(prop, ByteSpan(longer.Data(), longer.Size()), CodecLimits(), &refs).Is(ErrorCode::Corrupt));

    // A string longer than the limit, and an array whose count does not match its type.
    CodecLimits tight;
    tight.max_string_bytes = 2;
    refs.Clear();
    CHECK(ReadReferences(prop, ByteSpan(bytes.Data(), bytes.Size()), tight, &refs).Is(ErrorCode::LimitExceeded));
    Vector<byte> wrong_count;
    for (usize i = 0; i < bytes.Size(); ++i)
    {
        wrong_count.Push(bytes[i]);
    }
    wrong_count[16 + 1 + 3] = 5;  // two u64 refs, a 1-byte length and "abc", then the count of `offsets`
    refs.Clear();
    CHECK(ReadReferences(prop, ByteSpan(wrong_count.Data(), wrong_count.Size()), CodecLimits(), &refs)
              .Is(ErrorCode::Corrupt));
}

TEST_CASE("SkipField moves past each kind by exactly its stored size", "[storage][reference]")
{
    ComponentRegistry registry;
    FillRegistry(registry);
    const RegisteredComponent& prop = *registry.Find<FwProp>();
    FwProp value;
    value.label = String<HeapAllocator>(HeapAllocator::Default(), StringView("xyz"));
    const Vector<byte> bytes = FwEncoded(registry, value);
    bytes::ByteReader in(ByteSpan(bytes.Data(), bytes.Size()));
    const Span<const FieldDesc> fields = prop.Fields();
    usize before = 0;
    const usize sizes[8] = {8, 8, 1 + 3, 1 + 24, 1 + 3, 16, 8, 8};
    for (usize i = 0; i < fields.Size(); ++i)
    {
        REQUIRE(SkipField(fields[i], in, CodecLimits()).Ok());
        CHECK(in.Position() - before == sizes[i]);
        before = in.Position();
    }
    CHECK(in.Remaining() == 0);
}

}  // namespace vse::storage
