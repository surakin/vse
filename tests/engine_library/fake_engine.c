/* Phase 23 Day 347: stand-ins for engine libraries that must be refused (the refusal tests of host_dl.c). One source,
   three behaviors: FAKE_MODE 1: a library of another major version (refuses the version the host asks for, as a real
   one would); FAKE_MODE 2: a library that returns a table of another major although it says OK (the loader's own
   check); FAKE_MODE 3: a library with no engine_get_api at all. */
#include "engine_c/engine_c.h"

#if FAKE_MODE == 3
int engine_not_the_entry_point(void)
{
    return 3;
}
#else
static const engine_api g_table = {sizeof(engine_api), 2, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0};

engine_status engine_get_api(uint32_t wanted_major, uint32_t wanted_minor, size_t table_size, const engine_api** out)
{
    (void)wanted_minor;
    (void)table_size;
    *out = NULL;
#if FAKE_MODE == 1
    (void)wanted_major;
    return ENGINE_ERROR_UNSUPPORTED_VERSION;
#else
    (void)wanted_major;
    *out = &g_table;
    return ENGINE_OK;
#endif
}
#endif
