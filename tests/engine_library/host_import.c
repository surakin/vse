/* Phase 23 Day 351: a C program imports a glTF from memory through the function table (a run-time import), the way a
   host in another language would: the file's bytes in, resources in the engine's content out. It imports a fixture
   synchronously and asynchronously, with its external files given by a read_file callback, loads every mesh, skeleton,
   clip, texture and material it made (by id and by path), removes them and imports again with the manifest to see that
   the ids are kept, cancels imports, and feeds the importer hostile files (truncated, mutated, wrapped in a lying GLB
   container, over the limits): each must come back as a status and a message, never a crash (under asan, nothing is
   read or written out of bounds). */
#include <math.h>
#include <stddef.h>
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

/* engine_api.import_format_version of this source tree. A change means a blob container or type version, or the
   importer's output version (asset::kGltfImporterOutputVersion), moved: that invalidates every host's cache of imported
   blobs, so the number is updated here on purpose, in the commit that moved it. */
#define EXPECTED_FORMAT_VERSION 0x1ba4a729f8cc6a6dull

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
    engine_buffer_release(&r->summary);
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
        /* A host built before `summary` existed passes a result of the old size, in memory of exactly that size: the
         * engine writes no byte past it (asan), on success and on the failure paths. */
        const size_t old_size = offsetof(engine_import_result, summary);
        char* old = (char*)malloc(old_size);
        engine_import_options good = Options("/oldresult");
        memset(old, 0xAB, old_size);
        *(uint32_t*)old = (uint32_t)old_size; /* struct_size is the first member */
        if (g_api->import_gltf(engine, gltf->data, gltf->size, &good, (engine_import_result*)(void*)old) == ENGINE_OK)
        {
            /* Copy what the old struct holds into a full one, to remove the resources it added. */
            engine_import_result full = NewResult();
            memcpy(&full.resource_count, old + offsetof(engine_import_result, resource_count),
                   offsetof(engine_import_result, manifest) + sizeof(engine_buffer) -
                       offsetof(engine_import_result, resource_count));
            RemoveAll(engine, &full);
            ReleaseResult(&full);
        }
        free(old);
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

/* Phase 24 Day 364: the default scene as a level: imported with ENGINE_IMPORT_EMIT_LEVEL, spawned into a world, found
 * by name. */
static uint64_t Find(engine_handle world, const char* name)
{
    uint64_t key = 0;
    if (g_api->entity_find(world, S(name), &key) != ENGINE_OK)
    {
        return 0;
    }
    return key;
}

static size_t CountEntities(engine_handle world)
{
    size_t total = 0;
    CHECK_STATUS(g_api->query(world, 0, NULL, 0, &total), ENGINE_OK);
    return total;
}

