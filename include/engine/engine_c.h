/* engine_c.h: the engine's C API, version 1 (Phase 23 Day 346). Valid C11 (tools/abi_check compiles it as C).
 *
 * CONVENTIONS. Every function of the API follows these; a new function that breaks one is a defect.
 *
 * Entry. The library exports ONE function, engine_get_api. It returns a table of function pointers (engine_api);
 * nothing else is exported. A host that links the library directly calls it like any function, a host that loads the
 * library at run time finds it with dlsym or GetProcAddress. The table is static: it lives as long as the library stays
 * loaded, and the same pointer comes back from every call.
 *
 * Versions. engine_abi_version.h holds MAJOR.MINOR. A different MAJOR is a different, incompatible API. A larger MINOR
 * only adds functions at the end of the table and fields at the end of structs. A host asks for the version it was
 * written against; the library serves any minor up to its own with the same major, and refuses a newer one
 * (ENGINE_ERROR_UNSUPPORTED_VERSION). A host that wants a function newer than the library it finds checks
 * `table->struct_size` (the table says how many bytes of itself the library filled) before it reads the pointer.
 *
 * Status. Every function that can fail returns an engine_status; ENGINE_OK is zero. A failing call changes nothing it
 * was asked to change. The text of the most recent failure on the calling thread is available from
 * engine_api.last_error (a message for people; never branch on it).
 *
 * Strings. engine_string is a pointer and a length: UTF-8, not NUL-terminated, `data` may be NULL only when `length` is
 * 0. A string a host passes in is borrowed for the duration of the call. A string the library hands out is either a
 * static string (engine_api.status_name) or inside an engine_buffer.
 *
 * Ownership. Nothing allocated by one side is freed by the other. Memory the library produces comes in an
 * engine_buffer; the host reads it, then calls engine_buffer_release exactly once (the library's own release function
 * does the freeing, in the library's allocator). The library never keeps a pointer into host memory past the call that
 * received it, except a user pointer it was given for a callback (below).
 *
 * Structs. Every struct that may grow starts with `uint32_t struct_size`. A struct passed IN: the host sets struct_size
 * to sizeof of the struct as the host compiled it; the library reads only that many bytes, treats the fields past them
 * as zero, and refuses a size smaller than the first version of the struct (ENGINE_ERROR_STRUCT_SIZE). A struct passed
 * OUT: the host sets struct_size to its sizeof; the library writes at most that many bytes and sets struct_size to the
 * number it wrote. Padding is never relied on: initialize a struct with `{0}` before filling it.
 *
 * Handles. An object is named by an engine_handle: 64 bits, zero is "no object", the low 32 bits say which slot, the
 * high 32 bits which generation of the slot. A handle of a destroyed object fails with ENGINE_ERROR_STALE_HANDLE, a
 * handle that never existed with ENGINE_ERROR_INVALID_ARGUMENT; neither crashes. Handles are plain values: copy them,
 * store them, pass them between threads.
 *
 * Callbacks. A callback is a function pointer and a `void* user` the host chose, passed back unchanged as its first
 * argument. The thread a callback runs on and what it may call are documented where the callback is declared. A
 * callback never throws, never longjmps and never unwinds across the boundary.
 *
 * Threads. Every function may be called from any thread unless its comment says otherwise; engine_create and
 * engine_destroy are serialized with each other and with every other call on the same engine by the host (the library
 * does not wait for calls still running when an engine is destroyed).
 *
 * Process. One engine exists per process on this version: the engine's singletons (the allocator, the log, the job
 * system, the crash handler) are the process's. A second engine_create while one exists fails with
 * ENGINE_ERROR_LIMIT_EXCEEDED.
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

/* ENGINE_C_API is empty for a host. The library's own definition of engine_get_api carries the export attribute
 * (ENGINE_API of foundation/build_defs.h). */
#ifndef ENGINE_C_API
#define ENGINE_C_API
#endif

    /* ---- status
     * ------------------------------------------------------------------------------------------------------------------------------------
     */

    /* What a call returns. The numbers are part of the ABI: a value is never renumbered or reused, new codes are added
     * at the end. */
    typedef enum engine_status
    {
        ENGINE_OK = 0,                     /* Success. */
        ENGINE_ERROR_INVALID_ARGUMENT = 1, /* A pointer is NULL where one is required, a value is out of range, a key or
                                              value is malformed, a flag is unknown. */
        ENGINE_ERROR_STRUCT_SIZE = 2,      /* A struct's struct_size is smaller than the struct's first version. */
        ENGINE_ERROR_UNSUPPORTED_VERSION = 3, /* The host asked for another major, or a newer minor, than the library
                                                 serves; or a stored format is not current. */
        ENGINE_ERROR_STALE_HANDLE = 4,        /* The handle names an object that has been destroyed. */
        ENGINE_ERROR_NOT_FOUND = 5,           /* A named thing does not exist: a setting key, a path, an id. */
        ENGINE_ERROR_ALREADY_EXISTS = 6,      /* Two things claim one name or id. */
        ENGINE_ERROR_CORRUPT = 7,             /* Bytes are damaged. */
        ENGINE_ERROR_UNSUPPORTED = 8,     /* The feature, platform or target is not supported (or the setting needs a
                                             capability that is missing). */
        ENGINE_ERROR_IO = 9,              /* The medium failed. */
        ENGINE_ERROR_CANCELLED = 10,      /* The operation was cancelled and changed nothing that was committed. */
        ENGINE_ERROR_LIMIT_EXCEEDED = 11, /* A limit (a count, a budget, one engine per process) is exceeded. */
        ENGINE_ERROR_NOT_READY = 12,      /* The answer needs something that has not arrived yet. */
        ENGINE_ERROR_IN_USE = 13,         /* The thing cannot be removed or replaced while something depends on it. */
        ENGINE_ERROR_TYPE_MISMATCH = 14,  /* The thing is not of the type the caller asked for. */
        ENGINE_ERROR_MISSING_DEPENDENCY = 15, /* A dependency does not exist. */
        ENGINE_ERROR_DEPENDENCY_CYCLE = 16,   /* Things depend on each other in a cycle. */
        ENGINE_ERROR_OFFLINE = 17,            /* The network is needed and is not there. */
        ENGINE_ERROR_TOO_LARGE = 18,          /* A size is above a bound. */
        ENGINE_ERROR_DENIED = 19,  /* An administrator's policy holds the value: the program cannot change it. */
        ENGINE_ERROR_INTERNAL = 20 /* The engine failed in a way it has no better word for (the message says more). */
    } engine_status;

    /* ---- strings, buffers, handles
     * -----------------------------------------------------------------------------------------------------------------
     */

    /* A UTF-8 string by pointer and length (not NUL-terminated). */
    typedef struct engine_string
    {
        const char* data;
        size_t length;
    } engine_string;

    /* Memory the library produced for the host. `data` and `size` are what to read; `release` frees them. Call
     * engine_buffer_release once, from any thread, before the library is unloaded. A zeroed buffer (release NULL) owns
     * nothing and releasing it does nothing. */
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

    /* ---- the engine object
     * -------------------------------------------------------------------------------------------------------------------------
     */

    typedef enum engine_log_level
    {
        ENGINE_LOG_ERROR = 0,
        ENGINE_LOG_WARN = 1,
        ENGINE_LOG_INFO = 2,
        ENGINE_LOG_DEBUG = 3,
        ENGINE_LOG_TRACE = 4
    } engine_log_level;

    /* The log callback. Called once for each line the engine emits (after the channel's own filter, so the
     * `log.<channel>.verbosity` settings decide what arrives), on the thread that logged, possibly from several threads
     * at once. It must return quickly, must not throw, and must not call engine_create or engine_destroy (destroy waits
     * for callbacks in flight). `channel` and `message` are valid only during the call: copy what you keep. */
    typedef void (*engine_log_fn)(void* user, engine_log_level level, engine_string channel, engine_string message);

    /* One setting given at creation: a key as `engine_settings` declares it (`log.core.verbosity`) and its value as
     * text (`debug`, `1280`, `true`). */
    typedef struct engine_setting_entry
    {
        engine_string key;
        engine_string value;
    } engine_setting_entry;

/* engine_create_info.flags */
#define ENGINE_CREATE_LOG_SUPPRESS_CONSOLE                                                                             \
    0x1u /* lines sent to the log callback are not also written to the console                                         \
          */
#define ENGINE_CREATE_FLAGS_MASK 0x1u

    /* What engine_create needs. Fields are listed in the order they were added; a later minor appends. */
    typedef struct engine_create_info
    {
        uint32_t struct_size;
        uint32_t flags;         /* ENGINE_CREATE_* bits; an unknown bit is ENGINE_ERROR_INVALID_ARGUMENT */
        engine_string app_name; /* names the program in messages; empty means "engine" */
        const engine_setting_entry*
            settings; /* the host's settings, `setting_count` of them, a layer below the command line */
        size_t setting_count;
        const engine_string* args; /* the command line as main sees it (args[0] is the program name and is skipped),
                                      `arg_count` of them */
        size_t arg_count;
        engine_log_fn log; /* or NULL */
        void* log_user;
    } engine_create_info;

    /* What engine_api.get_info reports. */
    typedef struct engine_info
    {
        uint32_t struct_size;
        uint32_t setting_count; /* how many settings the engine declares */
        uint32_t caps; /* ENGINE_CAP_* bits: what this build of the library has (written when struct_size covers it) */
        uint32_t reserved;
    } engine_info;

/* Bits of engine_info.caps. */
#define ENGINE_CAP_IMPORT_GLTF                                                                                         \
    0x1u /* the glTF importer is built in (import_gltf and import_gltf_async work; without it they fail with           \
            UNSUPPORTED) */
#define ENGINE_CAP_SURFACE_X11 0x2u     /* surface_create renders into an X11 window the host owns (Day 352) */
#define ENGINE_CAP_SURFACE_WAYLAND 0x4u /* surface_create renders into a Wayland wl_surface the host owns (Day 352) */
#define ENGINE_CAP_READBACK 0x8u        /* surface_read_pixels works (Day 352) */
#define ENGINE_CAP_SURFACE_COCOA                                                                                       \
    0x10u /* surface_create renders with Metal into an NSView or CAMetalLayer the host owns (macOS, Day 355) */
#define ENGINE_CAP_SURFACE_WIN32                                                                                       \
    0x20u /* surface_create renders into a Win32 window (HWND) the host owns, with Direct3D 12 or Vulkan (Day 354) */
#define ENGINE_CAP_WORLD_ON_SURFACE                                                                                    \
    0x40u                              /* surface_set_world draws a world's meshes on a surface (Phase 24 Day 359)     \
                                        */
#define ENGINE_CAP_HOST_TEXTURES 0x80u /* texture_create and the host textures (Phase 24 Day 365) */
#define ENGINE_CAP_SURFACE_OFFSCREEN                                                                                   \
    0x100u /* surface_create with ENGINE_SURFACE_OFFSCREEN: a surface with no window (Phase 24 Day 378) */
#define ENGINE_CAP_AUDIO_PUSH_STREAMS                                                                                  \
    0x400u /* audio_stream_open_push, audio_stream_push, audio_stream_stats and the stream field of the audio source   \
              work (maintenance, 2026-11-18) */
#define ENGINE_CAP_SCRIPT                                                                                              \
    0x800u /* script_load, script_call, script_register_function, script_set_budget, script_stats, the event and task  \
              functions work, and an entity can carry ENGINE_COMPONENT_SCRIPT (Phase 27 Day 471): Linux, Windows and   \
              macOS x86-64; a build without the script module fails them with UNSUPPORTED */
#define ENGINE_CAP_RESOURCE_CACHE                                                                                      \
    0x200u /* resource_read, resource_add_many (and its asynchronous form) and import_format_version work: set in      \
              every build, with or without the importer (maintenance, 2026-11-18) */

/* ---- Day 352: rendering into a window the host owns ---- */

/* Which kind of native window engine_surface_info names. */
#define ENGINE_SURFACE_X11 1u /* `display` is the Display*, `window_id` the X11 Window */
#define ENGINE_SURFACE_WAYLAND                                                                                         \
    2u /* `display` is the wl_display*, `surface` the wl_surface* (the host's own connection and surface) */
/* macOS (Day 355): `display` is the host's NSView* (required), `surface` a CAMetalLayer* the host made for it, or NULL.
 * With NULL, the view's own layer is used when it is a CAMetalLayer, and otherwise the engine adds a CAMetalLayer of
 * its own as a sublayer that fills the view (and removes it at surface_destroy and surface_rebind). surface_create,
 * surface_rebind, surface_resized and surface_destroy of a Cocoa surface are called on the main thread
 * (INVALID_ARGUMENT otherwise), and frame draws it only there: a frame on another thread skips it and fails its pending
 * read-backs with INVALID_ARGUMENT. */
#define ENGINE_SURFACE_COCOA 3u
#define ENGINE_SURFACE_WIN32                                                                                           \
    4u /* `display` is the HINSTANCE of the window's class (GetModuleHandleW(NULL) for a class of the program),        \
          `surface` the HWND (Day 354) */
/* Phase 24 Day 378: a surface with no window. The engine draws into a colour target of its own, `width` by `height`
 * pixels (RGBA8, sRGB-encoded as a window shows it), and the host reads the pictures with surface_read_pixels (and,
 * from Day 379, surface_acquire_frame). Only `kind`, `flags` (the graphics API; 0 is the platform's own), `width`,
 * `height` and `scale` are read; `display`, `surface`, `window_id`, `present_mode` and `callbacks` are ignored (a
 * `callbacks` table is still used for the cursor, pointer lock and clipboard requests the engine's UI makes). The
 * host forwards the input of its own canvas with surface_input in the surface's pixels. surface_resized changes the
 * size of the target (a size of 0 makes the surface draw nothing). Needs ENGINE_CAP_SURFACE_OFFSCREEN. */
#define ENGINE_SURFACE_OFFSCREEN 5u

/* engine_surface_info.flags: which graphics API draws into the window. Neither bit is the platform's own (Direct3D 12
 * on Windows, Vulkan on Linux, Metal on macOS, which has no bit); both at once is INVALID_ARGUMENT; one that this
 * platform has not is UNSUPPORTED. A surface keeps its API across surface_rebind. */
#define ENGINE_SURFACE_FLAG_VULKAN 0x1u
#define ENGINE_SURFACE_FLAG_D3D12 0x2u

    /* Input forwarded by the host (Day 353). The values of engine_key are those of the engine's own key enumeration
     * (layout-aware keys: the symbol the layout produces), 0 is "unknown". */
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
        ENGINE_INPUT_SCROLL = 6, /* `x`, `y` are the notches turned (positive is right and down) */
        ENGINE_INPUT_TEXT = 7,   /* `text`: UTF-8 the keyboard produced (one or more scalars, at most 256 bytes) */
        ENGINE_INPUT_FOCUS = 8,  /* `down` 1: the host's view got keyboard focus, 0: lost it */
        ENGINE_INPUT_TOUCH = 9 /* `touch_id`, `down`, `x`, `y`: the first finger down acts as the pointer with the left
                                  button; the others are ignored */
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

    /* What the host does for the engine's requests on a surface. Every callback may be NULL (the request is refused).
     * They run on the thread that drives the engine's UI for the surface (the host's frame thread), never from an
     * engine worker, and must not call back into the engine. */
    typedef struct engine_surface_callbacks
    {
        uint32_t struct_size;
        uint32_t reserved;
        void* user;
        int (*set_cursor)(void* user, uint32_t shape);         /* engine_cursor; nonzero when applied */
        int (*set_pointer_lock)(void* user, int locked);       /* nonzero when done */
        int (*clipboard_set)(void* user, engine_string text);  /* UTF-8; nonzero when stored */
        int (*clipboard_get)(void* user, engine_string* text); /* fills `*text` (valid until the next callback) and
                                                                  returns nonzero, or returns 0 for no text */
        void (*text_input_rect)(void* user, int32_t x, int32_t y, int32_t width,
                                int32_t height); /* the caret, in window pixels */
        void (*request_focus)(void* user, int focused);
    } engine_surface_callbacks;

    /* A window the host owns. The engine never creates, moves, resizes or destroys it: the host keeps it alive until
     * surface_destroy returns, and tells the engine its size and scale with surface_resized. */
    typedef struct engine_surface_info
    {
        uint32_t struct_size;
        uint32_t kind;  /* ENGINE_SURFACE_* */
        uint32_t flags; /* ENGINE_SURFACE_FLAG_*, or 0 for the platform's own graphics API */
        uint32_t width; /* in pixels */
        uint32_t height;
        float scale;           /* the DPI scale of the window; 0 means 1 */
        uint32_t present_mode; /* ENGINE_PRESENT_*: what the host wants; the surface falls back to FIFO when it cannot
                                  (engine_surface_state.present_mode says what it got) */
        void* display;
        void* surface;
        uint64_t window_id;
        const engine_surface_callbacks* callbacks; /* or NULL: copied by surface_create and surface_rebind */
    } engine_surface_info;

