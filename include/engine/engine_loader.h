/* engine_loader.h: loads the engine library at run time and returns its function table (Phase 23 Day 347). Header-only
 * plain C with no engine dependency but engine_c.h: a host copies this file and engine_c.h and nothing else. POSIX
 * (`dlopen`), and Windows (`LoadLibraryExW` and `GetProcAddress`, Day 354: the path is UTF-8, as everywhere in the API;
 * for a path with a folder in it the DLLs engine.dll depends on are looked for beside it first).
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

#include <string.h>

#if defined(_WIN32)
#include <windows.h>
#else
#include <dlfcn.h>
#endif

#include "engine_c.h"

typedef struct engine_loader
{
    uint32_t struct_size;
    void* library;         /* the dlopen handle (Windows: the HMODULE), NULL when nothing is loaded */
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

/* The system's loader, one function each: open (NULL, and the reason appended to `out`, on failure), find a symbol,
 * close (0 on success). */
#if defined(_WIN32)
static inline void* engine_loader_open(const char* path, engine_loader* out)
{
    wchar_t wide[1024];
    HMODULE module;
    DWORD flags = 0;
    DWORD error;
    char reason[256];
    if (MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, path, -1, wide, (int)(sizeof(wide) / sizeof(wide[0]))) == 0)
    {
        engine_loader_append(out, "cannot load ");
        engine_loader_append(out, path);
        engine_loader_append(out, ": the path is not UTF-8 or is too long");
        return NULL;
    }
    if (strchr(path, '/') != NULL || strchr(path, '\\') != NULL)
    {
        flags = LOAD_WITH_ALTERED_SEARCH_PATH; /* the library's own folder first, for the DLLs it needs */
    }
    module = LoadLibraryExW(wide, NULL, flags);
    if (module != NULL)
    {
        return (void*)module;
    }
    error = GetLastError();
    reason[0] = '\0';
    if (FormatMessageA(FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS, NULL, error, 0, reason,
                       (DWORD)sizeof(reason), NULL) == 0)
    {
        reason[0] = '\0';
    }
    else
    {
        size_t n = strlen(reason);
        while (n > 0 &&
               (reason[n - 1] == '\r' || reason[n - 1] == '\n' || reason[n - 1] == ' ' || reason[n - 1] == '.'))
        {
            reason[--n] = '\0'; /* the system's sentence ends with a period and a line break */
        }
    }
    engine_loader_append(out, "cannot load ");
    engine_loader_append(out, path);
    engine_loader_append(out, ": ");
    engine_loader_append(out, reason[0] != '\0' ? reason : "unknown reason");
    engine_loader_append(out, " (error ");
    engine_loader_append_uint(out, (unsigned long)error);
    engine_loader_append(out, ")");
    return NULL;
}
static inline void* engine_loader_symbol(void* library, const char* name)
{
    FARPROC p = GetProcAddress((HMODULE)library, name);
    void* symbol;
    memcpy(&symbol, &p, sizeof(symbol)); /* function pointer to object pointer without a cast ISO C forbids */
    return symbol;
}
static inline int engine_loader_close(void* library)
{
    return FreeLibrary((HMODULE)library) ? 0 : -1;
}
#else
static inline void* engine_loader_open(const char* path, engine_loader* out)
{
    void* library = dlopen(path, RTLD_NOW | RTLD_LOCAL);
    if (library == NULL)
    {
        const char* reason = dlerror();
        engine_loader_append(out, "cannot load ");
        engine_loader_append(out, path);
        engine_loader_append(out, ": ");
        engine_loader_append(out, reason != NULL ? reason : "unknown reason");
    }
    return library;
}
static inline void* engine_loader_symbol(void* library, const char* name)
{
    return dlsym(library, name);
}
static inline int engine_loader_close(void* library)
{
    return dlclose(library);
}
#endif

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
    library = engine_loader_open(path, out);
    if (library == NULL)
    {
        return ENGINE_ERROR_IO;
    }
    symbol = engine_loader_symbol(library, "engine_get_api");
    if (symbol == NULL)
    {
        engine_loader_append(out, path);
        engine_loader_append(out, " is not an engine library: it has no engine_get_api");
        engine_loader_close(library);
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
        engine_loader_close(library);
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
        engine_loader_close(library);
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
    if (loader->library != NULL && engine_loader_close(loader->library) != 0)
    {
        status = ENGINE_ERROR_IO;
    }
    loader->library = NULL;
    loader->api = NULL;
    return status;
}

#endif
