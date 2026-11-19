// container_bench: the engine paths the container audit found slow (Day 1 of the container audit series). Each case
// times a public entry point at two or more sizes, so that the growth shows: a doubled size that quadruples the time
// is quadratic. Paths with a bench of their own (physics_bench, script_bench, renderer_bench) are measured there.
#include "asset/resource_table.h"
#include "containers/array.h"
#include "bench.h"
#include "cstr/format.h"
#include "ecs/transform.h"
#include "ecs/world.h"
#include "renderer/device_resources.h"
#include "renderer/gbuffer.h"
#include "renderer/material.h"
#include "rhi/null/null_device.h"
#include "script/value.h"
#include "settings/config_text.h"
#include "storage/memory_store.h"
#include "ui/theme.h"

namespace container_bench
{

namespace
{

// ---- renderer: the material pipeline cache, scanned once per drawn object

void Materials(const Options& o, Report* report)
{
    rhi::NullDevicePtr device = rhi::CreateNullDevice();
    renderer::MaterialSystem materials(device.Get(), renderer::MaterialSystemDesc{});
    const renderer::GBufferTemplates templates = renderer::CreateGBufferTemplates(&materials);
    const renderer::MaterialId material = renderer::CreateGBufferMaterial(&materials, templates,
                                                                          renderer::GBufferOptions{}, "bench");
    if (!material.IsValid())
    {
        report->Add("paths", "materials: no G-buffer material on the null device (skipped)", 0, 0.0, 1);
        return;
    }
    const renderer::VertexLayoutData layout = renderer::StandardVertexLayout();
    // Distinct pipeline keys: color formats, color counts, depth formats and topologies.
    const Array<rhi::Format, 8> colors = {
        rhi::Format::RGBA8_UNorm, rhi::Format::BGRA8_UNorm, rhi::Format::RGBA16_Float, rhi::Format::RGBA32_Float,
        rhi::Format::RGBA8_SRGB,  rhi::Format::BGRA8_SRGB,  rhi::Format::R32_Float,    rhi::Format::RG16_Float};
    const Array<rhi::Format, 3> depths = {rhi::Format::Undefined, rhi::Format::D32_Float,
                                          rhi::Format::D24_UNorm_S8_UInt};
    struct Key
    {
        renderer::PipelineTarget target;
        rhi::PrimitiveTopology topology;
        bool restart;
    };
    Vector<Key> keys;
    for (u32 count = 1; count <= 4; ++count)
    {
        for (rhi::Format depth : depths)
        {
            for (rhi::Format color : colors)
            {
                for (u32 t = 0; t < 3; ++t)
                {
                    Key k;
                    k.target.color_count = count;
                    for (u32 c = 0; c < count; ++c)
                    {
                        k.target.color_formats[c] = color;
                    }
                    k.target.depth_format = depth;
                    k.topology = t == 0 ? rhi::PrimitiveTopology::TriangleList : rhi::PrimitiveTopology::TriangleStrip;
                    k.restart = t == 2;
                    keys.Push(k);
                }
            }
        }
    }
    const Array<u32, 3> pipelines = {16, 64, 250};
    const u32 objects = o.quick ? 1000 : 10000;
    u32 cached = 0;
    u32 made = 0;  // the keys the device made a pipeline for: the entries the cache holds
    for (u32 p : pipelines)
    {
        while (cached < p && cached < keys.Size())
        {
            const Key& k = keys[cached++];
            made += materials.GetPipeline(material, k.target, layout, k.topology, k.restart).pipeline.IsValid() ? 1u
                                                                                                                : 0u;
        }
        // Each object asks for the last key made: the scan walks every cached entry before it.
        const Key& last = keys[cached - 1];
        u64 hits = 0;
        const double ms = MedianMs(
            o.runs, [] {},
            [&]()
            {
                for (u32 i = 0; i < objects; ++i)
                {
                    hits += materials.GetPipeline(material, last.target, layout, last.topology, last.restart)
                                .pipeline.IsValid();
                }
            });
        char name[96];
        FormatTo(name, sizeof(name), "materials: %u objects, %u cached pipelines", objects, made);
        report->Add("paths", name, made, ms, objects);
        Keep(hits);
    }
}

// ---- renderer: DeviceResources' capacity (the audit's 256-owner cap)

class Owner final : public renderer::Recreatable
{
public:
    void Release() override {}
    bool Recreate(rhi::Device*) override { return true; }
    bool Live() const override { return true; }
    const char* RecreatableName() const override { return "bench"; }
};

void DeviceOwners(const Options&, Report* report)
{
    renderer::DeviceResources resources;
    Vector<UniquePtr<Owner>> owners;
    u32 accepted = 0;
    const u32 asked = 300;  // past the cap of 256 today; each refusal is logged
    const u64 t0 = os::MonotonicTimeNanos();
    for (u32 i = 0; i < asked; ++i)
    {
        owners.Push(MakeUnique<Owner>());
        accepted += resources.Register(owners[i].Get(), 0) ? 1u : 0u;
    }
    const double ms = static_cast<double>(os::MonotonicTimeNanos() - t0) / 1.0e6;
    char name[96];
    FormatTo(name, sizeof(name), "device resources: %u of %u owners accepted", accepted, asked);
    report->Add("paths", name, accepted, ms, asked);
    for (u32 i = 0; i < asked; ++i)
    {
        resources.Unregister(owners[i].Get());
    }
}

// ---- ui: the theme cache, scanned once per widget draw

void Theme(const Options& o, Report* report)
{
    const Array<u32, 4> distinct = {100, 1000, 4000, 8000};
    const u32 resolves = o.quick ? 2000 : 20000;
    for (u32 k : distinct)
    {
        ui::ThemeSet set;
        // Fill the cache with `k` keys (the scale makes each distinct), then resolve over them round robin.
        for (u32 i = 0; i < k; ++i)
        {
            set.Resolve(ui::widget_kind::kButton, 0, 0, 1.0f + static_cast<f32>(i) * 0.001f);
        }
        u64 sum = 0;
        const double ms = MedianMs(
            o.runs, [] {},
            [&]()
            {
                for (u32 i = 0; i < resolves; ++i)
                {
                    sum += static_cast<u64>(
                        set.Resolve(ui::widget_kind::kButton, 0, 0, 1.0f + static_cast<f32>(i % k) * 0.001f).padding_x);
                }
            });
        char name[96];
        FormatTo(name, sizeof(name), "theme: %u resolves over %u distinct keys", resolves, k);
        report->Add("paths", name, k, ms, resolves);
        Keep(sum);
    }
}

// ---- script: Value maps, built key by key

void ValueMaps(const Options& o, Report* report)
{
    const Array<u32, 3> sizes = {16, 1000, 8000};
    for (u32 n : sizes)
    {
        if (o.quick && n > 1000)
        {
            continue;
        }
        Vector<String<HeapAllocator>> names;
        for (u32 i = 0; i < n; ++i)
        {
            char text[24];
            FormatTo(text, sizeof(text), "key_%u", i);
            names.Push(String<HeapAllocator>(HeapAllocator::Default(), StringView(text)));
        }
        u64 found = 0;
        const double ms = MedianMs(
            o.runs, [] {},
            [&]()
            {
                script::Value map = script::Value::MakeMap();
                for (u32 i = 0; i < n; ++i)
                {
                    map.Set(names[i].View(), script::Value::FromInt(i));
                }
                for (u32 i = 0; i < n; ++i)
                {
                    found += map.Find(names[i].View()) != nullptr;
                }
            });
        char name[64];
        FormatTo(name, sizeof(name), "script values: map of %u keys, set + find", n);
        report->Add("paths", name, n, ms, 2ull * n);
        Keep(found);
    }
}

// ---- ecs: building a hierarchy with SetParent

void Hierarchy(const Options& o, Report* report)
{
    const Array<u32, 2> sizes = {1000, 4000};
    for (u32 n : sizes)
    {
        if (o.quick && n > 1000)
        {
            continue;
        }
        UniquePtr<ecs::World> world;
        Vector<ecs::Entity> entities;
        const auto setup = [&]()
        {
            world = MakeUnique<ecs::World>();
            entities.Clear();
            for (u32 i = 0; i < n; ++i)
            {
                entities.Push(world->Create(ecs::LocalTransform()));
            }
        };
        const double ms = MedianMs(o.runs, setup,
                                   [&]()
                                   {
                                       for (u32 i = 1; i < n; ++i)  // a wide tree: entity i under (i - 1) / 8
                                       {
                                           ecs::SetParent(*world, entities[i], entities[(i - 1) / 8]);
                                       }
                                   });
        char name[64];
        FormatTo(name, sizeof(name), "ecs: SetParent for %u entities", n);
        report->Add("paths", name, n, ms, n);
    }
}

// ---- asset: removing paths from a resource table

void Table(const Options& o, Report* report)
{
    const Array<u32, 2> sizes = {2000, 10000};
    for (u32 n : sizes)
    {
        if (o.quick && n > 2000)
        {
            continue;
        }
        Vector<String<HeapAllocator>> paths;
        for (u32 i = 0; i < n; ++i)
        {
            char text[64];
            FormatTo(text, sizeof(text), "/meshes/level/item_%u.mesh", i);
            paths.Push(String<HeapAllocator>(HeapAllocator::Default(), StringView(text)));
        }
        u64 ok = 0;
        UniquePtr<asset::ResourceTable> table;
        const auto setup = [&]()
        {
            table = MakeUnique<asset::ResourceTable>();
            for (u32 i = 0; i < n; ++i)
            {
                ok += table->Add(paths[i].View(), asset::ResourceId::FromValue(i + 1u), asset::types::kMesh).Ok() ? 1u
                                                                                                                  : 0u;
            }
        };
        const u32 removed = n / 10;
        const double ms = MedianMs(o.runs, setup,
                                   [&]()
                                   {
                                       for (u32 i = 0; i < removed; ++i)
                                       {
                                           table->Remove(paths[i * 10].View());
                                       }
                                   });
        char name[64];
        FormatTo(name, sizeof(name), "asset: remove %u of %u table paths%s", removed, n,
                 ok == static_cast<u64>(n) * o.runs ? "" : " (ADDS REFUSED: invalid case)");
        report->Add("paths", name, n, ms, removed);
        Keep(ok);
    }
}

// ---- storage: saving records in random key order

void Store(const Options& o, Report* report)
{
    const Array<u32, 2> sizes = {2000, 10000};
    for (u32 n : sizes)
    {
        if (o.quick && n > 2000)
        {
            continue;
        }
        Vector<storage::EntityRecord> records;
        Rng rng;
        const Array<byte, 32> blob{};
        for (u32 i = 0; i < n; ++i)
        {
            storage::EntityKey key;
            key.high = rng.Next() | 1u;
            key.low = rng.Next();
            records.Push(storage::EntityRecord::Make(key, ByteSpan(blob.Data(), blob.Size())));
        }
        UniquePtr<storage::MemoryStore> store;
        u64 ok = 0;
        const double ms = MedianMs(
            o.runs, [&]() { store = MakeUnique<storage::MemoryStore>(); },
            [&]()
            {
                for (u32 i = 0; i < n; ++i)
                {
                    ok += store->Put(records[i]).Ok() ? 1u : 0u;
                }
            });
        char name[64];
        FormatTo(name, sizeof(name), "storage: put %u records in random order", n);
        report->Add("paths", name, n, ms, n);
        Keep(ok);
    }
}

// ---- settings: reading every key of a parsed file

void Settings(const Options& o, Report* report)
{
    const Array<u32, 2> sizes = {500, 2000};
    for (u32 n : sizes)
    {
        if (o.quick && n > 500)
        {
            continue;
        }
        Vector<char> text;
        Vector<String<HeapAllocator>> keys;
        for (u32 i = 0; i < n; ++i)
        {
            char line[64];
            const usize length = FormatTo(line, sizeof(line), "section.key_%u = %u\n", i, i);
            for (usize c = 0; c < length; ++c)
            {
                text.Push(line[c]);
            }
            char key[32];
            FormatTo(key, sizeof(key), "section.key_%u", i);
            keys.Push(String<HeapAllocator>(HeapAllocator::Default(), StringView(key)));
        }
        u64 found = 0;
        const double ms = MedianMs(
            o.runs, [] {},
            [&]()
            {
                settings::ConfigDocument document;
                settings::ConfigReport problems;
                document.Parse(StringView(text.Data(), text.Size()), &problems);
                for (u32 i = 0; i < n; ++i)
                {
                    StringView value;
                    found += document.Find(keys[i].View(), &value);
                }
            });
        char name[64];
        FormatTo(name, sizeof(name), "settings: parse and read %u keys%s", n,
                 found == static_cast<u64>(n) * o.runs ? "" : " (KEYS NOT FOUND: invalid case)");
        report->Add("paths", name, n, ms, n);
        Keep(found);
    }
}

}  // namespace

void RunPaths(const Options& o, Report* report)
{
    Materials(o, report);
    DeviceOwners(o, report);
    Theme(o, report);
    ValueMaps(o, report);
    Hierarchy(o, report);
    Table(o, report);
    Store(o, report);
    Settings(o, report);
}

}  // namespace container_bench
