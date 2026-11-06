// Phase 5 Day 72: the permutation half of shader_mint - the option declaration, the manifest, and the emitted table.
#include <string>
#include <vector>

#include <catch2/catch_test_macros.hpp>

#include "permutations.h"
#include "renderer/shader_permutation.h"

using namespace engine_placeholder;
using namespace engine_placeholder::shader_mint;

namespace {

const char* kSource = "// header\n// permutation-options: FOG SHADOW_MAP  ALPHA_TEST\nvoid f() {\n#ifdef FOG\n#endif\n"
                      "#if defined(SHADOW_MAP) && ALPHA_TEST\n#endif\n}\n";

std::vector<std::string> Options() { return {"FOG", "SHADOW_MAP", "ALPHA_TEST"}; }

} // namespace

TEST_CASE("The option declaration is read from the source", "[shader_mint][permutations]") {
    std::vector<std::string> options;
    std::string error;
    REQUIRE(ParseOptionDeclaration(kSource, &options, &error));
    CHECK(options == Options());
}

TEST_CASE("Bad option declarations are rejected with a reason", "[shader_mint][permutations]") {
    std::vector<std::string> options;
    std::string error;
    auto fails = [&](const std::string& source, const char* expected) {
        INFO(source);
        CHECK_FALSE(ParseOptionDeclaration(source, &options, &error));
        CHECK(error.find(expected) != std::string::npos);
    };
    fails("void f() {}\n", "no '// permutation-options");
    fails("// permutation-options:\nvoid f() {}\n", "between 1 and");
    fails("// permutation-options: Fog\nint Fog;\n", "upper-case");
    fails("// permutation-options: 1FOG\n", "upper-case");
    fails("// permutation-options: FOG FOG\n#ifdef FOG\n#endif\n", "twice");
    fails("// permutation-options: FOG\nvoid f() {}\n", "never used");
    // Comments do not count as uses: the header comment, a trailing note and a block comment all mention the option.
    fails("// permutation-options: FOG\n// FOG is not implemented yet\nvoid f() {}\n", "never used");
    fails("// permutation-options: FOG\nvoid f() {} // FOG\n", "never used");
    fails("// permutation-options: FOG\n/* #ifdef FOG\n#endif */\nvoid f() {}\n", "never used");
    // A typo in the code is caught even though the real name appears in the comment above it.
    fails("// permutation-options: FOG\n// FOG makes it foggy\n#ifdef FOGG\n#endif\n", "never used");
    // A name that only appears inside a longer identifier does not count as a use.
    fails("// permutation-options: FOG\nint FOGGY; int MY_FOG;\n", "never used");

    std::string many = "// permutation-options:";
    std::string body;
    for (int i = 0; i < 17; ++i) {
        many += " O" + std::to_string(i);
        body += "#ifdef O" + std::to_string(i) + "\n#endif\n";
    }
    fails(many + "\n" + body, "between 1 and 16");

    // The declaration must be near the top of the file.
    std::string late(70, '\n');
    fails(late + "// permutation-options: FOG\n#ifdef FOG\n#endif\n", "first 60 lines");

    std::string sixteen = "// permutation-options:";
    body.clear();
    for (int i = 0; i < 16; ++i) {
        sixteen += " O" + std::to_string(i);
        body += "#ifdef O" + std::to_string(i) + "\n#endif\n";
    }
    CHECK(ParseOptionDeclaration(sixteen + "\n" + body, &options, &error)); // exactly the limit is fine
    CHECK(options.size() == 16);
}

TEST_CASE("The declaration tolerates spacing and sits anywhere in the first 60 lines", "[shader_mint][permutations]") {
    std::vector<std::string> options;
    std::string error;
    const char* body = "#ifdef FOG\n#endif\n";
    for (const char* line : {"//permutation-options: FOG\n", "    // permutation-options:   FOG  \n", "\t//\tpermutation-options: FOG\r\n"}) {
        INFO(line);
        CHECK(ParseOptionDeclaration(std::string(line) + body, &options, &error));
        CHECK(options == std::vector<std::string>{"FOG"});
    }
    std::string deep(59, '\n'); // the declaration is line 60, the last one searched
    CHECK(ParseOptionDeclaration(deep + "// permutation-options: FOG\n" + body, &options, &error));
    // A block comment that mentions the declaration is not a declaration.
    CHECK_FALSE(ParseOptionDeclaration("/* permutation-options: FOG */\n" + std::string(body), &options, &error));
}

TEST_CASE("Modules with identical SPIR-V and reflection are shared", "[shader_mint][permutations]") {
    BuiltModule a{{1, 2, 3}, {9}};
    BuiltModule b{{1, 2, 4}, {9}};
    BuiltModule same_words_other_reflection{{1, 2, 3}, {8}};
    std::vector<BuiltModule> modules;
    std::vector<u32> key_module;
    DedupeModules({a, b, a, same_words_other_reflection, b}, &modules, &key_module);
    CHECK(key_module == std::vector<u32>{0, 1, 0, 2, 1});
    REQUIRE(modules.size() == 3);
    CHECK(modules[0] == a);
    CHECK(modules[2] == same_words_other_reflection); // equal words alone are not enough: the reflection differs

    DedupeModules({}, &modules, &key_module);
    CHECK(modules.empty());
    CHECK(key_module.empty());
}

