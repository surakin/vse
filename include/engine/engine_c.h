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
    uint32_t caps;          /* ENGINE_CAP_* bits: what this build of the library has (written when struct_size covers it) */
    uint32_t reserved;
} engine_info;

/* Bits of engine_info.caps. */
#define ENGINE_CAP_IMPORT_GLTF 0x1u /* the glTF importer is built in (import_gltf and import_gltf_async work; without it they fail with UNSUPPORTED) */
#define ENGINE_CAP_SURFACE_X11 0x2u /* surface_create renders into an X11 window the host owns (Day 352) */
#define ENGINE_CAP_SURFACE_WAYLAND 0x4u /* surface_create renders into a Wayland wl_surface the host owns (Day 352) */
#define ENGINE_CAP_READBACK 0x8u /* surface_read_pixels works (Day 352) */
#define ENGINE_CAP_SURFACE_COCOA 0x10u /* surface_create renders with Metal into an NSView or CAMetalLayer the host owns (macOS, Day 355) */
#define ENGINE_CAP_SURFACE_WIN32 0x20u /* surface_create renders into a Win32 window (HWND) the host owns, with Direct3D 12 or Vulkan (Day 354) */

/* ---- Day 352: rendering into a window the host owns ---- */

/* Which kind of native window engine_surface_info names. */
#define ENGINE_SURFACE_X11 1u     /* `display` is the Display*, `window_id` the X11 Window */
#define ENGINE_SURFACE_WAYLAND 2u /* `display` is the wl_display*, `surface` the wl_surface* (the host's own connection and surface) */
/* macOS (Day 355): `display` is the host's NSView* (required), `surface` a CAMetalLayer* the host made for it, or NULL. With NULL, the view's own layer is used
 * when it is a CAMetalLayer, and otherwise the engine adds a CAMetalLayer of its own as a sublayer that fills the view (and removes it at surface_destroy and
 * surface_rebind). surface_create, surface_rebind, surface_resized and surface_destroy of a Cocoa surface are called on the main thread (INVALID_ARGUMENT
 * otherwise), and frame draws it only there: a frame on another thread skips it and fails its pending read-backs with INVALID_ARGUMENT. */
#define ENGINE_SURFACE_COCOA 3u
#define ENGINE_SURFACE_WIN32 4u /* `display` is the HINSTANCE of the window's class (GetModuleHandleW(NULL) for a class of the program), `surface` the HWND (Day 354) */

/* engine_surface_info.flags: which graphics API draws into the window. Neither bit is the platform's own (Direct3D 12 on Windows, Vulkan on Linux, Metal on
 * macOS, which has no bit); both at once is INVALID_ARGUMENT; one that this platform has not is UNSUPPORTED. A surface keeps its API across surface_rebind. */
#define ENGINE_SURFACE_FLAG_VULKAN 0x1u
#define ENGINE_SURFACE_FLAG_D3D12 0x2u

/* Input forwarded by the host (Day 353). The values of engine_key are those of the engine's own key enumeration (layout-aware keys: the symbol the layout
 * produces), 0 is "unknown". */
typedef enum engine_key
{
    ENGINE_KEY_UNKNOWN = 0,
    ENGINE_KEY_A = 1,
    ENGINE_KEY_B = 2,
    ENGINE_KEY_C = 3,
    ENGINE_KEY_D = 4,
    ENGINE_KEY_E = 5,
    ENGINE_KEY_F = 6,
    ENGINE_KEY_G = 7,
    ENGINE_KEY_H = 8,
    ENGINE_KEY_I = 9,
    ENGINE_KEY_J = 10,
    ENGINE_KEY_K = 11,
    ENGINE_KEY_L = 12,
    ENGINE_KEY_M = 13,
    ENGINE_KEY_N = 14,
    ENGINE_KEY_O = 15,
    ENGINE_KEY_P = 16,
    ENGINE_KEY_Q = 17,
    ENGINE_KEY_R = 18,
    ENGINE_KEY_S = 19,
    ENGINE_KEY_T = 20,
    ENGINE_KEY_U = 21,
    ENGINE_KEY_V = 22,
    ENGINE_KEY_W = 23,
    ENGINE_KEY_X = 24,
    ENGINE_KEY_Y = 25,
    ENGINE_KEY_Z = 26,
    ENGINE_KEY_NUM0 = 27,
    ENGINE_KEY_NUM1 = 28,
    ENGINE_KEY_NUM2 = 29,
    ENGINE_KEY_NUM3 = 30,
    ENGINE_KEY_NUM4 = 31,
    ENGINE_KEY_NUM5 = 32,
    ENGINE_KEY_NUM6 = 33,
    ENGINE_KEY_NUM7 = 34,
    ENGINE_KEY_NUM8 = 35,
    ENGINE_KEY_NUM9 = 36,
    ENGINE_KEY_F1 = 37,
    ENGINE_KEY_F2 = 38,
    ENGINE_KEY_F3 = 39,
    ENGINE_KEY_F4 = 40,
    ENGINE_KEY_F5 = 41,
    ENGINE_KEY_F6 = 42,
    ENGINE_KEY_F7 = 43,
    ENGINE_KEY_F8 = 44,
    ENGINE_KEY_F9 = 45,
    ENGINE_KEY_F10 = 46,
    ENGINE_KEY_F11 = 47,
    ENGINE_KEY_F12 = 48,
    ENGINE_KEY_ESCAPE = 49,
    ENGINE_KEY_ENTER = 50,
    ENGINE_KEY_TAB = 51,
    ENGINE_KEY_BACKSPACE = 52,
    ENGINE_KEY_SPACE = 53,
    ENGINE_KEY_INSERT = 54,
    ENGINE_KEY_DELETE = 55,
    ENGINE_KEY_HOME = 56,
    ENGINE_KEY_END = 57,
    ENGINE_KEY_PAGE_UP = 58,
    ENGINE_KEY_PAGE_DOWN = 59,
    ENGINE_KEY_LEFT = 60,
    ENGINE_KEY_RIGHT = 61,
    ENGINE_KEY_UP = 62,
    ENGINE_KEY_DOWN = 63,
    ENGINE_KEY_LEFT_SHIFT = 64,
    ENGINE_KEY_RIGHT_SHIFT = 65,
    ENGINE_KEY_LEFT_CTRL = 66,
    ENGINE_KEY_RIGHT_CTRL = 67,
    ENGINE_KEY_LEFT_ALT = 68,
    ENGINE_KEY_RIGHT_ALT = 69,
    ENGINE_KEY_LEFT_SUPER = 70,
    ENGINE_KEY_RIGHT_SUPER = 71,
    ENGINE_KEY_CAPS_LOCK = 72,
    ENGINE_KEY_MINUS = 73,
    ENGINE_KEY_EQUALS = 74,
    ENGINE_KEY_LEFT_BRACKET = 75,
    ENGINE_KEY_RIGHT_BRACKET = 76,
    ENGINE_KEY_BACKSLASH = 77,
    ENGINE_KEY_SEMICOLON = 78,
    ENGINE_KEY_APOSTROPHE = 79,
    ENGINE_KEY_GRAVE = 80,
    ENGINE_KEY_COMMA = 81,
    ENGINE_KEY_PERIOD = 82,
    ENGINE_KEY_SLASH = 83,
    ENGINE_KEY_COUNT
} engine_key;

