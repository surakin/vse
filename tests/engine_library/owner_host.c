/* Phase 23 Day 345: nothing allocated by the engine is freed by the host's allocator. A buffer the engine hands out is
   released through the engine's own function. The planted variant (OWNER_WRONG_FREE) frees it with free(): the engine
   still counts the block as live. (In the sanitized flavor ASan may also report an allocator mismatch; the engine
   library binds its own operator new, so the count is the check that does not depend on it.) */
#include <dlfcn.h>
#include <stdio.h>
#include <stdlib.h>

#include "host_api.h"

int main(void)
{
    void* engine = dlopen(ENGINE_LIB_PATH, RTLD_NOW | RTLD_LOCAL);
    const struct engine_test_extension* table = host_test_extension(engine);
    if (table == NULL)
    {
        printf("no state probe: %s\n", dlerror());
        return 2;
    }
    void* block = table->allocate(4096);
    if (block == NULL)
    {
        return 2;
    }
#ifdef OWNER_WRONG_FREE
    free(block); /* the host's allocator must not free what the engine allocated */
#else
    table->release(block, 4096);
#endif
    /* The engine's own count is the check: a block taken back by anyone else is still live in the engine. */
    const long live = table->live_blocks();
    if (live != 0)
    {
        printf("owner_host: %ld block(s) the engine allocated were not returned to the engine\n", live);
        return 1;
    }
    printf("owner_host: released\n");
    return 0;
}
