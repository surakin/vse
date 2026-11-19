// container_bench: the foundation containers and algorithms on their own (Day 1 of the container audit series).
#include "algorithm.h"
#include "bench.h"
#include "containers/array.h"
#include "containers/hash_map.h"
#include "containers/unique_ptr.h"
#include "cstr/format.h"
#include "cstr/mem_ops.h"
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

// A 56-byte element like the G-buffer's draw entries: a key, a few tie-breaks, a payload.
struct Wide
{
    u64 key = 0;
    u32 first = 0;
    u32 count = 0;
    u32 slot = 0;
    u32 pad = 0;
    Array<u64, 4> payload{};
};

inline bool Less(u32 a, u32 b)
{
    return a < b;
}
inline bool Less(const Wide& a, const Wide& b)
{
    if (a.key != b.key)
    {
        return a.key < b.key;
    }
    return a.slot < b.slot;
}
inline void SetKey(u32* v, u64 k, u32)
{
    *v = static_cast<u32>(k);
}
inline void SetKey(Wide* v, u64 k, u32 slot)
{
    v->key = k;
    v->slot = slot;
}
template <typename T> int Compare(const void* a, const void* b)
{
    const T& x = *static_cast<const T*>(a);
    const T& y = *static_cast<const T*>(b);
    return Less(x, y) ? -1 : (Less(y, x) ? 1 : 0);
}

template <typename T> void SortCasesOf(const Options& o, Report* report, u32 n, const char* type)
{
    Vector<T> v;
    v.Resize(n);
    Vector<T> scratch;
    Rng rng;
    const Array<const char*, 5> patterns = {"random", "sorted", "reversed", "all equal", "few distinct"};
    for (u32 pattern = 0; pattern < 5; ++pattern)
    {
        const auto fill = [&]()
        {
            for (u32 i = 0; i < n; ++i)
            {
                const u64 r = rng.Next() >> 8;
                const u64 k = pattern == 0 ? r : pattern == 1 ? i : pattern == 2 ? n - i : pattern == 3 ? 7 : r % 8;
                SetKey(&v[i], k, i);
            }
        };
        const Span<T> span(v.Data(), v.Size());
        char name[80];
        FormatTo(name, sizeof(name), "Sort %s %s", type, patterns[pattern]);
        report->Add("sort", name, n,
                    MedianMs(o.runs, fill, [&]() { Sort(span, [](const T& a, const T& b) { return Less(a, b); }); }),
                    n);
        FormatTo(name, sizeof(name), "StableSort %s %s", type, patterns[pattern]);
        report->Add("sort", name, n,
                    MedianMs(o.runs, fill,
                             [&]() { StableSort(span, [](const T& a, const T& b) { return Less(a, b); }, scratch); }),
                    n);
        FormatTo(name, sizeof(name), "qsort %s %s", type, patterns[pattern]);
        report->Add("sort", name, n, MedianMs(o.runs, fill, [&]() { SortRaw(v.Data(), n, sizeof(T), &Compare<T>); }),
                    n);
    }
}

void SortCases(const Options& o, Report* report, u32 n)
{
    SortCasesOf<u32>(o, report, n, "u32");
    SortCasesOf<Wide>(o, report, n / 4, "56B");
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
