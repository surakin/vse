/* Phase 23 Day 343 (Day 346: through the function table): a C program linked at build time against the shared engine
 * library: runs jobs inside it. */
#include <stdio.h>

#include "engine/engine_c.h"
#include "engine_c/engine_test_extension.h"

int main(void)
{
    const engine_api* api = NULL;
    if (engine_get_api(ENGINE_ABI_VERSION_MAJOR, ENGINE_ABI_VERSION_MINOR, sizeof(engine_api), &api) != ENGINE_OK)
    {
        printf("shared_link_probe: no function table\n");
        return 2;
    }
    const engine_string name = {"engine.test_state", sizeof("engine.test_state") - 1};
    const struct engine_test_extension* state = (const struct engine_test_extension*)api->get_extension(name);
    if (state == NULL)
    {
        printf("shared_link_probe: no test extension\n");
        return 2;
    }
    const int ran = state->run_jobs(1000) + state->run_jobs(1000);
    printf("shared_link_probe: %d jobs ran\n", ran);
    return ran == 2000 ? 0 : 1;
}
