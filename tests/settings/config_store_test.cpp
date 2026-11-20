#include <catch2/catch_test_macros.hpp>

#include "sync/atomic.h"
#include "sync/thread.h"

#include "catch_engine_types.h"
#include "config_store_conformance.h"
#include "settings/memory_store.h"
#include "store_doubles.h"
#include "test_text.h"

using namespace vse;
using namespace vse::settings;
using namespace settings_test;

namespace
{

ConfigStorePtr NewMemory(const MemoryBackingPtr& backing)
{
    return MakeUnique<MemoryConfigStore>(backing);
}

constexpr DefaultEntry kShipped[] = {
    {"renderer.width", "1280"}, {"renderer.present_mode", "mailbox"}, {"audio.volume", "0.8"}, {"Bad Key", "x"}};

}  // namespace

TEST_CASE("ConfigStore conformance: MemoryConfigStore", "[settings][store][conformance]")
{
    MemoryBackingPtr backing = MakeMemoryBacking();
    auto make = [&] { return NewMemory(backing); };
    RunWritableConformance(make);
    RunDurabilityConformance(make);
    RunCapabilityConformance(make);
}

TEST_CASE("ConfigStore conformance: ReadOnlyConfigStore", "[settings][store][conformance]")
{
    auto make = []
    { return ConfigStorePtr(MakeUnique<ReadOnlyConfigStore>(kShipped, sizeof(kShipped) / sizeof(kShipped[0]))); };
    const SettingDesc width = MakeInt("renderer.width", 0, 0, 100000);
    RunReadOnlyConformance(make, "renderer.width", width, Value::FromInt(1280));
    ConfigStorePtr store = make();
    CHECK(CountKeys(*store) == 3);  // the entry with an invalid key is skipped
    CHECK(CountKeys(*store, "renderer") == 2);
    const SettingDesc mode = MakeEnum("renderer.present_mode", kConformanceModes, 3, 0);
    CHECK(Has(*store, "renderer.present_mode", mode, Value::FromEnum(1)));
    CHECK(store->SetChangeListener(nullptr, nullptr).Is(StoreError::Unsupported));
    CHECK((store->Capabilities() & kCapPreservesOrder) != 0);
}

TEST_CASE("ConfigStore conformance: a store that enumerates in a different order each time",
          "[settings][store][conformance]")
{
    MemoryBackingPtr backing = MakeMemoryBacking();
    auto make = [&] { return ConfigStorePtr(MakeUnique<NoOrderConfigStore>(NewMemory(backing))); };
    RunWritableConformance(make);
    RunDurabilityConformance(make);
    RunCapabilityConformance(make);
    ConfigStorePtr store = make();
    REQUIRE(store->Load().Ok());
    const ConformanceDescs d;
    for (int i = 0; i < 6; ++i)
    {
        const engine_test::Text k = engine_test::Format("n.k%d", i);
        REQUIRE(store->Set(k.View(), Stored(*store, d.i, Value::FromInt(i))).Ok());
    }
    Vector<Key> first, second;
    REQUIRE(store->Enumerate(StringView(), &first).Ok());
    REQUIRE(store->Enumerate(StringView(), &second).Ok());
    bool differs = false;
    for (usize i = 0; i < first.Size(); ++i)
    {
        differs = differs || first[i] != second[i];
    }
    CHECK(differs);  // the double really does reorder, so passing the suite means nothing relied on the order
    CHECK((store->Capabilities() & kCapPreservesOrder) == 0);
}

TEST_CASE("ConfigStore conformance: a store whose calls run on another thread after a delay",
          "[settings][store][conformance]")
{
    MemoryBackingPtr backing = MakeMemoryBacking();
    auto make = [&] { return ConfigStorePtr(MakeUnique<SlowConfigStore>(NewMemory(backing), 20)); };
    RunWritableConformance(make);
    RunCapabilityConformance(make);
    ConfigStorePtr store = make();
    CHECK(store->Load().Ok());
    // (the durability suite runs on the memory store; here the point is that every call completed from the worker
    // thread)
    auto* slow = static_cast<SlowConfigStore*>(store.Get());
    CHECK(slow->last_thread() != ThisThread::Id());
}

