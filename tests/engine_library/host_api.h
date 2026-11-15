/* Phase 23 Day 346: what the C hosts of tests/engine_library share. A foreign host reaches the engine through one
   symbol, `engine_get_api`, and then the table: this finds both in a library it loaded with dlopen, and the test
   extension (`engine.test_state`) the library's test build carries. */
#ifndef HOST_API_H
#define HOST_API_H

#include "host_os.h"

#include <string.h>

#include "engine/engine_c.h"
#include "engine_c/engine_test_extension.h"

typedef engine_status (*host_get_api_fn)(uint32_t, uint32_t, size_t, const engine_api**);

/* The function table of the library `lib` (a handle from host_lib_open), or NULL. */
static const engine_api* host_get_api(void* lib)
{
    host_get_api_fn get = NULL;
    void* symbol = lib != NULL ? host_lib_symbol(lib, "engine_get_api") : NULL;
    memcpy(&get, &symbol, sizeof(get)); /* object pointer to function pointer without a cast ISO C forbids */
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