TEST_CASE("The array emitters use the committed layout", "[shader_mint][permutations]") {
    std::string out;
    AppendWordArray(&out, "kXSpv", {0x07230203, 1, 2, 3, 4, 5, 6});
    CHECK(out == "inline constexpr usize kXSpvWordCount = 7;\n\n"
                 "inline constexpr u32 kXSpv[kXSpvWordCount] = {\n"
                 "    0x07230203, 0x00000001, 0x00000002, 0x00000003, 0x00000004, 0x00000005,\n"
                 "    0x00000006,\n};\n");
    out.clear();
    AppendByteArray(&out, "kXRefl", {1, 2, 3});
    CHECK(out == "inline constexpr usize kXReflByteCount = 3;\n\n"
                 "inline constexpr u8 kXRefl[kXReflByteCount] = {\n    1, 2, 3,\n};\n");
}

TEST_CASE("The manifest lists keys as sets of options", "[shader_mint][permutations]") {
    std::vector<u32> keys;
    std::string error;
    const char* text = "# comment\n\nkey -\nkey SHADOW_MAP ALPHA_TEST   # trailing comment\nkey FOG\nkey FOG SHADOW_MAP\n";
    REQUIRE(ParseManifest(text, Options(), &keys, &error));
    CHECK(keys == std::vector<u32>{0, 1, 3, 6}); // sorted; FOG is bit 0, SHADOW_MAP bit 1, ALPHA_TEST bit 2
    CHECK(DescribeKey(Options(), 6) == "{SHADOW_MAP, ALPHA_TEST}");
    CHECK(DescribeKey(Options(), 0) == "{}");

    // The order of options within a line does not make a different key.
    REQUIRE(ParseManifest("key -\nkey FOG SHADOW_MAP\n", Options(), &keys, &error));
    CHECK(keys == std::vector<u32>{0, 3});
    CHECK_FALSE(ParseManifest("key -\nkey FOG SHADOW_MAP\nkey SHADOW_MAP FOG\n", Options(), &keys, &error));
    CHECK(error.find("listed twice") != std::string::npos);
}

TEST_CASE("Bad manifests are rejected with the line number", "[shader_mint][permutations]") {
    std::vector<u32> keys;
    std::string error;
    auto fails = [&](const char* text, const char* expected) {
        INFO(text);
        CHECK_FALSE(ParseManifest(text, Options(), &keys, &error));
        CHECK(error.find(expected) != std::string::npos);
    };
    fails("key FOG\n", "no base key");
    fails("", "no base key");
    fails("key -\nkey SEPIA\n", "line 2");
    fails("key -\nkey SEPIA\n", "'SEPIA' is not declared");
    fails("key -\nbuild FOG\n", "expected 'key");
    fails("key -\nkey\n", "expected 'key");
    fails("key -\nkey -\n", "listed twice");
    fails("key - FOG\n", "not declared"); // '-' is only valid alone

    // 32 keys is the limit; build them from 5 binary options.
    std::vector<std::string> five = {"A", "B", "C", "D", "E"};
    std::string text;
    for (u32 bits = 0; bits < 32; ++bits) {
        text += "key";
        if (bits == 0) {
            text += " -";
        }
        for (u32 i = 0; i < 5; ++i) {
            if ((bits >> i) & 1u) {
                text += " " + five[i];
            }
        }
        text += "\n";
    }
    CHECK(ParseManifest(text, five, &keys, &error));
    CHECK(keys.size() == 32);
    std::vector<std::string> six = {"A", "B", "C", "D", "E", "F"};
    CHECK_FALSE(ParseManifest(text + "key F\n", six, &keys, &error));
    CHECK(error.find("limit is 32") != std::string::npos);
}

TEST_CASE("The emitted table is deterministic and describes the keys", "[shader_mint][permutations]") {
    std::vector<BuiltModule> modules(2);
    modules[0].words = {0x07230203, 1, 2};
    modules[0].reflection = {9, 8};
    modules[1].words = {0x07230203, 3};
    modules[1].reflection = {7};
    const std::vector<u32> keys = {0, 1, 2};
    const std::vector<u32> key_module = {0, 1, 1}; // the last two keys share a module
    const std::string a = EmitPermutationInc("kDemoFrag", "demo.frag", "demo.frag.slang", "slangc test", Options(), keys, key_module, modules);
    const std::string b = EmitPermutationInc("kDemoFrag", "demo.frag", "demo.frag.slang", "slangc test", Options(), keys, key_module, modules);
    CHECK(a == b);

    CHECK(a.find("// compiler: slangc test") != std::string::npos);
    CHECK(a.find("#include \"demo.frag.spv.inc\"") != std::string::npos);
    CHECK(a.find("kDemoFragPerm1Spv") != std::string::npos);
    CHECK(a.find("kDemoFragPerm2Spv") == std::string::npos); // only two modules: the base lives in the plain files
    CHECK(a.find("{0x0, 0},") != std::string::npos);
    CHECK(a.find("{0x2, 1},") != std::string::npos);
    CHECK(a.find("\"FOG\"") != std::string::npos);
    CHECK(a.find("kDemoFragPermutations = {\"demo.frag\"") != std::string::npos);
}
