/* Phase 23 Day 347: a C host that loads the engine library at run time through engine_loader.h: it checks every refusal
   (a missing file, a file that is not a library, a library without the entry point, another major, a table that lies, a
   minor newer than the library's), then loads the real library, checks the table, creates an engine, runs jobs through
   the test extension (the frame loop arrives on Day 349), destroys the engine, unloads, and checks that the library
   left the address space. */
#include <dlfcn.h>
#include <stdio.h>
#include <string.h>

#include "engine_c/engine_c.h"
#include "engine_c/engine_loader.h"
#include "engine_c/engine_test_extension.h"

static int g_failures = 0;

#define CHECK(condition)                                                                                               \
    do                                                                                                                 \
    {                                                                                                                  \
        if (!(condition))                                                                                              \
        {                                                                                                              \
            printf("host_dl: line %d: CHECK failed: %s\n", __LINE__, #condition);                                      \
            ++g_failures;                                                                                              \
        }                                                                                                              \
    } while (0)

/* A refusal: the status is the documented one, the message names what is wrong, nothing is left loaded. */
static void Refused(const char* path, uint32_t major, uint32_t minor, engine_status expected, const char* in_message)
{
    engine_loader loader;
    const engine_status status = engine_load(path, major, minor, &loader);
    if (status != expected || strstr(loader.message, in_message) == NULL || loader.library != NULL ||
        loader.api != NULL)
    {
        printf("host_dl: refusal of %s: status %d (expected %d), message \"%s\" (expected to contain \"%s\")\n", path,
               (int)status, (int)expected, loader.message, in_message);
        ++g_failures;
    }
    CHECK(engine_unload(&loader) == ENGINE_OK); /* unloading what is not loaded is harmless */
}

int main(void)
{
    engine_loader loader;
    engine_create_info info;
    engine_handle engine;
    engine_buffer value;
    const struct engine_test_extension* state;
    engine_string name = {"engine.test_state", sizeof("engine.test_state") - 1};
    engine_setting_entry entry = {{"rhi.pipeline_cache", 18}, {"true", 4}};

    Refused("/nonexistent/libengine.so", ENGINE_ABI_VERSION_MAJOR, ENGINE_ABI_VERSION_MINOR, ENGINE_ERROR_IO,
            "cannot load");
    Refused(NOT_A_LIBRARY_PATH, ENGINE_ABI_VERSION_MAJOR, ENGINE_ABI_VERSION_MINOR, ENGINE_ERROR_IO, "cannot load");
    Refused("", ENGINE_ABI_VERSION_MAJOR, ENGINE_ABI_VERSION_MINOR, ENGINE_ERROR_INVALID_ARGUMENT, "no library path");
    Refused(FAKE_NO_ENTRY_PATH, ENGINE_ABI_VERSION_MAJOR, ENGINE_ABI_VERSION_MINOR, ENGINE_ERROR_UNSUPPORTED,
            "no engine_get_api");
    Refused(FAKE_OTHER_MAJOR_PATH, ENGINE_ABI_VERSION_MAJOR, ENGINE_ABI_VERSION_MINOR, ENGINE_ERROR_UNSUPPORTED_VERSION,
            "refuses ABI");
    Refused(FAKE_LYING_TABLE_PATH, ENGINE_ABI_VERSION_MAJOR, ENGINE_ABI_VERSION_MINOR, ENGINE_ERROR_UNSUPPORTED_VERSION,
            "returned a table for ABI 2.0");
    /* The real library, asked for a newer minor than it has, and for another major. */
    Refused(ENGINE_LIB_PATH, ENGINE_ABI_VERSION_MAJOR, ENGINE_ABI_VERSION_MINOR + 1, ENGINE_ERROR_UNSUPPORTED_VERSION,
            "refuses ABI");
    Refused(ENGINE_LIB_PATH, ENGINE_ABI_VERSION_MAJOR + 1, 0, ENGINE_ERROR_UNSUPPORTED_VERSION, "refuses ABI");
    CHECK(engine_load(ENGINE_LIB_PATH, 1, 0, NULL) == ENGINE_ERROR_INVALID_ARGUMENT);
    CHECK(engine_unload(NULL) == ENGINE_ERROR_INVALID_ARGUMENT);
    /* None of the refusals left a library behind. */
    CHECK(dlopen(FAKE_OTHER_MAJOR_PATH, RTLD_NOW | RTLD_NOLOAD) == NULL);
    CHECK(dlopen(ENGINE_LIB_PATH, RTLD_NOW | RTLD_NOLOAD) == NULL);

    /* The real library. */
    if (engine_load(ENGINE_LIB_PATH, ENGINE_ABI_VERSION_MAJOR, ENGINE_ABI_VERSION_MINOR, &loader) != ENGINE_OK)
    {
        printf("host_dl: cannot load the engine: %s\n", loader.message);
        return 2;
    }
    CHECK(loader.api->abi_major == ENGINE_ABI_VERSION_MAJOR && loader.api->abi_minor == ENGINE_ABI_VERSION_MINOR);
    CHECK(loader.api->struct_size == sizeof(engine_api));

    memset(&info, 0, sizeof(info));
    info.struct_size = sizeof(info);
    info.settings = &entry;
    info.setting_count = 1;
    CHECK(loader.api->create(&info, &engine) == ENGINE_OK);
    memset(&value, 0, sizeof(value));
    CHECK(loader.api->setting_get(engine, entry.key, &value) == ENGINE_OK && value.size == 4 &&
          memcmp(value.data, "true", 4) == 0);
    engine_buffer_release(&value);

    /* "A headless frame": jobs through the extension, until the frame loop exists (Day 349). */
    state = (const struct engine_test_extension*)loader.api->get_extension(name);
    CHECK(state != NULL && state->run_jobs(200) == 200);

    CHECK(loader.api->destroy(engine) == ENGINE_OK);
    loader.api->thread_detach();
    CHECK(engine_unload(&loader) == ENGINE_OK);
    CHECK(loader.api == NULL && loader.library == NULL);
    CHECK(dlopen(ENGINE_LIB_PATH, RTLD_NOW | RTLD_NOLOAD) == NULL); /* the library left the address space */

    if (g_failures != 0)
    {
        printf("host_dl: %d checks failed\n", g_failures);
        return 1;
    }
    printf("host_dl: ok\n");
    return 0;
}