TEST_CASE("FailingConfigStore fails exactly the Nth call and the store keeps working after",
          "[settings][store][doubles]")
{
    MemoryBackingPtr backing = MakeMemoryBacking();
    const ConformanceDescs d;
    FailingConfigStore store(NewMemory(backing), 3, StoreError::Io);
    REQUIRE(store.Load().Ok());                                                         // call 1
    REQUIRE(store.Set("f.a", Stored(store, d.i, Value::FromInt(1))).Ok());              // call 2
    CHECK(store.Set("f.b", Stored(store, d.i, Value::FromInt(2))).Is(StoreError::Io));  // call 3 fails
    CHECK(store.Set("f.b", Stored(store, d.i, Value::FromInt(2))).Ok());                // call 4: not sticky
    CHECK(store.failures() == 1);
    CHECK(CountKeys(store) == 2);
    // A failed Commit leaves the durable state as it was and the changes staged: retrying works.
    FailingConfigStore flaky(NewMemory(backing), 2, StoreError::Io);
    REQUIRE(flaky.Load().Ok());                               // call 1 (loads nothing: nothing is durable yet)
    CHECK(flaky.Commit(CommitOptions()).Is(StoreError::Io));  // call 2 fails
    ConfigStorePtr observer = NewMemory(backing);
    REQUIRE(observer->Load().Ok());
    CHECK(CountKeys(*observer) == 0);
    CHECK(flaky.Set("f.c", Stored(flaky, d.i, Value::FromInt(3))).Ok());
    CHECK(flaky.Commit(CommitOptions()).Ok());
    REQUIRE(observer->Load().Ok());
    CHECK(CountKeys(*observer) == 1);
    // A sticky failure keeps failing.
    FailingConfigStore dead(NewMemory(backing), 1, StoreError::PermissionDenied, true);
    CHECK(dead.Load().Is(StoreError::PermissionDenied));
    CHECK(dead.Commit(CommitOptions()).Is(StoreError::PermissionDenied));
    CHECK(dead.failures() == 2);
}

TEST_CASE("Memory stores sharing a backing merge commits and notify each other from another thread",
          "[settings][store][notify]")
{
    MemoryBackingPtr backing = MakeMemoryBacking();
    const ConformanceDescs d;
    MemoryConfigStore a(backing), b(backing);
    struct Seen
    {
        Atomic<int> calls{0};
        ThreadId thread;
    } seen_by_b, seen_by_a;
    auto listener = [](void* user)
    {
        Seen* s = static_cast<Seen*>(user);
        s->thread = ThisThread::Id();
        s->calls.FetchAdd(1);
    };
    REQUIRE(b.SetChangeListener(listener, &seen_by_b).Ok());
    REQUIRE(a.SetChangeListener(listener, &seen_by_a).Ok());
    Atomic<int> writer_failures{
        0};  // Catch2 assertions stay on the main thread: a REQUIRE that throws in a worker thread would terminate
    Thread writer(
        [&]
        {
            StoredValue sv;
            if (ToStored(d.i, Value::FromInt(1), true, &sv) != StoreError::None)
            {
                writer_failures++;
            }
            if (a.Set("s.x", sv).Failed())
            {
                writer_failures++;
            }
            if (a.Commit(CommitOptions()).Failed())
            {
                writer_failures++;
            }
        });
    writer.Join();
    CHECK(writer_failures.Load() == 0);
    CHECK(seen_by_b.calls.Load() == 1);
    CHECK(seen_by_b.thread != ThisThread::Id());  // called on the committing (writer) thread, not this one
    CHECK(seen_by_a.calls.Load() == 0);           // the committer is not told about its own commit
    // b has not reloaded: it sees nothing until it does, then sees a's change; its own commit merges instead of
    // replacing.
    CHECK(CountKeys(b) == 0);
    REQUIRE(b.Load().Ok());
    CHECK(Has(b, "s.x", d.i, Value::FromInt(1)));
    REQUIRE(b.Set("s.y", Stored(b, d.i, Value::FromInt(2))).Ok());
    REQUIRE(a.Set("s.z", Stored(a, d.i, Value::FromInt(3))).Ok());
    REQUIRE(b.Commit(CommitOptions()).Ok());
    REQUIRE(a.Commit(CommitOptions()).Ok());
    MemoryConfigStore c(backing);
    CHECK(CountKeys(c) == 3);
    CHECK(seen_by_b.calls.Load() == 2);
    // Removing the listener stops the calls; destroying a store removes it from the backing (no call after the
    // destructor).
    REQUIRE(b.SetChangeListener(nullptr, nullptr).Ok());
    REQUIRE(a.Set("s.w", Stored(a, d.i, Value::FromInt(4))).Ok());
    REQUIRE(a.Commit(CommitOptions()).Ok());
    CHECK(seen_by_b.calls.Load() == 2);
}