static void TestLevel(engine_handle engine, const Bytes* gltf)
{
    engine_import_options options = Options("/lvl");
    engine_import_result result = NewResult();
    engine_import_result plain = NewResult();
    engine_handle world;
    uint64_t level;
    uint64_t root = 0;
    uint64_t second_root = 0;
    uint64_t keys[8];
    size_t total = 0;
    engine_spawn_params place;
    uint64_t body;
    uint64_t tip;
    uint64_t parent = 0;
    uint32_t i;
    uint64_t mesh_id = 0;
    int has = 0;
    options.flags = ENGINE_IMPORT_EMIT_LEVEL;
    options.id_seed = 777;
    CHECK_STATUS(g_api->import_gltf(engine, gltf->data, gltf->size, &options, &result), ENGINE_OK);
    CHECK(result.resource_count == 6); /* the five resources of TestSync and the level, last */
    level = IdAt(&result, result.resource_count - 1);
    CHECK(TypeAt(&result, result.resource_count - 1) == ENGINE_RESOURCE_LEVEL);
    for (i = 0; i < result.resource_count; ++i)
    {
        if (TypeAt(&result, i) == ENGINE_RESOURCE_MESH)
        {
            mesh_id = IdAt(&result, i);
        }
    }
    /* without the flag no level is made */
    options.flags = 0;
    options.id_seed = 778;
    options.path_prefix = S("/lvl_plain");
    CHECK_STATUS(g_api->import_gltf(engine, gltf->data, gltf->size, &options, &plain), ENGINE_OK);
    CHECK(plain.resource_count == 5);

    memset(&world, 0, sizeof(world));
    CHECK_STATUS(g_api->world_create(engine, &world), ENGINE_OK);
    CHECK_STATUS(g_api->level_spawn(world, level, NULL, NULL, NULL, 0, NULL), ENGINE_ERROR_INVALID_ARGUMENT);
    CHECK(CountEntities(world) == 0);
    /* the root, and the three nodes of the file: root, tip, body */
    CHECK_STATUS(g_api->level_spawn(world, level, NULL, &root, keys, 8, &total), ENGINE_OK);
    CHECK(root != 0 && total == 4 && keys[0] == root);
    CHECK(CountEntities(world) == 4);
    body = Find(world, "body");
    tip = Find(world, "tip");
    CHECK(body != 0 && tip != 0 && Find(world, "root") != 0 && Find(world, "nothing") == 0);
    CHECK_STATUS(g_api->component_has(world, body, ENGINE_COMPONENT_MESH_REFERENCE, &has), ENGINE_OK);
    CHECK(has == 1);
    CHECK_STATUS(g_api->component_has(world, body, ENGINE_COMPONENT_SKELETON_INSTANCE, &has), ENGINE_OK);
    CHECK(has == 1);
    CHECK_STATUS(g_api->component_has(world, tip, ENGINE_COMPONENT_MESH_REFERENCE, &has), ENGINE_OK);
    CHECK(has == 0);
    CHECK_STATUS(g_api->entity_get_parent(world, tip, &parent), ENGINE_OK);
    CHECK(parent == Find(world, "root"));
    CHECK_STATUS(g_api->entity_get_parent(world, parent, &parent), ENGINE_OK);
    CHECK(parent == root);
    /* the node's own transform: the tip is one above its parent */
    {
        engine_transform t;
        memset(&t, 0, sizeof(t));
        t.struct_size = sizeof(t);
        CHECK_STATUS(g_api->component_get(world, tip, ENGINE_COMPONENT_TRANSFORM, &t), ENGINE_OK);
        CHECK(fabsf(t.position[1] - 1.0f) < 1e-5f);
    }
    /* a second spawn is a second, independent copy: the prefix tells the names apart */
    memset(&place, 0, sizeof(place));
    place.struct_size = sizeof(place);
    place.position[0] = 10.0f;
    place.name_prefix = S("b");
    CHECK_STATUS(g_api->level_spawn(world, level, &place, &second_root, NULL, 0, &total), ENGINE_OK);
    CHECK(second_root != 0 && second_root != root && total == 4 && CountEntities(world) == 8);
    CHECK(Find(world, "b/body") != 0 && Find(world, "b/body") != body);
    {
        engine_frame_info frame;
        engine_world_transform moved;
        memset(&frame, 0, sizeof(frame));
        frame.struct_size = sizeof(frame);
        frame.delta_seconds = 0.016;
        CHECK_STATUS(g_api->frame(engine, &frame, NULL), ENGINE_OK);
        memset(&moved, 0, sizeof(moved));
        moved.struct_size = sizeof(moved);
        CHECK_STATUS(g_api->component_get(world, Find(world, "b/tip"), ENGINE_COMPONENT_WORLD_TRANSFORM, &moved),
                     ENGINE_OK);
        CHECK(fabsf(moved.matrix[12] - 10.0f) < 1e-4f && fabsf(moved.matrix[13] - 1.0f) < 1e-4f);
    }
    /* refusals leave the world as it was */
    {
        uint64_t none = 0;
        const size_t before = CountEntities(world);
        CHECK_STATUS(g_api->level_spawn(world, 0x1234567ull, NULL, &none, NULL, 0, NULL), ENGINE_ERROR_NOT_FOUND);
        CHECK_STATUS(g_api->level_spawn(world, mesh_id, NULL, &none, NULL, 0, NULL), ENGINE_ERROR_TYPE_MISMATCH);
        place.name_prefix = S("a prefix that is far too long");
        CHECK_STATUS(g_api->level_spawn(world, level, &place, &none, NULL, 0, NULL), ENGINE_ERROR_INVALID_ARGUMENT);
        CHECK(CountEntities(world) == before && none == 0);
        /* a level whose mesh is gone is refused whole */
        CHECK_STATUS(g_api->resource_remove(engine, mesh_id, ENGINE_RESOURCE_FORCE, NULL), ENGINE_OK);
        CHECK_STATUS(g_api->level_spawn(world, level, NULL, &none, NULL, 0, NULL), ENGINE_ERROR_NOT_FOUND);
        CHECK(CountEntities(world) == before);
        LastErrorIsNotEmpty();
    }
    CHECK_STATUS(g_api->world_destroy(world), ENGINE_OK);
    ReleaseResult(&result);
    ReleaseResult(&plain);
}

