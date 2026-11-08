// Phase 5 Day 72: shader permutations. The table for permute_test.frag is committed (permute_test.frag.perm.inc); these
// tests check lookup, the error for a key that was not built, module sharing, table validation, and (on a GPU) that the
// module chosen by a key really draws the colour its options describe.
#include <cstdlib>
#include <cstring>
#include <vector>

#include <catch2/catch_test_macros.hpp>

#include "fullscreen.vert.spv.inc"
#include "permute_test.frag.perm.inc"
#include "renderer/shader_permutation.h"
#include "renderer/shader_reflection.h"
#include "rhi/rhi.h"

using namespace engine_placeholder;
using namespace engine_placeholder::renderer;

namespace {

const ShaderPermutations& Table() { return shaders::kPermuteTestFragPermutations; }

PermutationKey KeyOf(std::initializer_list<const char*> names) {
    PermutationKey key;
    ReflectionError error;
    REQUIRE(MakePermutationKey(Table(), names.begin(), static_cast<u32>(names.size()), &key, &error));
    return key;
}

const PermutationModule* Find(std::initializer_list<const char*> names) {
    ReflectionError error;
    const PermutationModule* module = FindPermutation(Table(), KeyOf(names), &error);
    INFO(error.message);
    REQUIRE(module != nullptr);
    return module;
}

} // namespace

TEST_CASE("The committed permutation table is valid and names its options", "[renderer][permutation]") {
    const ShaderPermutations& t = Table();
    CHECK(ValidateShaderPermutations(t) == nullptr);
    CHECK(std::strcmp(t.shader, "permute_test.frag") == 0);
    REQUIRE(t.option_count == 4);
    CHECK(std::strcmp(t.options[0], "INVERT") == 0);
    CHECK(std::strcmp(t.options[3], "DEBUG") == 0);
    CHECK(t.entry_count == 7);
    CHECK(t.module_count == 6);
    CHECK(t.entry_count <= kMaxPermutationKeys);
    CHECK(t.option_count <= kMaxPermutationOptions);
}

TEST_CASE("Keys are built from option names and found by lookup", "[renderer][permutation]") {
    const PermutationModule* base = Find({});
    CHECK(base->words == shaders::kPermuteTestFragSpv); // key with nothing on is the plain module
    CHECK(base->word_count == shaders::kPermuteTestFragSpvWordCount);

    CHECK(KeyOf({"INVERT"}).bits == 1);
    CHECK(KeyOf({"HALF", "INVERT"}).bits == 3);
    CHECK(KeyOf({"INVERT", "HALF"}) == KeyOf({"HALF", "INVERT"})); // order of names does not matter
    CHECK(KeyOf({"DEBUG"}).Has(3));
    CHECK_FALSE(KeyOf({"DEBUG"}).Has(0));

    // Every built key resolves to a module whose words and reflection are usable.
    for (const char* key_names : {"", "INVERT", "HALF", "INVERT HALF", "TINT", "DEBUG"}) {
        std::vector<const char*> names;
        char buffer[64];
        std::strncpy(buffer, key_names, sizeof(buffer) - 1);
        buffer[sizeof(buffer) - 1] = '\0';
        for (char* word = std::strtok(buffer, " "); word != nullptr; word = std::strtok(nullptr, " ")) {
            for (u32 i = 0; i < Table().option_count; ++i) {
                if (std::strcmp(word, Table().options[i]) == 0) {
                    names.push_back(Table().options[i]);
                }
            }
        }
        PermutationKey key;
        ReflectionError error;
        REQUIRE(MakePermutationKey(Table(), names.data(), static_cast<u32>(names.size()), &key, &error));
        const PermutationModule* module = FindPermutation(Table(), key, &error);
        INFO(key_names);
        REQUIRE(module != nullptr);
        CHECK(module->word_count > 20);
        ShaderReflection reflection;
        REQUIRE(ParseShaderReflection(module->reflection, module->reflection_size, &reflection) == nullptr);
        CHECK(reflection.stage == rhi::ShaderStage::Fragment);
    }
}

TEST_CASE("Different options build different modules; identical output is shared", "[renderer][permutation]") {
    const PermutationModule* base = Find({});
    const PermutationModule* invert = Find({"INVERT"});
    const PermutationModule* half = Find({"HALF"});
    const PermutationModule* both = Find({"INVERT", "HALF"});
    CHECK(base != invert);
    CHECK(base != half);
    CHECK(invert != half);
    CHECK(both != invert);
    CHECK(both != half);
    CHECK(base->words != invert->words);

    // `key DEBUG TINT` is in the manifest but builds the same SPIR-V as `key DEBUG`: one module serves both keys.
    CHECK(Find({"DEBUG"}) == Find({"DEBUG", "TINT"}));
    CHECK(Find({"DEBUG"}) != Find({"TINT"}));
}

