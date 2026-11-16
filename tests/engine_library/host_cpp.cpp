// A C++ program as a host of the C API through engine.h (the thin RAII layer): the engine, a world and a counter are
// destroyed by their owners, a moved object owns nothing, a buffer is released once, `Init` sets the struct sizes, and
// a failing call returns its status. The same calls through the bare table are the other host_*.c programs.
#include <stdio.h>

#include "engine/engine.h"

static int g_failures = 0;

#define CHECK(condition)                                                                                               \
    do                                                                                                                 \
    {                                                                                                                  \
        if (!(condition))                                                                                              \
        {                                                                                                              \
            printf("host_cpp: line %d: CHECK failed: %s\n", __LINE__, #condition);                                     \
            ++g_failures;                                                                                              \
        }                                                                                                              \
    } while (0)

using namespace engine_host;

static void CountJob(void* user)
{
    *static_cast<int*>(user) = 7;
}

// The rest of the table through the layer: components, names, parents, queries, physics, counters and jobs.
static void RunRest(const engine_api* api, Engine& engine)
{
    World world;
    CHECK(engine.CreateWorld(&world));
    uint64_t a = 0;
    uint64_t b = 0;
    CHECK(world.Entity(&a) && world.Entity(&b));
    engine_transform t = Init<engine_transform>();
    t.rotation[3] = 1.0f;
    t.scale[0] = t.scale[1] = t.scale[2] = 1.0f;
    t.position[1] = 2.0f;
    CHECK(world.SetComponent(a, ENGINE_COMPONENT_TRANSFORM, &t));
    bool has = false;
    CHECK(world.HasComponent(a, ENGINE_COMPONENT_TRANSFORM, &has) && has);
    CHECK(world.HasComponent(b, ENGINE_COMPONENT_TRANSFORM, &has) && !has);
    engine_transform back = Init<engine_transform>();
    CHECK(world.GetComponent(a, ENGINE_COMPONENT_TRANSFORM, &back) && back.position[1] == 2.0f);
    uint64_t keys[4] = {};
    size_t total = 0;
    CHECK(world.Query(ENGINE_COMPONENT_BIT(ENGINE_COMPONENT_TRANSFORM), keys, 4, &total) && total == 1 && keys[0] == a);
    CHECK(world.RemoveComponent(a, ENGINE_COMPONENT_TRANSFORM));
    CHECK(world.HasComponent(a, ENGINE_COMPONENT_TRANSFORM, &has) && !has);

    CHECK(world.SetName(a, "parent") && world.SetName(b, "child"));
    Buffer name;
    CHECK(world.GetName(a, &name) && name.Equals("parent"));
    uint64_t found = 0;
    CHECK(world.Find("child", &found) && found == b);
    CHECK(world.FindAll("child", keys, 4, &total) && total == 1);
    CHECK(world.SetParent(b, a));
    uint64_t parent = 0;
    CHECK(world.GetParent(b, &parent) && parent == a);

    const float gravity[3] = {0.0f, -3.0f, 0.0f};
    CHECK(world.SetGravity(gravity));
    const float origin[3] = {0.0f, 5.0f, 0.0f};
    const float down[3] = {0.0f, -1.0f, 0.0f};
    engine_ray_hit hit;
    bool hit_any = true;
    CHECK(world.RayCast(origin, down, 100.0f, 0xFFFFFFFFu, &hit, &hit_any) && !hit_any);
    CHECK(world.OverlapSphere(origin, 1.0f, 0xFFFFFFFFu, keys, 4, &total) && total == 0);
    engine_physics_event events[2];
    CHECK(world.PhysicsEvents(events, 2, &total) && total == 0);
    Buffer snapshot;
    CHECK(world.PhysicsSnapshot(&snapshot) && snapshot.Size() > 0);
    CHECK(world.PhysicsRestore(snapshot.Data(), snapshot.Size()));
    CHECK(world.PhysicsRestore(snapshot.Data(), 3) != ENGINE_OK);

    Counter counter;
    CHECK(engine.CreateCounter(&counter));
    int ran = 0;
    CHECK(engine.SubmitJob(&CountJob, &ran, counter));
    CHECK(counter.Wait(5000));
    uint64_t pending = 1;
    CHECK(counter.Value(&pending) && pending == 0 && ran == 7);
    CHECK(GetExtension(api, "no_such_extension") == nullptr);
    ThreadDetach(api);

    Buffer none;
    CHECK(engine.ReplaceResource(Init<engine_resource_add_info>(), &none) != ENGINE_OK);
    engine_resource_info mounted;
    CHECK(engine.MountedResource(0x1234, &mounted) != ENGINE_OK);
    Resource resource;
    CHECK(engine.LoadResource(0x1234, "no/such/path", 0, &resource) != ENGINE_OK && !resource);
}

int main()
{
    const engine_api* api = nullptr;
    if (engine_get_api(ENGINE_ABI_VERSION_MAJOR, ENGINE_ABI_VERSION_MINOR, sizeof(engine_api), &api) != ENGINE_OK)
    {
        printf("host_cpp: no table\n");
        return 1;
    }

    // Init sets the size and zeroes the rest.
    const engine_create_info info = Init<engine_create_info>();
    CHECK(info.struct_size == sizeof(engine_create_info) && info.flags == 0 && info.setting_count == 0);

    {
        Engine engine;
        CHECK(!engine);
        CHECK(Engine::Create(api, info, &engine));
        CHECK(engine);

        // A second engine is refused with the C API's status, and owns nothing.
        Engine second;
        const Status refused = Engine::Create(api, info, &second);
        CHECK(!refused && refused == ENGINE_ERROR_LIMIT_EXCEEDED && !second);
        CHECK(Equals(refused.Name(api), "limit_exceeded"));

        engine_info reported = {};
        CHECK(engine.Info(&reported) && reported.struct_size == sizeof(engine_info));

        // A buffer is filled, read and released by its owner; a reused buffer releases the old contents first.
        CHECK(engine.SettingSet("rhi.pipeline_cache", "false"));
        Buffer value;
        CHECK(engine.SettingGet("rhi.pipeline_cache", &value) && value.Equals("false"));
        CHECK(engine.SettingGet("rhi.pipeline_cache", &value) && value.Equals("false"));
        CHECK(engine.SettingGet("no.such.key", &value) == ENGINE_ERROR_NOT_FOUND);
        Buffer message;
        CHECK(LastError(api, &message) && message.Size() > 0);
        Buffer moved(static_cast<Buffer&&>(message));
        CHECK(message.Size() == 0 && moved.Size() > 0);

        // A world is destroyed before the engine (declared after it); moving it transfers the one ownership.
        World world;
        CHECK(engine.CreateWorld(&world) && world);
        uint64_t key = 0;
        CHECK(world.Entity(&key) && key != 0);
        World other(static_cast<World&&>(world));
        CHECK(!world && other);
        CHECK(other.DestroyEntity(key));
        CHECK(other.DestroyEntity(key) == ENGINE_ERROR_STALE_HANDLE);

        // A released handle is the caller's: the table destroys it.
        World doomed;
        CHECK(engine.CreateWorld(&doomed));
        const engine_handle raw = doomed.Release();
        CHECK(!doomed && raw.value != 0);
        CHECK(api->world_destroy(raw) == ENGINE_OK);

        // Destroy() reports; a second Destroy of the same owner is a no-op.
        CHECK(other.Destroy() == ENGINE_OK && !other);
        CHECK(other.Destroy() == ENGINE_OK);

        Counter counter;
        CHECK(engine.CreateCounter(&counter) && counter);
        RunRest(api, engine);

        engine_frame_stats stats = {};
        engine_frame_info frame = Init<engine_frame_info>();
        frame.delta_seconds = 0.016;
        CHECK(engine.Frame(frame, &stats) && stats.struct_size == sizeof(engine_frame_stats));
        frame.delta_seconds = -1.0;
        CHECK(engine.Frame(frame) == ENGINE_ERROR_INVALID_ARGUMENT);
    }

    // Everything is gone: a new engine can be made again.
    {
        Engine again;
        CHECK(Engine::Create(api, info, &again));
        World world;
        CHECK(again.CreateWorld(&world));
        // The engine refuses while a world lives and the owner keeps it: after the world goes, Destroy succeeds.
        CHECK(again.Destroy() == ENGINE_ERROR_IN_USE && again);
        CHECK(world.Destroy() == ENGINE_OK);
        CHECK(again.Destroy() == ENGINE_OK && !again);
    }

    if (g_failures != 0)
    {
        printf("host_cpp: %d failures\n", g_failures);
        return 1;
    }
    printf("host_cpp: ok\n");
    return 0;
}