/* ---- host textures (Phase 24 Day 365) ---- */

typedef struct texture_producer
{
    engine_handle texture;
    int failures;
} texture_producer;

static void ProduceTexture(void* user)
{
    texture_producer* p = (texture_producer*)user;
    unsigned char pixels[4 * 4 * 4];
    int i;
    for (i = 0; i < 200; ++i)
    {
        memset(pixels, i & 0xFF, sizeof(pixels));
        if (g_api->texture_update(p->texture, pixels, 16, NULL) != ENGINE_OK)
        {
            ++p->failures;
        }
    }
    g_api->thread_detach();
}

/* A host texture is made, updated (also from another thread: the latest update wins, the replaced ones are counted),
   read back, resized, and destroyed; wrong arguments are statuses. */
static void TestTextures(engine_handle engine)
{
    engine_texture_info info;
    engine_handle texture = NoHandle();
    engine_handle other = NoHandle();
    unsigned char pixels[4 * 4 * 4];
    unsigned char big[8 * 8 * 4];
    engine_buffer out;
    uint32_t w = 0;
    uint32_t h = 0;
    engine_update_info update;
    engine_frame_info frame;
    engine_frame_stats stats;
    host_thread thread;
    texture_producer producer;
    int i;
    memset(&info, 0, sizeof(info));
    memset(&out, 0, sizeof(out));
    info.struct_size = sizeof(info);
    info.width = 4;
    info.height = 4;
    info.name = S("test texture");
    CHECK_STATUS(g_api->texture_create(engine, NULL, &texture), ENGINE_ERROR_INVALID_ARGUMENT);
    CHECK_STATUS(g_api->texture_create(engine, &info, NULL), ENGINE_ERROR_INVALID_ARGUMENT);
    info.width = 0;
    CHECK_STATUS(g_api->texture_create(engine, &info, &texture), ENGINE_ERROR_INVALID_ARGUMENT);
    info.width = 1u << 20;
    CHECK_STATUS(g_api->texture_create(engine, &info, &texture), ENGINE_ERROR_TOO_LARGE);
    info.width = 4;
    info.flags = 0x80000000u;
    CHECK_STATUS(g_api->texture_create(engine, &info, &texture), ENGINE_ERROR_INVALID_ARGUMENT);
    info.flags = ENGINE_TEXTURE_SRGB;
    CHECK_STATUS(g_api->texture_create(engine, &info, &texture), ENGINE_OK);

    /* no pixels yet */
    CHECK_STATUS(g_api->texture_read_pixels(texture, &out, &w, &h), ENGINE_ERROR_NOT_READY);
    memset(pixels, 0x7F, sizeof(pixels));
    CHECK_STATUS(g_api->texture_update(texture, NULL, 16, NULL), ENGINE_ERROR_INVALID_ARGUMENT);
    CHECK_STATUS(g_api->texture_update(texture, pixels, 8, NULL), ENGINE_ERROR_INVALID_ARGUMENT); /* stride < row */
    CHECK_STATUS(g_api->texture_update(texture, pixels, 16, NULL), ENGINE_OK);
    CHECK_STATUS(g_api->texture_read_pixels(texture, &out, &w, &h), ENGINE_OK);
    CHECK(w == 4 && h == 4 && out.size == sizeof(pixels) && memcmp(out.data, pixels, sizeof(pixels)) == 0);
    engine_buffer_release(&out);

    /* a padded stride copies rows, not the padding */
    {
        unsigned char padded[4 * 32];
        memset(padded, 0xEE, sizeof(padded));
        for (i = 0; i < 4; ++i)
        {
            memset(padded + i * 32, 0x10 + i, 16);
        }
        CHECK_STATUS(g_api->texture_update(texture, padded, 32, NULL), ENGINE_OK);
        CHECK_STATUS(g_api->texture_read_pixels(texture, &out, &w, &h), ENGINE_OK);
        CHECK(out.size == 64 && ((unsigned char*)out.data)[16] == 0x11 && ((unsigned char*)out.data)[63] == 0x13);
        engine_buffer_release(&out);
    }

    /* an update that carries another size resizes; a bad size changes nothing */
    memset(big, 0x55, sizeof(big));
    memset(&update, 0, sizeof(update));
    update.struct_size = sizeof(update);
    update.width = 8;
    update.height = 8;
    CHECK_STATUS(g_api->texture_update(texture, big, 32, &update), ENGINE_OK);
    CHECK_STATUS(g_api->texture_read_pixels(texture, &out, &w, &h), ENGINE_OK);
    CHECK(w == 8 && h == 8 && out.size == sizeof(big));
    engine_buffer_release(&out);
    update.width = 1u << 20;
    CHECK_STATUS(g_api->texture_update(texture, big, 32, &update), ENGINE_ERROR_TOO_LARGE);
    update.width = 8;
    update.flags = 1;
    CHECK_STATUS(g_api->texture_update(texture, big, 32, &update), ENGINE_ERROR_INVALID_ARGUMENT);

    /* resize drops the pixels until the next update */
    CHECK_STATUS(g_api->texture_resize(texture, 0, 4), ENGINE_ERROR_INVALID_ARGUMENT);
    CHECK_STATUS(g_api->texture_resize(texture, 4, 4), ENGINE_OK);
    CHECK_STATUS(g_api->texture_read_pixels(texture, &out, &w, &h), ENGINE_ERROR_NOT_READY);

    /* a producer thread and the host's frame thread: the final update is what is read back */
    memset(&frame, 0, sizeof(frame));
    frame.struct_size = sizeof(frame);
    producer.texture = texture;
    producer.failures = 0;
    CHECK(host_thread_start(&thread, ProduceTexture, &producer) == 0);
    memset(&stats, 0, sizeof(stats));
    stats.struct_size = sizeof(stats);
    for (i = 0; i < 5; ++i)
    {
        CHECK_STATUS(g_api->frame(engine, &frame, &stats), ENGINE_OK);
    }
    host_thread_join(&thread);
    CHECK(producer.failures == 0);
    CHECK_STATUS(g_api->frame(engine, &frame, &stats), ENGINE_OK);
    CHECK_STATUS(g_api->texture_read_pixels(texture, &out, &w, &h), ENGINE_OK);
    CHECK(out.size == 64 && ((unsigned char*)out.data)[0] == 199);
    engine_buffer_release(&out);
    /* the counters are read and reset by each frame call: the frame after the last one reports none */
    CHECK_STATUS(g_api->frame(engine, &frame, &stats), ENGINE_OK);
    CHECK(stats.updates_replaced == 0 && stats.textures_uploaded == 0 && stats.bytes_uploaded == 0);

    /* destroy: the handle is stale after, a second texture is independent */
    CHECK_STATUS(g_api->texture_create(engine, &info, &other), ENGINE_OK);
    CHECK_STATUS(g_api->texture_destroy(texture), ENGINE_OK);
    CHECK_STATUS(g_api->texture_destroy(texture), ENGINE_ERROR_STALE_HANDLE);
    CHECK_STATUS(g_api->texture_update(texture, pixels, 16, NULL), ENGINE_ERROR_STALE_HANDLE);
    CHECK_STATUS(g_api->texture_update(other, pixels, 16, NULL), ENGINE_OK);
    CHECK_STATUS(g_api->texture_destroy(other), ENGINE_OK);

    /* the engine counts a texture as an object: destroy is refused while one lives, and slots are reused */
    for (i = 0; i < 24; ++i)
    {
        CHECK_STATUS(g_api->texture_create(engine, &info, &texture), ENGINE_OK);
        CHECK_STATUS(g_api->texture_update(texture, pixels, 16, NULL), ENGINE_OK); /* pending, never uploaded */
        CHECK_STATUS(g_api->texture_destroy(texture), ENGINE_OK);
    }
}