TEST_CASE("A key that was not built is an error naming the shader and the key", "[renderer][permutation]") {
    ReflectionError error;
    const PermutationKey key = KeyOf({"INVERT", "TINT"}); // valid options, but not in the manifest
    CHECK(FindPermutation(Table(), key, &error) == nullptr);
    CHECK(std::strstr(error.message, "permute_test.frag") != nullptr);
    CHECK(std::strstr(error.message, "{INVERT, TINT}") != nullptr);
    CHECK(std::strstr(error.message, "manifest") != nullptr);

    // A caller that does not want the message may pass no error object.
    CHECK(FindPermutation(Table(), key, nullptr) == nullptr);

    // An option the shader does not declare is rejected when the key is made, naming the option.
    PermutationKey made;
    const char* names[] = {"INVERT", "SEPIA"};
    CHECK_FALSE(MakePermutationKey(Table(), names, 2, &made, &error));
    CHECK(std::strstr(error.message, "SEPIA") != nullptr);
    CHECK(std::strstr(error.message, "permute_test.frag") != nullptr);
}

TEST_CASE("Lookup is bounded by the table, not by the key", "[renderer][permutation]") {
    ReflectionError error;
    // Bits beyond the declared options, and the highest bits of the key word, are simply not found.
    CHECK(FindPermutation(Table(), PermutationKey{0x8000u}, &error) == nullptr);
    CHECK(std::strstr(error.message, "?") != nullptr); // an undeclared option is shown as ?
    CHECK(FindPermutation(Table(), PermutationKey{0xffffffffu}, &error) == nullptr);
    CHECK(FindPermutation(Table(), PermutationKey{0x10u}, &error) == nullptr);

    // With() and Has() ignore indices past the limit instead of shifting out of range.
    PermutationKey key;
    CHECK(key.With(kMaxPermutationOptions).bits == 0);
    CHECK(key.With(200).bits == 0);
    CHECK_FALSE(key.Has(kMaxPermutationOptions));
    CHECK_FALSE(key.Has(200));

    // A table whose entry count claims more than the limit is not read past the limit (and fails validation).
    // The lookup cannot know a table's real length, so the array here really holds the limit's worth of entries (the
    // committed ones, then empties); a sentinel one past the limit matches the key, so reading past it would find it.
    PermutationEntry padded[kMaxPermutationKeys + 1] = {};
    for (u32 i = 0; i < Table().entry_count; ++i) {
        padded[i] = Table().entries[i];
    }
    padded[kMaxPermutationKeys].key_bits = 0x10u;
    ShaderPermutations lying = Table();
    lying.entries = padded;
    lying.entry_count = 1000;
    CHECK(ValidateShaderPermutations(lying) != nullptr);
    CHECK(FindPermutation(lying, PermutationKey{0x10u}, &error) == nullptr);

    // Describing a key never overruns a small buffer.
    char tiny[8];
    DescribePermutationKey(Table(), KeyOf({"INVERT", "HALF", "TINT", "DEBUG"}), tiny, sizeof(tiny));
    CHECK(std::strlen(tiny) < sizeof(tiny));
    char one[1] = {'x'};
    DescribePermutationKey(Table(), PermutationKey{1}, one, 1);
    CHECK(one[0] == '\0');
    DescribePermutationKey(Table(), PermutationKey{1}, nullptr, 0); // no capacity: writes nothing
}

TEST_CASE("A corrupt table is reported as corrupt, not as a missing key", "[renderer][permutation]") {
    const u32 words[4] = {0x07230203, 0, 0, 0};
    const u8 blob[4] = {1, 2, 3, 4};
    const PermutationModule modules[1] = {{words, 4, blob, 4}};
    const char* const options[1] = {"A"};
    const PermutationEntry entries[2] = {{0, 0}, {1, 9}}; // key {A} names module 9 of 1
    ShaderPermutations t = {"corrupt.frag", options, 1, modules, 1, entries, 2};
    ReflectionError error;
    CHECK(ValidateShaderPermutations(t) != nullptr);
    CHECK(FindPermutation(t, PermutationKey{1}, &error) == nullptr);
    CHECK(std::strstr(error.message, "corrupt") != nullptr);
    CHECK(std::strstr(error.message, "manifest") == nullptr); // do not send the reader to the manifest

    // Null arrays are never dereferenced.
    t.entries = nullptr;
    CHECK(FindPermutation(t, PermutationKey{0}, &error) == nullptr);
    t.entries = entries;
    t.modules = nullptr;
    CHECK(FindPermutation(t, PermutationKey{0}, &error) == nullptr);
}

