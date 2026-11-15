/* Phase 23 Day 354: a host built with another C runtime than the engine library's loads it and uses it. On Windows the
   program is built with the static runtime (/MT) and engine.dll with the DLL runtime (/MD): two heaps, two sets of
   stdio, two errno. Nothing crosses between them: the host's bytes are copied into the engine, the engine's memory
   comes back as an engine_buffer released through the library's own function, and no FILE* or allocation passes the
   boundary. On Linux it runs the same against libengine.so (one C runtime there: the test is the same calls). It runs
   the headless frame, the memory test and the resource test of Day 350 through the loader. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "host_os.h"
#include "engine/engine_c.h"
#include "engine/engine_loader.h"
#include "engine_c/engine_test_extension.h"

#define TEXTURE_ID 0xd01u

static int g_failures = 0;

#define CHECK(condition)                                                                                               \
    do                                                                                                                 \
    {                                                                                                                  \
        if (!(condition))                                                                                              \
        {                                                                                                              \
            printf("host_crt: line %d: CHECK failed: %s\n", __LINE__, #condition);                                     \
            ++g_failures;                                                                                              \
        }                                                                                                              \
    } while (0)

static engine_string S(const char* text)
{
    engine_string s;
    s.data = text;
    s.length = strlen(text);
    return s;
}

/* The blob, read with this program's own C runtime into this program's own heap. */
static unsigned char* ReadBlob(const char* path, size_t* size)
{
    unsigned char* data;
    long length;
    FILE* f = fopen(path, "rb");
    *size = 0;
    if (f == NULL)
    {
        printf("host_crt: cannot open %s\n", path);
        return NULL;
    }
    fseek(f, 0, SEEK_END);
    length = ftell(f);
    fseek(f, 0, SEEK_SET);
    data = length > 0 ? (unsigned char*)malloc((size_t)length) : NULL;
    if (data != NULL && fread(data, 1, (size_t)length, f) == (size_t)length)
    {
        *size = (size_t)length;
    }
    fclose(f);
    return data;
}

int main(void)
{
    engine_loader loader;
    engine_create_info info;
    engine_handle engine, texture;
    engine_frame_info frame;
    engine_frame_stats stats;
    engine_buffer value;
    engine_resource_add_info add;
    engine_resource_info status;
    const struct engine_test_extension* state;
    engine_string name = {"engine.test_state", sizeof("engine.test_state") - 1};
    char path[1024];
    unsigned char* blob;
    size_t blob_size = 0;
    void* block;
    int i;
    host_quiet_failures(); /* Windows: report failures on stderr, never in a dialog */

    if (engine_load(ENGINE_LIB_PATH, ENGINE_ABI_VERSION_MAJOR, ENGINE_ABI_VERSION_MINOR, &loader) != ENGINE_OK)
    {
        printf("host_crt: %s\n", loader.message);
        return 2;
    }
    memset(&info, 0, sizeof(info));
    info.struct_size = sizeof(info);
    info.flags = ENGINE_CREATE_LOG_SUPPRESS_CONSOLE;
    CHECK(loader.api->create(&info, &engine) == ENGINE_OK);

    /* The headless frame. */
    memset(&frame, 0, sizeof(frame));
    frame.struct_size = sizeof(frame);
    frame.delta_seconds = 1.0 / 60.0;
    for (i = 0; i < 30; ++i)
    {
        memset(&stats, 0, sizeof(stats));
        stats.struct_size = sizeof(stats);
        CHECK(loader.api->frame(engine, &frame, &stats) == ENGINE_OK);
    }

    /* Memory: what the engine allocates goes back to the engine, through its own functions. */
    memset(&value, 0, sizeof(value));
    CHECK(loader.api->setting_get(engine, S("rhi.pipeline_cache"), &value) == ENGINE_OK && value.size > 0);
    engine_buffer_release(&value);
    state = (const struct engine_test_extension*)loader.api->get_extension(name);
    CHECK(state != NULL);
    if (state != NULL)
    {
        block = state->allocate(4096);
        CHECK(block != NULL);
        if (block != NULL)
        {
            memset(block, 0x5a, 4096);
            state->release(block, 4096);
        }
        CHECK(state->live_blocks() == 0);
    }

    /* Resources: the host's bytes are copied in, then the host frees its own buffer with its own runtime. */
    snprintf(path, sizeof(path), "%s/brick.tex.blob", RES_DATA_DIR);
    blob = ReadBlob(path, &blob_size);
    CHECK(blob != NULL && blob_size > 0);
    if (blob != NULL)
    {
        memset(&add, 0, sizeof(add));
        add.struct_size = sizeof(add);
        add.id = TEXTURE_ID;
        add.path = S("/textures/brick");
        add.bytes = blob;
        add.size = blob_size;
        CHECK(loader.api->resource_add(engine, &add) == ENGINE_OK);
        memset(blob, 0, blob_size); /* the engine holds its own copy */
        free(blob);
        memset(&texture, 0, sizeof(texture));
        CHECK(loader.api->resource_load(engine, TEXTURE_ID, S(""), ENGINE_RESOURCE_TEXTURE, &texture) == ENGINE_OK);
        memset(&status, 0, sizeof(status));
        status.struct_size = sizeof(status);
        CHECK(loader.api->resource_status(texture, &status) == ENGINE_OK && status.id == TEXTURE_ID &&
              status.type == ENGINE_RESOURCE_TEXTURE && status.loaded == 1 && status.size == blob_size);
        CHECK(loader.api->resource_release(texture) == ENGINE_OK);
        CHECK(loader.api->resource_remove(engine, TEXTURE_ID, 0, NULL) == ENGINE_OK);
    }

    CHECK(loader.api->destroy(engine) == ENGINE_OK);
    loader.api->thread_detach();
    CHECK(engine_unload(&loader) == ENGINE_OK);
    CHECK(!host_lib_is_loaded(ENGINE_LIB_PATH));
    if (g_failures != 0)
    {
        printf("host_crt: %d checks failed\n", g_failures);
        return 1;
    }
    printf("host_crt: ok\n");
    return 0;
}
