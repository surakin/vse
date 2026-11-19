// The resource manager and the CPU loaders (Phase 22 Day 328).

#include <catch2/catch_test_macros.hpp>

#include "asset/clip.h"
#include "asset/cpu_loaders.h"
#include "asset/memory_backend.h"
#include "asset/pak.h"
#include "asset/pak_backend.h"
#include "asset/resource_manager.h"
#include "clip_source.h"
#include "cstr/cstr.h"
#include "cstr/format.h"
#include "cstr/mem_ops.h"
#include "level_source.h"
#include "mesh_source.h"
#include "storage/builtin_components.h"
#include "skeleton_source.h"
#include "os/os.h"
#include "support.h"
#include "sync/thread.h"

namespace engine_placeholder::asset
{

namespace
{

using test::AsBytes;
using test::Contains;
using test::Rid;
using test::TempDir;

constexpr ResourceType kChainType = ResourceType::FromChars('T', 'C', 'H', 'N');
constexpr ResourceType kOtherType = ResourceType::FromChars('T', 'O', 'T', 'H');

// The test type: a value in section 16 and a list of dependencies.
struct ChainObject
{
    u32 value = 0;
    u32 dependency_count = 0;
};

Status LoadChain(const LoadContext& context, ChainObject* out, void*)
{
    ByteSpan section;
    if (!context.Blob().FindSection(16, &section) || section.Size() < 4)
    {
        return Status::Make(ErrorCode::Corrupt, "a chain blob has no value");
    }
    out->value = static_cast<u32>(section[0]) | (static_cast<u32>(section[1]) << 8);
    out->dependency_count = static_cast<u32>(context.Blob().DependencyCount());
    for (usize i = 0; i < context.Blob().DependencyCount(); ++i)
    {
        if (context.Dependency<ChainObject>(context.Blob().DependencyAt(i).id) == nullptr)
        {
            return Status::Make(ErrorCode::DependencyTypeMismatch, "a dependency's object is missing");
        }
    }
    return Status::Success();
}

}  // namespace

struct OtherObject
{
    u32 value = 0;
};

template <> struct ResourceTraits<OtherObject>
{
    static constexpr ResourceType kType = kOtherType;
    static constexpr u32 kKind = ResourceType::FromChars('t', 'O', 'T', 'H').Tag();
};

template <> struct ResourceTraits<ChainObject>
{
    static constexpr ResourceType kType = kChainType;
    static constexpr u32 kKind = ResourceType::FromChars('t', 'C', 'H', 'N').Tag();
};

namespace
{

struct Edge
{
    u64 id;
    u32 version;  // the version the reference declares
};

Vector<byte> ChainBlob(const TypeRegistry& types, u64 id, u32 value, const Edge* deps = nullptr, usize count = 0,
                       ResourceType type = kChainType)
{
    BlobWriter writer(types, type, Rid(id));
    byte payload[8] = {static_cast<byte>(value), static_cast<byte>(value >> 8), 0, 0, 0, 0, 0, 0};
    REQUIRE(writer.AddSection(16, ByteSpan(payload, 8)).Ok());
    for (usize i = 0; i < count; ++i)
    {
        Dependency d;
        d.id = Rid(deps[i].id);
        d.type = kChainType;
        d.type_version = deps[i].version;
        writer.AddDependency(d);
    }
    Vector<byte> blob;
    REQUIRE(writer.Finish(&blob).Ok());
    return blob;
}

struct MgrRig
{
    TypeRegistry types;
    HostMemoryBackend backend;
    ManagerConfig config;
    UniquePtr<ResourceManager> manager;

