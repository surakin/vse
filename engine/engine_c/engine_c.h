/* engine_c.h: the engine's C API, version 1 (Phase 23 Day 346). Valid C11 (tools/abi_check compiles it as C).
 *
 * CONVENTIONS. Every function of the API follows these; a new function that breaks one is a defect.
 *
 * Entry. The library exports ONE function, engine_get_api. It returns a table of function pointers (engine_api); nothing else is exported. A host that
 * links the library directly calls it like any function, a host that loads the library at run time finds it with dlsym or GetProcAddress. The table
 * is static: it lives as long as the library stays loaded, and the same pointer comes back from every call.
 *
 * Versions. engine_abi_version.h holds MAJOR.MINOR. A different MAJOR is a different, incompatible API. A larger MINOR only adds functions at the end of
 * the table and fields at the end of structs. A host asks for the version it was written against; the library serves any minor up to its own with
 * the same major, and refuses a newer one (ENGINE_ERROR_UNSUPPORTED_VERSION). A host that wants a function newer than the library it finds checks
 * `table->struct_size` (the table says how many bytes of itself the library filled) before it reads the pointer.
 *
 * Status. Every function that can fail returns an engine_status; ENGINE_OK is zero. A failing call changes nothing it was asked to change. The text
 * of the most recent failure on the calling thread is available from engine_api.last_error (a message for people; never branch on it).
 *
 * Strings. engine_string is a pointer and a length: UTF-8, not NUL-terminated, `data` may be NULL only when `length` is 0. A string a host passes in
 * is borrowed for the duration of the call. A string the library hands out is either a static string (engine_api.status_name) or inside an engine_buffer.
 *
 * Ownership. Nothing allocated by one side is freed by the other. Memory the library produces comes in an engine_buffer; the host reads it, then
 * calls engine_buffer_release exactly once (the library's own release function does the freeing, in the library's allocator). The library never keeps
 * a pointer into host memory past the call that received it, except a user pointer it was given for a callback (below).
 *
 * Structs. Every struct that may grow starts with `uint32_t struct_size`. A struct passed IN: the host sets struct_size to sizeof of the struct as the host
 * compiled it; the library reads only that many bytes, treats the fields past them as zero, and refuses a size smaller than the first version of the
 * struct (ENGINE_ERROR_STRUCT_SIZE). A struct passed OUT: the host sets struct_size to its sizeof; the library writes at most that many bytes and sets
 * struct_size to the number it wrote. Padding is never relied on: initialize a struct with `{0}` before filling it.
 *
 * Handles. An object is named by an engine_handle: 64 bits, zero is "no object", the low 32 bits say which slot, the high 32 bits which generation of
 * the slot. A handle of a destroyed object fails with ENGINE_ERROR_STALE_HANDLE, a handle that never existed with ENGINE_ERROR_INVALID_ARGUMENT; neither
 * crashes. Handles are plain values: copy them, store them, pass them between threads.
 *
 * Callbacks. A callback is a function pointer and a `void* user` the host chose, passed back unchanged as its first argument. The thread a callback
 * runs on and what it may call are documented where the callback is declared. A callback never throws, never longjmps and never unwinds across the
 * boundary.
 *
 * Threads. Every function may be called from any thread unless its comment says otherwise; engine_create and engine_destroy are serialized with each
 * other and with every other call on the same engine by the host (the library does not wait for calls still running when an engine is destroyed).
 *
 * Process. One engine exists per process on this version: the engine's singletons (the allocator, the log, the job system, the crash handler) are the
 * process's. A second engine_create while one exists fails with ENGINE_ERROR_LIMIT_EXCEEDED.
 */
#ifndef ENGINE_C_H
#define ENGINE_C_H

#include <stddef.h>
#include <stdint.h>

#include "engine_abi_version.h"

