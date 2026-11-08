// shader_mint's Direct3D 12 rules (Phase 13 Day 163): vertex inputs are TEXCOORD<location>, the DXIL build's registers follow the
// Vulkan bindings, the Windows command line quotes arguments so the child reads them back unchanged, and RunProcess starts a real
// child with those arguments on every platform.
#include <catch2/catch_test_macros.hpp>

#include "io/file.h"
#include "io/file_system.h"
#include "io/path.h"
#include "reflect_json.h"
#include "run_process.h"

using namespace engine_placeholder;
using namespace engine_placeholder::shader_mint;
using renderer::ShaderReflection;

namespace {

Text VertexJson(const char* semantic_fields) {
    Text json = MakeText(R"({"parameters": [], "entryPoints": [{"name": "main", "stage": "vertex", "bindings": [],
        "parameters": [{"name": "pos", "binding": {"kind": "varyingInput", "index": 2}, )");
    json.Append(StringView(semantic_fields));
    json.Append(StringView(R"(,
          "type": {"kind": "vector", "elementCount": 3, "elementType": {"kind": "scalar", "scalarType": "float32"}}}]}]})"));
    return json;
}

// A fragment shader with a uniform block at (set 1, binding 3), a texture at (1, 4), a read-only buffer at (2, 0) and push constants.
const char* kFragmentJson = R"({
  "parameters": [
    {"name": "mat", "binding": {"kind": "descriptorTableSlot", "space": 1, "index": 3},
     "type": {"kind": "constantBuffer", "elementType": {"kind": "struct", "name": "Material", "fields": [
        {"name": "tint", "type": {"kind": "vector", "elementCount": 4, "elementType": {"kind": "scalar", "scalarType": "float32"}},
         "binding": {"kind": "uniform", "offset": 0, "size": 16}}],
       "sizes": [{"kind": "uniform", "value": 16, "alignment": 16}]}}},
    {"name": "tex", "binding": {"kind": "descriptorTableSlot", "space": 1, "index": 4}, "type": {"kind": "resource", "baseShape": "texture2D"}},
    {"name": "objects", "binding": {"kind": "descriptorTableSlot", "space": 2, "index": 0},
     "type": {"kind": "resource", "baseShape": "structuredBuffer",
       "resultType": {"kind": "struct", "name": "Object", "fields": [], "sizes": [{"kind": "uniform", "value": 72, "alignment": 16}]}}},
    {"name": "push", "bindings": [{"kind": "pushConstantBuffer", "index": 0}, {"kind": "descriptorTableSlot", "space": 15, "index": 0}],
     "type": {"kind": "constantBuffer", "elementType": {"kind": "struct", "name": "Push", "fields": [
        {"name": "scale", "type": {"kind": "scalar", "scalarType": "float32"}, "binding": {"kind": "uniform", "offset": 0, "size": 4}}],
       "sizes": [{"kind": "uniform", "value": 4, "alignment": 4}]}}}
  ],
  "entryPoints": [{"name": "main", "stage": "fragment",
    "bindings": [{"name": "mat", "binding": {"kind": "descriptorTableSlot", "space": 1, "index": 3, "used": 1}},
                 {"name": "tex", "binding": {"kind": "descriptorTableSlot", "space": 1, "index": 4, "used": 1}},
                 {"name": "objects", "binding": {"kind": "descriptorTableSlot", "space": 2, "index": 0, "used": 1}},
                 {"name": "push", "bindings": [{"kind": "pushConstantBuffer", "index": 0},
                                               {"kind": "descriptorTableSlot", "space": 15, "index": 0, "used": 1}]}]}]
})";

// What `slangc -target dxil -reflection-json` reports for the same shader when it follows the register rule.
const char* kGoodDxilJson = R"({"parameters": [
    {"name": "mat", "binding": {"kind": "constantBuffer", "space": 1, "index": 3}},
    {"name": "tex", "binding": {"kind": "shaderResource", "space": 1, "index": 4}},
    {"name": "objects", "binding": {"kind": "shaderResource", "space": 2, "index": 0}},
    {"name": "push", "binding": {"kind": "constantBuffer", "space": 15, "index": 0}}]})";

// `text` with the first `from` replaced by `to`.
Text EditJson(StringView text, StringView from, StringView to) {
    const usize at = text.Find(from);
    REQUIRE(at != text.Length());
    Text out = MakeText(text.Substring(0, at));
    out.Append(to);
    out.Append(text.Substring(at + from.Length(), text.Length() - at - from.Length()));
    return out;
}

} // namespace

TEST_CASE("A vertex input must have the semantic TEXCOORD with its location's number", "[shader_mint][dxil]") {
    ShaderReflection r;
    Text error = MakeText();
    CHECK(ReflectionFromJson(VertexJson(R"("semanticName": "TEXCOORD", "semanticIndex": 2)").View(), &r, &error));
    CHECK_FALSE(ReflectionFromJson(VertexJson(R"("semanticName": "POSITION")").View(), &r, &error));
    CHECK(error.View().Contains(StringView("must be TEXCOORD2")));
    CHECK_FALSE(ReflectionFromJson(VertexJson(R"("semanticName": "TEXCOORD", "semanticIndex": 1)").View(), &r, &error));
    CHECK(error.View().Contains(StringView("TEXCOORD1")));
    CHECK_FALSE(ReflectionFromJson(VertexJson(R"("semanticName": "TEXCOORD")").View(), &r, &error)); // TEXCOORD0 at location 2
}