/* Modifier bits of engine_input_event.modifiers. */
#define ENGINE_MOD_SHIFT 0x1u
#define ENGINE_MOD_CTRL 0x2u
#define ENGINE_MOD_ALT 0x4u
#define ENGINE_MOD_SUPER 0x8u
#define ENGINE_MOD_CAPS_LOCK 0x10u
#define ENGINE_MOD_NUM_LOCK 0x20u

/* Mouse buttons (engine_input_event.button). */
#define ENGINE_BUTTON_LEFT 0x1u
#define ENGINE_BUTTON_RIGHT 0x2u
#define ENGINE_BUTTON_MIDDLE 0x4u
#define ENGINE_BUTTON_BACK 0x8u
#define ENGINE_BUTTON_FORWARD 0x10u

typedef enum engine_input_type
{
    ENGINE_INPUT_KEY = 1,            /* `key`, `down`, `repeat`, `modifiers` */
    ENGINE_INPUT_POINTER_MOVE = 2,   /* `x`, `y` in the window's pixels */
    ENGINE_INPUT_POINTER_BUTTON = 3, /* `button` (one ENGINE_BUTTON_* bit), `down`, `x`, `y` */
    ENGINE_INPUT_POINTER_ENTER = 4,  /* `x`, `y` */
    ENGINE_INPUT_POINTER_LEAVE = 5,
    ENGINE_INPUT_SCROLL = 6,         /* `x`, `y` are the notches turned (positive is right and down) */
    ENGINE_INPUT_TEXT = 7,           /* `text`: UTF-8 the keyboard produced (one or more scalars, at most 256 bytes) */
    ENGINE_INPUT_FOCUS = 8,          /* `down` 1: the host's view got keyboard focus, 0: lost it */
    ENGINE_INPUT_TOUCH = 9           /* `touch_id`, `down`, `x`, `y`: the first finger down acts as the pointer with the left button; the others are ignored */
} engine_input_type;

/* One input event. `time_ns` is a monotonic time in nanoseconds (0: the engine stamps it when the call arrives). */
typedef struct engine_input_event
{
    uint32_t struct_size;
    uint32_t type; /* engine_input_type */
    uint64_t time_ns;
    uint32_t modifiers;
    uint32_t key; /* engine_key */
    uint32_t button;
    uint32_t down;
    uint32_t repeat;
    uint32_t touch_id;
    double x;
    double y;
    engine_string text;
} engine_input_event;

/* Cursor shapes the engine asks the host for (callbacks.set_cursor). */
typedef enum engine_cursor
{
    ENGINE_CURSOR_ARROW = 0,
    ENGINE_CURSOR_I_BEAM = 1,
    ENGINE_CURSOR_HAND = 2,
    ENGINE_CURSOR_RESIZE_HORIZONTAL = 3,
    ENGINE_CURSOR_RESIZE_VERTICAL = 4,
    ENGINE_CURSOR_RESIZE_DIAGONAL_NWSE = 5,
    ENGINE_CURSOR_RESIZE_DIAGONAL_NESW = 6,
    ENGINE_CURSOR_RESIZE_ALL = 7,
    ENGINE_CURSOR_NOT_ALLOWED = 8,
    ENGINE_CURSOR_HIDDEN = 9,
    ENGINE_CURSOR_COUNT
} engine_cursor;

/* What the host does for the engine's requests on a surface. Every callback may be NULL (the request is refused). They run on the thread that drives the engine's UI
 * for the surface (the host's frame thread), never from an engine worker, and must not call back into the engine. */
typedef struct engine_surface_callbacks
{
    uint32_t struct_size;
    uint32_t reserved;
    void* user;
    int (*set_cursor)(void* user, uint32_t shape);                        /* engine_cursor; nonzero when applied */
    int (*set_pointer_lock)(void* user, int locked);                      /* nonzero when done */
    int (*clipboard_set)(void* user, engine_string text);                 /* UTF-8; nonzero when stored */
    int (*clipboard_get)(void* user, engine_string* text);                /* fills `*text` (valid until the next callback) and returns nonzero, or returns 0 for no text */
    void (*text_input_rect)(void* user, int32_t x, int32_t y, int32_t width, int32_t height); /* the caret, in window pixels */
    void (*request_focus)(void* user, int focused);
} engine_surface_callbacks;

/* A window the host owns. The engine never creates, moves, resizes or destroys it: the host keeps it alive until surface_destroy returns, and
 * tells the engine its size and scale with surface_resized. */
