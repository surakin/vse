/* Phase 23 Day 346: what the C hosts of tests/engine_library share. A foreign host reaches the engine through one symbol, `engine_get_api`, and then the
   table: this finds both in a library it loaded with dlopen, and the test extension (`engine.test_state`) the library's test build carries. */
#ifndef HOST_API_H
#define HOST_API_H

#include <dlfcn.h>

#include "engine_c/engine_c.h"
#include "engine_c/engine_test_extension.h"

typedef engine_status (*host_get_api_fn)(uint32_t, uint32_t, size_t, const engine_api**);

/* The function table of the library `lib` (a dlopen handle), or NULL. */
static const engine_api* host_get_api(void* lib)
{
    host_get_api_fn get = lib != NULL ? (host_get_api_fn)dlsym(lib, "engine_get_api") : NULL;
    const engine_api* table = NULL;
    if (get == NULL || get(ENGINE_ABI_VERSION_MAJOR, ENGINE_ABI_VERSION_MINOR, sizeof(engine_api), &table) != ENGINE_OK)
    {
        return NULL;
    }
    return table;
}

/* The state table of the boundary tests, or NULL when the library does not carry the extension. */
static const struct engine_test_extension* host_test_extension(void* lib)
{
    const engine_api* api = host_get_api(lib);
    if (api == NULL || api->struct_size < sizeof(engine_api))
    {
        return NULL;
    }
    engine_string name = {"engine.test_state", sizeof("engine.test_state") - 1};
    return (const struct engine_test_extension*)api->get_extension(name);
}

#endif
