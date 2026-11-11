#include <string>
#include <vector>

#include <catch2/catch_test_macros.hpp>

#include "reflect_json.h"

using namespace engine_placeholder;
using namespace engine_placeholder::shader_mint;
using renderer::ShaderReflection;

namespace
{

// A fragment shader with one uniform block { float4 tint; } at (set 1, binding 3) and one sampler at binding 4.
const char* kGoodJson = R"({
  "version": "1.1",
  "parameters": [
    {"name": "mat", "binding": {"kind": "descriptorTableSlot", "space": 1, "index": 3},
     "type": {"kind": "constantBuffer", "elementType": {"kind": "struct", "name": "Material", "fields": [
        {"name": "tint", "type": {"kind": "vector", "elementCount": 4, "elementType": {"kind": "scalar", "scalarType": "float32"}},
         "binding": {"kind": "uniform", "offset": 0, "size": 16}}],
       "sizes": [{"kind": "uniform", "value": 16, "alignment": 16}]}}},
    {"name": "smp", "binding": {"kind": "descriptorTableSlot", "space": 1, "index": 4}, "type": {"kind": "samplerState"}}
  ],
  "entryPoints": [{"name": "main", "stage": "fragment",
    "bindings": [{"name": "mat", "binding": {"kind": "descriptorTableSlot", "space": 1, "index": 3, "used": 1}},
                 {"name": "smp", "binding": {"kind": "descriptorTableSlot", "space": 1, "index": 4, "used": 1}}]}]
})";

// Replaces the first occurrence of `from` with `to`.
// The JSON text is std::string: this file tests the nlohmann/json boundary.
StringView V(const std::string& s)
{
    return StringView(s.data(), s.size());
}
StringView V(const char* s)
{
    return StringView(s);
}

std::string Edit(std::string text, const std::string& from, const std::string& to)
{
    const size_t at = text.find(from);
    REQUIRE(at != std::string::npos);
    text.replace(at, from.size(), to);
    return text;
}

bool Fails(const std::string& json, const char* expected_in_message)
{
    ShaderReflection r;
    Text error = MakeText();
    if (ReflectionFromJson(V(json), &r, &error))
    {
        return false;
    }
    INFO("error was: " << error.CStr());
    return error.View().Contains(StringView(expected_in_message));
}

}  // namespace

TEST_CASE("A well-formed reflection file converts", "[shader_mint]")
{
    ShaderReflection r;
    Text error = MakeText();
    REQUIRE(ReflectionFromJson(V(kGoodJson), &r, &error));
    CHECK(r.stage == rhi::ShaderStage::Fragment);
    REQUIRE(r.binding_count == 2);
    CHECK(r.bindings[0].set == 1);
    CHECK(r.bindings[0].binding == 3);
    CHECK(r.bindings[0].type == rhi::BindingType::UniformBuffer);
    CHECK(r.blocks[r.bindings[0].block].size == 16);
    CHECK(r.bindings[1].type == rhi::BindingType::Sampler);
}

TEST_CASE("A binding the entry point does not use is left out", "[shader_mint]")
{
    ShaderReflection r;
    Text error = MakeText();
    REQUIRE(
        ReflectionFromJson(V(Edit(kGoodJson, "\"index\": 4, \"used\": 1", "\"index\": 4, \"used\": 0")), &r, &error));
    CHECK(r.binding_count == 1);
}

TEST_CASE("Bad input is rejected with a message", "[shader_mint]")
{
    CHECK(Fails("not json at all", "not valid JSON"));
    CHECK(Fails("[1, 2, 3]", "not valid JSON"));
    CHECK(Fails("{}", "exactly one entry point"));
    CHECK(Fails(Edit(kGoodJson, "\"stage\": \"fragment\"", "\"stage\": \"geometry\""), "unsupported stage"));
    CHECK(Fails(Edit(kGoodJson, "\"kind\": \"samplerState\"", "\"kind\": \"parameterBlock\""), "unsupported type"));
    CHECK(Fails(Edit(kGoodJson, "\"scalarType\": \"float32\"", "\"scalarType\": \"float64\""), "unsupported"));
    CHECK(Fails(Edit(kGoodJson, "\"name\": \"Material\"",
                     "\"name\": \"A_block_name_that_is_far_too_long_to_fit_in_the_fixed_array\""),
                "longer than"));
    CHECK(Fails(Edit(kGoodJson, "\"stage\": \"fragment\"", "\"stage\": \"compute\""), "threadGroupSize"));
    CHECK(Fails(Edit(kGoodJson, "\"smp\", \"binding\"", "\"missing\", \"binding\""), "no matching parameter"));
    CHECK(Fails(Edit(kGoodJson, "\"descriptorTableSlot\", \"space\": 1, \"index\": 4, \"used\"",
                     "\"specializationConstant\", \"space\": 1, \"index\": 4, \"used\""),
                "unsupported slot kind"));
}

TEST_CASE("Two bindings at the same (set, binding) are rejected", "[shader_mint]")
{
    CHECK(Fails(
        Edit(Edit(kGoodJson, "\"index\": 4}", "\"index\": 3}"), "\"index\": 4, \"used\"", "\"index\": 3, \"used\""),
        "share a set and binding"));
}