#define ENGINE_PRESENT_FIFO 0u      /* wait for the display's refresh; always available */
#define ENGINE_PRESENT_MAILBOX 1u   /* replace the queued image with the newest, no tearing */
#define ENGINE_PRESENT_IMMEDIATE 2u /* present at once, possibly tearing */

    /* What engine_api.surface_state reports. */
    typedef struct engine_surface_state
    {
        uint32_t struct_size;
        uint32_t image_width; /* of the swapchain, 0 when there is none */
        uint32_t image_height;
        uint32_t visible;      /* 1 when the last frame was presented */
        uint32_t lost;         /* 1 when the window is gone from under the engine: call surface_rebind */
        uint32_t present_mode; /* ENGINE_PRESENT_* in use */
        uint64_t frames_presented;
        uint64_t recoveries;     /* how many times surface_rebind rebuilt the swapchain */
        uint64_t frames_skipped; /* frames that drew nothing (zero size, lost) */
        uint64_t input_dropped; /* input events dropped because the queue (1024) was full: the engine's UI did not drain
                                   it */
        int32_t world_status;   /* ENGINE_OK, or why the world set with surface_set_world was unbound (STALE_HANDLE: it
                                   was destroyed under the surface); the surface is back to its background color then */
        uint32_t objects_drawn; /* objects the last frame of the bound world drew */
        uint64_t entities_unresolved; /* entities of the bound world in the last frame whose mesh or material did not
                                         resolve (not mounted, not of the type, or broken); they draw nothing */
        int32_t camera_status;  /* ENGINE_OK while the camera named with surface_set_camera is in use (or none is named:
                                   the default camera), else why the surface fell back to the default camera: NOT_FOUND
                                   (the entity was destroyed, or has no engine_camera or no transform) */
        uint64_t camera_entity; /* the entity named with surface_set_camera, 0 for the default camera */
        uint32_t
            lights_refused; /* lights of the bound world the last frame could not use (the surface has room for 64) */
        uint32_t reserved;  /* zero */
        uint64_t textures_uploaded;      /* uploads of host textures to this surface's device (Phase 24 Day 366) */
        uint64_t texture_bytes_uploaded; /* bytes of those uploads */
        uint32_t labels_drawn;           /* labels the last frame drew (Phase 24 Day 367) */
        uint32_t labels_refused;  /* labels of the bound world the last frame did not draw: no font, no room, or behind
                                     the camera counts as not drawn only when the font is missing */
        uint64_t frames_rendered; /* off-screen surfaces (Phase 24 Day 379): frames drawn into the target */
        uint64_t frames_acquired; /* frames the host took with surface_acquire_frame */
        uint64_t frames_dropped;  /* finished frames the host never took because a newer one was ready */
        uint64_t frames_skipped_slow_host; /* frames whose copy was skipped because every ring slot was in flight or
                                              unread */
    } engine_surface_state;

    /* A finished picture of an off-screen surface (surface_acquire_frame): RGBA8, sRGB-encoded as a window shows it,
     * straight alpha, `stride` = width * 4, top row first. `data` is a copy the engine keeps for the host: valid until
     * surface_release_frame or surface_destroy, readable from any one thread, and never changed by later frames. */
    typedef struct engine_frame_image
    {
        uint32_t struct_size;
        uint32_t width;
        uint32_t height;
        uint32_t stride;
        const void* data;
        uint64_t frame;   /* counts the frames the surface rendered; the first is 1 */
        uint64_t dropped; /* finished frames between the previous acquire and this one that were never taken */
    } engine_frame_image;

    /* The pixels of a rendered frame: RGBA8, `stride` bytes per row (width * 4), top row first. `data` is the host's to
     * release. */
    typedef struct engine_pixels
    {
        uint32_t struct_size;
        uint32_t width;
        uint32_t height;
        uint32_t stride;
        engine_buffer data;
    } engine_pixels;

    /* Which sanitizer the library was built with. A sanitized library loads only into a host built with the same one.
     */
    typedef enum engine_sanitizer
    {
        ENGINE_LIB_SANITIZER_NONE = 0,
        ENGINE_LIB_SANITIZER_ADDRESS = 1,
        ENGINE_LIB_SANITIZER_THREAD = 2
    } engine_sanitizer;

    /* ---- frames, worlds, entities, components, jobs (Phase 23 Day 349)
     * ---------------------------------------------------------------------------- */

    /* What engine_api.frame is given. */
    typedef struct engine_frame_info
    {
        uint32_t struct_size;
        uint32_t flags;       /* must be 0 */
        double delta_seconds; /* the time step the systems see */
        uint64_t budget_us;   /* microseconds the frame may take before it stops starting new work; 0 means no budget */
    } engine_frame_info;

    /* What engine_api.frame reports. */
    typedef struct engine_frame_stats
    {
        uint32_t struct_size;
        uint32_t worlds_run;     /* worlds whose systems ran */
        uint32_t worlds_skipped; /* worlds left for the next frame because the budget was spent */
        uint32_t entities;       /* entities alive in the worlds that ran */
        uint64_t frame_index;    /* frames run so far, this one included, from 1 */
        uint64_t elapsed_us;     /* wall time of this call */
        /* Phase 24 Days 365-366: host textures, since the previous frame call */
        uint64_t textures_uploaded; /* uploads of host textures to surfaces' devices, one per texture and device */
        uint64_t bytes_uploaded;    /* bytes of those uploads */
        uint64_t updates_replaced;  /* texture_update calls whose pixels were replaced by a newer update before any
                                       device uploaded them */
        uint64_t slots_reverted;    /* material slots that went back to their own value because their host texture was
                                       destroyed or had no pixels */
        /* Phase 25 Day 408: physics */
        uint64_t physics_us;     /* time spent making the physics bodies follow the entities (the step itself is part of
                                    the worlds' systems) */
        uint32_t physics_bodies; /* bodies alive in the worlds that ran */
        uint32_t physics_steps;  /* fixed steps the worlds that ran made in this frame */
        /* glTF colliders (2026-11-20): the static colliders of spawned levels and mesh shapes, made at the end of the
         * frame that first saw their entity's world matrix, at that world scale */
        uint32_t colliders_made;    /* colliders made in this frame */
        uint32_t colliders_refused; /* colliders that could not be made (a shear or zero scale in the entity's world
                                       matrix, a mesh with no triangle left at that scale, a bad size) */
        uint32_t collider_scale_changes_ignored; /* colliders whose entity's world scale changed after they were made
                                                    (each counted once; the collider keeps its shape) */
        uint32_t collider_triangles_dropped;     /* mesh triangles a world scale made degenerate, left out */
    } engine_frame_stats;

    /* The components a host can set on an entity. The numbers are part of the ABI. ENGINE_COMPONENT_BIT(kind) is the
     * bit of a kind in a query's mask. */
    typedef enum engine_component_kind
    {
        ENGINE_COMPONENT_TRANSFORM = 1, /* engine_transform: the entity's place relative to its parent, or to the world
                                         */
        ENGINE_COMPONENT_WORLD_TRANSFORM = 2, /* engine_world_transform: where the frame's transform system put it; read
                                                 only */
        ENGINE_COMPONENT_NAME = 3,            /* text, up to ENGINE_NAME_MAX_BYTES bytes; set and read with
                                                 engine_api.entity_name_set/get */
        ENGINE_COMPONENT_MESH_REFERENCE = 4,  /* engine_mesh_reference: which mesh and material the entity draws */
        ENGINE_COMPONENT_CAMERA = 5,          /* engine_camera: a perspective camera (Phase 24 Day 360) */
        ENGINE_COMPONENT_LIGHT = 6,           /* engine_light: a directional or point light (Phase 24 Day 361) */
        ENGINE_COMPONENT_VISIBILITY = 7,      /* engine_visibility: hides an entity and its children (Day 362) */
        ENGINE_COMPONENT_SKELETON_INSTANCE = 8, /* engine_skeleton_instance: the skeleton a skinned mesh is drawn with
                                                   (Day 363) */
        ENGINE_COMPONENT_ANIMATION_PLAYER = 9,  /* engine_animation_state: what animation_play started; read only (Day
                                                   363) */
        ENGINE_COMPONENT_LABEL = 10,            /* engine_label: text drawn at the entity's place (Phase 24 Day 367) */
        ENGINE_COMPONENT_RIGID_BODY = 11,       /* engine_rigid_body: a physics body (Phase 25 Day 408) */
        ENGINE_COMPONENT_COLLIDER = 12,         /* engine_collider: the shape of a physics body (Phase 25 Day 408) */
        ENGINE_COMPONENT_JOINT = 13,            /* engine_joint: ties two entities' bodies (Phase 25 Day 409) */
        ENGINE_COMPONENT_CHARACTER = 14,        /* engine_character: a capsule character controller (Day 409) */
        ENGINE_COMPONENT_AUDIO_SOURCE = 15,     /* engine_audio_source: a sound at the entity's place (Phase 26) */
        ENGINE_COMPONENT_AUDIO_LISTENER = 16,   /* engine_audio_listener: the ears (Phase 26) */
        ENGINE_COMPONENT_SCRIPT = 17, /* engine_script_component: a behavior of a loaded script (Phase 27 Day 471) */
        ENGINE_COMPONENT_MORPH_WEIGHTS = 18, /* engine_morph_weights: the weights of a morphed mesh's targets
                                                (maintenance, 2026-11-20) */
        ENGINE_COMPONENT_TERRAIN = 19 /* engine_terrain: a height-map terrain drawn at the entity's place (Phase 28) */
    } engine_component_kind;

#define ENGINE_COMPONENT_BIT(kind) (1u << ((kind) - 1))
#define ENGINE_COMPONENT_MASK_ALL 0x7FFFFu
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

    /* The mesh an entity draws. `mesh` is the resource id of a mesh the host added or loaded (Day 350) and
     * `material_resource` that of a material (0: the surface's default grey material); a surface bound to the entity's
     * world (surface_set_world, Day 359) draws it, and an entity whose mesh or material is not mounted or is not of the
     * type draws nothing and is counted in engine_surface_state.entities_unresolved. `material` is a number of the
     * host's own: the engine stores and returns it and never reads it. */
    typedef struct engine_mesh_reference
    {
        uint32_t struct_size;
        uint32_t material;
        uint64_t mesh;
        uint64_t material_resource;
    } engine_mesh_reference;

    /* Bits of engine_camera.flags. */
#define ENGINE_CAMERA_FIXED_ASPECT 0x1u /* use engine_camera.aspect instead of the surface's width over height */

    /* A perspective camera (Phase 24 Day 360). The camera looks along its entity's -Z with +Y up and +X right (the
     * engine's convention: the entity's world matrix, as the frame's transform system computed it, is the camera's
     * placement; scale is ignored), so setting the entity's transform each frame is the whole camera control. An
     * entity that is a camera needs ENGINE_COMPONENT_TRANSFORM too. `fov_y` is the vertical field of view in radians in
     * (0, pi); `near_plane` > 0 and `far_plane` > `near_plane`, in metres. `aspect` is used only with
     * ENGINE_CAMERA_FIXED_ASPECT (> 0); otherwise the surface's width over height is. */
    typedef struct engine_camera
    {
        uint32_t struct_size;
        uint32_t flags; /* ENGINE_CAMERA_* bits; an unknown bit is INVALID_ARGUMENT */
        float fov_y;
        float near_plane;
        float far_plane;
        float aspect;
    } engine_camera;

    /* Whether an entity is drawn (Phase 24 Day 362). An entity without this component is visible. `hidden` is 0 or 1;
     * a hidden entity and every entity below it in the hierarchy (entity_set_parent) are not drawn, and are drawn again
     * the frame the flag is cleared (set `hidden` to 0 or remove the component). Hiding changes nothing else: the
     * entity keeps its transform, its mesh reference and its animation. */
    typedef struct engine_visibility
    {
        uint32_t struct_size;
        uint32_t hidden;
    } engine_visibility;

    /* The skeleton of a skinned mesh (Phase 24 Day 363). `skeleton` is the resource id of a skeleton the host added or
     * loaded; setting the component loads it (NOT_FOUND when it is not mounted, TYPE_MISMATCH when it is not a
     * skeleton) and keeps it loaded until the engine is destroyed. An entity with a skeleton instance and a mesh
     * reference whose mesh was made for the skeleton is drawn skinned with a skinned material (the surface's default
     * skinned material when `material_resource` is 0); without a clip it is drawn in its bind pose. */
    typedef struct engine_skeleton_instance
    {
        uint32_t struct_size;
        uint32_t reserved; /* zero */
        uint64_t skeleton;
    } engine_skeleton_instance;

#define ENGINE_MORPH_WEIGHTS_MAX 64u

    /* The weights of a morphed mesh's targets (maintenance, 2026-11-20): the entity's mesh is drawn as its base shape
     * plus weights[i] times target i's offsets, for the first `count` targets (at most ENGINE_MORPH_WEIGHTS_MAX;
     * targets beyond `count`, or beyond the mesh's own target count, stay at 0). Each weight is finite (glTF allows any
     * value; 0 to 1 is usual). An imported level brings the weights its file gives a mesh node, and a clip with weight
     * tracks (animation_play) writes them every frame. An entity without the component is drawn in its base shape. */
    typedef struct engine_morph_weights
    {
        uint32_t struct_size;
        uint32_t count;
        float weights[ENGINE_MORPH_WEIGHTS_MAX];
    } engine_morph_weights;

#define ENGINE_TERRAIN_LAYERS_MAX 4u
#define ENGINE_TERRAIN_COLLIDER 0x1u /* engine_terrain.flags: also make the terrain a static height-field collider */

    /* One layer of a terrain's surface: `tiling` is the metres across one repeat of the layer's texture (above 0),
     * `tint` multiplies the albedo (not negative), `roughness` and `metallic` are 0 to 1. */
    typedef struct engine_terrain_layer
    {
        float tiling;
        float tint[3];
        float roughness;
        float metallic;
    } engine_terrain_layer;

    /* A terrain (Phase 28): the height map `height_map` (a resource of type ENGINE_RESOURCE_HEIGHT_MAP the host added,
     * or made with height_map_create) drawn at the entity's world transform, which must be a translation and a turn
     * about the y axis (a scaled or tilted terrain is not drawn). The map's first sample is at the entity's origin and
     * the map extends along +x and +z. `layer_texture` is a texture resource (an array of 2 to 4 RGBA8 sRGB layers, at
     * least `layer_count`) or 0 for flat grey; `splat_texture` is the weights of the layers (an RGBA8 texture across
     * the terrain) or 0 for layer 0 alone. `slope_layer` (-1 for none, else a layer below `layer_count`) replaces the
     * weights where the surface leans between `slope_start` and `slope_end` (0 to 1, start below end); the detail fades
     * into the layer's average between `fade_start` and `fade_end` metres from the camera (start below end). A
     * resource that is not mounted or of another type is NOT_FOUND or TYPE_MISMATCH when set; one that is replaced or
     * not drawable (a terrain with no usable height map) draws nothing and counts in
     * engine_surface_state.entities_unresolved. With ENGINE_TERRAIN_COLLIDER the engine makes the terrain a static body
     * with a height-field collider of the same samples at the end of the next frame (the entity must have no other
     * collider, and its world transform no scale); `friction` and `restitution` are the collider's. */
    typedef struct engine_terrain
    {
        uint32_t struct_size;
        uint32_t flags; /* ENGINE_TERRAIN_* */
        uint64_t height_map;
        uint64_t layer_texture;
        uint64_t splat_texture;
        uint32_t layer_count; /* 1 to ENGINE_TERRAIN_LAYERS_MAX */
        int32_t slope_layer;
        float slope_start;
        float slope_end;
        float fade_start;
        float fade_end;
        engine_terrain_layer layers[ENGINE_TERRAIN_LAYERS_MAX];
        float friction;
        float restitution;
    } engine_terrain;

    /* What animation_play started on an entity, as ENGINE_COMPONENT_ANIMATION_PLAYER returns it (read only: use
     * animation_play, animation_set_speed and animation_stop to change it; removing the component stops the animation).
     */
    typedef struct engine_animation_state
    {
        uint32_t struct_size;
        uint32_t playing; /* 1 while time advances */
        uint32_t looping;
        uint32_t finished; /* 1 once a clip that does not loop reached its end */
        uint32_t fading;   /* 1 while a cross-fade from the previous clip is running */
        uint32_t reserved; /* zero */
        uint64_t clip;     /* the resource id of the clip played now */
        float time;        /* seconds into the clip */
        float speed;
    } engine_animation_state;

    /* Bits of engine_play_params.flags. */
#define ENGINE_PLAY_LOOP                                                                                               \
    0x1u /* wrap at the end of the clip (otherwise the last pose is held and `finished` is set)                        \
          */

    /* How animation_play starts a clip. `speed` scales the clip's time against the frame's (0 means 1; use
     * animation_set_speed to pause); `start_time` is where in the clip to start, in seconds; `fade_seconds` > 0 blends
     * from what was playing to the new clip over that time (the old clip keeps playing while it fades out). */
    typedef struct engine_play_params
    {
        uint32_t struct_size;
        uint32_t flags; /* ENGINE_PLAY_* bits; an unknown bit is INVALID_ARGUMENT */
        float speed;
        float start_time;
        float fade_seconds;
    } engine_play_params;

    /* Flags of engine_texture_info. */
#define ENGINE_TEXTURE_SRGB 0x1u /* the pixels are sRGB encoded (a colour picture); without it they are linear data */

    /* A texture the host fills with its own pixels (Phase 24 Day 365): RGBA8, `width` x `height`, at most
     * `assets.host_texture.max_size` (default 8192) each way. `name` (optional) is shown in diagnostics. */
    typedef struct engine_texture_info
    {
        uint32_t struct_size;
        uint32_t flags; /* ENGINE_TEXTURE_* */
        uint32_t width;
        uint32_t height;
        engine_string name;
    } engine_texture_info;

    /* How texture_update takes its pixels. `width` and `height` of 0 mean the texture's own size; another size resizes
     * the texture together with the update (a video source that changes resolution). */
    typedef struct engine_update_info
    {
        uint32_t struct_size;
        uint32_t flags; /* zero */
        uint32_t width;
        uint32_t height;
    } engine_update_info;

    /* How level_spawn places a level (Phase 24 Day 364). `position`, `rotation` (x, y, z, w) and `scale` (one factor
     * for the three axes; 0 means 1) are the transform of the root entity the engine makes for the spawn; the level's
     * own entities are its children. An all-zero rotation means no rotation. `name_prefix` (up to 15 bytes) is put
     * before the name of every entity of the level followed by '/', and names the root entity (empty: the root is
     * called "level" and the names are the level's own); a name longer than ENGINE_NAME_MAX_BYTES is cut. */
    typedef struct engine_spawn_params
    {
        uint32_t struct_size;
        uint32_t flags; /* zero */
        float position[3];
        float rotation[4];
        float scale;
        uint32_t reserved; /* zero */
        engine_string name_prefix;
    } engine_spawn_params;

    /* The kinds of engine_slot_override.kind. */
#define ENGINE_SLOT_FLOAT 1u   /* value[0] */
#define ENGINE_SLOT_COLOR 2u   /* value[0..3]: linear RGBA */
#define ENGINE_SLOT_TEXTURE 3u /* texture_resource: the resource id of a texture the host added or loaded */
#define ENGINE_SLOT_HOST_TEXTURE                                                                                       \
    4u /* host_texture: a host texture (texture_create); the slot shows its newest pixels (Phase 24 Day 366) */
#define ENGINE_SLOT_VEC4 5u /* value[0..3]: four plain floats, not a colour (the "albedo_uv" slot) */

    /* A per-entity override of one slot of the material its mesh reference names (Phase 24 Day 362): the entity draws
     * with its own copy of the material in which that slot has this value, and every other entity that uses the
     * material is untouched. The slots are those of the engine's G-buffer material: ENGINE_SLOT_COLOR for "base_color"
     * and "emissive_color" (linear rgb, the strength in alpha; the surface emits rgb * alpha * the emissive map, added
     * after the lights; it replaced "emissive_scale", which is refused now); ENGINE_SLOT_FLOAT for "roughness",
     * "metallic", "alpha_cutoff", "occlusion" and "occlusion_strength"; ENGINE_SLOT_TEXTURE (a resource) or
     * ENGINE_SLOT_HOST_TEXTURE (a host texture, which only the surfaces that draw the entity upload, once per device;
     * destroying it or having no pixels yet leaves the slot at the material's own value, counted in
     * engine_frame_stats.slots_reverted) for "albedo_tex", "normal_tex", "metallic_roughness_tex", "occlusion_tex" and
     * "emissive_tex" (glTF's maps: the first two linear, roughness times green, metallic times blue, occlusion times
     * 1 + occlusion_strength * (red - 1); the emissive map sRGB; unset they are white).
     *
     * A texture-coordinate transform for the albedo (maintenance, 2026-11-18), to fit a picture of another aspect into
     * a quad without copying its pixels: ENGINE_SLOT_VEC4 for "albedo_uv" = (scale u, scale v, offset u, offset v), the
     * albedo is sampled at uv * scale + offset; the default is (1, 1, 0, 0), which draws as before. ENGINE_SLOT_COLOR
     * for "albedo_uv_outside" = linear RGBA: when its alpha is above 0, the part of the quad whose transformed
     * coordinates leave 0..1 shows its rgb instead of the texture (it replaces the texel, so base_color still
     * multiplies it), and the coordinates inside are kept half a texel from the picture's edge so that filtering never
     * reads across it (the same on every graphics API); with alpha 0 the texture's address mode applies. The normal
     * map keeps the mesh's own coordinates. To fit a frame of aspect a on a screen of aspect s: for a > s scale v by
     * a / s and offset v by (1 - a / s) / 2; for a < s scale u by s / a and offset u by (1 - s / a) / 2.
     *
     * A display that glows (maintenance, 2026-11-22): ENGINE_SLOT_FLOAT for "emissive_from_albedo" (default 0) makes
     * the albedo emit: the surface adds albedo * this value to its emitted light, after the lights, where the albedo is
     * the picture as fitted by "albedo_uv" and "albedo_uv_outside" times base_color (and the mesh's vertex colours, as
     * the albedo is drawn); the bars glow with their own colour (black stays black). */
    typedef struct engine_slot_override
    {
        uint32_t struct_size;
        uint32_t kind; /* ENGINE_SLOT_* */
        float value[4];
        uint64_t texture_resource;
        engine_handle host_texture; /* for ENGINE_SLOT_HOST_TEXTURE */
    } engine_slot_override;

    /* The bounds and options of engine_label. */
