// Container audit series Day 2: Fnv1a64, the engine's one byte hash, against the published FNV-1a 64-bit values and
// against the formulas the modules wrote out by hand before it (their values must not change).
#include <catch2/catch_test_macros.hpp>

#include "containers/array.h"
#include "containers/hash.h"
#include "string_view.h"
#include "types.h"

using namespace engine_placeholder;

TEST_CASE("Fnv1a64 gives the published FNV-1a 64-bit values", "[containers][hash]")
{
    CHECK(Fnv1a64(StringView("")) == 0xcbf29ce484222325ull);
    CHECK(Fnv1a64(StringView("a")) == 0xaf63dc4c8601ec8cull);
    CHECK(Fnv1a64(StringView("foobar")) == 0x85944171f73967e8ull);
    const Array<byte, 6> bytes = {byte{'f'}, byte{'o'}, byte{'o'}, byte{'b'}, byte{'a'}, byte{'r'}};
    CHECK(Fnv1a64(ByteSpan(bytes.Data(), bytes.Size())) == 0x85944171f73967e8ull);
    CHECK(Fnv1a64Builder().AddCString("foo").AddText(StringView("bar")).Value() == 0x85944171f73967e8ull);
}

TEST_CASE("Fnv1a64Builder matches the hand-written copies it replaces", "[containers][hash]")
{
    // The byte loop of resource_table.cpp, io/path.cpp, settings/key.cpp and the others.
    u64 by_hand = 14695981039346656037ull;
    const char* text = "meshes/crate.mesh";
    for (usize i = 0; text[i] != '\0'; ++i)
    {
        by_hand = (by_hand ^ static_cast<u8>(text[i])) * 1099511628211ull;
    }
    CHECK(Fnv1a64(StringView(text)) == by_hand);
    // The word fold of the state hashes (physics, the script runtime, the script demo): h = (h ^ word) * prime.
    u64 folded = 14695981039346656037ull;
    const Array<u64, 3> words = {0x1234u, 0xffffffffffffffffull, 0};
    for (u64 w : words)
    {
        folded ^= w;
        folded *= 1099511628211ull;
    }
    Fnv1a64Builder b;
    for (u64 w : words)
    {
        b.AddWord(w);
    }
    CHECK(b.Value() == folded);
    // A seed continues an earlier hash.
    CHECK(Fnv1a64Builder(Fnv1a64(StringView("foo"))).AddText(StringView("bar")).Value() ==
          Fnv1a64(StringView("foobar")));
}

TEST_CASE("HashCombine depends on both hashes and their order", "[containers][hash]")
{
    const usize a = Hash<u64>{}(1);
    const usize b = Hash<u64>{}(2);
    CHECK(HashCombine(a, b) != HashCombine(b, a));
    CHECK(HashCombine(a, b) != HashCombine(a, a));
    CHECK(HashCombine(a, b) == HashCombine(a, b));
}
