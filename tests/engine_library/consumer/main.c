/* The installed-engine consumer: includes only the public headers and links the pre-built library. */
#include <stdio.h>

#include "engine/engine_c.h"

int main(void)
{
    const engine_api* api = NULL;
    engine_handle engine = {0};
    engine_create_info info = {0};
    engine_info about = {0};
    if (engine_get_api(ENGINE_ABI_VERSION_MAJOR, ENGINE_ABI_VERSION_MINOR, sizeof(engine_api), &api) != ENGINE_OK)
    {
        printf("consumer: no function table\n");
        return 2;
    }
    info.struct_size = sizeof info;
    about.struct_size = sizeof about;
    if (api->create(&info, &engine) != ENGINE_OK || api->get_info(engine, &about) != ENGINE_OK)
    {
        printf("consumer: create failed\n");
        return 2;
    }
    printf("consumer: engine %u.%u, %u settings\n", (unsigned)ENGINE_ABI_VERSION_MAJOR,
           (unsigned)ENGINE_ABI_VERSION_MINOR, (unsigned)about.setting_count);
    return api->destroy(engine) == ENGINE_OK ? 0 : 1;
}