#define ENGINE_LABEL_MAX_BYTES 256u
#define ENGINE_LABEL_ANCHOR_CENTER 0u /* the text is centred on the label's point */
#define ENGINE_LABEL_ANCHOR_LEFT 1u   /* the text starts at the point */
#define ENGINE_LABEL_ANCHOR_RIGHT 2u  /* the text ends at the point */
#define ENGINE_LABEL_SIZE_METRES 0x1u /* `size` is the height of the text's em square in metres, not pixels */

    /* A label (Phase 24 Day 367): UTF-8 text drawn over the surface at the entity's world position plus `offset`
     * (world axes, metres), facing the viewer, in the font given with label_font_set. `size` is a pixel size (kept as
     * the camera moves) or, with ENGINE_LABEL_SIZE_METRES, a height in metres (the pixel size then follows the
     * distance, rounded to a set of sizes, and 6..256 pixels). `color` is RGBA 0..1 as sRGB. Labels have no depth
     * test (never hidden by geometry), are hidden with their entity, follow it (and the joint it is attached to), and
     * a surface draws up to 1024 of them (the rest are counted in engine_surface_state.labels_refused). component_get
     * returns every field but the text's bytes (text.data is NULL, text.length is its length). The entity needs
     * ENGINE_COMPONENT_TRANSFORM too. */
    typedef struct engine_label
    {
        uint32_t struct_size;
        engine_string text; /* at most ENGINE_LABEL_MAX_BYTES bytes, copied */
        float color[4];
        float size;
        float offset[3];
        uint32_t anchor; /* ENGINE_LABEL_ANCHOR_* */
        uint32_t flags;  /* ENGINE_LABEL_* options */
    } engine_label;

    /* The kinds of engine_light.kind. */
#define ENGINE_LIGHT_DIRECTIONAL                                                                                       \
    1u                        /* shines along the entity's -Z, from infinitely far away (the position is ignored) */
#define ENGINE_LIGHT_POINT 2u /* shines from the entity's position and reaches nothing beyond `radius` */
#define ENGINE_LIGHT_SPOT                                                                                              \
    3u /* a point light that shines along the entity's -Z only, inside `outer_cone` (maintenance, 2026-11-20) */

    /* A light (Phase 24 Day 361). A directional light shines along its entity's world -Z axis (the way a camera looks);
     * a point light sits at the entity's position; a spot light sits there and shines along -Z. The entity needs
     * ENGINE_COMPONENT_TRANSFORM too. `color` is linear RGB, `intensity` >= 0 scales it, `radius` (> 0, in metres) is
     * where a point or spot light's contribution reaches exactly zero (ignored for a directional light). A spot light
     * is at full strength inside `inner_cone` and gives nothing beyond `outer_cone` (half-angles from its axis, in
     * radians: 0 <= inner_cone < outer_cone <= pi/2); the cones are ignored for the other kinds. The cone fields came
     * later (maintenance, 2026-11-20): a struct that ends before `inner_cone` sets and reads directional and point
     * lights as before, a spot light needs them (STRUCT_SIZE), and component_get fills only what the struct holds. A
     * light that level_spawn made keeps the level's own axis (a glTF light's is -Z, the same); setting the component
     * again gives it the entity's -Z. A surface lights its world with up to 64 of them; the rest are counted in
     * engine_surface_state.lights_refused. */
    typedef struct engine_light
    {
        uint32_t struct_size;
        uint32_t kind; /* ENGINE_LIGHT_* */
        float color[3];
        float intensity;
        float radius;
        float inner_cone; /* spot: radians, full strength inside */
        float outer_cone; /* spot: radians, nothing beyond */
    } engine_light;

    /* The kinds of engine_rigid_body.type. */
#define ENGINE_BODY_STATIC 0u    /* never moves */
#define ENGINE_BODY_KINEMATIC 1u /* moves as its velocity says, pushes dynamic bodies, is not pushed */
#define ENGINE_BODY_DYNAMIC 2u   /* moved by forces and contacts */

    /* A physics body (Phase 25 Day 408). An entity with ENGINE_COMPONENT_TRANSFORM, this and an
     * ENGINE_COMPONENT_COLLIDER is simulated by the frame: the body starts at the entity's transform and the frame
     * writes its pose back to the transform (not for an entity with a parent). `mass` 0 takes the mass from the
     * collider's volume and density, otherwise it is in kilograms (1e-9 to 1e15). The dampings are >= 0 (per second),
     * `gravity_scale` multiplies gravity (a magnitude of at most 1e6). `flags` is 0 (reserved). */
    typedef struct engine_rigid_body
    {
        uint32_t struct_size;
        uint32_t type; /* ENGINE_BODY_* */
        float mass;
        float linear_damping;
        float angular_damping;
        float gravity_scale;
        uint32_t flags;
    } engine_rigid_body;

    /* The kinds of engine_collider.shape. */
#define ENGINE_SHAPE_SPHERE 0u  /* `radius` */
#define ENGINE_SHAPE_BOX 1u     /* `half_extents` */
#define ENGINE_SHAPE_CAPSULE 2u /* `radius` and `half_height` (of the straight part, along Y) */
#define ENGINE_SHAPE_MESH                                                                                              \
    4u /* `mesh_resource`: an MCOL triangle mesh, static bodies only (glTF colliders, 2026-11-20; 3 is kept for a      \
          hull) */
#define ENGINE_SHAPE_HEIGHT_FIELD                                                                                      \
    5u /* `mesh_resource`: an ENGINE_RESOURCE_HEIGHT_MAP, static bodies only; the field's first sample is at the       \
          entity's origin (Phase 28) */

    /* The shape of a physics body (Phase 25 Day 408), in the entity's space: sizes are in metres (above 0, at most
     * 1e5), `offset` and `rotation` (a unit quaternion x, y, z, w) place it on the body. `friction` >= 0, `restitution`
     * in 0..1, `density` in kilograms per cubic metre (1e-3 to 1e9). `layer` is 0..31, `group` and `mask` select which
     * colliders it meets (a pair collides when each one's group is in the other's mask). A trigger (`is_trigger` 1)
     * reports overlaps and does not collide. A mesh (ENGINE_SHAPE_MESH, glTF colliders, 2026-11-20) names an MCOL
     * resource the host added or an import made (`mesh_resource`; NOT_FOUND when it is not mounted, TYPE_MISMATCH when
     * it is not an MCOL): it is for static bodies only (a mesh collider on an entity whose body is not static, or a
     * body type other than static on an entity with a mesh collider, is INVALID_ARGUMENT), it is sized by the entity's
     * world scale when it is made at the end of the next frame (the collider's `offset` scales with it), and
     * component_has and queries see it from then on. The two last fields came later: a struct that ends before
     * `reserved` sets and reads the other shapes as before, and a mesh needs them (STRUCT_SIZE). Convex hulls and
     * compounds are refused (UNSUPPORTED). */
    typedef struct engine_collider
    {
        uint32_t struct_size;
        uint32_t shape; /* ENGINE_SHAPE_* */
        float radius;
        float half_height;
        float half_extents[3];
        float offset[3];
        float rotation[4];
        float friction;
        float restitution;
        float density;
        uint32_t layer;
        uint32_t group;
        uint32_t mask;
        uint32_t is_trigger;
        uint32_t reserved;      /* zero */
        uint64_t mesh_resource; /* ENGINE_SHAPE_MESH: the MCOL's resource id */
    } engine_collider;

    /* What a ray cast found. */
    typedef struct engine_ray_hit
    {
        uint32_t struct_size;
        uint64_t entity; /* the key of the entity whose collider was hit */
        float point[3];
        float normal[3]; /* facing against the ray */
        float distance;  /* along the ray */
    } engine_ray_hit;

    /* The kinds of engine_joint.kind. */
#define ENGINE_JOINT_BALL 0u /* the anchors stay together; the bodies turn freely */
#define ENGINE_JOINT_DISTANCE                                                                                          \
    1u                        /* the anchors stay `min_length`..`max_length` apart (rigid when both are -1: the        \
                                 distance they have now) */
#define ENGINE_JOINT_FIXED 2u /* the anchors stay together and the bodies keep their relative orientation */
#define ENGINE_JOINT_HINGE 3u /* the bodies turn about `axis` through the anchor */
/* engine_joint.flags */
#define ENGINE_JOINT_COLLIDE_CONNECTED 0x1u /* the two bodies still collide with each other */
#define ENGINE_JOINT_LIMIT 0x2u             /* a hinge is limited to [lower_angle, upper_angle] */
#define ENGINE_JOINT_MOTOR 0x4u             /* a hinge has a motor */

    /* A joint between two entities' bodies (Phase 25 Day 409); the entity that has this component is the joint.
     * `entity_a` and `entity_b` are keys of entities with a rigid body and a collider (0 on one side: the joint ties
     * the other to the world). `point_a` and `point_b` are the anchors in world space at the time the joint is made
     * (one point for a ball, hinge or fixed joint: `point_b` is ignored), `axis` the hinge axis in world space. `hertz`
     * above 0 makes a distance joint a spring (`damping_ratio` as usual); `break_force` and `break_torque` above 0
     * break it. The joint is made by the frame when both bodies exist, and removed (the component too) when it breaks.
     */
    typedef struct engine_joint
    {
        uint32_t struct_size;
        uint32_t kind; /* ENGINE_JOINT_* */
        uint64_t entity_a;
        uint64_t entity_b;
        float point_a[3];
        float point_b[3];
        float axis[3];
        float min_length;
        float max_length;
        float hertz;
        float damping_ratio;
        float break_force;
        float break_torque;
        uint32_t flags; /* ENGINE_JOINT_* flags */
        float lower_angle;
        float upper_angle;
        float motor_speed;
        float max_motor_torque;
    } engine_joint;

    /* A character controller (Phase 25 Day 409): an upright capsule that is moved with character_move, slides along
     * walls, climbs steps and slopes and stays on the ground. The entity needs ENGINE_COMPONENT_TRANSFORM; setting the
     * component (again) puts the capsule at the entity's transform, and the frame writes the capsule's position back to
     * it (the world matrix follows one frame later). Sizes are in metres.
     *
     * A world has one set of characters, shared with its scripts (2026-11-20): a character made by this component is
     * the one a behavior's character.* calls see, and one made by character.create is the one character_move and
     * character_jump drive (an entity has at most one). The characters move inside each fixed physics step, from
     * the held move (character_move); after the frame's steps the capsule's centre is written to the entity's
     * transform, interpolated between the last two steps when physics.interpolate is on (the default), so a camera
     * that follows the entity moves smoothly when frames and steps do not line up. Setting the entity's transform
     * does not move the character (the write-back replaces it): set this component again, or call
     * character.set_position from a script. */
    typedef struct engine_character
    {
        uint32_t struct_size;
        float radius;
        float height; /* of the whole capsule, at least 2 * radius */
        float step_height;
        float min_ground_y; /* the least Y of a normal that counts as ground (0.64: a slope of about 50 degrees) */
        float skin_width;
        float gravity_scale;
    } engine_character;

    /* What the character's last fixed step found and where it stands. */
    typedef struct engine_character_state
    {
        uint32_t struct_size;
        uint32_t on_ground;
        uint32_t began_ground; /* in the last fixed step */
        uint32_t ended_ground;
        uint32_t wall_hits;
        uint64_t ground_entity; /* the entity of the body underfoot (0: none or the world's) */
        float ground_normal[3];
        float wall_normal[3];
        float vertical_speed;
    } engine_character_state;

    /* The kinds of engine_physics_event.kind. */
#define ENGINE_PHYSICS_CONTACT_BEGIN 1u
#define ENGINE_PHYSICS_CONTACT_STAY 2u /* only with physics.stay_events */
#define ENGINE_PHYSICS_CONTACT_END 3u
#define ENGINE_PHYSICS_TRIGGER_ENTER 4u
#define ENGINE_PHYSICS_TRIGGER_EXIT 5u
#define ENGINE_PHYSICS_JOINT_BREAK 6u

    /* One event of the last physics steps. `entity_a` and `entity_b` are the entities (for a trigger: the trigger and
     * what entered; for a joint break: the joint's two entities, 0 for the world). A contact has the deepest point, the
     * normal and `value` the depth; a joint break has the force in `point`, the torque in `normal` and `value` 0. */
    typedef struct engine_physics_event
    {
        uint32_t struct_size;
        uint32_t kind; /* ENGINE_PHYSICS_* */
        uint64_t entity_a;
        uint64_t entity_b;
        float point[3];
        float normal[3];
        float value;
    } engine_physics_event;

    /* What surrounds a world (Phase 24 Day 361): one per world, shared by every surface that shows it. Unset, a world
     * has an ambient colour of white at intensity 0.2, a black sky, exposure 1, and a default sun when it has no light
     * entity (the world of Day 359); setting the environment (even with these values) removes the default sun. */
    typedef struct engine_environment
    {
        uint32_t struct_size;
        float ambient_color[3];  /* linear RGB */
        float ambient_intensity; /* >= 0 */
        float sky_color[3];      /* linear RGB behind the scene, before the tone map */
        float exposure;          /* > 0: the tone map's scale */
        /* >= 0: how much of sky_color the lit surfaces reflect, times a Fresnel term (about 4% head on for a smooth
         * dielectric, nearly all of it at a glancing angle; a constant colour, no cube map). 0 reflects nothing. The
         * first version of the struct ended before this field: a struct_size that does not cover it means 0. The
         * reflection is added on top of the diffuse and the lights, with no energy conservation. */
        float reflection_intensity;
        /* The sky behind the scene (Phase 28): ENGINE_SKY_COLOR draws the flat sky_color (the default and what a
         * struct_size that does not cover these fields means), ENGINE_SKY_NONE draws black, ENGINE_SKY_PROCEDURAL a
         * gradient from sky_ground (straight down) through sky_horizon to sky_zenith (straight up) with a sun disc of
         * sun_size radians (> 0, at most pi / 2) toward sun_direction (not zero) of radiance sun_color, and a band of
         * sky_horizon_blend (> 0, at most 1) below the horizon. All colours linear, >= 0; sky_exposure > 0 scales the
         * whole sky. A procedural sky also lights the world: surfaces reflect it (reflection_intensity) and take
         * sky_ambient_intensity (>= 0) times its blurred colour as ambient light, replacing the ambient colour above
         * when that is above 0. The fields of a mode that is not used are not checked. */
        uint32_t sky_mode;
        float sky_zenith[3];
        float sky_horizon[3];
        float sky_ground[3];
        float sun_direction[3];
        float sun_color[3];
        float sun_size;
        float sky_horizon_blend;
        float sky_exposure;
        float sky_ambient_intensity;
    } engine_environment;
#define ENGINE_SKY_COLOR 0u
#define ENGINE_SKY_NONE 1u
#define ENGINE_SKY_PROCEDURAL 2u

    /* A job: called once, on one of the engine's worker threads (never on the thread that submitted it), with the
     * `user` pointer given to jobs_submit. While it runs it may call, through the table it was given: status_name,
     * last_error, thread_detach (not needed), the world, entity, component and query functions (they are serialized
     * with each other), counter_value, counter_create/destroy and jobs_submit. It must not call frame, create, destroy,
     * counter_wait, crash_handler_install or crash_handler_uninstall: wait for other work from outside the job. It
     * never throws or longjmps. */
    typedef void (*engine_job_fn)(void* user);

/* ---- Day 350: resources and host-provided content
 * ---------------------------------------------------------------------------------------------- */

/* A resource type is its four characters as a uint32_t, the first in the lowest byte:
 * ENGINE_RESOURCE_TYPE('M','E','S','H'). Types with a loader today: skeleton, animation, mesh, texture, material and
 * level, loaded as their CPU data (the engine's own objects; nothing of them is exposed). */
#define ENGINE_RESOURCE_TYPE(a, b, c, d)                                                                               \
    ((uint32_t)(unsigned char)(a) | ((uint32_t)(unsigned char)(b) << 8) | ((uint32_t)(unsigned char)(c) << 16) |       \
     ((uint32_t)(unsigned char)(d) << 24))
#define ENGINE_RESOURCE_MESH ENGINE_RESOURCE_TYPE('M', 'E', 'S', 'H')
#define ENGINE_RESOURCE_TEXTURE ENGINE_RESOURCE_TYPE('T', 'E', 'X', 'R')
#define ENGINE_RESOURCE_MATERIAL ENGINE_RESOURCE_TYPE('M', 'A', 'T', 'L')
#define ENGINE_RESOURCE_LEVEL ENGINE_RESOURCE_TYPE('L', 'E', 'V', 'L')
#define ENGINE_RESOURCE_ANIMATION ENGINE_RESOURCE_TYPE('A', 'N', 'I', 'M')
#define ENGINE_RESOURCE_SKELETON ENGINE_RESOURCE_TYPE('S', 'K', 'E', 'L')
#define ENGINE_RESOURCE_HEIGHT_MAP ENGINE_RESOURCE_TYPE('H', 'G', 'H', 'T') /* 16-bit terrain heights (Phase 28) */
#define ENGINE_RESOURCE_MESH_COLLIDER                                                                                  \
    ENGINE_RESOURCE_TYPE('M', 'C', 'O', 'L') /* a static triangle-mesh collider (glTF colliders, 2026-11-20) */

/* Flags of engine_resource_add_info and of resource_remove. */
#define ENGINE_RESOURCE_BORROW 0x1u /* add, replace: lend the bytes instead of copying them; `release` is required */
#define ENGINE_RESOURCE_CHECK_HASH                                                                                     \
    0x2u /* add, replace: the blob's content hash must equal `expected_hash` (CORRUPT otherwise) */
