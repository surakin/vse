/* Phase 23 Day 355: the translation unit of the iOS compile check (ios_header_check.cmake), compiled as C and as C++: a
   host's file that includes the C API's header and makes one call through the table. */
#include "engine/engine_c.h"

int ios_host_status_is_ok(const engine_api* api, engine_handle engine);

int ios_host_status_is_ok(const engine_api* api, engine_handle engine)
{
    engine_info info = {sizeof(engine_info), 0, 0, 0};
    return api->get_info(engine, &info) == ENGINE_OK;
}
