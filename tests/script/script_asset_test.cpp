// Phase 27 Day 462: the script blob and the script minter (tools/asset_mint/script_source.cpp, compiled into this
// test).

#include <catch2/catch_test_macros.hpp>

#include "asset/script_blob.h"
#include "containers/shared_bytes.h"
#include "cstr/cstr.h"
#include "cstr/mem_ops.h"
#include "script/luajit/luajit_backend.h"
#include "script_source.h"

namespace vse::asset
{
namespace
{

StringView Text(const char* s)
{
    return StringView(s, CStrLength(s));
}

ScriptBuild SampleBuild()
{
    ScriptBuild b;
    b.language = String<HeapAllocator>(HeapAllocator::Default(), Text("luajit"));
    b.language_version = String<HeapAllocator>(HeapAllocator::Default(), Text("2.1"));
    b.source = String<HeapAllocator>(HeapAllocator::Default(), Text("behavior('a', {})\n"));
    b.entries.Push(String<HeapAllocator>(HeapAllocator::Default(), Text("a")));
    b.dependencies.Push(ResourceId::FromValue(5));
    b.dependencies.Push(ResourceId::FromValue(9));
    b.bytecode_key = String<HeapAllocator>(HeapAllocator::Default(), Text("k1"));
    b.bytecode.Push(byte{1});
    b.bytecode.Push(byte{2});
    return b;
}

u64 NextRandom(u64* state)
{
    *state = *state * 6364136223846793005ull + 1442695040888963407ull;
    return *state >> 17;
}

// A scratch script source for the minter tests.
ScriptSource Source(u64 id, const char* lua)
{
    ScriptSource s;
    s.id = ResourceId::FromValue(id);
    s.lua = String<HeapAllocator>(HeapAllocator::Default(), Text(lua));
    return s;
}

void AddDep(ScriptSource* s, const char* name, u64 id)
{
    ScriptDependency d;
    d.name = String<HeapAllocator>(HeapAllocator::Default(), Text(name));
    d.id = ResourceId::FromValue(id);
    s->dependencies.Push(Move(d));
}

struct Minted
{
    Status status;
    Vector<byte> blob;
    ScriptSourceError error;
};

Minted Mint(const ScriptSource& source, const char* lua, bool bytecode = false, bool run = true)
{
    static UniquePtr<script::Backend> backend = script::luajit::CreateLuaJitBackend();
    Minted m;
    const TypeRegistry types;
    ScriptMintOptions options;
    options.backend = backend.Get();
    options.bytecode = bytecode;
    options.check_entries = run;
    m.status = MintScript(source, Text(lua), types, options, &m.blob, &m.error);
    return m;
}

}  // namespace

TEST_CASE("SCRP: a blob builds, opens and reads back", "[script][asset][scrp]")
{
    const TypeRegistry types;
    Vector<byte> blob;
    REQUIRE(BuildScriptBlob(types, ResourceId::FromValue(77), SampleBuild(), &blob).Ok());
    ScriptView view;
    REQUIRE(view.Open(CopyShared(blob), types).Ok());
    CHECK(view.Id() == ResourceId::FromValue(77));
    CHECK(StringView(reinterpret_cast<const char*>(view.Source().Data()), view.Source().Size()) ==
          Text("behavior('a', {})\n"));
    CHECK(view.Language() == Text("luajit"));
    CHECK(view.LanguageVersion() == Text("2.1"));
    REQUIRE(view.EntryCount() == 1);
    CHECK(view.Entry(0) == Text("a"));
    CHECK(view.BytecodeFor(Text("k1")).Size() == 2);
    CHECK(view.BytecodeFor(Text("other")).Size() == 0);
    CHECK(view.SourceHash() != 0);
    // The source is a range of the loaded buffer, not a copy.
    CHECK(view.Source().Data() == view.Bytes()->Bytes().Data() + view.SourceOffset());
}

TEST_CASE("SCRP: a build that breaks a rule is refused", "[script][asset][scrp]")
{
    const TypeRegistry types;
    Vector<byte> blob;
    ScriptBuild unsorted = SampleBuild();
    unsorted.dependencies[0] = ResourceId::FromValue(9);
    unsorted.dependencies[1] = ResourceId::FromValue(5);
    CHECK(BuildScriptBlob(types, ResourceId::FromValue(1), unsorted, &blob).Failed());
    ScriptBuild no_key = SampleBuild();
    no_key.bytecode_key = String<HeapAllocator>(HeapAllocator::Default());
    CHECK(BuildScriptBlob(types, ResourceId::FromValue(1), no_key, &blob).Failed());
    CHECK(BuildScriptBlob(types, ResourceId::None(), SampleBuild(), &blob).Failed());
    ScriptBuild big = SampleBuild();
    for (u32 i = 0; i < kMaxScriptSourceBytes; ++i)
    {
        big.source.Append('x');
    }
    CHECK(BuildScriptBlob(types, ResourceId::FromValue(1), big, &blob).Failed());
}

TEST_CASE("SCRP: a truncated or damaged blob is refused by value", "[script][asset][scrp][hostile]")
{
    const TypeRegistry types;
    Vector<byte> blob;
    REQUIRE(BuildScriptBlob(types, ResourceId::FromValue(77), SampleBuild(), &blob).Ok());
    u64 state = 12345;
    for (usize cut = 0; cut < blob.Size(); cut += 3)
    {
        Vector<byte> shorter;
        for (usize i = 0; i < cut; ++i)
        {
            shorter.Push(blob[i]);
        }
        ScriptView view;
        CHECK(view.Open(CopyShared(shorter), types).Failed());
    }
    // Mutated blobs: whatever Open accepts must read without leaving the bytes (the sanitizers watch).
    u32 accepted = 0;
    for (u32 round = 0; round < 3000; ++round)
    {
        Vector<byte> bad;
        for (usize i = 0; i < blob.Size(); ++i)
        {
            bad.Push(blob[i]);
        }
        const u32 flips = 1 + round % 4;
        for (u32 f = 0; f < flips; ++f)
        {
            bad[static_cast<usize>(NextRandom(&state) % bad.Size())] = static_cast<byte>(NextRandom(&state));
        }
        ScriptView view;
        if (view.Open(CopyShared(bad), types).Ok())
        {
            ++accepted;
            u64 sum = view.Source().Size() + view.SourceHash();
            for (usize i = 0; i < view.Source().Size(); ++i)
            {
                sum += static_cast<u8>(view.Source().Data()[i]);
            }
            for (usize i = 0; i < view.EntryCount(); ++i)
            {
                sum += view.Entry(i).Length();
            }
            sum += view.BytecodeFor(Text("k1")).Size() + view.Language().Length() + view.LanguageVersion().Length();
            CHECK(sum != 0);
        }
    }
    CHECK(accepted < 3000);
}

TEST_CASE("script source: the text form parses and refuses what is wrong", "[script][asset][scrp]")
{
    ScriptSource s;
    ScriptSourceError e;
    CHECK(ParseScriptSource(
              Text("# a script\nid 00000000000000b2\nlua player.lua\ndep assets.weapon 00000000000000a1\n"), &s, &e)
              .Ok());
    CHECK(s.id == ResourceId::FromValue(0xb2));
    CHECK(s.lua.View() == Text("player.lua"));
    REQUIRE(s.dependencies.Size() == 1);
    CHECK(s.dependencies[0].name.View() == Text("assets.weapon"));
    CHECK(s.dependencies[0].id == ResourceId::FromValue(0xa1));

    CHECK(ParseScriptSource(Text("lua a.lua\n"), &s, &e).Failed());
    CHECK(e.line == 1);
    CHECK(ParseScriptSource(Text("id 00000000000000b2\n"), &s, &e).Failed());
    CHECK(ParseScriptSource(Text("id 00000000000000b2\nlua a.lua\nlua b.lua\n"), &s, &e).Failed());
    CHECK(e.line == 3);
    CHECK(ParseScriptSource(Text("id 00000000000000b2\nlua a.lua\ndep a..b 00000000000000a1\n"), &s, &e).Failed());
    CHECK(ParseScriptSource(Text("id 00000000000000b2\nlua a.lua\ndep a 00000000000000b2\n"), &s, &e).Failed());
    CHECK(ParseScriptSource(Text("id 00000000000000b2\nlua a.lua\ndep a 00000000000000a1\ndep a 00000000000000a2\n"),
                            &s, &e)
              .Failed());
    CHECK(ParseScriptSource(Text("id 00000000000000b2\nlua a.lua\ndep a 00000000000000a1\ndep b 00000000000000a1\n"),
                            &s, &e)
              .Failed());
    CHECK(ParseScriptSource(Text("id 00000000000000b2\nlua a.lua\nwhat\n"), &s, &e).Failed());
}

TEST_CASE("script scan: requires, banned names, comments and strings", "[script][asset][scrp]")
{
    ScriptScan scan;
    ScanScriptText(Text("local a = require(\"x.y\")\nlocal b = require 'z'\n-- require(\"no\")\nlocal s = \"io.open\"\n"
                        "--[==[ os.exit ]==]\nlocal t = [[ debug ]]\nlocal u = t.io + t:os()\n"),
                   &scan);
    REQUIRE(scan.requires_.Size() == 2);
    CHECK(scan.requires_[0].name.View() == Text("x.y"));
    CHECK(scan.requires_[0].line == 1);
    CHECK(scan.requires_[1].name.View() == Text("z"));
    CHECK(scan.requires_[1].line == 2);
    CHECK(scan.banned.Length() == 0);
    CHECK(scan.dynamic_require_line == 0);

    ScanScriptText(Text("local c = { debug = true, load = 1 }\nlocal d = { a = 1, os = 2; io = 3 }\n"), &scan);
    CHECK(scan.banned.Length() == 0);
    ScanScriptText(Text("local x = 1\nlocal f = io.open('a')\n"), &scan);
    CHECK(scan.banned.View() == Text("io"));
    CHECK(scan.banned_line == 2);
    ScanScriptText(Text("local d = string.dump(f)\n"), &scan);
    CHECK(scan.banned.View() == Text("string.dump"));
    ScanScriptText(Text("local n = name\nrequire(n)\n"), &scan);
    CHECK(scan.dynamic_require_line == 2);
    ScanScriptText(Text("require(\"a\" .. b)\n"), &scan);
    CHECK(scan.dynamic_require_line == 1);
    ScanScriptText(Text("local x = require\n"), &scan);
    CHECK(scan.dynamic_require_line == 1);
}

TEST_CASE("script source text: normalization", "[script][asset][scrp]")
{
    String<HeapAllocator> out(HeapAllocator::Default());
    ScriptSourceError e;
    REQUIRE(NormalizeScriptText(Text("\xEF\xBB\xBF"
                                     "a\r\nb\rc\n"),
                                &out, &e)
                .Ok());
    CHECK(out.View() == Text("a\nb\nc\n"));
    CHECK(NormalizeScriptText(StringView("a\0b", 3), &out, &e).Failed());
    CHECK(NormalizeScriptText(Text("a\xC3"), &out, &e).Failed());
    CHECK(NormalizeScriptText(Text("a\xC0\xAF"), &out, &e).Failed());
    CHECK(NormalizeScriptText(Text("\xED\xA0\x80"), &out, &e).Failed());
    CHECK(NormalizeScriptText(Text("caf\xC3\xA9 \xE2\x82\xAC \xF0\x9F\x98\x80"), &out, &e).Ok());
}

TEST_CASE("script minter: a valid script mints and reads back", "[script][asset][scrp]")
{
    ScriptSource s = Source(0xb2, "player.lua");
    AddDep(&s, "assets.weapon", 0xa1);
    const Minted m = Mint(s, "local weapon = require(\"assets.weapon\")\n"
                             "behavior('player', { on_update = function(self, dt) end })\n"
                             "behavior('enemy', { on_create = function(self) end })\n");
    REQUIRE(m.status.Ok());
    const TypeRegistry types;
    ScriptView view;
    REQUIRE(view.Open(CopyShared(m.blob), types).Ok());
    CHECK(view.Id() == ResourceId::FromValue(0xb2));
    REQUIRE(view.EntryCount() == 2);
    CHECK(view.Entry(0) == Text("player"));
    CHECK(view.Entry(1) == Text("enemy"));
    CHECK(view.Language() == Text("luajit"));
    const StringView src(reinterpret_cast<const char*>(view.Source().Data()), view.Source().Size());
    CHECK(src.Find(Text("__require(\"00000000000000a1\")")) < src.Length());
    CHECK(src.Find(Text("require(\"assets.weapon\")")) == src.Length());
}

TEST_CASE("script minter: each class of invalid script fails with a line", "[script][asset][scrp]")
{
    ScriptSource plain = Source(1, "a.lua");
    Minted m = Mint(plain, "local x = = 1\n");
    CHECK(m.status.Failed());
    CHECK(m.error.line == 1);
    m = Mint(plain, "local ok = 1\nlocal f = io.open('a')\n");
    CHECK(m.status.Failed());
    CHECK(m.error.line == 2);
    m = Mint(plain, "\n\nlocal t = package.loaded\n");
    CHECK(m.error.line == 3);
    m = Mint(plain, "local m = require(\"nobody\")\n");
    CHECK(m.status.Failed());
    CHECK(m.error.line == 1);
    m = Mint(plain, "local n = 'x'\nrequire(n)\n");
    CHECK(m.status.Failed());
    CHECK(m.error.line == 2);
    m = Mint(plain, "behavior('a', { on_update = 5 })\n");
    CHECK(m.status.Failed());
    m = Mint(plain, "behavior('a', {})\nbehavior('a', {})\n");
    CHECK(m.status.Failed());
    m = Mint(plain, "behavior('a\\nb', {})\n");
    CHECK(m.status.Failed());
    m = Mint(plain, "error('boom')\n");
    CHECK(m.status.Failed());
    m = Mint(plain, "local x = 1\n\xFF\n");
    CHECK(m.status.Failed());
    // A script that is fine to compile but only runs inside the engine passes when the run is skipped.
    m = Mint(plain, "local dt = time.dt()\n", false, false);
    CHECK(m.status.Ok());

    ScriptSource unused = Source(1, "a.lua");
    AddDep(&unused, "assets.never", 2);
    m = Mint(unused, "local x = 1\n");
    CHECK(m.status.Failed());
}

TEST_CASE("script minter: minting twice gives identical blobs, with and without bytecode", "[script][asset][scrp]")
{
    ScriptSource s = Source(0xb2, "player.lua");
    AddDep(&s, "assets.weapon", 0xa1);
    const char* lua = "local w = require(\"assets.weapon\")\nbehavior('p', { on_update = function(self, dt) end })\n";
    const Minted a = Mint(s, lua);
    const Minted b = Mint(s, lua);
    REQUIRE(a.status.Ok());
    REQUIRE(b.status.Ok());
    REQUIRE(a.blob.Size() == b.blob.Size());
    CHECK(MemCompare(a.blob.Data(), b.blob.Data(), a.blob.Size()) == 0);

    const Minted c = Mint(s, lua, true);
    const Minted d = Mint(s, lua, true);
    REQUIRE(c.status.Ok());
    REQUIRE(d.status.Ok());
    REQUIRE(c.blob.Size() == d.blob.Size());
    CHECK(MemCompare(c.blob.Data(), d.blob.Data(), c.blob.Size()) == 0);
    CHECK(c.blob.Size() > a.blob.Size());

    const TypeRegistry types;
    ScriptView plain;
    ScriptView cached;
    REQUIRE(plain.Open(CopyShared(a.blob), types).Ok());
    REQUIRE(cached.Open(CopyShared(c.blob), types).Ok());
    CHECK(plain.SourceHash() == cached.SourceHash());
    CHECK(plain.BytecodeFor(Text("anything")).Size() == 0);
    // The key names the language, its version and the processor; only that exact key finds the chunk.
    CHECK(cached.BytecodeFor(Text("luajit")).Size() == 0);
}

namespace
{

struct GraphFiles
{
    const char* name[4];
    u64 id[4];
    const char* deps[4];  // the name of one dependency or null
    u64 dep_id[4];
};

bool GraphLoader(void* context, StringView name, ResourceId expected, ScriptSource* out, ScriptSourceError* error)
{
    const GraphFiles* g = static_cast<const GraphFiles*>(context);
    for (u32 i = 0; i < 4; ++i)
    {
        if (g->name[i] != nullptr && name == Text(g->name[i]))
        {
            if (ResourceId::FromValue(g->id[i]) != expected)
            {
                SetScriptError(error, "wrong id");
                return false;
            }
            *out = Source(g->id[i], "x.lua");
            if (g->deps[i] != nullptr)
            {
                AddDep(out, g->deps[i], g->dep_id[i]);
            }
            return true;
        }
    }
    SetScriptError(error, "no such module");
    return false;
}

}  // namespace

TEST_CASE("script graph: order, missing dependencies and cycles are found", "[script][asset][scrp]")
{
    ScriptSourceError e;
    Vector<ResourceId> order;
    // root -> a -> b, root -> b
    GraphFiles ok = {{"a", "b", nullptr, nullptr}, {10, 20, 0, 0}, {"b", nullptr, nullptr, nullptr}, {20, 0, 0, 0}};
    ScriptSource root = Source(1, "r.lua");
    AddDep(&root, "a", 10);
    AddDep(&root, "b", 20);
    REQUIRE(CheckScriptGraph(Text("root"), root, &GraphLoader, &ok, &e, &order).Ok());
    REQUIRE(order.Size() == 3);
    CHECK(order[0] == ResourceId::FromValue(20));  // b first: it is a's dependency
    CHECK(order[1] == ResourceId::FromValue(10));
    CHECK(order[2] == ResourceId::FromValue(1));

    GraphFiles missing = {
        {"a", nullptr, nullptr, nullptr}, {10, 0, 0, 0}, {"gone", nullptr, nullptr, nullptr}, {99, 0, 0, 0}};
    CHECK(CheckScriptGraph(Text("root"), root, &GraphLoader, &missing, &e, &order).Failed());
    CHECK(StringView(e.message.Data(), CStrLength(e.message.Data())).Find(Text("root -> ")) <
          StringView(e.message.Data(), CStrLength(e.message.Data())).Length());

    // root -> a -> b -> a
    GraphFiles cycle = {{"a", "b", nullptr, nullptr}, {10, 20, 0, 0}, {"b", "a", nullptr, nullptr}, {20, 10, 0, 0}};
    ScriptSource top = Source(1, "r.lua");
    AddDep(&top, "a", 10);
    CHECK(CheckScriptGraph(Text("root"), top, &GraphLoader, &cycle, &e, &order).Failed());
    const StringView text(e.message.Data(), CStrLength(e.message.Data()));
    CHECK(text.Find(Text("cycle")) < text.Length());
    CHECK(text.Find(Text("a -> b -> a")) < text.Length());
}

}  // namespace vse::asset