    explicit MgrRig(ManagerConfig cfg = ManagerConfig()) : types(), backend(Prepare(types), u64{64} << 20), config(cfg)
    {
        config.buffer_pool_bytes = u64{64} << 20;
        manager = MakeUnique<ResourceManager>(types, config);
        manager->RegisterLoader<ChainObject>(&LoadChain);
        RegisterCpuLoaders(manager.Get());
        REQUIRE(manager->Mount(StringView("/"), &backend, 0).Ok());
    }
    static const TypeRegistry& Prepare(TypeRegistry& types)
    {
        REQUIRE(types.Register(kChainType, 1, "chain").Ok());
        REQUIRE(types.Register(kOtherType, 1, "other").Ok());
        return types;
    }
    void Add(u64 id, u32 value, const Edge* deps = nullptr, usize count = 0, ResourceType type = kChainType)
    {
        const Vector<byte> blob = ChainBlob(types, id, value, deps, count, type);
        REQUIRE(backend.Add(Rid(id), AsBytes(blob)).Ok());
    }
};

}  // namespace

TEST_CASE("manager: a chain loads with its closure and frees when the last handle goes", "[asset][manager]")
{
    MgrRig rig;
    const Edge to_b[1] = {{2, 1}};
    const Edge to_c[1] = {{3, 1}};
    rig.Add(3, 33);
    rig.Add(2, 22, to_c, 1);
    rig.Add(1, 11, to_b, 1);
    Handle<ChainObject> handle;
    REQUIRE(rig.manager->Load<ChainObject>(Rid(1), &handle).Ok());
    REQUIRE(handle.IsValid());
    REQUIRE(handle.Get() != nullptr);
    CHECK(handle.Get()->value == 11);
    CHECK(handle.Get()->dependency_count == 1);
    ResourceStats stats = rig.manager->Stats();
    CHECK(stats.loads == 3);
    CHECK(stats.resident == 3);
    CHECK(stats.load_conversions == 0);
    CHECK(stats.bytes_resident > 0);
    CHECK(stats.mounts.Size() == 1);
    CHECK(stats.mounts[0].reads == 3);

    // A copy adds a reference; the resource stays until both are gone.
    Handle<ChainObject> copy = handle;
    handle.Reset();
    CHECK(rig.manager->Stats().resident == 3);
    CHECK(copy.Get()->value == 11);
    copy.Reset();
    stats = rig.manager->Stats();
    CHECK(stats.resident == 0);
    CHECK(stats.bytes_resident == 0);
    CHECK(rig.manager->Pool().BytesInUse() == 0);
}

TEST_CASE("manager: a shared dependency is loaded once (a diamond)", "[asset][manager]")
{
    MgrRig rig;
    const Edge both[2] = {{2, 1}, {3, 1}};
    const Edge to_d[1] = {{4, 1}};
    rig.Add(4, 44);
    rig.Add(2, 22, to_d, 1);
    rig.Add(3, 33, to_d, 1);
    rig.Add(1, 11, both, 2);
    Handle<ChainObject> handle;
    REQUIRE(rig.manager->Load<ChainObject>(Rid(1), &handle).Ok());
    const ResourceStats stats = rig.manager->Stats();
    CHECK(stats.loads == 4);
    CHECK(stats.cache_hits == 1);
    // Releasing the root frees all four, the shared one last.
    handle.Reset();
    CHECK(rig.manager->Stats().resident == 0);
}

TEST_CASE("manager: a missing dependency at any depth leaves nothing loaded and names the chain", "[asset][manager]")
{
    for (u64 missing = 2; missing <= 4; ++missing)
    {
        MgrRig rig;
        const Edge e12[1] = {{2, 1}};
        const Edge e23[1] = {{3, 1}};
        const Edge e34[1] = {{4, 1}};
        if (missing != 4)
        {
            rig.Add(4, 44);
        }
        if (missing != 3)
        {
            rig.Add(3, 33, e34, 1);
        }
        if (missing != 2)
        {
            rig.Add(2, 22, e23, 1);
        }
        rig.Add(1, 11, e12, 1);
        // A chain 1 -> 2 -> 3 -> 4 with one link removed: the first absent id stops the closure.
        Handle<ChainObject> handle;
        LoadFailure failure;
        const Status s = rig.manager->Load<ChainObject>(Rid(1), &handle, &failure);
        INFO("missing " << missing);
        CHECK(s.Is(ErrorCode::MissingDependency));
        CHECK_FALSE(handle.IsValid());
        CHECK(failure.resource == Rid(missing));
        CHECK(Contains(failure.chain.Data(), " -> "));
        const ResourceStats stats = rig.manager->Stats();
        CHECK(stats.resident == 0);
        CHECK(stats.bytes_resident == 0);
        CHECK(rig.manager->Pool().BytesInUse() == 0);
    }
}

TEST_CASE("manager: a dependency of another type or version is refused", "[asset][manager]")
{
    {
        MgrRig rig;
        const Edge to_b[1] = {{2, 1}};
        rig.Add(2, 22, nullptr, 0, kOtherType);  // the reference declares a chain; the blob is another type
        rig.Add(1, 11, to_b, 1);
        Handle<ChainObject> handle;
        CHECK(rig.manager->Load<ChainObject>(Rid(1), &handle).Is(ErrorCode::DependencyTypeMismatch));
        CHECK(rig.manager->Stats().resident == 0);
    }
    {
        MgrRig rig;
        const Edge to_b[1] = {{2, 7}};  // declares version 7 of the type; the blob is version 1
        rig.Add(2, 22);
        rig.Add(1, 11, to_b, 1);
        Handle<ChainObject> handle;
        CHECK(rig.manager->Load<ChainObject>(Rid(1), &handle).Is(ErrorCode::DependencyTypeMismatch));
        CHECK(rig.manager->Stats().resident == 0);
    }
    {
        // Already loaded as the wrong type: the cache is held to the declaration too.
        MgrRig rig;
        const Edge to_b[1] = {{2, 1}};
        rig.Add(2, 22);
        rig.Add(1, 11, to_b, 1);
        Handle<ChainObject> b;
        REQUIRE(rig.manager->Load<ChainObject>(Rid(2), &b).Ok());
        BlobWriter writer(rig.types, kChainType, Rid(9));
        Dependency declared;
        declared.id = Rid(2);
        declared.type = kOtherType;
        declared.type_version = 1;
        writer.AddDependency(declared);
        byte payload[8] = {};
        REQUIRE(writer.AddSection(16, ByteSpan(payload, 8)).Ok());
        Vector<byte> blob;
        REQUIRE(writer.Finish(&blob).Ok());
        REQUIRE(rig.backend.Add(Rid(9), AsBytes(blob)).Ok());
        Handle<ChainObject> nine;
        CHECK(rig.manager->Load<ChainObject>(Rid(9), &nine).Is(ErrorCode::DependencyTypeMismatch));
    }
}

TEST_CASE("manager: a dependency cycle in hostile content is refused", "[asset][manager]")
{
    MgrRig rig;
    const Edge a_to_b[1] = {{2, 1}};
    const Edge b_to_a[1] = {{1, 1}};
    rig.Add(1, 1, a_to_b, 1);
    rig.Add(2, 2, b_to_a, 1);
    Handle<ChainObject> handle;
    LoadFailure failure;
    CHECK(rig.manager->Load<ChainObject>(Rid(1), &handle, &failure).Is(ErrorCode::DependencyCycle));
    CHECK(Contains(failure.chain.Data(), " -> "));
    CHECK(rig.manager->Stats().resident == 0);
}

TEST_CASE("manager: handles carry generations, so a stale one is never another resource", "[asset][manager]")
{
    MgrRig rig;
    rig.Add(1, 11);
    rig.Add(2, 22);
    Handle<ChainObject> first;
    REQUIRE(rig.manager->Load<ChainObject>(Rid(1), &first).Ok());
    Handle<ChainObject> held = first;
    REQUIRE(rig.manager->Unload(Rid(1), true).Ok());  // forced: the handles go stale
    CHECK_FALSE(first.IsValid());
    CHECK(first.Get() == nullptr);
    CHECK(held.Get() == nullptr);
    // The slot is reused by another resource; the stale handle still reads as nothing.
    Handle<ChainObject> second;
    REQUIRE(rig.manager->Load<ChainObject>(Rid(2), &second).Ok());
    CHECK(second.Get()->value == 22);
    CHECK(first.Get() == nullptr);
    CHECK_FALSE(first.IsValid());
    first.Reset();  // releasing a stale handle changes nothing
    held.Reset();
    CHECK(second.IsValid());
    CHECK(rig.manager->Stats().resident == 1);
    // The same id loaded again is a new generation.
    Handle<ChainObject> again;
    REQUIRE(rig.manager->Load<ChainObject>(Rid(1), &again).Ok());
    CHECK(again.Get()->value == 11);
}

TEST_CASE("manager: unloading what a loaded resource depends on is InUse; pinning keeps a resource", "[asset][manager]")
{
    MgrRig rig;
    const Edge to_b[1] = {{2, 1}};
    rig.Add(2, 22);
    rig.Add(1, 11, to_b, 1);
    Handle<ChainObject> root;
    REQUIRE(rig.manager->Load<ChainObject>(Rid(1), &root).Ok());
    Vector<ResourceId> users;
    CHECK(rig.manager->ResourceInUse(Rid(2), &users));
    REQUIRE(users.Size() == 1);
    CHECK(users[0] == Rid(1));
    users.Clear();
    CHECK(rig.manager->Unload(Rid(2), false, &users).Is(ErrorCode::InUse));
    CHECK(users.Size() == 1);
    CHECK(rig.manager->Unload(Rid(9)).Is(ErrorCode::NotFound));

    // A pinned resource stays when its last handle goes; unpinning frees it.
    root.Reset();
    CHECK(rig.manager->Stats().resident == 0);
    Handle<ChainObject> pin;
    REQUIRE(rig.manager->Load<ChainObject>(Rid(2), &pin).Ok());
    REQUIRE(rig.manager->Pin(Rid(2)).Ok());
    pin.Reset();
    CHECK(rig.manager->Stats().resident == 1);
    REQUIRE(rig.manager->Unpin(Rid(2)).Ok());
    CHECK(rig.manager->Stats().resident == 0);

    // A forced unload takes the dependents with it.
    REQUIRE(rig.manager->Load<ChainObject>(Rid(1), &root).Ok());
    users.Clear();
    REQUIRE(rig.manager->Unload(Rid(2), true, &users).Ok());
    CHECK(users.Size() == 1);
    CHECK_FALSE(root.IsValid());
    CHECK(rig.manager->Stats().resident == 0);
}

TEST_CASE("manager: the HostMemoryBackend refuses to replace or remove what a loaded resource depends on",
          "[asset][manager]")
{
    MgrRig rig;
    rig.backend.SetInUseQuery(rig.manager->InUseQuery());
    const Edge to_b[1] = {{2, 1}};
    rig.Add(2, 22);
    rig.Add(1, 11, to_b, 1);
    Handle<ChainObject> root;
    REQUIRE(rig.manager->Load<ChainObject>(Rid(1), &root).Ok());
    const Vector<byte> other = ChainBlob(rig.types, 2, 99);
    Vector<ResourceId> users;
    CHECK(rig.backend.Replace(Rid(2), AsBytes(other), false, &users).Is(ErrorCode::InUse));
    CHECK(users.Size() == 1);
    CHECK(rig.backend.Remove(Rid(2)).Is(ErrorCode::InUse));
    // Forced, it unloads the dependent first.
    REQUIRE(rig.backend.Replace(Rid(2), AsBytes(other), true).Ok());
    CHECK_FALSE(root.IsValid());
}

TEST_CASE("manager: a changed resource reloads with its dependents, atomically", "[asset][manager]")
{
    MgrRig rig;  // no in-use query: the host may change what is loaded, as a watched directory does
    const Edge to_b[1] = {{2, 1}};
    rig.Add(2, 22);
    rig.Add(1, 11, to_b, 1);
    Handle<ChainObject> root;
    REQUIRE(rig.manager->Load<ChainObject>(Rid(1), &root).Ok());
    Handle<ChainObject> dep;
    REQUIRE(rig.manager->Load<ChainObject>(Rid(2), &dep).Ok());
    Vector<ReloadResult> results;
    REQUIRE(rig.manager->PollChanges(&results).Ok());  // the adds
    CHECK(results.Size() == 0);

    // A good replacement: the dependency and the root, which depends on it, are rebuilt.
    Vector<byte> good = ChainBlob(rig.types, 2, 77);
    REQUIRE(rig.backend.Replace(Rid(2), AsBytes(good)).Ok());
    REQUIRE(rig.manager->PollChanges(&results).Ok());
    REQUIRE(results.Size() == 1);
    CHECK(results[0].status.Ok());
    CHECK(results[0].reloaded == 2);
    CHECK(dep.Get()->value == 77);
    CHECK(root.Get()->value == 11);

    // A replacement of another type: refused, the old version stays loaded.
    Vector<byte> wrong = ChainBlob(rig.types, 2, 5, nullptr, 0, kOtherType);
    REQUIRE(rig.backend.Replace(Rid(2), AsBytes(wrong)).Ok());
    REQUIRE(rig.manager->PollChanges(&results).Ok());
    REQUIRE(results.Size() == 1);
    CHECK(results[0].status.Is(ErrorCode::TypeMismatch));
    CHECK(dep.Get()->value == 77);

    // A replacement whose own dependency is missing: refused too.
    const Edge to_missing[1] = {{404, 1}};
    Vector<byte> broken = ChainBlob(rig.types, 2, 6, to_missing, 1);
    REQUIRE(rig.backend.Replace(Rid(2), AsBytes(broken)).Ok());
    REQUIRE(rig.manager->PollChanges(&results).Ok());
    REQUIRE(results.Size() == 1);
    CHECK(results[0].status.Is(ErrorCode::MissingDependency));
    CHECK(dep.Get()->value == 77);
    CHECK(rig.manager->Stats().resident == 2);

    // A replacement that gains a dependency: the new one is loaded with it, and the byte accounting stays exact.
    rig.Add(5, 55);
    const Edge to_five[1] = {{5, 1}};
    Vector<byte> grown = ChainBlob(rig.types, 2, 8, to_five, 1);
    REQUIRE(rig.backend.Replace(Rid(2), AsBytes(grown)).Ok());
    REQUIRE(rig.manager->PollChanges(&results).Ok());
    REQUIRE(results.Size() == 1);
    CHECK(results[0].status.Ok());
    CHECK(dep.Get()->dependency_count == 1);
    CHECK(rig.manager->Stats().resident == 3);
    dep.Reset();
    root.Reset();
    const ResourceStats after = rig.manager->Stats();
    CHECK(after.resident == 0);
    CHECK(after.bytes_resident == 0);
    CHECK(rig.manager->Pool().BytesInUse() == 0);
}

namespace
{

// A backend that serves whatever bytes it is given, unchecked: what a stale pak or a damaged file looks like to the
// manager.
class RawBackend final : public ResourceBackend
{
public:
    struct Entry
    {
        ResourceId id;
        Vector<byte> bytes;
    };
    class Reader final : public ResourceReader
    {
    public:
        explicit Reader(const Vector<byte>& bytes) : bytes_(&bytes) {}
        u64 Size() const override { return bytes_->Size(); }
        Status ReadAt(u64 offset, void* buffer, usize length) override
        {
            if (offset + length > bytes_->Size())
            {
                return Status::Make(ErrorCode::Invalid, "out of range");
            }
            MemCopy(buffer, bytes_->Data() + offset, length);
            return Status::Success();
        }

