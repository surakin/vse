// The script value (Phase 27 Day 446): kinds, deep copies, bounded nesting, the limits check, equality and the hash.
#include <catch2/catch_test_macros.hpp>

#include "lang/limits.h"
#include "lang/scalar.h"
#include "script/value.h"

using namespace engine_placeholder;
using namespace engine_placeholder::script;

namespace
{

Value Nested(u32 levels)
{
    Value v = Value::FromInt(1);
    for (u32 i = 0; i < levels; ++i)
    {
        Value outer = Value::MakeArray();
        REQUIRE(outer.Push(Move(v)));
        v = Move(outer);
    }
    return v;
}

}  // namespace

TEST_CASE("Value: every kind holds what it was made with", "[script][value]")
{
    CHECK(Value().IsNil());
    CHECK(Value::FromBool(true).AsBool());
    CHECK(Value::FromInt(-7).AsInt() == -7);
    CHECK(Value::FromFloat(2.5).AsFloat() == 2.5);
    CHECK(Value::FromInt(3).ToDouble() == 3.0);
    CHECK(Value::FromFloat(0.5).ToDouble() == 0.5);
    CHECK(Value::FromText("hello").Text() == StringView("hello"));
    CHECK(Value::FromVec2(Vec2(1, 2)).AsVec2().y == 2.0f);
    CHECK(Value::FromVec3(Vec3(1, 2, 3)).AsVec3().z == 3.0f);
    CHECK(Value::FromVec4(Vec4(1, 2, 3, 4)).AsVec4().w == 4.0f);
    Quaternion q;
    q.x = 0.1f;
    q.w = 0.9f;
    CHECK(Value::FromQuat(q).AsQuat().w == 0.9f);
    ecs::Entity e;
    e.index = 5;
    e.generation = 2;
    CHECK(Value::FromEntity(e).AsEntity().index == 5);
    const u8 bytes[3] = {1, 2, 3};
    const Value blob = Value::FromBytes(Span<const u8>(bytes, 3));
    REQUIRE(blob.Bytes().Size() == 3);
    CHECK(blob.Bytes()[2] == 3);
    CHECK(Value::FromBool(false).Kind() == ValueKind::Bool);
    CHECK(Value::MakeMap().Kind() == ValueKind::Map);
}

TEST_CASE("Value: a copy is deep and independent, a move leaves nil", "[script][value]")
{
    Value map = Value::MakeMap();
    Value list = Value::MakeArray();
    REQUIRE(list.Push(Value::FromText("a")));
    REQUIRE(map.Set("list", Move(list)));
    REQUIRE(map.Set("n", Value::FromInt(4)));

    Value copy(map);
    CHECK(copy.Equals(map));
    REQUIRE(copy.Set("n", Value::FromInt(5)));
    CHECK(map.Find("n")->AsInt() == 4);  // the original is untouched
    CHECK_FALSE(copy.Equals(map));

    Value moved(Move(copy));
    CHECK(copy.IsNil());  // NOLINT: the moved-from state is part of the contract
    CHECK(moved.Find("n")->AsInt() == 5);
    CHECK(moved.Find("list")->At(0).Text() == StringView("a"));

    Value assigned;
    assigned = map;
    CHECK(assigned.Equals(map));
    assigned = Move(moved);
    CHECK(assigned.Find("n")->AsInt() == 5);
}

TEST_CASE("Value: maps keep insertion order and replace in place", "[script][value]")
{
    Value map = Value::MakeMap();
    REQUIRE(map.Set("b", Value::FromInt(1)));
    REQUIRE(map.Set("a", Value::FromInt(2)));
    REQUIRE(map.Set("b", Value::FromInt(3)));
    REQUIRE(map.Size() == 2);
    CHECK(map.KeyAt(0) == StringView("b"));
    CHECK(map.At(0).AsInt() == 3);
    CHECK(map.KeyAt(1) == StringView("a"));
    CHECK(map.Find("missing") == nullptr);
}

TEST_CASE("Value: nesting is bounded when a child is added", "[script][value]")
{
    Value ok = Nested(kMaxValueDepth - 1);
    CHECK(ok.Depth() == kMaxValueDepth);
    Value deeper = Value::MakeArray();
    Value item = Move(ok);
    CHECK_FALSE(deeper.Push(Move(item)));   // one level too many: refused...
    CHECK(item.Depth() == kMaxValueDepth);  // ...and the item is not consumed
    CHECK(deeper.Size() == 0);
    // A copy and a destruction of the deepest allowed value work (bounded recursion).
    Value copy(item);
    CHECK(copy.Equals(item));
}

