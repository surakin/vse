// container_bench: the foundation containers and algorithms on their own (Day 1 of the container audit series).
#include "algorithm.h"
#include "bench.h"
#include "containers/array.h"
#include "containers/hash_map.h"
#include "containers/unique_ptr.h"
#include "cstr/format.h"
#include "string/string.h"

namespace container_bench
{

namespace
{

void MapU64(const Options& o, Report* report, u32 n)
{
    Rng rng;
    Vector<u64> keys;
    keys.Reserve(n);
    for (u32 i = 0; i < n; ++i)
    {
        keys.Push(rng.Next());
    }
    UniquePtr<HashMap<u64, u32, HeapAllocator>> map;
    const auto fresh = [&]() { map = MakeUnique<HashMap<u64, u32, HeapAllocator>>(HeapAllocator::Default()); };
    const auto fill = [&]()
    {
        for (u32 i = 0; i < n; ++i)
        {
            map->Insert(keys[i], i);
        }
    };
    report->Add("micro", "HashMap<u64> insert", n, MedianMs(o.runs, fresh, fill), n);
    fresh();
    fill();
    u64 sum = 0;
    report->Add("micro", "HashMap<u64> find hit", n,
                MedianMs(
                    o.runs, [] {},
                    [&]()
                    {
                        for (u32 i = 0; i < n; ++i)
                        {
                            sum += *map->Find(keys[i]);
                        }
                    }),
                n);
    report->Add("micro", "HashMap<u64> find miss", n,
                MedianMs(
                    o.runs, [] {},
                    [&]()
                    {
                        for (u32 i = 0; i < n; ++i)
                        {
                            sum += map->Find(keys[i] ^ 1u) != nullptr;
                        }
                    }),
                n);
    report->Add("micro", "HashMap<u64> remove", n,
                MedianMs(
                    o.runs, [&]() { fresh(), fill(); },
                    [&]()
                    {
                        for (u32 i = 0; i < n; ++i)
                        {
                            sum += map->Remove(keys[i]);
                        }
                    }),
                n);
    Keep(sum);
}

void MapString(const Options& o, Report* report, u32 n)
{
    Vector<String<HeapAllocator>> keys;
    keys.Reserve(n);
    for (u32 i = 0; i < n; ++i)
    {
        char text[48];
        FormatTo(text, sizeof(text), "resources/meshes/item_%u.mesh", i);
        keys.Push(String<HeapAllocator>(HeapAllocator::Default(), StringView(text)));
    }
    HashMap<String<HeapAllocator>, u32, HeapAllocator> map(HeapAllocator::Default());
    for (u32 i = 0; i < n; ++i)
    {
        map.Insert(String<HeapAllocator>(HeapAllocator::Default(), keys[i].View()), i);
    }
    u64 sum = 0;
    report->Add("micro", "HashMap<String> find by view", n,
                MedianMs(
                    o.runs, [] {},
                    [&]()
                    {
                        for (u32 i = 0; i < n; ++i)
                        {
                            sum += *map.Find(keys[i].View());
                        }
                    }),
                n);
    // The linear scan a Vector of names gives, for comparison (a tenth of the lookups: it is slow).
    const u32 lookups = n / 10 > 0 ? n / 10 : 1;
    report->Add("micro", "Vector<String> linear find", n,
                MedianMs(
                    o.runs, [] {},
                    [&]()
                    {
                        for (u32 i = 0; i < lookups; ++i)
                        {
                            const StringView want = keys[(i * 7919u) % n].View();
                            for (u32 k = 0; k < n; ++k)
                            {
                                if (keys[k].View() == want)
                                {
                                    sum += k;
                                    break;
                                }
                            }
                        }
                    }),
                lookups);
    Keep(sum);
}

void VectorCases(const Options& o, Report* report, u32 n)
{
    Vector<u32> v;
    report->Add("micro", "Vector<u32> push", n,
                MedianMs(
                    o.runs, [&]() { v.Reset(); },
                    [&]()
                    {
                        for (u32 i = 0; i < n; ++i)
                        {
                            v.Push(i);
                        }
                    }),
                n);
    const u32 q = n / 10 > 0 ? n / 10 : 1;  // Remove(0) is quadratic: a tenth of the size
    const auto fill = [&]()
    {
        v.Clear();
        for (u32 i = 0; i < q; ++i)
        {
            v.Push(i);
        }
    };
    report->Add("micro", "Vector<u32> Remove(0) drain", q,
                MedianMs(o.runs, fill,
                         [&]()
                         {
                             while (!v.Empty())
                             {
                                 v.Remove(0);
                             }
                         }),
                q);
    report->Add("micro", "Vector<u32> Pop drain", q,
                MedianMs(o.runs, fill,
                         [&]()
                         {
                             while (!v.Empty())
                             {
                                 v.Pop();
                             }
                         }),
                q);
}

void SortCases(const Options& o, Report* report, u32 n)
{
    Vector<u32> v;
    v.Resize(n);
    Rng rng;
    const auto random = [&]()
    {
        for (u32 i = 0; i < n; ++i)
        {
            v[i] = static_cast<u32>(rng.Next());
        }
    };
    const auto sorted = [&]()
    {
        for (u32 i = 0; i < n; ++i)
        {
            v[i] = i;
        }
    };
    const auto equal = [&]()
    {
        for (u32 i = 0; i < n; ++i)
        {
            v[i] = 7;
        }
    };
    const auto sort = [&]() { Sort(Span<u32>(v.Data(), v.Size())); };
    const auto stable = [&]() { StableSort(Span<u32>(v.Data(), v.Size()), [](u32 a, u32 b) { return a < b; }); };
    report->Add("micro", "Sort random", n, MedianMs(o.runs, random, sort), n);
    report->Add("micro", "Sort sorted", n, MedianMs(o.runs, sorted, sort), n);
    report->Add("micro", "Sort all equal", n, MedianMs(o.runs, equal, sort), n);
    report->Add("micro", "StableSort random", n, MedianMs(o.runs, random, stable), n);
}

void StringCases(const Options& o, Report* report, u32 bytes)
{
    const char piece[] = "0123456789abcdef";
    UniquePtr<String<HeapAllocator>> s;
    report->Add("micro", "String append 16 B pieces", bytes,
                MedianMs(
                    o.runs, [&]() { s = MakeUnique<String<HeapAllocator>>(HeapAllocator::Default()); },
                    [&]()
                    {
                        for (u32 i = 0; i < bytes / 16; ++i)
                        {
                            s->Append(StringView(piece, 16));
                        }
                    }),
                bytes);
    Keep(s->Length());
}

}  // namespace

void RunMicro(const Options& o, Report* report)
{
    const Array<u32, 3> sizes = {1000, 100000, 1000000};
    for (u32 n : sizes)
    {
        if (o.quick && n > 1000)
        {
            continue;
        }
        MapU64(o, report, n);
    }
    MapString(o, report, o.quick ? 1000 : 20000);
    VectorCases(o, report, o.quick ? 1000 : 1000000);
    SortCases(o, report, o.quick ? 1000 : 1000000);
    StringCases(o, report, o.quick ? 4096 : 1 << 20);
}

}  // namespace container_bench
