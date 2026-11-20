#pragma once

// The store conformance suite (Phase 19 Day 259): the same cases against every record store. A fixture type provides
//   static constexpr bool kInjectsFailure;  (true when the store can fail on demand)
//   static const char* Name();
//   Store& Db();                            (the store under test)
//   void FailAfter(u32 n);                  (only used when kInjectsFailure)
//   bool Corrupt(EntityKey key);            (damage a record; only used when kCanCorrupt)
//   static constexpr bool kCanCorrupt;
//   static constexpr bool kReadersSeeCommitted;  (true when a reader on another thread sees the committed state while a
//   batch is open; false: it is Busy)
// and instantiates the cases with STORE_CONFORMANCE_CASES(Fixture).

#include <catch2/catch_test_macros.hpp>

#include "storage/identity.h"
#include "storage/store.h"
#include "sync/thread.h"

namespace vse::storage::conformance
{

inline EntityRecord Rec(u64 key, u8 fill, usize size = 8)
{
    Vector<byte> bytes;
    for (usize i = 0; i < size; ++i)
    {
        bytes.Push(static_cast<byte>(fill + i));
    }
    return EntityRecord::Make(EntityKey{0, key}, ByteSpan(bytes.Data(), bytes.Size()));
}

class Collect final : public RecordVisitor
{
public:
    bool Visit(const EntityRecord& r) override
    {
        keys.Push(r.key);
        return stop_after == 0 || keys.Size() < stop_after;
    }
    Vector<EntityKey> keys;
    usize stop_after = 0;
};

inline usize CountOf(const Store& s)
{
    usize n = ~usize(0);
    REQUIRE(s.Count(&n).Ok());
    return n;
}

// The whole state as one comparable string: keys, hashes and metadata.
inline u64 Fingerprint(const Store& s)
{
    Collect all;
    REQUIRE(s.Scan(KeyRange(), all).Ok());
    u64 h = 1469598103934665603ull;
    for (usize i = 0; i < all.keys.Size(); ++i)
    {
        EntityRecord r;
        REQUIRE(s.Get(all.keys[i], &r).Ok());
        h = (h ^ all.keys[i].low) * 1099511628211ull;
        h = (h ^ r.hash) * 1099511628211ull;
    }
    return h ^ all.keys.Size();
}

}  // namespace vse::storage::conformance