/* A hierarchy deeper than a level holds, and more nodes than the limit, are refused with a message, never a crash. */
static void TestDeepScene(engine_handle engine)
{
    char text[16384];
    size_t used;
    int i;
    engine_import_options options = Options("/deep");
    engine_import_result result = NewResult();
    options.flags = ENGINE_IMPORT_EMIT_LEVEL;
    used = (size_t)snprintf(text, sizeof(text),
                            "{\"asset\":{\"version\":\"2.0\"},\"scene\":0,\"scenes\":[{\"nodes\":[0]}],\"nodes\":[");
    for (i = 0; i < 70; ++i)
    {
        if (i + 1 < 70)
        {
            used += (size_t)snprintf(text + used, sizeof(text) - used, "%s{\"name\":\"n%d\",\"children\":[%d]}",
                                     i == 0 ? "" : ",", i, i + 1);
        }
        else
        {
            used += (size_t)snprintf(text + used, sizeof(text) - used, ",{\"name\":\"n%d\"}", i);
        }
    }
    used += (size_t)snprintf(text + used, sizeof(text) - used, "]}");
    CHECK_STATUS(g_api->import_gltf(engine, text, used, &options, &result), ENGINE_ERROR_TOO_LARGE);
    LastErrorIsNotEmpty();
    ReleaseResult(&result);
    /* the same file without the level imports (nothing in it needs one) */
    options.flags = 0;
    result = NewResult();
    CHECK_STATUS(g_api->import_gltf(engine, text, used, &options, &result), ENGINE_OK);
    ReleaseResult(&result);
    /* fewer nodes than the file has */
    options = Options("/deep2");
    options.flags = ENGINE_IMPORT_EMIT_LEVEL;
    options.max_nodes = 10;
    result = NewResult();
    CHECK_STATUS(g_api->import_gltf(engine, text, used, &options, &result), ENGINE_ERROR_TOO_LARGE);
    ReleaseResult(&result);
}

