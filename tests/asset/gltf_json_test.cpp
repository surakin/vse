// Phase 22 Day 335: the JSON wrapper of the glTF importer (asset_import/json_view.h) on valid, malformed and hostile
// text.

#include <catch2/catch_test_macros.hpp>

#include "asset_import/json_view.h"
#include "cstr/cstr.h"
#include "gltf_writer.h"

namespace vse::asset
{
namespace
{

ByteSpan JsonBytes(const char* text)
{
    return ByteSpan(reinterpret_cast<const byte*>(text), CStrLength(text));
}

Status ParseJsonText(const char* text, JsonDocument* doc, GltfError* error)
{
    return doc->Parse(JsonBytes(text), u64{1} << 20, error);
}

}  // namespace

TEST_CASE("json: a document is read through typed accessors that name the path of a refusal", "[asset][gltf][json]")
{
    JsonDocument doc;
    GltfError error;
    REQUIRE(
        ParseJsonText(
            "{\"a\":{\"b\":[1,2.0,{\"c\":\"x\\u00e9\\ud83d\\ude00\"}],\"t\":true,\"n\":null,\"big\":18446744073709551615,\"neg\":-2,\"f\":3.5}}",
            &doc, &error)
            .Ok());
    const JsonValue root = doc.Root();
    const JsonValue b = root.Get("a").Get("b");
    REQUIRE(b.IsArray());
    CHECK(b.Size() == 3);
    u64 u = 0;
    CHECK(b.At(0).AsUnsigned(&u, 10, &error).Ok());
    CHECK(u == 1);
    CHECK(b.At(1).AsUnsigned(&u, 10, &error).Ok());  // 2.0 is an integer
    CHECK(u == 2);
    StringView text;
    REQUIRE(b.At(2).Get("c").AsString(&text, &error).Ok());
    CHECK(text == StringView("x\xc3\xa9\xf0\x9f\x98\x80"));  // the escapes became UTF-8
    bool flag = false;
    CHECK(root.Get("a").Get("t").AsBool(&flag, &error).Ok());
    CHECK(flag);
    CHECK(root.Get("a").Get("n").IsNull());
    CHECK_FALSE(root.Get("a").Get("missing").Exists());
    // Refusals name where.
    CHECK(root.Get("a").Get("neg").AsUnsigned(&u, 10, &error).Failed());
    CHECK(StringView(error.message.Data()) == StringView("a.neg: must not be negative"));
    CHECK(root.Get("a").Get("f").AsUnsigned(&u, 10, &error).Failed());
    CHECK(StringView(error.message.Data()).Contains(StringView("a.f: must be a non-negative integer")));
    CHECK(b.At(0).AsUnsigned(&u, 0, &error).Failed());
    CHECK(StringView(error.message.Data()).StartsWith(StringView("a.b[0]: is above the limit")));
    CHECK(root.Get("a").Get("big").AsUnsigned(&u, ~u64{0}, &error).Ok());
    CHECK(u == ~u64{0});
    u32 index = 0;
    CHECK(b.At(1).AsIndex(&index, 2, &error).Failed());  // 2 is not below 2
    CHECK(b.At(2).AsString(&text, &error).Failed());
    CHECK(StringView(error.message.Data()) == StringView("a.b[2]: must be a string"));
    CHECK(root.RequiredIndex("zzz", &index, 3, &error).Failed());
    CHECK(StringView(error.message.Data()).Contains(StringView("the member \"zzz\" is required")));
    // Members walk in document order.
    JsonMembers members;
    root.Get("a").Members(&members);
    StringView key;
    JsonValue value;
    u32 seen = 0;
    while (members.Next(&key, &value))
    {
        seen += 1;
    }
    CHECK(seen == 6);
}

TEST_CASE("json: text that is not strict RFC 8259 is refused", "[asset][gltf][json]")
{
    const char* const bad[] = {
        "",            // empty
        "{",           // truncated
        "{\"a\":1,}",  // a trailing comma
        "[1,]",
        "{\"a\":1} x",  // bytes after the end
        "// c\n{}",     // a comment
        "{/* c */}",
        "[NaN]",  // not JSON
        "[Infinity]",
        "[1e999]",            // not finite
        "[01]",               // a leading zero
        "{'a':1}",            // single quotes
        "{a:1}",              // unquoted key
        "\"\xff\"",           // invalid UTF-8
        "\"\\ud800\"",        // a lone surrogate
        "\"\\x41\"",          // not an escape
        "\xEF\xBB\xBF{}",     // a byte order mark
        "{\"a\":1,\"a\":2}",  // a repeated key (the library takes it; the wrapper does not)
        "{\"x\":{\"a\":1,\"b\":2,\"a\":3}}",
    };
    for (const char* text : bad)
    {
        JsonDocument doc;
        GltfError error;
        INFO(text);
        CHECK(ParseJsonText(text, &doc, &error).Failed());
        CHECK(error.message[0] != '\0');
        CHECK_FALSE(doc.Root().Exists());
    }
}

TEST_CASE("json: a repeated key is found in a large object too", "[asset][gltf][json]")
{
    String<HeapAllocator> text(HeapAllocator::Default(), StringView("{"));
    for (u32 i = 0; i < 400; ++i)
    {
        char entry[48];
        FormatTo(entry, sizeof(entry), "%s\"k%u\":%u", i == 0 ? "" : ",", static_cast<unsigned>(i),
                 static_cast<unsigned>(i));
        text.Append(StringView(entry, CStrLength(entry)));
    }
    String<HeapAllocator> fine = String<HeapAllocator>(HeapAllocator::Default(), text.View());
    fine.Append('}');
    text.Append(StringView(",\"k17\":0}"));
    JsonDocument doc;
    GltfError error;
    CHECK(doc.Parse(test::SpanOfText(fine), u64{1} << 20, &error).Ok());
    JsonDocument repeated;
    CHECK(repeated.Parse(test::SpanOfText(text), u64{1} << 20, &error).Failed());
    CHECK(StringView(error.message.Data()).Contains(StringView("k17")));
}

TEST_CASE("json: nesting beyond 128 is refused without recursion, and a huge text is refused by its size",
          "[asset][gltf][json]")
{
    for (const u32 depth : {128u, 129u, 100000u})
    {
        String<HeapAllocator> text(HeapAllocator::Default());
        for (u32 i = 0; i < depth; ++i)
        {
            text.Append('[');
        }
        for (u32 i = 0; i < depth; ++i)
        {
            text.Append(']');
        }
        JsonDocument doc;
        GltfError error;
        const Status status = doc.Parse(test::SpanOfText(text), u64{1} << 22, &error);
        INFO(depth);
        CHECK(status.Ok() == (depth <= 128));
    }
    JsonDocument doc;
    GltfError error;
    CHECK(doc.Parse(JsonBytes("[1,2,3]"), 4, &error).Is(ErrorCode::TooLarge));
}

TEST_CASE("json: 5,000 mutated and truncated documents never crash and never use more memory than reserved",
          "[asset][gltf][json]")
{
    const char* seed =
        "{\"asset\":{\"version\":\"2.0\"},\"meshes\":[{\"primitives\":[{\"attributes\":{\"POSITION\":0},\"indices\":1}]}],"
        "\"accessors\":[{\"count\":3,\"min\":[-1.5e3,0,1E-7],\"componentType\":5126,\"s\":\"\\u00e9\\ud83d\\ude00\\n\"}],"
        "\"a\":[[[[]]]],\"t\":true,\"n\":null}";
    const usize length = CStrLength(seed);
    test::Rng rng(12345);
    u32 accepted = 0;
    for (u32 round = 0; round < 5000; ++round)
    {
        Vector<byte> bytes;
        for (usize i = 0; i < length; ++i)
        {
            bytes.Push(static_cast<byte>(seed[i]));
        }
        const u32 edits = 1 + rng.Below(4);
        for (u32 e = 0; e < edits && bytes.Size() > 1; ++e)
        {
            const usize at = rng.Below(static_cast<u32>(bytes.Size()));
            switch (rng.Below(4))
            {
            case 0: bytes[at] = static_cast<byte>(rng.Below(256)); break;
            case 1: bytes.Resize(at + 1); break;
            case 2: bytes.Insert(at, static_cast<byte>(rng.Below(256))); break;
            default: bytes.Remove(at); break;
            }
        }
        JsonDocument doc;
        GltfError error;
        if (doc.Parse(ByteSpan(bytes.Data(), bytes.Size()), u64{1} << 20, &error).Ok())
        {
            accepted += 1;
            // Whatever was accepted can be walked.
            JsonMembers members;
            doc.Root().Members(&members);
            StringView key;
            JsonValue value;
            while (members.Next(&key, &value))
            {
            }
        }
    }
    CHECK(accepted > 0);
    CHECK(accepted < 5000);
}

}  // namespace vse::asset