TEST_CASE("Malformed tables are rejected by validation", "[renderer][permutation]") {
    const u32 words[4] = {0x07230203, 0, 0, 0};
    const u8 blob[4] = {1, 2, 3, 4};
    const PermutationModule modules[2] = {{words, 4, blob, 4}, {words, 4, blob, 4}};
    const char* const options[2] = {"A", "B"};
    const PermutationEntry good[3] = {{0, 0}, {1, 1}, {3, 1}};
    ShaderPermutations t = {"t", options, 2, modules, 2, good, 3};
    REQUIRE(ValidateShaderPermutations(t) == nullptr);

    SECTION("no base key") {
        const PermutationEntry entries[2] = {{1, 0}, {3, 1}};
        t.entries = entries;
        t.entry_count = 2;
        CHECK(ValidateShaderPermutations(t) != nullptr);
    }
    SECTION("keys out of order or repeated") {
        const PermutationEntry unsorted[3] = {{0, 0}, {3, 1}, {1, 1}};
        t.entries = unsorted;
        CHECK(ValidateShaderPermutations(t) != nullptr);
        const PermutationEntry repeated[3] = {{0, 0}, {1, 1}, {1, 1}};
        t.entries = repeated;
        CHECK(ValidateShaderPermutations(t) != nullptr);
    }
    SECTION("a key switches on an undeclared option") {
        const PermutationEntry entries[2] = {{0, 0}, {4, 1}};
        t.entries = entries;
        t.entry_count = 2;
        CHECK(ValidateShaderPermutations(t) != nullptr);
    }
    SECTION("a key names a module that does not exist") {
        const PermutationEntry entries[2] = {{0, 0}, {1, 2}};
        t.entries = entries;
        t.entry_count = 2;
        CHECK(ValidateShaderPermutations(t) != nullptr);
    }
    SECTION("base key must use module 0") {
        const PermutationEntry entries[2] = {{0, 1}, {1, 0}};
        t.entries = entries;
        t.entry_count = 2;
        CHECK(ValidateShaderPermutations(t) != nullptr);
    }
    SECTION("empty or null arrays, too many modules") {
        ShaderPermutations broken = t;
        broken.entry_count = 0;
        CHECK(ValidateShaderPermutations(broken) != nullptr);
        broken = t;
        broken.modules = nullptr;
        CHECK(ValidateShaderPermutations(broken) != nullptr);
        broken = t;
        broken.module_count = 4; // more modules than keys
        CHECK(ValidateShaderPermutations(broken) != nullptr);
        broken = t;
        broken.option_count = kMaxPermutationOptions + 1;
        CHECK(ValidateShaderPermutations(broken) != nullptr);
    }
    SECTION("option names that are null, empty or repeated") {
        const char* const null_name[2] = {"A", nullptr};
        t.options = null_name;
        CHECK(ValidateShaderPermutations(t) != nullptr);
        const char* const empty_name[2] = {"A", ""};
        t.options = empty_name;
        CHECK(ValidateShaderPermutations(t) != nullptr);
        const char* const repeated[2] = {"A", "A"};
        t.options = repeated;
        CHECK(ValidateShaderPermutations(t) != nullptr);
    }
    SECTION("a module without words or reflection") {
        const PermutationModule empty[2] = {{words, 4, blob, 4}, {words, 0, blob, 4}};
        t.modules = empty;
        CHECK(ValidateShaderPermutations(t) != nullptr);
        const PermutationModule no_refl[2] = {{words, 4, blob, 4}, {words, 4, nullptr, 0}};
        t.modules = no_refl;
        CHECK(ValidateShaderPermutations(t) != nullptr);
    }
}