typedef struct engine_surface_info
{
    uint32_t struct_size;
    uint32_t kind;   /* ENGINE_SURFACE_* */
    uint32_t flags;  /* ENGINE_SURFACE_FLAG_*, or 0 for the platform's own graphics API */
    uint32_t width;  /* in pixels */
    uint32_t height;
    float scale;     /* the DPI scale of the window; 0 means 1 */
    uint32_t present_mode; /* ENGINE_PRESENT_*: what the host wants; the surface falls back to FIFO when it cannot (engine_surface_state.present_mode says what it got) */
    void* display;
    void* surface;
    uint64_t window_id;
    const engine_surface_callbacks* callbacks; /* or NULL: copied by surface_create and surface_rebind */
} engine_surface_info;

#define ENGINE_PRESENT_FIFO 0u    /* wait for the display's refresh; always available */
#define ENGINE_PRESENT_MAILBOX 1u /* replace the queued image with the newest, no tearing */
#define ENGINE_PRESENT_IMMEDIATE 2u /* present at once, possibly tearing */

/* What engine_api.surface_state reports. */
typedef struct engine_surface_state
{
    uint32_t struct_size;
    uint32_t image_width;  /* of the swapchain, 0 when there is none */
    uint32_t image_height;
    uint32_t visible;      /* 1 when the last frame was presented */
    uint32_t lost;         /* 1 when the window is gone from under the engine: call surface_rebind */
    uint32_t present_mode; /* ENGINE_PRESENT_* in use */
    uint64_t frames_presented;
    uint64_t recoveries;   /* how many times surface_rebind rebuilt the swapchain */
    uint64_t frames_skipped; /* frames that drew nothing (zero size, lost) */
    uint64_t input_dropped;  /* input events dropped because the queue (1024) was full: the engine's UI did not drain it */
} engine_surface_state;

/* The pixels of a rendered frame: RGBA8, `stride` bytes per row (width * 4), top row first. `data` is the host's to release. */
typedef struct engine_pixels
{
    uint32_t struct_size;
    uint32_t width;
    uint32_t height;
    uint32_t stride;
    engine_buffer data;
} engine_pixels;

/* Which sanitizer the library was built with. A sanitized library loads only into a host built with the same one. */
typedef enum engine_sanitizer
{
    ENGINE_LIB_SANITIZER_NONE = 0,
    ENGINE_LIB_SANITIZER_ADDRESS = 1,
    ENGINE_LIB_SANITIZER_THREAD = 2
} engine_sanitizer;

/* ---- frames, worlds, entities, components, jobs (Phase 23 Day 349) ---------------------------------------------------------------------------- */

/* What engine_api.frame is given. */
typedef struct engine_frame_info
{
    uint32_t struct_size;
    uint32_t flags;        /* must be 0 */
    double delta_seconds;  /* the time step the systems see */
    uint64_t budget_us;    /* microseconds the frame may take before it stops starting new work; 0 means no budget */
} engine_frame_info;

/* What engine_api.frame reports. */
typedef struct engine_frame_stats
{
    uint32_t struct_size;
    uint32_t worlds_run;      /* worlds whose systems ran */
    uint32_t worlds_skipped;  /* worlds left for the next frame because the budget was spent */
    uint32_t entities;        /* entities alive in the worlds that ran */
    uint64_t frame_index;     /* frames run so far, this one included, from 1 */
    uint64_t elapsed_us;      /* wall time of this call */
} engine_frame_stats;

/* The components a host can set on an entity. The numbers are part of the ABI. ENGINE_COMPONENT_BIT(kind) is the bit of a kind in a query's mask. */
typedef enum engine_component_kind
{
    ENGINE_COMPONENT_TRANSFORM = 1,        /* engine_transform: the entity's place relative to its parent, or to the world */
    ENGINE_COMPONENT_WORLD_TRANSFORM = 2,  /* engine_world_transform: where the frame's transform system put it; read only */
    ENGINE_COMPONENT_NAME = 3,             /* text, up to ENGINE_NAME_MAX_BYTES bytes; set and read with engine_api.entity_name_set/get */
    ENGINE_COMPONENT_MESH_REFERENCE = 4    /* engine_mesh_reference: which mesh and material the entity draws */
} engine_component_kind;

#define ENGINE_COMPONENT_BIT(kind) (1u << ((kind)-1))
#define ENGINE_COMPONENT_MASK_ALL 0xFu
#define ENGINE_NAME_MAX_BYTES 63u

/* The local transform: scale, then rotate, then translate. The rotation is a unit quaternion (x, y, z, w). */
typedef struct engine_transform
{
    uint32_t struct_size;
    float position[3];
    float rotation[4];
    float scale[3];
} engine_transform;

/* The matrix from the entity's space to the world's, column-major (element [4 * column + row]). */
typedef struct engine_world_transform
{
    uint32_t struct_size;
    float matrix[16];
} engine_world_transform;

/* The mesh an entity draws. `mesh` is an identifier the host chooses (loaded resources name theirs on Day 350); the engine stores it. */
typedef struct engine_mesh_reference
{
    uint32_t struct_size;
    uint32_t material;
    uint64_t mesh;
} engine_mesh_reference;

/* A job: called once, on one of the engine's worker threads (never on the thread that submitted it), with the `user` pointer given to jobs_submit.
 * While it runs it may call, through the table it was given: status_name, last_error, thread_detach (not needed), the world, entity, component and
 * query functions (they are serialized with each other), counter_value, counter_create/destroy and jobs_submit. It must not call frame, create,
 * destroy, counter_wait, crash_handler_install or crash_handler_uninstall: wait for other work from outside the job. It never throws or longjmps. */
typedef void (*engine_job_fn)(void* user);

/* ---- Day 350: resources and host-provided content ---------------------------------------------------------------------------------------------- */

/* A resource type is its four characters as a uint32_t, the first in the lowest byte: ENGINE_RESOURCE_TYPE('M','E','S','H'). Types with a loader today:
 * skeleton, animation, mesh, texture, material and level, loaded as their CPU data (the engine's own objects; nothing of them is exposed). */
#define ENGINE_RESOURCE_TYPE(a, b, c, d) \
    ((uint32_t)(unsigned char)(a) | ((uint32_t)(unsigned char)(b) << 8) | ((uint32_t)(unsigned char)(c) << 16) | ((uint32_t)(unsigned char)(d) << 24))
