/* Phase 23 Day 351: the C API of a library built without the glTF importer (ENGINE_LIBRARY_WITH_IMPORTER=OFF): the
   capability bit is clear, and every import call fails with UNSUPPORTED (a result is zeroed, a ticket is never made)
   and the engine is otherwise whole. */
#include <stdio.h>
#include <string.h>

#include "host_os.h"
#include "engine/engine_c.h"

static int g_failures = 0;

#define CHECK(condition)                                                                                               \
    do                                                                                                                 \
    {                                                                                                                  \
        if (!(condition))                                                                                              \
        {                                                                                                              \
            printf("host_import_off: line %d: CHECK failed: %s\n", __LINE__, #condition);                              \
            ++g_failures;                                                                                              \
        }                                                                                                              \
    } while (0)

int main(void)
{
    const engine_api* api = NULL;
    engine_create_info create;
    engine_handle engine;
    engine_info info;
    engine_import_options options;
    engine_import_result result;
    engine_handle ticket;
    engine_import_info poll;
    engine_resource_info unused;
    const char text[] = "{}";
    host_quiet_failures(); /* Windows: report failures on stderr, never in a dialog */
    (void)unused;
    if (engine_get_api(ENGINE_ABI_VERSION_MAJOR, ENGINE_ABI_VERSION_MINOR, sizeof(engine_api), &api) != ENGINE_OK)
    {
        return 2;
    }
    memset(&create, 0, sizeof(create));
    create.struct_size = sizeof(create);
    create.flags = ENGINE_CREATE_LOG_SUPPRESS_CONSOLE;
    memset(&engine, 0, sizeof(engine));
    CHECK(api->create(&create, &engine) == ENGINE_OK);
    memset(&info, 0, sizeof(info));
    info.struct_size = sizeof(info);
    CHECK(api->get_info(engine, &info) == ENGINE_OK);
    CHECK((info.caps & ENGINE_CAP_IMPORT_GLTF) == 0);
    memset(&options, 0, sizeof(options));
    options.struct_size = sizeof(options);
    memset(&result, 0, sizeof(result));
    result.struct_size = sizeof(result);
    CHECK(api->import_gltf(engine, text, sizeof(text) - 1, &options, &result) == ENGINE_ERROR_UNSUPPORTED);
    CHECK(result.resource_count == 0 && result.ids.size == 0 && result.manifest.size == 0);
    memset(&ticket, 0, sizeof(ticket));
    CHECK(api->import_gltf_async(engine, text, sizeof(text) - 1, &options, &ticket) == ENGINE_ERROR_UNSUPPORTED);
    CHECK(ticket.value == 0);
    memset(&poll, 0, sizeof(poll));
    poll.struct_size = sizeof(poll);
    CHECK(api->import_poll(ticket, &poll) != ENGINE_OK);
    CHECK(api->destroy(engine) == ENGINE_OK); /* no object of the engine is left */
    if (g_failures != 0)
    {
        return 1;
    }
    printf("host_import_off: ok\n");
    return 0;
}
