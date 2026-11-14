// Phase 22 Day 335: the glTF container, the accessors, the manifest, the id sources and ImportIntoBackend
// (asset_import/gltf_container.h, gltf_accessor.h, gltf_import.h).

#include <catch2/catch_test_macros.hpp>

#include "asset/memory_backend.h"
#include "asset_import/gltf_accessor.h"
#include "asset_import/gltf_container.h"
#include "cstr/cstr.h"
#include "gltf_writer.h"

namespace engine_placeholder::asset
{
namespace
{

using test::GltfWriter;
using test::SpanOfText;
using test::TestSource;

template <typename T> u32 AddArray(GltfWriter* w, const T* values, usize count, u32 stride = 0)
{
    return w->AddView(values, count * sizeof(T), stride);
}

Vector<byte> TriangleGlb()
{
    GltfWriter w;
    const f32 positions[9] = {0, 0, 0, 1, 0, 0, 0, 1, 0};
    const u16 indices[3] = {0, 1, 2};
    const u32 pv = AddArray(&w, positions, 9);
    const u32 iv = AddArray(&w, indices, 3);
    w.AddAccessor(pv, 5126, 3, "VEC3");
    w.AddAccessor(iv, 5123, 3, "SCALAR");
    w.AddRoot("\"meshes\":[{\"name\":\"tri\",\"primitives\":[{\"attributes\":{\"POSITION\":0},\"indices\":1}]}]");
    return w.Glb();
}

Status Parse(const Vector<byte>& file, GltfSource& source, GltfDocument* doc, GltfError* error,
             const ImportLimits& limits = ImportLimits())
{
    return ParseGltf(ByteSpan(file.Data(), file.Size()), source, limits, doc, error);
}

Status ParseText(const String<HeapAllocator>& json, GltfSource& source, GltfDocument* doc, GltfError* error,
                 const ImportLimits& limits = ImportLimits())
{
    const Status status = ParseGltf(SpanOfText(json), source, limits, doc, error);
    if (status.Failed())
    {
        UNSCOPED_INFO("ParseGltf: " << GltfMessage(*error, status) << " | " << json.CStr());
    }
    return status;
}

bool Says(const GltfError& error, const char* text)
{
    return StringView(error.message.Data()).Contains(StringView(text));
}

}  // namespace

TEST_CASE("gltf: a .glb with a BIN chunk reads, and its accessors give the values", "[asset][gltf]")
{
    NoGltfSource none;
    GltfDocument doc;
    GltfError error;
    const Vector<byte> glb = TriangleGlb();
    REQUIRE(Parse(glb, none, &doc, &error).Ok());
    CHECK(doc.is_glb);
    REQUIRE(doc.accessors.Size() == 2);
    CHECK(doc.accessors[0].count == 3);
    CHECK(doc.accessors[0].components == 3);
    Vector<f32> positions;
    REQUIRE(ReadAccessorFloats(doc, 0, &positions, &error).Ok());
    REQUIRE(positions.Size() == 9);
    CHECK(positions[3] == 1.0f);
    CHECK(positions[7] == 1.0f);
    Vector<u32> indices;
    REQUIRE(ReadAccessorUnsigned(doc, 1, &indices, &error).Ok());
    REQUIRE(indices.Size() == 3);
    CHECK(indices[2] == 2);
    CHECK(ReadAccessorFloats(doc, 9, &positions, &error).Failed());  // no such accessor
    CHECK(ReadAccessorUnsigned(doc, 0, &indices, &error).Failed());  // floats are not unsigned
}

TEST_CASE("gltf: a .gltf reads its buffer from an external file, from a data: URI, and refuses what is missing",
          "[asset][gltf]")
{
    GltfWriter w;
    const f32 values[3] = {1.5f, -2.0f, 3.25f};
    const u32 view = AddArray(&w, values, 3);
    w.AddAccessor(view, 5126, 3, "SCALAR");
    const String<HeapAllocator> json = w.Json("model%20data.bin");
    TestSource source;
    source.Add("model data.bin", w.bin);
    GltfDocument doc;
    GltfError error;
    REQUIRE(ParseText(json, source, &doc, &error).Ok());
    CHECK(source.reads == 1);
    CHECK(source.last.View() == StringView("model data.bin"));  // the percent-escape is decoded
    Vector<f32> read;
    REQUIRE(ReadAccessorFloats(doc, 0, &read, &error).Ok());
    CHECK(read[1] == -2.0f);
    // The same bytes inside the file, base64.
    const String<HeapAllocator> encoded = w.DataUri();
    NoGltfSource none;
    GltfDocument inline_doc;
    REQUIRE(ParseText(w.Json(encoded.CStr()), none, &inline_doc, &error).Ok());
    Vector<f32> again;
    REQUIRE(ReadAccessorFloats(inline_doc, 0, &again, &error).Ok());
    CHECK(again[2] == 3.25f);
    // A file the source lacks, and a file shorter than its byteLength.
    GltfDocument missing;
    CHECK(ParseText(w.Json("other.bin"), source, &missing, &error).Is(ErrorCode::NotFound));
    CHECK(Says(error, "buffers[0]"));
    TestSource short_source;
    Vector<byte> cut;
    for (usize i = 0; i + 1 < w.bin.Size(); ++i)
    {
        cut.Push(w.bin[i]);
    }
    short_source.Add("model.bin", cut);
    CHECK(ParseText(w.Json("model.bin"), short_source, &missing, &error).Failed());
    CHECK(Says(error, "shorter than its byteLength"));
}

TEST_CASE("gltf: what a buffer reads is bounded by its byteLength, before anything is allocated",
          "[asset][gltf][hostile]")
{
    GltfError error;
    GltfDocument doc;
    // A file of a thousand bytes for a buffer of twelve: the source is told to stop at twelve.
    GltfWriter w;
    const f32 values[3] = {1, 2, 3};
    AddArray(&w, values, 3);
    TestSource source;
    Vector<byte> big;
    for (u32 i = 0; i < 1000; ++i)
    {
        big.Push(static_cast<byte>(i));
    }
    source.Add("big.bin", big);
    REQUIRE(ParseText(w.Json("big.bin"), source, &doc, &error).Ok());
    CHECK(source.last_max == 12);
    REQUIRE(doc.buffers.Size() == 1);
    CHECK(doc.buffers[0].Size() == 12);
    // A data: URI that decodes to far more than its byteLength is refused by length, not decoded.
    String<HeapAllocator> bomb(HeapAllocator::Default(), StringView("data:application/octet-stream;base64,"));
    for (u32 i = 0; i < 100; ++i)
    {
        bomb.Append(StringView("AAAAAAAA"));
    }
    NoGltfSource none;
    CHECK(ParseText(w.Json(bomb.CStr()), none, &doc, &error).Is(ErrorCode::TooLarge));
    CHECK(Says(error, "buffers[0]"));
    // A byteLength of zero is invalid (not "too large").
    const String<HeapAllocator> zero(
        HeapAllocator::Default(),
        StringView(
            "{\"asset\":{\"version\":\"2.0\"},\"buffers\":[{\"byteLength\":0,\"uri\":\"data:application/octet-stream;base64,\"}]}"));
    CHECK(ParseText(zero, none, &doc, &error).Is(ErrorCode::Invalid));
    CHECK(Says(error, "byteLength of zero"));
}

TEST_CASE("gltf: a URI that leaves the glTF's folder is refused before the source is asked", "[asset][gltf][hostile]")
{
    GltfWriter w;
    const f32 values[1] = {1.0f};
    AddArray(&w, values, 1);
    const char* const hostile[] = {"../secret.bin",
                                   "a/../../secret.bin",
                                   "/etc/passwd",
                                   "file:///etc/passwd",
                                   "http://x/y.bin",
                                   "C:\\\\x.bin",
                                   "a//b.bin",
                                   "%2e%2e/secret.bin",
                                   "a%5Cb.bin",
                                   "a%00b.bin",
                                   "%zz.bin",
                                   "x%2",
                                   "",
                                   "data.bin:stream",
                                   "./"};
    for (const char* uri : hostile)
    {
        TestSource source;
        GltfDocument doc;
        GltfError error;
        INFO(uri);
        CHECK(ParseText(w.Json(uri), source, &doc, &error).Failed());
        CHECK(source.reads == 0);
        CHECK(Says(error, "buffers[0]"));
    }
    String<HeapAllocator> name(HeapAllocator::Default());
    GltfError error;
    REQUIRE(CleanRelativeUri(StringView("./a/./b%20c.bin"), &name, &error).Ok());
    CHECK(name.View() == StringView("a/b c.bin"));
}

TEST_CASE("gltf: a damaged .glb is refused naming what is wrong", "[asset][gltf][hostile]")
{
    NoGltfSource none;
    const Vector<byte> good = TriangleGlb();
    struct Case
    {
        const char* name;
        usize at;   // the byte to change
        u32 value;  // its new value (a 32-bit word at `at`)
        const char* says;
    };
    const Case cases[] = {
        {"version 1", 4, 1, "container version 2"},
        {"declared length", 8, 12345, "length in the .glb header"},
        {"json chunk past the end", 12, 0x7fffff00u, "runs past the end"},
        {"json chunk not a multiple of four", 12, 5, "multiple of four"},
        {"first chunk is not json", 16, 0x004E4942u, "not the JSON chunk"},
    };
    for (const Case& c : cases)
    {
        Vector<byte> file;
        for (usize i = 0; i < good.Size(); ++i)
        {
            file.Push(good[i]);
        }
        for (u32 i = 0; i < 4; ++i)
        {
            file[c.at + i] = static_cast<byte>(c.value >> (8 * i));
        }
        GltfDocument doc;
        GltfError error;
        INFO(c.name);
        CHECK(Parse(file, none, &doc, &error).Failed());
        CHECK(Says(error, c.says));
    }
    // Every truncation fails (and none crashes).
    for (usize cut = 0; cut + 1 < good.Size(); cut += 7)
    {
        Vector<byte> file;
        for (usize i = 0; i < cut; ++i)
        {
            file.Push(good[i]);
        }
        GltfDocument doc;
        GltfError error;
        CHECK(Parse(file, none, &doc, &error).Failed());
    }
    // A buffer that wants a BIN chunk the file does not have.
    GltfWriter w;
    const f32 v[1] = {1.0f};
    AddArray(&w, v, 1);
    const String<HeapAllocator> json = w.Json();
    Vector<byte> glb;
    Vector<byte> chunk;
    test::AppendText(&chunk, json.View());
    while (chunk.Size() % 4 != 0)
    {
        chunk.Push(' ');
    }
    test::Put32(&glb, 0x46546C67);
    test::Put32(&glb, 2);
    test::Put32(&glb, static_cast<u32>(12 + 8 + chunk.Size()));
    test::Put32(&glb, static_cast<u32>(chunk.Size()));
    test::Put32(&glb, 0x4E4F534A);
    for (usize i = 0; i < chunk.Size(); ++i)
    {
        glb.Push(chunk[i]);
    }
    GltfDocument doc;
    GltfError error;
    CHECK(Parse(glb, none, &doc, &error).Failed());
    CHECK(Says(error, "no uri"));
}

TEST_CASE("gltf: the asset, the extensions and the limits are checked", "[asset][gltf]")
{
    GltfWriter w;
    const f32 v[3] = {1, 2, 3};
    AddArray(&w, v, 3);
    NoGltfSource none;
    GltfDocument doc;
    GltfError error;
    CHECK(ParseText(w.Json(nullptr, false), none, &doc, &error).Failed());
    CHECK(Says(error, "asset"));
    CHECK(ParseText(w.Json(nullptr, true, "1.0"), none, &doc, &error).Failed());
    CHECK(Says(error, "is not 2.0"));
    GltfWriter ext;
    AddArray(&ext, v, 3);
    ext.AddRoot("\"extensionsRequired\":[\"KHR_draco_mesh_compression\"]");
    CHECK(ParseText(ext.JsonInline(), none, &doc, &error).Is(ErrorCode::Unsupported));
    CHECK(Says(error, "KHR_draco_mesh_compression"));
    GltfWriter supported;
    AddArray(&supported, v, 3);
    supported.AddRoot("\"extensionsRequired\":[\"KHR_texture_transform\"]");
    CHECK(ParseText(supported.JsonInline(), none, &doc, &error).Ok());
    // Limits.
    ImportLimits tight;
    tight.max_accessors = 1;
    GltfWriter many;
    AddArray(&many, v, 3);
    many.AddAccessor(0, 5126, 1, "SCALAR");
    many.AddAccessor(0, 5126, 1, "SCALAR");
    CHECK(ParseText(many.JsonInline(), none, &doc, &error, tight).Is(ErrorCode::TooLarge));
    CHECK(Says(error, "accessors"));
    ImportLimits small_file;
    small_file.max_file_bytes = 10;
    const String<HeapAllocator> json = w.JsonInline();
    CHECK(ParseText(json, none, &doc, &error, small_file).Is(ErrorCode::TooLarge));
    ImportLimits small_buffers;
    small_buffers.max_buffer_bytes = 4;
    CHECK(ParseText(json, none, &doc, &error, small_buffers).Failed());
}

TEST_CASE("gltf: views and accessors that leave their buffers are refused", "[asset][gltf][hostile]")
{
    NoGltfSource none;
    GltfDocument doc;
    GltfError error;
    const f32 v[6] = {1, 2, 3, 4, 5, 6};
    {
        GltfWriter w;
        const u32 view = AddArray(&w, v, 6);
        w.AddAccessor(view, 5126, 7, "SCALAR");  // 7 floats in 6
        CHECK(ParseText(w.JsonInline(), none, &doc, &error).Failed());
        CHECK(Says(error, "accessors[0]"));
        CHECK(Says(error, "buffer view"));
    }
    {
        GltfWriter w;
        const u32 view = AddArray(&w, v, 6);
        w.AddAccessor(view, 5126, 0xffffffffull, "VEC4");  // a count that overflows every product
        CHECK(ParseText(w.JsonInline(), none, &doc, &error).Failed());
    }
    {
        GltfWriter w;
        AddArray(&w, v, 6);
        w.AddAccessor(0, 5126, 1, "SCALAR", "", 24);  // an offset exactly at the end
        CHECK(ParseText(w.JsonInline(), none, &doc, &error).Failed());
    }
    {
        GltfWriter w;
        AddArray(&w, v, 6);
        w.AddRoot("\"bufferViews\":[]");  // a second bufferViews member: a repeated key
        CHECK(ParseText(w.JsonInline(), none, &doc, &error).Failed());
    }
    {
        // A view that runs past its four-byte buffer.
        const String<HeapAllocator> json(
            HeapAllocator::Default(),
            StringView("{\"asset\":{\"version\":\"2.0\"},\"buffers\":[{\"byteLength\":4,\"uri\":"
                       "\"data:application/octet-stream;base64,AAAAAA==\"}],"
                       "\"bufferViews\":[{\"buffer\":0,\"byteLength\":24}],\"accessors\":[]}"));
        CHECK(ParseText(json, none, &doc, &error).Failed());
        CHECK(Says(error, "bufferViews[0]"));
        CHECK(Says(error, "inside its buffer"));
    }
    {
        GltfWriter w;
        const u32 view = AddArray(&w, v, 6);
        w.AddAccessor(view, 5126, 1, "SCALAR", ",\"normalized\":true");  // a float cannot be normalized
        CHECK(ParseText(w.JsonInline(), none, &doc, &error).Failed());
        CHECK(Says(error, "normalized"));
    }
    {
        GltfWriter w;
        const u32 view = AddArray(&w, v, 6);
        w.AddAccessor(view, 5124, 1, "SCALAR");  // INT is not a glTF component type
        CHECK(ParseText(w.JsonInline(), none, &doc, &error).Failed());
        w.AddAccessor(view, 5126, 1, "VEC5");
        CHECK(ParseText(w.JsonInline(), none, &doc, &error).Failed());
    }
}

TEST_CASE("gltf: accessors read every component type, normalized values, strides, matrices and sparse overrides",
          "[asset][gltf][accessor]")
{
    NoGltfSource none;
    GltfError error;
    GltfWriter w;
    const i8 bytes[4] = {-128, -1, 0, 127};
    const u8 ubytes[3] = {0, 128, 255};
    const i16 shorts[2] = {-32768, 32767};
    const u16 ushorts[2] = {0, 65535};
    const u32 uints[2] = {0, 4000000000u};
    const f32 floats[2] = {0.5f, -0.25f};
    w.AddAccessor(AddArray(&w, bytes, 4), 5120, 4, "SCALAR");
    w.AddAccessor(AddArray(&w, ubytes, 3), 5121, 3, "SCALAR");
    w.AddAccessor(AddArray(&w, shorts, 2), 5122, 2, "SCALAR");
    w.AddAccessor(AddArray(&w, ushorts, 2), 5123, 2, "SCALAR");
    w.AddAccessor(AddArray(&w, uints, 2), 5125, 2, "SCALAR");
    w.AddAccessor(AddArray(&w, floats, 2), 5126, 2, "SCALAR");
    w.AddAccessor(1, 5121, 3, "SCALAR", ",\"normalized\":true");
    w.AddAccessor(0, 5120, 4, "SCALAR", ",\"normalized\":true");
    w.AddAccessor(3, 5123, 2, "SCALAR", ",\"normalized\":true");
    w.AddAccessor(2, 5122, 2, "SCALAR", ",\"normalized\":true");
    GltfDocument doc;
    REQUIRE(ParseText(w.JsonInline(), none, &doc, &error).Ok());
    Vector<f32> f;
    REQUIRE(ReadAccessorFloats(doc, 0, &f, &error).Ok());
    CHECK(f[0] == -128.0f);
    CHECK(f[3] == 127.0f);
    REQUIRE(ReadAccessorFloats(doc, 4, &f, &error).Ok());
    CHECK(f[1] == 4000000000.0f);
    REQUIRE(ReadAccessorFloats(doc, 6, &f, &error).Ok());
    CHECK(f[0] == 0.0f);
    CHECK(f[2] == 1.0f);
    CHECK(f[1] > 0.50f);
    CHECK(f[1] < 0.51f);
    REQUIRE(ReadAccessorFloats(doc, 7, &f, &error).Ok());
    CHECK(f[0] == -1.0f);  // -128 clamps to -1
    CHECK(f[3] == 1.0f);
    REQUIRE(ReadAccessorFloats(doc, 8, &f, &error).Ok());
    CHECK(f[1] == 1.0f);
    REQUIRE(ReadAccessorFloats(doc, 9, &f, &error).Ok());
    CHECK(f[0] == -1.0f);
    CHECK(f[1] == 1.0f);
    Vector<u32> u;
    REQUIRE(ReadAccessorUnsigned(doc, 4, &u, &error).Ok());
    CHECK(u[1] == 4000000000u);
    CHECK(ReadAccessorUnsigned(doc, 0, &u, &error).Failed());  // signed
    CHECK(ReadAccessorUnsigned(doc, 6, &u, &error).Failed());  // normalized
}

TEST_CASE("gltf: a byteStride spaces the elements, a MAT3 of bytes pads its columns, an accessor with no view is zeros",
          "[asset][gltf][accessor]")
{
    NoGltfSource none;
    GltfError error;
    GltfWriter w;
    // Positions interleaved with a four-byte attribute: stride 16.
    f32 interleaved[8] = {1, 2, 3, 0, 4, 5, 6, 0};
    w.AddAccessor(w.AddView(interleaved, sizeof(interleaved), 16), 5126, 2, "VEC3");
    // A MAT3 of signed bytes: each of the three columns is three bytes padded to four.
    const i8 matrix[12] = {1, 2, 3, 0, 4, 5, 6, 0, 7, 8, 9, 0};
    w.AddAccessor(w.AddView(matrix, sizeof(matrix)), 5120, 1, "MAT3");
    w.AddAccessor(-1, 5126, 5, "VEC2");  // no bufferView: zeros
    GltfDocument doc;
    REQUIRE(ParseText(w.JsonInline(), none, &doc, &error).Ok());
    Vector<f32> f;
    REQUIRE(ReadAccessorFloats(doc, 0, &f, &error).Ok());
    REQUIRE(f.Size() == 6);
    CHECK(f[0] == 1.0f);
    CHECK(f[3] == 4.0f);
    CHECK(f[5] == 6.0f);
    REQUIRE(ReadAccessorFloats(doc, 1, &f, &error).Ok());
    REQUIRE(f.Size() == 9);
    CHECK(f[2] == 3.0f);
    CHECK(f[3] == 4.0f);  // the second column starts after the padding byte
    CHECK(f[8] == 9.0f);
    REQUIRE(ReadAccessorFloats(doc, 2, &f, &error).Ok());
    REQUIRE(f.Size() == 10);
    CHECK(f[9] == 0.0f);
    // An element wider than its view's stride is refused.
    GltfWriter narrow;
    narrow.AddAccessor(narrow.AddView(interleaved, sizeof(interleaved), 8), 5126, 2, "VEC3");
    CHECK(ParseText(narrow.JsonInline(), none, &doc, &error).Failed());
    CHECK(Says(error, "wider than the byteStride"));
}

TEST_CASE("gltf: sparse accessors override elements of the base, and refuse bad indices", "[asset][gltf][accessor]")
{
    NoGltfSource none;
    GltfError error;
    auto build = [](const u16* sparse_indices, usize sparse_count, String<HeapAllocator>* json)
    {
        GltfWriter w;
        const f32 base[4] = {1, 2, 3, 4};
        const f32 values[2] = {20, 40};
        const u32 base_view = AddArray(&w, base, 4);
        const u32 index_view = AddArray(&w, sparse_indices, sparse_count);
        const u32 value_view = AddArray(&w, values, 2);
        char extra[200];
        FormatTo(
            extra, sizeof(extra),
            ",\"sparse\":{\"count\":%u,\"indices\":{\"bufferView\":%u,\"componentType\":5123},\"values\":{\"bufferView\":%u}}",
            static_cast<unsigned>(sparse_count), static_cast<unsigned>(index_view), static_cast<unsigned>(value_view));
        w.AddAccessor(base_view, 5126, 4, "SCALAR", extra);
        w.AddAccessor(-1, 5126, 4, "SCALAR", extra);  // a sparse accessor with no base: zeros, then the overrides
        *json = w.JsonInline();
    };
    const u16 good[2] = {1, 3};
    String<HeapAllocator> json(HeapAllocator::Default());
    build(good, 2, &json);
    GltfDocument doc;
    REQUIRE(ParseText(json, none, &doc, &error).Ok());
    Vector<f32> f;
    REQUIRE(ReadAccessorFloats(doc, 0, &f, &error).Ok());
    CHECK(f[0] == 1.0f);
    CHECK(f[1] == 20.0f);
    CHECK(f[2] == 3.0f);
    CHECK(f[3] == 40.0f);
    REQUIRE(ReadAccessorFloats(doc, 1, &f, &error).Ok());
    CHECK(f[0] == 0.0f);
    CHECK(f[3] == 40.0f);
    const u16 past[2] = {1, 4};
    build(past, 2, &json);
    REQUIRE(ParseText(json, none, &doc, &error).Ok());  // the index is checked when the accessor is read
    CHECK(ReadAccessorFloats(doc, 0, &f, &error).Failed());
    CHECK(Says(error, "past the accessor's count"));
    const u16 repeated[2] = {2, 2};
    build(repeated, 2, &json);
    REQUIRE(ParseText(json, none, &doc, &error).Ok());
    CHECK(ReadAccessorFloats(doc, 0, &f, &error).Failed());
    CHECK(Says(error, "strictly increasing"));
    // A sparse count above the accessor's count is refused when the document is read.
    GltfWriter w;
    const f32 base[2] = {1, 2};
    const u16 idx[3] = {0, 1, 2};
    const f32 values[3] = {1, 2, 3};
    const u32 b = AddArray(&w, base, 2);
    const u32 i = AddArray(&w, idx, 3);
    const u32 v = AddArray(&w, values, 3);
    char extra[200];
    FormatTo(
        extra, sizeof(extra),
        ",\"sparse\":{\"count\":3,\"indices\":{\"bufferView\":%u,\"componentType\":5123},\"values\":{\"bufferView\":%u}}",
        static_cast<unsigned>(i), static_cast<unsigned>(v));
    w.AddAccessor(b, 5126, 2, "SCALAR", extra);
    CHECK(ParseText(w.JsonInline(), none, &doc, &error).Failed());
}

TEST_CASE("gltf: a non-finite float in an accessor is refused", "[asset][gltf][accessor]")
{
    NoGltfSource none;
    GltfError error;
    GltfWriter w;
    const u32 bits[2] = {0x3f800000u, 0x7fc00000u};  // 1.0 and a NaN
    w.AddAccessor(AddArray(&w, bits, 2), 5126, 2, "SCALAR");
    GltfDocument doc;
    REQUIRE(ParseText(w.JsonInline(), none, &doc, &error).Ok());
    Vector<f32> f;
    CHECK(ReadAccessorFloats(doc, 0, &f, &error).Failed());
    CHECK(Says(error, "not finite"));
}

TEST_CASE("gltf: base64 decodes exactly, and refuses what is not base64 or is too long", "[asset][gltf]")
{
    GltfError error;
    Vector<byte> out;
    REQUIRE(DecodeBase64(StringView("SGVsbG8="), 100, &out, &error).Ok());
    REQUIRE(out.Size() == 5);
    CHECK(out[0] == 'H');
    CHECK(out[4] == 'o');
    REQUIRE(DecodeBase64(StringView("SGVsbA=="), 100, &out, &error).Ok());
    CHECK(out.Size() == 4);
    REQUIRE(DecodeBase64(StringView(""), 100, &out, &error).Ok());
    CHECK(out.Size() == 0);
    CHECK(DecodeBase64(StringView("SGVsbG8"), 100, &out, &error).Failed());    // not a multiple of four
    CHECK(DecodeBase64(StringView("SGV$bG8="), 100, &out, &error).Failed());   // a character outside the alphabet
    CHECK(DecodeBase64(StringView("SG=sbG8="), 100, &out, &error).Failed());   // a misplaced =
    CHECK(DecodeBase64(StringView("SGVs bG8="), 100, &out, &error).Failed());  // whitespace
    CHECK(DecodeBase64(StringView("SGVsbG8="), 4, &out, &error).Is(ErrorCode::TooLarge));
    // A bomb: forty million characters are refused by length, before anything is decoded.
    String<HeapAllocator> big(HeapAllocator::Default());
    for (u32 i = 0; i < 1000; ++i)
    {
        big.Append(StringView("AAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAA"));
    }
    CHECK(DecodeBase64(big.View(), 100, &out, &error).Is(ErrorCode::TooLarge));
}

TEST_CASE("gltf: the import manifest keeps ids through a reorder and a rename, and round trips as text",
          "[asset][gltf][manifest]")
{
    ImportManifest first;
    first.Add(ImportKind::Mesh, 0, StringView("Cube"), ResourceId::FromValue(0x11));
    first.Add(ImportKind::Mesh, 1, StringView("Sphere"), ResourceId::FromValue(0x12));
    first.Add(ImportKind::Mesh, 2, StringView("Cube"), ResourceId::FromValue(0x13));  // a repeated name
    first.Add(ImportKind::Material, 0, StringView(""), ResourceId::FromValue(0x21));
    String<HeapAllocator> text(HeapAllocator::Default());
    first.Format(&text);
    ImportManifest second;
    GltfError error;
    REQUIRE(second.Parse(text.View(), &error).Ok());
    REQUIRE(second.Entries().Size() == 4);
    // A reordered re-import: the sphere moved to index 0, and two cubes share a name, so they can match only by index.
    const ManifestObject objects[] = {{0, StringView("Sphere")}, {1, StringView("Cube")}, {2, StringView("Cube")}};
    Vector<ResourceId> ids;
    second.Match(ImportKind::Mesh, Span<const ManifestObject>(objects, 3), &ids);
    REQUIRE(ids.Size() == 3);
    CHECK(ids[0] == ResourceId::FromValue(0x12));  // the unique name
    CHECK(ids[1].IsNone());                        // index 1 held the sphere: no entry of its own, so a new id
    CHECK(ids[2] == ResourceId::FromValue(0x13));  // the same name and index as an entry
    // Materials: an unnamed entry matches by index, a renamed object keeps its entry's id.
    const ManifestObject renamed[] = {{0, StringView("Renamed")}};
    second.Match(ImportKind::Material, Span<const ManifestObject>(renamed, 1), &ids);
    CHECK(ids[0] == ResourceId::FromValue(0x21));
    // The result does not depend on the order of the objects: a new unnamed mesh first does not take A's entry.
    ImportManifest two;
    two.Add(ImportKind::Mesh, 0, StringView("A"), ResourceId::FromValue(0xa));
    two.Add(ImportKind::Mesh, 1, StringView("B"), ResourceId::FromValue(0xb));
    const ManifestObject shifted[] = {{0, StringView("")}, {1, StringView("A")}};
    two.Match(ImportKind::Mesh, Span<const ManifestObject>(shifted, 2), &ids);
    CHECK(ids[1] == ResourceId::FromValue(0xa));  // A by its name
    CHECK(ids[0].IsNone());                       // the new mesh must not take A's (or B's) entry
    // Names with blanks, percent signs and non-ASCII bytes survive.
    ImportManifest odd;
    odd.Add(ImportKind::Clip, 3, StringView("a b%c\xc3\xa9"), ResourceId::FromValue(0x31));
    String<HeapAllocator> odd_text(HeapAllocator::Default());
    odd.Format(&odd_text);
    ImportManifest back;
    REQUIRE(back.Parse(odd_text.View(), &error).Ok());
    CHECK(back.Entries()[0].name.View() == StringView("a b%c\xc3\xa9"));
    // Malformed manifests name the line.
    ImportManifest bad;
    CHECK(bad.Parse(StringView("gltf-import 2\n"), &error).Failed());
    CHECK(bad.Parse(StringView("gltf-import 1\nmesh 0 0000000000000011 A\nmesh 1 0000000000000011 B\n"), &error)
              .Failed());
    CHECK(Says(error, "line 3"));
    CHECK(bad.Parse(StringView("gltf-import 1\nwidget 0 0000000000000011 A\n"), &error).Failed());
    CHECK(bad.Parse(StringView("gltf-import 1\nmesh x 0000000000000011 A\n"), &error).Failed());
    CHECK(bad.Parse(StringView("gltf-import 1\nmesh 0 0000000000000011 A%2\n"), &error).Failed());
}

TEST_CASE("gltf: seeded id sources repeat, random ones do not, and none gives the null id", "[asset][gltf][ids]")
{
    SeededIdSource a(7);
    SeededIdSource b(7);
    SeededIdSource c(8);
    for (u32 i = 0; i < 100; ++i)
    {
        const ResourceId x = a.Next();
        CHECK(x == b.Next());
        CHECK_FALSE(x.IsNone());
        CHECK(x != c.Next());
    }
    RandomIdSource random;
    CHECK(random.Next() != random.Next());
}

TEST_CASE("gltf: ImportIntoBackend adds every resource under its path, and adds nothing when one is refused",
          "[asset][gltf]")
{
    TypeRegistry types = test::MakeBackendRegistry();
    HostMemoryBackend backend(types, u64{16} << 20);
    ImportResult result;
    for (u32 i = 0; i < 3; ++i)
    {
        ImportedResource r;
        r.id = test::Rid(0x100 + i);
        r.type = test::kBackendTestType;
        char path[32];
        FormatTo(path, sizeof(path), "/gltf/thing%u", static_cast<unsigned>(i));
        r.path = String<HeapAllocator>(HeapAllocator::Default(), StringView(path));
        r.blob = test::MakeTestBlob(types, r.id, 64, i);
        result.resources.Push(static_cast<ImportedResource&&>(r));
    }
    REQUIRE(ImportIntoBackend(backend, result).Ok());
    ResourceTable table;
    REQUIRE(backend.ReadTable(&table).Ok());
    CHECK(table.Size() == 3);
    ResourceId id;
    CHECK(table.Resolve(StringView("/gltf/thing1"), &id));
    CHECK(id == test::Rid(0x101));
    // A second import with one id that is already there: nothing of it stays.
    ImportResult clash;
    for (u32 i = 0; i < 3; ++i)
    {
        ImportedResource r;
        r.id = test::Rid(0x200 + i);
        if (i == 2)
        {
            r.id = test::Rid(0x100);  // already in the backend
        }
        r.type = test::kBackendTestType;
        char path[32];
        FormatTo(path, sizeof(path), "/other/thing%u", static_cast<unsigned>(i));
        r.path = String<HeapAllocator>(HeapAllocator::Default(), StringView(path));
        r.blob = test::MakeTestBlob(types, r.id, 64, 9 + i);
        clash.resources.Push(static_cast<ImportedResource&&>(r));
    }
    CHECK(ImportIntoBackend(backend, clash).Failed());
    ResourceTable after;
    REQUIRE(backend.ReadTable(&after).Ok());
    CHECK(after.Size() == 3);
    CHECK_FALSE(after.Resolve(StringView("/other/thing0"), &id));
}

TEST_CASE("gltf: ImportGltf reads a valid file's container and reports what it holds", "[asset][gltf]")
{
    NoGltfSource none;
    ImportResult result;
    GltfError error;
    const Vector<byte> glb = TriangleGlb();
    const Status status = ImportGltf(ByteSpan(glb.Data(), glb.Size()), none, ImportOptions(), &result, &error);
    CHECK(status.Is(ErrorCode::Unsupported));  // no resource kind is imported before Day 336
    CHECK(result.report.meshes == 1);
    CHECK(result.report.accessors == 2);
    CHECK(result.resources.Size() == 0);
    GltfError bad;
    const Vector<byte> junk = test::MakeTestBlob(test::MakeBackendRegistry(), test::Rid(1), 32, 1);
    CHECK(ImportGltf(ByteSpan(junk.Data(), junk.Size()), none, ImportOptions(), &result, &bad).Is(ErrorCode::Invalid));
}

}  // namespace engine_placeholder::asset