/* ---- Maintenance (2026-11-18): the host's own cache of imported resources */

typedef struct CachedResource
{
    uint64_t id;
    uint32_t type;
    char path[256];
    engine_buffer blob;
    engine_resource_info info;
} CachedResource;

static engine_resource_info NewInfo(void)
{
    engine_resource_info i;
    memset(&i, 0, sizeof(i));
    i.struct_size = sizeof(i);
    return i;
}

static engine_resource_add_info AddInfoOf(const CachedResource* c)
{
    engine_resource_add_info a;
    memset(&a, 0, sizeof(a));
    a.struct_size = sizeof(a);
    a.id = c->id;
    a.path = S(c->path);
    a.bytes = c->blob.data;
    a.size = c->blob.size;
    a.flags = ENGINE_RESOURCE_CHECK_HASH;
    memcpy(a.expected_hash, c->info.content_hash, 16);
    return a;
}

static int NoneMounted(engine_handle engine, const CachedResource* items, uint32_t count)
{
    uint32_t i;
    for (i = 0; i < count; ++i)
    {
        engine_resource_info info = NewInfo();
        if (g_api->resource_mounted(engine, items[i].id, &info) != ENGINE_ERROR_NOT_FOUND)
        {
            return 0;
        }
    }
    return 1;
}

static int AllMountedAsCached(engine_handle engine, const CachedResource* items, uint32_t count)
{
    uint32_t i;
    for (i = 0; i < count; ++i)
    {
        engine_resource_info info = NewInfo();
        engine_buffer again;
        memset(&again, 0, sizeof(again));
        if (g_api->resource_mounted(engine, items[i].id, &info) != ENGINE_OK || info.type != items[i].type ||
            info.size != items[i].blob.size || memcmp(info.content_hash, items[i].info.content_hash, 16) != 0)
        {
            return 0;
        }
        if (g_api->resource_read(engine, items[i].id, 0, &again) != ENGINE_OK || again.size != items[i].blob.size ||
            memcmp(again.data, items[i].blob.data, again.size) != 0)
        {
            engine_buffer_release(&again);
            return 0;
        }
        engine_buffer_release(&again);
    }
    return 1;
}