namespace {

// Draws the fullscreen triangle with the fragment module chosen by `names` into an 8x8 target and returns pixel (6, 2).
struct Pixel {
    int r, g, b, a;
};

Pixel DrawWithKey(rhi::Device* device, std::initializer_list<const char*> names) {
    const PermutationModule* module = Find(names);

    rhi::ShaderDesc vs;
    vs.stage = rhi::ShaderStage::Vertex;
    vs.SetCode(rhi::SpirvBlob(shaders::kFullscreenVertSpv, shaders::kFullscreenVertSpvWordCount));
    rhi::ShaderDesc fs;
    fs.stage = rhi::ShaderStage::Fragment;
    fs.SetCode(module->Blob(rhi::ShaderFormat::Spirv));
    const rhi::ShaderHandle vshader = device->CreateShader(vs);
    const rhi::ShaderHandle fshader = device->CreateShader(fs);
    REQUIRE(vshader.IsValid());
    REQUIRE(fshader.IsValid());

    rhi::GraphicsPipelineDesc pd;
    pd.vertex_shader = vshader;
    pd.fragment_shader = fshader;
    pd.cull_mode = rhi::CullMode::None;
    pd.color_targets[0].format = rhi::Format::RGBA8_UNorm;
    pd.color_target_count = 1;
    const rhi::PipelineHandle pipeline = device->CreateGraphicsPipeline(pd);
    REQUIRE(pipeline.IsValid());

    rhi::TextureDesc rt;
    rt.width = 8;
    rt.height = 8;
    rt.format = rhi::Format::RGBA8_UNorm;
    rt.usage = rhi::TextureUsage::ColorAttachment | rhi::TextureUsage::CopySrc;
    const rhi::TextureHandle target = device->CreateTexture(rt);
    REQUIRE(target.IsValid());

    device->BeginFrame();
    rhi::CommandList* list = device->CreateCommandList();
    list->Begin();
    list->Barrier(target, rhi::ResourceState::Undefined, rhi::ResourceState::ColorAttachment);
    rhi::RenderingDesc pass;
    pass.color_count = 1;
    pass.color[0].texture = target;
    list->BeginRendering(pass);
    list->SetPipeline(pipeline);
    list->SetViewport(rhi::Viewport{0, 0, 8, 8, 0, 1});
    list->SetScissor(rhi::ScissorRect{0, 0, 8, 8});
    list->Draw(3, 1, 0, 0);
    list->EndRendering();
    list->End();
    rhi::CommandList* lists[] = {list};
    REQUIRE(device->Submit(lists, 1) != 0);
    device->EndFrame();

    std::vector<unsigned char> pixels(8 * 8 * 4);
    REQUIRE(device->ReadbackTexture(target, 0, 0, pixels.data(), pixels.size(), rhi::ResourceState::ColorAttachment));
    const unsigned char* p = &pixels[(2 * 8 + 6) * 4];
    Pixel out = {p[0], p[1], p[2], p[3]};

    device->DestroyPipeline(pipeline);
    device->DestroyShader(vshader);
    device->DestroyShader(fshader);
    device->DestroyTexture(target);
    return out;
}

bool Near(const Pixel& p, int r, int g, int b) {
    auto close = [](int a, int c) { return a - c <= 2 && c - a <= 2; };
    return close(p.r, r) && close(p.g, g) && close(p.b, b) && p.a == 255;
}

} // namespace

TEST_CASE("The module a key selects draws the colour its options describe", "[renderer][permutation][gpu]") {
    rhi::DeviceCreateInfo info;
    info.validation = rhi::ValidationSetting::On;
    info.sync_validation = true;
    info.pipeline_cache = false;
    rhi::DevicePtr device = rhi::CreateDevice(info);
    if (device == nullptr) {
        const char* required = std::getenv("ENGINE_RHI_REQUIRE_GPU");
        if (required != nullptr && required[0] == '1') {
            FAIL("ENGINE_RHI_REQUIRE_GPU=1 but no Vulkan device could be created");
        }
        SKIP("no Vulkan device on this machine");
    }

    // The shader's flat colour is (0.8, 0.4, 0.2); each option changes it analytically (see permute_test.frag.slang).
    CHECK(Near(DrawWithKey(device.Get(), {}), 204, 102, 51));
    CHECK(Near(DrawWithKey(device.Get(), {"INVERT"}), 51, 153, 204));
    CHECK(Near(DrawWithKey(device.Get(), {"HALF"}), 102, 51, 26));
    CHECK(Near(DrawWithKey(device.Get(), {"INVERT", "HALF"}), 26, 77, 102));
    CHECK(Near(DrawWithKey(device.Get(), {"TINT"}), 204, 102, 26));
    // DEBUG paints (u, v, 0): pixel (6, 2) of 8x8 is at uv (6.5/8, 2.5/8). The shared DEBUG TINT key draws the same.
    CHECK(Near(DrawWithKey(device.Get(), {"DEBUG"}), 207, 80, 0));
    CHECK(Near(DrawWithKey(device.Get(), {"DEBUG", "TINT"}), 207, 80, 0));

    device->WaitIdle();
    device.Reset();
}
