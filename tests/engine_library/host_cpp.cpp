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
