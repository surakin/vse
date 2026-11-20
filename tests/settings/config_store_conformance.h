#pragma once

// The checks every ConfigStore must pass. A store test calls RunStoreConformance with a factory: a callable returning a
// new ConfigStorePtr over the same durable backing each time it is called (a second instance models a restart). The
// suite reads Capabilities() and checks what the store claims, so a store that overstates what it does fails here.

#include <catch2/catch_test_macros.hpp>

#include "catch_engine_types.h"
#include "settings/config_store.h"
#include "test_text.h"

namespace settings_test
{

using namespace vse;
using namespace vse::settings;

inline const char* const kConformanceModes[] = {"fifo", "mailbox", "immediate"};

/// A float, int, bool, enum and text declaration used by the suite.
struct ConformanceDescs
{
    SettingDesc b = MakeBool("c.flag", false);
    SettingDesc i = MakeInt("c.count", 0, -1000000, 1000000);
    SettingDesc f = MakeFloat("c.scale", 1.0, -1e6, 1e6);
    SettingDesc e = MakeEnum("c.mode", kConformanceModes, 3, 0);
    SettingDesc t = MakeText("c.name", "");
};

inline StoredValue Stored(const ConfigStore& store, const SettingDesc& desc, const Value& v)
{
    StoredValue out;
    REQUIRE(ToStored(desc, v, (store.Capabilities() & kCapTypedValues) != 0, &out) == StoreError::None);
    return out;
}

inline bool Has(ConfigStore& store, StringView key, const SettingDesc& desc, const Value& expected)
{
    StoredValue sv;
    if (store.Get(key, &sv).Failed())
    {
        return false;
    }
    Value got;
    return FromStored(desc, sv, &got) == ValueError::None && got == expected;
}

inline usize CountKeys(ConfigStore& store, StringView prefix = StringView())
{
    Vector<Key> keys;
    REQUIRE(store.Enumerate(prefix, &keys).Ok());
    return keys.Size();
}

inline void ClearStore(ConfigStore& store)
{
    Vector<Key> keys;
    REQUIRE(store.Enumerate(StringView(), &keys).Ok());
    for (usize k = 0; k < keys.Size(); ++k)
    {
        REQUIRE(store.Remove(keys[k].View()).Ok());
    }
    REQUIRE(store.Commit(CommitOptions()).Ok());
}

template <typename Factory> void RunWritableConformance(Factory&& make)
{
    const ConformanceDescs d;
    ConfigStorePtr store = make();
    REQUIRE(store->Load().Ok());
    ClearStore(*store);
    const u32 caps = store->Capabilities();
    CHECK((caps & kCapWritable) != 0);
    char description[64];
    store->Describe(description, sizeof(description));
    CHECK(description[0] != '\0');

    // Every type sets and gets back equal; a second set overwrites.
    REQUIRE(store->Set("c.flag", Stored(*store, d.b, Value::FromBool(true))).Ok());
    REQUIRE(store->Set("c.count", Stored(*store, d.i, Value::FromInt(-42))).Ok());
    REQUIRE(store->Set("c.scale", Stored(*store, d.f, Value::FromFloat(0.1))).Ok());
    REQUIRE(store->Set("c.mode", Stored(*store, d.e, Value::FromEnum(2))).Ok());
    REQUIRE(store->Set("c.name", Stored(*store, d.t, Value::FromText("a \"quoted\"\nname"))).Ok());
    CHECK(Has(*store, "c.flag", d.b, Value::FromBool(true)));
    CHECK(Has(*store, "c.count", d.i, Value::FromInt(-42)));
    CHECK(Has(*store, "c.scale", d.f, Value::FromFloat(0.1)));
    CHECK(Has(*store, "c.mode", d.e, Value::FromEnum(2)));
    CHECK(Has(*store, "c.name", d.t, Value::FromText("a \"quoted\"\nname")));
    REQUIRE(store->Set("c.count", Stored(*store, d.i, Value::FromInt(7))).Ok());
    CHECK(Has(*store, "c.count", d.i, Value::FromInt(7)));
    CHECK(CountKeys(*store) == 5);

    // Remove, and removing what is not there.
    REQUIRE(store->Remove("c.flag").Ok());
    StoredValue sv;
    CHECK(store->Get("c.flag", &sv).Is(StoreError::NotFound));
    CHECK(store->Remove("c.flag").Is(StoreError::NotFound));
    CHECK(store->Get("no.such", &sv).Is(StoreError::NotFound));
    CHECK(CountKeys(*store) == 4);

    // Invalid keys are refused everywhere.
    for (const char* bad : {"", "Bad", "a..b", "a b", "a.b.c.d"})
    {
        INFO(bad);
        CHECK(store->Get(bad, &sv).Is(StoreError::Invalid));
        CHECK(store->Set(bad, Stored(*store, d.i, Value::FromInt(1))).Is(StoreError::Invalid));
        CHECK(store->Remove(bad).Is(StoreError::Invalid));
    }
    Vector<Key> keys;
    CHECK(store->Enumerate("Bad Prefix", &keys).Is(StoreError::Invalid));

    // Prefix enumeration: the keys equal to the prefix or below it, not keys that merely share its letters.
    REQUIRE(store->Set("renderer.width", Stored(*store, d.i, Value::FromInt(1))).Ok());
    REQUIRE(store->Set("renderer.height", Stored(*store, d.i, Value::FromInt(2))).Ok());
    REQUIRE(store->Set("renderers.other", Stored(*store, d.i, Value::FromInt(3))).Ok());
    REQUIRE(store->Set("renderer.sub.deep", Stored(*store, d.i, Value::FromInt(4))).Ok());
    CHECK(CountKeys(*store, "renderer") == 3);
    CHECK(CountKeys(*store, "renderer.sub") == 1);
    CHECK(CountKeys(*store, "renderers") == 1);
    CHECK(CountKeys(*store, "nothing") == 0);

    // Every legal key character, the longest segment and the longest key.
    const char* const odd[] = {"a", "a.b", "a.b.c", "z9_.x", "x_y_z9.q_1"};
    for (const char* k : odd)
    {
        REQUIRE(store->Set(k, Stored(*store, d.i, Value::FromInt(5))).Ok());
        CHECK(Has(*store, k, d.i, Value::FromInt(5)));
    }
    const engine_test::Text longest = engine_test::Concat(
        engine_test::Repeated(31, 'a').View(), ".",
        engine_test::Concat(engine_test::Repeated(31, 'b').View(), ".", engine_test::Repeated(31, 'c').View()).View());
    REQUIRE(store->Set(longest.View(), Stored(*store, d.i, Value::FromInt(9))).Ok());
    CHECK(Has(*store, longest.View(), d.i, Value::FromInt(9)));

    // Committing nothing succeeds.
    ConfigStorePtr idle = make();
    REQUIRE(idle->Load().Ok());
    CHECK(idle->Commit(CommitOptions()).Ok());
}

template <typename Factory> void RunDurabilityConformance(Factory&& make)
{
    const ConformanceDescs d;
    ConfigStorePtr store = make();
    REQUIRE(store->Load().Ok());
    ClearStore(*store);
    const u32 caps = store->Capabilities();
    REQUIRE(store->Set("d.one", Stored(*store, d.i, Value::FromInt(1))).Ok());
    REQUIRE(store->Set("d.two", Stored(*store, d.i, Value::FromInt(2))).Ok());
    // A second instance (a restart) sees committed content only.
    {
        ConfigStorePtr other = make();
        REQUIRE(other->Load().Ok());
        if ((caps & kCapPerKeyWrites) != 0)
        {
            CHECK(CountKeys(*other) == 2);  // durable at once
        }
        else
        {
            CHECK(CountKeys(*other) == 0);  // still staged
        }
    }
    const Status commit = store->Commit(CommitOptions());
    REQUIRE(commit.Ok());
    {
        ConfigStorePtr other = make();
        REQUIRE(other->Load().Ok());
        CHECK(Has(*other, "d.one", d.i, Value::FromInt(1)));
        CHECK(Has(*other, "d.two", d.i, Value::FromInt(2)));
        CHECK(CountKeys(*other) == 2);
    }
    // Staged changes are visible to their own store, and Load discards them (unless every write is already durable).
    REQUIRE(store->Set("d.three", Stored(*store, d.i, Value::FromInt(3))).Ok());
    REQUIRE(store->Remove("d.one").Ok());
    CHECK(Has(*store, "d.three", d.i, Value::FromInt(3)));
    StoredValue sv;
    CHECK(store->Get("d.one", &sv).Is(StoreError::NotFound));
    if ((caps & kCapPerKeyWrites) == 0)
    {
        REQUIRE(store->Load().Ok());
        CHECK(Has(*store, "d.one", d.i, Value::FromInt(1)));
        CHECK(store->Get("d.three", &sv).Is(StoreError::NotFound));
    }
    // A committed removal is gone for the next instance.
    REQUIRE(store->Remove("d.two").Ok());
    REQUIRE(store->Commit(CommitOptions()).Ok());
    ConfigStorePtr other = make();
    REQUIRE(other->Load().Ok());
    CHECK(other->Get("d.two", &sv).Is(StoreError::NotFound));
    // A thousand keys survive a commit and a reload.
    ClearStore(*store);
    for (int n = 0; n < 1000; ++n)
    {
        const engine_test::Text key = engine_test::Format("many.k%d", n);
        REQUIRE(store->Set(key.View(), Stored(*store, d.i, Value::FromInt(n))).Ok());
    }
    REQUIRE(store->Commit(CommitOptions()).Ok());
    ConfigStorePtr reloaded = make();
    REQUIRE(reloaded->Load().Ok());
    CHECK(CountKeys(*reloaded, "many") == 1000);
    CHECK(Has(*reloaded, "many.k999", d.i, Value::FromInt(999)));
}

template <typename Factory> void RunCapabilityConformance(Factory&& make)
{
    const ConformanceDescs d;
    ConfigStorePtr store = make();
    REQUIRE(store->Load().Ok());
    ClearStore(*store);
    const u32 caps = store->Capabilities();
    if ((caps & kCapPreservesOrder) != 0)
    {
        const char* const order[] = {"z.last", "a.first", "m.middle", "b.second"};
        for (const char* k : order)
        {
            REQUIRE(store->Set(k, Stored(*store, d.i, Value::FromInt(1))).Ok());
        }
        Vector<Key> keys;
        REQUIRE(store->Enumerate(StringView(), &keys).Ok());
        REQUIRE(keys.Size() == 4);
        for (usize i = 0; i < 4; ++i)
        {
            CHECK(keys[i].View() == StringView(order[i]));  // the order they were first stored in
        }
        REQUIRE(store->Set("z.last", Stored(*store, d.i, Value::FromInt(2))).Ok());  // an overwrite keeps its place
        Vector<Key> again;
        REQUIRE(store->Enumerate(StringView(), &again).Ok());
        CHECK(again[0].View() == "z.last");
    }
    if ((caps & kCapNotifies) == 0)
    {
        CHECK(store->SetChangeListener(nullptr, nullptr).Is(StoreError::Unsupported));
    }
    else
    {
        CHECK(store->SetChangeListener(nullptr, nullptr).Ok());
    }
    if ((caps & kCapTypedValues) != 0)
    {
        REQUIRE(store->Set("t.n", Stored(*store, d.i, Value::FromInt(3))).Ok());
        StoredValue sv;
        REQUIRE(store->Get("t.n", &sv).Ok());
        CHECK(sv.typed);
        CHECK(sv.value.GetType() == Type::Int);
    }
    else
    {
        REQUIRE(store->Set("t.n", Stored(*store, d.i, Value::FromInt(3))).Ok());
        StoredValue sv;
        REQUIRE(store->Get("t.n", &sv).Ok());
        CHECK_FALSE(sv.typed);
        CHECK(sv.value.GetType() == Type::Text);
    }
}

/// For a store that cannot be written: reads work, every write says ReadOnly, nothing changes.
template <typename Factory>
void RunReadOnlyConformance(Factory&& make, StringView known_key, const SettingDesc& desc, const Value& expected)
{
    ConfigStorePtr store = make();
    const u32 caps = store->Capabilities();
    CHECK((caps & kCapWritable) == 0);
    REQUIRE(store->Load().Ok());
    CHECK(Has(*store, known_key, desc, expected));
    const usize before = CountKeys(*store);
    CHECK(before > 0);
    CHECK(store->Set(known_key, Stored(*store, desc, expected)).Is(StoreError::ReadOnly));
    CHECK(store->Remove(known_key).Is(StoreError::ReadOnly));
    CHECK(store->Commit(CommitOptions()).Is(StoreError::ReadOnly));
    CHECK(CountKeys(*store) == before);
    CHECK(Has(*store, known_key, desc, expected));
    StoredValue sv;
    CHECK(store->Get("no.such", &sv).Is(StoreError::NotFound));
    CHECK(store->Get("Bad", &sv).Is(StoreError::Invalid));
}

}  // namespace settings_test