#define ENGINE_RESOURCE_MESH ENGINE_RESOURCE_TYPE('M', 'E', 'S', 'H')
#define ENGINE_RESOURCE_TEXTURE ENGINE_RESOURCE_TYPE('T', 'E', 'X', 'R')
#define ENGINE_RESOURCE_MATERIAL ENGINE_RESOURCE_TYPE('M', 'A', 'T', 'L')
#define ENGINE_RESOURCE_LEVEL ENGINE_RESOURCE_TYPE('L', 'E', 'V', 'L')
#define ENGINE_RESOURCE_ANIMATION ENGINE_RESOURCE_TYPE('A', 'N', 'I', 'M')
#define ENGINE_RESOURCE_SKELETON ENGINE_RESOURCE_TYPE('S', 'K', 'E', 'L')

/* Flags of engine_resource_add_info and of resource_remove. */
#define ENGINE_RESOURCE_BORROW 0x1u     /* add, replace: lend the bytes instead of copying them; `release` is required */
#define ENGINE_RESOURCE_CHECK_HASH 0x2u /* add, replace: the blob's content hash must equal `expected_hash` (CORRUPT otherwise) */
#define ENGINE_RESOURCE_FORCE 0x4u      /* replace, remove: unload the loaded resources that depend on this one, atomically, instead of refusing (IN_USE) */

/* Called when the engine is done with lent bytes: exactly once per successful add or replace that lent them, after the last reader of those bytes
 * is gone (the resource was replaced or removed and no load is reading it, or the engine was destroyed), on one of the engine's worker threads (never
 * on the thread that called the engine, and never while the engine holds a lock the callback could need). From then on the bytes are the host's
 * again. It may call status_name, last_error, thread_detach, the resource_* and world functions and jobs_submit; it must not call frame, create,
 * destroy, counter_wait or ticket_wait. It never throws or longjmps, and engine workers wait for it: it should return soon. A failed add or replace never calls it: the host keeps the bytes. engine_destroy runs every
 * pending release before it returns. */
typedef void (*engine_resource_release_fn)(void* user, const void* bytes, size_t size);

/* One resource's bytes: a complete blob as `asset_mint` makes it (container, type, id, content hash; the engine checks all of them). */
typedef struct engine_resource_add_info
{
    uint32_t struct_size;
    uint32_t flags;                           /* ENGINE_RESOURCE_BORROW, ENGINE_RESOURCE_CHECK_HASH, ENGINE_RESOURCE_FORCE (replace only) */
    uint64_t id;                              /* the resource id; it must be the id inside the blob; not zero */
    engine_string path;                       /* a path that names the resource for resource_load; empty: reachable by id only. Replace: empty keeps the paths */
    const void* bytes;                        /* the blob */
    size_t size;                              /* its length */
    engine_resource_release_fn release;       /* with ENGINE_RESOURCE_BORROW; NULL otherwise */
    void* release_user;                       /* passed to `release` */
    uint8_t expected_hash[16];                /* with ENGINE_RESOURCE_CHECK_HASH */
} engine_resource_add_info;

/* What resource_mounted and resource_status report. */
typedef struct engine_resource_info
{
    uint32_t struct_size;
    uint32_t type;           /* the resource type tag */
    uint64_t id;
    uint64_t size;           /* bytes of the blob in the memory backend (0 when the resource is not in it) */
    uint8_t content_hash[16];
    int32_t loaded;          /* 1 while the engine has it loaded (for resource_status: while the handle is valid) */
    int32_t reserved;
} engine_resource_info;

/* How soon an asynchronous load is served. */
#define ENGINE_LOAD_IMMEDIATE 0u
#define ENGINE_LOAD_NORMAL 1u
#define ENGINE_LOAD_BACKGROUND 2u

/* What ticket_poll reports. */
typedef struct engine_ticket_info
{
    uint32_t struct_size;
    int32_t done;          /* 1 once the load ended (with a resource, an error or a cancellation) */
    uint32_t blobs_read;   /* blobs read of ... */
    uint32_t blobs_total;  /* ... the blobs known so far (the total grows as the closure is found) */
} engine_ticket_info;

/* ---- glTF import (Phase 23 Day 351)
 * ----------------------------------------------------------------------------------------------------------- */