TEST_CASE("The DXIL build's registers must follow the bindings, and push constants are b0 in space 15", "[shader_mint][dxil]") {
    ShaderReflection r;
    Text error = MakeText();
    REQUIRE(ReflectionFromJson(StringView(kFragmentJson), &r, &error));
    REQUIRE(r.has_push_constants); // the two-binding form is read as the push constant range
    CHECK(r.binding_count == 3);
    CHECK(r.bindings[2].type == rhi::BindingType::ReadOnlyStorageBuffer); // a StructuredBuffer
    CHECK(r.bindings[2].stride == 80); // 72 bytes rounded up to the element's 16-byte alignment (Phase 13 Day 166)
    CHECK(r.bindings[0].stride == 0);
    CHECK(CheckD3D12Registers(r, StringView(kGoodDxilJson), &error));

    const StringView good(kGoodDxilJson);
    CHECK_FALSE(CheckD3D12Registers(r, EditJson(good, R"("space": 1, "index": 4)", R"("space": 0, "index": 1)").View(), &error));
    CHECK(error.View().Contains(StringView("binding 'tex'")));
    CHECK_FALSE(CheckD3D12Registers(r, EditJson(good, R"("kind": "shaderResource", "space": 2)", R"("kind": "unorderedAccess", "space": 2)").View(), &error));
    CHECK(error.View().Contains(StringView("must be shaderResource register 0 in space 2")));
    CHECK_FALSE(CheckD3D12Registers(r, EditJson(good, R"("space": 15, "index": 0)", R"("space": 0, "index": 2)").View(), &error));
    CHECK(error.View().Contains(StringView("push constant")));
    CHECK_FALSE(CheckD3D12Registers(r, EditJson(good, R"({"name": "mat",)", R"({"name": "other",)").View(), &error));
    CHECK(error.View().Contains(StringView("missing from the DXIL")));
    CHECK_FALSE(CheckD3D12Registers(r, StringView("not json"), &error));
}

TEST_CASE("A structured buffer needs a known element size, and a ByteAddressBuffer is refused", "[shader_mint][dxil]") {
    ShaderReflection r;
    Text error = MakeText();
    const StringView good(kFragmentJson);
    CHECK_FALSE(ReflectionFromJson(EditJson(good, R"("value": 72, "alignment": 16)", R"("value": 0, "alignment": 16)").View(), &r, &error));
    CHECK(error.View().Contains(StringView("element size is unknown")));
    CHECK_FALSE(ReflectionFromJson(EditJson(good, R"("value": 72, "alignment": 16)", R"("value": 4096, "alignment": 16)").View(), &r, &error));
    CHECK(error.View().Contains(StringView("above 2048")));
    CHECK_FALSE(ReflectionFromJson(EditJson(good, R"("baseShape": "structuredBuffer")", R"("baseShape": "byteAddressBuffer")").View(), &r, &error));
    CHECK(error.View().Contains(StringView("ByteAddressBuffer")));
}

TEST_CASE("Windows arguments are quoted so CommandLineToArgvW reads them back unchanged", "[shader_mint]") {
    struct Case {
        const char* arg;
        const char* quoted;
    };
    const Case cases[] = {
        {"plain", "plain"},
        {"C:/Program Files/x", "\"C:/Program Files/x\""},
        {"", "\"\""},
        {"a\"b", "\"a\\\"b\""},
        {"tab\there", "\"tab\there\""},
        {"back\\slash", "back\\slash"},              // no quote follows: backslashes stay literal and unquoted
        {"dir with space\\", "\"dir with space\\\\\""}, // before the closing quote they are doubled
        {"x\\\"y z", "\"x\\\\\\\"y z\""},              // a backslash before a quote is doubled and the quote escaped
    };
    for (const Case& c : cases) {
        Text line = MakeText();
        AppendWindowsArgument(&line, StringView(c.arg));
        INFO(c.arg);
        CHECK(line.View() == StringView(c.quoted));
    }
    Text two = MakeText();
    AppendWindowsArgument(&two, StringView("a"));
    AppendWindowsArgument(&two, StringView("b c"));
    CHECK(two.View() == StringView("a \"b c\""));
}

TEST_CASE("RunProcess starts a child with every argument intact and reports its exit status", "[shader_mint]") {
    io::Path out;
    REQUIRE(io::Path::Parse(StringView(SHADER_MINT_TEST_DIR "/args_probe_out.txt"), io::NativePathStyle(), &out));
    io::Remove(out);
    Vector<Text> args;
    args.Push(MakeText(StringView(SHADER_MINT_ARGS_PROBE)));
    args.Push(MakeText(StringView(SHADER_MINT_TEST_DIR "/args_probe_out.txt")));
    const char* tricky[] = {"plain", "with space", "", "quote\"inside", "trailing\\", "x\\\"y z", "-DENGINE_TARGET_D3D12=1"};
    for (const char* t : tricky) args.Push(MakeText(StringView(t)));
    REQUIRE(RunProcess(args));
    Vector<byte> bytes;
    REQUIRE(io::ReadFile(out, &bytes).Ok());
    Text expected = MakeText();
    for (const char* t : tricky) {
        expected.Append(StringView(t));
        expected.Append('\n');
    }
    CHECK(StringView(reinterpret_cast<const char*>(bytes.Data()), bytes.Size()) == expected.View());
    io::Remove(out);

    Vector<Text> failing;
    failing.Push(MakeText(StringView(SHADER_MINT_ARGS_PROBE)));
    failing.Push(MakeText(StringView("--exit-3")));
    CHECK_FALSE(RunProcess(failing)); // a non-zero status is a failure
    Vector<Text> missing;
    missing.Push(MakeText(StringView(SHADER_MINT_TEST_DIR "/no_such_program_here")));
    CHECK_FALSE(RunProcess(missing));
}