    private:
        const Vector<byte>* bytes_;
    };
    const char* Name() const override { return "raw"; }
    u32 Capabilities() const override { return kBackendRanged; }
    Status Stat(ResourceId id, ResourceInfo* out) override
    {
        for (usize i = 0; i < entries.Size(); ++i)
        {
            if (entries[i].id == id)
            {
                out->size = entries[i].bytes.Size();
                return Status::Success();
            }
        }
        return Status::Make(ErrorCode::NotFound, "no such resource");
    }
    Status Open(ResourceId id, UniquePtr<ResourceReader>* out) override
    {
        for (usize i = 0; i < entries.Size(); ++i)
        {
            if (entries[i].id == id)
            {
                UniquePtr<Reader> reader = MakeUnique<Reader>(entries[i].bytes);
                *out = UniquePtr<ResourceReader>(reader.Release());
                return Status::Success();
            }
        }
        return Status::Make(ErrorCode::NotFound, "no such resource");
    }
    Status ReadTable(ResourceTable*) override { return Status::Success(); }
    void Put(u64 id, const Vector<byte>& bytes)
    {
        Entry entry;
        entry.id = Rid(id);
        for (usize i = 0; i < bytes.Size(); ++i)
        {
            entry.bytes.Push(bytes[i]);
        }
        entries.Push(static_cast<Entry&&>(entry));
    }
    Vector<Entry> entries;
};

}  // namespace

TEST_CASE("manager: an old-version blob is refused at once and counted; the hash is checked only on request",
          "[asset][manager]")
{
    TypeRegistry types;
    REQUIRE(types.Register(kChainType, 1, "chain").Ok());
    RawBackend raw;
    Vector<byte> old = ChainBlob(types, 1, 11);
    old[12] = 0;  // the type version field: not the current version
    raw.Put(1, old);
    Vector<byte> damaged = ChainBlob(types, 2, 22);
    damaged[damaged.Size() - 1] = static_cast<byte>(damaged[damaged.Size() - 1] ^ 0xff);  // payload padding or content
    raw.Put(2, damaged);
    Vector<byte> wrong_id = ChainBlob(types, 3, 33);
    raw.Put(4, wrong_id);  // served under another id than the blob's own
    Vector<byte> truncated = ChainBlob(types, 5, 55);
    truncated.Resize(40);
    raw.Put(5, truncated);

    for (const bool verify : {false, true})
    {
        ManagerConfig config;
        config.verify_on_load = verify;
        config.buffer_pool_bytes = u64{8} << 20;
        ResourceManager manager(types, config);
        manager.RegisterLoader<ChainObject>(&LoadChain);
        REQUIRE(manager.Mount(StringView("/"), &raw, 0).Ok());
        Handle<ChainObject> handle;
        const Status s = manager.Load<ChainObject>(Rid(1), &handle);
        CHECK(s.Is(ErrorCode::UnsupportedVersion));
        CHECK(Contains(s.Message(), "asset_mint remint"));
        CHECK(manager.Stats().rejected_old_version == 1);
        CHECK_FALSE(handle.IsValid());
        // A damaged blob loads unless the hash is checked.
        const Status d = manager.Load<ChainObject>(Rid(2), &handle);
        if (verify)
        {
            CHECK(d.Is(ErrorCode::Corrupt));
        }
        else
        {
            CHECK(d.Ok());
        }
        handle.Reset();
        CHECK(manager.Load<ChainObject>(Rid(4), &handle).Is(ErrorCode::Corrupt));
        CHECK(manager.Load<ChainObject>(Rid(5), &handle).Is(ErrorCode::Corrupt));
        CHECK(manager.Stats().resident == 0);
        CHECK(manager.Pool().BytesInUse() == 0);
    }
}

TEST_CASE("manager: the cache budget refuses a load past it", "[asset][manager]")
{
    ManagerConfig config;
    config.cache_bytes = 700;
    MgrRig rig(config);
    rig.Add(1, 1);
    rig.Add(2, 2);
    rig.Add(3, 3);
    Handle<ChainObject> a;
    Handle<ChainObject> b;
    Handle<ChainObject> c;
    REQUIRE(rig.manager->Load<ChainObject>(Rid(1), &a).Ok());
    const u64 one = rig.manager->Stats().bytes_resident;
    REQUIRE(one > 0);
    Status last = Status::Success();
    last = rig.manager->Load<ChainObject>(Rid(2), &b);
    if (last.Ok())
    {
        last = rig.manager->Load<ChainObject>(Rid(3), &c);
    }
    CHECK(last.Is(ErrorCode::LimitExceeded));
    CHECK(rig.manager->Stats().bytes_resident <= 700);
}

TEST_CASE("manager: fallbacks stand in only when allowed, are counted, and never replace a dependency",
          "[asset][manager]")
{
    for (const bool allow : {false, true})
    {
        ManagerConfig config;
        config.allow_fallbacks = allow;
        MgrRig rig(config);
        const Vector<byte> fallback = ChainBlob(rig.types, 900, 90);
        REQUIRE(rig.manager->RegisterFallback(kChainType, AsBytes(fallback)).Ok());
        Handle<ChainObject> handle;
        const Status s = rig.manager->Load<ChainObject>(Rid(5), &handle);  // nothing has id 5
        if (!allow)
        {
            CHECK(s.Is(ErrorCode::NotFound));
            CHECK_FALSE(handle.IsValid());
            CHECK(rig.manager->Stats().fallbacks_used == 0);
        }
        else
        {
            REQUIRE(s.Ok());
            CHECK(handle.Get()->value == 90);
            CHECK(rig.manager->Stats().fallbacks_used == 1);
            // A closure with a missing member fails as a whole.
            const Edge to_missing[1] = {{6, 1}};
            rig.Add(7, 7, to_missing, 1);
            Handle<ChainObject> seven;
            const Status closure = rig.manager->Load<ChainObject>(Rid(7), &seven);
            CHECK(closure.Ok());              // the top-level resource was found, but its dependency was not...
            CHECK(seven.Get()->value == 90);  // ...so the root itself is replaced, never the member
            CHECK(rig.manager->Stats().fallbacks_used == 2);
        }
    }
    // A fallback must be a blob of its type.
    MgrRig rig;
    const Vector<byte> other = ChainBlob(rig.types, 901, 1, nullptr, 0, kOtherType);
    CHECK(rig.manager->RegisterFallback(kChainType, AsBytes(other)).Is(ErrorCode::Invalid));
}

TEST_CASE("manager: loads from two roots on two threads share one dependency", "[asset][manager][threads]")
{
    MgrRig rig;
    const Edge shared[1] = {{3, 1}};
    rig.Add(3, 33);
    rig.Add(1, 11, shared, 1);
    rig.Add(2, 22, shared, 1);
    for (u32 round = 0; round < 200; ++round)
    {
        Handle<ChainObject> one;
        Handle<ChainObject> two;
        Atomic<int> go{0};
        Thread a(
            [&]
            {
                while (go.Load(MemoryOrder::Acquire) == 0)
                {
                }
                CHECK(rig.manager->Load<ChainObject>(Rid(1), &one).Ok());
            });
        Thread b(
            [&]
            {
                while (go.Load(MemoryOrder::Acquire) == 0)
                {
                }
                CHECK(rig.manager->Load<ChainObject>(Rid(2), &two).Ok());
            });
        go.Store(1, MemoryOrder::Release);
        a.Join();
        b.Join();
        REQUIRE(one.Get() != nullptr);
        REQUIRE(two.Get() != nullptr);
        CHECK(rig.manager->Stats().resident == 3);
        one.Reset();
        CHECK(rig.manager->Stats().resident == 2);  // the shared dependency is still held by the other root
        two.Reset();
        REQUIRE(rig.manager->Stats().resident == 0);
    }
    CHECK(rig.manager->Stats().loads == 3 * 200);
}

TEST_CASE("manager: a loose mount overrides a pak, and a pak and a memory backend give the same objects",
          "[asset][manager]")
{
    TempDir dir;
    TypeRegistry types;
    REQUIRE(types.Register(kChainType, 1, "chain").Ok());
    REQUIRE(types.Register(kOtherType, 1, "other").Ok());
    const Edge to_b[1] = {{2, 1}};
    const Vector<byte> a = ChainBlob(types, 1, 11, to_b, 1);
    const Vector<byte> b = ChainBlob(types, 2, 22);
    const Vector<byte> b_loose = ChainBlob(types, 2, 99);

    const io::Path pak_path = dir.Path().Join("content.pak");
    {
        io::FilePtr file;
        REQUIRE(io::File::Open(pak_path, io::OpenMode::Write, 0, &file).Ok());
        PakWriter writer(types, PakWriterOptions());
        REQUIRE(writer.Add(Rid(1), AsBytes(a), StringView("/a")).Ok());
        REQUIRE(writer.Add(Rid(2), AsBytes(b), StringView("/b")).Ok());
        REQUIRE(writer.Finish(*file).Ok());
        REQUIRE(file->Close().Ok());
    }
    ManagerConfig config;
    config.buffer_pool_bytes = u64{16} << 20;

    // From the pak.
    ResourceManager from_pak(types, config);
    from_pak.RegisterLoader<ChainObject>(&LoadChain);
    REQUIRE(from_pak.MountPath(StringView("/"), pak_path, 0).Ok());
    Handle<ChainObject> h1;
    REQUIRE(from_pak.Load<ChainObject>(Rid(1), &h1).Ok());

    // From memory: the same blobs, the same objects.
    HostMemoryBackend memory(types, u64{1} << 20);
    REQUIRE(memory.Add(Rid(1), AsBytes(a)).Ok());
    REQUIRE(memory.Add(Rid(2), AsBytes(b)).Ok());
    ResourceManager from_memory(types, config);
    from_memory.RegisterLoader<ChainObject>(&LoadChain);
    REQUIRE(from_memory.Mount(StringView("/"), &memory, 0).Ok());
    Handle<ChainObject> h2;
    REQUIRE(from_memory.Load<ChainObject>(Rid(1), &h2).Ok());
    CHECK(h1.Get()->value == h2.Get()->value);
    CHECK(h1.Get()->dependency_count == h2.Get()->dependency_count);
    CHECK(from_pak.Stats().bytes_resident == from_memory.Stats().bytes_resident);

    // A loose mount of higher priority overrides the pak entry.
    HostMemoryBackend loose(types, u64{1} << 20);
    REQUIRE(loose.Add(Rid(2), AsBytes(b_loose)).Ok());
    ResourceManager layered(types, config);
    layered.RegisterLoader<ChainObject>(&LoadChain);
    REQUIRE(layered.MountPath(StringView("/"), pak_path, 0).Ok());
    REQUIRE(layered.Mount(StringView("/"), &loose, 10).Ok());
    Handle<ChainObject> dep;
    REQUIRE(layered.Load<ChainObject>(Rid(2), &dep).Ok());
    CHECK(dep.Get()->value == 99);
    // A path load goes through the mounts' tables.
    REQUIRE(layered.Mounts().RefreshTables().Ok());
    Handle<ChainObject> by_path;
    REQUIRE(layered.LoadPath<ChainObject>(StringView("/a"), &by_path).Ok());
    CHECK(by_path.Get()->value == 11);
    CHECK(layered.LoadPath<ChainObject>(StringView("/nothing"), &by_path).Is(ErrorCode::NotFound));
    // The wrong C++ type for a loaded resource is a type mismatch, not another resource.
    Handle<SkeletonView> wrong;
    CHECK(layered.Load<SkeletonView>(Rid(2), &wrong).Failed());
}

TEST_CASE("manager: mounts and settings", "[asset][manager]")
{
    settings::Registry registry;
    RegisterAssetSettings(&registry);
    RegisterManagerSettings(&registry);
    const ManagerConfig config = ManagerConfigFromSettings(registry);
    CHECK_FALSE(config.allow_fallbacks);
    CHECK_FALSE(config.verify_on_load);
    CHECK(config.cache_bytes == 0);
    CHECK(config.buffer_pool_bytes == u64{256} << 20);
    Vector<MountSpec> specs;
    REQUIRE(MountSpecsFromSettings(registry, &specs).Ok());
    CHECK(specs.Size() == 0);
    TypeRegistry types;
    ResourceManager manager(types, config);
    CHECK(manager.MountPath(StringView("/"), io::Path(), 0).Failed());
    CHECK(manager.MountFromSettings(registry).Ok());  // nothing to mount
}

namespace
{

Vector<byte> MintSkeletonBlob(const TypeRegistry& types, u64 id, const char* prefix, u32 joints = 3)
{
    char text[600];
    usize at = static_cast<usize>(FormatTo(text, sizeof(text), "id %016llx\n", static_cast<unsigned long long>(id)));
    for (u32 i = 0; i < joints; ++i)
    {
        char parent[24];
        FormatTo(parent, sizeof(parent), i == 0 ? "-" : "%s%u", prefix, i - 1);
        at += static_cast<usize>(
            FormatTo(text + at, sizeof(text) - at, "joint %s%u %s 0 1 0  0 0 0 1  1 1 1\n", prefix, i, parent));
    }
    Vector<byte> blob;
    SkeletonSourceError error;
    REQUIRE(MintSkeleton(StringView(text, at), types, &blob, &error).Ok());
    return blob;
}

}  // namespace

TEST_CASE("manager: skeletons, clips and skinned meshes load with their closure rules", "[asset][manager][skeletal]")
{
    MgrRig rig;
    const Vector<byte> skeleton_blob = MintSkeletonBlob(rig.types, 0xaa, "j");
    SkeletonView skeleton;
    REQUIRE(skeleton.Open(AsBytes(skeleton_blob), rig.types).Ok());
    Vector<byte> clip_blob;
    {
        const char* text = "id 00000000000000cc\nskeleton 00000000000000aa\nduration 1\nrate 30\nflags loop\ntrack j0\n"
                           "r 0 0 0 0 1\nr 1 0 0 0.3826834 0.9238795\n";
        ClipSourceError error;
        REQUIRE(
            MintClip(StringView(text, CStrLength(text)), skeleton, rig.types, ClipLimits(), &clip_blob, &error).Ok());
    }
    Vector<byte> mesh_blob;
    {
        const char*
            text = "id 00000000000000bb\nskeleton 00000000000000aa\n"
                   "v 0 0 0  0 0 1  1 0 0 1  0 0\nv 1 0 0  0 0 1  1 0 0 1  1 0\nv 0 1 0  0 0 1  1 0 0 1  0 1\ni 0 1 2\n"
                   "skin 0 0 1\nskin 1 1 1\nskin 2 2 1\n";
        MeshSourceError error;
        REQUIRE(MintMesh(StringView(text, CStrLength(text)), rig.types, &skeleton, &mesh_blob, &error).Ok());
    }
    REQUIRE(rig.backend.Add(Rid(0xaa), AsBytes(skeleton_blob)).Ok());
    REQUIRE(rig.backend.Add(Rid(0xcc), AsBytes(clip_blob)).Ok());
    REQUIRE(rig.backend.Add(Rid(0xbb), AsBytes(mesh_blob)).Ok());

    Handle<ClipView> clip;
    Handle<MeshView> mesh;
    REQUIRE(rig.manager->Load<ClipView>(Rid(0xcc), &clip).Ok());
    REQUIRE(rig.manager->Load<MeshView>(Rid(0xbb), &mesh).Ok());
    CHECK(clip.Get()->TrackCount() == 1);
    CHECK(mesh.Get()->Skinned());
    ResourceStats stats = rig.manager->Stats();
    CHECK(stats.loads == 3);  // the skeleton once
    CHECK(stats.cache_hits == 1);
    CHECK(stats.load_conversions == 0);

    // The skeleton cannot be unloaded while the mesh and the clip use it.
    Vector<ResourceId> users;
    CHECK(rig.manager->Unload(Rid(0xaa), false, &users).Is(ErrorCode::InUse));
    CHECK(users.Size() == 2);
    clip.Reset();
    mesh.Reset();
    CHECK(rig.manager->Stats().resident == 0);

    // The same skeleton id with other joints: the hash differs, so the mesh and the clip are refused, naming the cause.
    MgrRig other;
    const Vector<byte> impostor = MintSkeletonBlob(other.types, 0xaa, "k");
    REQUIRE(other.backend.Add(Rid(0xaa), AsBytes(impostor)).Ok());
    REQUIRE(other.backend.Add(Rid(0xcc), AsBytes(clip_blob)).Ok());
    REQUIRE(other.backend.Add(Rid(0xbb), AsBytes(mesh_blob)).Ok());
    Handle<ClipView> bad_clip;
    Handle<MeshView> bad_mesh;
    CHECK(other.manager->Load<ClipView>(Rid(0xcc), &bad_clip).Is(ErrorCode::DependencyTypeMismatch));
    CHECK(other.manager->Load<MeshView>(Rid(0xbb), &bad_mesh).Is(ErrorCode::DependencyTypeMismatch));
    CHECK(other.manager->Stats().resident == 0);
}

// glTF coverage (2026-11-20): a clip that animates morph weights only has no skeleton and no dependency (Day 9). The
// manager loaded it as a skeletal clip and refused it ("the clip's skeleton is not among its loaded dependencies"), so
// a glTF with a weight animation did not open in the model viewer.
TEST_CASE("manager: a clip of morph weights only loads without a skeleton", "[asset][manager][skeletal]")
{
    MgrRig rig;
    const Array<f32, 2> times = {{0.0f, 1.0f}};
    const Array<f32, 2> values = {{0.0f, 1.0f}};
    ClipBuildInput input;
    input.duration = 1.0f;
    input.weights.target_count = 1;
    input.weights.times = Span<const f32>(times.Data(), times.Size());
    input.weights.values = Span<const f32>(values.Data(), values.Size());
    Vector<byte> blob;
    REQUIRE(BuildClipBlob(rig.types, Rid(0xcd), input, &blob).Ok());
    REQUIRE(rig.backend.Add(Rid(0xcd), AsBytes(blob)).Ok());
    Handle<ClipView> clip;
    REQUIRE(rig.manager->Load<ClipView>(Rid(0xcd), &clip).Ok());
    CHECK(clip.Get()->Skeleton().IsNone());
    CHECK(clip.Get()->WeightTargetCount() == 1);
    CHECK(rig.manager->Stats().loads == 1);
}

TEST_CASE("manager: a level loads as its summary with the closure it declares", "[asset][manager][level]")
{
    MgrRig rig;
    // A level that includes another level.
    const char* inner = "id 0000000000000020\nentity a\n";
    const char* outer = "id 0000000000000010\ninclude @0000000000000020\nentity b\n";
    storage::ComponentRegistry registry;
    REQUIRE(storage::RegisterBuiltinComponents(&registry).Ok());
    REQUIRE(RegisterLevelComponents(&registry).Ok());
    struct Resolver final : LevelReferenceResolver
    {
        ByteSpan blob;
        Status ResolvePath(StringView, ResourceId*, ByteSpan*) const override
        {
            return Status::Make(ErrorCode::NotFound, "no");
        }
        Status ResolveId(ResourceId id, ByteSpan* out) const override
        {
            if (id != Rid(0x20))
            {
                return Status::Make(ErrorCode::NotFound, "no");
            }
            *out = blob;
            return Status::Success();
        }
        Status PathOf(ResourceId, String<HeapAllocator>*) const override
        {
            return Status::Make(ErrorCode::NotFound, "no");
        }
    } resolver;
    Vector<byte> inner_blob;
    Vector<byte> outer_blob;
    LevelSourceError error;
    REQUIRE(MintLevel(StringView(inner, CStrLength(inner)), resolver, rig.types, registry, &inner_blob, &error).Ok());
    resolver.blob = AsBytes(inner_blob);
    REQUIRE(MintLevel(StringView(outer, CStrLength(outer)), resolver, rig.types, registry, &outer_blob, &error).Ok());
    REQUIRE(rig.backend.Add(Rid(0x20), AsBytes(inner_blob)).Ok());
    REQUIRE(rig.backend.Add(Rid(0x10), AsBytes(outer_blob)).Ok());

    Handle<LevelSummary> level;
    REQUIRE(rig.manager->Load<LevelSummary>(Rid(0x10), &level).Ok());
    REQUIRE(level.Get()->resources.Size() == 1);
    CHECK(level.Get()->resources[0].id == Rid(0x20));
    CHECK(level.Get()->resources[0].roles == kLevelRoleInclude);
    CHECK(level.Get()->entity_count == 1);
    CHECK(rig.manager->Stats().resident == 2);  // the included level came with it
    CHECK(rig.manager->Stats().load_conversions == 0);
}

TEST_CASE("manager: a replacement that would make a cycle is refused and the old version stays", "[asset][manager]")
{
    MgrRig rig;
    const Edge b_to_a[1] = {{1, 1}};
    rig.Add(1, 11);
    rig.Add(2, 22, b_to_a, 1);  // 2 depends on 1
    Handle<ChainObject> b;
    REQUIRE(rig.manager->Load<ChainObject>(Rid(2), &b).Ok());
    Vector<ReloadResult> results;
    REQUIRE(rig.manager->PollChanges(&results).Ok());
    const Edge a_to_b[1] = {{2, 1}};
    const Vector<byte> cyclic = ChainBlob(rig.types, 1, 12, a_to_b, 1);  // 1 would depend on 2, which depends on 1
    REQUIRE(rig.backend.Replace(Rid(1), AsBytes(cyclic)).Ok());
    REQUIRE(rig.manager->PollChanges(&results).Ok());
    REQUIRE(results.Size() == 1);
    CHECK(results[0].status.Is(ErrorCode::DependencyCycle));
    // The old resources are untouched and can still be released.
    b.Reset();
    CHECK(rig.manager->Stats().resident == 0);
    CHECK(rig.manager->Stats().bytes_resident == 0);
}

TEST_CASE("manager: a reload is held to the budget by its net size, not the old and the new together",
          "[asset][manager]")
{
    TypeRegistry probe;
    REQUIRE(probe.Register(kChainType, 1, "chain").Ok());
    const u64 one = ChainBlob(probe, 1, 1).Size();
    ManagerConfig config;
    config.cache_bytes = one + one / 2;  // room for one blob, not for two
    MgrRig rig(config);
    rig.Add(1, 1);
    Handle<ChainObject> handle;
    REQUIRE(rig.manager->Load<ChainObject>(Rid(1), &handle).Ok());
    const Vector<byte> same_size = ChainBlob(rig.types, 1, 2);
    REQUIRE(rig.backend.Replace(Rid(1), AsBytes(same_size)).Ok());
    Vector<ReloadResult> results;
    REQUIRE(rig.manager->PollChanges(&results).Ok());
    REQUIRE(results.Size() == 1);
    CHECK(results[0].status.Ok());
    CHECK(handle.Get()->value == 2);
}

TEST_CASE("manager: a type with no loader is a type mismatch, never a fallback", "[asset][manager]")
{
    ManagerConfig config;
    config.allow_fallbacks = true;
    MgrRig rig(config);
    const Vector<byte> fallback = ChainBlob(rig.types, 900, 1, nullptr, 0, kOtherType);
    REQUIRE(rig.manager->RegisterFallback(kOtherType, AsBytes(fallback)).Ok());
    rig.Add(1, 1, nullptr, 0, kOtherType);
    Handle<OtherObject> handle;
    CHECK(rig.manager->Load<OtherObject>(Rid(1), &handle).Is(ErrorCode::TypeMismatch));
    CHECK(rig.manager->Stats().fallbacks_used == 0);
}

TEST_CASE("manager: the time counter counts each read and each loader once", "[asset][manager]")
{
    MgrRig rig;
    const Edge to_b[1] = {{2, 1}};
    const Edge to_c[1] = {{3, 1}};
    rig.Add(3, 3);
    rig.Add(2, 2, to_c, 1);
    rig.Add(1, 1, to_b, 1);
    Handle<ChainObject> handle;
    const u64 before = os::MonotonicTimeNanos();
    REQUIRE(rig.manager->Load<ChainObject>(Rid(1), &handle).Ok());
    const u64 wall = os::MonotonicTimeNanos() - before;
    CHECK(rig.manager->Stats().read_nanos <= wall);  // nested timers used to add the closure to every parent
}

}  // namespace engine_placeholder::asset