TEST_CASE("Listeners and commits from many threads do not race", "[settings][store][notify][threads]")
{
    MemoryBackingPtr backing = MakeMemoryBacking();
    const ConformanceDescs d;
    Atomic<int> total{0};
    auto listener = [](void* user) { static_cast<Atomic<int>*>(user)->FetchAdd(1); };
    MemoryConfigStore watcher(backing);
    REQUIRE(watcher.SetChangeListener(listener, &total).Ok());
    Atomic<int> failures{0};
    Vector<Thread> writers;
    writers.Resize(4);
    for (usize t = 0; t < writers.Size(); ++t)
    {
        writers[t] = Thread(
            [&, t]
            {
                for (int i = 0; i < 50; ++i)
                {
                    MemoryConfigStore w(backing);
                    const engine_test::Text key = engine_test::Format("t%d.k%d", static_cast<int>(t), i);
                    StoredValue sv;
                    if (ToStored(d.i, Value::FromInt(i), true, &sv) != StoreError::None ||
                        w.Set(key.View(), sv).Failed() || w.Commit(CommitOptions()).Failed())
                    {
                        failures++;
                    }
                }
            });
    }
    for (usize t = 0; t < writers.Size(); ++t)
    {
        writers[t].Join();
    }
    CHECK(failures.Load() == 0);
    CHECK(total.Load() == 200);
    MemoryConfigStore reader(backing);
    CHECK(CountKeys(reader) == 200);
}

TEST_CASE("ToStored and FromStored convert between typed values and text for every type", "[settings][store]")
{
    const ConformanceDescs d;
    const SettingDesc* descs[] = {&d.b, &d.i, &d.f, &d.e, &d.t};
    const Value values[] = {Value::FromBool(true), Value::FromInt(-5), Value::FromFloat(2.5), Value::FromEnum(1),
                            Value::FromText("x y")};
    for (int k = 0; k < 5; ++k)
    {
        for (bool typed : {true, false})
        {
            StoredValue sv;
            REQUIRE(ToStored(*descs[k], values[k], typed, &sv) == StoreError::None);
            CHECK(sv.typed == typed);
            if (!typed)
            {
                CHECK(sv.value.GetType() == Type::Text);
            }
            Value back;
            CHECK(FromStored(*descs[k], sv, &back) == ValueError::None);
            CHECK(back == values[k]);
        }
    }
    // A typed value of the wrong type, and text that is not a value of the type.
    StoredValue wrong;
    wrong.typed = true;
    wrong.value = Value::FromBool(true);
    Value out = Value::FromInt(3);
    CHECK(FromStored(d.i, wrong, &out) == ValueError::BadNumber);
    CHECK(out.AsInt() == 3);
    StoredValue junk;
    junk.value = Value::FromText("not a number");
    CHECK(FromStored(d.i, junk, &out) == ValueError::BadNumber);
    // Text too long for a stored value once quoted and escaped.
    StoredValue too_long;
    const engine_test::Text quotes = engine_test::Repeated(400, '"');
    CHECK(ToStored(d.t, Value::FromText(quotes.View()), false, &too_long) == StoreError::TooLong);
    CHECK(StringView(StoreErrorName(StoreError::PermissionDenied)) == "permission_denied");
}

TEST_CASE("A typed stored value is checked against the declaration like text is", "[settings][store]")
{
    const ConformanceDescs d;
    StoredValue huge;
    huge.typed = true;
    huge.value = Value::FromInt(99999999);
    Value out;
    CHECK(FromStored(d.i, huge, &out) == ValueError::Clamped);
    CHECK(out.AsInt() == 1000000);
    const SettingDesc strict = MakeInt("c.strict", 0, 0, 10, RangePolicy::Reject);
    CHECK(FromStored(strict, huge, &out) == ValueError::OutOfRange);
    StoredValue bad_enum;
    bad_enum.typed = true;
    bad_enum.value = Value::FromEnum(7);
    CHECK(FromStored(d.e, bad_enum, &out) == ValueError::NotInEnum);
    StoredValue huge_float;
    huge_float.typed = true;
    huge_float.value = Value::FromFloat(1e9);
    CHECK(FromStored(d.f, huge_float, &out) == ValueError::Clamped);
    CHECK(out.AsFloat() == 1e6);
}

TEST_CASE("Repeated changes to one key stage one change", "[settings][store]")
{
    MemoryBackingPtr backing = MakeMemoryBacking();
    const ConformanceDescs d;
    MemoryConfigStore store(backing);
    for (int i = 0; i < 5000; ++i)
    {
        StoredValue sv;
        REQUIRE(ToStored(d.i, Value::FromInt(i), true, &sv) == StoreError::None);
        REQUIRE(store.Set("slider.value", sv).Ok());
    }
    REQUIRE(store.Remove("slider.value").Ok());
    StoredValue sv;
    REQUIRE(ToStored(d.i, Value::FromInt(1), true, &sv) == StoreError::None);
    REQUIRE(store.Set("slider.other", sv).Ok());
    const Status s = store.Commit(CommitOptions());
    REQUIRE(s.Ok());
    CHECK(s.total == 2);  // the removal of slider.value and the set of slider.other
    CHECK(backing->Size() == 1);
}

