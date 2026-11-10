// Phase 5 Day 72: the permutation half of shader_mint - the option declaration, the manifest, and the emitted table.
// Phase 10 Day 128: written against the engine's Text, StringView and Vector (the tool's own vocabulary).
#include <catch2/catch_test_macros.hpp>

#include "catch_engine_types.h"
#include "lang/initializer_list.h"
#include "permutations.h"
#include "renderer/shader_permutation.h"

using namespace engine_placeholder;
using namespace engine_placeholder::shader_mint;

namespace {

const char* kSource = "// header\n// permutation-options: FOG SHADOW_MAP  ALPHA_TEST\nvoid f() {\n#ifdef FOG\n#endif\n"
                      "#if defined(SHADOW_MAP) && ALPHA_TEST\n#endif\n}\n";

Vector<Text> Names(InitializerList<const char*> names) {
    Vector<Text> out;
    for (const char* n : names) out.Push(MakeText(StringView(n)));
    return out;
}
Vector<Text> Options() { return Names({"FOG", "SHADOW_MAP", "ALPHA_TEST"}); }

bool Contains(const Text& text, const char* needle) { return text.View().Contains(StringView(needle)); }

template <typename T>
bool SameItems(const Vector<T>& v, InitializerList<T> expected) {
    if (v.Size() != expected.size()) return false;
    usize i = 0;
    for (const T& e : expected) {
        if (!(v[i++] == e)) return false;
    }
    return true;
}

bool SameNames(const Vector<Text>& v, InitializerList<const char*> expected) {
    if (v.Size() != expected.size()) return false;
    usize i = 0;
    for (const char* e : expected) {
        if (!(v[i++].View() == StringView(e))) return false;
    }
    return true;
}

// `prefix` followed by `count` repetitions of the lines produced for index 0..count-1.
Text Repeat(const char* prefix, int count, const char* format) {
    Text out = MakeText(StringView(prefix));
    for (int i = 0; i < count; ++i) Appendf(&out, format, i);
    return out;
}

Text Concat(const Text& a, const char* b) {
    Text out = MakeText(a.View());
    out.Append(StringView(b));
    return out;
}

} // namespace

TEST_CASE("The option declaration is read from the source", "[shader_mint][permutations]") {
    Vector<Text> options;
    Text error = MakeText();
    REQUIRE(ParseOptionDeclaration(kSource, &options, &error));
    CHECK(SameNames(options, {"FOG", "SHADOW_MAP", "ALPHA_TEST"}));
}

