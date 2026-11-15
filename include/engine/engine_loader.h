/* engine_loader.h: loads the engine library at run time and returns its function table (Phase 23 Day 347). Header-only
 * plain C with no engine dependency but engine_c.h: a host copies this file and engine_c.h and nothing else. POSIX
 * (`dlopen`) today; the Windows variant arrives with Day 354.
 *
 *     engine_loader loader;
 *     if (engine_load("libengine.so.1", ENGINE_ABI_VERSION_MAJOR, ENGINE_ABI_VERSION_MINOR, &loader) != ENGINE_OK)
 *         fprintf(stderr, "%s\n", loader.message);
 *     else
 *         ... loader.api->create(...) ...
 *     engine_unload(&loader);
 *
 * Every refusal leaves a sentence in `loader.message` and loads nothing: a missing or damaged file (ENGINE_ERROR_IO), a
 * library without `engine_get_api` (ENGINE_ERROR_UNSUPPORTED), another major version or a newer minor than the library
 * has (ENGINE_ERROR_UNSUPPORTED_VERSION), a table that does not say what it should (ENGINE_ERROR_UNSUPPORTED_VERSION).
 * Unload only after destroying every engine and releasing every buffer. */
#ifndef ENGINE_LOADER_H
#define ENGINE_LOADER_H

#include <dlfcn.h>
#include <string.h>

#include "engine_c.h"

typedef struct engine_loader
{
    uint32_t struct_size;
    void* library;         /* the dlopen handle, NULL when nothing is loaded */
    const engine_api* api; /* the function table, NULL when nothing is loaded */
    char message[256];     /* why the last engine_load refused, or empty */
} engine_loader;

/* Appends `text` to the loader's message, cut to fit. */
static inline void engine_loader_append(engine_loader* loader, const char* text)
{
    size_t used = strlen(loader->message);
    size_t room = sizeof(loader->message) - 1 - used;
    size_t n = strlen(text);
    if (n > room)
    {
        n = room;
    }
    memcpy(loader->message + used, text, n);
    loader->message[used + n] = '\0';
}

static inline void engine_loader_append_uint(engine_loader* loader, unsigned long value)
{
    char digits[24];
    size_t i = sizeof(digits) - 1;
    digits[i] = '\0';
    do
    {
        digits[--i] = (char)('0' + value % 10);
        value /= 10;
    } while (value != 0);
    engine_loader_append(loader, digits + i);
}

/* Loads the library at `path` and checks that it serves ABI `wanted_major`.`wanted_minor`. On success `out->api` is the
 * table and `out->library` the handle; on any failure nothing stays loaded. Fails with: INVALID_ARGUMENT, IO,
 * UNSUPPORTED, UNSUPPORTED_VERSION. */
static inline engine_status engine_load(const char* path, uint32_t wanted_major, uint32_t wanted_minor,
                                        engine_loader* out)
{
    typedef engine_status (*get_api_fn)(uint32_t, uint32_t, size_t, const engine_api**);
    void* symbol;
    get_api_fn get_api;
    const engine_api* api = NULL;
    engine_status status;
    void* library;
    if (out == NULL)
    {
        return ENGINE_ERROR_INVALID_ARGUMENT;
    }
    memset(out, 0, sizeof(*out));
    out->struct_size = (uint32_t)sizeof(*out);
    if (path == NULL || path[0] == '\0')
    {
        engine_loader_append(out, "no library path given");
        return ENGINE_ERROR_INVALID_ARGUMENT;
    }
    library = dlopen(path, RTLD_NOW | RTLD_LOCAL);
    if (library == NULL)
    {
        const char* reason = dlerror();
        engine_loader_append(out, "cannot load ");
        engine_loader_append(out, path);
        engine_loader_append(out, ": ");
        engine_loader_append(out, reason != NULL ? reason : "unknown reason");
        return ENGINE_ERROR_IO;
    }
    symbol = dlsym(library, "engine_get_api");
    if (symbol == NULL)
    {
        engine_loader_append(out, path);
        engine_loader_append(out, " is not an engine library: it has no engine_get_api");
        dlclose(library);
        return ENGINE_ERROR_UNSUPPORTED;
    }
    memcpy(&get_api, &symbol, sizeof(get_api)); /* object pointer to function pointer without a cast ISO C forbids */
    status = get_api(wanted_major, wanted_minor, sizeof(engine_api), &api);
    if (status != ENGINE_OK || api == NULL)
    {
        engine_loader_append(out, path);
        engine_loader_append(out, " refuses ABI ");
        engine_loader_append_uint(out, wanted_major);
        engine_loader_append(out, ".");
        engine_loader_append_uint(out, wanted_minor);
        engine_loader_append(out, " (status ");
        engine_loader_append_uint(out, (unsigned long)status);
        engine_loader_append(out, ")");
        dlclose(library);
        return status != ENGINE_OK ? status : ENGINE_ERROR_UNSUPPORTED_VERSION;
    }
    if (api->struct_size < offsetof(engine_api, status_name) || api->abi_major != wanted_major ||
        api->abi_minor < wanted_minor)
    {
        engine_loader_append(out, path);
        engine_loader_append(out, " returned a table for ABI ");
        engine_loader_append_uint(out, api->abi_major);
        engine_loader_append(out, ".");
        engine_loader_append_uint(out, api->abi_minor);
        engine_loader_append(out, ", not ");
        engine_loader_append_uint(out, wanted_major);
        engine_loader_append(out, ".");
        engine_loader_append_uint(out, wanted_minor);
        dlclose(library);
        return ENGINE_ERROR_UNSUPPORTED_VERSION;
    }
    out->library = library;
    out->api = api;
    return ENGINE_OK;
}

/* Unloads the library and clears the loader. Safe on a loader that holds nothing. Fails with: IO (the system refused to
 * unload). */
static inline engine_status engine_unload(engine_loader* loader)
{
    engine_status status = ENGINE_OK;
    if (loader == NULL)
    {
        return ENGINE_ERROR_INVALID_ARGUMENT;
    }
    if (loader->library != NULL && dlclose(loader->library) != 0)
    {
        status = ENGINE_ERROR_IO;
    }
    loader->library = NULL;
    loader->api = NULL;
    return status;
}

#endif