TEST_CASE("ReadOnlyConfigStore skips null text and invalid keys, and the first entry for a key wins",
          "[settings][store]")
{
    const DefaultEntry entries[] = {{"a.x", "1"}, {"a.y", nullptr}, {nullptr, "2"},
                                    {"a.x", "3"}, {"Bad", "4"},     {"b.z", "5"}};
    ReadOnlyConfigStore store(entries, 6);
    const SettingDesc i = MakeInt("a.x", 0, 0, 100);
    CHECK(Has(store, "a.x", i, Value::FromInt(1)));
    StoredValue sv;
    CHECK(store.Get("a.y", &sv).Is(StoreError::NotFound));
    CHECK(CountKeys(store) == 2);  // a.x once, b.z
}

TEST_CASE("A commit that lands only some changes says how many (the contract of a store without an atomic commit)",
          "[settings][store][doubles]")
{
    MemoryBackingPtr backing = MakeMemoryBacking();
    const ConformanceDescs d;
    PartialCommitConfigStore store(NewMemory(backing), 2);
    REQUIRE(store.Load().Ok());
    CHECK((store.Capabilities() & kCapAtomicCommit) == 0);
    for (const char* k : {"p.a", "p.b", "p.c", "p.d"})
    {
        REQUIRE(store.Set(k, Stored(store, d.i, Value::FromInt(1))).Ok());
    }
    const Status first = store.Commit(CommitOptions());
    CHECK(first.Is(StoreError::Io));
    CHECK(first.done == 2);
    CHECK(first.total == 4);
    ConfigStorePtr observer = NewMemory(backing);
    REQUIRE(observer->Load().Ok());
    CHECK(CountKeys(*observer) == 2);                    // exactly the first two landed
    const Status retry = store.Commit(CommitOptions());  // the rest are still staged
    CHECK(retry.Ok());
    CHECK(retry.done == 2);
    REQUIRE(observer->Load().Ok());
    CHECK(CountKeys(*observer) == 4);
}

TEST_CASE("ConfigStore conformance: a registry-shaped store (typed, per-key, unordered, notifying)",
          "[settings][store][conformance][registry]")
{
    RegistryHive hive;
    auto make = [&] { return ConfigStorePtr(MakeUnique<RegistryShapedStore>(&hive)); };
    RunWritableConformance(make);
    RunDurabilityConformance(make);
    RunCapabilityConformance(make);
    ConfigStorePtr store = make();
    const u32 caps = store->Capabilities();
    CHECK((caps & kCapPerKeyWrites) != 0);
    CHECK((caps & kCapAtomicCommit) == 0);
    CHECK((caps & kCapPreservesOrder) == 0);
}

TEST_CASE("A registry-shaped store notifies other stores from another thread, never the writer",
          "[settings][store][notify][registry]")
{
    RegistryHive hive;
    const ConformanceDescs d;
    RegistryShapedStore a(&hive), b(&hive);
    struct Seen
    {
        Atomic<int> calls{0};
        Atomic<bool> other_thread{false};
        ThreadId main;
    } seen_a, seen_b;
    seen_a.main = seen_b.main = ThisThread::Id();
    auto listener = [](void* user)
    {
        Seen* s = static_cast<Seen*>(user);
        s->other_thread = ThisThread::Id() != s->main;
        s->calls.FetchAdd(1);
    };
    REQUIRE(a.SetChangeListener(listener, &seen_a).Ok());
    REQUIRE(b.SetChangeListener(listener, &seen_b).Ok());
    REQUIRE(a.Set("n.x", Stored(a, d.i, Value::FromInt(1))).Ok());
    for (int i = 0; i < 200 && seen_b.calls.Load() == 0; ++i)
    {
        ThisThread::SleepForMilliseconds(5);
    }
    CHECK(seen_b.calls.Load() == 1);
    CHECK(seen_b.other_thread.Load());
    CHECK(seen_a.calls.Load() == 0);
    REQUIRE(b.SetChangeListener(nullptr, nullptr).Ok());
    REQUIRE(a.Set("n.y", Stored(a, d.i, Value::FromInt(2))).Ok());
    ThisThread::SleepForMilliseconds(50);
    CHECK(seen_b.calls.Load() == 1);  // removed
}