TEST_CASE("Bad option declarations are rejected with a reason", "[shader_mint][permutations]") {
    Vector<Text> options;
    Text error = MakeText();
    auto fails = [&](StringView source, const char* expected) {
        INFO(Catch::Detail::stringify(source));
        CHECK_FALSE(ParseOptionDeclaration(source, &options, &error));
        CHECK(Contains(error, expected));
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

    Text many = Repeat("// permutation-options:", 17, " O%d");
    many.Append("\n");
    Appendf(&many, "%s", Repeat("", 17, "#ifdef O%d\n#endif\n").CStr());
    fails(many.View(), "between 1 and 16");

    // The declaration must be near the top of the file.
    Text late = MakeText();
    for (int i = 0; i < 70; ++i) late.Append('\n');
    late.Append("// permutation-options: FOG\n#ifdef FOG\n#endif\n");
    fails(late.View(), "first 60 lines");

    Text sixteen = Repeat("// permutation-options:", 16, " O%d");
    sixteen.Append("\n");
    Appendf(&sixteen, "%s", Repeat("", 16, "#ifdef O%d\n#endif\n").CStr());
    CHECK(ParseOptionDeclaration(sixteen.View(), &options, &error)); // exactly the limit is fine
    CHECK(options.Size() == 16);
}

TEST_CASE("The declaration tolerates spacing and sits anywhere in the first 60 lines", "[shader_mint][permutations]") {
    Vector<Text> options;
    Text error = MakeText();
    const char* body = "#ifdef FOG\n#endif\n";
    for (const char* line : {"//permutation-options: FOG\n", "    // permutation-options:   FOG  \n", "\t//\tpermutation-options: FOG\r\n"}) {
        INFO(line);
        Text source = MakeText(StringView(line));
        source.Append(StringView(body));
        CHECK(ParseOptionDeclaration(source.View(), &options, &error));
        CHECK(SameNames(options, {"FOG"}));
    }
    Text deep = MakeText();
    for (int i = 0; i < 59; ++i) deep.Append('\n'); // the declaration is line 60, the last one searched
    deep.Append("// permutation-options: FOG\n");
    deep.Append(StringView(body));
    CHECK(ParseOptionDeclaration(deep.View(), &options, &error));
    // A block comment that mentions the declaration is not a declaration.
    Text block = MakeText("/* permutation-options: FOG */\n");
    block.Append(StringView(body));
    CHECK_FALSE(ParseOptionDeclaration(block.View(), &options, &error));
}

namespace {
BuiltModule Module(InitializerList<u32> words, InitializerList<u8> reflection) {
    BuiltModule m;
    for (u32 w : words) m.words.Push(w);
    for (u8 r : reflection) m.reflection.Push(r);
    return m;
}
} // namespace

TEST_CASE("Modules with identical SPIR-V and reflection are shared", "[shader_mint][permutations]") {
    Vector<BuiltModule> per_key;
    per_key.Push(Module({1, 2, 3}, {9}));
    per_key.Push(Module({1, 2, 4}, {9}));
    per_key.Push(Module({1, 2, 3}, {9}));
    per_key.Push(Module({1, 2, 3}, {8})); // same words, other reflection
    per_key.Push(Module({1, 2, 4}, {9}));
    Vector<BuiltModule> modules;
    Vector<u32> key_module;
    DedupeModules(per_key, &modules, &key_module);
    CHECK(SameItems<u32>(key_module, {0, 1, 0, 2, 1}));
    REQUIRE(modules.Size() == 3);
    CHECK(modules[0] == per_key[0]);
    CHECK(modules[2] == per_key[3]); // equal words alone are not enough: the reflection differs

    Vector<BuiltModule> none;
    DedupeModules(none, &modules, &key_module);
    CHECK(modules.Empty());
    CHECK(key_module.Empty());
}

TEST_CASE("The array emitters use the committed layout", "[shader_mint][permutations]") {
    Text out = MakeText();
    Vector<u32> words;
    for (u32 w : {0x07230203u, 1u, 2u, 3u, 4u, 5u, 6u}) words.Push(w);
    AppendWordArray(&out, "kXSpv", words);
    CHECK(out.View() == StringView("inline constexpr usize kXSpvWordCount = 7;\n\n"
                                   "inline constexpr u32 kXSpv[kXSpvWordCount] = {\n"
                                   "    0x07230203, 0x00000001, 0x00000002, 0x00000003, 0x00000004, 0x00000005,\n"
                                   "    0x00000006,\n};\n"));
    out = MakeText();
    Vector<u8> bytes;
    for (u8 b : {u8{1}, u8{2}, u8{3}}) bytes.Push(b);
    AppendByteArray(&out, "kXRefl", bytes);
    CHECK(out.View() == StringView("inline constexpr usize kXReflByteCount = 3;\n\n"
                                   "inline constexpr u8 kXRefl[kXReflByteCount] = {\n    1, 2, 3,\n};\n"));
}

TEST_CASE("The manifest lists keys as sets of options", "[shader_mint][permutations]") {
    Vector<u32> keys;
    Text error = MakeText();
    const char* text = "# comment\n\nkey -\nkey SHADOW_MAP ALPHA_TEST   # trailing comment\nkey FOG\nkey FOG SHADOW_MAP\n";
    REQUIRE(ParseManifest(text, Options(), &keys, &error));
    CHECK(SameItems<u32>(keys, {0, 1, 3, 6})); // sorted; FOG is bit 0, SHADOW_MAP bit 1, ALPHA_TEST bit 2
    CHECK(DescribeKey(Options(), 6).View() == StringView("{SHADOW_MAP, ALPHA_TEST}"));
    CHECK(DescribeKey(Options(), 0).View() == StringView("{}"));

    // The order of options within a line does not make a different key.
    REQUIRE(ParseManifest("key -\nkey FOG SHADOW_MAP\n", Options(), &keys, &error));
    CHECK(SameItems<u32>(keys, {0, 3}));
    CHECK_FALSE(ParseManifest("key -\nkey FOG SHADOW_MAP\nkey SHADOW_MAP FOG\n", Options(), &keys, &error));
    CHECK(Contains(error, "listed twice"));
}

TEST_CASE("Bad manifests are rejected with the line number", "[shader_mint][permutations]") {
    Vector<u32> keys;
    Text error = MakeText();
    auto fails = [&](const char* text, const char* expected) {
        INFO(text);
        CHECK_FALSE(ParseManifest(text, Options(), &keys, &error));
        CHECK(Contains(error, expected));
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
    Vector<Text> five = Names({"A", "B", "C", "D", "E"});
    Text text = MakeText();
    for (u32 bits = 0; bits < 32; ++bits) {
        text.Append("key");
        if (bits == 0) {
            text.Append(" -");
        }
        for (u32 i = 0; i < 5; ++i) {
            if ((bits >> i) & 1u) {
                text.Append(' ');
                text.Append(five[i].View());
            }
        }
        text.Append("\n");
    }
    CHECK(ParseManifest(text.View(), five, &keys, &error));
    CHECK(keys.Size() == 32);
    Vector<Text> six = Names({"A", "B", "C", "D", "E", "F"});
    CHECK_FALSE(ParseManifest(Concat(text, "key F\n").View(), six, &keys, &error));
    CHECK(Contains(error, "limit is 32"));
}

TEST_CASE("The emitted table is deterministic and describes the keys", "[shader_mint][permutations]") {
    Vector<BuiltModule> modules;
    modules.Push(Module({0x07230203, 1, 2}, {9, 8}));
    modules.Push(Module({0x07230203, 3}, {7}));
    Vector<u32> keys;
    for (u32 k : {0u, 1u, 2u}) keys.Push(k);
    Vector<u32> key_module; // the last two keys share a module
    for (u32 k : {0u, 1u, 1u}) key_module.Push(k);
    const Text a = EmitPermutationInc("kDemoFrag", "demo.frag", "demo.frag.slang", "slangc test", Options(), keys, key_module, modules);
    const Text b = EmitPermutationInc("kDemoFrag", "demo.frag", "demo.frag.slang", "slangc test", Options(), keys, key_module, modules);
    CHECK(a.View() == b.View());

    CHECK(Contains(a, "// compiler: slangc test"));
    CHECK(Contains(a, "#include \"demo.frag.spv.inc\""));
    CHECK(Contains(a, "kDemoFragPerm1Spv"));
    CHECK_FALSE(Contains(a, "kDemoFragPerm2Spv")); // only two modules: the base lives in the plain files
    CHECK(Contains(a, "{0x0, 0},"));
    CHECK(Contains(a, "{0x2, 1},"));
    CHECK(Contains(a, "\"FOG\""));
    CHECK(Contains(a, "kDemoFragPermutations = {\"demo.frag\""));
}