#define ENGINE_RESOURCE_FORCE                                                                                          \
    0x4u /* replace, remove: unload the loaded resources that depend on this one, atomically, instead of refusing      \
            (IN_USE) */

    /* Called when the engine is done with lent bytes: exactly once per successful add or replace that lent them, after
     * the last reader of those bytes is gone (the resource was replaced or removed and no load is reading it, or the
     * engine was destroyed), on one of the engine's worker threads (never on the thread that called the engine, and
     * never while the engine holds a lock the callback could need). From then on the bytes are the host's again. It may
     * call status_name, last_error, thread_detach, the resource_* and world functions and jobs_submit; it must not call
     * frame, create, destroy, counter_wait or ticket_wait. It never throws or longjmps, and engine workers wait for it:
     * it should return soon. A failed add or replace never calls it: the host keeps the bytes. engine_destroy runs
     * every pending release before it returns. */
    typedef void (*engine_resource_release_fn)(void* user, const void* bytes, size_t size);

    /* One resource's bytes: a complete blob as `asset_mint` makes it (container, type, id, content hash; the engine
     * checks all of them). */
    typedef struct engine_resource_add_info
    {
        uint32_t struct_size;
        uint32_t flags; /* ENGINE_RESOURCE_BORROW, ENGINE_RESOURCE_CHECK_HASH, ENGINE_RESOURCE_FORCE (replace only) */
        uint64_t id;    /* the resource id; it must be the id inside the blob; not zero */
        engine_string path; /* a path that names the resource for resource_load; empty: reachable by id only. Replace:
                               empty keeps the paths */
        const void* bytes;  /* the blob */
        size_t size;        /* its length */
        engine_resource_release_fn release; /* with ENGINE_RESOURCE_BORROW; NULL otherwise */
        void* release_user;                 /* passed to `release` */
        uint8_t expected_hash[16];          /* with ENGINE_RESOURCE_CHECK_HASH */
    } engine_resource_add_info;

    /* What resource_mounted and resource_status report. */
    typedef struct engine_resource_info
    {
        uint32_t struct_size;
        uint32_t type; /* the resource type tag */
        uint64_t id;
        uint64_t size; /* bytes of the blob in the memory backend (0 when the resource is not in it) */
        uint8_t content_hash[16];
        int32_t loaded; /* 1 while the engine has it loaded (for resource_status: while the handle is valid) */
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
        int32_t done;         /* 1 once the load ended (with a resource, an error or a cancellation) */
        uint32_t blobs_read;  /* blobs read of ... */
        uint32_t blobs_total; /* ... the blobs known so far (the total grows as the closure is found) */
    } engine_ticket_info;

/* ---- glTF import (Phase 23 Day 351)
 * ----------------------------------------------------------------------------------------------------------- */

/* Flags of engine_import_options. */
#define ENGINE_IMPORT_FLAT_NORMALS 0x1u /* a mesh without normals gets flat ones (default: smooth) */
#define ENGINE_IMPORT_DROP_MORPH_TARGETS                                                                               \
    0x2u /* import the base shape of a primitive that has morph targets (a warning) instead of refusing */
#define ENGINE_IMPORT_EMIT_LEVEL                                                                                       \
    0x4u /* also make a level of the default scene's node tree (resource type ENGINE_RESOURCE_LEVEL, the last of       \
            `ids`), which level_spawn puts into a world (Phase 24 Day 364) */

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
        /* The triangles of every collider of the import together (glTF colliders, 2026-11-20; 0 takes the setting
         * `assets.import.max_collision_triangles`, default 4,194,304): an import that would make more fails with
         * TOO_LARGE. A struct that ends before it is served (the setting applies). */
        uint64_t max_collision_triangles;
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
        /* UTF-8 JSON, the facts about the file that a host needs after adding the blobs back from its own cache and
         * that no engine call reads from a loaded level (maintenance, 2026-11-18); empty when the file made no level.
         * An object with these members:
         * - "version": 1.
         * - "nodes": the level's entities in order, each {"name":"Spawn","position":[x,y,z],"mesh":-1}, with the
         *   names level_spawn gives them (entity_find).
         * - "bounds": {"min":[x,y,z],"max":[x,y,z]} of the meshes' POSITION ranges in the world, or null.
         * - "materials": the material names in the file's order, "" if unnamed.
         * - "animations": each {"name":"Walk","duration":1.2}, in seconds (the longest sampler input).
         * - "lights": one per level node with a KHR_lights_punctual light, in level order, each
         *   {"name":"Lamp","type":"point","node":"Lamp","position":[x,y,z]}: its name ("" if unnamed), its glTF type
         *   (point, spot or directional), the entity it is on and its world position.
         * - "collision": {"colliders":N,"meshes":M,"triangles":T,"primitives":P,"dropped":D}: the nodes that got a
         * static collider (`-col`/`-colonly` names or OMI_physics_*), the MCOL meshes made, their triangles together,
         * the colliders that are a box, sphere or capsule, and the marked nodes that got none (glTF colliders,
         * 2026-11-20). Positions and bounds are in the units of the level (the import's `scale` applied). The host
         * stores this text beside the blobs it caches. Present when struct_size covers it. */
        engine_buffer summary;
    } engine_import_result;

    /* What import_poll reports. */
    typedef struct engine_import_info
    {
        uint32_t struct_size;
        int32_t done;         /* 1 once the import ended (with a result, an error or a cancellation) */
        uint32_t steps_done;  /* of ... */
        uint32_t steps_total; /* ... the import's steps */
    } engine_import_info;

    /* ---- Phase 26 Day 438: audio -------------------------------------------------------------------------------- */

    /* The audio of an engine is host driven: the engine opens no device and starts no audio thread. The host calls
     * audio_render from its own audio callback (or from any thread for an offline render). */

    /* How audio_open sets the mixer up. */
    typedef struct engine_audio_info
    {
        uint32_t struct_size;
        uint32_t sample_rate; /* 8000 to 384000 */
        uint32_t channels;    /* 1 (mono), 2 (stereo), 4 (quad), 6 (5.1) or 8 (7.1) */
        uint32_t max_voices;  /* 1 to 4096; 0: 256 */
        uint32_t max_buses;   /* 1 to 32 (the master is one of them); 0: 16 */
        uint32_t limiter;     /* 1: the master limiter starts on */
    } engine_audio_info;

    /* How audio_clip_create reads the bytes it is given. */
#define ENGINE_AUDIO_FORMAT_F32 0u /* 32-bit float, little-endian, interleaved */
#define ENGINE_AUDIO_FORMAT_S16 1u /* signed 16-bit, little-endian, interleaved */
    typedef struct engine_audio_clip_info
    {
        uint32_t struct_size;
        uint32_t sample_rate; /* 8000 to 384000 */
        uint32_t channels;    /* 1, 2, 4, 6 or 8 */
        uint32_t format;      /* ENGINE_AUDIO_FORMAT_* */
        uint32_t loop_start;  /* in frames */
        uint32_t loop_end;    /* in frames; 0: the clip has no loop points, else loop_start < loop_end <= frames */
    } engine_audio_clip_info;

    /* One voice to start. The values are used as given (a gain of 0 is silence): 1 for gain and pitch, 0 for pan,
     * bus 0 (the master) are the neutral ones. */
    typedef struct engine_audio_play
    {
        uint32_t struct_size;
        uint64_t clip;    /* from audio_clip_create */
        uint32_t bus;     /* from audio_bus_create; 0: the master */
        float gain;       /* 0 to 16 */
        float pitch;      /* 0.01 to 16 */
        float pan;        /* -1 (left) to 1 (right) */
        uint32_t loop;    /* 1: loop (at the clip's loop points, else the whole clip) */
        int32_t priority; /* higher wins when the pool is full */
    } engine_audio_play;

    /* What audio_voice_set changes: the fields whose bit is in `mask`. */
#define ENGINE_AUDIO_VOICE_GAIN 1u
#define ENGINE_AUDIO_VOICE_PITCH 2u
#define ENGINE_AUDIO_VOICE_PAN 4u
#define ENGINE_AUDIO_VOICE_LOOP 8u
    typedef struct engine_audio_voice_params
    {
        uint32_t struct_size;
        uint32_t mask; /* ENGINE_AUDIO_VOICE_* */
        float gain;
        float pitch;
        float pan;
        uint32_t loop;
        uint32_t fade_frames; /* the gain moves over this many frames (0: the engine's default ramp) */
    } engine_audio_voice_params;

    /* What audio_bus_set changes: the fields whose bit is in `mask`. */
#define ENGINE_AUDIO_BUS_GAIN 1u
#define ENGINE_AUDIO_BUS_MUTE 2u
#define ENGINE_AUDIO_BUS_LIMITER 4u /* the master limiter on or off (the master bus only) */
    typedef struct engine_audio_bus_params
    {
        uint32_t struct_size;
        uint32_t mask; /* ENGINE_AUDIO_BUS_* */
        float gain;    /* 0 to 16 */
        uint32_t mute;
        uint32_t limiter;
    } engine_audio_bus_params;

    /* A sound on an entity (ENGINE_COMPONENT_AUDIO_SOURCE). The entity needs ENGINE_COMPONENT_TRANSFORM; its world
     * matrix places the sound (the cone's axis is the entity's local -Z). The sound plays through the engine's
     * host-driven mixer: audio_open first, then the frame (engine_api.frame) starts, moves and stops the voices, so the
     * host's audio_render hears them. A source set while audio is not open plays once it is. Distances are in the
     * world's units. */
#define ENGINE_AUDIO_SOURCE_LOOP 1u
#define ENGINE_AUDIO_SOURCE_PLAY_ON_START 2u /* starts when first seen (set it for the usual case) */
#define ENGINE_AUDIO_SOURCE_SPATIAL 4u       /* placed in the world; clear for music and interface sounds */
#define ENGINE_AUDIO_SOURCE_DOPPLER 8u
#define ENGINE_AUDIO_SOURCE_OCCLUSION 16u
#define ENGINE_AUDIO_MODEL_INVERSE 0u
#define ENGINE_AUDIO_MODEL_LINEAR 1u
#define ENGINE_AUDIO_MODEL_EXPONENTIAL 2u
    typedef struct engine_audio_source
    {
        uint32_t struct_size;
        uint64_t clip;      /* from audio_clip_create; 0 plays nothing */
        uint32_t bus;       /* from audio_bus_create; 0: the master */
        float gain;         /* 0 to 16, before distance and cone */
        float pitch;        /* the rate ratio, 0.25 to 4 */
        uint32_t flags;     /* ENGINE_AUDIO_SOURCE_* */
        int32_t priority;   /* higher survives when voices are scarce */
        uint32_t model;     /* ENGINE_AUDIO_MODEL_* */
        float min_distance; /* full level up to here */
        float max_distance; /* nothing is heard beyond; above min_distance */
        float rolloff;
        float cone_inner; /* full angle in radians; 6.2831853 or more: all directions */
        float cone_outer; /* at least cone_inner */
        float cone_outer_gain;
        /* A stream (audio_stream_open_push, audio_stream_open) the voice plays instead of `clip`: 0 for a clip. Both
         * set is INVALID_ARGUMENT, and a stream that another source of the world already has is INVALID_ARGUMENT (a
         * live stream has one reader). Everything else of the source applies as for a clip. The voice follows the
         * entity and starts even while the source is out of earshot (so that the stream goes on being read); removing
         * the source, destroying the entity or setting another stream stops it, and the stream stays open until
         * audio_stream_close, so it can be set again (a participant who leaves and comes back). Present when
         * struct_size covers it (maintenance, 2026-11-18); an older host's struct has no stream. */
        uint64_t stream;
    } engine_audio_source;

    /* The ears (ENGINE_COMPONENT_AUDIO_LISTENER): the active listener with the lowest entity id places every spatial
     * source from its world matrix (forward is local -Z); its velocity is the change of its position between frames. */
    typedef struct engine_audio_listener
    {
        uint32_t struct_size;
        float gain;      /* 0 to 4 */
        uint32_t active; /* nonzero: a candidate */
    } engine_audio_listener;

    /* The kinds of engine_audio_event.kind. */
#define ENGINE_AUDIO_EVENT_FINISHED 1u    /* the voice ran off the end of its clip */
#define ENGINE_AUDIO_EVENT_LOOPED 2u      /* the voice wrapped its loop (at most one per voice per block) */
#define ENGINE_AUDIO_EVENT_VIRTUALISED 3u /* the voice was made inaudible and no longer mixed */
#define ENGINE_AUDIO_EVENT_STOLEN 4u      /* a better voice took the slot */
#define ENGINE_AUDIO_EVENT_STOPPED 5u     /* a stop finished and the voice was freed */
    typedef struct engine_audio_event
    {
        uint32_t struct_size;
        uint32_t kind; /* ENGINE_AUDIO_EVENT_* */
        uint64_t voice;
        uint64_t block; /* the mixer block the event happened in */
    } engine_audio_event;

    /* What audio_state reports. */
    typedef struct engine_audio_state
    {
        uint32_t struct_size;
        uint32_t sample_rate;
        uint32_t channels;
        uint32_t live_voices;
        uint64_t blocks_rendered;
        uint64_t voices_stolen;
        uint64_t voices_finished;
        uint64_t commands_dropped;
    } engine_audio_state;

    /* ---- Phase 26 Day 439: streams, music, effects and snapshots ---------------------------------------------- */

    /* A stream whose frames come from the host: the engine's refill job calls `read` and `seek` (never the audio
     * thread, and never two calls at once), so a host with its own decoder or file reader feeds music. Both functions
     * must not block for long and must not call the engine. */
    typedef struct engine_audio_stream_source
    {
        uint32_t struct_size;
        uint32_t sample_rate; /* 8000 to 384000 (resampled to the mixer's) */
        uint32_t channels;    /* 1, 2, 4, 6 or 8 */
        uint64_t frames;      /* the length; at least 1 */
        uint32_t loop_start;  /* as for an engine_audio_clip_info */
        uint32_t loop_end;
        /* Writes up to `frames` interleaved float frames to `out` and returns how many (0 at the end). */
        uint32_t (*read)(void* user, float* out, uint32_t frames);
        /* Moves to `frame`; returns 0 on success. */
        int (*seek)(void* user, uint64_t frame);
        void* user;
    } engine_audio_stream_source;

    /* A live stream the host feeds as its audio arrives (audio_stream_open_push), for a call's participants: no
     * callback, no refill job, nothing between audio_stream_push and the audio thread but a ring. Frames are
     * interleaved, at the mixer's sample rate (engine_audio_state.sample_rate: no resampling). */
    typedef struct engine_audio_push_info
    {
        uint32_t struct_size;
        uint32_t sample_rate;     /* the mixer's */
        uint32_t channels;        /* 1, 2, 4, 6 or 8 */
        uint32_t capacity_frames; /* the most frames kept, 64 to 480000 (9600 is 200 ms at 48 kHz): a push past it makes
                                     the oldest frames go, counted in frames_dropped */
        uint32_t
            prefill_frames; /* frames to collect before the voice plays, and again after it ran dry (a jitter buffer,
                               the delay it adds to the voice); 0 plays what is there; at most capacity_frames */
    } engine_audio_push_info;

    /* What audio_stream_stats reports. */
    typedef struct engine_audio_stream_stats
    {
        uint32_t struct_size;
        uint32_t is_push;         /* 1 for a stream of audio_stream_open_push */
        uint32_t buffered_frames; /* frames waiting for the audio thread */
        uint32_t reserved;
        uint64_t frames_pushed;  /* push streams: frames accepted */
        uint64_t frames_dropped; /* push streams: frames lost to a full ring (the oldest) or to a second thread pushing
                                    at once */
        uint64_t underruns; /* mixer blocks that found fewer frames than they needed (silence for the rest; the voice
                               stays alive) */
    } engine_audio_stream_stats;

    /* The tempo of a music track, for bar and beat timing. */
    typedef struct engine_audio_tempo
    {
        uint32_t struct_size;
        float beats_per_minute; /* above 0 */
        uint32_t beats_per_bar; /* at least 1 */
    } engine_audio_tempo;

    /* The effects of a bus insert (audio_bus_effect_set). The numbers in `params` by kind:
     *   FILTER: params[0] type (0 low-pass, 1 high-pass, 2 band-pass, 3 notch, 4 peaking, 5 low shelf, 6 high shelf),
     *           [1] cutoff in Hz, [2] q, [3] gain in dB (peaking and shelves);
     *   DELAY:  [0] delay in seconds (up to 2), [1] feedback 0 to 0.995, [2] wet, [3] dry, [4] damping 0 to 1;
     *   REVERB: [0] size, [1] decay in seconds, [2] damping, [3] wet, [4] dry, [5] pre-delay in seconds.
     * Kind NONE removes the insert of the slot. */
#define ENGINE_AUDIO_EFFECT_NONE 0u
#define ENGINE_AUDIO_EFFECT_FILTER 1u
#define ENGINE_AUDIO_EFFECT_DELAY 2u
#define ENGINE_AUDIO_EFFECT_REVERB 3u
    typedef struct engine_audio_effect
    {
        uint32_t struct_size;
        uint32_t kind; /* ENGINE_AUDIO_EFFECT_* */
        float params[8];
    } engine_audio_effect;

    /* ---- the function table
     * ------------------------------------------------------------------------------------------------------------------------
     */

    /* ---- Phase 27 Day 471: scripts ----
     *
     * A world can run scripts: behaviors (tables of callbacks a script registers with `behavior(name, table)`) attached
     * to entities by ENGINE_COMPONENT_SCRIPT and run by the frame on the engine's jobs, and global functions the host
     * calls with script_call. Values cross the boundary as an engine_value_pool: arrays of engine_value and a byte
     * pool, addressed by index and offset (no pointer into the engine, nothing allocated by one side and freed by the
     * other). The runtime of a world is made by the first script_load or the first script component set on it. */

    /* The kinds of engine_value.tag. */
    typedef enum engine_value_tag
    {
        ENGINE_VALUE_NIL = 0,
        ENGINE_VALUE_BOOL = 1,   /* `i` is 0 or 1 */
        ENGINE_VALUE_INT = 2,    /* `i` */
        ENGINE_VALUE_NUMBER = 3, /* `f` (a script's vec2, vec3, vec4 and quat come out as an ARRAY of NUMBERs) */
        ENGINE_VALUE_STRING = 4, /* `count` bytes at byte offset `i` of the pool's bytes (UTF-8, not NUL-terminated) */
        ENGINE_VALUE_BYTES = 5,  /* as STRING, a blob */
        ENGINE_VALUE_ARRAY = 6,  /* `count` values at item index `i`, one after the other */
        ENGINE_VALUE_MAP = 7,    /* `count` pairs at item index `i`: a STRING key then its value, 2 * count items */
        ENGINE_VALUE_ENTITY = 8  /* `i` is the entity key (the u64 the world functions take) */
    } engine_value_tag;

    /* One value. A STRING or BYTES value names bytes of the pool; an ARRAY or MAP names items of the pool (the items
     * of a container come after every root, a container's children after it; no cycles). */
    typedef struct engine_value
    {
        uint32_t tag; /* engine_value_tag */
        uint32_t count;
        int64_t i;
        double f;
    } engine_value;

    /* A list of values and what they point to. As an input (script_call's `args`, a script component's `params`) the
     * host fills `items[0 .. item_count)` and `bytes[0 .. byte_count)`; the first `root_count` items are the values
     * themselves (the arguments, in order), the rest are the children the roots name. As an output (script_call's
     * `results`, the results a host function returns) the writer sets the capacities and the arrays; it fills the same
     * fields and, when a capacity is too small, nothing is written but `item_count` and `byte_count` tell what is
     * needed (the call fails with TOO_LARGE, and a retry with that room succeeds when the script is deterministic).
     * Limits of an input: 65536 items, 1 MiB of text, 16 MiB of blob, depth 32; a value outside them is
     * INVALID_ARGUMENT. */
    typedef struct engine_value_pool
    {
        uint32_t struct_size;
        uint32_t root_count;
        size_t item_count;
        size_t item_capacity;
        engine_value* items;
        size_t byte_count;
        size_t byte_capacity;
        uint8_t* bytes;
    } engine_value_pool;

    /* script_load: a script every state of the world loads, run in the order loaded. */
    typedef struct engine_script_load
    {
        uint32_t struct_size;
        uint32_t flags;       /* 0 */
        engine_string name;   /* the chunk name in messages (may be empty) */
        engine_string source; /* the script's text; copied */
    } engine_script_load;

    /* ENGINE_COMPONENT_SCRIPT: attaches a behavior to an entity. The instance is made at the next frame; `enabled` 0
     * keeps it from running. On a read `params` is NULL. */
    typedef struct engine_script_component
    {
        uint32_t struct_size;
        uint32_t enabled;
        char behavior[64];               /* the behavior's name, NUL-terminated */
        const engine_value_pool* params; /* a map (or NULL): the instance reads it as self.params; copied */
    } engine_script_component;

    /* script_set_budget: the limits of one script_call, and of the callbacks of the frame when set before the world's
     * runtime exists. 0 means no limit for that field. */
    typedef struct engine_script_budget
    {
        uint32_t struct_size;
        uint32_t reserved;
        uint64_t instructions; /* VM instructions */
        uint64_t nanoseconds;  /* wall time */
        uint64_t memory_bytes; /* the state's memory */
    } engine_script_budget;

    /* What script_stats reports. */
    typedef struct engine_script_stats
    {
        uint32_t struct_size;
        uint32_t partitions;     /* states the world runs callbacks in; 0 before the runtime exists */
        uint64_t ticks;          /* frames the scripts ran */
        uint64_t instances;      /* instances alive */
        uint64_t updates;        /* on_update calls */
        uint64_t errors;         /* callbacks that failed */
        uint64_t calls;          /* script_call calls made */
        uint64_t host_calls;     /* calls scripts made to host functions */
        uint32_t scripts_loaded; /* script_load calls that succeeded */
        uint32_t host_functions; /* script_register_function calls that succeeded */
    } engine_script_stats;

    /* The flags of script_register_function. */
