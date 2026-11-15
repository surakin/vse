/* Phase 24 Day 359: a C program that owns an X11 window embeds the engine, adds a mesh, a material and a texture from
   memory, puts an entity with a mesh reference in a world, binds the world to its surface and has the engine draw it.
   It checks the frame against a golden image (the rule of the spec: no channel off by more than 4, at most 0.5 % of the
   values off by more than 1), a reference that does not resolve (counted, draws nothing), a world destroyed under the
   surface (back to the background color, with the reason), unbinding, several surfaces on one world, and the arguments.
   Set ENGINE_UPDATE_GOLDEN=1 to write the golden image instead of comparing. */
#include <stdint.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <unistd.h>

#include <X11/Xlib.h>

#include "engine/engine_c.h"

#define WIDTH 160u
#define HEIGHT 120u
#define MESH_ID 0x0b03ull
#define LATE_MESH_ID 0x0b04ull
#define MATERIAL_ID 0x0e01ull
#define TEXTURE_ID 0x0d01ull

static int g_failures = 0;
static const engine_api* g_api = NULL;

#define CHECK(condition)                                                                                               \
    do                                                                                                                 \
    {                                                                                                                  \
        if (!(condition))                                                                                              \
        {                                                                                                              \
            printf("host_scene: line %d: CHECK failed: %s\n", __LINE__, #condition);                                   \
            ++g_failures;                                                                                              \
        }                                                                                                              \
    } while (0)

#define CHECK_STATUS(call, expected)                                                                                   \
    do                                                                                                                 \
    {                                                                                                                  \
        const engine_status got_ = (call);                                                                             \
        if (got_ != (expected))                                                                                        \
        {                                                                                                              \
            printf("host_scene: line %d: %s returned %d, expected %d\n", __LINE__, #call, (int)got_, (int)(expected)); \
            ++g_failures;                                                                                              \
        }                                                                                                              \
    } while (0)

typedef struct Blob
{
    unsigned char* data;
    size_t size;
} Blob;

static Blob ReadFile(const char* directory, const char* name)
{
    char path[1024];
    Blob blob = {NULL, 0};
    FILE* f;
    snprintf(path, sizeof(path), "%s/%s", directory, name);
    f = fopen(path, "rb");
    if (f == NULL)
    {
        printf("host_scene: cannot open %s\n", path);
        ++g_failures;
        return blob;
    }
    fseek(f, 0, SEEK_END);
    blob.size = (size_t)ftell(f);
    fseek(f, 0, SEEK_SET);
    blob.data = (unsigned char*)malloc(blob.size);
    if (fread(blob.data, 1, blob.size, f) != blob.size)
    {
        printf("host_scene: short read of %s\n", path);
        ++g_failures;
    }
    fclose(f);
    return blob;
}

static void AddBlob(engine_handle engine, uint64_t id, const Blob* blob)
{
    engine_resource_add_info info;
    memset(&info, 0, sizeof(info));
    info.struct_size = sizeof(info);
    info.id = id;
    info.bytes = blob->data;
    info.size = blob->size;
    CHECK_STATUS(g_api->resource_add(engine, &info), ENGINE_OK);
}

static engine_status FrameAndRead(engine_handle engine, engine_handle surface, engine_pixels* pixels)
{
    engine_handle ticket;
    engine_frame_info frame;
    engine_status status;
    memset(&ticket, 0, sizeof(ticket));
    memset(&frame, 0, sizeof(frame));
    frame.struct_size = sizeof(frame);
    frame.delta_seconds = 0.016;
    if (pixels->data.data != NULL)
    {
        engine_buffer_release(&pixels->data); /* the previous frame's pixels */
    }
    memset(pixels, 0, sizeof(*pixels));
    pixels->struct_size = sizeof(*pixels);
    status = g_api->surface_read_pixels(surface, &ticket);
    if (status != ENGINE_OK)
    {
        return status;
    }
    CHECK_STATUS(g_api->frame(engine, &frame, NULL), ENGINE_OK);
    status = g_api->pixels_finish(ticket, pixels);
    CHECK_STATUS(g_api->pixels_destroy(ticket), ENGINE_OK);
    return status;
}

static const unsigned char* Pixel(const engine_pixels* pixels, unsigned x, unsigned y)
{
    return (const unsigned char*)pixels->data.data + (size_t)y * pixels->stride + (size_t)x * 4;
}

/* The pixels of the surface's window are not the clear color anywhere near the middle. */
static int HasObject(const engine_pixels* pixels, const unsigned char* clear)
{
    unsigned x;
    unsigned y;
    int different = 0;
    for (y = pixels->height / 3; y < 2 * pixels->height / 3; ++y)
    {
        for (x = pixels->width / 3; x < 2 * pixels->width / 3; ++x)
        {
            const unsigned char* p = Pixel(pixels, x, y);
            if (abs(p[0] - clear[0]) > 8 || abs(p[1] - clear[1]) > 8 || abs(p[2] - clear[2]) > 8)
            {
                ++different;
            }
        }
    }
    return different > 50;
}

static int AllClear(const engine_pixels* pixels, const unsigned char* clear)
{
    unsigned x;
    unsigned y;
    for (y = 0; y < pixels->height; ++y)
    {
        for (x = 0; x < pixels->width; ++x)
        {
            const unsigned char* p = Pixel(pixels, x, y);
            if (abs(p[0] - clear[0]) > 2 || abs(p[1] - clear[1]) > 2 || abs(p[2] - clear[2]) > 2)
            {
                return 0;
            }
        }
    }
    return 1;
}

/* The golden rule: no channel off by more than 4, at most 0.5 % of the values off by more than 1. */
static void CompareOrWriteGolden(const engine_pixels* pixels)
{
    const char* update = getenv("ENGINE_UPDATE_GOLDEN");
    const size_t bytes = (size_t)WIDTH * HEIGHT * 4;
    unsigned char* expected;
    FILE* f;
    size_t off_by_more_than_one = 0;
    size_t worst = 0;
    size_t i;
    unsigned y;
    CHECK(pixels->width == WIDTH && pixels->height == HEIGHT);
    if (pixels->width != WIDTH || pixels->height != HEIGHT)
    {
        return;
    }
    if (update != NULL && update[0] == '1')
    {
        f = fopen(GOLDEN_FILE, "wb");
        if (f != NULL)
        {
            for (y = 0; y < HEIGHT; ++y)
            {
                fwrite(Pixel(pixels, 0, y), 1, (size_t)WIDTH * 4, f);
            }
            fclose(f);
            printf("host_scene: wrote %s\n", GOLDEN_FILE);
        }
        return;
    }
    f = fopen(GOLDEN_FILE, "rb");
    if (f == NULL)
    {
        printf("host_scene: no golden image %s (ENGINE_UPDATE_GOLDEN=1 writes it)\n", GOLDEN_FILE);
        ++g_failures;
        return;
    }
    expected = (unsigned char*)malloc(bytes);
    if (fread(expected, 1, bytes, f) != bytes)
    {
        printf("host_scene: the golden image is short\n");
        ++g_failures;
    }
    fclose(f);
    for (y = 0; y < HEIGHT; ++y)
    {
        const unsigned char* row = Pixel(pixels, 0, y);
        for (i = 0; i < (size_t)WIDTH * 4; ++i)
        {
            const int d = abs((int)row[i] - (int)expected[(size_t)y * WIDTH * 4 + i]);
            if (d > 1)
            {
                ++off_by_more_than_one;
            }
            if ((size_t)d > worst)
            {
                worst = (size_t)d;
            }
        }
    }
    printf("host_scene: against the golden image: largest difference %zu, %zu values off by more than 1\n", worst,
           off_by_more_than_one);
    CHECK(worst <= 4);
    CHECK(off_by_more_than_one * 200 <= bytes);
    free(expected);
}

static unsigned long long Checksum(const engine_pixels* pixels)
{
    unsigned long long sum = 1469598103934665603ull;
    unsigned x;
    unsigned y;
    for (y = 0; y < pixels->height; ++y)
    {
        for (x = 0; x < pixels->width * 4; ++x)
        {
            sum = (sum ^ Pixel(pixels, 0, y)[x]) * 1099511628211ull;
        }
    }
    return sum;
}

static engine_status SetCamera(engine_handle world, uint64_t entity, float fov, unsigned flags, float aspect)
{
    engine_camera camera;
    memset(&camera, 0, sizeof(camera));
    camera.struct_size = sizeof(camera);
    camera.flags = flags;
    camera.fov_y = fov;
    camera.near_plane = 0.1f;
    camera.far_plane = 100.0f;
    camera.aspect = aspect;
    return g_api->component_set(world, entity, ENGINE_COMPONENT_CAMERA, &camera);
}

static engine_status SetMeshReference(engine_handle world, uint64_t entity, uint64_t mesh, uint64_t material)
{
    engine_mesh_reference reference;
    memset(&reference, 0, sizeof(reference));
    reference.struct_size = sizeof(reference);
    reference.mesh = mesh;
    reference.material_resource = material;
    return g_api->component_set(world, entity, ENGINE_COMPONENT_MESH_REFERENCE, &reference);
}

static void PlaceAt(engine_handle world, uint64_t entity, float x, float y, float z)
{
    engine_transform t;
    memset(&t, 0, sizeof(t));
    t.struct_size = sizeof(t);
    t.position[0] = x;
    t.position[1] = y;
    t.position[2] = z;
    t.rotation[3] = 1.0f;
    t.scale[0] = t.scale[1] = t.scale[2] = 1.0f;
    CHECK_STATUS(g_api->component_set(world, entity, ENGINE_COMPONENT_TRANSFORM, &t), ENGINE_OK);
}

int main(void)
{
    Display* display;
    Window window;
    engine_create_info create;
    engine_handle engine;
    engine_handle world;
    engine_handle surface;
    engine_handle second;
    engine_handle none;
    engine_surface_info info;
    engine_surface_state state;
    engine_info about;
    engine_pixels pixels;
    uint64_t entity;
    uint64_t stray;
    uint64_t cam;
    Blob texture;
    Blob material;
    Blob mesh;
    Blob late;
    const float background[4] = {0.0f, 0.0f, 0.4f, 1.0f};
    unsigned char clear[3];
    setvbuf(stdout, NULL, _IONBF, 0);
    memset(&pixels, 0, sizeof(pixels));
    if (engine_get_api(ENGINE_ABI_VERSION_MAJOR, ENGINE_ABI_VERSION_MINOR, sizeof(engine_api), &g_api) != ENGINE_OK)
    {
        printf("host_scene: engine_get_api failed\n");
        return 2;
    }
    display = XOpenDisplay(NULL);
    if (display == NULL)
    {
        printf("host_scene: skipped: no X display\n");
        return 0;
    }
    memset(&create, 0, sizeof(create));
    create.struct_size = sizeof(create);
    create.flags = ENGINE_CREATE_LOG_SUPPRESS_CONSOLE;
    memset(&engine, 0, sizeof(engine));
    CHECK_STATUS(g_api->create(&create, &engine), ENGINE_OK);
    memset(&about, 0, sizeof(about));
    about.struct_size = sizeof(about);
    CHECK_STATUS(g_api->get_info(engine, &about), ENGINE_OK);
    if ((about.caps & ENGINE_CAP_SURFACE_X11) == 0 || (about.caps & ENGINE_CAP_WORLD_ON_SURFACE) == 0)
    {
        printf("host_scene: skipped: the library has no X11 surface or world-on-surface support\n");
        CHECK_STATUS(g_api->destroy(engine), ENGINE_OK);
        XCloseDisplay(display);
        return 0;
    }
    window = XCreateSimpleWindow(display, DefaultRootWindow(display), 0, 0, WIDTH, HEIGHT, 0, 0, 0);
    XMapWindow(display, window);
    XSync(display, False);
    memset(&info, 0, sizeof(info));
    info.struct_size = sizeof(info);
    info.kind = ENGINE_SURFACE_X11;
    info.width = WIDTH;
    info.height = HEIGHT;
    info.scale = 1.0f;
    info.display = display;
    info.window_id = (uint64_t)window;
    memset(&surface, 0, sizeof(surface));
    if (g_api->surface_create(engine, &info, &surface) != ENGINE_OK)
    {
        printf("host_scene: skipped: no device can present to this window\n");
        CHECK_STATUS(g_api->destroy(engine), ENGINE_OK);
        XCloseDisplay(display);
        return 0;
    }
    CHECK_STATUS(g_api->surface_set_background(surface, background), ENGINE_OK);
    clear[0] = 0;
    clear[1] = 0;
    clear[2] = 0; /* read from the first, empty frame below: the swapchain's own encoding of the clear color */

    texture = ReadFile(RES_DATA_DIR, "brick.tex.blob");
    material = ReadFile(RES_DATA_DIR, "brick.mat.blob");
    mesh = ReadFile(RES_DATA_DIR, "scene.mesh.blob");
    late = ReadFile(RES_DATA_DIR, "late.mesh.blob");
    if (texture.data == NULL || material.data == NULL || mesh.data == NULL || late.data == NULL)
    {
        return 1;
    }
    AddBlob(engine, TEXTURE_ID, &texture);
    AddBlob(engine, MATERIAL_ID, &material);
    AddBlob(engine, MESH_ID, &mesh);

    memset(&world, 0, sizeof(world));
    CHECK_STATUS(g_api->world_create(engine, &world), ENGINE_OK);
    CHECK_STATUS(g_api->entity_create(world, &entity), ENGINE_OK);
    PlaceAt(world, entity, 0.0f, 0.0f, 0.0f);
    CHECK_STATUS(SetMeshReference(world, entity, MESH_ID, MATERIAL_ID), ENGINE_OK);

    /* the arguments */
    memset(&none, 0, sizeof(none));
    CHECK_STATUS(g_api->surface_set_world(none, world), ENGINE_ERROR_INVALID_ARGUMENT);
    CHECK_STATUS(g_api->surface_set_world(world, world), ENGINE_ERROR_TYPE_MISMATCH);     /* not a surface */
    CHECK_STATUS(g_api->surface_set_world(surface, surface), ENGINE_ERROR_TYPE_MISMATCH); /* not a world */

    /* before the world is bound: the clear color */
    CHECK_STATUS(FrameAndRead(engine, surface, &pixels), ENGINE_OK);
    clear[0] = Pixel(&pixels, 0, 0)[0];
    clear[1] = Pixel(&pixels, 0, 0)[1];
    clear[2] = Pixel(&pixels, 0, 0)[2];
    CHECK(AllClear(&pixels, clear));
    CHECK(clear[2] > 100 && clear[0] < 10);

    /* the world on the surface */
    CHECK_STATUS(g_api->surface_set_world(surface, world), ENGINE_OK);
    CHECK_STATUS(FrameAndRead(engine, surface, &pixels), ENGINE_OK);
    CHECK(HasObject(&pixels, clear));
    CompareOrWriteGolden(&pixels);
    memset(&state, 0, sizeof(state));
    state.struct_size = sizeof(state);
    CHECK_STATUS(g_api->surface_state(surface, &state), ENGINE_OK);
    CHECK(state.world_status == ENGINE_OK);
    CHECK(state.objects_drawn == 1);
    CHECK(state.entities_unresolved == 0);
    CHECK(state.visible == 1);

    /* an entity whose mesh does not exist draws nothing and is counted; its neighbour still draws */
    CHECK_STATUS(g_api->entity_create(world, &stray), ENGINE_OK);
    PlaceAt(world, stray, 2.0f, 0.0f, 0.0f);
    CHECK_STATUS(SetMeshReference(world, stray, 0xdeadull, 0), ENGINE_OK);
    CHECK_STATUS(FrameAndRead(engine, surface, &pixels), ENGINE_OK);
    CHECK_STATUS(g_api->surface_state(surface, &state), ENGINE_OK);
    CHECK(state.objects_drawn == 1);
    CHECK(state.entities_unresolved == 1);
    /* a mesh the host adds after the entity names it: the next frame resolves it (a failed lookup is not remembered) */
    CHECK_STATUS(SetMeshReference(world, stray, LATE_MESH_ID, 0), ENGINE_OK);
    CHECK_STATUS(FrameAndRead(engine, surface, &pixels), ENGINE_OK);
    CHECK_STATUS(g_api->surface_state(surface, &state), ENGINE_OK);
    CHECK(state.entities_unresolved == 1);
    AddBlob(engine, LATE_MESH_ID, &late);
    CHECK_STATUS(FrameAndRead(engine, surface, &pixels), ENGINE_OK);
    CHECK_STATUS(g_api->surface_state(surface, &state), ENGINE_OK);
    CHECK(state.objects_drawn == 2);
    CHECK(state.entities_unresolved == 0);
    CHECK_STATUS(SetMeshReference(world, stray, MESH_ID, 0xdeadull),
                 ENGINE_OK); /* a mesh that exists, a material that does not */
    CHECK_STATUS(FrameAndRead(engine, surface, &pixels), ENGINE_OK);
    CHECK_STATUS(g_api->surface_state(surface, &state), ENGINE_OK);
    CHECK(state.objects_drawn == 1);
    CHECK(state.entities_unresolved == 1);
    CHECK_STATUS(SetMeshReference(world, stray, MESH_ID, 0), ENGINE_OK); /* the default material: it draws */
    CHECK_STATUS(FrameAndRead(engine, surface, &pixels), ENGINE_OK);
    CHECK_STATUS(g_api->surface_state(surface, &state), ENGINE_OK);
    CHECK(state.objects_drawn == 2);
    CHECK(state.entities_unresolved == 0);
    CHECK_STATUS(g_api->entity_destroy(world, stray), ENGINE_OK);

    /* Day 360: a camera entity decides what the surface shows */
    {
        float origin[3];
        float direction[3];
        float point[3] = {1.0f, 0.0f, 0.0f};
        float out[3];
        float again[3];
        float far_point[3];
        unsigned long long before;
        engine_camera read_back;
        CHECK_STATUS(g_api->entity_create(world, &cam), ENGINE_OK);
        PlaceAt(world, cam, 0.0f, 0.0f, 4.0f);
        CHECK_STATUS(SetCamera(world, cam, 0.0f, 0, 1.0f), ENGINE_ERROR_INVALID_ARGUMENT); /* fov 0 */
        CHECK_STATUS(SetCamera(world, cam, 1.0471976f, 0x80u, 1.0f), ENGINE_ERROR_INVALID_ARGUMENT);
        CHECK_STATUS(SetCamera(world, cam, 1.0471976f, ENGINE_CAMERA_FIXED_ASPECT, 0.0f),
                     ENGINE_ERROR_INVALID_ARGUMENT);
        CHECK_STATUS(SetCamera(world, cam, 1.0471976f, 0, 1.0f), ENGINE_OK);
        memset(&read_back, 0, sizeof(read_back));
        read_back.struct_size = sizeof(read_back);
        CHECK_STATUS(g_api->component_get(world, cam, ENGINE_COMPONENT_CAMERA, &read_back), ENGINE_OK);
        CHECK(read_back.fov_y > 1.04f && read_back.fov_y < 1.05f && read_back.far_plane == 100.0f);
        CHECK_STATUS(g_api->surface_set_camera(none, cam), ENGINE_ERROR_INVALID_ARGUMENT);
        CHECK_STATUS(g_api->surface_set_camera(world, cam), ENGINE_ERROR_TYPE_MISMATCH);
        CHECK_STATUS(FrameAndRead(engine, surface, &pixels), ENGINE_OK);
        before = Checksum(&pixels);
        CHECK_STATUS(g_api->surface_set_camera(surface, cam), ENGINE_OK);
        CHECK_STATUS(FrameAndRead(engine, surface, &pixels), ENGINE_OK);
        CHECK(Checksum(&pixels) != before); /* another camera, another picture */
        CHECK(HasObject(&pixels, clear));
        CHECK_STATUS(g_api->surface_state(surface, &state), ENGINE_OK);
        CHECK(state.camera_status == ENGINE_OK && state.camera_entity == cam);
        /* analytic: the camera at (0,0,4) looks at the origin with a 60 degree field and a 4:3 picture */
        CHECK_STATUS(g_api->surface_project(surface, point, out), ENGINE_OK);
        CHECK(fabsf(out[0] - 109.98f) < 0.5f && fabsf(out[1] - 60.0f) < 0.5f && fabsf(out[2] - 4.0f) < 0.01f);
        point[0] = 0.0f;
        CHECK_STATUS(g_api->surface_project(surface, point, out), ENGINE_OK);
        CHECK(fabsf(out[0] - 80.0f) < 0.5f && fabsf(out[1] - 60.0f) < 0.5f);
        /* the ray of a pixel passes through what projects to that pixel */
        CHECK_STATUS(g_api->surface_pixel_ray(surface, 109.98f, 60.0f, origin, direction), ENGINE_OK);
        CHECK(fabsf(origin[2] - 4.0f) < 0.001f && direction[2] < 0.0f);
        far_point[0] = origin[0] + direction[0] * 4.0f;
        far_point[1] = origin[1] + direction[1] * 4.0f;
        far_point[2] = origin[2] + direction[2] * 4.0f;
        CHECK(fabsf(far_point[0] - 1.0f) < 0.02f && fabsf(far_point[1]) < 0.02f && fabsf(far_point[2]) < 0.05f);
        CHECK_STATUS(g_api->surface_project(surface, far_point, again), ENGINE_OK);
        CHECK(fabsf(again[0] - 109.98f) < 0.1f && fabsf(again[1] - 60.0f) < 0.1f);
        CHECK_STATUS(g_api->surface_pixel_ray(surface, 1.0f, 1.0f, NULL, direction), ENGINE_ERROR_INVALID_ARGUMENT);
        /* a point behind the camera has a negative distance */
        point[2] = 10.0f;
        CHECK_STATUS(g_api->surface_project(surface, point, out), ENGINE_OK);
        CHECK(out[2] < 0.0f);
        /* a moved camera changes the image */
        before = Checksum(&pixels);
        PlaceAt(world, cam, 1.5f, 0.0f, 4.0f);
        CHECK_STATUS(FrameAndRead(engine, surface, &pixels), ENGINE_OK);
        CHECK(Checksum(&pixels) != before);
        /* a fixed aspect is used as given */
        CHECK_STATUS(SetCamera(world, cam, 1.0471976f, ENGINE_CAMERA_FIXED_ASPECT, 2.0f), ENGINE_OK);
        PlaceAt(world, cam, 0.0f, 0.0f, 4.0f);
        CHECK_STATUS(FrameAndRead(engine, surface, &pixels), ENGINE_OK);
        point[0] = 1.0f;
        point[2] = 0.0f;
        CHECK_STATUS(g_api->surface_project(surface, point, out), ENGINE_OK);
        CHECK(fabsf(out[0] - 97.3f) < 1.0f); /* 0.25 / (tan 30 * 2) = 0.2165 of the half width: (0.108 + 0.5) * 160 */
        CHECK_STATUS(SetCamera(world, cam, 1.0471976f, 0, 1.0f), ENGINE_OK);
        /* a camera that loses its component: the surface draws through the default camera and says why */
        CHECK_STATUS(g_api->component_remove(world, cam, ENGINE_COMPONENT_CAMERA), ENGINE_OK);
        CHECK_STATUS(FrameAndRead(engine, surface, &pixels), ENGINE_OK);
        CHECK(HasObject(&pixels, clear));
        CHECK_STATUS(g_api->surface_state(surface, &state), ENGINE_OK);
        CHECK(state.camera_status == ENGINE_ERROR_NOT_FOUND && state.camera_entity == cam);
        CHECK_STATUS(SetCamera(world, cam, 1.0471976f, 0, 1.0f), ENGINE_OK); /* it comes back: used again */
        CHECK_STATUS(FrameAndRead(engine, surface, &pixels), ENGINE_OK);
        CHECK_STATUS(g_api->surface_state(surface, &state), ENGINE_OK);
        CHECK(state.camera_status == ENGINE_OK);
        /* a destroyed camera entity */
        CHECK_STATUS(g_api->entity_destroy(world, cam), ENGINE_OK);
        CHECK_STATUS(FrameAndRead(engine, surface, &pixels), ENGINE_OK);
        CHECK(HasObject(&pixels, clear));
        CHECK_STATUS(g_api->surface_state(surface, &state), ENGINE_OK);
        CHECK(state.camera_status == ENGINE_ERROR_NOT_FOUND);
        CHECK_STATUS(g_api->surface_set_camera(surface, 0), ENGINE_OK);
        CHECK_STATUS(FrameAndRead(engine, surface, &pixels), ENGINE_OK);
        CHECK_STATUS(g_api->surface_state(surface, &state), ENGINE_OK);
        CHECK(state.camera_status == ENGINE_OK && state.camera_entity == 0);
        CHECK_STATUS(g_api->entity_create(world, &cam), ENGINE_OK); /* one for the second surface below */
        PlaceAt(world, cam, 0.0f, 0.0f, 4.0f);
        CHECK_STATUS(SetCamera(world, cam, 1.0471976f, 0, 1.0f), ENGINE_OK);
    }

    /* a second surface on the same window size shows the same world through its own scene */
    {
        Window other = XCreateSimpleWindow(display, DefaultRootWindow(display), 0, 0, WIDTH, HEIGHT, 0, 0, 0);
        engine_surface_info other_info = info;
        XMapWindow(display, other);
        XSync(display, False);
        other_info.window_id = (uint64_t)other;
        memset(&second, 0, sizeof(second));
        CHECK_STATUS(g_api->surface_create(engine, &other_info, &second), ENGINE_OK);
        CHECK_STATUS(g_api->surface_set_background(second, background), ENGINE_OK);
        CHECK_STATUS(g_api->surface_set_world(second, world), ENGINE_OK);
        {
            float ray_origin[3];
            float ray_direction[3];
            unsigned long long first_picture;
            CHECK_STATUS(g_api->surface_pixel_ray(second, 1.0f, 1.0f, ray_origin, ray_direction),
                         ENGINE_ERROR_NOT_READY); /* no frame has drawn it yet */
            CHECK_STATUS(FrameAndRead(engine, second, &pixels), ENGINE_OK);
            CHECK(HasObject(&pixels, clear));
            first_picture = Checksum(&pixels);
            /* two surfaces, two cameras, one world */
            CHECK_STATUS(g_api->surface_set_camera(second, cam), ENGINE_OK);
            CHECK_STATUS(FrameAndRead(engine, second, &pixels), ENGINE_OK);
            CHECK(Checksum(&pixels) != first_picture);
            CHECK_STATUS(FrameAndRead(engine, surface, &pixels), ENGINE_OK);
            CHECK(Checksum(&pixels) == first_picture); /* the first surface still looks through the default camera */
        }
        CHECK_STATUS(g_api->surface_destroy(second), ENGINE_OK);
        XDestroyWindow(display, other);
    }

    /* unbound: the clear color again; bound to a world destroyed under it: the clear color and the reason */
    CHECK_STATUS(g_api->surface_set_world(surface, none), ENGINE_OK);
    CHECK_STATUS(FrameAndRead(engine, surface, &pixels), ENGINE_OK);
    CHECK(AllClear(&pixels, clear));
    CHECK_STATUS(g_api->surface_set_world(surface, world), ENGINE_OK);
    CHECK_STATUS(g_api->world_destroy(world), ENGINE_OK);
    CHECK_STATUS(FrameAndRead(engine, surface, &pixels), ENGINE_OK);
    CHECK(AllClear(&pixels, clear));
    CHECK_STATUS(g_api->surface_state(surface, &state), ENGINE_OK);
    CHECK(state.world_status == ENGINE_ERROR_STALE_HANDLE);
    CHECK_STATUS(g_api->surface_set_world(surface, world), ENGINE_ERROR_STALE_HANDLE);

    engine_buffer_release(&pixels.data);
    CHECK_STATUS(g_api->surface_destroy(surface), ENGINE_OK);
    CHECK_STATUS(g_api->destroy(engine), ENGINE_OK);
    XDestroyWindow(display, window);
    XCloseDisplay(display);
    free(texture.data);
    free(material.data);
    free(mesh.data);
    free(late.data);
    if (g_failures != 0)
    {
        printf("host_scene: %d checks failed\n", g_failures);
        return 1;
    }
    printf("host_scene: ok\n");
    return 0;
}
