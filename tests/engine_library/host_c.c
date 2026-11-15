/* Phase 23 Day 346: a C program as a foreign host. It links the C API statically (Day 347 loads it at run time) and
   uses the engine through `engine_get_api` only: it creates an engine with settings, a command line and a log callback,
   reads and changes a setting, queries the version, destroys the engine, and makes every bad call the API documents:
   each must fail with the documented status and never crash. */
#include <stdio.h>
#include <string.h>

#include "engine_c/engine_c.h"
#include "engine_c/engine_test_extension.h"

static int g_failures = 0;
static const engine_api* g_api = NULL; /* for the failure report: what the engine said went wrong */
static void ReportLastError(void);

#define CHECK(condition)                                                                                               \
    do                                                                                                                 \
    {                                                                                                                  \
        if (!(condition))                                                                                              \
        {                                                                                                              \
            printf("host_c: line %d: CHECK failed: %s\n", __LINE__, #condition);                                       \
            ++g_failures;                                                                                              \
        }                                                                                                              \
    } while (0)

#define CHECK_STATUS(call, expected)                                                                                   \
    do                                                                                                                 \
    {                                                                                                                  \
        const engine_status got_ = (call);                                                                             \
        if (got_ != (expected))                                                                                        \
        {                                                                                                              \
            printf("host_c: line %d: %s returned %d, expected %d\n", __LINE__, #call, (int)got_, (int)(expected));     \
            ReportLastError();                                                                                         \
            ++g_failures;                                                                                              \
        }                                                                                                              \
    } while (0)

static engine_string S(const char* text)
{
    engine_string s;
    s.data = text;
    s.length = strlen(text);
    return s;
}

static int StringIs(engine_string s, const char* text)
{
    return s.length == strlen(text) && (s.length == 0 || memcmp(s.data, text, s.length) == 0);
}

static int BufferIs(const engine_buffer* b, const char* text)
{
    return b->size == strlen(text) && (b->size == 0 || memcmp(b->data, text, b->size) == 0);
}

/* What the log callback saw. */
typedef struct log_record
{
    int calls;
    engine_log_level level;
    char channel[64];
    char message[128];
} log_record;

static void OnLog(void* user, engine_log_level level, engine_string channel, engine_string message)
{
    log_record* record = (log_record*)user;
    ++record->calls;
    record->level = level;
    snprintf(record->channel, sizeof(record->channel), "%.*s", (int)channel.length, channel.data);
    snprintf(record->message, sizeof(record->message), "%.*s", (int)message.length, message.data);
}

static engine_create_info BaseInfo(void)
{
    engine_create_info info;
    memset(&info, 0, sizeof(info));
    info.struct_size = sizeof(info);
    return info;
}

static char* LastError(const engine_api* api, char* out, size_t capacity)
{
    engine_buffer buffer;
    memset(&buffer, 0, sizeof(buffer));
    if (api->last_error(&buffer) == ENGINE_OK)
    {
        snprintf(out, capacity, "%.*s", (int)buffer.size, buffer.data);
    }
    else
    {
        out[0] = '\0';
    }
    engine_buffer_release(&buffer);
    return out;
}

static void ReportLastError(void)
{
    char text[256];
    if (g_api != NULL)
    {
        printf("host_c:   last error: %s\n", LastError(g_api, text, sizeof(text)));
    }
}

static void TestNegotiation(void)
{
    const engine_api* api = (const engine_api*)1;
    CHECK_STATUS(engine_get_api(ENGINE_ABI_VERSION_MAJOR + 1, 0, sizeof(engine_api), &api),
                 ENGINE_ERROR_UNSUPPORTED_VERSION);
    CHECK(api == NULL);
    api = (const engine_api*)1;
    CHECK_STATUS(engine_get_api(ENGINE_ABI_VERSION_MAJOR, ENGINE_ABI_VERSION_MINOR + 1, sizeof(engine_api), &api),
                 ENGINE_ERROR_UNSUPPORTED_VERSION);
    CHECK(api == NULL);
    CHECK_STATUS(engine_get_api(ENGINE_ABI_VERSION_MAJOR, ENGINE_ABI_VERSION_MINOR, sizeof(engine_api), NULL),
                 ENGINE_ERROR_INVALID_ARGUMENT);
    api = (const engine_api*)1;
    CHECK_STATUS(engine_get_api(ENGINE_ABI_VERSION_MAJOR, ENGINE_ABI_VERSION_MINOR, 4, &api),
                 ENGINE_ERROR_INVALID_ARGUMENT);
    CHECK(api == NULL);

    CHECK_STATUS(engine_get_api(ENGINE_ABI_VERSION_MAJOR, ENGINE_ABI_VERSION_MINOR, sizeof(engine_api), &api),
                 ENGINE_OK);
    CHECK(api != NULL);
    if (api == NULL)
    {
        return;
    }
    CHECK(api->struct_size == sizeof(engine_api));
    CHECK(api->abi_major == ENGINE_ABI_VERSION_MAJOR);
    CHECK(api->abi_minor == ENGINE_ABI_VERSION_MINOR);
    CHECK(api->sanitizer <= ENGINE_LIB_SANITIZER_THREAD);

    /* A host that knows less of the table gets as much as it asked for, and the table says how much that is. */
    const engine_api* older = NULL;
    const size_t header_only = offsetof(engine_api, status_name);
    CHECK_STATUS(engine_get_api(ENGINE_ABI_VERSION_MAJOR, 0, header_only, &older), ENGINE_OK);
    CHECK(older == api); /* the table is static: the same pointer */
    /* The library does not shorten its own table by a smaller request: a host reads struct_size of what it was given.
     */
    CHECK(older != NULL && older->struct_size >= header_only);
}

static void TestStatusNames(const engine_api* api)
{
    CHECK(StringIs(api->status_name(ENGINE_OK), "ok"));
    CHECK(StringIs(api->status_name(ENGINE_ERROR_STALE_HANDLE), "stale_handle"));
    CHECK(StringIs(api->status_name(ENGINE_ERROR_INTERNAL), "internal"));
    CHECK(StringIs(api->status_name((engine_status)(ENGINE_ERROR_INTERNAL + 1)), "unknown"));
    CHECK(StringIs(api->status_name((engine_status)-1), "unknown"));
}

static void TestCreateErrors(const engine_api* api)
{
    char text[256];
    engine_handle handle;
    handle.value = 77;

    CHECK_STATUS(api->create(NULL, &handle), ENGINE_ERROR_INVALID_ARGUMENT);
    CHECK(handle.value == 0);
    engine_create_info info = BaseInfo();
    CHECK_STATUS(api->create(&info, NULL), ENGINE_ERROR_INVALID_ARGUMENT);

    info.struct_size = 4;
    CHECK_STATUS(api->create(&info, &handle), ENGINE_ERROR_STRUCT_SIZE);
    CHECK(strstr(LastError(api, text, sizeof(text)), "struct_size") != NULL);

    info = BaseInfo();
    info.flags = 0x80000000u;
    CHECK_STATUS(api->create(&info, &handle), ENGINE_ERROR_INVALID_ARGUMENT);

    info = BaseInfo();
    info.setting_count = 1; /* a count with no list */
    CHECK_STATUS(api->create(&info, &handle), ENGINE_ERROR_INVALID_ARGUMENT);

    engine_setting_entry null_string = {{NULL, 4}, S("1")};
    info = BaseInfo();
    info.settings = &null_string;
    info.setting_count = 1;
    CHECK_STATUS(api->create(&info, &handle), ENGINE_ERROR_INVALID_ARGUMENT);
    engine_string null_arg = {NULL, 2};
    info = BaseInfo();
    info.args = &null_arg;
    info.arg_count = 1;
    CHECK_STATUS(api->create(&info, &handle), ENGINE_ERROR_INVALID_ARGUMENT);

    engine_setting_entry unknown = {S("no.such.setting"), S("1")};
    info = BaseInfo();
    info.settings = &unknown;
    info.setting_count = 1;
    CHECK_STATUS(api->create(&info, &handle), ENGINE_ERROR_NOT_FOUND);
    CHECK(handle.value == 0);
    CHECK(strstr(LastError(api, text, sizeof(text)), "no.such.setting") != NULL);

    engine_setting_entry bad_value = {S("rhi.pipeline_cache"), S("sometimes")};
    info.settings = &bad_value;
    CHECK_STATUS(api->create(&info, &handle), ENGINE_ERROR_INVALID_ARGUMENT);

    engine_string bad_args[] = {S("host"), S("--set"), S("no.such.setting=1")};
    info = BaseInfo();
    info.args = bad_args;
    info.arg_count = 3;
    CHECK_STATUS(api->create(&info, &handle), ENGINE_ERROR_INVALID_ARGUMENT);
    CHECK(handle.value == 0);
}

static void TestLifecycle(const engine_api* api)
{
    char text[256];
    engine_handle first;
    engine_handle second;
    engine_buffer value;
    memset(&value, 0, sizeof(value));

    /* Create with a host setting, a command line and a log callback. */
    engine_setting_entry entries[] = {{S("rhi.pipeline_cache"), S("true")}, {S("platform.window_backend"), S("x11")}};
    engine_string args[] = {S("host_c"), S("--set"), S("platform.window_backend=wayland")};
    log_record record;
    memset(&record, 0, sizeof(record));
    engine_create_info info = BaseInfo();
    info.settings = entries;
    info.setting_count = 2;
    info.args = args;
    info.arg_count = 3;
    info.flags = ENGINE_CREATE_LOG_SUPPRESS_CONSOLE;
    info.log = &OnLog;
    info.log_user = &record;
    CHECK_STATUS(api->create(&info, &first), ENGINE_OK);
    CHECK(first.value != 0);

    /* One engine per process. */
    engine_handle other;
    other.value = 5;
    CHECK_STATUS(api->create(&info, &other), ENGINE_ERROR_LIMIT_EXCEEDED);
    CHECK(other.value == 0);

    /* The host's entry is the value; the command line outranks a host entry. */
    CHECK_STATUS(api->setting_get(first, S("rhi.pipeline_cache"), &value), ENGINE_OK);
    CHECK(BufferIs(&value, "true"));
    engine_buffer_release(&value);
    CHECK(value.data == NULL && value.size == 0 && value.release == NULL);
    engine_buffer_release(&value); /* releasing twice is safe */
    CHECK_STATUS(api->setting_get(first, S("platform.window_backend"), &value), ENGINE_OK);
    CHECK(BufferIs(&value, "wayland"));
    engine_buffer_release(&value);

    /* A change by the host's own call. */
    CHECK_STATUS(api->setting_set(first, S("rhi.pipeline_cache"), S("false")), ENGINE_OK);
    CHECK_STATUS(api->setting_get(first, S("rhi.pipeline_cache"), &value), ENGINE_OK);
    CHECK(BufferIs(&value, "false"));
    engine_buffer_release(&value);
    CHECK_STATUS(api->setting_set(first, S("rhi.pipeline_cache"), S("sometimes")), ENGINE_ERROR_INVALID_ARGUMENT);
    CHECK_STATUS(api->setting_set(first, S("no.such.setting"), S("1")), ENGINE_ERROR_NOT_FOUND);
    CHECK(strstr(LastError(api, text, sizeof(text)), "no.such.setting") != NULL);
    CHECK_STATUS(api->setting_get(first, S("no.such.setting"), &value), ENGINE_ERROR_NOT_FOUND);
    CHECK(value.release == NULL);
    engine_string null_with_length = {NULL, 3};
    CHECK_STATUS(api->setting_set(first, null_with_length, S("1")), ENGINE_ERROR_INVALID_ARGUMENT);
    CHECK_STATUS(api->setting_get(first, S("rhi.pipeline_cache"), NULL), ENGINE_ERROR_INVALID_ARGUMENT);

    /* The engine reports what it holds; an out struct is checked for its size. */
    engine_info report;
    memset(&report, 0, sizeof(report));
    report.struct_size = sizeof(report);
    CHECK_STATUS(api->get_info(first, &report), ENGINE_OK);
    CHECK(report.struct_size == sizeof(engine_info) && report.setting_count > 0);
    report.struct_size = 2;
    CHECK_STATUS(api->get_info(first, &report), ENGINE_ERROR_STRUCT_SIZE);
    report.struct_size = sizeof(report);
    CHECK_STATUS(api->get_info(first, NULL), ENGINE_ERROR_INVALID_ARGUMENT);

    /* The log callback: a line the engine emits arrives with its channel and level, and not on the console (the flag).
     */
    const struct engine_test_extension* state = NULL;
    {
        const engine_string name = S("engine.test_state");
        state = (const struct engine_test_extension*)api->get_extension(name);
    }
    if (state != NULL)
    {
        state->log_message("a line from the engine");
        CHECK(record.calls == 1);
        CHECK(record.level == ENGINE_LOG_WARN);
        CHECK(strcmp(record.message, "a line from the engine") == 0);
        CHECK(record.channel[0] != '\0');
    }
    else
    {
        printf("host_c: the library has no test extension: the log callback is not checked\n");
    }
    CHECK(api->get_extension(S("engine.no_such_extension")) == NULL);

    /* Destroy: the handle is stale from now on, and no callback runs. */
    CHECK_STATUS(api->destroy(first), ENGINE_OK);
    CHECK_STATUS(api->destroy(first), ENGINE_ERROR_STALE_HANDLE);
    CHECK_STATUS(api->get_info(first, &report), ENGINE_ERROR_STALE_HANDLE);
    CHECK_STATUS(api->setting_set(first, S("rhi.pipeline_cache"), S("true")), ENGINE_ERROR_STALE_HANDLE);
    CHECK_STATUS(api->setting_get(first, S("rhi.pipeline_cache"), &value), ENGINE_ERROR_STALE_HANDLE);
    CHECK(value.release == NULL);
    if (state != NULL)
    {
        state->log_message("nobody is listening");
        CHECK(record.calls == 1);
    }

    /* A handle that was never issued, and the zero handle: refused, not mistaken for a stale one. */
    engine_handle never;
    never.value = ((uint64_t)999 << 32) | 1;
    CHECK_STATUS(api->destroy(never), ENGINE_ERROR_INVALID_ARGUMENT);
    never.value = 0;
    CHECK_STATUS(api->destroy(never), ENGINE_ERROR_INVALID_ARGUMENT);
    never.value = ((uint64_t)first.value >> 32 << 32) | 2; /* a slot that does not exist */
    CHECK_STATUS(api->destroy(never), ENGINE_ERROR_INVALID_ARGUMENT);

    /* A new engine gets a new generation: the old handle stays stale, and starts from the declared defaults (the host's
     * entries were not kept). */
    CHECK_STATUS(api->create(&info, &second), ENGINE_OK);
    CHECK(second.value != first.value);
    CHECK_STATUS(api->get_info(first, &report), ENGINE_ERROR_STALE_HANDLE);
    CHECK_STATUS(api->get_info(second, &report), ENGINE_OK);
    CHECK_STATUS(api->destroy(second), ENGINE_OK);
}

int main(void)
{
    const engine_api* api = NULL;
    TestNegotiation();
    if (engine_get_api(ENGINE_ABI_VERSION_MAJOR, ENGINE_ABI_VERSION_MINOR, sizeof(engine_api), &api) != ENGINE_OK ||
        api == NULL)
    {
        printf("host_c: no function table\n");
        return 2;
    }
    g_api = api;
    TestStatusNames(api);
    TestCreateErrors(api);
    TestLifecycle(api);
    api->thread_detach(); /* this thread is done with the engine */
    CHECK(api->thread_detach != NULL);
    if (g_failures != 0)
    {
        printf("host_c: %d checks failed\n", g_failures);
        return 1;
    }
    printf("host_c: ok (ABI %u.%u)\n", api->abi_major, api->abi_minor);
    return 0;
}