#define STORE_CONFORMANCE_CASES(Fixture)                                                                               \
    TEST_CASE("Store conformance: put, get, replace and remove round trip [" #Fixture "]", "[storage][store]")         \
    {                                                                                                                  \
        Fixture f;                                                                                                     \
        Store& s = f.Db();                                                                                             \
        using namespace conformance;                                                                                   \
        CHECK(CountOf(s) == 0);                                                                                        \
        REQUIRE(s.Put(Rec(5, 1)).Ok());                                                                                \
        REQUIRE(s.Put(Rec(2, 9, 0)).Ok()); /* an empty blob is a record */                                             \
        EntityRecord out;                                                                                              \
        REQUIRE(s.Get(EntityKey{0, 5}, &out).Ok());                                                                    \
        CHECK(out.blob.Size() == 8);                                                                                   \
        CHECK(out.HashMatches());                                                                                      \
        REQUIRE(s.Put(Rec(5, 77, 3)).Ok()); /* replaces */                                                             \
        REQUIRE(s.Get(EntityKey{0, 5}, &out).Ok());                                                                    \
        CHECK(out.blob.Size() == 3);                                                                                   \
        CHECK(out.blob[0] == static_cast<byte>(77));                                                                   \
        CHECK(CountOf(s) == 2);                                                                                        \
        CHECK(s.Get(EntityKey{0, 6}, &out).Is(ErrorCode::NotFound));                                                   \
        CHECK(s.Remove(EntityKey{0, 6}).Is(ErrorCode::NotFound));                                                      \
        REQUIRE(s.Remove(EntityKey{0, 5}).Ok());                                                                       \
        CHECK(s.Get(EntityKey{0, 5}, &out).Is(ErrorCode::NotFound));                                                   \
        CHECK(CountOf(s) == 1);                                                                                        \
        CHECK(s.Put(EntityRecord::Make(EntityKey{}, ByteSpan())).Is(ErrorCode::Invalid)); /* the null key */           \
        EntityRecord bad = Rec(9, 1);                                                                                  \
        bad.hash ^= 1;                                                                                                 \
        CHECK(s.Put(bad).Is(ErrorCode::Invalid)); /* a hash that is not the blob's */                                  \
    }                                                                                                                  \
    TEST_CASE("Store conformance: scan is in key order, in ranges, and stops when told [" #Fixture "]",                \
              "[storage][store]")                                                                                      \
    {                                                                                                                  \
        Fixture f;                                                                                                     \
        Store& s = f.Db();                                                                                             \
        using namespace conformance;                                                                                   \
        const u64 order[] = {50, 10, 40, 20, 30};                                                                      \
        for (u64 k : order)                                                                                            \
        {                                                                                                              \
            REQUIRE(s.Put(Rec(k, 1)).Ok());                                                                            \
        }                                                                                                              \
        REQUIRE(s.Put(EntityRecord::Make(EntityKey{3, 1}, ByteSpan())).Ok()); /* a larger high half sorts after */     \
        Collect all;                                                                                                   \
        REQUIRE(s.Scan(KeyRange(), all).Ok());                                                                         \
        REQUIRE(all.keys.Size() == 6);                                                                                 \
        for (usize i = 1; i < all.keys.Size(); ++i)                                                                    \
        {                                                                                                              \
            CHECK(all.keys[i - 1] < all.keys[i]);                                                                      \
        }                                                                                                              \
        Collect part;                                                                                                  \
        KeyRange range;                                                                                                \
        range.first = EntityKey{0, 20};                                                                                \
        range.end = EntityKey{0, 40};                                                                                  \
        range.unbounded = false;                                                                                       \
        REQUIRE(s.Scan(range, part).Ok());                                                                             \
        REQUIRE(part.keys.Size() == 2); /* [20, 40) */                                                                 \
        CHECK(part.keys[0].low == 20);                                                                                 \
        CHECK(part.keys[1].low == 30);                                                                                 \
        Collect two;                                                                                                   \
        two.stop_after = 2;                                                                                            \
        REQUIRE(s.Scan(KeyRange(), two).Ok());                                                                         \
        CHECK(two.keys.Size() == 2);                                                                                   \
        Vector<EntityKey> listed;                                                                                      \
        REQUIRE(ListKeys(s, KeyRange(), 4, &listed).Ok());                                                             \
        CHECK(listed.Size() == 4);                                                                                     \
        CHECK(listed[0].low == 10);                                                                                    \
    }                                                                                                                  \
    TEST_CASE("Store conformance: a committed batch shows all of it, a rolled back one none [" #Fixture "]",           \
              "[storage][store]")                                                                                      \
    {                                                                                                                  \
        Fixture f;                                                                                                     \
        Store& s = f.Db();                                                                                             \
        using namespace conformance;                                                                                   \
        REQUIRE(s.Put(Rec(1, 1)).Ok());                                                                                \
        const u64 before = Fingerprint(s);                                                                             \
        REQUIRE(s.Begin().Ok());                                                                                       \
        CHECK(s.Begin().Is(ErrorCode::Busy));                                                                          \
        REQUIRE(s.Put(Rec(2, 2)).Ok());                                                                                \
        REQUIRE(s.Remove(EntityKey{0, 1}).Ok());                                                                       \
        REQUIRE(s.SetMetadata("k", "v").Ok());                                                                         \
        CHECK(CountOf(s) == 1); /* the owner sees its own changes */                                                   \
        REQUIRE(s.Rollback().Ok());                                                                                    \
        CHECK(Fingerprint(s) == before);                                                                               \
        String<HeapAllocator> value(HeapAllocator::Default());                                                         \
        CHECK(s.GetMetadata("k", &value).Is(ErrorCode::NotFound));                                                     \
        REQUIRE(s.Begin().Ok());                                                                                       \
        REQUIRE(s.Put(Rec(2, 2)).Ok());                                                                                \
        REQUIRE(s.Remove(EntityKey{0, 1}).Ok());                                                                       \
        REQUIRE(s.SetMetadata("k", "v").Ok());                                                                         \
        REQUIRE(s.Commit().Ok());                                                                                      \
        CHECK(CountOf(s) == 1);                                                                                        \
        EntityRecord out;                                                                                              \
        CHECK(s.Get(EntityKey{0, 1}, &out).Is(ErrorCode::NotFound));                                                   \
        REQUIRE(s.Get(EntityKey{0, 2}, &out).Ok());                                                                    \
        REQUIRE(s.GetMetadata("k", &value).Ok());                                                                      \
        CHECK(value.View() == StringView("v"));                                                                        \
        CHECK(s.Commit().Is(ErrorCode::Invalid)); /* none open */                                                      \
        CHECK(s.Rollback().Is(ErrorCode::Invalid));                                                                    \
    }                                                                                                                  \
    TEST_CASE("Store conformance: metadata persists and replaces [" #Fixture "]", "[storage][store]")                  \
    {                                                                                                                  \
        Fixture f;                                                                                                     \
        Store& s = f.Db();                                                                                             \
        String<HeapAllocator> value(HeapAllocator::Default());                                                         \
        CHECK(s.GetMetadata(metadata::kFormatVersion, &value).Is(ErrorCode::NotFound));                                \
        REQUIRE(s.SetMetadata(metadata::kFormatVersion, "1").Ok());                                                    \
        REQUIRE(s.SetMetadata(metadata::kLastSaveLabel, "autosave 3").Ok());                                           \
        REQUIRE(s.SetMetadata(metadata::kFormatVersion, "2").Ok());                                                    \
        REQUIRE(s.GetMetadata(metadata::kFormatVersion, &value).Ok());                                                 \
        CHECK(value.View() == StringView("2"));                                                                        \
        REQUIRE(s.GetMetadata(metadata::kLastSaveLabel, &value).Ok());                                                 \
        CHECK(value.View() == StringView("autosave 3"));                                                               \
        CHECK(s.SetMetadata("", "x").Is(ErrorCode::Invalid));                                                          \
        REQUIRE(s.SetMetadata("empty", "").Ok());                                                                      \
        REQUIRE(s.GetMetadata("empty", &value).Ok());                                                                  \
        CHECK(value.Length() == 0);                                                                                    \
    }                                                                                                                  \
    TEST_CASE("Store conformance: a batch commits all or nothing, with a failure at every operation [" #Fixture "]",   \
              "[storage][store]")                                                                                      \
    {                                                                                                                  \
        if constexpr (Fixture::kInjectsFailure)                                                                        \
        {                                                                                                              \
            using namespace conformance;                                                                               \
            /* The batch below does Begin, 6 mutations and Commit: 8 operations. Fail each one in turn. */             \
            for (u32 fail = 1; fail <= 8; ++fail)                                                                      \
            {                                                                                                          \
                Fixture f;                                                                                             \
                Store& s = f.Db();                                                                                     \
                for (u64 k = 1; k <= 4; ++k)                                                                           \
                {                                                                                                      \
                    REQUIRE(s.Put(Rec(k, 1)).Ok());                                                                    \
                }                                                                                                      \
                REQUIRE(s.SetMetadata("m", "old").Ok());                                                               \
                const u64 before = Fingerprint(s);                                                                     \
                f.FailAfter(fail);                                                                                     \
                bool failed = false;                                                                                   \
                Status st = s.Begin();                                                                                 \
                failed = failed || st.Failed();                                                                        \
                Status first = st;                                                                                     \
                if (st.Ok())                                                                                           \
                {                                                                                                      \
                    const Status ops[] = {s.Put(Rec(10, 3)), s.Put(Rec(2, 3)),          s.Remove(EntityKey{0, 1}),     \
                                          s.Put(Rec(11, 3)), s.Remove(EntityKey{0, 3}), s.SetMetadata("m", "new")};    \
                    for (const Status& o : ops)                                                                        \
                    {                                                                                                  \
                        if (o.Failed() && !failed)                                                                     \
                        {                                                                                              \
                            first = o;                                                                                 \
                        }                                                                                              \
                        failed = failed || o.Failed();                                                                 \
                    }                                                                                                  \
                    if (!failed)                                                                                       \
                    {                                                                                                  \
                        st = s.Commit();                                                                               \
                        failed = st.Failed();                                                                          \
                        first = st;                                                                                    \
                    }                                                                                                  \
                    if (failed)                                                                                        \
                    {                                                                                                  \
                        REQUIRE(s.Rollback().Ok());                                                                    \
                    }                                                                                                  \
                }                                                                                                      \
                REQUIRE(failed);                                                                                       \
                CHECK(first.Is(ErrorCode::Io));                                                                        \
                CHECK(Fingerprint(s) == before);                                                                       \
                String<HeapAllocator> value(HeapAllocator::Default());                                                 \
                REQUIRE(s.GetMetadata("m", &value).Ok());                                                              \
                CHECK(value.View() == StringView("old"));                                                              \
            }                                                                                                          \
        }                                                                                                              \
    }                                                                                                                  \
    TEST_CASE("Store conformance: a damaged record is Corrupt, not an abort [" #Fixture "]", "[storage][store]")       \
    {                                                                                                                  \
        if constexpr (Fixture::kCanCorrupt)                                                                            \
        {                                                                                                              \
            using namespace conformance;                                                                               \
            Fixture f;                                                                                                 \
            Store& s = f.Db();                                                                                         \
            REQUIRE(s.Put(Rec(1, 1)).Ok());                                                                            \
            REQUIRE(s.Put(Rec(2, 1)).Ok());                                                                            \
            REQUIRE(f.Corrupt(EntityKey{0, 2}));                                                                       \
            EntityRecord out;                                                                                          \
            CHECK(s.Get(EntityKey{0, 1}, &out).Ok());                                                                  \
            CHECK(s.Get(EntityKey{0, 2}, &out).Is(ErrorCode::Corrupt));                                                \
            Collect all;                                                                                               \
            CHECK(s.Scan(KeyRange(), all).Is(ErrorCode::Corrupt));                                                     \
            CHECK(all.keys.Size() == 1); /* the scan delivered what came before the damage */                          \
        }                                                                                                              \
    }                                                                                                                  \
    TEST_CASE("Store conformance: readers on other threads see the old state until commit [" #Fixture "]",             \
              "[storage][store]")                                                                                      \
    {                                                                                                                  \
        if constexpr (Fixture::kReadersSeeCommitted)                                                                   \
        {                                                                                                              \
            Fixture f;                                                                                                 \
            Store& s = f.Db();                                                                                         \
            using namespace conformance;                                                                               \
            for (u64 k = 1; k <= 3; ++k)                                                                               \
            {                                                                                                          \
                REQUIRE(s.Put(Rec(k, 1)).Ok());                                                                        \
            }                                                                                                          \
            REQUIRE(s.Begin().Ok());                                                                                   \
            REQUIRE(s.Put(Rec(4, 1)).Ok());                                                                            \
            REQUIRE(s.Remove(EntityKey{0, 1}).Ok());                                                                   \
            usize seen = 0;                                                                                            \
            bool old_has_one = false;                                                                                  \
            Status writer_busy;                                                                                        \
            Thread reader(                                                                                             \
                [&]                                                                                                    \
                {                                                                                                      \
                    static_cast<void>(s.Count(&seen));                                                                 \
                    EntityRecord r;                                                                                    \
                    old_has_one = s.Get(EntityKey{0, 1}, &r).Ok();                                                     \
                    writer_busy = s.Put(Rec(99, 1));                                                                   \
                });                                                                                                    \
            reader.Join();                                                                                             \
            CHECK(seen == 3);                                                                                          \
            CHECK(old_has_one);                                                                                        \
            CHECK(writer_busy.Is(ErrorCode::Busy));                                                                    \
            REQUIRE(s.Commit().Ok());                                                                                  \
            CHECK(CountOf(s) == 3);                                                                                    \
            EntityRecord r;                                                                                            \
            CHECK(s.Get(EntityKey{0, 99}, &r).Is(ErrorCode::NotFound));                                                \
        }                                                                                                              \
    }