TEST_CASE("The SPIR-V cross-check catches a disagreement", "[shader_mint]")
{
    ShaderReflection r;
    Text error = MakeText();
    REQUIRE(ReflectionFromJson(V(kGoodJson), &r, &error));
    // A module with the right header and decorations for (set 1, binding 3) and (set 1, binding 4).
    auto decorate = [](std::vector<u32>& w, u32 id, u32 decoration, u32 value)
    {
        w.push_back((4u << 16) | 71u);
        w.push_back(id);
        w.push_back(decoration);
        w.push_back(value);
    };
    std::vector<u32> words = {0x07230203u, 0x00010500u, 0, 20, 0};
    decorate(words, 1, 34, 1);
    decorate(words, 1, 33, 3);
    decorate(words, 2, 34, 1);
    decorate(words, 2, 33, 4);
    CHECK(CrossCheckSpirv(r, words.data(), words.size(), &error));

    std::vector<u32> wrong = words;
    wrong[wrong.size() - 1] = 5;  // binding 4 -> 5
    CHECK_FALSE(CrossCheckSpirv(r, wrong.data(), wrong.size(), &error));
    CHECK(error.View().Contains(StringView("(set, binding)")));

    std::vector<u32> broken = words;
    broken[5] = (0u << 16) | 71u;  // zero-length instruction
    CHECK_FALSE(CrossCheckSpirv(r, broken.data(), broken.size(), &error));
}

TEST_CASE("Missing or mistyped layout facts are errors, not zeros", "[shader_mint]")
{
    CHECK(Fails(Edit(kGoodJson, "\"offset\": 0, ", ""), "missing or invalid 'offset'"));
    CHECK(Fails(Edit(kGoodJson, "\"size\": 16}}]", "\"size\": \"16\"}}]"), "missing or invalid 'size'"));
    CHECK(Fails(
        Edit(kGoodJson, "\"sizes\": [{\"kind\": \"uniform\", \"value\": 16, \"alignment\": 16}]", "\"sizes\": []"),
        "no uniform size"));
    CHECK(Fails(Edit(kGoodJson, "\"space\": 1, \"index\": 3, \"used\"", "\"space\": \"one\", \"index\": 3, \"used\""),
                "missing or invalid 'space'"));
    CHECK(Fails(Edit(kGoodJson, "\"space\": 1, \"index\": 3, \"used\"", "\"space\": 1, \"used\""),
                "missing or invalid 'index'"));
}

TEST_CASE("A stale error message is cleared by a successful call", "[shader_mint]")
{
    ShaderReflection r;
    Text error = MakeText("left over");
    REQUIRE(ReflectionFromJson(V(kGoodJson), &r, &error));
    CHECK(error.Length() == 0);
}

TEST_CASE("A compute group size that does not fit 32 bits is rejected", "[shader_mint]")
{
    const std::string compute = Edit(kGoodJson, "\"stage\": \"fragment\"",
                                     "\"stage\": \"compute\", \"threadGroupSize\": [4294967297, 1, 1]");
    CHECK(Fails(compute, "32-bit"));
    ShaderReflection r;
    Text error = MakeText();
    CHECK(ReflectionFromJson(V(Edit(compute, "4294967297", "64")), &r, &error));
    CHECK(r.local_size[0] == 64);
}

TEST_CASE("Vertex input locations are checked against the SPIR-V Input variables only", "[shader_mint]")
{
    // A vertex shader with one float3 input at location 2.
    const char* json = R"({"parameters": [], "entryPoints": [{"name": "main", "stage": "vertex", "bindings": [],
        "parameters": [{"name": "pos", "binding": {"kind": "varyingInput", "index": 2}, "semanticName": "TEXCOORD", "semanticIndex": 2,
          "type": {"kind": "vector", "elementCount": 3, "elementType": {"kind": "scalar", "scalarType": "float32"}}}]}]})";
    ShaderReflection r;
    Text error = MakeText();
    REQUIRE(ReflectionFromJson(V(json), &r, &error));
    REQUIRE(r.vertex_input_count == 1);
    auto variable = [](std::vector<u32>& w, u32 id, u32 storage_class)
    {
        w.push_back((4u << 16) | 59u);  // OpVariable %type %id StorageClass
        w.push_back(1);
        w.push_back(id);
        w.push_back(storage_class);
    };
    auto location = [](std::vector<u32>& w, u32 id, u32 value)
    {
        w.push_back((4u << 16) | 71u);
        w.push_back(id);
        w.push_back(30);
        w.push_back(value);
    };
    std::vector<u32> good = {0x07230203u, 0x00010500u, 0, 20, 0};
    variable(good, 10, 1);  // Input
    location(good, 10, 2);
    CHECK(CrossCheckSpirv(r, good.data(), good.size(), &error));

    std::vector<u32> output_only = {0x07230203u, 0x00010500u, 0, 20, 0};
    variable(output_only, 10, 3);  // Output: location 2 exists, but not as an input
    location(output_only, 10, 2);
    CHECK_FALSE(CrossCheckSpirv(r, output_only.data(), output_only.size(), &error));

    std::vector<u32> extra_input = good;
    variable(extra_input, 11, 1);
    location(extra_input, 11, 5);  // an input the reflection does not list
    CHECK_FALSE(CrossCheckSpirv(r, extra_input.data(), extra_input.size(), &error));
}