#ifdef __cplusplus
extern "C"
{
#endif

/* ENGINE_C_API is empty for a host. The library's own definition of engine_get_api carries the export attribute (ENGINE_API of foundation/build_defs.h). */
#ifndef ENGINE_C_API
#define ENGINE_C_API
#endif

/* ---- status ------------------------------------------------------------------------------------------------------------------------------------ */

/* What a call returns. The numbers are part of the ABI: a value is never renumbered or reused, new codes are added at the end. */
typedef enum engine_status
{
    ENGINE_OK = 0,                      /* Success. */
    ENGINE_ERROR_INVALID_ARGUMENT = 1,  /* A pointer is NULL where one is required, a value is out of range, a key or value is malformed, a flag is unknown. */
    ENGINE_ERROR_STRUCT_SIZE = 2,       /* A struct's struct_size is smaller than the struct's first version. */
    ENGINE_ERROR_UNSUPPORTED_VERSION = 3, /* The host asked for another major, or a newer minor, than the library serves; or a stored format is not current. */
    ENGINE_ERROR_STALE_HANDLE = 4,      /* The handle names an object that has been destroyed. */
    ENGINE_ERROR_NOT_FOUND = 5,         /* A named thing does not exist: a setting key, a path, an id. */
    ENGINE_ERROR_ALREADY_EXISTS = 6,    /* Two things claim one name or id. */
    ENGINE_ERROR_CORRUPT = 7,           /* Bytes are damaged. */
    ENGINE_ERROR_UNSUPPORTED = 8,       /* The feature, platform or target is not supported (or the setting needs a capability that is missing). */
    ENGINE_ERROR_IO = 9,                /* The medium failed. */
    ENGINE_ERROR_CANCELLED = 10,        /* The operation was cancelled and changed nothing that was committed. */
    ENGINE_ERROR_LIMIT_EXCEEDED = 11,   /* A limit (a count, a budget, one engine per process) is exceeded. */
    ENGINE_ERROR_NOT_READY = 12,        /* The answer needs something that has not arrived yet. */
    ENGINE_ERROR_IN_USE = 13,           /* The thing cannot be removed or replaced while something depends on it. */
    ENGINE_ERROR_TYPE_MISMATCH = 14,    /* The thing is not of the type the caller asked for. */
    ENGINE_ERROR_MISSING_DEPENDENCY = 15, /* A dependency does not exist. */
    ENGINE_ERROR_DEPENDENCY_CYCLE = 16, /* Things depend on each other in a cycle. */
    ENGINE_ERROR_OFFLINE = 17,          /* The network is needed and is not there. */
    ENGINE_ERROR_TOO_LARGE = 18,        /* A size is above a bound. */
    ENGINE_ERROR_DENIED = 19,           /* An administrator's policy holds the value: the program cannot change it. */
    ENGINE_ERROR_INTERNAL = 20          /* The engine failed in a way it has no better word for (the message says more). */
} engine_status;

/* ---- strings, buffers, handles ----------------------------------------------------------------------------------------------------------------- */

/* A UTF-8 string by pointer and length (not NUL-terminated). */
typedef struct engine_string
{
    const char* data;
    size_t length;
} engine_string;

/* Memory the library produced for the host. `data` and `size` are what to read; `release` frees them. Call engine_buffer_release once, from any thread,
 * before the library is unloaded. A zeroed buffer (release NULL) owns nothing and releasing it does nothing. */
typedef struct engine_buffer
{
    const char* data;
    size_t size;
    void* owner;                            /* the library's, never touched by the host */
    void (*release)(struct engine_buffer*); /* set by the library; NULL when the buffer owns nothing */
} engine_buffer;

/* Releases a buffer and zeroes it. Safe on a zeroed or already released buffer. */
static inline void engine_buffer_release(engine_buffer* buffer)
{
    if (buffer != NULL && buffer->release != NULL)
    {
        buffer->release(buffer);
    }
}

/* A generation handle (see "Handles" above). */
typedef struct engine_handle
{
    uint64_t value;
} engine_handle;

/* ---- the engine object ------------------------------------------------------------------------------------------------------------------------- */

typedef enum engine_log_level
{
    ENGINE_LOG_ERROR = 0,
    ENGINE_LOG_WARN = 1,
    ENGINE_LOG_INFO = 2,
    ENGINE_LOG_DEBUG = 3,
    ENGINE_LOG_TRACE = 4
} engine_log_level;

/* The log callback. Called once for each line the engine emits (after the channel's own filter, so the `log.<channel>.verbosity` settings decide what
 * arrives), on the thread that logged, possibly from several threads at once. It must return quickly, must not throw, and must not call engine_create or
 * engine_destroy (destroy waits for callbacks in flight). `channel` and `message` are valid only during the call: copy what you keep. */
typedef void (*engine_log_fn)(void* user, engine_log_level level, engine_string channel, engine_string message);

/* One setting given at creation: a key as `engine_settings` declares it (`log.core.verbosity`) and its value as text (`debug`, `1280`, `true`). */
typedef struct engine_setting_entry
{
    engine_string key;
    engine_string value;
} engine_setting_entry;

/* engine_create_info.flags */
#define ENGINE_CREATE_LOG_SUPPRESS_CONSOLE 0x1u /* lines sent to the log callback are not also written to the console */
#define ENGINE_CREATE_FLAGS_MASK 0x1u

/* What engine_create needs. Fields are listed in the order they were added; a later minor appends. */
typedef struct engine_create_info
{
    uint32_t struct_size;
    uint32_t flags;                           /* ENGINE_CREATE_* bits; an unknown bit is ENGINE_ERROR_INVALID_ARGUMENT */
    engine_string app_name;                   /* names the program in messages; empty means "engine" */
    const engine_setting_entry* settings;     /* the host's settings, `setting_count` of them, a layer below the command line */
    size_t setting_count;
    const engine_string* args;                /* the command line as main sees it (args[0] is the program name and is skipped), `arg_count` of them */
    size_t arg_count;
    engine_log_fn log;                        /* or NULL */
    void* log_user;
} engine_create_info;

/* What engine_api.get_info reports. */
typedef struct engine_info
{
    uint32_t struct_size;
    uint32_t setting_count; /* how many settings the engine declares */
} engine_info;

/* Which sanitizer the library was built with. A sanitized library loads only into a host built with the same one. */
typedef enum engine_sanitizer
{
    ENGINE_LIB_SANITIZER_NONE = 0,
    ENGINE_LIB_SANITIZER_ADDRESS = 1,
    ENGINE_LIB_SANITIZER_THREAD = 2
} engine_sanitizer;

/* ---- the function table ------------------------------------------------------------------------------------------------------------------------ */

typedef struct engine_api
{
    uint32_t struct_size;  /* bytes of this table the library filled (at most what the host asked for) */
    uint32_t abi_major;    /* the library's own version */
    uint32_t abi_minor;
    uint32_t sanitizer;    /* an engine_sanitizer */

    /* The name of a status, `ok`, `invalid_argument`, ...; static, never NULL; `unknown` for a number that is not a status. */
    engine_string (*status_name)(engine_status status);

    /* Creates the engine: its settings (declared by the engine, filled from the host's entries, then the command line; the environment and the user's own
     * settings file are not read), its logging, its job system on demand. A bad key, a value of the wrong type or a command-line error fails the call and
     * creates nothing. `*out` is zero on failure.
     * Fails with: INVALID_ARGUMENT, STRUCT_SIZE, LIMIT_EXCEEDED (an engine exists), NOT_FOUND (an unknown key), UNSUPPORTED, DENIED. */
    engine_status (*create)(const engine_create_info* info, engine_handle* out);

    /* Destroys the engine and invalidates its handle. Waits for log callbacks in flight; no callback runs after it returns.
     * Fails with: STALE_HANDLE, INVALID_ARGUMENT. */
    engine_status (*destroy)(engine_handle engine);

    /* Reports what the engine holds. Fails with: STALE_HANDLE, INVALID_ARGUMENT, STRUCT_SIZE. */
    engine_status (*get_info)(engine_handle engine, engine_info* out);

    /* Sets a setting from its text form, as the command line would not (the value is a layer below the command line, so a flag the host passed wins).
     * Fails with: STALE_HANDLE, INVALID_ARGUMENT (bad value), NOT_FOUND (unknown key), UNSUPPORTED, DENIED, IO. */
    engine_status (*setting_set)(engine_handle engine, engine_string key, engine_string value);

    /* Reads a setting's current value as text into a buffer the host releases. Fails with: STALE_HANDLE, INVALID_ARGUMENT, NOT_FOUND. */
    engine_status (*setting_get)(engine_handle engine, engine_string key, engine_buffer* out);

    /* The text of the calling thread's most recent failed call, in a buffer the host releases (empty if none failed). Always succeeds with a valid `out`.
     * Fails with: INVALID_ARGUMENT (`out` is NULL). */
    engine_status (*last_error)(engine_buffer* out);

    /* Returns a named extension: a struct of functions beyond the core, or NULL when this library does not have it. The extension's own header says what
     * the pointer points to; it is static, like the table. The core API never needs one. The library's own tests use `engine.test_state`, which only a
     * build made for testing carries (a release build returns NULL for it). */
    const void* (*get_extension)(engine_string name);

    /* Tells the engine that the calling thread is about to end: it hands back what the thread cached (frame memory, crash-handler registration) so that
     * nothing is left behind in a library that may be unloaded afterwards. A host calls it on every thread that called the engine, once, just before the
     * thread exits; the engine's own worker threads do it themselves. Calling it earlier is harmless: the thread may keep using the engine. */
    void (*thread_detach)(void);
} engine_api;

/* Returns the function table.
 *
 * `wanted_major`/`wanted_minor`: the version the host was written against (ENGINE_ABI_VERSION_MAJOR/MINOR of the header it compiled with).
 * `table_size`: sizeof(engine_api) as the host compiled it. The library always returns its whole table (static, the same pointer every time); the host reads
 *   only the fields below min(table_size, table->struct_size), the first being what it knows and the second what the library has.
 * `out`: receives a pointer to the static table; set to NULL on failure.
 * Fails with: INVALID_ARGUMENT (`out` NULL or `table_size` below the table's first fields), UNSUPPORTED_VERSION (another major or a newer minor).
 * Thread-safe; callable before and after any engine exists. */
ENGINE_C_API engine_status engine_get_api(uint32_t wanted_major, uint32_t wanted_minor, size_t table_size, const engine_api** out);

#ifdef __cplusplus
}
#endif

#endif
