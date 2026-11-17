/* Phase 24 Day 378: a C program with no window system creates surfaces of kind ENGINE_SURFACE_OFFSCREEN, binds the
   world of the two-slot mesh of host_scene.c to them, runs frames and reads the pictures back with surface_read_pixels
   (the per-frame ring of Day 379 is host_offscreen_frames.c). It checks the capability bit, the size of the picture,
   resizing (a size of 0 draws nothing), two surfaces of different sizes on one world, input, pixel rays, the
   arguments, and destroying a surface that has a read-back waiting. */
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "engine/engine_c.h"
#include "engine_c/engine_test_extension.h"

#define MESH_ID 0xb06ull
#define RED_ID 0xe02ull
#define BLUE_ID 0xe03ull

static int g_failures = 0;
static const engine_api* g_api = NULL;

#define CHECK(condition)                                                                                               \
    do                                                                                                                 \
    {                                                                                                                  \
        if (!(condition))                                                                                              \
        {                                                                                                              \
            printf("host_offscreen: line %d: CHECK failed: %s\n", __LINE__, #condition);                               \
            ++g_failures;                                                                                              \
        }                                                                                                              \
    } while (0)

#define CHECK_STATUS(call, expected)                                                                                   \
    do                                                                                                                 \
    {                                                                                                                  \
        const engine_status got_ = (call);                                                                             \
        if (got_ != (expected))                                                                                        \
        {                                                                                                              \
            printf("host_offscreen: line %d: %s returned %d, expected %d\n", __LINE__, #call, (int)got_,               \
                   (int)(expected));                                                                                   \
            ++g_failures;                                                                                              \
        }                                                                                                              \
    } while (0)

typedef struct Blob
{
    unsigned char* data;
    size_t size;
} Blob;

static Blob ReadWholeFile(const char* name)
{
    char path[1024];
    Blob blob = {NULL, 0};
    FILE* f;
    snprintf(path, sizeof(path), "%s/%s", RES_DATA_DIR, name);
    f = fopen(path, "rb");
    if (f == NULL)
    {
        printf("host_offscreen: cannot open %s\n", path);
        ++g_failures;
        return blob;
    }
    fseek(f, 0, SEEK_END);
    blob.size = (size_t)ftell(f);
    fseek(f, 0, SEEK_SET);
    blob.data = (unsigned char*)malloc(blob.size);
    if (fread(blob.data, 1, blob.size, f) != blob.size)
    {
        ++g_failures;
    }
    fclose(f);
    return blob;
}

static void AddBlob(engine_handle engine, uint64_t id, const char* file)
{
    engine_resource_add_info info;
    Blob blob = ReadWholeFile(file);
    memset(&info, 0, sizeof(info));
    info.struct_size = sizeof(info);
    info.id = id;
    info.bytes = blob.data;
    info.size = blob.size;
    CHECK_STATUS(g_api->resource_add(engine, &info), ENGINE_OK); /* the engine reads it until destroyed: not freed */
}

static engine_surface_info OffscreenInfo(uint32_t width, uint32_t height)
{
    engine_surface_info info;
    memset(&info, 0, sizeof(info));
    info.struct_size = sizeof(info);
    info.kind = ENGINE_SURFACE_OFFSCREEN;
    info.width = width;
    info.height = height;
    info.scale = 1.0f;
    return info;
}

static engine_status Frame(engine_handle engine)
{
    engine_frame_info frame;
    memset(&frame, 0, sizeof(frame));
    frame.struct_size = sizeof(frame);
    frame.delta_seconds = 0.016;
    return g_api->frame(engine, &frame, NULL);
}

/* One frame, and the picture of the surface. */
static engine_status FrameAndRead(engine_handle engine, engine_handle surface, engine_pixels* pixels)
{
    engine_handle ticket;
    engine_status status;
    memset(&ticket, 0, sizeof(ticket));
    if (pixels->data.data != NULL)
    {
        engine_buffer_release(&pixels->data);
    }
    memset(pixels, 0, sizeof(*pixels));
    pixels->struct_size = sizeof(*pixels);
    status = g_api->surface_read_pixels(surface, &ticket);
    if (status != ENGINE_OK)
    {
        return status;
    }
    CHECK_STATUS(Frame(engine), ENGINE_OK);
    status = g_api->pixels_finish(ticket, pixels);
    CHECK_STATUS(g_api->pixels_destroy(ticket), ENGINE_OK);
    return status;
}

static int CountColor(const engine_pixels* pixels, unsigned x0, unsigned x1, int red)
{
    unsigned x;
    unsigned y;
    int count = 0;
    for (y = 0; y < pixels->height; ++y)
    {
        for (x = x0; x < x1; ++x)
        {
            const unsigned char* p = (const unsigned char*)pixels->data.data + (size_t)y * pixels->stride +
                                     (size_t)x * 4;
            const int is_red = p[0] > 100 && p[1] < 60 && p[2] < 60;
            const int is_blue = p[2] > 100 && p[0] < 60 && p[1] < 60;
            count += (red ? is_red : is_blue) ? 1 : 0;
        }
    }
    return count;
}

static unsigned long long Checksum(const engine_pixels* pixels)
{
    unsigned long long sum = 1469598103934665603ull;
    size_t i;
    const unsigned char* p = (const unsigned char*)pixels->data.data;
    for (i = 0; i < (size_t)pixels->stride * pixels->height; ++i)
    {
        sum = (sum ^ p[i]) * 1099511628211ull;
    }
    return sum;
}

static void PlaceAtOrigin(engine_handle world, uint64_t entity)
{
    engine_transform t;
    memset(&t, 0, sizeof(t));
    t.struct_size = sizeof(t);
    t.rotation[3] = 1.0f;
    t.scale[0] = t.scale[1] = t.scale[2] = 1.0f;
    CHECK_STATUS(g_api->component_set(world, entity, ENGINE_COMPONENT_TRANSFORM, &t), ENGINE_OK);
}

int main(void)
{
    engine_create_info create;
    engine_handle engine;
    engine_info about;
    engine_handle surface;
    engine_handle small;
    engine_handle none;
    engine_handle world;
    uint64_t entity;
    engine_pixels pixels;
    engine_surface_state state;
    engine_surface_info info;
    engine_mesh_reference reference;
    const float background[4] = {0.0f, 0.0f, 0.4f, 1.0f};
    float origin[3];
    float direction[3];
    engine_input_event event;
    engine_handle ticket;

    CHECK_STATUS(engine_get_api(ENGINE_ABI_VERSION_MAJOR, ENGINE_ABI_VERSION_MINOR, sizeof(engine_api), &g_api),
                 ENGINE_OK);
    if (g_api == NULL)
    {
        return 1;
    }
    memset(&create, 0, sizeof(create));
    create.struct_size = sizeof(create);
    create.flags = ENGINE_CREATE_LOG_SUPPRESS_CONSOLE;
    memset(&engine, 0, sizeof(engine));
    CHECK_STATUS(g_api->create(&create, &engine), ENGINE_OK);
    memset(&about, 0, sizeof(about));
    about.struct_size = sizeof(about);
    CHECK_STATUS(g_api->get_info(engine, &about), ENGINE_OK);
    if ((about.caps & ENGINE_CAP_SURFACE_OFFSCREEN) == 0)
    {
        printf("host_offscreen: skipped: this build has no off-screen surfaces\n");
        CHECK_STATUS(g_api->destroy(engine), ENGINE_OK);
        return 0;
    }
    CHECK((about.caps & ENGINE_CAP_READBACK) != 0 && (about.caps & ENGINE_CAP_WORLD_ON_SURFACE) != 0);

    /* the arguments */
    memset(&none, 0, sizeof(none));
    info = OffscreenInfo(160, 120);
    CHECK_STATUS(g_api->surface_create(engine, NULL, &surface), ENGINE_ERROR_INVALID_ARGUMENT);
    info.struct_size = 8;
    CHECK_STATUS(g_api->surface_create(engine, &info, &surface), ENGINE_ERROR_STRUCT_SIZE);
    info = OffscreenInfo(1u << 30, 120);
    CHECK_STATUS(g_api->surface_create(engine, &info, &surface), ENGINE_ERROR_INVALID_ARGUMENT);
    info = OffscreenInfo(160, 120);
    info.flags = 0x80u;
    CHECK_STATUS(g_api->surface_create(engine, &info, &surface), ENGINE_ERROR_INVALID_ARGUMENT);

    info = OffscreenInfo(160, 120);
    memset(&surface, 0, sizeof(surface));
    if (g_api->surface_create(engine, &info, &surface) != ENGINE_OK)
    {
        printf("host_offscreen: skipped: no device could be made\n");
        CHECK_STATUS(g_api->destroy(engine), ENGINE_OK);
        return 0;
    }
    CHECK_STATUS(g_api->surface_set_background(surface, background), ENGINE_OK);

    AddBlob(engine, MESH_ID, "two.mesh.blob");
    AddBlob(engine, RED_ID, "red.mat.blob");
    AddBlob(engine, BLUE_ID, "blue.mat.blob");
    CHECK_STATUS(g_api->world_create(engine, &world), ENGINE_OK);
    CHECK_STATUS(g_api->entity_create(world, &entity), ENGINE_OK);
    PlaceAtOrigin(world, entity);
    memset(&reference, 0, sizeof(reference));
    reference.struct_size = sizeof(reference);
    reference.mesh = MESH_ID;
    reference.material_resource = RED_ID;
    CHECK_STATUS(g_api->component_set(world, entity, ENGINE_COMPONENT_MESH_REFERENCE, &reference), ENGINE_OK);

    /* no world: the picture is the background colour, the size asked for */
    memset(&pixels, 0, sizeof(pixels));
    CHECK_STATUS(FrameAndRead(engine, surface, &pixels), ENGINE_OK);
    CHECK(pixels.width == 160 && pixels.height == 120);
    CHECK(CountColor(&pixels, 0, pixels.width, 1) == 0);
    memset(&state, 0, sizeof(state));
    state.struct_size = sizeof(state);
    CHECK_STATUS(g_api->surface_state(surface, &state), ENGINE_OK);
    CHECK(state.image_width == 160 && state.image_height == 120 && state.visible == 1 && state.frames_presented >= 1);

    /* a world: the mesh is drawn, both halves red */
    CHECK_STATUS(g_api->surface_set_world(surface, world), ENGINE_OK);
    CHECK_STATUS(FrameAndRead(engine, surface, &pixels), ENGINE_OK);
    CHECK(CountColor(&pixels, 0, pixels.width / 2, 1) > 100);
    CHECK(CountColor(&pixels, pixels.width / 2, pixels.width, 1) > 100);

    /* a pixel ray works as on a window */
    CHECK_STATUS(g_api->surface_pixel_ray(surface, 80.0f, 60.0f, origin, direction), ENGINE_OK);
    CHECK(direction[2] < -0.5f);

    /* input is accepted as on a window */
    memset(&event, 0, sizeof(event));
    event.struct_size = sizeof(event);
    event.type = ENGINE_INPUT_POINTER_MOVE;
    event.x = 10;
    event.y = 20;
    CHECK_STATUS(g_api->surface_input(surface, &event), ENGINE_OK);

    /* resized: the picture follows; a size of 0 draws nothing and keeps the read-back waiting */
    CHECK_STATUS(g_api->surface_resized(surface, 80, 60, 1.0f), ENGINE_OK);
    CHECK_STATUS(FrameAndRead(engine, surface, &pixels), ENGINE_OK);
    CHECK(pixels.width == 80 && pixels.height == 60);
    CHECK(CountColor(&pixels, 0, pixels.width, 1) > 50);
    CHECK_STATUS(g_api->surface_resized(surface, 0, 0, 1.0f), ENGINE_OK);
    memset(&ticket, 0, sizeof(ticket));
    CHECK_STATUS(g_api->surface_read_pixels(surface, &ticket), ENGINE_OK);
    CHECK_STATUS(Frame(engine), ENGINE_OK);
    engine_buffer_release(&pixels.data);
    memset(&pixels, 0, sizeof(pixels));
    pixels.struct_size = sizeof(pixels);
    CHECK_STATUS(g_api->pixels_finish(ticket, &pixels), ENGINE_ERROR_NOT_READY);
    CHECK_STATUS(g_api->surface_resized(surface, 200, 100, 1.0f), ENGINE_OK);
    CHECK_STATUS(Frame(engine), ENGINE_OK);
    CHECK_STATUS(g_api->pixels_finish(ticket, &pixels), ENGINE_OK); /* the waiting ticket got the first picture */
    CHECK(pixels.width == 200 && pixels.height == 100);
    CHECK_STATUS(g_api->pixels_destroy(ticket), ENGINE_OK);

    /* the per-frame picture (Day 379): the newest finished frame, never waiting for the GPU */
    {
        engine_frame_image a;
        engine_frame_image b;
        engine_frame_image c;
        engine_frame_image bad;
        unsigned char first_pixel[4];
        int tries;
        engine_status got = ENGINE_ERROR_NOT_READY;
        memset(&a, 0, sizeof(a));
        a.struct_size = sizeof(a);
        for (tries = 0; tries < 200 && got == ENGINE_ERROR_NOT_READY; ++tries)
        {
            CHECK_STATUS(Frame(engine), ENGINE_OK);
            got = g_api->surface_acquire_frame(surface, &a);
        }
        CHECK_STATUS(got, ENGINE_OK);
        CHECK(a.width == 200 && a.height == 100 && a.stride == 800 && a.data != NULL && a.frame >= 1);
        if (a.data != NULL)
        {
            memcpy(first_pixel, a.data, 4);
        }
        /* a host that takes frames slowly: many frames pass, the newest is taken, the rest are counted */
        {
            int i;
            for (i = 0; i < 30; ++i)
            {
                CHECK_STATUS(Frame(engine), ENGINE_OK);
            }
        }
        memset(&b, 0, sizeof(b));
        b.struct_size = sizeof(b);
        got = ENGINE_ERROR_NOT_READY;
        for (tries = 0; tries < 200 && got == ENGINE_ERROR_NOT_READY; ++tries)
        {
            got = g_api->surface_acquire_frame(surface, &b);
            if (got == ENGINE_ERROR_NOT_READY)
            {
                CHECK_STATUS(Frame(engine), ENGINE_OK);
            }
        }
        CHECK_STATUS(got, ENGINE_OK);
        CHECK(b.frame > a.frame + 5);
        CHECK(b.data != a.data);
        memset(&state, 0, sizeof(state));
        state.struct_size = sizeof(state);
        CHECK_STATUS(g_api->surface_state(surface, &state), ENGINE_OK);
        CHECK(state.frames_rendered >= b.frame && state.frames_acquired == 2);
        CHECK(state.frames_dropped + state.frames_skipped_slow_host > 0);
        /* two are held: a third is refused until one is released */
        memset(&c, 0, sizeof(c));
        c.struct_size = sizeof(c);
        CHECK_STATUS(g_api->surface_acquire_frame(surface, &c), ENGINE_ERROR_LIMIT_EXCEEDED);
        /* a resize leaves the held frames at their size and unchanged */
        CHECK_STATUS(g_api->surface_resized(surface, 40, 30, 1.0f), ENGINE_OK);
        CHECK_STATUS(Frame(engine), ENGINE_OK);
        CHECK(a.width == 200 && memcmp(a.data, first_pixel, 4) == 0);
        CHECK_STATUS(g_api->surface_release_frame(surface, &a), ENGINE_OK);
        CHECK_STATUS(g_api->surface_release_frame(surface, &a), ENGINE_ERROR_INVALID_ARGUMENT);
        got = ENGINE_ERROR_NOT_READY;
        for (tries = 0; tries < 200 && got == ENGINE_ERROR_NOT_READY; ++tries)
        {
            CHECK_STATUS(Frame(engine), ENGINE_OK);
            got = g_api->surface_acquire_frame(surface, &c);
            if (got == ENGINE_OK && c.width != 40)
            { /* a frame of the old size that was still in flight: take the next */
                CHECK_STATUS(g_api->surface_release_frame(surface, &c), ENGINE_OK);
                got = ENGINE_ERROR_NOT_READY;
            }
        }
        CHECK_STATUS(got, ENGINE_OK);
        CHECK(c.width == 40 && c.height == 30 && c.stride == 160);
        CHECK_STATUS(g_api->surface_release_frame(surface, &b), ENGINE_OK);
        CHECK_STATUS(g_api->surface_release_frame(surface, &c), ENGINE_OK);
        /* arguments */
        memset(&bad, 0, sizeof(bad));
        CHECK_STATUS(g_api->surface_acquire_frame(surface, NULL), ENGINE_ERROR_INVALID_ARGUMENT);
        CHECK_STATUS(g_api->surface_acquire_frame(surface, &bad), ENGINE_ERROR_STRUCT_SIZE);
        CHECK_STATUS(g_api->surface_acquire_frame(world, &c), ENGINE_ERROR_TYPE_MISMATCH);
        CHECK_STATUS(g_api->surface_release_frame(surface, NULL), ENGINE_ERROR_INVALID_ARGUMENT);
        CHECK_STATUS(g_api->surface_resized(surface, 200, 100, 1.0f), ENGINE_OK);
        /* a frame held while the surface is destroyed is released with it */
        {
            engine_handle other;
            engine_frame_image h;
            memset(&other, 0, sizeof(other));
            info = OffscreenInfo(32, 32);
            CHECK_STATUS(g_api->surface_create(engine, &info, &other), ENGINE_OK);
            CHECK_STATUS(g_api->surface_set_world(other, world), ENGINE_OK);
            memset(&h, 0, sizeof(h));
            h.struct_size = sizeof(h);
            got = ENGINE_ERROR_NOT_READY;
            for (tries = 0; tries < 200 && got == ENGINE_ERROR_NOT_READY; ++tries)
            {
                CHECK_STATUS(Frame(engine), ENGINE_OK);
                got = g_api->surface_acquire_frame(other, &h);
            }
            CHECK_STATUS(got, ENGINE_OK);
            CHECK_STATUS(g_api->surface_destroy(other), ENGINE_OK);
            CHECK_STATUS(g_api->surface_release_frame(other, &h), ENGINE_ERROR_STALE_HANDLE);
        }
    }

    /* a second surface of another size on the same world, drawn by the same frame */
    info = OffscreenInfo(64, 48);
    memset(&small, 0, sizeof(small));
    CHECK_STATUS(g_api->surface_create(engine, &info, &small), ENGINE_OK);
    CHECK_STATUS(g_api->surface_set_world(small, world), ENGINE_OK);
    CHECK_STATUS(FrameAndRead(engine, small, &pixels), ENGINE_OK);
    CHECK(pixels.width == 64 && pixels.height == 48);
    CHECK(CountColor(&pixels, 0, pixels.width, 1) > 20);
    CHECK_STATUS(FrameAndRead(engine, surface, &pixels), ENGINE_OK);
    CHECK(pixels.width == 200 && pixels.height == 100);

    /* destroying a surface that has a read-back waiting cancels the ticket; the other surface goes on */
    memset(&ticket, 0, sizeof(ticket));
    CHECK_STATUS(g_api->surface_read_pixels(small, &ticket), ENGINE_OK);
    CHECK_STATUS(g_api->surface_destroy(small), ENGINE_OK);
    CHECK_STATUS(g_api->pixels_finish(ticket, &pixels), ENGINE_ERROR_CANCELLED);
    CHECK_STATUS(g_api->pixels_destroy(ticket), ENGINE_OK);
    CHECK_STATUS(g_api->surface_state(small, &state), ENGINE_ERROR_STALE_HANDLE);
    CHECK_STATUS(FrameAndRead(engine, surface, &pixels), ENGINE_OK);

    /* a lost device (Day 380): the next frame finds it lost, a pending read-back and the acquire fail by value, the
       rebind makes a new device and target, and the same picture comes back; a frame held across the loss stays
       readable. A second off-screen surface and the loss of both on a shared device. */
    {
        const engine_string name = {"engine.test_state", sizeof("engine.test_state") - 1};
        const struct engine_test_extension* hook = (const struct engine_test_extension*)g_api->get_extension(name);
        if (hook == NULL || hook->size < offsetof(struct engine_test_extension, surface_lose_device) + sizeof(void*))
        {
            printf("host_offscreen: the device-loss part is skipped: no test extension in this library\n");
        }
        else
        {
            unsigned long long before;
            unsigned long long held_before;
            engine_frame_image held;
            engine_handle second;
            engine_surface_info again = OffscreenInfo(200, 100);
            int tries;
            engine_status got = ENGINE_ERROR_NOT_READY;
            CHECK_STATUS(FrameAndRead(engine, surface, &pixels), ENGINE_OK);
            before = Checksum(&pixels);
            memset(&held, 0, sizeof(held));
            held.struct_size = sizeof(held);
            for (tries = 0; tries < 200 && got == ENGINE_ERROR_NOT_READY; ++tries)
            {
                CHECK_STATUS(Frame(engine), ENGINE_OK);
                got = g_api->surface_acquire_frame(surface, &held);
            }
            CHECK_STATUS(got, ENGINE_OK);
            {
                engine_pixels view;
                memset(&view, 0, sizeof(view));
                view.width = held.width;
                view.height = held.height;
                view.stride = held.stride;
                view.data.data = (void*)held.data;
                held_before = Checksum(&view);
            }
            /* a second surface on the same world, to be lost with the first when they share a device */
            info = OffscreenInfo(64, 48);
            memset(&second, 0, sizeof(second));
            CHECK_STATUS(g_api->surface_create(engine, &info, &second), ENGINE_OK);
            CHECK_STATUS(g_api->surface_set_world(second, world), ENGINE_OK);
            CHECK_STATUS(FrameAndRead(engine, second, &pixels), ENGINE_OK);

            /* a surface of size 0 still holds ring buffers of the old device: they must not outlive it */
            CHECK_STATUS(g_api->surface_resized(surface, 0, 0, 1.0f), ENGINE_OK);
            CHECK_STATUS(Frame(engine), ENGINE_OK);
            CHECK(hook->surface_lose_device(surface.value) == 1);
            CHECK_STATUS(g_api->surface_resized(surface, 200, 100, 1.0f), ENGINE_OK);
            CHECK(FrameAndRead(engine, surface, &pixels) != ENGINE_OK);
            CHECK_STATUS(g_api->surface_state(surface, &state), ENGINE_OK);
            CHECK(state.lost == 1);
            {
                engine_frame_image none_image;
                memset(&none_image, 0, sizeof(none_image));
                none_image.struct_size = sizeof(none_image);
                CHECK(g_api->surface_acquire_frame(surface, &none_image) != ENGINE_OK);
            }
            CHECK_STATUS(g_api->surface_rebind(surface, &again), ENGINE_OK);
            CHECK_STATUS(FrameAndRead(engine, surface, &pixels), ENGINE_OK);
            CHECK(Checksum(&pixels) == before);
            CHECK_STATUS(g_api->surface_state(surface, &state), ENGINE_OK);
            CHECK(state.lost == 0 && state.recoveries >= 1 && state.world_status == ENGINE_OK);
            {
                engine_pixels view;
                memset(&view, 0, sizeof(view));
                view.width = held.width;
                view.height = held.height;
                view.stride = held.stride;
                view.data.data = (void*)held.data;
                CHECK(Checksum(&view) == held_before); /* the held copy outlived the device */
            }
            CHECK_STATUS(g_api->surface_release_frame(surface, &held), ENGINE_OK);
            /* the frames of the new device come through the ring again */
            got = ENGINE_ERROR_NOT_READY;
            for (tries = 0; tries < 200 && got == ENGINE_ERROR_NOT_READY; ++tries)
            {
                CHECK_STATUS(Frame(engine), ENGINE_OK);
                got = g_api->surface_acquire_frame(surface, &held);
            }
            CHECK_STATUS(got, ENGINE_OK);
            CHECK(held.width == 200 && held.height == 100);
            CHECK_STATUS(g_api->surface_release_frame(surface, &held), ENGINE_OK);
            /* the second surface: lost or not with the first, it recovers and draws the same world */
            CHECK_STATUS(g_api->surface_state(second, &state), ENGINE_OK);
            if (state.lost != 0)
            {
                info = OffscreenInfo(64, 48);
                CHECK_STATUS(g_api->surface_rebind(second, &info), ENGINE_OK);
            }
            CHECK_STATUS(FrameAndRead(engine, second, &pixels), ENGINE_OK);
            CHECK(pixels.width == 64 && CountColor(&pixels, 0, pixels.width, 1) > 20);
            CHECK_STATUS(g_api->surface_destroy(second), ENGINE_OK);
        }
    }

    /* destroying the world under the surface: the background again, with the reason */
    CHECK_STATUS(g_api->world_destroy(world), ENGINE_OK);
    CHECK_STATUS(FrameAndRead(engine, surface, &pixels), ENGINE_OK);
    CHECK(CountColor(&pixels, 0, pixels.width, 1) == 0);
    CHECK_STATUS(g_api->surface_state(surface, &state), ENGINE_OK);
    CHECK(state.world_status == ENGINE_ERROR_STALE_HANDLE);

    engine_buffer_release(&pixels.data);
    CHECK_STATUS(g_api->surface_destroy(surface), ENGINE_OK);
    CHECK_STATUS(g_api->destroy(engine), ENGINE_OK);
    if (g_failures == 0)
    {
        printf("host_offscreen: ok\n");
        return 0;
    }
    printf("host_offscreen: %d failure(s)\n", g_failures);
    return 1;
}
