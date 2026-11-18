/* Phase 23 Day 351: the C API of a library built without the glTF importer (ENGINE_LIBRARY_WITH_IMPORTER=OFF): the
   capability bit is clear, and every import call fails with UNSUPPORTED (a result is zeroed, a ticket is never made)
   and the engine is otherwise whole. */
#include <stdio.h>
#include <stdlib.h>
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

/* The resource functions of the host cache work without the importer: the minted sample blobs (the texture and the
   material that depends on it) are added in one batch, read back byte for byte and loaded. */
static unsigned char* ReadBlob(const char* name, size_t* size)
{
    char path[1024];
    unsigned char* data;
    FILE* f;
    snprintf(path, sizeof(path), "%s/%s", RES_DATA_DIR, name);
    f = fopen(path, "rb");
    if (f == NULL)
    {
        printf("host_import_off: cannot open %s\n", path);
        ++g_failures;
        return NULL;
    }
    fseek(f, 0, SEEK_END);
    *size = (size_t)ftell(f);
    fseek(f, 0, SEEK_SET);
    data = (unsigned char*)malloc(*size);
    if (fread(data, 1, *size, f) != *size)
    {
        ++g_failures;
    }
    fclose(f);
    return data;
}

static void TestCache(const engine_api* api, engine_handle engine)
{
    size_t texture_size = 0;
    size_t material_size = 0;
    unsigned char* texture = ReadBlob("brick.tex.blob", &texture_size);
    unsigned char* material = ReadBlob("brick.mat.blob", &material_size);
    engine_resource_add_info infos[2];
    engine_resource_info mounted;
    engine_buffer blob;
    engine_handle loaded;
    engine_handle ticket;
    size_t failed = 99;
    if (texture == NULL || material == NULL)
    {
        return;
    }
    memset(infos, 0, sizeof(infos));
    infos[0].struct_size = sizeof(infos[0]);
    infos[0].id = 0xd01u;
    infos[0].path.data = "/cache/brick";
    infos[0].path.length = strlen("/cache/brick");
    infos[0].bytes = texture;
    infos[0].size = texture_size;
    infos[1].struct_size = sizeof(infos[1]);
    infos[1].id = 0xe01u;
    infos[1].bytes = material;
    infos[1].size = material_size;
    memset(&blob, 0, sizeof(blob));
    memset(&loaded, 0, sizeof(loaded));
    CHECK(api->import_format_version() != 0);
    CHECK(api->resource_add_many(engine, infos, 2, &failed) == ENGINE_OK);
    memset(&mounted, 0, sizeof(mounted));
    mounted.struct_size = sizeof(mounted);
    CHECK(api->resource_mounted(engine, 0xe01u, &mounted) == ENGINE_OK && mounted.size == material_size);
    CHECK(api->resource_read(engine, 0xe01u, 0, &blob) == ENGINE_OK && blob.size == material_size &&
          memcmp(blob.data, material, material_size) == 0);
    engine_buffer_release(&blob);
    CHECK(api->resource_load(engine, 0xe01u, infos[1].path, ENGINE_RESOURCE_MATERIAL, &loaded) != ENGINE_OK ||
          loaded.value != 0);
    if (loaded.value != 0)
    {
        CHECK(api->resource_release(loaded) == ENGINE_OK);
    }
    /* a batch that has one live id adds nothing */
    CHECK(api->resource_add_many(engine, infos, 2, &failed) == ENGINE_ERROR_ALREADY_EXISTS && failed == 0);
    CHECK(api->resource_remove(engine, 0xe01u, 0, NULL) == ENGINE_OK);
    CHECK(api->resource_remove(engine, 0xd01u, 0, NULL) == ENGINE_OK);
    /* on a worker, too */
    memset(&ticket, 0, sizeof(ticket));
    CHECK(api->resource_add_many_async(engine, infos, 2, &ticket) == ENGINE_OK);
    CHECK(api->resource_add_wait(ticket, 0xFFFFFFFFFFFFFFFFull) == ENGINE_OK);
    CHECK(api->resource_add_finish(ticket, &failed) == ENGINE_OK);
    CHECK(api->resource_mounted(engine, 0xd01u, &mounted) == ENGINE_OK);
    free(texture);
    free(material);
}

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
    CHECK((info.caps & ENGINE_CAP_RESOURCE_CACHE) != 0);
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
    TestCache(api, engine);
    CHECK(api->destroy(engine) == ENGINE_OK); /* no object of the engine is left */
    if (g_failures != 0)
    {
        return 1;
    }
    printf("host_import_off: ok\n");
    return 0;
}