#define ENGINE_SCRIPT_FUNCTION_PURE                                                                                    \
    0x1u /* the function touches only its arguments: it is also installed in the states of script tasks */

    /* A host function scripts can call. `args` holds the call's arguments (root_count of them; read only). Fill
     * `results` (its arrays have room for 64 items and 4096 bytes; set root_count, item_count and byte_count) and
     * return ENGINE_OK, or return an error status: the calling script gets a script error. It runs on an engine worker
     * thread, in whichever partition called it, several calls at once: it must be thread-safe, must not block for long
     * and must not call any function of the engine API (the frame holds the world). */
    typedef engine_status (*engine_script_fn)(void* user, const engine_value_pool* args, engine_value_pool* results);

    /* A script event delivered to the host. `payload` holds one root (nil when the event has none); it is valid only
     * during the call. It runs on the thread that runs the frame (world_frame), at the start of the script tick, in
     * the bus order, with the world held: it must not call any function of the engine API and must not block. */
    typedef void (*engine_script_event_fn)(void* user, engine_string name, uint64_t sender,
                                           const engine_value_pool* payload);

    /* The kinds of script_task_run. */
#define ENGINE_SCRIPT_TASK_RUN 1u /* the source is the body of `function(args)`; the result is its first result */
#define ENGINE_SCRIPT_TASK_MAP                                                                                         \
    2u /* `function(item, index)` over the array in `data`; the result is the array of the results */