/* Flags of engine_import_options. */
#define ENGINE_IMPORT_FLAT_NORMALS 0x1u /* a mesh without normals gets flat ones (default: smooth) */
#define ENGINE_IMPORT_DROP_MORPH_TARGETS                                                                               \
    0x2u /* import the base shape of a primitive that has morph targets (a warning) instead of refusing */

    /* What a host's read_file callback fills: the bytes of one external file of a glTF (a `.bin` or an image). The
     * engine copies them at once, then calls `release` (when not NULL) exactly once with the same data pointer and
     * size, so the host may free them there. */
    typedef struct engine_import_file
    {
        const void* data;
        size_t size;
        void (*release)(void* release_user, const void* data, size_t size);
        void* release_user;
    } engine_import_file;

    /* Reads an external file of a glTF for the engine. `name` is a clean relative name (segments joined by '/', no
     * "..", no scheme, percent-escapes decoded). Return ENGINE_OK and fill `*out`, or an error: ENGINE_ERROR_NOT_FOUND
     * (no such file), ENGINE_ERROR_IO, or another status the engine reports as the import's failure. A file longer than
     * `max_bytes` makes the import fail with TOO_LARGE. In engine_api.import_gltf it runs on the calling thread; in
     * import_gltf_async on an engine worker thread, at any time until the ticket is destroyed, and it must be
     * thread-safe. It must not call the engine except status_name and last_error, and it never throws or longjmps. */
    typedef engine_status (*engine_import_read_file_fn)(void* user, engine_string name, uint64_t max_bytes,
                                                        engine_import_file* out);

    /* How an import is made. A field that is 0 (or empty) takes its default. */
    typedef struct engine_import_options
    {
        uint32_t struct_size;
        uint32_t flags;            /* ENGINE_IMPORT_* */
        float scale;               /* positions are multiplied by it (0: 1, glTF is in metres) */
        float clip_rate;           /* the rate cubic-spline channels are resampled at (0: 30) */
        engine_string path_prefix; /* the resource paths start with it (empty: "/gltf") */
        engine_string manifest;    /* the manifest text an earlier import of this file returned: ids are kept for the
                                      objects that match */
        uint64_t id_seed; /* not 0: new ids come from a generator seeded with it (the same file and seed give the same
                             ids); 0: random ids */
        engine_import_read_file_fn read_file; /* NULL: a glTF that names an external file is refused */
        void* read_user;
        /* Limits; 0 takes the setting `assets.import.*` of the engine. A limit the host sets can be lower or higher
         * than the setting. */
        uint64_t max_file_bytes;
        uint64_t max_json_bytes;
        uint64_t max_buffer_bytes;
        uint64_t max_output_bytes;
        uint64_t max_pixels; /* of one decoded image */
        uint32_t max_nodes;
        uint32_t max_accessors;
        uint32_t max_images;
        uint32_t reserved;
    } engine_import_options;

    /* What an import made. Every buffer is the host's to release (engine_buffer_release). */
    typedef struct engine_import_result
    {
        uint32_t struct_size;
        uint32_t resource_count; /* resources made and added to the memory backend, dependencies first */
        uint32_t warning_count;  /* features dropped or approximated (see `warnings`) */
        uint32_t reserved;
        engine_buffer ids;      /* resource_count values of uint64_t: the ids */
        engine_buffer types;    /* resource_count values of uint32_t: the type tags (ENGINE_RESOURCE_*) */
        engine_buffer paths;    /* the paths, UTF-8, one per line (each ends with '\n'), in the order of `ids` */
        engine_buffer warnings; /* UTF-8, one line per warning */
        engine_buffer manifest; /* UTF-8: pass it to the next import of this file as `manifest` to keep the ids */
    } engine_import_result;

    /* What import_poll reports. */
    typedef struct engine_import_info
    {
        uint32_t struct_size;
        int32_t done;         /* 1 once the import ended (with a result, an error or a cancellation) */
        uint32_t steps_done;  /* of ... */
        uint32_t steps_total; /* ... the import's steps */
    } engine_import_info;

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

    /* ---- Day 349 ---- */

    /* Runs one frame: every world's systems (today the transform propagation) once, in the order the worlds were created, on the calling thread. The
     * host drives the loop: nothing here waits for the host or blocks on a thread of its own, and no engine thread runs a frame by itself. With a budget,
     * a world that has not started when the budget is spent is left for the next frame (the first world of a frame always runs, so a budget too small for one world cannot starve it) (counted in `worlds_skipped`), and the next frame begins with the first world it left. `stats` may be NULL.
     * Thread rule: one thread at a time per engine (the host's frame thread); other API calls may run beside it, world calls wait for it.
     * Fails with: STALE_HANDLE, INVALID_ARGUMENT (flags, a delta that is negative, NaN or above 3600 seconds), STRUCT_SIZE. */
    engine_status (*frame)(engine_handle engine, const engine_frame_info* info, engine_frame_stats* stats);

    /* Creates an empty world (entities and their components). The world counts as an object of the engine: engine_destroy refuses (IN_USE) while one exists.
     * Fails with: STALE_HANDLE, INVALID_ARGUMENT, LIMIT_EXCEEDED. */
    engine_status (*world_create)(engine_handle engine, engine_handle* out);

    /* Destroys a world and every entity in it. Fails with: STALE_HANDLE, INVALID_ARGUMENT, TYPE_MISMATCH (not a world). */
    engine_status (*world_destroy)(engine_handle world);

    /* Creates an entity with no components and returns its key: 64 bits whose generation part makes a key of a destroyed entity fail with STALE_HANDLE.
     * Fails with: STALE_HANDLE, INVALID_ARGUMENT, TYPE_MISMATCH, LIMIT_EXCEEDED. */
    engine_status (*entity_create)(engine_handle world, uint64_t* out_key);

    /* Destroys an entity and its components. Fails with: STALE_HANDLE (the world or the entity), INVALID_ARGUMENT, TYPE_MISMATCH. */
    engine_status (*entity_destroy)(engine_handle world, uint64_t key);

    /* A component's `kind` is an engine_component_kind passed as a uint32_t (a number that is not a kind is INVALID_ARGUMENT, and a C++ enum could not hold it).
 * Sets a component from a struct (`value` points to the struct of the kind, its struct_size first; a smaller size than the first version is
     * STRUCT_SIZE). The component is added if the entity has none. Nothing of the world escapes: the engine copies. ENGINE_COMPONENT_WORLD_TRANSFORM is
     * read only (UNSUPPORTED), ENGINE_COMPONENT_NAME has its own functions (INVALID_ARGUMENT).
     * Fails with: STALE_HANDLE, INVALID_ARGUMENT, TYPE_MISMATCH, STRUCT_SIZE, UNSUPPORTED. */
    engine_status (*component_set)(engine_handle world, uint64_t key, uint32_t kind, const void* value);

    /* Reads a component into `out` (the host sets out->struct_size to its sizeof; the library writes at most that many bytes and sets it to what it wrote).
     * Fails with: STALE_HANDLE, INVALID_ARGUMENT, TYPE_MISMATCH, STRUCT_SIZE, NOT_FOUND (the entity has no such component). */
    engine_status (*component_get)(engine_handle world, uint64_t key, uint32_t kind, void* out);

    /* Removes a component. Fails with: STALE_HANDLE, INVALID_ARGUMENT, TYPE_MISMATCH, NOT_FOUND (the entity has none), UNSUPPORTED (world transform). */
    engine_status (*component_remove)(engine_handle world, uint64_t key, uint32_t kind);

    /* Tells whether an entity has a component: `*out` is 1 or 0. Fails with: STALE_HANDLE, INVALID_ARGUMENT, TYPE_MISMATCH. */
    engine_status (*component_has)(engine_handle world, uint64_t key, uint32_t kind, int* out);

    /* Gives an entity a name (UTF-8, at most ENGINE_NAME_MAX_BYTES bytes). Fails with: STALE_HANDLE, INVALID_ARGUMENT, TYPE_MISMATCH, TOO_LARGE. */
    engine_status (*entity_name_set)(engine_handle world, uint64_t key, engine_string name);

    /* Reads an entity's name into a buffer the host releases. Fails with: STALE_HANDLE, INVALID_ARGUMENT, TYPE_MISMATCH, NOT_FOUND (no name). */
    engine_status (*entity_name_get)(engine_handle world, uint64_t key, engine_buffer* out);

    /* Finds the entities that have every component of `mask` (ENGINE_COMPONENT_BIT bits; 0 matches every entity). Writes up to `capacity` keys to `keys`
     * (which may be NULL when `capacity` is 0) and the number of matches to `*total`: when it is above `capacity` the host calls again with a bigger
     * array. The order is not specified and may change when components are added or removed.
     * Fails with: STALE_HANDLE, INVALID_ARGUMENT (an unknown bit, NULL `total`), TYPE_MISMATCH. */
    engine_status (*query)(engine_handle world, uint32_t mask, uint64_t* keys, size_t capacity, size_t* total);

    /* Creates a counter: a count of jobs not yet finished. It counts as an object of the engine until counter_destroy.
     * Fails with: STALE_HANDLE, INVALID_ARGUMENT, LIMIT_EXCEEDED. */
    engine_status (*counter_create)(engine_handle engine, engine_handle* out);

    /* Destroys a counter. Fails with: STALE_HANDLE, INVALID_ARGUMENT, TYPE_MISMATCH, IN_USE (jobs submitted with it have not finished, or a thread waits on it). */
    engine_status (*counter_destroy)(engine_handle counter);

    /* Queues `fn(user)` to run on an engine worker thread (the engine starts its job system on the first call) and counts it on `counter` until it ends.
     * Any thread, a job included. At most 2048 jobs may be queued or running at once: past that the call fails and the host waits for some to finish.
     * Fails with: STALE_HANDLE, INVALID_ARGUMENT (NULL `fn`), TYPE_MISMATCH, LIMIT_EXCEEDED. */
    engine_status (*jobs_submit)(engine_handle engine, engine_job_fn fn, void* user, engine_handle counter);

    /* Reads how many jobs submitted with the counter have not finished. Fails with: STALE_HANDLE, INVALID_ARGUMENT, TYPE_MISMATCH. */
    engine_status (*counter_value)(engine_handle counter, uint64_t* out);

    /* Waits until the counter reaches zero, at most `timeout_ms` milliseconds (UINT64_MAX waits for ever). The calling thread sleeps; it does not run jobs.
     * Not from inside a job (INVALID_ARGUMENT).
     * Returns OK when the count is zero, NOT_READY when the time ran out. Fails with: STALE_HANDLE, INVALID_ARGUMENT, TYPE_MISMATCH, NOT_READY. */
    engine_status (*counter_wait)(engine_handle counter, uint64_t timeout_ms);

    /* Installs the engine's crash handler for the process: a crash writes a dump to the directory of the setting `crash.dump_directory` (empty: the
     * user's crash directory) and the handler that was there before is back after crash_handler_uninstall (or engine_destroy, which uninstalls).
     * Fails with: STALE_HANDLE, INVALID_ARGUMENT, ALREADY_EXISTS (installed), IO (the directory), UNSUPPORTED. */
    engine_status (*crash_handler_install)(engine_handle engine);

    /* Removes the engine's crash handler and restores the previous one. Fails with: STALE_HANDLE, INVALID_ARGUMENT, NOT_FOUND (not installed). */
    engine_status (*crash_handler_uninstall)(engine_handle engine);

    /* ---- Day 350: resources ---- */

    /* Adds a resource to the engine's memory backend (host-provided content, mounted above the files and paks: setting `assets.memory_priority`). The
     * blob is checked as a whole before anything changes. Without ENGINE_RESOURCE_BORROW the bytes are copied (the host may free them when the call
     * returns); with it the engine reads them in place and the host must keep them valid and unchanged until `release` runs. The budget is the setting
     * `assets.memory_backend_bytes`, lent bytes included.
     * Fails with: STALE_HANDLE, INVALID_ARGUMENT (zero id, bytes NULL, flags, release missing or given without BORROW), STRUCT_SIZE, ALREADY_EXISTS (the id
     * or the path is live: use resource_replace), CORRUPT (not a blob, the id inside is another, the hash does not verify or differs from
     * `expected_hash`), UNSUPPORTED_VERSION, TYPE_MISMATCH, LIMIT_EXCEEDED (the budget). */
    engine_status (*resource_add)(engine_handle engine, const engine_resource_add_info* info);

    /* Replaces the bytes of a resource in the memory backend. A load that is reading the old bytes finishes with them; the old bytes are released after
     * their last reader. The paths stay unless `info->path` names one. A loaded copy of the replaced resource is rebuilt from the new bytes before the call returns (its handles
     * stay valid; if the new blob cannot be loaded, for instance a dependency no longer matches, the old version stays loaded). If a loaded resource depends on this one the call
     * fails with IN_USE and writes those resources' ids (uint64_t values) to `*dependents` (a buffer the host releases; may be NULL) and changes nothing; with ENGINE_RESOURCE_FORCE
     * the dependents are unloaded first (their handles become stale: resource_status reports `loaded` 0) and their ids are written.
     * Fails with: those of resource_add except ALREADY_EXISTS (for a path of another resource), and NOT_FOUND (not in the memory backend), IN_USE. */
    engine_status (*resource_replace)(engine_handle engine, const engine_resource_add_info* info, engine_buffer* dependents);

    /* Removes a resource from the memory backend (flags: ENGINE_RESOURCE_FORCE), as resource_replace refuses or forces for dependents. A copy of the resource that is loaded and
     * has no dependents stays loaded for as long as its handles are held (it no longer has its bytes in the backend: a load of it fails with NOT_FOUND).
     * Fails with: STALE_HANDLE, INVALID_ARGUMENT (flags), NOT_FOUND, IN_USE. */
    engine_status (*resource_remove)(engine_handle engine, uint64_t id, uint32_t flags, engine_buffer* dependents);

    /* Reads what the memory backend holds of a resource (`out->struct_size` set by the host). Fails with: STALE_HANDLE, INVALID_ARGUMENT, STRUCT_SIZE, NOT_FOUND. */
    engine_status (*resource_mounted)(engine_handle engine, uint64_t id, engine_resource_info* out);

    /* Loads a resource and everything it depends on (a closure, atomically), or finds it loaded, and returns a handle that keeps it loaded. The resource is named by
     * `path` (resolved through the mounts' tables) when `path.length` is not 0, else by `id`. `type` is the type the host expects (a resource of another type is
     * TYPE_MISMATCH). Nothing of the loaded data is exposed: rendering and the other engine functions use the handle. The handle counts as an object of the engine until
     * resource_release. Fails with: STALE_HANDLE, INVALID_ARGUMENT, NOT_FOUND, MISSING_DEPENDENCY, TYPE_MISMATCH, DEPENDENCY_CYCLE, CORRUPT, UNSUPPORTED_VERSION, LIMIT_EXCEEDED, NOT_READY, UNSUPPORTED (no loader for the type). */
    engine_status (*resource_load)(engine_handle engine, uint64_t id, engine_string path, uint32_t type, engine_handle* out);

    /* Gives a resource handle back. Fails with: STALE_HANDLE, INVALID_ARGUMENT, TYPE_MISMATCH. */
    engine_status (*resource_release)(engine_handle resource);

    /* Reports a resource handle: `loaded` is 1 while the resource is still loaded, 0 after a forced replace or remove unloaded it. Fails with: STALE_HANDLE, INVALID_ARGUMENT, STRUCT_SIZE, TYPE_MISMATCH. */
    engine_status (*resource_status)(engine_handle resource, engine_resource_info* out);

    /* Starts loading a resource on the engine's workers and returns a ticket at once (a ticket that is already done when the resource is loaded). `priority` is
     * ENGINE_LOAD_*. The objects are built by calls of the host's own thread (frame, ticket_poll, ticket_wait, ticket_finish), never by an engine thread on its
     * own. The ticket counts as an object of the engine until ticket_destroy. Fails with: INVALID_ARGUMENT, UNSUPPORTED (no loader), LIMIT_EXCEEDED (too many tickets, STALE_HANDLE; every error of the load itself, a full queue included, comes with
     * ticket_finish). */
    engine_status (*resource_load_async)(engine_handle engine, uint64_t id, engine_string path, uint32_t type, uint32_t priority, engine_handle* ticket);

    /* Looks at a ticket without waiting (it also builds what is ready). Fails with: STALE_HANDLE, INVALID_ARGUMENT, STRUCT_SIZE, TYPE_MISMATCH. */
    engine_status (*ticket_poll)(engine_handle ticket, engine_ticket_info* out);

    /* Waits until the load is done, at most `timeout_ms` (UINT64_MAX waits for ever), building the objects as they are read. Not from inside a job or a release callback (INVALID_ARGUMENT).
     * Returns OK when done (the outcome comes from ticket_finish), NOT_READY on a timeout. Fails with: STALE_HANDLE, INVALID_ARGUMENT, TYPE_MISMATCH, NOT_READY. */
    engine_status (*ticket_wait)(engine_handle ticket, uint64_t timeout_ms);

    /* Takes the outcome of a finished load: OK and a resource handle in `*resource` (as resource_load's), or the load's error (and `*resource` is zero), CANCELLED after ticket_cancel.
     * Fails with: NOT_READY (not done), and the errors of resource_load, CANCELLED, STALE_HANDLE, INVALID_ARGUMENT, TYPE_MISMATCH. */
    engine_status (*ticket_finish)(engine_handle ticket, engine_handle* resource);

    /* Asks the load to stop; the ticket reads as done with CANCELLED soon after (unless it had finished). Fails with: STALE_HANDLE, INVALID_ARGUMENT, TYPE_MISMATCH. */
    engine_status (*ticket_cancel)(engine_handle ticket);

    /* Destroys a ticket, cancelling a load that has not finished. A resource the ticket would have given is released. Fails with: STALE_HANDLE, INVALID_ARGUMENT, TYPE_MISMATCH. */
    engine_status (*ticket_destroy)(engine_handle ticket);
    /* ---- Day 351: glTF import ---- */

        /* Imports a glTF (`.gltf` or `.glb`, in memory) and adds everything it makes (meshes, skeletons, clips,
         * textures, materials) to the engine's memory backend, atomically: when one resource is refused none is added.
         * `bytes` is borrowed for the call. The new resources load like any others (resource_load by id or by the paths
         * in `result`). Ids that are already in the backend are refused (ALREADY_EXISTS): to import a changed file
         * again with the same ids, resource_remove the old ones, then import with the `manifest` the first import
         * returned. Fails with: STALE_HANDLE, INVALID_ARGUMENT (NULL arguments, flags, a manifest that does not parse,
         * a prefix that is not a path, and a file that is not valid glTF: last_error names the glTF object),
         * STRUCT_SIZE, UNSUPPORTED (the library was built without the importer, or the file uses a feature the importer
         * does not handle), TOO_LARGE and LIMIT_EXCEEDED (a limit), NOT_FOUND and IO (read_file), CANCELLED
         * (read_file), ALREADY_EXISTS. `result` is zeroed on failure. */
        engine_status (*import_gltf)(engine_handle engine, const void* bytes, size_t size,
                                     const engine_import_options* options, engine_import_result* result);

        /* Starts the same import on an engine worker and returns a ticket at once. The engine copies `bytes`, the
         * options' strings and the manifest; the host's read_file (if any) and its user pointer must stay valid until
         * the ticket is destroyed. The ticket counts as an object of the engine until import_destroy. Fails with: the
         * argument errors of import_gltf, UNSUPPORTED, LIMIT_EXCEEDED (too many tickets). The import's own errors come
         * with import_finish. */
        engine_status (*import_gltf_async)(engine_handle engine, const void* bytes, size_t size,
                                           const engine_import_options* options, engine_handle* ticket);

        /* Looks at an import ticket without waiting. Fails with: STALE_HANDLE, INVALID_ARGUMENT, STRUCT_SIZE,
         * TYPE_MISMATCH. */
        engine_status (*import_poll)(engine_handle ticket, engine_import_info* out);

        /* Waits until the import is done, at most `timeout_ms` (UINT64_MAX waits for ever). Not from inside a job.
         * Returns OK when done, NOT_READY on a timeout. Fails with: STALE_HANDLE, INVALID_ARGUMENT, TYPE_MISMATCH,
         * NOT_READY. */
        engine_status (*import_wait)(engine_handle ticket, uint64_t timeout_ms);

        /* Takes the outcome of a finished import: adds the resources to the memory backend (as import_gltf does) and
         * fills `result`, or reports the import's error (and zeroes `result`), CANCELLED after import_cancel. Once
         * only: a second call fails with NOT_FOUND. Fails with: NOT_READY (not done), the errors of import_gltf,
         * CANCELLED, STALE_HANDLE, INVALID_ARGUMENT, STRUCT_SIZE, TYPE_MISMATCH. */
        engine_status (*import_finish)(engine_handle ticket, engine_import_result* result);

        /* Asks the import to stop at its next step; it then reads as done with CANCELLED. Fails with: STALE_HANDLE,
         * INVALID_ARGUMENT, TYPE_MISMATCH. */
        engine_status (*import_cancel)(engine_handle ticket);

        /* Destroys an import ticket, cancelling an import that has not finished and waiting for its job to end. Fails
         * with: STALE_HANDLE, INVALID_ARGUMENT, TYPE_MISMATCH, IN_USE. */
        engine_status (*import_destroy)(engine_handle ticket);

        /* ---- Day 352: rendering into a window the host owns ---- */

        /* Makes a surface: the engine renders into the host's window on every `frame` (a clear to the background color in this version). A device is made for
         * the window (one per surface). Counts as an object of the engine until surface_destroy. Fails with: STALE_HANDLE, INVALID_ARGUMENT (NULL, flags,
         * missing handles, a size or scale out of range), STRUCT_SIZE, UNSUPPORTED (the kind is not in engine_info.caps, or no GPU can present to the
         * window), LIMIT_EXCEEDED (64 surfaces). */
        engine_status (*surface_create)(engine_handle engine, const engine_surface_info* info, engine_handle* out);

        /* Destroys a surface and its device (read-back tickets of the surface fail with CANCELLED). Call it before the window goes. Fails with: STALE_HANDLE,
         * INVALID_ARGUMENT, TYPE_MISMATCH. */
        engine_status (*surface_destroy)(engine_handle surface);

        /* Forwards one input event of the host's window to the engine's input stream for the surface (the UI toolkit, the camera controllers and the demos read it as from
         * a window of their own). Call it from the host's frame thread, in the order the host got the events. A full queue (1024) drops the event and counts it in
         * surface_state. Fails with: STALE_HANDLE, INVALID_ARGUMENT (NULL, unknown type or key or button, text that is not UTF-8 or longer than 256 bytes), STRUCT_SIZE,
         * TYPE_MISMATCH. */
        engine_status (*surface_input)(engine_handle surface, const engine_input_event* event);

        /* Tells the engine the window's size in pixels and its scale (0 means 1). A size of 0 in either dimension pauses rendering (the window is hidden or
         * minimized); the next frame after a real size resumes. Call from the host's frame thread, before `frame`. Fails with: STALE_HANDLE, INVALID_ARGUMENT,
         * TYPE_MISMATCH. */
        engine_status (*surface_resized)(engine_handle surface, uint32_t width, uint32_t height, float scale);

        /* Rebuilds the swapchain on a window the host re-created or re-mapped (after `lost` in surface_state, or for a new native window of the same kind),
         * through the device reset path. Fails with: those of surface_create, UNSUPPORTED (the device cannot present to the new window; the surface stays lost). */
        engine_status (*surface_rebind)(engine_handle surface, const engine_surface_info* info);

        /* Sets the color the surface is cleared to (four values from 0 to 1, RGBA). Fails with: STALE_HANDLE, INVALID_ARGUMENT, TYPE_MISMATCH. */
        engine_status (*surface_set_background)(engine_handle surface, const float* rgba);

        /* Reads the state of a surface (`out->struct_size` set by the host). Fails with: STALE_HANDLE, INVALID_ARGUMENT, STRUCT_SIZE, TYPE_MISMATCH. */
        engine_status (*surface_state)(engine_handle surface, engine_surface_state* out);

        /* Asks for the pixels of the next frame the surface presents and returns a ticket at once (the frame is rendered by `frame`). The ticket counts as an
         * object of the engine until pixels_destroy. Fails with: STALE_HANDLE, INVALID_ARGUMENT, TYPE_MISMATCH, LIMIT_EXCEEDED (256 tickets). */
        engine_status (*surface_read_pixels)(engine_handle surface, engine_handle* ticket);

        /* Takes the pixels of a finished read-back (once). Fails with: NOT_READY (no frame has been presented since the request), CANCELLED (the surface was
         * destroyed), UNSUPPORTED (the frame could not be read), NOT_FOUND (taken already), STALE_HANDLE, INVALID_ARGUMENT, STRUCT_SIZE, TYPE_MISMATCH. */
        engine_status (*pixels_finish)(engine_handle ticket, engine_pixels* out);

        /* Destroys a read-back ticket (and the pixels it still holds). Fails with: STALE_HANDLE, INVALID_ARGUMENT, TYPE_MISMATCH. */
        engine_status (*pixels_destroy)(engine_handle ticket);
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
