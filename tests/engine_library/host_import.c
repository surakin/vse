/* Phase 23 Day 351: a C program imports a glTF from memory through the function table (the the host path), the way a
   host in another language would: the file's bytes in, resources in the engine's content out. It imports a fixture
   synchronously and asynchronously, with its external files given by a read_file callback, loads every mesh, skeleton,
   clip, texture and material it made (by id and by path), removes them and imports again with the manifest to see that
   the ids are kept, cancels imports, and feeds the importer hostile files (truncated, mutated, wrapped in a lying GLB
   container, over the limits): each must come back as a status and a message, never a crash (under asan, nothing is
   read or written out of bounds). */
#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "host_os.h"
#include "engine/engine_c.h"
#if IMPORT_DYNAMIC
#include "engine/engine_loader.h"
#endif

static int g_failures = 0;
static const engine_api* g_api = NULL;

#define CHECK(condition)                                                                                               \
    do                                                                                                                 \
    {                                                                                                                  \
        if (!(condition))                                                                                              \
        {                                                                                                              \
            printf("host_import: line %d: CHECK failed: %s\n", __LINE__, #condition);                                  \
            ++g_failures;                                                                                              \
        }                                                                                                              \
    } while (0)

static void ReportStatus(int line, const char* call, engine_status got, int expected)
{
    const engine_string name = g_api->status_name(got);
    printf("host_import: line %d: %s returned %d (%.*s), expected %d\n", line, call, (int)got, (int)name.length,
           name.data, expected);
    ++g_failures;
}

#define CHECK_STATUS(call, expected)                                                                                   \
    do                                                                                                                 \
    {                                                                                                                  \
        const engine_status got_ = (call);                                                                             \
        if (got_ != (expected))                                                                                        \
        {                                                                                                              \
            ReportStatus(__LINE__, #call, got_, (int)(expected));                                                      \
        }                                                                                                              \
    } while (0)

static engine_string S(const char* text)
{
    engine_string s;
    s.data = text;
    s.length = strlen(text);
    return s;
}

static engine_string NoPath(void)
{
    engine_string s;
    s.data = NULL;
    s.length = 0;
    return s;
}

static engine_handle NoHandle(void)
{
    engine_handle h;
    memset(&h, 0, sizeof(h));
    return h;
}

/* ---- the fixture and the files made from it */

typedef struct Bytes
{
    unsigned char* data;
    size_t size;
} Bytes;

static Bytes ReadWholeFile(const char* path)
{
    Bytes b = {NULL, 0};
    FILE* f = fopen(path, "rb");
    if (f == NULL)
    {
        printf("host_import: cannot open %s\n", path);
        ++g_failures;
        return b;
    }
    fseek(f, 0, SEEK_END);
    b.size = (size_t)ftell(f);
    fseek(f, 0, SEEK_SET);
    b.data = (unsigned char*)malloc(b.size + 1);
    if (fread(b.data, 1, b.size, f) != b.size)
    {
        ++g_failures;
    }
    b.data[b.size] = 0;
    fclose(f);
    return b;
}

static int Base64Value(int c)
{
    if (c >= 'A' && c <= 'Z')
    {
        return c - 'A';
    }
    if (c >= 'a' && c <= 'z')
    {
        return c - 'a' + 26;
    }
    if (c >= '0' && c <= '9')
    {
        return c - '0' + 52;
    }
    return c == '+' ? 62 : (c == '/' ? 63 : -1);
}

static Bytes Base64Decode(const char* text, size_t length)
{
    Bytes out;
    size_t i;
    unsigned int accumulator = 0;
    int bits = 0;
    out.data = (unsigned char*)malloc(length);
    out.size = 0;
    for (i = 0; i < length; ++i)
    {
        const int v = Base64Value((unsigned char)text[i]);
        if (v < 0)
        {
            continue;
        }
        accumulator = (accumulator << 6) | (unsigned)v;
        bits += 6;
        if (bits >= 8)
        {
            bits -= 8;
            out.data[out.size++] = (unsigned char)((accumulator >> bits) & 0xFF);
        }
    }
    return out;
}

/* The external files of the rewritten glTF: the fixture's two data URIs turned into files the host serves. */
#define MAX_FILES 4
static struct
{
    char name[32];
    Bytes bytes;
    int released;
} g_files[MAX_FILES];
static int g_file_count = 0;
static int g_read_calls = 0;
static int g_releases = 0;

/* Replaces every `"uri": "data:...base64,XXXX"` of `json` by `"uri": "extN.bin"` (or .png) and keeps the decoded bytes
 * as files. */
static Bytes MakeExternal(const Bytes* json)
{
    const char* key = "\"uri\": \"data:";
    Bytes out;
    const char* p = (const char*)json->data;
    const char* end = p + json->size;
    out.data = (unsigned char*)malloc(json->size + 256);
    out.size = 0;
    while (p < end)
    {
        const char* hit = strstr(p, key);
        if (hit == NULL)
        {
            memcpy(out.data + out.size, p, (size_t)(end - p));
            out.size += (size_t)(end - p);
            break;
        }
        {
            const char* mime = hit + strlen(key);
            const char* comma = strchr(mime, ',');
            const char* quote = comma != NULL ? strchr(comma, '"') : NULL;
            const int is_png = strncmp(mime, "image/png", 9) == 0;
            int n;
            if (comma == NULL || quote == NULL || g_file_count >= MAX_FILES)
            {
                ++g_failures;
                return out;
            }
            memcpy(out.data + out.size, p, (size_t)(hit - p));
            out.size += (size_t)(hit - p);
            snprintf(g_files[g_file_count].name, sizeof(g_files[0].name), "ext%d.%s", g_file_count,
                     is_png ? "png" : "bin");
            g_files[g_file_count].bytes = Base64Decode(comma + 1, (size_t)(quote - comma - 1));
            n = snprintf((char*)out.data + out.size, json->size + 256 - out.size, "\"uri\": \"%s\"",
                         g_files[g_file_count].name); /* the data URI it replaces was longer: it fits */
            out.size += (size_t)n;
            ++g_file_count;
            p = quote + 1;
        }
    }
    out.data[out.size] = 0;
    return out;
}

static engine_status ReadExternal(void* user, engine_string name, uint64_t max_bytes, engine_import_file* out)
{
    int i;
    (void)user;
    ++g_read_calls;
    for (i = 0; i < g_file_count; ++i)
    {
        if (strlen(g_files[i].name) == name.length && memcmp(g_files[i].name, name.data, name.length) == 0)
        {
            unsigned char* copy = (unsigned char*)malloc(g_files[i].bytes.size);
            if (g_files[i].bytes.size > max_bytes)
            {
                free(copy);
                return ENGINE_ERROR_TOO_LARGE;
            }
            memcpy(copy, g_files[i].bytes.data, g_files[i].bytes.size);
            out->data = copy;
            out->size = g_files[i].bytes.size;
            out->release = NULL;
            return ENGINE_OK;
        }
    }
    return ENGINE_ERROR_NOT_FOUND;
}

static void ReleaseExternal(void* release_user, const void* data, size_t size)
{
    (void)release_user;
    (void)size;
    ++g_releases;
    free((void*)data);
}

/* The same, with the release set (the callback above is written so that the release function can be named once). */
static engine_status ReadExternalWithRelease(void* user, engine_string name, uint64_t max_bytes,
                                             engine_import_file* out)
{
    const engine_status status = ReadExternal(user, name, max_bytes, out);
    if (status == ENGINE_OK)
    {
        out->release = &ReleaseExternal;
        out->release_user = NULL;
    }
    return status;
}

static engine_status RefuseExternal(void* user, engine_string name, uint64_t max_bytes, engine_import_file* out)
{
    (void)user;
    (void)name;
    (void)max_bytes;
    (void)out;
    return ENGINE_ERROR_NOT_FOUND;
}

/* A GLB container around the JSON (no binary chunk: the buffer and the image are data: URIs). */
static void PutU32(unsigned char* p, uint32_t v)
{
    p[0] = (unsigned char)(v & 0xFF);
    p[1] = (unsigned char)((v >> 8) & 0xFF);
    p[2] = (unsigned char)((v >> 16) & 0xFF);
    p[3] = (unsigned char)((v >> 24) & 0xFF);
}

static Bytes MakeGlb(const Bytes* json)
{
    const size_t padded = (json->size + 3) & ~(size_t)3;
    Bytes out;
    out.size = 12 + 8 + padded;
    out.data = (unsigned char*)malloc(out.size);
    PutU32(out.data, 0x46546C67u);
    PutU32(out.data + 4, 2);
    PutU32(out.data + 8, (uint32_t)out.size);
    PutU32(out.data + 12, (uint32_t)padded);
    PutU32(out.data + 16, 0x4E4F534Au);
    memcpy(out.data + 20, json->data, json->size);
    memset(out.data + 20 + json->size, ' ', padded - json->size);
    return out;
}

/* ---- the engine and what an import made */

static engine_handle NewEngine(void)
{
    engine_create_info info;
    engine_handle engine;
    memset(&info, 0, sizeof(info));
    info.struct_size = sizeof(info);
    info.flags = ENGINE_CREATE_LOG_SUPPRESS_CONSOLE;
    memset(&engine, 0, sizeof(engine));
    CHECK_STATUS(g_api->create(&info, &engine), ENGINE_OK);
    return engine;
}

static engine_import_options Options(const char* prefix)
{
    engine_import_options o;
    memset(&o, 0, sizeof(o));
    o.struct_size = sizeof(o);
    o.id_seed = 12345;
    if (prefix != NULL)
    {
        o.path_prefix = S(prefix);
    }
    return o;
}

static uint64_t IdAt(const engine_import_result* r, uint32_t i)
{
    uint64_t v;
    memcpy(&v, r->ids.data + i * sizeof(v), sizeof(v));
    return v;
}

static uint32_t TypeAt(const engine_import_result* r, uint32_t i)
{
    uint32_t v;
    memcpy(&v, r->types.data + i * sizeof(v), sizeof(v));
    return v;
}

static engine_import_result NewResult(void)
{
    engine_import_result r;
    memset(&r, 0, sizeof(r));
    r.struct_size = sizeof(r);
    return r;
}

static void ReleaseResult(engine_import_result* r)
{
    engine_buffer_release(&r->ids);
    engine_buffer_release(&r->types);
    engine_buffer_release(&r->paths);
    engine_buffer_release(&r->warnings);
    engine_buffer_release(&r->manifest);
}

/* Loads every resource of a result by its id, then each by its path, and releases them. Counts the types. */
static void LoadAll(engine_handle engine, const engine_import_result* r, int counts[6])
{
    uint32_t i;
    const char* line = r->paths.data;
    memset(counts, 0, 6 * sizeof(int));
    for (i = 0; i < r->resource_count; ++i)
    {
        engine_handle h = NoHandle();
        engine_handle by_path = NoHandle();
        const uint32_t type = TypeAt(r, i);
        const char* newline = memchr(line, '\n', (size_t)(r->paths.data + r->paths.size - line));
        engine_string path;
        engine_resource_info info;
        CHECK(newline != NULL);
        path.data = line;
        path.length = newline != NULL ? (size_t)(newline - line) : 0;
        line = newline != NULL ? newline + 1 : line;
        CHECK_STATUS(g_api->resource_load(engine, IdAt(r, i), NoPath(), type, &h), ENGINE_OK);
        CHECK_STATUS(g_api->resource_load(engine, 0, path, type, &by_path), ENGINE_OK);
        memset(&info, 0, sizeof(info));
        info.struct_size = sizeof(info);
        CHECK_STATUS(g_api->resource_status(h, &info), ENGINE_OK);
        CHECK(info.id == IdAt(r, i) && info.loaded == 1);
        CHECK_STATUS(g_api->resource_release(h), ENGINE_OK);
        CHECK_STATUS(g_api->resource_release(by_path), ENGINE_OK);
        counts[type == ENGINE_RESOURCE_MESH        ? 0
               : type == ENGINE_RESOURCE_SKELETON  ? 1
               : type == ENGINE_RESOURCE_ANIMATION ? 2
               : type == ENGINE_RESOURCE_TEXTURE   ? 3
               : type == ENGINE_RESOURCE_MATERIAL  ? 4
                                                   : 5]++;
    }
}

static void RemoveAll(engine_handle engine, const engine_import_result* r)
{
    uint32_t i;
    for (i = 0; i < r->resource_count; ++i)
    {
        engine_buffer dependents;
        memset(&dependents, 0, sizeof(dependents));
        CHECK_STATUS(g_api->resource_remove(engine, IdAt(r, i), ENGINE_RESOURCE_FORCE, &dependents), ENGINE_OK);
        engine_buffer_release(&dependents);
    }
}

static int SameIds(const engine_import_result* a, const engine_import_result* b)
{
    return a->resource_count == b->resource_count && a->ids.size == b->ids.size &&
           memcmp(a->ids.data, b->ids.data, a->ids.size) == 0;
}

static void LastErrorIsNotEmpty(void)
{
    engine_buffer text;
    memset(&text, 0, sizeof(text));
    CHECK_STATUS(g_api->last_error(&text), ENGINE_OK);
    CHECK(text.size > 0);
    engine_buffer_release(&text);
}

/* ---- the tests */

static void TestInfo(engine_handle engine)
{
    engine_info info;
    memset(&info, 0, sizeof(info));
    info.struct_size = sizeof(info);
    CHECK_STATUS(g_api->get_info(engine, &info), ENGINE_OK);
    CHECK((info.caps & ENGINE_CAP_IMPORT_GLTF) != 0);
    CHECK(info.struct_size == sizeof(info));
    memset(&info, 0, sizeof(info));
    info.struct_size = 8; /* the first version of the struct: no caps */
    CHECK_STATUS(g_api->get_info(engine, &info), ENGINE_OK);
    CHECK(info.struct_size == 8 && info.caps == 0);
}

/* The synchronous import: counts, loading, the second import refused, remove then import with the manifest. */
static void TestSync(engine_handle engine, const Bytes* gltf)
{
    engine_import_options options = Options("/room");
    engine_import_result first = NewResult();
    engine_import_result again = NewResult();
    engine_import_result kept = NewResult();
    int counts[6];
    CHECK_STATUS(g_api->import_gltf(engine, gltf->data, gltf->size, &options, &first), ENGINE_OK);
    CHECK(first.resource_count == 5 && first.ids.size == 5 * sizeof(uint64_t) &&
          first.types.size == 5 * sizeof(uint32_t));
    CHECK(first.manifest.size > 0 && first.paths.size > 0);
    LoadAll(engine, &first, counts);
    CHECK(counts[0] == 1 && counts[1] == 1 && counts[2] == 1 && counts[3] == 1 && counts[4] == 1);
    printf("host_import: sync import: %u resources, %u warnings, %zu bytes of manifest\n", first.resource_count,
           first.warning_count, first.manifest.size);

    /* The same file again: the ids (the same seed) and the paths are taken. Nothing changes. */
    CHECK_STATUS(g_api->import_gltf(engine, gltf->data, gltf->size, &options, &again), ENGINE_ERROR_ALREADY_EXISTS);
    CHECK(again.resource_count == 0 && again.ids.size == 0 && again.manifest.size == 0);
    LastErrorIsNotEmpty();
    LoadAll(engine, &first, counts); /* all still there */

    /* Remove, then import with the manifest and another seed: the ids are the first import's. */
    RemoveAll(engine, &first);
    options.id_seed = 99999;
    options.manifest.data = first.manifest.data;
    options.manifest.length = first.manifest.size;
    CHECK_STATUS(g_api->import_gltf(engine, gltf->data, gltf->size, &options, &kept), ENGINE_OK);
    CHECK(SameIds(&first, &kept));
    LoadAll(engine, &kept, counts);
    RemoveAll(engine, &kept);

    /* Without the manifest and with another seed the ids are new. */
    options.manifest = NoPath();
    CHECK_STATUS(g_api->import_gltf(engine, gltf->data, gltf->size, &options, &again), ENGINE_OK);
    CHECK(!SameIds(&first, &again));
    RemoveAll(engine, &again);

    /* Scale and flags are accepted. */
    options.flags = ENGINE_IMPORT_FLAT_NORMALS | ENGINE_IMPORT_DROP_MORPH_TARGETS;
    options.scale = 2.0f;
    options.clip_rate = 24.0f;
    ReleaseResult(&again);
    CHECK_STATUS(g_api->import_gltf(engine, gltf->data, gltf->size, &options, &again), ENGINE_OK);
    RemoveAll(engine, &again);
    ReleaseResult(&first);
    ReleaseResult(&again);
    ReleaseResult(&kept);
}

/* External files through read_file: the buffer and the image are served by the host, and each file is released once. */
static void TestExternal(engine_handle engine, const Bytes* gltf)
{
    Bytes external;
    engine_import_options options = Options("/ext");
    engine_import_result r = NewResult();
    engine_import_result refused = NewResult();
    int counts[6];
    g_file_count = 0;
    external = MakeExternal(gltf);
    CHECK(g_file_count == 2);
    options.read_file = &ReadExternalWithRelease;
    g_read_calls = 0;
    g_releases = 0;
    CHECK_STATUS(g_api->import_gltf(engine, external.data, external.size, &options, &r), ENGINE_OK);
    CHECK(r.resource_count == 5);
    CHECK(g_read_calls == 2 && g_releases == 2);
    LoadAll(engine, &r, counts);
    CHECK(counts[0] == 1 && counts[3] == 1);
    RemoveAll(engine, &r);
    ReleaseResult(&r);

    /* No callback, or one that finds nothing: the import fails and says why; nothing is added. */
    options = Options("/ext2");
    CHECK_STATUS(g_api->import_gltf(engine, external.data, external.size, &options, &refused), ENGINE_ERROR_NOT_FOUND);
    LastErrorIsNotEmpty();
    options.read_file = &RefuseExternal;
    CHECK_STATUS(g_api->import_gltf(engine, external.data, external.size, &options, &refused), ENGINE_ERROR_NOT_FOUND);
    /* A limit below the size of the buffer file: the host's callback is told the limit and refuses. */
    options.read_file = &ReadExternalWithRelease;
    options.max_buffer_bytes = 16;
    g_releases = 0;
    CHECK(g_api->import_gltf(engine, external.data, external.size, &options, &refused) != ENGINE_OK);
    LastErrorIsNotEmpty();
    CHECK(refused.resource_count == 0);
    {
        engine_handle h = NoHandle();
        CHECK_STATUS(g_api->resource_load(engine, 0, S("/ext2/meshes/quad"), ENGINE_RESOURCE_MESH, &h),
                     ENGINE_ERROR_NOT_FOUND);
    }
    free(external.data);
}

/* The asynchronous import: poll, wait, finish once, destroy; cancel; destroy while it runs. */
static void TestAsync(engine_handle engine, const Bytes* gltf)
{
    engine_import_options options = Options("/async");
    engine_handle ticket = NoHandle();
    engine_import_info info;
    engine_import_result r = NewResult();
    int counts[6];
    int i;
    uint32_t last_steps = 0;
    CHECK_STATUS(g_api->import_gltf_async(engine, gltf->data, gltf->size, &options, &ticket), ENGINE_OK);
    CHECK(ticket.value != 0);
    do
    {
        memset(&info, 0, sizeof(info));
        info.struct_size = sizeof(info);
        CHECK_STATUS(g_api->import_poll(ticket, &info), ENGINE_OK);
        CHECK(info.steps_done <= info.steps_total && info.steps_total > 0 && info.steps_done >= last_steps);
        last_steps = info.steps_done;
        if (!info.done)
        {
            const engine_status waited = g_api->import_wait(ticket, 1);
            CHECK(waited == ENGINE_OK || waited == ENGINE_ERROR_NOT_READY);
        }
    } while (!info.done);
    CHECK_STATUS(g_api->import_wait(ticket, 10000), ENGINE_OK);
    memset(&info, 0, sizeof(info));
    info.struct_size = sizeof(info);
    CHECK_STATUS(g_api->import_poll(ticket, &info), ENGINE_OK);
    CHECK(info.done == 1 && info.steps_done == info.steps_total);
    CHECK_STATUS(g_api->import_finish(ticket, &r), ENGINE_OK);
    CHECK(r.resource_count == 5);
    LoadAll(engine, &r, counts);
    CHECK(counts[0] == 1 && counts[1] == 1 && counts[2] == 1);
    {
        engine_import_result second = NewResult();
        CHECK_STATUS(g_api->import_finish(ticket, &second), ENGINE_ERROR_NOT_FOUND);
    }
    /* The engine refuses to be destroyed while the ticket exists. */
    CHECK_STATUS(g_api->destroy(engine), ENGINE_ERROR_IN_USE);
    CHECK_STATUS(g_api->import_destroy(ticket), ENGINE_OK);
    CHECK_STATUS(g_api->import_destroy(ticket), ENGINE_ERROR_STALE_HANDLE);
    CHECK_STATUS(g_api->import_poll(ticket, &info), ENGINE_ERROR_STALE_HANDLE);
    RemoveAll(engine, &r);
    ReleaseResult(&r);

    /* Cancelling: the import ends as CANCELLED, or it had finished already (then the host takes its result). Each round
     * has its own prefix. */
    for (i = 0; i < 24; ++i)
    {
        char prefix[32];
        engine_import_result c = NewResult();
        engine_status status;
        snprintf(prefix, sizeof(prefix), "/cancel%d", i);
        options = Options(prefix);
        options.id_seed = (uint64_t)(1000 + i);
        CHECK_STATUS(g_api->import_gltf_async(engine, gltf->data, gltf->size, &options, &ticket), ENGINE_OK);
        if (i % 2 == 0)
        {
            CHECK_STATUS(g_api->import_cancel(ticket), ENGINE_OK);
        }
        CHECK_STATUS(g_api->import_wait(ticket, 10000), ENGINE_OK);
        status = g_api->import_finish(ticket, &c);
        CHECK(status == ENGINE_OK || status == ENGINE_ERROR_CANCELLED);
        if (status == ENGINE_OK)
        {
            RemoveAll(engine, &c);
        }
        else
        {
            CHECK(c.resource_count == 0 && c.ids.size == 0);
        }
        ReleaseResult(&c);
        CHECK_STATUS(g_api->import_destroy(ticket), ENGINE_OK);
    }
    /* At most 256 tickets exist at once: the 257th is refused, and a destroyed ticket makes room. */
    {
        static engine_handle tickets[256];
        engine_handle extra = NoHandle();
        for (i = 0; i < 256; ++i)
        {
            options = Options("/many");
            options.id_seed = (uint64_t)(3000 + i);
            CHECK_STATUS(g_api->import_gltf_async(engine, gltf->data, gltf->size, &options, &tickets[i]), ENGINE_OK);
            CHECK_STATUS(g_api->import_cancel(tickets[i]), ENGINE_OK);
        }
        CHECK_STATUS(g_api->import_gltf_async(engine, gltf->data, gltf->size, &options, &extra),
                     ENGINE_ERROR_LIMIT_EXCEEDED);
        CHECK(extra.value == 0);
        CHECK_STATUS(g_api->import_destroy(tickets[100]), ENGINE_OK);
        CHECK_STATUS(g_api->import_gltf_async(engine, gltf->data, gltf->size, &options, &extra), ENGINE_OK);
        CHECK_STATUS(g_api->import_destroy(extra), ENGINE_OK);
        for (i = 0; i < 256; ++i)
        {
            if (i != 100)
            {
                CHECK_STATUS(g_api->import_destroy(tickets[i]), ENGINE_OK);
            }
        }
    }
    /* Destroying a ticket whose import is still running cancels it and waits (no crash, nothing left). */
    for (i = 0; i < 8; ++i)
    {
        options = Options("/gone");
        options.id_seed = (uint64_t)(2000 + i);
        CHECK_STATUS(g_api->import_gltf_async(engine, gltf->data, gltf->size, &options, &ticket), ENGINE_OK);
        CHECK_STATUS(g_api->import_destroy(ticket), ENGINE_OK);
    }
    {
        engine_import_result unfinished = NewResult();
        engine_handle t2 = NoHandle();
        options = Options("/early");
        CHECK_STATUS(g_api->import_gltf_async(engine, gltf->data, gltf->size, &options, &t2), ENGINE_OK);
        {
            engine_status s = g_api->import_finish(t2, &unfinished);
            CHECK(s == ENGINE_OK || s == ENGINE_ERROR_NOT_READY);
            if (s == ENGINE_OK)
            {
                RemoveAll(engine, &unfinished);
            }
        }
        ReleaseResult(&unfinished);
        CHECK_STATUS(g_api->import_destroy(t2), ENGINE_OK);
    }
}

/* Wrong arguments of every call. */
static void TestArguments(engine_handle engine, const Bytes* gltf)
{
    engine_import_options options = Options("/args");
    engine_import_result r = NewResult();
    engine_handle ticket = NoHandle();
    engine_import_info info;
    engine_handle resource = NoHandle();
    CHECK_STATUS(g_api->import_gltf(engine, NULL, 10, &options, &r), ENGINE_ERROR_INVALID_ARGUMENT);
    CHECK_STATUS(g_api->import_gltf(engine, gltf->data, 0, &options, &r), ENGINE_ERROR_INVALID_ARGUMENT);
    CHECK_STATUS(g_api->import_gltf(engine, gltf->data, gltf->size, &options, NULL), ENGINE_ERROR_INVALID_ARGUMENT);
    CHECK_STATUS(g_api->import_gltf(NoHandle(), gltf->data, gltf->size, &options, &r), ENGINE_ERROR_INVALID_ARGUMENT);
    r.struct_size = 4;
    CHECK_STATUS(g_api->import_gltf(engine, gltf->data, gltf->size, &options, &r), ENGINE_ERROR_STRUCT_SIZE);
    r = NewResult();
    options.struct_size = 8;
    CHECK_STATUS(g_api->import_gltf(engine, gltf->data, gltf->size, &options, &r), ENGINE_ERROR_STRUCT_SIZE);
    {
        /* A struct of the smallest size the host may claim, in memory of exactly that size: the engine reads no byte
         * past it (asan). */
        void* raw = malloc(8);
        const engine_import_options* tiny = (const engine_import_options*)raw;
        memset(raw, 0, 8);
        *(uint32_t*)raw = 8;
        CHECK_STATUS(g_api->import_gltf(engine, gltf->data, gltf->size, tiny, &r), ENGINE_ERROR_STRUCT_SIZE);
        CHECK_STATUS(g_api->import_gltf_async(engine, gltf->data, gltf->size, tiny, &ticket), ENGINE_ERROR_STRUCT_SIZE);
        free(raw);
    }
    {
        /* Odd prefixes are refused or accepted, never a crash. */
        static const char* prefixes[] = {"/", "/..", "/a/../b", "/a//b", "/a b", "/\xff\xfe", "/a/", "//"};
        size_t k;
        for (k = 0; k < sizeof(prefixes) / sizeof(prefixes[0]); ++k)
        {
            engine_import_result pr = NewResult();
            options = Options(prefixes[k]);
            if (g_api->import_gltf(engine, gltf->data, gltf->size, &options, &pr) == ENGINE_OK)
            {
                RemoveAll(engine, &pr);
            }
            ReleaseResult(&pr);
        }
    }
    options = Options("/args");
    options.flags = 0x80;
    CHECK_STATUS(g_api->import_gltf(engine, gltf->data, gltf->size, &options, &r), ENGINE_ERROR_INVALID_ARGUMENT);
    options = Options("/args");
    options.scale = NAN;
    CHECK_STATUS(g_api->import_gltf(engine, gltf->data, gltf->size, &options, &r), ENGINE_ERROR_INVALID_ARGUMENT);
    options.scale = -1.0f;
    CHECK_STATUS(g_api->import_gltf(engine, gltf->data, gltf->size, &options, &r), ENGINE_ERROR_INVALID_ARGUMENT);
    options = Options("no-slash");
    CHECK_STATUS(g_api->import_gltf(engine, gltf->data, gltf->size, &options, &r), ENGINE_ERROR_INVALID_ARGUMENT);
    options = Options("/args");
    options.manifest = S("this is not a manifest");
    CHECK_STATUS(g_api->import_gltf(engine, gltf->data, gltf->size, &options, &r), ENGINE_ERROR_INVALID_ARGUMENT);
    LastErrorIsNotEmpty();
    /* A NULL options pointer takes every default (path /gltf). */
    CHECK_STATUS(g_api->import_gltf(engine, gltf->data, gltf->size, NULL, &r), ENGINE_OK);
    CHECK(r.resource_count == 5);
    RemoveAll(engine, &r);
    ReleaseResult(&r);

    /* Tickets: bad arguments and handles of other kinds. */
    CHECK_STATUS(g_api->import_gltf_async(engine, gltf->data, gltf->size, &options, NULL),
                 ENGINE_ERROR_INVALID_ARGUMENT);
    CHECK_STATUS(g_api->import_gltf_async(engine, NULL, 5, &options, &ticket), ENGINE_ERROR_INVALID_ARGUMENT);
    memset(&info, 0, sizeof(info));
    info.struct_size = sizeof(info);
    CHECK_STATUS(g_api->import_poll(NoHandle(), &info), ENGINE_ERROR_INVALID_ARGUMENT);
    {
        engine_handle bogus;
        bogus.value = ((uint64_t)5 << 56) | ((uint64_t)1 << 24) |
                      1u; /* an import ticket of a generation that was never issued */
        CHECK(g_api->import_poll(bogus, &info) != ENGINE_OK);
    }
    /* A handle of another kind given to an import call is a TYPE_MISMATCH (a resource handle, a load ticket). */
    options = Options("/args");
    CHECK_STATUS(g_api->import_gltf(engine, gltf->data, gltf->size, &options, &r), ENGINE_OK);
    CHECK_STATUS(g_api->resource_load(engine, IdAt(&r, 0), NoPath(), TypeAt(&r, 0), &resource), ENGINE_OK);
    CHECK_STATUS(g_api->import_poll(resource, &info), ENGINE_ERROR_TYPE_MISMATCH);
    CHECK_STATUS(g_api->import_cancel(resource), ENGINE_ERROR_TYPE_MISMATCH);
    CHECK_STATUS(g_api->import_destroy(resource), ENGINE_ERROR_TYPE_MISMATCH);
    {
        engine_handle load_ticket = NoHandle();
        CHECK_STATUS(
            g_api->resource_load_async(engine, IdAt(&r, 0), NoPath(), TypeAt(&r, 0), ENGINE_LOAD_NORMAL, &load_ticket),
            ENGINE_OK);
        CHECK_STATUS(g_api->import_wait(load_ticket, 10), ENGINE_ERROR_TYPE_MISMATCH);
        CHECK_STATUS(g_api->ticket_destroy(load_ticket), ENGINE_OK);
    }
    CHECK_STATUS(g_api->resource_release(resource), ENGINE_OK);
    RemoveAll(engine, &r);
    ReleaseResult(&r);
}

/* Hostile files. Every call returns; a refusal has a message; the engine is usable afterwards. */
static int g_hostile_refused = 0;
static int g_hostile_accepted = 0;

static void ImportHostile(engine_handle engine, const unsigned char* data, size_t size, int round)
{
    engine_import_options options;
    engine_import_result r = NewResult();
    char prefix[32];
    engine_status status;
    snprintf(prefix, sizeof(prefix), "/h%d", round);
    options = Options(prefix);
    options.id_seed = (uint64_t)(round + 1);
    status = g_api->import_gltf(engine, data, size, &options, &r);
    if (status == ENGINE_OK)
    {
        ++g_hostile_accepted;
        RemoveAll(engine, &r);
    }
    else
    {
        ++g_hostile_refused;
        CHECK(r.resource_count == 0 && r.ids.size == 0 && r.manifest.size == 0);
        LastErrorIsNotEmpty();
    }
    ReleaseResult(&r);
}

/* The bytes of the image's PNG in the fixture's JSON. ThreadSanitizer cannot follow libpng's longjmp (KNOWN-ISSUES.md),
   so its flavor of this test leaves a corrupt PNG alone: a mutation that falls inside is moved to the byte before it.
 */
static size_t g_png_begin = 0;
static size_t g_png_end = 0;

static size_t AvoidPng(size_t at, size_t shift)
{
#if HOST_IMPORT_TSAN
    if (at >= g_png_begin + shift && at < g_png_end + shift)
    {
        return g_png_begin + shift - 1;
    }
#else
    (void)shift;
#endif
    return at;
}

static void TestHostile(engine_handle engine, const Bytes* gltf)
{
    Bytes glb = MakeGlb(gltf);
    {
        const char* marker = strstr((const char*)gltf->data, "image/png;base64,");
        const char* quote = marker != NULL ? strchr(marker, '"') : NULL;
        if (marker != NULL && quote != NULL)
        {
            g_png_begin = (size_t)(marker - (const char*)gltf->data);
            g_png_end = (size_t)(quote - (const char*)gltf->data);
        }
        CHECK(g_png_end > g_png_begin);
    }
    unsigned char* copy = (unsigned char*)malloc(glb.size > gltf->size ? glb.size : gltf->size);
    size_t i;
    int round = 0;
    engine_import_options options;
    engine_import_result r = NewResult();
    /* The whole glb imports. */
    options = Options("/glb");
    CHECK_STATUS(g_api->import_gltf(engine, glb.data, glb.size, &options, &r), ENGINE_OK);
    RemoveAll(engine, &r);
    ReleaseResult(&r);
    /* Truncated at 64 places, as .gltf text and as glb. */
    for (i = 1; i < 64; ++i)
    {
        ImportHostile(engine, gltf->data, gltf->size * i / 64, round++);
        ImportHostile(engine, glb.data, glb.size * i / 64, round++);
    }
    /* A byte changed at 150 places of each. */
    for (i = 0; i < 150; ++i)
    {
        size_t at = (gltf->size * i) / 150 + (i % 7);
        memcpy(copy, gltf->data, gltf->size);
        at = AvoidPng(at < gltf->size ? at : gltf->size - 1, 0);
        copy[at] = (unsigned char)("~{}[]\"0\xff"[i % 8]);
        ImportHostile(engine, copy, gltf->size, round++);
        at = 12 + (glb.size - 12) * i / 150;
        memcpy(copy, glb.data, glb.size);
        at = AvoidPng(at < glb.size ? at : glb.size - 1, 20);
        copy[at] = (unsigned char)("~{}[]\"0\xff"[i % 8]);
        ImportHostile(engine, copy, glb.size, round++);
    }
    /* A glb whose header lies: the total length, the chunk length, the magic, the version, the chunk type. */
    {
        static const uint32_t lies[] = {0u, 1u, 19u, 20u, 0x7fffffffu, 0xffffffffu};
        size_t j;
        for (j = 0; j < sizeof(lies) / sizeof(lies[0]); ++j)
        {
            memcpy(copy, glb.data, glb.size);
            PutU32(copy + 8, lies[j]);
            ImportHostile(engine, copy, glb.size, round++);
            memcpy(copy, glb.data, glb.size);
            PutU32(copy + 12, lies[j]);
            ImportHostile(engine, copy, glb.size, round++);
        }
        memcpy(copy, glb.data, glb.size);
        PutU32(copy, 0x12345678u);
        ImportHostile(engine, copy, glb.size, round++);
        memcpy(copy, glb.data, glb.size);
        PutU32(copy + 4, 7);
        ImportHostile(engine, copy, glb.size, round++);
        memcpy(copy, glb.data, glb.size);
        PutU32(copy + 16, 0x004E4942u);
        ImportHostile(engine, copy, glb.size, round++);
    }
    /* Other shapes: one byte, no JSON, deep nesting, a huge number, a page of zeros. */
    {
        static const char* texts[] = {
            "{",
            "[]",
            "null",
            "{\"asset\":{\"version\":\"2.0\"}}",
            "{\"asset\":{\"version\":\"1.0\"}}",
            "{\"asset\":{\"version\":\"2.0\"},\"accessors\":[{\"count\":99999999999999999999}]}",
            "{\"asset\":{\"version\":\"2.0\"},\"meshes\":[{\"primitives\":[{\"attributes\":{\"POSITION\":7}}]}]}"};
        size_t j;
        char* deep = (char*)malloc(100001);
        unsigned char zeros[4096];
        for (j = 0; j < sizeof(texts) / sizeof(texts[0]); ++j)
        {
            ImportHostile(engine, (const unsigned char*)texts[j], strlen(texts[j]), round++);
        }
        memset(deep, '[', 100000);
        ImportHostile(engine, (const unsigned char*)deep, 100000, round++);
        free(deep);
        memset(zeros, 0, sizeof(zeros));
        ImportHostile(engine, zeros, sizeof(zeros), round++);
    }
    /* Over the limits: the same good file with a limit set below what it needs. */
    {
        struct
        {
            const char* name;
            engine_import_options o;
        } cases[8];
        size_t j;
        for (j = 0; j < 8; ++j)
        {
            cases[j].o = Options("/limit");
            cases[j].o.id_seed = (uint64_t)(500 + j);
        }
        cases[0].name = "max_file_bytes";
        cases[0].o.max_file_bytes = 1024;
        cases[1].name = "max_json_bytes";
        cases[1].o.max_json_bytes = 1024;
        cases[2].name = "max_buffer_bytes";
        cases[2].o.max_buffer_bytes = 16;
        cases[3].name = "max_output_bytes";
        cases[3].o.max_output_bytes = 1024;
        cases[4].name = "max_pixels (the image of the fixture)";
        cases[4].o.max_pixels = 1;
        cases[5].name = "max_nodes";
        cases[5].o.max_nodes = 1;
        cases[6].name = "max_accessors";
        cases[6].o.max_accessors = 2;
        cases[7].name = "max_images";
        cases[7].o.max_images = 0; /* 0 means the setting: this one imports */
        for (j = 0; j < 8; ++j)
        {
            engine_import_result lr = NewResult();
            const engine_status status = g_api->import_gltf(engine, gltf->data, gltf->size, &cases[j].o, &lr);
            if (j == 7)
            {
                CHECK_STATUS(status, ENGINE_OK);
                RemoveAll(engine, &lr);
            }
            else
            {
                if (status == ENGINE_OK)
                {
                    printf("host_import: the limit %s did not refuse the import\n", cases[j].name);
                    ++g_failures;
                    RemoveAll(engine, &lr);
                }
                else
                {
                    LastErrorIsNotEmpty();
                }
            }
            ReleaseResult(&lr);
        }
    }
    printf("host_import: hostile files: %d refused with a message, %d still valid glTF, no crash\n", g_hostile_refused,
           g_hostile_accepted);
    CHECK(g_hostile_refused > 200);
    free(copy);
    free(glb.data);
}

int main(void)
{
    Bytes gltf;
    engine_handle engine;
    host_quiet_failures(); /* Windows: report failures on stderr, never in a dialog */
    gltf = ReadWholeFile(IMPORT_FIXTURE);
    setvbuf(stdout, NULL, _IONBF,
            0); /* unbuffered: the Windows C runtime takes _IOLBF as full buffering and asserts on size 0 */
    if (gltf.data == NULL)
    {
        return 2;
    }
#if IMPORT_DYNAMIC
    /* The same test against the shared library, loaded with dlopen: the importer is inside it. */
    engine_loader loader;
    if (engine_load(ENGINE_LIB_PATH, ENGINE_ABI_VERSION_MAJOR, ENGINE_ABI_VERSION_MINOR, &loader) != ENGINE_OK)
    {
        printf("host_import: cannot load the engine library: %s\n", loader.message);
        return 2;
    }
    g_api = loader.api;
#else
    if (engine_get_api(ENGINE_ABI_VERSION_MAJOR, ENGINE_ABI_VERSION_MINOR, sizeof(engine_api), &g_api) != ENGINE_OK)
    {
        printf("host_import: engine_get_api failed\n");
        return 2;
    }
#endif
    engine = NewEngine();
    TestInfo(engine);
    TestSync(engine, &gltf);
    TestExternal(engine, &gltf);
    TestAsync(engine, &gltf);
    TestArguments(engine, &gltf);
    TestHostile(engine, &gltf);
    CHECK_STATUS(g_api->destroy(engine), ENGINE_OK);
    free(gltf.data);
#if IMPORT_DYNAMIC
    g_api->thread_detach();
    CHECK_STATUS(engine_unload(&loader), ENGINE_OK);
#endif
    if (g_failures != 0)
    {
        printf("host_import: %d checks failed\n", g_failures);
        return 1;
    }
    printf("host_import: ok\n");
    return 0;
}