TEST_CASE("Value: a container refuses an item past kMaxContainerItems", "[script][value]")
{
    Value list = Value::MakeArray();
    for (usize i = 0; i < kMaxContainerItems; ++i)
    {
        REQUIRE(list.Push(Value()));
    }
    CHECK_FALSE(list.Push(Value::FromInt(1)));
    CHECK(list.Size() == kMaxContainerItems);
    Value map = Value::MakeMap();
    for (usize i = 0; i < kMaxContainerItems; ++i)
    {
        const u32 key = static_cast<u32>(i);
        REQUIRE(map.Set(StringView(reinterpret_cast<const char*>(&key), 4), Value()));
    }
    CHECK_FALSE(map.Set("one more", Value()));
    CHECK(map.Set(StringView(reinterpret_cast<const char*>("\0\0\0\0"), 4),
                  Value::FromInt(9)));  // replacing is still allowed
}

TEST_CASE("Value::Check applies the limits of a boundary", "[script][value]")
{
    ValueLimits limits;
    CHECK(Value::FromInt(1).Check(limits) == ValueCheck::Ok);

    limits.max_text_bytes = 4;
    CHECK(Value::FromText("abcd").Check(limits) == ValueCheck::Ok);
    CHECK(Value::FromText("abcde").Check(limits) == ValueCheck::TextTooLong);
    Value map = Value::MakeMap();
    REQUIRE(map.Set("toolong", Value()));
    CHECK(map.Check(limits) == ValueCheck::TextTooLong);  // a key counts as text

    limits = ValueLimits();
    limits.max_blob_bytes = 2;
    const u8 bytes[3] = {1, 2, 3};
    CHECK(Value::FromBytes(Span<const u8>(bytes, 3)).Check(limits) == ValueCheck::BlobTooLarge);

    limits = ValueLimits();
    limits.max_depth = 3;
    CHECK(Nested(2).Check(limits) == ValueCheck::Ok);
    CHECK(Nested(3).Check(limits) == ValueCheck::TooDeep);

    limits = ValueLimits();
    limits.max_nodes = 5;
    Value list = Value::MakeArray();
    for (i64 i = 0; i < 5; ++i)
    {
        REQUIRE(list.Push(Value::FromInt(i)));
    }
    CHECK(list.Check(limits) == ValueCheck::TooManyNodes);  // the container itself is a node: 6

    limits = ValueLimits();
    limits.require_finite = true;
    CHECK(Value::FromFloat(1.0).Check(limits) == ValueCheck::Ok);
    CHECK(Value::FromFloat(Limits<f64>::Infinity()).Check(limits) == ValueCheck::NotFinite);
    CHECK(Value::FromVec3(Vec3(0, Limits<f32>::QuietNaN(), 0)).Check(limits) == ValueCheck::NotFinite);
    Value inner = Value::MakeArray();
    REQUIRE(inner.Push(Value::FromFloat(Limits<f64>::QuietNaN())));
    CHECK(inner.Check(limits) == ValueCheck::NotFinite);
    limits.require_finite = false;
    CHECK(inner.Check(limits) == ValueCheck::Ok);
}

TEST_CASE("Value::Equals compares floats by bit pattern and maps in order", "[script][value]")
{
    const f64 nan = Limits<f64>::QuietNaN();
    CHECK(Value::FromFloat(nan).Equals(Value::FromFloat(nan)));
    CHECK_FALSE(Value::FromFloat(0.0).Equals(Value::FromFloat(-0.0)));
    CHECK_FALSE(Value::FromInt(1).Equals(Value::FromFloat(1.0)));  // kinds differ
    Value a = Value::MakeMap();
    Value b = Value::MakeMap();
    REQUIRE(a.Set("x", Value::FromInt(1)));
    REQUIRE(a.Set("y", Value::FromInt(2)));
    REQUIRE(b.Set("y", Value::FromInt(2)));
    REQUIRE(b.Set("x", Value::FromInt(1)));
    CHECK_FALSE(
        a.Equals(b));  // the same entries in another order are another value (iteration order is part of the state)
}

TEST_CASE("Value::Hash is a stable function of the content", "[script][value]")
{
    CHECK(Value::FromInt(1).Hash() == Value::FromInt(1).Hash());
    CHECK(Value::FromInt(1).Hash() != Value::FromInt(2).Hash());
    CHECK(Value::FromInt(1).Hash() != Value::FromFloat(1.0).Hash());
    CHECK(Value::FromText("ab").Hash() != Value::FromText("ba").Hash());
    Value a = Value::MakeArray();
    Value b = Value::MakeArray();
    REQUIRE(a.Push(Value::FromText("x")));
    REQUIRE(b.Push(Value::FromText("x")));
    CHECK(a.Hash() == b.Hash());
    // Pinned, and computed independently (FNV-1a over the kind byte and the little-endian payload): the same on every
    // machine, so a recorded session can be compared across them.
    CHECK(Value().Hash() == 0x44bd2bd473ccf799ull);
    CHECK(Value::FromInt(1).Hash() == 0x9d335f801aafee72ull);
}