#define ENGINE_SCRIPT_TASK_FOR                                                                                         \
    3u /* `function(first, last, args)` once per chunk of `count` indices; the result is one result per chunk */

    /* What script_task_run runs: a pure task over the engine's task states. */
    typedef struct engine_script_task
    {
        uint32_t struct_size;
        uint32_t mode;        /* ENGINE_SCRIPT_TASK_* */
        engine_string source; /* the function body */
        const engine_value_pool*
            data;          /* RUN: the argument; MAP: the array; FOR: the argument of every chunk; or NULL */
        uint64_t count;    /* FOR: the number of indices (MAP takes it from the array) */
        uint64_t seed;     /* the seed of the task's random numbers */
        uint32_t chunk;    /* elements per chunk; 0 the default (256) */
        uint32_t reserved; /* zero */
    } engine_script_task;

    typedef struct engine_api
    {
        uint32_t struct_size; /* bytes of this table the library filled (at most what the host asked for) */
        uint32_t abi_major;   /* the library's own version */
        uint32_t abi_minor;
        uint32_t sanitizer; /* an engine_sanitizer */

        /* The name of a status, `ok`, `invalid_argument`, ...; static, never NULL; `unknown` for a number that is not a
         * status. */
        engine_string (*status_name)(engine_status status);

        /* Creates the engine: its settings (declared by the engine, filled from the host's entries, then the command
         * line; the environment and the user's own settings file are not read), its logging, its job system on demand.
         * A bad key, a value of the wrong type or a command-line error fails the call and creates nothing. `*out` is
         * zero on failure. Fails with: INVALID_ARGUMENT, STRUCT_SIZE, LIMIT_EXCEEDED (an engine exists), NOT_FOUND (an
         * unknown key), UNSUPPORTED, DENIED. */
        engine_status (*create)(const engine_create_info* info, engine_handle* out);

        /* Destroys the engine and invalidates its handle. Waits for log callbacks in flight; no callback runs after it
         * returns. Fails with: STALE_HANDLE, INVALID_ARGUMENT. */
        engine_status (*destroy)(engine_handle engine);

        /* Reports what the engine holds. Fails with: STALE_HANDLE, INVALID_ARGUMENT, STRUCT_SIZE. */
        engine_status (*get_info)(engine_handle engine, engine_info* out);

        /* Sets a setting from its text form, as the command line would not (the value is a layer below the command
         * line, so a flag the host passed wins). Fails with: STALE_HANDLE, INVALID_ARGUMENT (bad value), NOT_FOUND
         * (unknown key), UNSUPPORTED, DENIED, IO. */
        engine_status (*setting_set)(engine_handle engine, engine_string key, engine_string value);

        /* Reads a setting's current value as text into a buffer the host releases. Fails with: STALE_HANDLE,
         * INVALID_ARGUMENT, NOT_FOUND. */
        engine_status (*setting_get)(engine_handle engine, engine_string key, engine_buffer* out);

        /* The text of the calling thread's most recent failed call, in a buffer the host releases (empty if none
         * failed). Always succeeds with a valid `out`. Fails with: INVALID_ARGUMENT (`out` is NULL). */
        engine_status (*last_error)(engine_buffer* out);

        /* Returns a named extension: a struct of functions beyond the core, or NULL when this library does not have it.
         * The extension's own header says what the pointer points to; it is static, like the table. The core API never
         * needs one. The library's own tests use `engine.test_state`, which only a build made for testing carries (a
         * release build returns NULL for it). */
        const void* (*get_extension)(engine_string name);

        /* Tells the engine that the calling thread is about to end: it hands back what the thread cached (frame memory,
         * crash-handler registration) so that nothing is left behind in a library that may be unloaded afterwards. A
         * host calls it on every thread that called the engine, once, just before the thread exits; the engine's own
         * worker threads do it themselves. Calling it earlier is harmless: the thread may keep using the engine. */
        void (*thread_detach)(void);

        /* ---- Day 349 ---- */

        /* Runs one frame: every world's systems (today the transform propagation) once, in the order the worlds were
         * created, on the calling thread. The host drives the loop: nothing here waits for the host or blocks on a
         * thread of its own, and no engine thread runs a frame by itself. With a budget, a world that has not started
         * when the budget is spent is left for the next frame (the first world of a frame always runs, so a budget too
         * small for one world cannot starve it) (counted in `worlds_skipped`), and the next frame begins with the first
         * world it left. `stats` may be NULL. Thread rule: one thread at a time per engine (the host's frame thread);
         * other API calls may run beside it, world calls wait for it. Fails with: STALE_HANDLE, INVALID_ARGUMENT
         * (flags, a delta that is negative, NaN or above 3600 seconds), STRUCT_SIZE. */
        engine_status (*frame)(engine_handle engine, const engine_frame_info* info, engine_frame_stats* stats);

        /* Creates an empty world (entities and their components). The world counts as an object of the engine:
         * engine_destroy refuses (IN_USE) while one exists. Fails with: STALE_HANDLE, INVALID_ARGUMENT, LIMIT_EXCEEDED.
         */
        engine_status (*world_create)(engine_handle engine, engine_handle* out);

        /* Destroys a world and every entity in it. Fails with: STALE_HANDLE, INVALID_ARGUMENT, TYPE_MISMATCH (not a
         * world). */
        engine_status (*world_destroy)(engine_handle world);

        /* Creates an entity with no components and returns its key: 64 bits whose generation part makes a key of a
         * destroyed entity fail with STALE_HANDLE. Fails with: STALE_HANDLE, INVALID_ARGUMENT, TYPE_MISMATCH,
         * LIMIT_EXCEEDED. */
        engine_status (*entity_create)(engine_handle world, uint64_t* out_key);

        /* Destroys an entity and its components. Fails with: STALE_HANDLE (the world or the entity), INVALID_ARGUMENT,
         * TYPE_MISMATCH. */
        engine_status (*entity_destroy)(engine_handle world, uint64_t key);

        /* A component's `kind` is an engine_component_kind passed as a uint32_t (a number that is not a kind is
         * INVALID_ARGUMENT, and a C++ enum could not hold it). Sets a component from a struct (`value` points to the
         * struct of the kind, its struct_size first; a smaller size than the first version is STRUCT_SIZE). The
         * component is added if the entity has none. Nothing of the world escapes: the engine copies.
         * ENGINE_COMPONENT_WORLD_TRANSFORM is read only (UNSUPPORTED), ENGINE_COMPONENT_NAME has its own functions
         * (INVALID_ARGUMENT). Fails with: STALE_HANDLE, INVALID_ARGUMENT, TYPE_MISMATCH, STRUCT_SIZE, UNSUPPORTED. */
        engine_status (*component_set)(engine_handle world, uint64_t key, uint32_t kind, const void* value);

        /* Reads a component into `out` (the host sets out->struct_size to its sizeof; the library writes at most that
         * many bytes and sets it to what it wrote). Fails with: STALE_HANDLE, INVALID_ARGUMENT, TYPE_MISMATCH,
         * STRUCT_SIZE, NOT_FOUND (the entity has no such component). */
        engine_status (*component_get)(engine_handle world, uint64_t key, uint32_t kind, void* out);

        /* Removes a component. Fails with: STALE_HANDLE, INVALID_ARGUMENT, TYPE_MISMATCH, NOT_FOUND (the entity has
         * none), UNSUPPORTED (world transform). */
        engine_status (*component_remove)(engine_handle world, uint64_t key, uint32_t kind);

        /* Tells whether an entity has a component: `*out` is 1 or 0. Fails with: STALE_HANDLE, INVALID_ARGUMENT,
         * TYPE_MISMATCH. */
        engine_status (*component_has)(engine_handle world, uint64_t key, uint32_t kind, int* out);

        /* Gives an entity a name (UTF-8, at most ENGINE_NAME_MAX_BYTES bytes). Fails with: STALE_HANDLE,
         * INVALID_ARGUMENT, TYPE_MISMATCH, TOO_LARGE. */
        engine_status (*entity_name_set)(engine_handle world, uint64_t key, engine_string name);

        /* Reads an entity's name into a buffer the host releases. Fails with: STALE_HANDLE, INVALID_ARGUMENT,
         * TYPE_MISMATCH, NOT_FOUND (no name). */
        engine_status (*entity_name_get)(engine_handle world, uint64_t key, engine_buffer* out);

        /* Finds the entities that have every component of `mask` (ENGINE_COMPONENT_BIT bits; 0 matches every entity).
         * Writes up to `capacity` keys to `keys` (which may be NULL when `capacity` is 0) and the number of matches to
         * `*total`: when it is above `capacity` the host calls again with a bigger array. The order is not specified
         * and may change when components are added or removed. Fails with: STALE_HANDLE, INVALID_ARGUMENT (an unknown
         * bit, NULL `total`), TYPE_MISMATCH. */
        engine_status (*query)(engine_handle world, uint32_t mask, uint64_t* keys, size_t capacity, size_t* total);

        /* Creates a counter: a count of jobs not yet finished. It counts as an object of the engine until
         * counter_destroy. Fails with: STALE_HANDLE, INVALID_ARGUMENT, LIMIT_EXCEEDED. */
        engine_status (*counter_create)(engine_handle engine, engine_handle* out);

        /* Destroys a counter. Fails with: STALE_HANDLE, INVALID_ARGUMENT, TYPE_MISMATCH, IN_USE (jobs submitted with it
         * have not finished, or a thread waits on it). */
        engine_status (*counter_destroy)(engine_handle counter);

        /* Queues `fn(user)` to run on an engine worker thread (the engine starts its job system on the first call) and
         * counts it on `counter` until it ends. Any thread, a job included. At most 2048 jobs may be queued or running
         * at once: past that the call fails and the host waits for some to finish. Fails with: STALE_HANDLE,
         * INVALID_ARGUMENT (NULL `fn`), TYPE_MISMATCH, LIMIT_EXCEEDED. */
        engine_status (*jobs_submit)(engine_handle engine, engine_job_fn fn, void* user, engine_handle counter);

        /* Reads how many jobs submitted with the counter have not finished. Fails with: STALE_HANDLE, INVALID_ARGUMENT,
         * TYPE_MISMATCH. */
        engine_status (*counter_value)(engine_handle counter, uint64_t* out);

        /* Waits until the counter reaches zero, at most `timeout_ms` milliseconds (UINT64_MAX waits for ever). The
         * calling thread sleeps; it does not run jobs. Not from inside a job (INVALID_ARGUMENT). Returns OK when the
         * count is zero, NOT_READY when the time ran out. Fails with: STALE_HANDLE, INVALID_ARGUMENT, TYPE_MISMATCH,
         * NOT_READY. */
        engine_status (*counter_wait)(engine_handle counter, uint64_t timeout_ms);

        /* Installs the engine's crash handler for the process: a crash writes a dump to the directory of the setting
         * `crash.dump_directory` (give it in engine_create_info.settings; empty: the user's crash directory) and the
         * handler that was there before is back after crash_handler_uninstall (or engine_destroy, which uninstalls).
         * On Linux and macOS that earlier handler also runs at a crash, after the report and the dump, once, in signal
         * context on the faulting thread; on Windows an unhandled-exception filter set before runs after them on the
         * faulting thread (a SIGABRT handler set before, for an abort); then the original fault ends the process.
         * Fails with:
         * STALE_HANDLE, INVALID_ARGUMENT, ALREADY_EXISTS (installed), IO (the directory), UNSUPPORTED. */
        engine_status (*crash_handler_install)(engine_handle engine);

        /* Removes the engine's crash handler and restores the previous one. Fails with: STALE_HANDLE, INVALID_ARGUMENT,
         * NOT_FOUND (not installed). */
        engine_status (*crash_handler_uninstall)(engine_handle engine);

        /* ---- Day 350: resources ---- */

        /* Adds a resource to the engine's memory backend (host-provided content, mounted above the files and paks:
         * setting `assets.memory_priority`). The blob is checked as a whole before anything changes. Without
         * ENGINE_RESOURCE_BORROW the bytes are copied (the host may free them when the call returns); with it the
         * engine reads them in place and the host must keep them valid and unchanged until `release` runs. The budget
         * is the setting `assets.memory_backend_bytes`, lent bytes included. Fails with: STALE_HANDLE, INVALID_ARGUMENT
         * (zero id, bytes NULL, flags, release missing or given without BORROW), STRUCT_SIZE, ALREADY_EXISTS (the id or
         * the path is live: use resource_replace), CORRUPT (not a blob, the id inside is another, the hash does not
         * verify or differs from `expected_hash`), UNSUPPORTED_VERSION, TYPE_MISMATCH, LIMIT_EXCEEDED (the budget). */
        engine_status (*resource_add)(engine_handle engine, const engine_resource_add_info* info);

        /* Replaces the bytes of a resource in the memory backend. A load that is reading the old bytes finishes with
         * them; the old bytes are released after their last reader. The paths stay unless `info->path` names one. A
         * loaded copy of the replaced resource is rebuilt from the new bytes before the call returns (its handles stay
         * valid; if the new blob cannot be loaded, for instance a dependency no longer matches, the old version stays
         * loaded). If a loaded resource depends on this one the call fails with IN_USE and writes those resources' ids
         * (uint64_t values) to `*dependents` (a buffer the host releases; may be NULL) and changes nothing; with
         * ENGINE_RESOURCE_FORCE the dependents are unloaded first (their handles become stale: resource_status reports
         * `loaded` 0) and their ids are written. Fails with: those of resource_add except ALREADY_EXISTS (for a path of
         * another resource), and NOT_FOUND (not in the memory backend), IN_USE. */
        engine_status (*resource_replace)(engine_handle engine, const engine_resource_add_info* info,
                                          engine_buffer* dependents);

        /* Removes a resource from the memory backend (flags: ENGINE_RESOURCE_FORCE), as resource_replace refuses or
         * forces for dependents. A copy of the resource that is loaded and has no dependents stays loaded for as long
         * as its handles are held (it no longer has its bytes in the backend: a load of it fails with NOT_FOUND). Fails
         * with: STALE_HANDLE, INVALID_ARGUMENT (flags), NOT_FOUND, IN_USE. */
        engine_status (*resource_remove)(engine_handle engine, uint64_t id, uint32_t flags, engine_buffer* dependents);

        /* Reads what the memory backend holds of a resource (`out->struct_size` set by the host). Fails with:
         * STALE_HANDLE, INVALID_ARGUMENT, STRUCT_SIZE, NOT_FOUND. */
        engine_status (*resource_mounted)(engine_handle engine, uint64_t id, engine_resource_info* out);

        /* Loads a resource and everything it depends on (a closure, atomically), or finds it loaded, and returns a
         * handle that keeps it loaded. The resource is named by `path` (resolved through the mounts' tables) when
         * `path.length` is not 0, else by `id`. `type` is the type the host expects (a resource of another type is
         * TYPE_MISMATCH). Nothing of the loaded data is exposed: rendering and the other engine functions use the
         * handle. The handle counts as an object of the engine until resource_release. Fails with: STALE_HANDLE,
         * INVALID_ARGUMENT, NOT_FOUND, MISSING_DEPENDENCY, TYPE_MISMATCH, DEPENDENCY_CYCLE, CORRUPT,
         * UNSUPPORTED_VERSION, LIMIT_EXCEEDED, NOT_READY, UNSUPPORTED (no loader for the type). */
        engine_status (*resource_load)(engine_handle engine, uint64_t id, engine_string path, uint32_t type,
                                       engine_handle* out);

        /* Gives a resource handle back. Fails with: STALE_HANDLE, INVALID_ARGUMENT, TYPE_MISMATCH. */
        engine_status (*resource_release)(engine_handle resource);

        /* Reports a resource handle: `loaded` is 1 while the resource is still loaded, 0 after a forced replace or
         * remove unloaded it. Fails with: STALE_HANDLE, INVALID_ARGUMENT, STRUCT_SIZE, TYPE_MISMATCH. */
        engine_status (*resource_status)(engine_handle resource, engine_resource_info* out);

        /* Starts loading a resource on the engine's workers and returns a ticket at once (a ticket that is already done
         * when the resource is loaded). `priority` is ENGINE_LOAD_*. The objects are built by calls of the host's own
         * thread (frame, ticket_poll, ticket_wait, ticket_finish), never by an engine thread on its own. The ticket
         * counts as an object of the engine until ticket_destroy. Fails with: INVALID_ARGUMENT, UNSUPPORTED (no
         * loader), LIMIT_EXCEEDED (too many tickets, STALE_HANDLE; every error of the load itself, a full queue
         * included, comes with ticket_finish). */
        engine_status (*resource_load_async)(engine_handle engine, uint64_t id, engine_string path, uint32_t type,
                                             uint32_t priority, engine_handle* ticket);

        /* Looks at a ticket without waiting (it also builds what is ready). Fails with: STALE_HANDLE, INVALID_ARGUMENT,
         * STRUCT_SIZE, TYPE_MISMATCH. */
        engine_status (*ticket_poll)(engine_handle ticket, engine_ticket_info* out);

        /* Waits until the load is done, at most `timeout_ms` (UINT64_MAX waits for ever), building the objects as they
         * are read. Not from inside a job or a release callback (INVALID_ARGUMENT). Returns OK when done (the outcome
         * comes from ticket_finish), NOT_READY on a timeout. Fails with: STALE_HANDLE, INVALID_ARGUMENT, TYPE_MISMATCH,
         * NOT_READY. */
        engine_status (*ticket_wait)(engine_handle ticket, uint64_t timeout_ms);

        /* Takes the outcome of a finished load: OK and a resource handle in `*resource` (as resource_load's), or the
         * load's error (and `*resource` is zero), CANCELLED after ticket_cancel. Fails with: NOT_READY (not done), and
         * the errors of resource_load, CANCELLED, STALE_HANDLE, INVALID_ARGUMENT, TYPE_MISMATCH. */
        engine_status (*ticket_finish)(engine_handle ticket, engine_handle* resource);

        /* Asks the load to stop; the ticket reads as done with CANCELLED soon after (unless it had finished). Fails
         * with: STALE_HANDLE, INVALID_ARGUMENT, TYPE_MISMATCH. */
        engine_status (*ticket_cancel)(engine_handle ticket);

        /* Destroys a ticket, cancelling a load that has not finished. A resource the ticket would have given is
         * released. Fails with: STALE_HANDLE, INVALID_ARGUMENT, TYPE_MISMATCH. */
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

        /* Makes a surface: the engine renders into the host's window on every `frame` (a clear to the background color
         * in this version). A device is made for the window (one per surface). Counts as an object of the engine until
         * surface_destroy. Fails with: STALE_HANDLE, INVALID_ARGUMENT (NULL, flags, missing handles, a size or scale
         * out of range), STRUCT_SIZE, UNSUPPORTED (the kind is not in engine_info.caps, or no GPU can present to the
         * window), LIMIT_EXCEEDED (64 surfaces). */
        engine_status (*surface_create)(engine_handle engine, const engine_surface_info* info, engine_handle* out);

        /* Destroys a surface and its device (read-back tickets of the surface fail with CANCELLED). Call it before the
         * window goes. Fails with: STALE_HANDLE, INVALID_ARGUMENT, TYPE_MISMATCH. */
        engine_status (*surface_destroy)(engine_handle surface);

        /* Forwards one input event of the host's window to the engine's input stream for the surface (the UI toolkit,
         * the camera controllers and the demos read it as from a window of their own). Call it from the host's frame
         * thread, in the order the host got the events. A full queue (1024) drops the event and counts it in
         * surface_state. Fails with: STALE_HANDLE, INVALID_ARGUMENT (NULL, unknown type or key or button, text that is
         * not UTF-8 or longer than 256 bytes), STRUCT_SIZE, TYPE_MISMATCH. */
        engine_status (*surface_input)(engine_handle surface, const engine_input_event* event);

        /* Tells the engine the window's size in pixels and its scale (0 means 1). A size of 0 in either dimension
         * pauses rendering (the window is hidden or minimized); the next frame after a real size resumes. Call from the
         * host's frame thread, before `frame`. Fails with: STALE_HANDLE, INVALID_ARGUMENT, TYPE_MISMATCH. */
        engine_status (*surface_resized)(engine_handle surface, uint32_t width, uint32_t height, float scale);

        /* Rebuilds the swapchain on a window the host re-created or re-mapped (after `lost` in surface_state, or for a
         * new native window of the same kind), through the device reset path. Fails with: those of surface_create,
         * UNSUPPORTED (the device cannot present to the new window; the surface stays lost). */
        engine_status (*surface_rebind)(engine_handle surface, const engine_surface_info* info);

        /* Sets the color the surface is cleared to (four values from 0 to 1, RGBA). Fails with: STALE_HANDLE,
         * INVALID_ARGUMENT, TYPE_MISMATCH. */
        engine_status (*surface_set_background)(engine_handle surface, const float* rgba);

        /* Reads the state of a surface (`out->struct_size` set by the host). Fails with: STALE_HANDLE,
         * INVALID_ARGUMENT, STRUCT_SIZE, TYPE_MISMATCH. */
        engine_status (*surface_state)(engine_handle surface, engine_surface_state* out);

        /* Asks for the pixels of the next frame the surface presents and returns a ticket at once (the frame is
         * rendered by `frame`). The ticket counts as an object of the engine until pixels_destroy. Fails with:
         * STALE_HANDLE, INVALID_ARGUMENT, TYPE_MISMATCH, LIMIT_EXCEEDED (256 tickets). */
        engine_status (*surface_read_pixels)(engine_handle surface, engine_handle* ticket);

        /* Takes the pixels of a finished read-back (once). Fails with: NOT_READY (no frame has been presented since the
         * request), CANCELLED (the surface was destroyed), UNSUPPORTED (the frame could not be read), NOT_FOUND (taken
         * already), STALE_HANDLE, INVALID_ARGUMENT, STRUCT_SIZE, TYPE_MISMATCH. */
        engine_status (*pixels_finish)(engine_handle ticket, engine_pixels* out);

        /* Destroys a read-back ticket (and the pixels it still holds). Fails with: STALE_HANDLE, INVALID_ARGUMENT,
         * TYPE_MISMATCH. */
        engine_status (*pixels_destroy)(engine_handle ticket);

        /* ---- Phase 24 Day 359: a world on a surface ---- */

        /* Makes the surface draw `world` (every entity with ENGINE_COMPONENT_TRANSFORM and
         * ENGINE_COMPONENT_MESH_REFERENCE) on each `frame`, after the worlds have run, instead of clearing to its
         * background color, through a fixed default camera and a default light (Days 360 and 361 let the host name its
         * own). A `world` whose value is 0 unbinds the surface (it clears again). Several surfaces may show one world.
         * A world destroyed under the surface unbinds it at the next frame with `world_status` STALE_HANDLE in
         * surface_state. The surface makes what it draws resident on its own device the first frame an entity names it:
         * a mesh or material that is not mounted (or not of the type) draws nothing and is counted in
         * `entities_unresolved`. Fails with: STALE_HANDLE, INVALID_ARGUMENT, TYPE_MISMATCH, UNSUPPORTED (the surface is
         * lost, or its device cannot draw a scene). Needs ENGINE_CAP_WORLD_ON_SURFACE. */
        engine_status (*surface_set_world)(engine_handle surface, engine_handle world);

        /* ---- Phase 24 Day 360: cameras ---- */

        /* Makes the surface look through the entity `camera_entity` (an entity of the bound world with
         * ENGINE_COMPONENT_CAMERA and ENGINE_COMPONENT_TRANSFORM) on each frame; 0 returns the surface to the default
         * camera. Several surfaces may name different cameras of one world. A camera entity that is destroyed or loses
         * a component makes the surface draw through the default camera and report NOT_FOUND in `camera_status` of
         * surface_state, never a crash; the name is kept, so a camera that comes back (its component set again) is used
         * again. Naming a camera before the world is bound is allowed. Fails with: STALE_HANDLE, INVALID_ARGUMENT,
         * TYPE_MISMATCH. */
        engine_status (*surface_set_camera)(engine_handle surface, uint64_t camera_entity);

        /* Turns the surface pixel (`x`, `y`) (top left is (0, 0); fractions allowed, the pixel centre is +0.5) into a
         * world ray for picking: `origin` and `direction` (unit length) receive three floats each. It reads the camera
         * and the size of the last frame that drew the surface (the camera as of the frame, not as of this call), so a
         * host that moved the camera since asks again after the next `frame`. Fails with: STALE_HANDLE,
         * INVALID_ARGUMENT, NOT_READY (no frame has drawn a scene on this surface yet), TYPE_MISMATCH. */
        engine_status (*surface_pixel_ray)(engine_handle surface, float x, float y, float* origin, float* direction);

        /* The inverse: projects the world point `point` (three floats) with the camera of the last frame. `out`
         * receives three floats: the pixel x and y (as above) and the distance along the camera's viewing direction in
         * metres (negative behind the camera, where x and y are not meaningful). Fails with: STALE_HANDLE,
         * INVALID_ARGUMENT, NOT_READY, TYPE_MISMATCH. */
        engine_status (*surface_project)(engine_handle surface, const float* point, float* out);

        /* ---- Phase 24 Day 361: lights and the environment ---- */

        /* Sets the environment of `world` (ambient light, the sky colour behind the scene, the exposure): see
         * engine_environment. Every surface that shows the world uses it from its next frame. Fails with:
         * STALE_HANDLE, INVALID_ARGUMENT (a NULL or not finite value, a negative intensity, an exposure that is not
         * positive), STRUCT_SIZE, TYPE_MISMATCH. */
        engine_status (*world_set_environment)(engine_handle world, const engine_environment* environment);

        /* ---- Phase 24 Day 362: hierarchy, names, visibility and material overrides ---- */

        /* Makes `parent_key` the parent of `child_key` (an entity with ENGINE_COMPONENT_TRANSFORM moves with its
         * parent: its transform becomes relative to the parent's). A `parent_key` of 0 makes the child a root again.
         * Destroying a parent does not destroy its children: they become roots at the next frame, keeping their world
         * place only if the host sets it. Fails with: STALE_HANDLE (either entity), INVALID_ARGUMENT (the same entity,
         * or a cycle: the parent is a descendant of the child), LIMIT_EXCEEDED (the hierarchy would be too deep),
         * TYPE_MISMATCH. */
        engine_status (*entity_set_parent)(engine_handle world, uint64_t child_key, uint64_t parent_key);

        /* Reads the parent of an entity into `*parent_key` (0 for a root). Fails with: STALE_HANDLE, INVALID_ARGUMENT,
         * TYPE_MISMATCH. */
        engine_status (*entity_get_parent)(engine_handle world, uint64_t key, uint64_t* parent_key);

        /* Finds the first entity whose name is exactly `name` (see entity_name_set); names are unique only by the
         * host's discipline, and "first" is the order of the world's storage, which is not creation order. Fails with:
         * STALE_HANDLE, INVALID_ARGUMENT, NOT_FOUND, TYPE_MISMATCH. */
        engine_status (*entity_find)(engine_handle world, engine_string name, uint64_t* key);

        /* Writes the keys of every entity named `name` into `keys` (up to `capacity`; `keys` may be NULL when
         * `capacity` is 0) and the number found to `*total`. Fails with: STALE_HANDLE, INVALID_ARGUMENT,
         * TYPE_MISMATCH. */
        engine_status (*entity_find_all)(engine_handle world, engine_string name, uint64_t* keys, size_t capacity,
                                         size_t* total);

        /* Sets an override of the slot named `slot` (see engine_slot_override) on the entity; setting the same slot
         * again replaces it. An entity holds up to 8 overrides. The override is applied from the next frame a surface
         * draws the entity; a texture that is not mounted makes the entity draw with the original texture and counts it
         * in `entities_unresolved`. Fails with: STALE_HANDLE, INVALID_ARGUMENT (an unknown slot, a kind that does not
         * match the slot, a value that is not finite; `last_error` names the slot), STRUCT_SIZE, LIMIT_EXCEEDED,
         * TYPE_MISMATCH. */
        engine_status (*entity_set_slot_override)(engine_handle world, uint64_t key, engine_string slot,
                                                  const engine_slot_override* value);

        /* Removes the override of `slot`, or every override of the entity when `slot` is empty. Fails with:
         * STALE_HANDLE, INVALID_ARGUMENT, NOT_FOUND (a named slot with no override), TYPE_MISMATCH. */
        engine_status (*entity_clear_slot_override)(engine_handle world, uint64_t key, engine_string slot);

        /* ---- Phase 24 Day 363: skinned meshes and animation ---- */

        /* Plays the clip `clip` (a resource id the host added or loaded; `params` may be NULL: play once at speed 1) on
         * an entity that has an ENGINE_COMPONENT_SKELETON_INSTANCE, or, for a clip of morph weights only (no skeleton;
         * maintenance, 2026-11-20), on any entity. A clip with weight tracks gives the entity an
         * ENGINE_COMPONENT_MORPH_WEIGHTS when it has none, and writes it every frame. The clip is loaded and kept
         * loaded. The frame advances it before the transforms are propagated and the surfaces draw. Fails with:
         * STALE_HANDLE, INVALID_ARGUMENT (a clip with joint tracks on an entity with no skeleton instance, a clip made
         * for another skeleton, a bad parameter), NOT_FOUND (the clip is not mounted), TYPE_MISMATCH (not a clip, or a
         * handle that is not a world), STRUCT_SIZE. */
        engine_status (*animation_play)(engine_handle world, uint64_t key, uint64_t clip,
                                        const engine_play_params* params);

        /* Changes the speed of what plays now (>= 0; 0 pauses). Fails with: STALE_HANDLE, INVALID_ARGUMENT, NOT_FOUND
         * (nothing was started on the entity), TYPE_MISMATCH. */
        engine_status (*animation_set_speed)(engine_handle world, uint64_t key, float speed);

        /* Stops time: the entity keeps its current pose (play again to move it). Fails with: STALE_HANDLE,
         * INVALID_ARGUMENT, NOT_FOUND, TYPE_MISMATCH. */
        engine_status (*animation_stop)(engine_handle world, uint64_t key);

        /* Reads the play position of the clip playing now, in seconds. Fails with: STALE_HANDLE, INVALID_ARGUMENT,
         * NOT_FOUND, TYPE_MISMATCH. */
        engine_status (*animation_time)(engine_handle world, uint64_t key, double* seconds);

        /* Finds the joint named `name` of the entity's skeleton (the skeleton must have been built with names) and
         * writes its index to `*index`. Fails with: STALE_HANDLE, INVALID_ARGUMENT, NOT_FOUND, TYPE_MISMATCH. */
        engine_status (*skeleton_find_joint)(engine_handle world, uint64_t key, engine_string name, uint32_t* index);

        /* Reads where a joint is in the skeleton entity's own space (the skeleton instance's model space), as the last
         * frame's animation left it: a column-major 4x4 matrix in `matrix` (16 floats). Before any clip has played this
         * is the bind pose. Multiply by the entity's world matrix (ENGINE_COMPONENT_WORLD_TRANSFORM) for the world.
         * Fails with: STALE_HANDLE, INVALID_ARGUMENT (a joint index that does not exist, a NULL pointer), NOT_FOUND (no
         * skeleton instance), TYPE_MISMATCH. */
        engine_status (*skeleton_joint_transform)(engine_handle world, uint64_t key, uint32_t joint, float* matrix);

        /* Makes `key` follow a joint of the skeleton entity `skeleton_key`: from the next frame its world matrix is the
         * skeleton entity's world matrix times the joint's model matrix times the entity's own transform (its
         * ENGINE_COMPONENT_TRANSFORM, now relative to the joint). The entity should not have a parent of its own, and
         * the entities below it in the hierarchy do not follow until the frame after. A `skeleton_key` of 0 detaches
         * the entity (its world matrix is then computed from its parent again). Fails with: STALE_HANDLE (either
         * entity), INVALID_ARGUMENT (the same entity, or a joint that does not exist), NOT_FOUND (the skeleton entity
         * has no skeleton instance), TYPE_MISMATCH. */
        engine_status (*entity_attach_to_joint)(engine_handle world, uint64_t key, uint64_t skeleton_key,
                                                uint32_t joint);

        /* ---- Phase 24 Day 364: a level as a scene ---- */

        /* Puts the level `level` (a resource of type ENGINE_RESOURCE_LEVEL the host added, for example the one an
         * import with ENGINE_IMPORT_EMIT_LEVEL made) into a world: a root entity with the transform of `params` (NULL:
         * at the origin), and below it one entity per entity of the level, with its name, transform, parent, mesh
         * reference, skeleton instance, morph weights and light (a glTF's KHR_lights_punctual lights: directional,
         * point and spot; maintenance, 2026-11-20; an ambient light of a level has no C kind and is left out), and for
         * an entity with a static collider (glTF colliders, 2026-11-20: `-col`/`-colonly` meshes, OMI_physics_* shapes)
         * a static body: its collider is made at the end of the first frame that propagates the entity's world matrix,
         * with that world scale (a later scale change is counted in engine_frame_stats and not followed), and the body
         * at the next frame; it follows the entity's world position and rotation, a frame behind. A level's mesh
         * colliders (MCOL) are loaded with it. The level is spawned whole or not at all: when it, or a resource it
         * names, is not mounted (or a skeleton cannot be loaded), nothing is made and the reason is in last_error. A
         * level spawned twice makes two independent copies. `*root` receives the root entity's key; `keys` (capacity
         * `capacity`, may be NULL with 0) receives the keys of every entity of the spawn, the root first, and `*total`
         * (may be NULL) the number of them. Fails with: STALE_HANDLE, INVALID_ARGUMENT (NULL `root`, a bad parameter),
         * STRUCT_SIZE, NOT_FOUND (the level, or a resource it names, is not mounted), TYPE_MISMATCH (not a level, or a
         * handle that is not a world), UNSUPPORTED (a level that includes other levels), LIMIT_EXCEEDED (the world
         * refused an entity). */
        engine_status (*level_spawn)(engine_handle world, uint64_t level, const engine_spawn_params* params,
                                     uint64_t* root, uint64_t* keys, size_t capacity, size_t* total);

        /* ---- Phase 24 Day 365: host textures ---- */

        /* Makes a texture with no pixels yet. It counts as an object of the engine until texture_destroy. Needs
         * ENGINE_CAP_HOST_TEXTURES. Fails with: INVALID_ARGUMENT (NULL, flags, a zero size), STRUCT_SIZE, TOO_LARGE (a
         * size above `assets.host_texture.max_size`), LIMIT_EXCEEDED (too many textures), STALE_HANDLE (no engine). */
        engine_status (*texture_create)(engine_handle engine, const engine_texture_info* info, engine_handle* out);

        /* Gives the texture new pixels: `height` rows of `width` RGBA8 pixels (4 bytes each) read from `pixels`,
         * `stride` bytes apart (at least width * 4, at most 16 MiB). May be called from any thread, also while frames
         * run: it copies the rows (a short lock, no GPU work, no allocation but the texture's own pixel buffer) and
         * returns; each surface that draws the texture uploads the newest pixels in its next frame, and an update that
         * a newer one overtakes before it was uploaded is counted in engine_frame_stats.updates_replaced. `info` may be
         * NULL (the texture's own size). Fails with: STALE_HANDLE, INVALID_ARGUMENT (NULL pixels, a stride below the
         * row), TOO_LARGE (a size or a stride above its limit), STRUCT_SIZE, TYPE_MISMATCH (not a texture). */
        engine_status (*texture_update)(engine_handle texture, const void* pixels, size_t stride,
                                        const engine_update_info* info);

        /* Changes the size; the pixels are dropped, and a material slot that shows the texture goes back to its own
         * value until the next texture_update. Fails with: STALE_HANDLE, INVALID_ARGUMENT, TOO_LARGE, TYPE_MISMATCH. */
        engine_status (*texture_resize)(engine_handle texture, uint32_t width, uint32_t height);

        /* Destroys the texture. A material slot that showed it goes back to its own value at the next frame. Fails
         * with: STALE_HANDLE, INVALID_ARGUMENT, TYPE_MISMATCH. */
        engine_status (*texture_destroy)(engine_handle texture);

        /* Copies the newest pixels the engine holds, tightly packed RGBA8, into `*out` (release it with
         * engine_buffer_release) and writes the size. Fails with: STALE_HANDLE, INVALID_ARGUMENT (NULL), NOT_READY (no
         * update yet), TYPE_MISMATCH. */
        engine_status (*texture_read_pixels)(engine_handle texture, engine_buffer* out, uint32_t* width,
                                             uint32_t* height);

        /* Gives the engine the font its labels are drawn with: the bytes of a font file (TrueType or OpenType), copied.
         * The engine ships no font. Surfaces use the font at their next frame; a label is drawn only after a font is
         * set. Fails with: INVALID_ARGUMENT (NULL, zero size), TOO_LARGE (above 32 MiB), STALE_HANDLE. */
        engine_status (*label_font_set)(engine_handle engine, const void* data, size_t size);

        /* ---- Phase 25 Day 408: physics ---- */

        /* Sets the gravity of a world's physics (metres per second squared; the default is (0, -9.81, 0)). Fails with:
         * STALE_HANDLE, INVALID_ARGUMENT (NULL, not finite), TYPE_MISMATCH. */
        engine_status (*physics_set_gravity)(engine_handle world, const float* gravity);

        /* Applies a force (newtons) to the body of an entity for the next step, at `point` in world space (NULL: its
         * centre of mass). The entity needs ENGINE_COMPONENT_RIGID_BODY and ENGINE_COMPONENT_COLLIDER, and a frame must
         * have made its body. Fails with: STALE_HANDLE, INVALID_ARGUMENT (NULL force, not finite), TYPE_MISMATCH,
         * NOT_FOUND (no entity, or no body yet), UNSUPPORTED (a static body). */
        engine_status (*body_apply_force)(engine_handle world, uint64_t key, const float* force, const float* point);

        /* As body_apply_force for an impulse (newton seconds): the velocity changes at once. */
        engine_status (*body_apply_impulse)(engine_handle world, uint64_t key, const float* impulse,
                                            const float* point);

        /* Sets the velocity of a body (metres and radians per second; either may be NULL to leave it). */
        engine_status (*body_set_velocity)(engine_handle world, uint64_t key, const float* linear,
                                           const float* angular);

        /* Reads the velocity of a body (either pointer may be NULL). */
        engine_status (*body_get_velocity)(engine_handle world, uint64_t key, float* linear, float* angular);

        /* Moves a body and its entity's transform to a place at once (position x, y, z; rotation a unit quaternion x,
         * y, z, w; either may be NULL to leave it). */
        engine_status (*body_teleport)(engine_handle world, uint64_t key, const float* position, const float* rotation);

        /* Casts a ray against the colliders of a world: `direction` need not be unit, `max_distance` > 0. `layer_mask`
         * selects the layers (0xFFFFFFFF: all); triggers are not hit. `*hit` (struct_size set by the host) receives
         * the closest hit, and `*found` is 1 when there was one, else 0. Fails with: STALE_HANDLE, INVALID_ARGUMENT,
         * STRUCT_SIZE, TYPE_MISMATCH. */
        engine_status (*physics_raycast)(engine_handle world, const float* origin, const float* direction,
                                         float max_distance, uint32_t layer_mask, engine_ray_hit* hit, int* found);

        /* Finds the entities whose colliders overlap a sphere (`center`, `radius` > 0) or a box (`center`,
         * `half_extents` > 0, axis aligned); keys to `keys` up to `capacity`, the number to `*total`, as for query.
         * Triggers are not counted. */
        engine_status (*physics_overlap_sphere)(engine_handle world, const float* center, float radius,
                                                uint32_t layer_mask, uint64_t* keys, size_t capacity, size_t* total);
        engine_status (*physics_overlap_box)(engine_handle world, const float* center, const float* half_extents,
                                             uint32_t layer_mask, uint64_t* keys, size_t capacity, size_t* total);

        /* ---- Phase 25 Day 409: joints, characters, events and snapshots ---- */

        /* Reads the force (newtons) and torque (newton metres) a joint carried in the last step; the entity is the one
         * with ENGINE_COMPONENT_JOINT. Either pointer may be NULL. Fails with: STALE_HANDLE, INVALID_ARGUMENT,
         * TYPE_MISMATCH, NOT_FOUND (no joint made yet, or it broke). */
        engine_status (*joint_reaction)(engine_handle world, uint64_t key, float* force, float* torque);

        /* Sets the velocity a character (ENGINE_COMPONENT_CHARACTER) walks at (m/s, x y z; y is ignored): it holds
         * until set again, and each fixed step carries it out (the controller adds gravity, slides, climbs steps and
         * keeps it on the ground), so the distance walked in a second does not depend on the frame rate; a frame
         * without a step moves nothing. Since 2026-11-20 the move is held and carried out in the steps, no longer once
         * per call: a host that wants the character to stop sets a zero velocity. `dt` is checked and otherwise
         * unused. `state` (struct_size set by the host; may be NULL) receives the last step's state. Fails with:
         * STALE_HANDLE, INVALID_ARGUMENT (not finite, dt not in (0, 1]), STRUCT_SIZE, TYPE_MISMATCH, NOT_FOUND (no
         * character). */
        engine_status (*character_move)(engine_handle world, uint64_t key, const float* velocity, float dt,
                                        engine_character_state* state);

        /* Asks for a jump at an upward speed (m/s) when the character stands on the ground: it starts at the next
         * fixed step (and waits for one when a frame makes none); reads the state without moving when `speed` is 0.
         * Same failures as character_move. */
        engine_status (*character_jump)(engine_handle world, uint64_t key, float speed, engine_character_state* state);

        /* Copies the events of the last frame (every step of it, in step order; contacts, then triggers, then joint
         * breaks) to `out` up to `capacity`, and their number to `*total`. Plain structs; no callback is made. Valid
         * until the next frame, which replaces them. Fails with: STALE_HANDLE, INVALID_ARGUMENT, STRUCT_SIZE (an
         * element's struct_size is below the struct's), TYPE_MISMATCH. */
        engine_status (*physics_events)(engine_handle world, engine_physics_event* out, size_t capacity, size_t* total);

        /* Writes the whole state of a world's physics into `*out` (release it with engine_buffer_release), with its
         * characters' state (vertical speed, ground, held move): a host can rewind with physics_restore, also in
         * another process of the same build. Fails with: STALE_HANDLE,
         * INVALID_ARGUMENT, TYPE_MISMATCH. */
        engine_status (*physics_snapshot)(engine_handle world, engine_buffer* out);

        /* Puts a snapshot of the same world's physics back: bodies, velocities and contacts return to the state they
         * had, and the entities' transforms follow. The world is unchanged when the data is refused. Fails with:
         * STALE_HANDLE, INVALID_ARGUMENT, TYPE_MISMATCH, CORRUPT (not a snapshot, truncated or damaged),
         * UNSUPPORTED_VERSION, MISSING_DEPENDENCY (a shape it names is not in the world), LIMIT_EXCEEDED. */
        engine_status (*physics_restore)(engine_handle world, const void* data, size_t size);

        /* ---- Phase 24 Day 377: a material per mesh slot ---- */

        /* Makes slot `slot` (0-based, below 16: the primitives of an imported glTF mesh in the file's order) of the
         * entity's mesh draw with the material `material_resource`; 0 puts the slot back to the material of the
         * entity's engine_mesh_reference. The first slot set turns the entity's mesh from one draw into one draw per
         * slot of the mesh (a mesh without slots, or with more than 16, keeps drawing whole). A slot the mesh does not
         * have is ignored by the draw; a material that is not mounted leaves that slot undrawn and is counted in
         * `entities_unresolved`. engine_slot_override overrides apply to every slot. Fails with: STALE_HANDLE,
         * INVALID_ARGUMENT (`slot` is 16 or more), TYPE_MISMATCH. */
        engine_status (*entity_set_mesh_slot_material)(engine_handle world, uint64_t key, uint32_t slot,
                                                       uint64_t material_resource);

        /* The same, naming the slot by the name the importer gave it (the name of the primitive's material, else
         * `primitive<N>`), looked up in the entity's mesh, which must be mounted. Fails with: STALE_HANDLE,
         * INVALID_ARGUMENT (the entity has no mesh reference, an empty or too long name), NOT_FOUND (the mesh is not
         * mounted or has no such slot), TYPE_MISMATCH. */
        engine_status (*entity_set_mesh_slot_material_by_name)(engine_handle world, uint64_t key, engine_string slot,
                                                               uint64_t material_resource);

        /* Removes every slot material of the entity: its mesh draws whole again. Fails with: STALE_HANDLE,
         * INVALID_ARGUMENT, TYPE_MISMATCH. */
        engine_status (*entity_clear_mesh_slot_materials)(engine_handle world, uint64_t key);

        /* The newest finished frame of an off-screen surface, copied out of the GPU's read-back ring; never waits for
         * the GPU. A host that takes frames slower than they are drawn skips pictures (counted in
         * engine_surface_state). At most two frames are held at once. Fails with: STALE_HANDLE, INVALID_ARGUMENT,
         * STRUCT_SIZE, TYPE_MISMATCH, UNSUPPORTED (not an off-screen surface), NOT_READY (no frame has finished since
         * the last acquire), LIMIT_EXCEEDED (two frames are held: release one). */
        engine_status (*surface_acquire_frame)(engine_handle surface, engine_frame_image* out);

        /* Gives a frame back; `image->data` names it. Fails with: STALE_HANDLE, INVALID_ARGUMENT (not a frame held
         * from this surface), TYPE_MISMATCH. */
        engine_status (*surface_release_frame)(engine_handle surface, const engine_frame_image* image);

        /* Like entity_set_slot_override, for one mesh slot only: the override applies to the draws of the slot named
         * `mesh_slot` (the importer's name, as in entity_set_mesh_slot_material_by_name) and not to the entity's other
         * slots; it makes the entity's mesh draw one part per slot, as slot materials do. entity_clear_slot_override
         * removes the slot's overrides of every mesh slot. Fails with: those of entity_set_slot_override, and
         * INVALID_ARGUMENT (the entity has no mesh reference, an empty or too long name), NOT_FOUND (the mesh is not
         * mounted or has no such slot). */
        engine_status (*entity_set_mesh_slot_override)(engine_handle world, uint64_t key, engine_string mesh_slot,
                                                       engine_string slot, const engine_slot_override* value);
        /* ---- Phase 26 Day 438: audio ---- */

        /* Makes the engine's mixer (no device, no thread: audio is host driven). Fails with: STALE_HANDLE,
         * INVALID_ARGUMENT (a value out of range), STRUCT_SIZE, ALREADY_EXISTS (already open), UNSUPPORTED (a
         * sample_rate or channel count the mixer does not take). Any thread. */
        engine_status (*audio_open)(engine_handle engine, const engine_audio_info* info);

        /* Closes the mixer: clips, buses and voices are gone, and their ids are stale. A table call in flight on
         * another thread finishes first (it keeps the mixer alive); not while another thread is in audio_render. Fails
         * with: STALE_HANDLE, NOT_READY (not open). */
        engine_status (*audio_close)(engine_handle engine);

        /* Copies the PCM `size` bytes at `data` (a whole number of frames) into a clip. Fails with: STALE_HANDLE,
         * INVALID_ARGUMENT (NULL, a size that is not whole frames, bad loop points), STRUCT_SIZE, TOO_LARGE (above
         * 512 MiB), NOT_READY (not open), LIMIT_EXCEEDED (4096 clips). Any thread. */
        engine_status (*audio_clip_create)(engine_handle engine, const engine_audio_clip_info* info, const void* data,
                                           size_t size, uint64_t* out_clip);

        /* Destroys a clip. Voices that play it keep playing from their own share until they end. Fails with:
         * STALE_HANDLE (the engine, or a clip that is gone), NOT_READY. Any thread. */
        engine_status (*audio_clip_destroy)(engine_handle engine, uint64_t clip);

        /* Starts a voice; `*out_voice` is its id (generation-checked: a finished voice's id is stale). The voice
         * starts at the next block plus the engine's lead. Fails with: STALE_HANDLE (the clip or the engine),
         * INVALID_ARGUMENT, STRUCT_SIZE, NOT_READY, LIMIT_EXCEEDED (the pool is full of better voices, or the command
         * queue is full). Any thread. */
        engine_status (*audio_play)(engine_handle engine, const engine_audio_play* desc, uint64_t* out_voice);

        /* Changes the fields of a voice named by `params->mask`, in the order gain, pitch, pan, loop; a refusal leaves
         * the fields before it changed. Fails with: STALE_HANDLE (a finished voice), INVALID_ARGUMENT, STRUCT_SIZE,
         * NOT_READY, LIMIT_EXCEEDED. Any thread. */
        engine_status (*audio_voice_set)(engine_handle engine, uint64_t voice, const engine_audio_voice_params* params);

        /* Stops a voice after a fade of `fade_frames` frames (0: the engine's default ramp). Fails as audio_voice_set.
         * Any thread. */
        engine_status (*audio_voice_stop)(engine_handle engine, uint64_t voice, uint32_t fade_frames);

        /* Makes a bus (a name of 1 to 63 bytes) under `parent` (0: the master); `*out_bus` is its index. Fails with:
         * STALE_HANDLE, INVALID_ARGUMENT, NOT_READY, LIMIT_EXCEEDED (no bus slot left), NOT_FOUND (no such parent).
         * Any thread. */
        engine_status (*audio_bus_create)(engine_handle engine, engine_string name, uint32_t parent, uint32_t* out_bus);

        /* Changes a bus (0 is the master). Fails with: STALE_HANDLE, INVALID_ARGUMENT, STRUCT_SIZE, NOT_FOUND (no such
         * bus), NOT_READY, LIMIT_EXCEEDED. Any thread. */
        engine_status (*audio_bus_set)(engine_handle engine, uint32_t bus, const engine_audio_bus_params* params);

        /* Renders `frames` interleaved frames of the master into `out` (`frames * channels` floats): the host's audio
         * callback. It takes no lock, allocates nothing, never blocks and calls no host function, and may be called
         * from one thread at a time (the audio thread; every other audio function may be called while it runs). The
         * same call is the offline render. Any number of frames: the output does not depend on the size asked for.
         * Fails with: STALE_HANDLE, INVALID_ARGUMENT (NULL `out`), NOT_READY (not open). */
        engine_status (*audio_render)(engine_handle engine, uint32_t frames, float* out);

        /* Copies the events since the last call to `out` up to `capacity` and their number to `*total` (events past
         * the capacity are dropped). Plain structs; no callback is made. Fails with: STALE_HANDLE, INVALID_ARGUMENT,
         * STRUCT_SIZE, NOT_READY. Any thread, one at a time. */
        engine_status (*audio_events)(engine_handle engine, engine_audio_event* out, size_t capacity, size_t* total);

        /* Reads the state of the mixer (struct_size set by the host). Fails with: STALE_HANDLE, INVALID_ARGUMENT,
         * STRUCT_SIZE, NOT_READY. Any thread. */
        engine_status (*audio_state)(engine_handle engine, engine_audio_state* out);
        /* ---- Phase 26 Day 439: streams, music, effects and snapshots ---- */

        /* Opens a stream over a clip of audio_clip_create (streams play at pitch 1). Fails with: STALE_HANDLE,
         * INVALID_ARGUMENT, NOT_READY, LIMIT_EXCEEDED (256 streams). Any thread. */
        engine_status (*audio_stream_open_clip)(engine_handle engine, uint64_t clip, uint64_t* out_stream);

        /* Opens a stream over the host's own source; the first fill of its buffer runs inside this call, on the calling
         * thread (later fills on the engine's job threads). Non-finite samples the source returns are played as
         * silence. Fails as audio_stream_open_clip, and with STRUCT_SIZE. Any thread. */
        engine_status (*audio_stream_open)(engine_handle engine, const engine_audio_stream_source* source,
                                           uint64_t* out_stream);

        /* Closes a stream; a voice that plays it ends. The source's callbacks are not called after this returns. Fails
         * with: STALE_HANDLE, NOT_READY. Any thread. */
        engine_status (*audio_stream_close)(engine_handle engine, uint64_t stream);

        /* Starts a voice that plays a stream (`desc->clip` is ignored). Fails as audio_play. Any thread. */
        engine_status (*audio_stream_play)(engine_handle engine, uint64_t stream, const engine_audio_play* desc,
                                           uint64_t* out_voice);

        /* The host's per-frame call (not from the audio thread): refills the streams that run low, on the engine's job
         * threads, and posts the music commands that are due. With `wait` 1 it returns when the refills are done (the
         * offline mode: the same samples whatever the timing). Fails with: STALE_HANDLE, NOT_READY. Any thread, one at
         * a time. */
        engine_status (*audio_update)(engine_handle engine, uint32_t wait);

        /* Music: plays a stream on `bus`, fading in over `fade_frames`; `tempo` may be NULL (120 bpm, 4 beats). A track
         * that plays is cross-faded out by audio_music_crossfade. The music plays on the bus of the first call; another
         * bus later is INVALID_ARGUMENT. Fails with: STALE_HANDLE, INVALID_ARGUMENT,
         * STRUCT_SIZE, NOT_READY, NOT_FOUND (no such bus), LIMIT_EXCEEDED. Any thread. */
        engine_status (*audio_music_play)(engine_handle engine, uint64_t stream, uint32_t bus, uint32_t fade_frames,
                                          const engine_audio_tempo* tempo);

        /* Replaces the playing track by another with an equal-power cross-fade of `fade_frames`, now or (`at_bar` 1) at
         * the next bar of the playing track. Fails as audio_music_play. Any thread. */
        engine_status (*audio_music_crossfade)(engine_handle engine, uint64_t stream, uint32_t fade_frames,
                                               uint32_t at_bar, const engine_audio_tempo* tempo);

        /* Stops the music after a fade-out. Fails with: STALE_HANDLE, NOT_READY. Any thread. */
        engine_status (*audio_music_stop)(engine_handle engine, uint32_t fade_frames);

        /* Plays `count` (1 to 8) streams of one length as layers started on one frame, each at its gain. Fails as
         * audio_music_play. */
        engine_status (*audio_music_layers)(engine_handle engine, const uint64_t* streams, const float* gains,
                                            uint32_t count, uint32_t bus);

        /* Moves one layer's gain over `frames`. Fails with: STALE_HANDLE, INVALID_ARGUMENT, NOT_READY. */
        engine_status (*audio_music_layer_gain)(engine_handle engine, uint32_t layer, float gain, uint32_t frames);

        /* Plays a clip over the music, on the next beat when `on_beat` is 1; `*out_frame` (may be NULL) receives the
         * mixer frame it sounds at. Fails with: STALE_HANDLE, NOT_READY, LIMIT_EXCEEDED. */
        engine_status (*audio_music_stinger)(engine_handle engine, uint64_t clip, float gain, uint32_t on_beat,
                                             uint64_t* out_frame);

        /* Sets (or, with kind NONE, removes) the effect in insert slot `slot` (0 to 3) of a bus. Fails with:
         * STALE_HANDLE, INVALID_ARGUMENT (a number out of range), STRUCT_SIZE, NOT_FOUND, NOT_READY, LIMIT_EXCEEDED
         * (the mixer makes at most 64 effects over its life: a call that only changes the numbers of the effect of the
         * same kind in the slot, or a reverb of the same size, retunes it and uses none). */
        engine_status (*audio_bus_effect_set)(engine_handle engine, uint32_t bus, uint32_t slot,
                                              const engine_audio_effect* effect);

        /* Sends a bus to an aux bus (slot 0 to 3) at `gain`; ducks `target` by `depth` while `source` is louder than
         * `threshold` (linear), releasing over `release_frames`. Fails with: STALE_HANDLE, INVALID_ARGUMENT,
         * NOT_FOUND, NOT_READY, LIMIT_EXCEEDED. */
        engine_status (*audio_bus_send)(engine_handle engine, uint32_t bus, uint32_t slot, uint32_t aux, float gain,
                                        uint32_t pre_fader);
        engine_status (*audio_bus_duck)(engine_handle engine, uint32_t target, uint32_t source, float depth,
                                        float threshold, float release_frames);

        /* Writes the state of the mixer (voices, buses, effects) into `*out` (release it with engine_buffer_release).
         * The mixer must have applied what it was sent: render a block first. Fails with: STALE_HANDLE,
         * INVALID_ARGUMENT, NOT_READY (not open, or commands are queued). Not while another thread is in audio_render.
         */
        engine_status (*audio_snapshot)(engine_handle engine, engine_buffer* out);

        /* Puts a snapshot back into a mixer set up the same way, with the clips and streams it named still open.
         * Unchanged when refused. Fails with: STALE_HANDLE, INVALID_ARGUMENT, NOT_READY, CORRUPT, UNSUPPORTED_VERSION,
         * MISSING_DEPENDENCY (a clip or stream is not open), UNSUPPORTED (the mixer is set up differently). Not while
         * another thread is in audio_render (it replaces the state that call reads). */
        engine_status (*audio_restore)(engine_handle engine, const void* data, size_t size);

        /* ---- Maintenance (2026-11-18): a host's own cache of imported resources ----
         *
         * A host that imports a file often (the same worlds and avatars again) can keep what import_gltf made and add
         * it back later instead of importing again: after an import it stores, for each id of `engine_import_result`,
         * the id, the type, the path (one per line of `paths`) and the blob that resource_read returns, together with
         * the `manifest`; it keys the cache by the file's own hash and by import_format_version(). To use the cache it
         * adds the stored blobs with resource_add_many, giving each its id and path again (the order of `ids` is
         * dependencies first), and loads the level by its id (the level is the resource made with
         * ENGINE_IMPORT_EMIT_LEVEL; store which id it is) with resource_load. Everything a load needs is in the blobs
         * and the paths, plus `summary` (the node positions, the bounds, the material and animation names, the lights:
         * nothing in the engine reads them back from a loaded level), which the host keeps in its own index with the
         * ids, the types, the paths and the level's id. */

        /* Copies the complete blob of a resource of the memory backend into `*out` (release it with
         * engine_buffer_release): byte for byte what resource_add takes, so that adding it again gives a resource with
         * the same container, type, id and content hash (resource_mounted reports the size and the hash before and
         * after). Only resources of the memory backend can be read (what resource_add and the imports put there): a
         * resource of a pak or of a file is NOT_FOUND. `max_size` (0: no limit) refuses a blob above it with TOO_LARGE
         * before any byte is copied. No blob is larger than the setting `assets.memory_backend_bytes` (the backend's
         * budget), and the blobs of one import together are at most `assets.import.max_output_bytes` (default 2 GiB),
         * so that a host can bound its cache by the file. `*out` is zeroed on failure. Fails with: STALE_HANDLE,
         * INVALID_ARGUMENT (NULL, a zero id), NOT_FOUND, TOO_LARGE, INTERNAL (memory ran out). Any thread. */
        engine_status (*resource_read)(engine_handle engine, uint64_t id, uint64_t max_size, engine_buffer* out);

        /* Adds `count` resources to the memory backend, all or none: every blob is checked (as resource_add checks
         * it), every id and path found free (also among the items: one id or path twice is ALREADY_EXISTS) and the sum
         * of the sizes found to fit the budget before the first is installed, and the resources become visible
         * together, in the given order. The bytes are always copied: ENGINE_RESOURCE_BORROW is INVALID_ARGUMENT here
         * (flags: ENGINE_RESOURCE_CHECK_HASH). A resource may name a dependency that comes later in the array or is
         * not there at all: adding does not look at dependencies, resource_load does (MISSING_DEPENDENCY). On failure
         * nothing was added, and `*failed_index` (may be NULL) is the index of the item that was refused, or `count`
         * when the batch as a whole was refused (the budget). Up to 65536 items. Fails with: the errors of
         * resource_add (for the first item that has one), LIMIT_EXCEEDED (the budget, or too many items). A `count`
         * of 0 does nothing. */
        engine_status (*resource_add_many)(engine_handle engine, const engine_resource_add_info* infos, size_t count,
                                           size_t* failed_index);

        /* The same on an engine worker, so that a frame loop goes on: returns a ticket at once. The checks of the
         * arguments (struct sizes, flags, ids, NULL bytes) are made before it returns and fail the call. The engine
         * copies the structs and the paths but reads the bytes in place: they must stay valid and unchanged until
         * resource_add_finish or resource_add_destroy returns for the ticket. The ticket counts as an object of the
         * engine until then. Fails with: the argument errors of resource_add_many, LIMIT_EXCEEDED (too many tickets or
         * items), INTERNAL. Needs ENGINE_CAP_RESOURCE_CACHE. */
        engine_status (*resource_add_many_async)(engine_handle engine, const engine_resource_add_info* infos,
                                                 size_t count, engine_handle* ticket);

        /* Waits until the adding is done, at most `timeout_ms` (0 only looks; UINT64_MAX waits for ever). Not from
         * inside a job. Returns OK when done, NOT_READY on a timeout. Fails with: STALE_HANDLE, INVALID_ARGUMENT,
         * TYPE_MISMATCH, NOT_READY. */
        engine_status (*resource_add_wait)(engine_handle ticket, uint64_t timeout_ms);

        /* Takes the outcome of a finished adding and destroys the ticket: OK (all added) or the error resource_add_many
         * would have returned, with `*failed_index` (may be NULL) as there. Fails with: NOT_READY (not done: the
         * ticket stays), STALE_HANDLE, INVALID_ARGUMENT, TYPE_MISMATCH, and the adding's own errors. */
        engine_status (*resource_add_finish)(engine_handle ticket, size_t* failed_index);

        /* Destroys a ticket without taking its outcome, waiting until the adding is done (whether the resources were
         * added is then read with resource_mounted). Fails with: STALE_HANDLE, INVALID_ARGUMENT, TYPE_MISMATCH,
         * IN_USE (another call is waiting on it). */
        engine_status (*resource_add_destroy)(engine_handle ticket);

        /* A fingerprint of what imports write: the same value in every build of the same sources, a different one
         * whenever a re-import of one file can give other blobs (the importer's output version, the blob container
         * version and the version of every resource type), so that a host keys its cache of imported blobs by it. It
         * needs no engine (callable before engine_create, from any thread) and never fails. A library built without
         * the importer folds in the formats only (the cache of a build with an importer is not valid for it and back:
         * check ENGINE_CAP_IMPORT_GLTF). Treat it as an opaque number: compare for equality, store it as a number. */
        uint64_t (*import_format_version)(void);

        /* ---- Live voices (maintenance, 2026-11-18; ENGINE_CAP_AUDIO_PUSH_STREAMS) ---- */

        /* Opens a push stream: a live stream the host feeds with audio_stream_push, played by a voice
         * (audio_stream_play, or the `stream` of an audio source, which places it in the world). It never ends: a voice
         * that finds the ring empty plays silence for the rest of the block (counted as an underrun) and goes on with
         * the next frames. Fails with: STALE_HANDLE, NOT_READY (audio is not open), INVALID_ARGUMENT (channels,
         * capacity, prefill, or a sample_rate other than the mixer's), STRUCT_SIZE, LIMIT_EXCEEDED (256 streams). Any
         * thread. */
        engine_status (*audio_stream_open_push)(engine_handle engine, const engine_audio_push_info* info,
                                                uint64_t* out_stream);

        /* Appends `frames` interleaved float frames (non-finite samples become silence) to a push stream and, when
         * `accepted` is not NULL, tells how many were taken (all, unless another thread was pushing to the same stream
         * at that moment: one thread at a time per stream). A full ring drops its oldest frames, counted in
         * audio_stream_stats. It takes no lock the audio thread takes and allocates nothing; the one lock it takes is
         * the engine's control lock, for the lookup of the stream (a control call that runs long, such as the first
         * fill of audio_stream_open, makes it wait): call it from the thread the audio arrives on. Fails with:
         * STALE_HANDLE, NOT_READY, INVALID_ARGUMENT (NULL data, a stream that is not a push stream). Any thread. */
        engine_status (*audio_stream_push)(engine_handle engine, uint64_t stream, const float* data, uint32_t frames,
                                           uint32_t* accepted);

        /* As audio_stream_push, with 16-bit samples (divided by 32768). */
        engine_status (*audio_stream_push_i16)(engine_handle engine, uint64_t stream, const int16_t* data,
                                               uint32_t frames, uint32_t* accepted);

        /* Reads the counters of a stream (a push stream's; for another only buffered_frames and underruns). Fails
         * with: STALE_HANDLE, NOT_READY, INVALID_ARGUMENT, STRUCT_SIZE. Any thread. */
        engine_status (*audio_stream_stats)(engine_handle engine, uint64_t stream, engine_audio_stream_stats* out);

        /* ---- Scripts (Phase 27 Day 471; ENGINE_CAP_SCRIPT) ---- */

        /* Adds a script to the world: every state of the world loads it, in the order of the calls, and it may define
         * behaviors and global functions. The world's script runtime is made by the first call (host functions are
         * registered before it). `*out_script` (may be NULL) receives the number of the script, from 1. A script that
         * fails to compile or run does not fail this call: its error is in the log and in script_stats.errors at the
         * next frame or call. Fails with: STALE_HANDLE, INVALID_ARGUMENT (NULL, an empty source, flags), STRUCT_SIZE,
         * TOO_LARGE (a source above 16 MiB), LIMIT_EXCEEDED (1024 scripts), UNSUPPORTED (no ENGINE_CAP_SCRIPT).
         * Main thread; not from a host function. */
        engine_status (*script_load)(engine_handle world, const engine_script_load* info, uint64_t* out_script);

        /* Calls a global function of the world's scripts (`name` or `table.name`) with `args` (may be NULL: no
         * arguments) and writes what it returns into `results` (may be NULL to drop them). It runs at once on the
         * calling thread, in one state of the world (state a call leaves is not shared with the behaviors' instances:
         * a global a script changes is seen by the calls and callbacks of that state only). Refused with NOT_READY, not
         * waited for, when a frame or another world call is running on another thread: a host calls it from the thread
         * that runs the frame or between frames. Fails with: STALE_HANDLE, INVALID_ARGUMENT (NULL, a bad pool),
         * STRUCT_SIZE, NOT_FOUND (no such function), NOT_READY (busy, or no script was loaded), TOO_LARGE (`results`
         * has no room: its counts say how much), LIMIT_EXCEEDED (the budget: instructions, time or memory), INTERNAL
         * (the script raised an error: the last-error text has the message), UNSUPPORTED. */
        engine_status (*script_call)(engine_handle world, engine_string function, const engine_value_pool* args,
                                     engine_value_pool* results);

        /* Makes `name` (`namespace.function`, lower case letters, digits and `_`) callable from the world's scripts,
         * taking `arity` arguments. Register before the first script_load of the world: a state made earlier would not
         * have it (NOT_READY). `flags` is 0 or ENGINE_SCRIPT_FUNCTION_PURE. See engine_script_fn for the thread rule.
         * Fails with: STALE_HANDLE, INVALID_ARGUMENT (NULL, a bad name, flags, arity above 16), ALREADY_EXISTS (the
         * name, or the namespace is one of Lua's), NOT_READY, UNSUPPORTED. */
        engine_status (*script_register_function)(engine_handle world, engine_string name, uint32_t arity,
                                                  uint32_t flags, engine_script_fn function, void* user);

        /* Sets the limits of script_call, and of the frame's callbacks when the world's runtime does not exist yet.
         * Fails with: STALE_HANDLE, INVALID_ARGUMENT, STRUCT_SIZE, UNSUPPORTED. */
        engine_status (*script_set_budget)(engine_handle world, const engine_script_budget* budget);

        /* Reads what the world's scripts have done. Fails with: STALE_HANDLE, INVALID_ARGUMENT, STRUCT_SIZE,
         * UNSUPPORTED. */
        engine_status (*script_stats)(engine_handle world, engine_script_stats* out);

        /* Emits the event `name` (1 to 64 bytes) with the first root of `payload` (NULL: no payload) from the host.
         * It is delivered at the start of the world's next script tick to the scripts that listen (`target` 0: every
         * subscriber; else only the entity with that id) and to the host's subscribers. Fails with: STALE_HANDLE,
         * INVALID_ARGUMENT (a bad name, a bad pool), STRUCT_SIZE, NOT_READY (busy, or no script was loaded),
         * UNSUPPORTED. */
        engine_status (*script_event_emit)(engine_handle world, engine_string name, const engine_value_pool* payload,
                                           uint64_t target);

        /* Calls `callback` for the events named `name` (empty: every event) that scripts or the host emit, at the
         * start of each script tick, on the frame's thread. `*out_id` receives an id for script_event_unsubscribe.
         * `user` must outlive the subscription. Fails with: STALE_HANDLE, INVALID_ARGUMENT, LIMIT_EXCEEDED (256
         * subscriptions), UNSUPPORTED. */
        engine_status (*script_event_subscribe)(engine_handle world, engine_string name,
                                                engine_script_event_fn callback, void* user, uint32_t* out_id);

        /* Removes a subscription. Fails with: STALE_HANDLE, NOT_FOUND, UNSUPPORTED. */
        engine_status (*script_event_unsubscribe)(engine_handle world, uint32_t id);

        /* Runs a pure task now, on the engine's task states (the same states and rules as `task.run`, `task.map` and
         * `task.parallel_for`: only functions registered with ENGINE_SCRIPT_FUNCTION_PURE exist there), and fills
         * `results` with its result (one root). The result does not depend on the number of threads. It runs on the
         * calling thread (a host thread is not a job-system participant) and holds the world. Fails with:
         * STALE_HANDLE, INVALID_ARGUMENT (a bad task or pool), STRUCT_SIZE, NOT_READY (busy), TOO_LARGE (`results`
         * has no room), LIMIT_EXCEEDED (the budget), INTERNAL (the task failed: the last-error text says why),
         * UNSUPPORTED. */
        engine_status (*script_task_run)(engine_handle world, const engine_script_task* task,
                                         engine_value_pool* results);

        /* ---- Terrain (Phase 28) ---- */

        /* Makes a height map resource from 16-bit samples the host holds and adds it to the memory backend under `id`
         * (not 0): `samples` is `width * height` values (2 to 8192 a side, rows from z = 0, sample value s is the
         * height range_min + s / 65535 * (range_max - range_min)), `cell_size` the metres between samples (above 0).
         * The engine keeps the blob (resource_read returns it), so a surface rebuilds the terrain from it after a
         * rebind; the host's samples are copied. Fails with: STALE_HANDLE, INVALID_ARGUMENT (NULL, a size, a range
         * or cell size the format refuses), ALREADY_EXISTS (the id), LIMIT_EXCEEDED (the backend's budget). Any thread.
         */
        engine_status (*height_map_create)(engine_handle engine, uint64_t id, const uint16_t* samples, uint32_t width,
                                           uint32_t height, float range_min, float range_max, float cell_size);

        /* Reads the ground height at the world point (`x`, `z`) into `*out_height`: the surface of the first terrain
         * entity (by entity key) whose map covers the point, at the entity's current world placement (the one the last
         * frame computed; before any frame, its local transform), the same surface the collider and the renderer use
         * (two triangles a cell). Fails with: STALE_HANDLE, INVALID_ARGUMENT (NULL, a non-finite coordinate),
         * NOT_FOUND (no terrain covers the point, or its height map is not mounted). */
        engine_status (*terrain_height)(engine_handle world, float x, float z, float* out_height);
    } engine_api;

    /* Returns the function table.
     *
     * `wanted_major`/`wanted_minor`: the version the host was written against (ENGINE_ABI_VERSION_MAJOR/MINOR of the
     * header it compiled with). `table_size`: sizeof(engine_api) as the host compiled it. The library always returns
     * its whole table (static, the same pointer every time); the host reads only the fields below min(table_size,
     * table->struct_size), the first being what it knows and the second what the library has. `out`: receives a pointer
     * to the static table; set to NULL on failure. Fails with: INVALID_ARGUMENT (`out` NULL or `table_size` below the
     * table's first fields), UNSUPPORTED_VERSION (another major or a newer minor). Thread-safe; callable before and
     * after any engine exists. */
    ENGINE_C_API engine_status engine_get_api(uint32_t wanted_major, uint32_t wanted_minor, size_t table_size,
                                              const engine_api** out);

#ifdef __cplusplus
}
#endif

#endif
