/* Phase 23 Day 350: a C program adds, replaces and removes host-provided content through the function table and loads
   it, synchronously and with tickets. The blobs are minted by asset_mint
   (tests/engine_library/mint_resources_data.cmake): a texture (0d01), a second version of it with the same id, a
   material (0e01) that depends on the texture and a mesh (0b02). It counts every release of a lent buffer: exactly one
   per successful add or replace that lent it, on an engine worker thread, after the last reader is done, and never for
   a call that failed. */
#include <pthread.h>
#include <stdatomic.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "engine_c/engine_c.h"

#define TEXTURE_ID 0xd01u
#define MATERIAL_ID 0xe01u
#define MESH_ID 0xb02u

static atomic_int g_failures;
static const engine_api* g_api = NULL;
static pthread_t g_main_thread;

#define CHECK(condition)                                                                                               \
    do                                                                                                                 \
    {                                                                                                                  \
        if (!(condition))                                                                                              \
        {                                                                                                              \
            printf("host_resources: line %d: CHECK failed: %s\n", __LINE__, #condition);                               \
            atomic_fetch_add(&g_failures, 1);                                                                          \
        }                                                                                                              \
    } while (0)

#define CHECK_STATUS(call, expected)                                                                                   \
    do                                                                                                                 \
    {                                                                                                                  \
        const engine_status got_ = (call);                                                                             \
        if (got_ != (expected))                                                                                        \
        {                                                                                                              \
            printf("host_resources: line %d: %s returned %d, expected %d\n", __LINE__, #call, (int)got_,               \
                   (int)(expected));                                                                                   \
            atomic_fetch_add(&g_failures, 1);                                                                          \
        }                                                                                                              \
    } while (0)

static engine_string S(const char* text)
{
    engine_string s;
    s.data = text;
    s.length = strlen(text);
    return s;
}

static engine_handle NoHandle(void)
{
    engine_handle h;
    memset(&h, 0, sizeof(h));
    return h;
}

static engine_string NoPath(void)
{
    engine_string s;
    s.data = NULL;
    s.length = 0;
    return s;
}

/* ---- the blobs */

typedef struct Blob
{
    unsigned char* data;
    size_t size;
} Blob;

static Blob ReadBlob(const char* name)
{
    char path[1024];
    Blob blob = {NULL, 0};
    FILE* f;
    snprintf(path, sizeof(path), "%s/%s", RES_DATA_DIR, name);
    f = fopen(path, "rb");
    if (f == NULL)
    {
        printf("host_resources: cannot open %s\n", path);
        atomic_fetch_add(&g_failures, 1);
        return blob;
    }
    fseek(f, 0, SEEK_END);
    blob.size = (size_t)ftell(f);
    fseek(f, 0, SEEK_SET);
    blob.data = (unsigned char*)malloc(blob.size);
    if (fread(blob.data, 1, blob.size, f) != blob.size)
    {
        printf("host_resources: short read of %s\n", path);
        atomic_fetch_add(&g_failures, 1);
    }
    fclose(f);
    return blob;
}

static Blob Copy(const Blob* blob)
{
    Blob copy;
    copy.size = blob->size;
    copy.data = (unsigned char*)malloc(blob->size);
    memcpy(copy.data, blob->data, blob->size);
    return copy;
}

/* ---- a lent buffer and the counting of its release */

typedef struct Lent
{
    Blob blob;              /* the host's bytes, valid until the release */
    atomic_int released;    /* how many times the engine handed it back */
    atomic_int wrong_place; /* a release on the thread that called the engine, or with other bytes */
} Lent;

static void OnRelease(void* user, const void* bytes, size_t size)
{
    Lent* lent = (Lent*)user;
    if (pthread_equal(pthread_self(), g_main_thread) || bytes != lent->blob.data || size != lent->blob.size)
    {
        atomic_fetch_add(&lent->wrong_place, 1);
    }
    atomic_fetch_add(&lent->released, 1);
}

static void LentInit(Lent* lent, const Blob* from)
{
    lent->blob = Copy(from);
    atomic_init(&lent->released, 0);
    atomic_init(&lent->wrong_place, 0);
}

/* Waits (at most 3 s) until the buffer was handed back, then a little longer to see that it is not handed back twice.
 */
static int Released(Lent* lent)
{
    int waited = 0;
    while (atomic_load(&lent->released) == 0 && waited < 3000)
    {
        usleep(1000);
        ++waited;
    }
    usleep(2000);
    return atomic_load(&lent->released);
}

/* Looks briefly and says how many times the buffer was handed back: for the cases where none is expected. */
static int StillMine(Lent* lent)
{
    usleep(30000);
    return atomic_load(&lent->released);
}

static engine_resource_add_info Info(uint64_t id, const char* path, const Blob* blob, uint32_t flags, Lent* lent)
{
    engine_resource_add_info info;
    memset(&info, 0, sizeof(info));
    info.struct_size = sizeof(info);
    info.flags = flags;
    info.id = id;
    info.path = path != NULL ? S(path) : NoPath();
    info.bytes = blob->data;
    info.size = blob->size;
    if (lent != NULL)
    {
        info.bytes = lent->blob.data; /* the lent buffer is the host's own copy: it is what is read in place */
        info.size = lent->blob.size;
        info.flags |= ENGINE_RESOURCE_BORROW;
        info.release = &OnRelease;
        info.release_user = lent;
    }
    return info;
}

static engine_handle NewEngine(const char* priority)
{
    engine_setting_entry entry;
    engine_create_info info;
    engine_handle engine;
    memset(&info, 0, sizeof(info));
    info.struct_size = sizeof(info);
    info.flags = ENGINE_CREATE_LOG_SUPPRESS_CONSOLE;
    if (priority != NULL)
    {
        entry.key = S("assets.memory_priority");
        entry.value = S(priority);
        info.settings = &entry;
        info.setting_count = 1;
    }
    memset(&engine, 0, sizeof(engine));
    CHECK_STATUS(g_api->create(&info, &engine), ENGINE_OK);
    return engine;
}

/* The ids a dependents buffer holds (it is an array of uint64_t values). */
static size_t DependentIds(const engine_buffer* buffer, uint64_t* out, size_t capacity)
{
    size_t count = buffer->size / sizeof(uint64_t);
    size_t i;
    for (i = 0; i < count && i < capacity; ++i)
    {
        memcpy(&out[i], buffer->data + i * sizeof(uint64_t), sizeof(uint64_t));
    }
    return count;
}

static engine_handle LoadSync(engine_handle engine, uint64_t id, uint32_t type)
{
    engine_handle handle;
    memset(&handle, 0, sizeof(handle));
    CHECK_STATUS(g_api->resource_load(engine, id, NoPath(), type, &handle), ENGINE_OK);
    return handle;
}

static engine_status MountedStatus(engine_handle engine, uint64_t id)
{
    engine_resource_info info;
    memset(&info, 0, sizeof(info));
    info.struct_size = sizeof(info);
    return g_api->resource_mounted(engine, id, &info);
}

static int Loaded(engine_handle resource)
{
    engine_resource_info info;
    memset(&info, 0, sizeof(info));
    info.struct_size = sizeof(info);
    if (g_api->resource_status(resource, &info) != ENGINE_OK)
    {
        return -1;
    }
    return info.loaded;
}

/* ---- the fixture: the minted blobs, and the texture's lent buffer that the parts hand on */

static Blob g_texture;
static Blob g_texture_b;
static Blob g_material;
static Blob g_mesh;
static Lent g_texture_lent;

/* ---- adding, the bad calls, the questions */

static void TestAddAndMounted(engine_handle engine)
{
    Lent duplicate_lent;
    Lent torn_lent;
    engine_resource_add_info info;
    engine_resource_info mounted;
    engine_handle none;
    int i;

    LentInit(&duplicate_lent, &g_texture);
    LentInit(&torn_lent, &g_mesh);
    torn_lent.blob.data[torn_lent.blob.size / 2] ^= 0xFF; /* a lent buffer with one flipped byte */
    memset(&none, 0, sizeof(none));

    /* the calls the API refuses, none of which keeps the host's bytes or calls its release */
    info = Info(0, NULL, &g_mesh, 0, NULL);
    CHECK_STATUS(g_api->resource_add(engine, &info), ENGINE_ERROR_INVALID_ARGUMENT);
    info = Info(MESH_ID, NULL, &g_mesh, ENGINE_RESOURCE_BORROW, NULL); /* BORROW without a release function */
    CHECK_STATUS(g_api->resource_add(engine, &info), ENGINE_ERROR_INVALID_ARGUMENT);
    info = Info(MESH_ID, NULL, &g_mesh, 0, NULL);
    info.release = &OnRelease; /* a release function without BORROW */
    CHECK_STATUS(g_api->resource_add(engine, &info), ENGINE_ERROR_INVALID_ARGUMENT);
    info = Info(MESH_ID, NULL, &g_mesh, 0, NULL);
    info.flags = 0x80;
    CHECK_STATUS(g_api->resource_add(engine, &info), ENGINE_ERROR_INVALID_ARGUMENT);
    info = Info(MESH_ID, NULL, &g_mesh, ENGINE_RESOURCE_FORCE, NULL); /* FORCE is for replace */
    CHECK_STATUS(g_api->resource_add(engine, &info), ENGINE_ERROR_INVALID_ARGUMENT);
    info = Info(MESH_ID, NULL, &g_mesh, 0, NULL);
    info.struct_size = 4;
    CHECK_STATUS(g_api->resource_add(engine, &info), ENGINE_ERROR_STRUCT_SIZE);
    CHECK_STATUS(g_api->resource_add(engine, NULL), ENGINE_ERROR_INVALID_ARGUMENT);
    info = Info(TEXTURE_ID, NULL, &g_mesh, 0, NULL); /* the blob's own id is not the one given */
    CHECK_STATUS(g_api->resource_add(engine, &info), ENGINE_ERROR_CORRUPT);
    {
        Blob torn = Copy(&g_mesh);
        torn.data[torn.size / 2] ^= 0xFF; /* one flipped byte: the blob's hash no longer verifies */
        info = Info(MESH_ID, NULL, &torn, 0, NULL);
        CHECK_STATUS(g_api->resource_add(engine, &info), ENGINE_ERROR_CORRUPT);
        info = Info(MESH_ID, NULL, &torn, 0, &torn_lent);
        CHECK_STATUS(g_api->resource_add(engine, &info), ENGINE_ERROR_CORRUPT);
        free(torn.data);
    }
    info = Info(MESH_ID, NULL, &g_mesh, ENGINE_RESOURCE_CHECK_HASH, NULL); /* the expected hash is zeros */
    CHECK_STATUS(g_api->resource_add(engine, &info), ENGINE_ERROR_CORRUPT);
    info = Info(MESH_ID, NULL, &g_mesh, 0, NULL);
    CHECK_STATUS(g_api->resource_add(none, &info), ENGINE_ERROR_INVALID_ARGUMENT);

    /* the content: the mesh and the material copied, the texture lent */
    info = Info(MESH_ID, "/meshes/rock", &g_mesh, 0, NULL);
    CHECK_STATUS(g_api->resource_add(engine, &info), ENGINE_OK);
    info = Info(MATERIAL_ID, "/materials/brick", &g_material, 0, NULL);
    CHECK_STATUS(g_api->resource_add(engine, &info), ENGINE_OK);
    info = Info(TEXTURE_ID, "/textures/brick", &g_texture, 0, &g_texture_lent);
    CHECK_STATUS(g_api->resource_add(engine, &info), ENGINE_OK);

    /* the id is live (so is the path of another id): a failed add of a lent buffer does not call its release */
    info = Info(TEXTURE_ID, NULL, &g_texture, 0, &duplicate_lent);
    CHECK_STATUS(g_api->resource_add(engine, &info), ENGINE_ERROR_ALREADY_EXISTS);
    info = Info(MESH_ID, "/materials/brick", &g_mesh, 0, NULL);
    CHECK_STATUS(g_api->resource_add(engine, &info), ENGINE_ERROR_ALREADY_EXISTS);
    CHECK(StillMine(&duplicate_lent) == 0 && StillMine(&torn_lent) == 0);
    CHECK(atomic_load(&g_texture_lent.released) == 0); /* still mounted: nothing handed back */

    /* what the engine holds */
    memset(&mounted, 0, sizeof(mounted));
    mounted.struct_size = sizeof(mounted);
    CHECK_STATUS(g_api->resource_mounted(engine, TEXTURE_ID, &mounted), ENGINE_OK);
    CHECK(mounted.id == TEXTURE_ID && mounted.type == ENGINE_RESOURCE_TEXTURE && mounted.size == g_texture.size &&
          mounted.loaded == 0);
    {
        int nonzero = 0;
        for (i = 0; i < 16; ++i)
        {
            nonzero |= mounted.content_hash[i] != 0;
        }
        CHECK(nonzero);
    }
    CHECK_STATUS(g_api->resource_mounted(engine, 0x777u, &mounted), ENGINE_ERROR_NOT_FOUND);
    CHECK_STATUS(g_api->resource_mounted(engine, 0, &mounted), ENGINE_ERROR_INVALID_ARGUMENT);
    mounted.struct_size = 4;
    CHECK_STATUS(g_api->resource_mounted(engine, TEXTURE_ID, &mounted), ENGINE_ERROR_STRUCT_SIZE);

    /* a hash the host promises: replacing the material with its own bytes and its own hash is accepted, another hash is
     * refused */
    mounted.struct_size = sizeof(mounted);
    CHECK_STATUS(g_api->resource_mounted(engine, MATERIAL_ID, &mounted), ENGINE_OK);
    info = Info(MATERIAL_ID, NULL, &g_material, ENGINE_RESOURCE_CHECK_HASH, NULL);
    memcpy(info.expected_hash, mounted.content_hash, 16);
    CHECK_STATUS(g_api->resource_replace(engine, &info, NULL), ENGINE_OK);
    info.expected_hash[0] ^= 1;
    CHECK_STATUS(g_api->resource_replace(engine, &info, NULL), ENGINE_ERROR_CORRUPT);
    info = Info(MATERIAL_ID, NULL, &g_texture, 0, NULL); /* the blob is another resource's */
    CHECK_STATUS(g_api->resource_replace(engine, &info, NULL), ENGINE_ERROR_CORRUPT);
    CHECK_STATUS(g_api->resource_remove(engine, 0x555u, 0, NULL), ENGINE_ERROR_NOT_FOUND);
    CHECK_STATUS(g_api->resource_remove(engine, MATERIAL_ID, 0x80, NULL), ENGINE_ERROR_INVALID_ARGUMENT);
    CHECK_STATUS(g_api->resource_remove(engine, 0, 0, NULL), ENGINE_ERROR_INVALID_ARGUMENT);
    free(duplicate_lent.blob.data);
    free(torn_lent.blob.data);
}

/* ---- loading, synchronously */

static void TestLoad(engine_handle engine)
{
    engine_handle mesh;
    engine_handle by_path;
    engine_handle bad;
    engine_resource_info info;
    engine_resource_info mounted;

    mesh = LoadSync(engine, MESH_ID, ENGINE_RESOURCE_MESH);
    CHECK(Loaded(mesh) == 1);
    memset(&info, 0, sizeof(info));
    info.struct_size = sizeof(info);
    CHECK_STATUS(g_api->resource_status(mesh, &info), ENGINE_OK);
    CHECK(info.id == MESH_ID && info.type == ENGINE_RESOURCE_MESH && info.size == g_mesh.size);
    memset(&mounted, 0, sizeof(mounted));
    mounted.struct_size = sizeof(mounted);
    CHECK_STATUS(g_api->resource_mounted(engine, MESH_ID, &mounted), ENGINE_OK);
    CHECK(mounted.loaded == 1);

    memset(&by_path, 0, sizeof(by_path));
    CHECK_STATUS(g_api->resource_load(engine, 0, S("/meshes/rock"), ENGINE_RESOURCE_MESH, &by_path), ENGINE_OK);
    CHECK(by_path.value != mesh.value); /* a handle each, one resource */
    memset(&bad, 0, sizeof(bad));
    CHECK_STATUS(g_api->resource_load(engine, 0, S("/meshes/none"), ENGINE_RESOURCE_MESH, &bad),
                 ENGINE_ERROR_NOT_FOUND);
    CHECK(bad.value == 0);
    CHECK_STATUS(g_api->resource_load(engine, 0x777u, NoPath(), ENGINE_RESOURCE_MESH, &bad), ENGINE_ERROR_NOT_FOUND);
    CHECK_STATUS(g_api->resource_load(engine, MESH_ID, NoPath(), ENGINE_RESOURCE_TEXTURE, &bad),
                 ENGINE_ERROR_TYPE_MISMATCH);
    CHECK_STATUS(g_api->resource_load(engine, MESH_ID, NoPath(), ENGINE_RESOURCE_TYPE('N', 'O', 'P', 'E'), &bad),
                 ENGINE_ERROR_UNSUPPORTED);
    CHECK_STATUS(g_api->resource_load(engine, 0, NoPath(), ENGINE_RESOURCE_MESH, &bad), ENGINE_ERROR_INVALID_ARGUMENT);
    CHECK_STATUS(g_api->resource_load(engine, MESH_ID, NoPath(), ENGINE_RESOURCE_MESH, NULL),
                 ENGINE_ERROR_INVALID_ARGUMENT);

    /* the handles are objects of the engine: it is not destroyed under them */
    CHECK_STATUS(g_api->destroy(engine), ENGINE_ERROR_IN_USE);
    CHECK_STATUS(g_api->resource_release(by_path), ENGINE_OK);
    CHECK_STATUS(g_api->resource_release(by_path), ENGINE_ERROR_STALE_HANDLE);
    CHECK_STATUS(g_api->resource_status(by_path, &info), ENGINE_ERROR_STALE_HANDLE);
    {
        engine_ticket_info ticket_info;
        memset(&ticket_info, 0, sizeof(ticket_info));
        ticket_info.struct_size = sizeof(ticket_info);
        CHECK_STATUS(g_api->ticket_poll(mesh, &ticket_info), ENGINE_ERROR_TYPE_MISMATCH);
    }
    CHECK_STATUS(g_api->resource_release(mesh), ENGINE_OK);
    CHECK_STATUS(g_api->resource_release(NoHandle()), ENGINE_ERROR_INVALID_ARGUMENT);
}

/* ---- what a loaded resource depends on: refused, forced, replaced, removed */

static Lent g_b1;
static Lent g_b2;

static void TestDependents(engine_handle engine)
{
    Lent* const b1 = &g_b1;
    Lent* const b2 = &g_b2;
    engine_handle material;
    engine_resource_add_info info;
    engine_resource_info mounted;
    engine_buffer deps;
    uint64_t ids[4];

    LentInit(b1, &g_texture_b);
    LentInit(b2, &g_texture);
    memset(&mounted, 0, sizeof(mounted));
    mounted.struct_size = sizeof(mounted);
    material = LoadSync(engine, MATERIAL_ID, ENGINE_RESOURCE_MATERIAL); /* the texture is loaded with it */

    /* refused, with the dependents named, and nothing changes */
    memset(&deps, 0, sizeof(deps));
    info = Info(TEXTURE_ID, NULL, &g_texture_b, 0, b1);
    CHECK_STATUS(g_api->resource_replace(engine, &info, &deps), ENGINE_ERROR_IN_USE);
    CHECK(DependentIds(&deps, ids, 4) == 1 && ids[0] == MATERIAL_ID);
    engine_buffer_release(&deps);
    CHECK_STATUS(g_api->resource_mounted(engine, TEXTURE_ID, &mounted), ENGINE_OK);
    CHECK(mounted.size == g_texture.size && Loaded(material) == 1);
    CHECK_STATUS(g_api->resource_remove(engine, TEXTURE_ID, 0, &deps), ENGINE_ERROR_IN_USE);
    CHECK(DependentIds(&deps, ids, 4) == 1 && ids[0] == MATERIAL_ID);
    engine_buffer_release(&deps);
    CHECK_STATUS(g_api->resource_remove(engine, TEXTURE_ID, 0, NULL), ENGINE_ERROR_IN_USE); /* no buffer asked for */
    CHECK(StillMine(b1) == 0 && atomic_load(&g_texture_lent.released) == 0);

    /* forced: the dependents are unloaded (their handles go stale), the new bytes are in, the old buffer is handed back
     * once */
    info = Info(TEXTURE_ID, NULL, &g_texture_b, ENGINE_RESOURCE_FORCE, b1);
    CHECK_STATUS(g_api->resource_replace(engine, &info, &deps), ENGINE_OK);
    CHECK(DependentIds(&deps, ids, 4) == 1 && ids[0] == MATERIAL_ID);
    engine_buffer_release(&deps);
    CHECK(Loaded(material) == 0);
    CHECK_STATUS(g_api->resource_mounted(engine, TEXTURE_ID, &mounted), ENGINE_OK);
    CHECK(mounted.size == g_texture_b.size);
    CHECK(Released(&g_texture_lent) == 1 && atomic_load(&g_texture_lent.wrong_place) == 0);
    CHECK(StillMine(b1) == 0);
    CHECK_STATUS(g_api->resource_release(material), ENGINE_OK);

    /* loaded again from the new texture, then the texture is removed by force */
    material = LoadSync(engine, MATERIAL_ID, ENGINE_RESOURCE_MATERIAL);
    CHECK(Loaded(material) == 1);
    CHECK_STATUS(g_api->resource_remove(engine, TEXTURE_ID, ENGINE_RESOURCE_FORCE, &deps), ENGINE_OK);
    CHECK(DependentIds(&deps, ids, 4) == 1 && ids[0] == MATERIAL_ID);
    engine_buffer_release(&deps);
    CHECK(Loaded(material) == 0);
    CHECK(Released(b1) == 1 && atomic_load(&b1->wrong_place) == 0);
    CHECK_STATUS(g_api->resource_mounted(engine, TEXTURE_ID, &mounted), ENGINE_ERROR_NOT_FOUND);
    CHECK_STATUS(g_api->resource_release(material), ENGINE_OK);
    {
        engine_handle gone;
        memset(&gone, 0, sizeof(gone));
        CHECK_STATUS(g_api->resource_load(engine, MATERIAL_ID, NoPath(), ENGINE_RESOURCE_MATERIAL, &gone),
                     ENGINE_ERROR_MISSING_DEPENDENCY);
        CHECK(gone.value == 0);
    }

    /* the texture comes back (lent again), and the material loads */
    info = Info(TEXTURE_ID, "/textures/brick", &g_texture, 0, b2);
    CHECK_STATUS(g_api->resource_add(engine, &info), ENGINE_OK);
    material = LoadSync(engine, MATERIAL_ID, ENGINE_RESOURCE_MATERIAL);
    CHECK(Loaded(material) == 1);
    CHECK_STATUS(g_api->resource_release(material), ENGINE_OK);
    free(b1->blob.data);
}

/* ---- a texture is replaced while frames run and a handle to it is held */

static atomic_int g_frames_stop;
static atomic_int g_frames_run;

static void* FrameThread(void* user)
{
    engine_handle engine = *(engine_handle*)user;
    engine_frame_info frame;
    memset(&frame, 0, sizeof(frame));
    frame.struct_size = sizeof(frame);
    frame.delta_seconds = 0.016;
    while (!atomic_load(&g_frames_stop))
    {
        CHECK_STATUS(g_api->frame(engine, &frame, NULL), ENGINE_OK);
        atomic_fetch_add(&g_frames_run, 1);
    }
    g_api->thread_detach();
    return NULL;
}

#define FLIPS 24

static void TestReplaceInFlight(engine_handle engine)
{
    Lent lent[FLIPS];
    engine_handle texture;
    engine_resource_add_info info;
    pthread_t thread;
    int i;

    /* the texture mounted so far (copy it out of the way: remove it, its lent buffer is handed back, then add a copy)
     */
    CHECK_STATUS(g_api->resource_remove(engine, MATERIAL_ID, 0, NULL), ENGINE_OK);
    CHECK_STATUS(g_api->resource_remove(engine, TEXTURE_ID, 0, NULL), ENGINE_OK);
    CHECK(Released(&g_b2) == 1 && atomic_load(&g_b2.wrong_place) == 0); /* the texture TestDependents put back */
    free(g_b2.blob.data);
    info = Info(TEXTURE_ID, "/textures/brick", &g_texture, 0, NULL);
    CHECK_STATUS(g_api->resource_add(engine, &info), ENGINE_OK);
    texture = LoadSync(engine, TEXTURE_ID, ENGINE_RESOURCE_TEXTURE);
    CHECK(Loaded(texture) == 1);

    atomic_store(&g_frames_stop, 0);
    atomic_store(&g_frames_run, 0);
    CHECK(pthread_create(&thread, NULL, &FrameThread, &engine) == 0);
    for (i = 0; i < FLIPS; ++i)
    {
        LentInit(&lent[i], (i % 2) == 0 ? &g_texture_b : &g_texture);
        info = Info(TEXTURE_ID, NULL, &lent[i].blob, 0, &lent[i]);
        CHECK_STATUS(g_api->resource_replace(engine, &info, NULL), ENGINE_OK);
        CHECK(Loaded(texture) == 1); /* reloaded in place: the handle stays valid */
        CHECK(atomic_load(&lent[i].released) == 0);
        if (i > 0)
        {
            CHECK(Released(&lent[i - 1]) == 1); /* the one before is handed back once its last reader is done */
        }
    }
    atomic_store(&g_frames_stop, 1);
    pthread_join(thread, NULL);
    CHECK(atomic_load(&g_frames_run) > 0);
    CHECK_STATUS(g_api->resource_release(texture), ENGINE_OK);
    CHECK_STATUS(g_api->resource_remove(engine, TEXTURE_ID, 0, NULL), ENGINE_OK);
    for (i = 0; i < FLIPS; ++i)
    {
        CHECK(Released(&lent[i]) == 1 && atomic_load(&lent[i].wrong_place) == 0);
        free(lent[i].blob.data);
    }
    CHECK_STATUS(MountedStatus(engine, TEXTURE_ID), ENGINE_ERROR_NOT_FOUND);
}

/* ---- tickets */

static int WaitTicket(engine_handle ticket)
{
    engine_ticket_info info;
    int i;
    for (i = 0; i < 3000; ++i)
    {
        memset(&info, 0, sizeof(info));
        info.struct_size = sizeof(info);
        CHECK_STATUS(g_api->ticket_poll(ticket, &info), ENGINE_OK);
        if (info.done)
        {
            return 1;
        }
        usleep(1000);
    }
    return 0;
}

static void TestAsync(engine_handle engine)
{
    engine_handle ticket;
    engine_handle resource;
    engine_handle second;
    engine_handle missing;
    engine_resource_add_info info;
    engine_ticket_info ticket_info;

    info = Info(TEXTURE_ID, "/textures/brick", &g_texture, 0, NULL);
    CHECK_STATUS(g_api->resource_add(engine, &info), ENGINE_OK);
    info = Info(MATERIAL_ID, "/materials/brick", &g_material, 0, NULL); /* TestReplaceInFlight removed both */
    CHECK_STATUS(g_api->resource_add(engine, &info), ENGINE_OK);

    /* by id: poll until done, finish into a handle, finish again for a second one */
    memset(&ticket, 0, sizeof(ticket));
    CHECK_STATUS(
        g_api->resource_load_async(engine, MESH_ID, NoPath(), ENGINE_RESOURCE_MESH, ENGINE_LOAD_NORMAL, &ticket),
        ENGINE_OK);
    CHECK(WaitTicket(ticket));
    CHECK_STATUS(g_api->ticket_wait(ticket, 0), ENGINE_OK);
    CHECK_STATUS(g_api->ticket_finish(ticket, &resource), ENGINE_OK);
    CHECK(Loaded(resource) == 1);
    CHECK_STATUS(g_api->ticket_finish(ticket, &second), ENGINE_OK);
    CHECK(second.value != resource.value && Loaded(second) == 1);
    CHECK_STATUS(g_api->resource_release(resource), ENGINE_OK);
    CHECK_STATUS(g_api->resource_release(second), ENGINE_OK);
    CHECK_STATUS(g_api->ticket_destroy(ticket), ENGINE_OK);
    CHECK_STATUS(g_api->ticket_destroy(ticket), ENGINE_ERROR_STALE_HANDLE);
    CHECK_STATUS(g_api->ticket_wait(ticket, 0), ENGINE_ERROR_STALE_HANDLE);

    /* by path, waiting instead of polling; a closure (the material and its texture) */
    CHECK_STATUS(g_api->resource_load_async(engine, 0, S("/materials/brick"), ENGINE_RESOURCE_MATERIAL,
                                            ENGINE_LOAD_IMMEDIATE, &ticket),
                 ENGINE_OK);
    CHECK_STATUS(g_api->ticket_wait(ticket, 10000), ENGINE_OK);
    memset(&ticket_info, 0, sizeof(ticket_info));
    ticket_info.struct_size = sizeof(ticket_info);
    CHECK_STATUS(g_api->ticket_poll(ticket, &ticket_info), ENGINE_OK);
    CHECK(ticket_info.done == 1 && ticket_info.blobs_read >= 2 && ticket_info.blobs_read == ticket_info.blobs_total);
    CHECK_STATUS(g_api->ticket_finish(ticket, &resource), ENGINE_OK);
    CHECK_STATUS(g_api->resource_release(resource), ENGINE_OK);
    CHECK_STATUS(g_api->ticket_destroy(ticket), ENGINE_OK);

    /* a load that fails reports it at finish, and no handle comes out */
    CHECK_STATUS(
        g_api->resource_load_async(engine, 0x777u, NoPath(), ENGINE_RESOURCE_MESH, ENGINE_LOAD_BACKGROUND, &missing),
        ENGINE_OK);
    CHECK_STATUS(g_api->ticket_wait(missing, 10000), ENGINE_OK);
    memset(&resource, 0, sizeof(resource));
    CHECK_STATUS(g_api->ticket_finish(missing, &resource), ENGINE_ERROR_NOT_FOUND);
    CHECK(resource.value == 0);
    CHECK_STATUS(g_api->ticket_destroy(missing), ENGINE_OK);

    /* a cancelled load ends CANCELLED unless it had finished already */
    CHECK_STATUS(g_api->resource_load_async(engine, MATERIAL_ID, NoPath(), ENGINE_RESOURCE_MATERIAL, ENGINE_LOAD_NORMAL,
                                            &ticket),
                 ENGINE_OK);
    CHECK_STATUS(g_api->ticket_cancel(ticket), ENGINE_OK);
    CHECK_STATUS(g_api->ticket_wait(ticket, 10000), ENGINE_OK);
    {
        const engine_status finished = g_api->ticket_finish(ticket, &resource);
        CHECK(finished == ENGINE_ERROR_CANCELLED || finished == ENGINE_OK);
        if (finished == ENGINE_OK)
        {
            CHECK_STATUS(g_api->resource_release(resource), ENGINE_OK);
        }
    }
    CHECK_STATUS(g_api->ticket_destroy(ticket), ENGINE_OK);

    /* a ticket destroyed before its load ends takes the load with it */
    CHECK_STATUS(g_api->resource_load_async(engine, MATERIAL_ID, NoPath(), ENGINE_RESOURCE_MATERIAL, ENGINE_LOAD_NORMAL,
                                            &ticket),
                 ENGINE_OK);
    CHECK_STATUS(g_api->ticket_destroy(ticket), ENGINE_OK);

    /* the bad calls */
    CHECK_STATUS(g_api->resource_load_async(engine, MESH_ID, NoPath(), ENGINE_RESOURCE_MESH, 9, &ticket),
                 ENGINE_ERROR_INVALID_ARGUMENT);
    CHECK_STATUS(g_api->resource_load_async(engine, 0, NoPath(), ENGINE_RESOURCE_MESH, ENGINE_LOAD_NORMAL, &ticket),
                 ENGINE_ERROR_INVALID_ARGUMENT);
    CHECK_STATUS(g_api->resource_load_async(engine, MESH_ID, NoPath(), ENGINE_RESOURCE_TYPE('N', 'O', 'P', 'E'),
                                            ENGINE_LOAD_NORMAL, &ticket),
                 ENGINE_ERROR_UNSUPPORTED);
    CHECK_STATUS(g_api->ticket_cancel(NoHandle()), ENGINE_ERROR_INVALID_ARGUMENT);
    CHECK_STATUS(g_api->ticket_finish(NoHandle(), &resource), ENGINE_ERROR_INVALID_ARGUMENT);
    CHECK_STATUS(g_api->resource_remove(engine, TEXTURE_ID, 0, NULL), ENGINE_OK);
    CHECK_STATUS(g_api->resource_remove(engine, MATERIAL_ID, 0, NULL), ENGINE_OK);
}

/* ---- the engine goes away holding lent buffers; a buffer overwritten by the host is noticed */

static void TestLifetime(void)
{
    Lent lent[3];
    engine_handle engine;
    engine_resource_add_info info;
    engine_handle bad;
    engine_resource_add_info planted;
    int i;

    engine = NewEngine("-5"); /* the mount's priority is a setting */
    { /* a handle of a resource on an engine that made none: refused, and no resource host is built for it */
        engine_handle forged;
        forged.value = (3ull << 56) | (1ull << 24) | 1ull;
        CHECK_STATUS(g_api->resource_release(forged), ENGINE_ERROR_INVALID_ARGUMENT);
        CHECK_STATUS(g_api->ticket_destroy(forged), ENGINE_ERROR_INVALID_ARGUMENT);
    }
    LentInit(&lent[0], &g_mesh);
    LentInit(&lent[1], &g_texture);
    LentInit(&lent[2], &g_texture_b);
    info = Info(MESH_ID, NULL, &lent[0].blob, 0, &lent[0]);
    CHECK_STATUS(g_api->resource_add(engine, &info), ENGINE_OK);
    info = Info(TEXTURE_ID, NULL, &lent[1].blob, 0, &lent[1]);
    CHECK_STATUS(g_api->resource_add(engine, &info), ENGINE_OK);
    info = Info(TEXTURE_ID, NULL, &lent[2].blob, 0, &lent[2]); /* refused: never handed back */
    CHECK_STATUS(g_api->resource_add(engine, &info), ENGINE_ERROR_ALREADY_EXISTS);
    {
        engine_handle mesh = LoadSync(engine, MESH_ID, ENGINE_RESOURCE_MESH);
        CHECK(Loaded(mesh) == 1);
        CHECK_STATUS(g_api->resource_release(mesh), ENGINE_OK);
    }
    CHECK_STATUS(g_api->destroy(engine), ENGINE_OK);
    /* destroy ran every pending release before it returned, each once, on an engine worker */
    CHECK(atomic_load(&lent[0].released) == 1 && atomic_load(&lent[1].released) == 1);
    CHECK(atomic_load(&lent[2].released) == 0);
    CHECK(atomic_load(&lent[0].wrong_place) == 0 && atomic_load(&lent[1].wrong_place) == 0);
    usleep(5000);
    CHECK(atomic_load(&lent[0].released) == 1 && atomic_load(&lent[1].released) == 1);
    for (i = 0; i < 3; ++i)
    {
        free(lent[i].blob.data);
    }

    /* the planted misuse: the host reuses a lent buffer while the engine still reads it (as freed memory that was
       reused would look): the load finds a blob that is not one and fails with CORRUPT (under ASan, reading really
       freed memory is the sanitizer's report instead) */
    engine = NewEngine(NULL);
    LentInit(&lent[0], &g_mesh);
    planted = Info(MESH_ID, NULL, &lent[0].blob, 0, &lent[0]);
    CHECK_STATUS(g_api->resource_add(engine, &planted), ENGINE_OK);
    memset(lent[0].blob.data, 0xDD, lent[0].blob.size);
    memset(&bad, 0, sizeof(bad));
    CHECK_STATUS(g_api->resource_load(engine, MESH_ID, NoPath(), ENGINE_RESOURCE_MESH, &bad), ENGINE_ERROR_CORRUPT);
    CHECK(bad.value == 0);
    CHECK_STATUS(g_api->resource_remove(engine, MESH_ID, 0, NULL), ENGINE_OK);
    CHECK(Released(&lent[0]) == 1);
    CHECK_STATUS(g_api->destroy(engine), ENGINE_OK);
    free(lent[0].blob.data);
}

int main(void)
{
    engine_handle engine;
    g_main_thread = pthread_self();
    atomic_init(&g_failures, 0);
    if (engine_get_api(ENGINE_ABI_VERSION_MAJOR, ENGINE_ABI_VERSION_MINOR, sizeof(engine_api), &g_api) != ENGINE_OK)
    {
        printf("host_resources: no function table\n");
        return 2;
    }
    g_texture = ReadBlob("brick.tex.blob");
    g_texture_b = ReadBlob("brick_b.tex.blob");
    g_material = ReadBlob("brick.mat.blob");
    g_mesh = ReadBlob("rock.mesh.blob");
    if (g_texture.data == NULL || g_texture_b.data == NULL || g_material.data == NULL || g_mesh.data == NULL)
    {
        return 2;
    }
    LentInit(&g_texture_lent, &g_texture);

    engine = NewEngine(NULL);
    TestAddAndMounted(engine);
    TestLoad(engine);
    TestDependents(engine);
    TestReplaceInFlight(engine);
    TestAsync(engine);
    CHECK_STATUS(g_api->destroy(engine), ENGINE_OK);
    TestLifetime();

    if (atomic_load(&g_failures) != 0)
    {
        printf("host_resources: %d checks failed\n", atomic_load(&g_failures));
        return 1;
    }
    printf("host_resources: ok\n");
    return 0;
}