static void TestResourceCache(const Bytes* gltf)
{
    enum
    {
        kMax = 16
    };
    CachedResource items[kMax];
    engine_handle engine;
    engine_import_options options = Options("/cache");
    engine_import_result result = NewResult();
    engine_resource_add_info infos[kMax + 1];
    uint32_t count;
    uint32_t i;
    uint64_t level = 0;
    uint64_t fingerprint = g_api->import_format_version();
    size_t failed = 99;
    char summary[16384];
    const char* line;
    engine_info info;

    /* the fingerprint needs no engine, is not zero and does not change */
    CHECK(fingerprint != 0 && g_api->import_format_version() == fingerprint);
    printf("host_import: format fingerprint %016llx\n", (unsigned long long)fingerprint);
    CHECK(fingerprint == EXPECTED_FORMAT_VERSION);

    engine = NewEngine();
    memset(&info, 0, sizeof(info));
    info.struct_size = sizeof(info);
    CHECK_STATUS(g_api->get_info(engine, &info), ENGINE_OK);
    CHECK((info.caps & ENGINE_CAP_RESOURCE_CACHE) != 0);
    options.flags = ENGINE_IMPORT_EMIT_LEVEL;
    options.id_seed = 4242;
    CHECK_STATUS(g_api->import_gltf(engine, gltf->data, gltf->size, &options, &result), ENGINE_OK);
    count = result.resource_count;
    CHECK(count == 6 && count <= kMax);
    level = IdAt(&result, count - 1);

    { /* a host built against the first version of the struct (no `summary`) is served, and its struct is not overrun */
        engine_import_result old = NewResult();
        engine_import_options again = Options("/cache_old");
        again.flags = ENGINE_IMPORT_EMIT_LEVEL;
        again.id_seed = 4343;
        old.struct_size = (uint32_t)offsetof(engine_import_result, summary);
        CHECK_STATUS(g_api->import_gltf(engine, gltf->data, gltf->size, &again, &old), ENGINE_OK);
        CHECK(old.struct_size == offsetof(engine_import_result, summary) && old.resource_count == 6);
        engine_buffer_release(&old.ids);
        engine_buffer_release(&old.types);
        engine_buffer_release(&old.paths);
        engine_buffer_release(&old.warnings);
        engine_buffer_release(&old.manifest);
        old.struct_size = 8;
        CHECK_STATUS(g_api->import_gltf(engine, gltf->data, gltf->size, &again, &old), ENGINE_ERROR_STRUCT_SIZE);
    }
    /* the facts about the file, as the host stores them beside the blobs */
    CHECK(result.summary.size > 0 && result.summary.size < sizeof(summary));
    memcpy(summary, result.summary.data, result.summary.size);
    summary[result.summary.size] = 0;
    CHECK(strstr(summary, "\"version\":1") != NULL && strstr(summary, "\"name\":\"body\"") != NULL &&
          strstr(summary, "\"name\":\"tip\"") != NULL && strstr(summary, "\"bounds\":{\"min\":[") != NULL &&
          strstr(summary, "\"materials\":[") != NULL && strstr(summary, "\"max\":[1,1,0]") != NULL &&
          strstr(summary, "\"animations\":[{\"name\":\"sway\",\"duration\":1}]") != NULL &&
          strstr(summary, "\"position\":[0,1,0]") != NULL);

    line = result.paths.data;
    for (i = 0; i < count; ++i)
    {
        const char* end = strchr(line, '\n');
        size_t length = end == NULL ? 0 : (size_t)(end - line);
        engine_buffer again;
        memset(&again, 0, sizeof(again));
        memset(&items[i], 0, sizeof(items[i]));
        items[i].id = IdAt(&result, i);
        items[i].type = TypeAt(&result, i);
        CHECK(length > 0 && length < sizeof(items[i].path));
        memcpy(items[i].path, line, length < sizeof(items[i].path) ? length : 0);
        line = end == NULL ? line : end + 1;
        items[i].info = NewInfo();
        CHECK_STATUS(g_api->resource_mounted(engine, items[i].id, &items[i].info), ENGINE_OK);
        CHECK_STATUS(g_api->resource_read(engine, items[i].id, 0, &items[i].blob), ENGINE_OK);
        CHECK(items[i].blob.size == items[i].info.size && items[i].blob.size > 0);
        /* a limit below the size refuses before copying; the buffer is zeroed on every failure */
        CHECK_STATUS(g_api->resource_read(engine, items[i].id, 1, &again), ENGINE_ERROR_TOO_LARGE);
        CHECK(again.data == NULL && again.size == 0);
        CHECK_STATUS(g_api->resource_read(engine, items[i].id, items[i].blob.size, &again), ENGINE_OK);
        CHECK(again.size == items[i].blob.size && memcmp(again.data, items[i].blob.data, again.size) == 0);
        engine_buffer_release(&again);
    }
    {
        engine_buffer none;
        memset(&none, 0, sizeof(none));
        CHECK_STATUS(g_api->resource_read(engine, 0, 0, &none), ENGINE_ERROR_INVALID_ARGUMENT);
        CHECK_STATUS(g_api->resource_read(engine, 0xDEADBEEFull, 0, &none), ENGINE_ERROR_NOT_FOUND);
        CHECK_STATUS(g_api->resource_read(engine, items[0].id, 0, NULL), ENGINE_ERROR_INVALID_ARGUMENT);
        CHECK(none.data == NULL);
    }
    ReleaseResult(&result);
    CHECK_STATUS(g_api->destroy(engine), ENGINE_OK);
    {
        engine_buffer stale;
        memset(&stale, 0, sizeof(stale));
        CHECK_STATUS(g_api->resource_read(engine, items[0].id, 0, &stale), ENGINE_ERROR_STALE_HANDLE);
    }

    /* a second engine takes the blobs back, all or none */
    engine = NewEngine();
    for (i = 0; i < count; ++i)
    {
        infos[i] = AddInfoOf(&items[i]);
    }
    CHECK_STATUS(g_api->resource_add_many(engine, NULL, 0, &failed), ENGINE_OK); /* nothing to add */
    CHECK_STATUS(g_api->resource_add_many(engine, NULL, 3, &failed), ENGINE_ERROR_INVALID_ARGUMENT);
    CHECK_STATUS(g_api->resource_add_many(engine, infos, 70000, &failed), ENGINE_ERROR_LIMIT_EXCEEDED);
    CHECK(NoneMounted(engine, items, count));
    { /* borrowed bytes are refused: the engine copies */
        engine_resource_add_info lent = infos[0];
        lent.flags |= ENGINE_RESOURCE_BORROW;
        CHECK_STATUS(g_api->resource_add_many(engine, &lent, 1, &failed), ENGINE_ERROR_INVALID_ARGUMENT);
        CHECK(failed == 0);
    }
    { /* the last blob is damaged: the error names it and nothing was added */
        engine_resource_add_info broken[kMax];
        unsigned char* damaged = (unsigned char*)malloc(items[count - 1].blob.size);
        memcpy(damaged, items[count - 1].blob.data, items[count - 1].blob.size);
        damaged[items[count - 1].blob.size - 1] ^= 0x5A;
        memcpy(broken, infos, count * sizeof(infos[0]));
        broken[count - 1].bytes = damaged;
        broken[count - 1].flags = 0;
        failed = 99;
        CHECK_STATUS(g_api->resource_add_many(engine, broken, count, &failed), ENGINE_ERROR_CORRUPT);
        CHECK(failed == count - 1);
        CHECK(NoneMounted(engine, items, count));
        free(damaged);
    }
    { /* an id twice in one batch: refused, and nothing was added */
        memcpy(&infos[count], &infos[0], sizeof(infos[0]));
        failed = 99;
        CHECK_STATUS(g_api->resource_add_many(engine, infos, count + 1, &failed), ENGINE_ERROR_ALREADY_EXISTS);
        CHECK(failed == count);
        CHECK(NoneMounted(engine, items, count));
    }
    CHECK_STATUS(g_api->resource_add_many(engine, infos, count, &failed), ENGINE_OK);
    CHECK(AllMountedAsCached(engine, items, count));
    /* the same again: every id is live, nothing changes */
    failed = 99;
    CHECK_STATUS(g_api->resource_add_many(engine, infos, count, &failed), ENGINE_ERROR_ALREADY_EXISTS);
    CHECK(failed == 0);
    CHECK(AllMountedAsCached(engine, items, count));
    { /* the cached level loads and spawns as the imported one does */
        engine_handle world;
        engine_handle loaded;
        uint64_t root = 0;
        uint64_t keys[8];
        size_t total = 0;
        memset(&world, 0, sizeof(world));
        memset(&loaded, 0, sizeof(loaded));
        CHECK_STATUS(g_api->resource_load(engine, level, NoPath(), ENGINE_RESOURCE_LEVEL, &loaded), ENGINE_OK);
        CHECK_STATUS(g_api->world_create(engine, &world), ENGINE_OK);
        CHECK_STATUS(g_api->level_spawn(world, level, NULL, &root, keys, 8, &total), ENGINE_OK);
        CHECK(total == 4 && Find(world, "body") != 0 && Find(world, "tip") != 0);
        CHECK_STATUS(g_api->world_destroy(world), ENGINE_OK);
        CHECK_STATUS(g_api->resource_release(loaded), ENGINE_OK);
    }
    /* remove them all, and add them on a worker */
    for (i = 0; i < count; ++i)
    {
        CHECK_STATUS(g_api->resource_remove(engine, items[count - 1 - i].id, ENGINE_RESOURCE_FORCE, NULL), ENGINE_OK);
    }
    CHECK(NoneMounted(engine, items, count));
    {
        engine_handle ticket;
        engine_handle second;
        memset(&ticket, 0, sizeof(ticket));
        memset(&second, 0, sizeof(second));
        CHECK_STATUS(g_api->resource_add_many_async(engine, infos, count, &ticket), ENGINE_OK);
        CHECK_STATUS(g_api->destroy(engine), ENGINE_ERROR_IN_USE); /* the ticket is an object of the engine */
        CHECK_STATUS(g_api->resource_add_wait(ticket, 0xFFFFFFFFFFFFFFFFull), ENGINE_OK);
        failed = 99;
        CHECK_STATUS(g_api->resource_add_finish(ticket, &failed), ENGINE_OK);
        CHECK_STATUS(g_api->resource_add_finish(ticket, &failed), ENGINE_ERROR_STALE_HANDLE);
        CHECK_STATUS(g_api->resource_add_wait(ticket, 0), ENGINE_ERROR_STALE_HANDLE);
        CHECK(AllMountedAsCached(engine, items, count));
        /* an asynchronous adding that is refused leaves nothing, and says which blob */
        CHECK_STATUS(g_api->resource_add_many_async(engine, infos, count, &second), ENGINE_OK);
        CHECK_STATUS(g_api->resource_add_wait(second, 0xFFFFFFFFFFFFFFFFull), ENGINE_OK);
        failed = 99;
        CHECK_STATUS(g_api->resource_add_finish(second, &failed), ENGINE_ERROR_ALREADY_EXISTS);
        CHECK(failed == 0);
        CHECK_STATUS(g_api->resource_add_many_async(engine, infos, count, &second), ENGINE_OK);
        CHECK_STATUS(g_api->resource_add_destroy(second), ENGINE_OK); /* waits for the worker */
        CHECK_STATUS(g_api->resource_add_destroy(second), ENGINE_ERROR_STALE_HANDLE);
        CHECK_STATUS(g_api->resource_add_many_async(engine, NULL, 3, &second), ENGINE_ERROR_INVALID_ARGUMENT);
        CHECK(second.value == 0);
        CHECK(AllMountedAsCached(engine, items, count));
    }
    CHECK_STATUS(g_api->destroy(engine), ENGINE_OK);

    /* a budget too small for the batch: refused as a whole, nothing added */
    {
        char budget[32];
        size_t total = 0;
        engine_setting_entry entry;
        engine_create_info create;
        for (i = 0; i < count; ++i)
        {
            total += items[i].blob.size;
        }
        snprintf(budget, sizeof(budget), "%llu", (unsigned long long)(total - 1));
        memset(&create, 0, sizeof(create));
        create.struct_size = sizeof(create);
        create.flags = ENGINE_CREATE_LOG_SUPPRESS_CONSOLE;
        entry.key = S("assets.memory_backend_bytes");
        entry.value = S(budget);
        create.settings = &entry;
        create.setting_count = 1;
        memset(&engine, 0, sizeof(engine));
        CHECK_STATUS(g_api->create(&create, &engine), ENGINE_OK);
        failed = 99;
        CHECK_STATUS(g_api->resource_add_many(engine, infos, count, &failed), ENGINE_ERROR_LIMIT_EXCEEDED);
        CHECK(failed == count);
        CHECK(NoneMounted(engine, items, count));
        CHECK_STATUS(g_api->destroy(engine), ENGINE_OK);
    }
    for (i = 0; i < count; ++i)
    {
        engine_buffer_release(&items[i].blob);
    }
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
    TestLevel(engine, &gltf);
    TestDeepScene(engine);
    TestTextures(engine);
    CHECK_STATUS(g_api->destroy(engine), ENGINE_OK);
    TestResourceCache(&gltf);
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
