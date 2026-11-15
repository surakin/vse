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

static void PlaceRotated(engine_handle world, uint64_t entity, float x, float y, float z, const float* q)
{
    engine_transform t;
    memset(&t, 0, sizeof(t));
    t.struct_size = sizeof(t);
    t.position[0] = x;
    t.position[1] = y;
    t.position[2] = z;
    memcpy(t.rotation, q, 4 * sizeof(float));
    t.scale[0] = t.scale[1] = t.scale[2] = 1.0f;
    CHECK_STATUS(g_api->component_set(world, entity, ENGINE_COMPONENT_TRANSFORM, &t), ENGINE_OK);
}

static engine_status SetLight(engine_handle world, uint64_t entity, unsigned kind, float intensity, float radius)
{
    engine_light light;
    memset(&light, 0, sizeof(light));
    light.struct_size = sizeof(light);
    light.kind = kind;
    light.color[0] = light.color[1] = light.color[2] = 1.0f;
    light.intensity = intensity;
    light.radius = radius;
    return g_api->component_set(world, entity, ENGINE_COMPONENT_LIGHT, &light);
}

static engine_status SetEnvironment(engine_handle world, float ambient, float sky_red, float exposure)
{
    engine_environment environment;
    memset(&environment, 0, sizeof(environment));
    environment.struct_size = sizeof(environment);
    environment.ambient_color[0] = environment.ambient_color[1] = environment.ambient_color[2] = 1.0f;
    environment.ambient_intensity = ambient;
    environment.sky_color[0] = sky_red;
    environment.exposure = exposure;
    return g_api->world_set_environment(world, &environment);
}

/* The summed brightness of the 3 x 3 pixels around (x, y). */
static int Brightness(const engine_pixels* pixels, unsigned x, unsigned y)
{
    int sum = 0;
    unsigned dx;
    unsigned dy;
    for (dy = 0; dy < 3; ++dy)
    {
        for (dx = 0; dx < 3; ++dx)
        {
            const unsigned char* p = Pixel(pixels, x - 1 + dx, y - 1 + dy);
            sum += p[0] + p[1] + p[2];
        }
    }
    return sum;
}

/* The brightness of the triangle's middle, found with surface_project, after one frame. */
static int TriangleBrightness(engine_handle engine, engine_handle surface, engine_pixels* pixels)
{
    const float middle[3] = {0.0f, -0.2f, 0.0f};
    float at[3];
    CHECK_STATUS(FrameAndRead(engine, surface, pixels), ENGINE_OK);
    CHECK_STATUS(g_api->surface_project(surface, middle, at), ENGINE_OK);
    return Brightness(pixels, (unsigned)at[0], (unsigned)at[1]);
}

static engine_string Str(const char* text)
{
    engine_string s;
    s.data = text;
    s.length = strlen(text);
    return s;
}

/* The colour of the pixel the world point projects to, in the last frame the surface drew. */
static void ColorAt(engine_handle surface, const engine_pixels* pixels, float x, float y, float z, int* rgb)
{
    const float point[3] = {x, y, z};
    float at[3];
    const unsigned char* p;
    CHECK_STATUS(g_api->surface_project(surface, point, at), ENGINE_OK);
    p = Pixel(pixels, (unsigned)at[0], (unsigned)at[1]);
    rgb[0] = p[0];
    rgb[1] = p[1];
    rgb[2] = p[2];
}

static engine_status SetVisibility(engine_handle world, uint64_t entity, unsigned hidden)
{
    engine_visibility visibility;
    memset(&visibility, 0, sizeof(visibility));
    visibility.struct_size = sizeof(visibility);
    visibility.hidden = hidden;
    return g_api->component_set(world, entity, ENGINE_COMPONENT_VISIBILITY, &visibility);
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

/* The blobs of the animation block: the engine reads them until it is destroyed, so they are freed at the end. */
/* Pixels of the frame that are text (white on the blue background) within `radius` pixels of (x, y). */
static int CountText(const engine_pixels* pixels, int x, int y, int radius)
{
    int count = 0;
    int py;
    int px;
    for (py = y - radius; py <= y + radius; ++py)
    {
        for (px = x - radius; px <= x + radius; ++px)
        {
            if (px >= 0 && py >= 0 && px < (int)WIDTH && py < (int)HEIGHT)
            {
                const unsigned char* p = Pixel(pixels, (unsigned)px, (unsigned)py);
                if (p[0] > 160 && p[1] > 160)
                {
                    ++count;
                }
            }
        }
    }
    return count;
}

static engine_status SetLabel(engine_handle world, uint64_t entity, const char* text, float size, unsigned anchor,
                              unsigned flags)
{
    engine_label label;
    memset(&label, 0, sizeof(label));
    label.struct_size = sizeof(label);
    label.text = Str(text);
    label.color[0] = label.color[1] = label.color[2] = label.color[3] = 1.0f;
    label.size = size;
    label.anchor = anchor;
    label.flags = flags;
    return g_api->component_set(world, entity, ENGINE_COMPONENT_LABEL, &label);
}

static Blob g_extra[6];
#define skeleton_blob g_extra[0]
#define other_skeleton g_extra[1]
#define idle_blob g_extra[2]
#define other_clip g_extra[3]
#define hold_blob g_extra[4]
#define skinned_blob g_extra[5]

/* The matrix of a joint in the skeleton entity's space (Day 363): the height of the joint is element 13. */
static float JointHeight(engine_handle world, uint64_t entity, uint32_t joint)
{
    float matrix[16];
    memset(matrix, 0, sizeof(matrix));
    CHECK_STATUS(g_api->skeleton_joint_transform(world, entity, joint, matrix), ENGINE_OK);
    return matrix[13];
}

static float Near(float a, float b)
{
    return a > b ? a - b : b - a;
}

static engine_status Play(engine_handle world, uint64_t entity, uint64_t clip, unsigned flags, float speed, float start,
                          float fade)
{
    engine_play_params params;
    memset(&params, 0, sizeof(params));
    params.struct_size = sizeof(params);
    params.flags = flags;
    params.speed = speed;
    params.start_time = start;
    params.fade_seconds = fade;
    return g_api->animation_play(world, entity, clip, &params);
}

static engine_status SetSkeleton(engine_handle world, uint64_t entity, uint64_t skeleton)
{
    engine_skeleton_instance instance;
    memset(&instance, 0, sizeof(instance));
    instance.struct_size = sizeof(instance);
    instance.skeleton = skeleton;
    return g_api->component_set(world, entity, ENGINE_COMPONENT_SKELETON_INSTANCE, &instance);
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

    /* Day 361: lights and the environment */
    {
        uint64_t lamp;
        uint64_t many[70];
        const float turned_around[4] = {0.0f, 1.0f, 0.0f, 0.0f}; /* half a turn about Y: the light shines along +Z */
        const float upright[4] = {0.0f, 0.0f, 0.0f, 1.0f};
        int ambient_only;
        int lit;
        int unlit;
        int near_lamp;
        int far_lamp;
        int outside;
        unsigned i;
        engine_light read_back;
        engine_surface_state lit_state;
        CHECK_STATUS(g_api->entity_create(world, &lamp), ENGINE_OK);
        PlaceAt(world, lamp, 0.0f, 0.0f, 0.0f);
        CHECK_STATUS(SetLight(world, lamp, 0, 1.0f, 1.0f), ENGINE_ERROR_INVALID_ARGUMENT); /* unknown kind */
        CHECK_STATUS(SetLight(world, lamp, ENGINE_LIGHT_POINT, 1.0f, 0.0f), ENGINE_ERROR_INVALID_ARGUMENT);
        CHECK_STATUS(SetLight(world, lamp, ENGINE_LIGHT_DIRECTIONAL, -1.0f, 1.0f), ENGINE_ERROR_INVALID_ARGUMENT);
        CHECK_STATUS(SetEnvironment(world, 0.2f, 0.0f, 0.0f), ENGINE_ERROR_INVALID_ARGUMENT); /* exposure 0 */
        CHECK_STATUS(SetEnvironment(world, -1.0f, 0.0f, 1.0f), ENGINE_ERROR_INVALID_ARGUMENT);
        CHECK_STATUS(g_api->world_set_environment(world, NULL), ENGINE_ERROR_INVALID_ARGUMENT);
        CHECK_STATUS(g_api->world_set_environment(none, NULL), ENGINE_ERROR_INVALID_ARGUMENT);
        CHECK_STATUS(SetEnvironment(world, 0.0f, 0.0f, 1.0f), ENGINE_OK); /* no ambient, no default sun */
        ambient_only = TriangleBrightness(engine, surface, &pixels);
        /* a directional light along -Z lights the front of the triangle; turned around, it lights nothing */
        CHECK_STATUS(SetLight(world, lamp, ENGINE_LIGHT_DIRECTIONAL, 3.0f, 1.0f), ENGINE_OK);
        memset(&read_back, 0, sizeof(read_back));
        read_back.struct_size = sizeof(read_back);
        CHECK_STATUS(g_api->component_get(world, lamp, ENGINE_COMPONENT_LIGHT, &read_back), ENGINE_OK);
        CHECK(read_back.kind == ENGINE_LIGHT_DIRECTIONAL && read_back.intensity == 3.0f);
        lit = TriangleBrightness(engine, surface, &pixels);
        CHECK(lit > ambient_only + 30);
        PlaceRotated(world, lamp, 0.0f, 0.0f, 0.0f, turned_around);
        unlit = TriangleBrightness(engine, surface, &pixels);
        CHECK(unlit < lit - 30);
        CHECK(abs(unlit - ambient_only) < 12);
        /* a removed light is gone next frame */
        PlaceRotated(world, lamp, 0.0f, 0.0f, 0.0f, upright);
        CHECK(TriangleBrightness(engine, surface, &pixels) > ambient_only + 30);
        CHECK_STATUS(g_api->component_remove(world, lamp, ENGINE_COMPONENT_LIGHT), ENGINE_OK);
        CHECK(abs(TriangleBrightness(engine, surface, &pixels) - ambient_only) < 12);
        /* the ambient term alone lights the scene */
        CHECK_STATUS(SetEnvironment(world, 1.0f, 0.0f, 1.0f), ENGINE_OK);
        CHECK(TriangleBrightness(engine, surface, &pixels) > ambient_only + 30);
        CHECK_STATUS(SetEnvironment(world, 0.0f, 0.0f, 1.0f), ENGINE_OK);
        /* a point light in front of the triangle: bright near, weaker far, nothing beyond its radius */
        CHECK_STATUS(SetLight(world, lamp, ENGINE_LIGHT_POINT, 4.0f, 6.0f), ENGINE_OK);
        PlaceAt(world, lamp, 0.0f, 0.0f, 1.0f);
        near_lamp = TriangleBrightness(engine, surface, &pixels);
        PlaceAt(world, lamp, 0.0f, 0.0f, 4.0f);
        far_lamp = TriangleBrightness(engine, surface, &pixels);
        PlaceAt(world, lamp, 0.0f, 0.0f, 9.0f);
        outside = TriangleBrightness(engine, surface, &pixels);
        CHECK(near_lamp > far_lamp + 10);
        CHECK(far_lamp > outside + 10);
        CHECK(abs(outside - ambient_only) < 12);
        CHECK_STATUS(g_api->component_remove(world, lamp, ENGINE_COMPONENT_LIGHT), ENGINE_OK);
        /* the sky colour is the background, and the exposure scales what is drawn */
        CHECK_STATUS(SetEnvironment(world, 0.0f, 0.5f, 1.0f), ENGINE_OK);
        CHECK_STATUS(FrameAndRead(engine, surface, &pixels), ENGINE_OK);
        CHECK(Pixel(&pixels, 2, 2)[0] > 40 && Pixel(&pixels, 2, 2)[1] < 8 && Pixel(&pixels, 2, 2)[2] < 8);
        CHECK_STATUS(SetEnvironment(world, 0.0f, 0.0f, 1.0f), ENGINE_OK);
        /* more lights than a surface holds: the rest are counted, nothing crashes */
        for (i = 0; i < 70; ++i)
        {
            CHECK_STATUS(g_api->entity_create(world, &many[i]), ENGINE_OK);
            PlaceAt(world, many[i], 0.0f, 0.0f, 1.0f);
            CHECK_STATUS(SetLight(world, many[i], ENGINE_LIGHT_POINT, 0.01f, 2.0f), ENGINE_OK);
        }
        CHECK_STATUS(FrameAndRead(engine, surface, &pixels), ENGINE_OK);
        memset(&lit_state, 0, sizeof(lit_state));
        lit_state.struct_size = sizeof(lit_state);
        CHECK_STATUS(g_api->surface_state(surface, &lit_state), ENGINE_OK);
        CHECK(lit_state.lights_refused == 7); /* 64 slots: the ambient term and 63 lights */
        for (i = 0; i < 70; ++i)
        {
            CHECK_STATUS(g_api->entity_destroy(world, many[i]), ENGINE_OK);
        }
        CHECK_STATUS(g_api->entity_destroy(world, lamp), ENGINE_OK);
        CHECK_STATUS(SetEnvironment(world, 0.2f, 0.0f, 1.0f), ENGINE_OK);
    }

    /* Day 362: names, a hierarchy, visibility and per-entity material overrides */
    {
        uint64_t left;
        uint64_t right;
        uint64_t parent;
        uint64_t child;
        uint64_t found;
        uint64_t list[4];
        uint64_t read_parent;
        size_t total;
        int on_left[3];
        int on_right[3];
        engine_world_transform placed;
        engine_slot_override color;
        engine_slot_override texture;
        const engine_string nothing = {NULL, 0};
        CHECK_STATUS(SetEnvironment(world, 3.0f, 0.0f, 1.0f), ENGINE_OK); /* bright ambient light: colours show */
        CHECK_STATUS(g_api->entity_create(world, &left), ENGINE_OK);
        CHECK_STATUS(g_api->entity_create(world, &right), ENGINE_OK);
        PlaceAt(world, left, -2.2f, 0.0f, 0.0f);
        PlaceAt(world, right, 2.2f, 0.0f, 0.0f);
        CHECK_STATUS(SetMeshReference(world, left, MESH_ID, 0), ENGINE_OK);
        CHECK_STATUS(SetMeshReference(world, right, MESH_ID, 0), ENGINE_OK);
        /* find by name */
        CHECK_STATUS(g_api->entity_name_set(world, left, Str("robot")), ENGINE_OK);
        CHECK_STATUS(g_api->entity_name_set(world, right, Str("robot")), ENGINE_OK);
        CHECK_STATUS(g_api->entity_find(world, Str("robot"), &found), ENGINE_OK);
        CHECK(found == left || found == right);
        CHECK_STATUS(g_api->entity_find(world, Str("nobody"), &found), ENGINE_ERROR_NOT_FOUND);
        CHECK_STATUS(g_api->entity_find(world, Str("robot"), NULL), ENGINE_ERROR_INVALID_ARGUMENT);
        CHECK_STATUS(g_api->entity_find_all(world, Str("robot"), list, 4, &total), ENGINE_OK);
        CHECK(total == 2 && ((list[0] == left && list[1] == right) || (list[0] == right && list[1] == left)));
        CHECK_STATUS(g_api->entity_find_all(world, Str("robot"), list, 1, &total), ENGINE_OK);
        CHECK(total == 2);
        CHECK_STATUS(g_api->entity_find_all(world, Str("robot"), NULL, 1, &total), ENGINE_ERROR_INVALID_ARGUMENT);
        /* overrides: the left entity turns red, the right one keeps the shared material */
        CHECK_STATUS(FrameAndRead(engine, surface, &pixels), ENGINE_OK);
        ColorAt(surface, &pixels, -2.2f, -0.2f, 0.0f, on_left);
        ColorAt(surface, &pixels, 2.2f, -0.2f, 0.0f, on_right);
        CHECK(abs(on_left[0] - on_right[0]) < 6 && abs(on_left[1] - on_right[1]) < 6);
        memset(&color, 0, sizeof(color));
        color.struct_size = sizeof(color);
        color.kind = ENGINE_SLOT_COLOR;
        color.value[0] = 1.0f;
        color.value[3] = 1.0f;
        CHECK_STATUS(g_api->entity_set_slot_override(world, left, Str("base_color"), &color), ENGINE_OK);
        CHECK_STATUS(FrameAndRead(engine, surface, &pixels), ENGINE_OK);
        ColorAt(surface, &pixels, -2.2f, -0.2f, 0.0f, on_left);
        ColorAt(surface, &pixels, 2.2f, -0.2f, 0.0f, on_right);
        CHECK(on_left[0] > 100 && on_left[1] < 20 && on_left[2] < 20);  /* red */
        CHECK(abs(on_right[0] - on_right[1]) < 12 && on_right[1] > 40); /* the shared material is untouched: grey */
        CHECK_STATUS(g_api->entity_set_slot_override(world, left, Str("no_such_slot"), &color),
                     ENGINE_ERROR_INVALID_ARGUMENT);
        {
            engine_buffer message;
            memset(&message, 0, sizeof(message));
            CHECK_STATUS(g_api->last_error(&message), ENGINE_OK);
            CHECK(message.data != NULL && strstr((const char*)message.data, "no_such_slot") != NULL);
            engine_buffer_release(&message);
        }
        CHECK_STATUS(g_api->entity_set_slot_override(world, left, Str("roughness"), &color),
                     ENGINE_ERROR_INVALID_ARGUMENT); /* a colour for a float slot */
        memset(&texture, 0, sizeof(texture));
        texture.struct_size = sizeof(texture);
        texture.kind = ENGINE_SLOT_TEXTURE;
        CHECK_STATUS(g_api->entity_set_slot_override(world, left, Str("albedo_tex"), &texture),
                     ENGINE_ERROR_INVALID_ARGUMENT); /* no texture id */
        CHECK_STATUS(g_api->entity_clear_slot_override(world, right, Str("base_color")), ENGINE_ERROR_NOT_FOUND);
        CHECK_STATUS(g_api->entity_clear_slot_override(world, left, nothing), ENGINE_OK); /* every override */
        CHECK_STATUS(FrameAndRead(engine, surface, &pixels), ENGINE_OK);
        ColorAt(surface, &pixels, -2.2f, -0.2f, 0.0f, on_left);
        CHECK(abs(on_left[0] - on_left[1]) < 12);
        /* a hierarchy: the child follows its parent */
        CHECK_STATUS(g_api->entity_create(world, &parent), ENGINE_OK);
        CHECK_STATUS(g_api->entity_create(world, &child), ENGINE_OK);
        PlaceAt(world, parent, 0.0f, 3.0f, 0.0f);
        PlaceAt(world, child, 1.0f, 0.0f, 0.0f);
        CHECK_STATUS(g_api->entity_set_parent(world, child, parent), ENGINE_OK);
        CHECK_STATUS(g_api->entity_set_parent(world, parent, child), ENGINE_ERROR_INVALID_ARGUMENT); /* a cycle */
        CHECK_STATUS(g_api->entity_set_parent(world, child, child), ENGINE_ERROR_INVALID_ARGUMENT);
        CHECK_STATUS(g_api->entity_get_parent(world, child, &read_parent), ENGINE_OK);
        CHECK(read_parent == parent);
        CHECK_STATUS(g_api->entity_get_parent(world, parent, &read_parent), ENGINE_OK);
        CHECK(read_parent == 0);
        CHECK_STATUS(FrameAndRead(engine, surface, &pixels), ENGINE_OK);
        memset(&placed, 0, sizeof(placed));
        placed.struct_size = sizeof(placed);
        CHECK_STATUS(g_api->component_get(world, child, ENGINE_COMPONENT_WORLD_TRANSFORM, &placed), ENGINE_OK);
        CHECK(fabsf(placed.matrix[12] - 1.0f) < 0.001f && fabsf(placed.matrix[13] - 3.0f) < 0.001f);
        PlaceAt(world, parent, 5.0f, 3.0f, 0.0f); /* the parent moves: the child goes with it */
        CHECK_STATUS(FrameAndRead(engine, surface, &pixels), ENGINE_OK);
        CHECK_STATUS(g_api->component_get(world, child, ENGINE_COMPONENT_WORLD_TRANSFORM, &placed), ENGINE_OK);
        CHECK(fabsf(placed.matrix[12] - 6.0f) < 0.001f);
        /* visibility: hiding a parent hides its child */
        CHECK_STATUS(SetMeshReference(world, child, MESH_ID, 0), ENGINE_OK);
        PlaceAt(world, parent, 0.0f, 0.0f, -2.0f);
        PlaceAt(world, child, 0.0f, 0.0f, 0.0f);
        CHECK_STATUS(FrameAndRead(engine, surface, &pixels), ENGINE_OK);
        CHECK_STATUS(g_api->surface_state(surface, &state), ENGINE_OK);
        CHECK(state.objects_drawn == 4); /* the original, left, right and the child */
        CHECK_STATUS(SetVisibility(world, parent, 1), ENGINE_OK);
        CHECK_STATUS(FrameAndRead(engine, surface, &pixels), ENGINE_OK);
        CHECK_STATUS(g_api->surface_state(surface, &state), ENGINE_OK);
        CHECK(state.objects_drawn == 3);
        CHECK_STATUS(SetVisibility(world, parent, 2), ENGINE_ERROR_INVALID_ARGUMENT);
        CHECK_STATUS(g_api->component_remove(world, parent, ENGINE_COMPONENT_VISIBILITY), ENGINE_OK);
        CHECK_STATUS(FrameAndRead(engine, surface, &pixels), ENGINE_OK);
        CHECK_STATUS(g_api->surface_state(surface, &state), ENGINE_OK);
        CHECK(state.objects_drawn == 4);
        CHECK_STATUS(SetVisibility(world, right, 1), ENGINE_OK);
        CHECK_STATUS(FrameAndRead(engine, surface, &pixels), ENGINE_OK);
        CHECK_STATUS(g_api->surface_state(surface, &state), ENGINE_OK);
        CHECK(state.objects_drawn == 3);
        /* destroying a parent leaves the child, a root from the next frame */
        CHECK_STATUS(g_api->entity_destroy(world, parent), ENGINE_OK);
        CHECK_STATUS(FrameAndRead(engine, surface, &pixels), ENGINE_OK);
        CHECK_STATUS(g_api->entity_get_parent(world, child, &read_parent), ENGINE_OK);
        CHECK(read_parent == 0);
        CHECK_STATUS(g_api->entity_destroy(world, child), ENGINE_OK);
        CHECK_STATUS(g_api->entity_destroy(world, left), ENGINE_OK);
        CHECK_STATUS(g_api->entity_destroy(world, right), ENGINE_OK);
        CHECK_STATUS(SetEnvironment(world, 0.2f, 0.0f, 1.0f), ENGINE_OK);
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

    /* Day 363: skeletons, clips, cross-fades, joints and attachments */
    {
        uint64_t hero;
        uint64_t bolt;
        uint64_t skinned;
        uint32_t joint = 99;
        double t0 = 0.0;
        double t1 = 0.0;
        double t2 = 0.0;
        skeleton_blob = ReadFile(RES_DATA_DIR, "hero.skel.blob");
        other_skeleton = ReadFile(RES_DATA_DIR, "other.skel.blob");
        idle_blob = ReadFile(RES_DATA_DIR, "idle.anim.blob");
        other_clip = ReadFile(RES_DATA_DIR, "other.anim.blob");
        hold_blob = ReadFile(RES_DATA_DIR, "hold.anim.blob");
        skinned_blob = ReadFile(RES_DATA_DIR, "skinned.mesh.blob");
        engine_animation_state state_out;
        engine_world_transform bolt_world;
        int i;
        if (skeleton_blob.data == NULL || other_skeleton.data == NULL || idle_blob.data == NULL ||
            other_clip.data == NULL || hold_blob.data == NULL || skinned_blob.data == NULL)
        {
            return 1;
        }
        AddBlob(engine, 0xa01ull, &skeleton_blob);
        AddBlob(engine, 0xa02ull, &other_skeleton);
        AddBlob(engine, 0xc01ull, &idle_blob);
        AddBlob(engine, 0xc02ull, &other_clip);
        AddBlob(engine, 0xc03ull, &hold_blob);
        AddBlob(engine, 0xb05ull, &skinned_blob);
        CHECK_STATUS(g_api->entity_create(world, &hero), ENGINE_OK);
        PlaceAt(world, hero, 0.0f, 0.0f, 0.0f);
        /* nothing to play on, nothing to read */
        CHECK_STATUS(Play(world, hero, 0xc01ull, ENGINE_PLAY_LOOP, 1.0f, 0.0f, 0.0f), ENGINE_ERROR_INVALID_ARGUMENT);
        CHECK_STATUS(g_api->animation_time(world, hero, &t0), ENGINE_ERROR_NOT_FOUND);
        CHECK_STATUS(SetSkeleton(world, hero, 0), ENGINE_ERROR_INVALID_ARGUMENT);
        CHECK_STATUS(SetSkeleton(world, hero, 0xaaaaull), ENGINE_ERROR_NOT_FOUND);
        CHECK_STATUS(SetSkeleton(world, hero, 0xa01ull), ENGINE_OK);
        /* the bind pose until a clip plays: the arm is one above the root */
        CHECK_STATUS(FrameAndRead(engine, surface, &pixels), ENGINE_OK);
        CHECK(Near(JointHeight(world, hero, 1), 1.0f) < 0.001f);
        CHECK_STATUS(g_api->skeleton_find_joint(world, hero, Str("arm"), &joint), ENGINE_OK);
        CHECK(joint == 1);
        CHECK_STATUS(g_api->skeleton_find_joint(world, hero, Str("tail"), &joint), ENGINE_ERROR_NOT_FOUND);
        CHECK_STATUS(g_api->skeleton_joint_transform(world, hero, 7, (float[16]){0}), ENGINE_ERROR_INVALID_ARGUMENT);
        /* a clip of another skeleton is refused, an unknown or a wrong-type id too */
        CHECK_STATUS(Play(world, hero, 0xc02ull, 0, 1.0f, 0.0f, 0.0f), ENGINE_ERROR_INVALID_ARGUMENT);
        CHECK_STATUS(Play(world, hero, 0xccccull, 0, 1.0f, 0.0f, 0.0f), ENGINE_ERROR_NOT_FOUND);
        CHECK_STATUS(Play(world, hero, 0xc01ull, 0x80u, 1.0f, 0.0f, 0.0f), ENGINE_ERROR_INVALID_ARGUMENT);
        CHECK_STATUS(Play(world, hero, 0xc01ull, 0, -1.0f, 0.0f, 0.0f), ENGINE_ERROR_INVALID_ARGUMENT);
        CHECK_STATUS(g_api->animation_set_speed(world, hero, 1.0f), ENGINE_ERROR_NOT_FOUND);
        /* time advances with the frame, at the speed set */
        CHECK_STATUS(Play(world, hero, 0xc01ull, ENGINE_PLAY_LOOP, 1.0f, 0.0f, 0.0f), ENGINE_OK);
        CHECK_STATUS(FrameAndRead(engine, surface, &pixels), ENGINE_OK);
        CHECK_STATUS(g_api->animation_time(world, hero, &t0), ENGINE_OK);
        CHECK(t0 > 0.0 && t0 < 0.1);
        CHECK(Near(JointHeight(world, hero, 1), 1.0f + (float)t0) < 0.01f);
        CHECK_STATUS(g_api->animation_set_speed(world, hero, 2.0f), ENGINE_OK);
        CHECK_STATUS(FrameAndRead(engine, surface, &pixels), ENGINE_OK);
        CHECK_STATUS(g_api->animation_time(world, hero, &t1), ENGINE_OK);
        CHECK(Near((float)(t1 - t0), 0.032f) < 0.006f);
        CHECK_STATUS(g_api->animation_set_speed(world, hero, 0.0f), ENGINE_OK);
        CHECK_STATUS(FrameAndRead(engine, surface, &pixels), ENGINE_OK);
        CHECK_STATUS(g_api->animation_time(world, hero, &t2), ENGINE_OK);
        CHECK(Near((float)t2, (float)t1) < 0.0001f);
        CHECK_STATUS(g_api->animation_stop(world, hero), ENGINE_OK);
        memset(&state_out, 0, sizeof(state_out));
        state_out.struct_size = sizeof(state_out);
        CHECK_STATUS(g_api->component_get(world, hero, ENGINE_COMPONENT_ANIMATION_PLAYER, &state_out), ENGINE_OK);
        CHECK(state_out.playing == 0 && state_out.clip == 0xc01ull && state_out.looping == 1);
        CHECK_STATUS(g_api->component_set(world, hero, ENGINE_COMPONENT_ANIMATION_PLAYER, &state_out),
                     ENGINE_ERROR_UNSUPPORTED);
        /* a cross-fade to a clip that holds the arm at 3: partway between, then there */
        CHECK_STATUS(Play(world, hero, 0xc01ull, ENGINE_PLAY_LOOP, 1.0f, 0.5f, 0.0f), ENGINE_OK);
        CHECK_STATUS(g_api->animation_set_speed(world, hero, 0.0f), ENGINE_OK);
        CHECK_STATUS(FrameAndRead(engine, surface, &pixels), ENGINE_OK);
        CHECK(Near(JointHeight(world, hero, 1), 1.5f) < 0.01f);
        CHECK_STATUS(Play(world, hero, 0xc03ull, ENGINE_PLAY_LOOP, 1.0f, 0.0f, 1.0f), ENGINE_OK);
        for (i = 0; i < 31; ++i)
        {
            CHECK_STATUS(FrameAndRead(engine, surface, &pixels), ENGINE_OK);
        }
        {
            const float mid = JointHeight(world, hero, 1);
            CHECK(mid > 1.7f && mid < 2.9f);
            CHECK_STATUS(g_api->component_get(world, hero, ENGINE_COMPONENT_ANIMATION_PLAYER, &state_out), ENGINE_OK);
            CHECK(state_out.fading == 1);
        }
        for (i = 0; i < 40; ++i)
        {
            CHECK_STATUS(FrameAndRead(engine, surface, &pixels), ENGINE_OK);
        }
        CHECK(Near(JointHeight(world, hero, 1), 3.0f) < 0.01f);
        CHECK_STATUS(g_api->component_get(world, hero, ENGINE_COMPONENT_ANIMATION_PLAYER, &state_out), ENGINE_OK);
        CHECK(state_out.fading == 0);
        /* an entity that follows the arm */
        CHECK_STATUS(g_api->entity_create(world, &bolt), ENGINE_OK);
        PlaceAt(world, bolt, 0.0f, 0.0f, 0.0f);
        CHECK_STATUS(g_api->entity_attach_to_joint(world, bolt, bolt, 1), ENGINE_ERROR_INVALID_ARGUMENT);
        CHECK_STATUS(g_api->entity_attach_to_joint(world, bolt, hero, 99), ENGINE_ERROR_INVALID_ARGUMENT);
        CHECK_STATUS(g_api->entity_attach_to_joint(world, bolt, entity, 1), ENGINE_ERROR_NOT_FOUND);
        CHECK_STATUS(g_api->entity_attach_to_joint(world, bolt, hero, 1), ENGINE_OK);
        CHECK_STATUS(FrameAndRead(engine, surface, &pixels), ENGINE_OK);
        memset(&bolt_world, 0, sizeof(bolt_world));
        bolt_world.struct_size = sizeof(bolt_world);
        CHECK_STATUS(g_api->component_get(world, bolt, ENGINE_COMPONENT_WORLD_TRANSFORM, &bolt_world), ENGINE_OK);
        CHECK(Near(bolt_world.matrix[13], 3.0f) < 0.05f);
        CHECK_STATUS(g_api->entity_attach_to_joint(world, bolt, 0, 0), ENGINE_OK);
        /* a skinned mesh draws with the skeleton's palette, and is not drawn without one */
        CHECK_STATUS(g_api->entity_create(world, &skinned), ENGINE_OK);
        PlaceAt(world, skinned, 0.0f, 0.0f, 0.0f);
        CHECK_STATUS(SetMeshReference(world, skinned, 0xb05ull, 0), ENGINE_OK);
        CHECK_STATUS(FrameAndRead(engine, surface, &pixels), ENGINE_OK);
        CHECK_STATUS(g_api->surface_state(surface, &state), ENGINE_OK);
        CHECK_STATUS(SetSkeleton(world, skinned, 0xa01ull), ENGINE_OK);
        CHECK_STATUS(FrameAndRead(engine, surface, &pixels), ENGINE_OK);
        CHECK(HasObject(&pixels, clear));
        CHECK_STATUS(g_api->entity_destroy(world, skinned), ENGINE_OK);
        CHECK_STATUS(g_api->entity_destroy(world, bolt), ENGINE_OK);
        CHECK_STATUS(g_api->entity_destroy(world, hero), ENGINE_OK);
    }

    /* host textures (Phase 24 Day 366): three entities share one material, each shows its own pattern, a producer's
       update shows in the next frame, a resize and a destroy fall back to the material's own value */
    {
        engine_handle world3;
        engine_handle textures[3];
        uint64_t three[3];
        engine_slot_override slot;
        engine_texture_info tinfo;
        engine_update_info uinfo;
        engine_frame_info finfo;
        engine_frame_stats fstats;
        unsigned char solid[4 * 4 * 4];
        int rgb[3][3];
        int k;
        int j;
        memset(&world3, 0, sizeof(world3));
        CHECK_STATUS(g_api->world_create(engine, &world3), ENGINE_OK);
        memset(&tinfo, 0, sizeof(tinfo));
        tinfo.struct_size = sizeof(tinfo);
        tinfo.width = 4;
        tinfo.height = 4;
        tinfo.flags = 0;
        memset(&slot, 0, sizeof(slot));
        slot.struct_size = sizeof(slot);
        slot.kind = ENGINE_SLOT_HOST_TEXTURE;
        memset(&finfo, 0, sizeof(finfo));
        finfo.struct_size = sizeof(finfo);
        finfo.delta_seconds = 0.016;
        for (k = 0; k < 3; ++k)
        {
            CHECK_STATUS(g_api->entity_create(world3, &three[k]), ENGINE_OK);
            PlaceAt(world3, three[k], -2.2f + 2.2f * (float)k, 0.0f, 0.0f);
            CHECK_STATUS(SetMeshReference(world3, three[k], MESH_ID, MATERIAL_ID), ENGINE_OK);
            memset(&textures[k], 0, sizeof(textures[k]));
            CHECK_STATUS(g_api->texture_create(engine, &tinfo, &textures[k]), ENGINE_OK);
            for (j = 0; j < 16; ++j)
            {
                solid[j * 4 + 0] = (unsigned char)(k == 0 ? 255 : 0);
                solid[j * 4 + 1] = (unsigned char)(k == 1 ? 255 : 0);
                solid[j * 4 + 2] = (unsigned char)(k == 2 ? 255 : 0);
                solid[j * 4 + 3] = 255;
            }
            CHECK_STATUS(g_api->texture_update(textures[k], solid, 16, NULL), ENGINE_OK);
            slot.host_texture = textures[k];
            CHECK_STATUS(g_api->entity_set_slot_override(world3, three[k], Str("albedo_tex"), &slot), ENGINE_OK);
        }
        /* the slot needs a live texture, and only the albedo and normal slots take one */
        slot.host_texture = none;
        CHECK_STATUS(g_api->entity_set_slot_override(world3, three[0], Str("albedo_tex"), &slot),
                     ENGINE_ERROR_STALE_HANDLE);
        slot.host_texture = textures[0];
        CHECK_STATUS(g_api->entity_set_slot_override(world3, three[0], Str("roughness"), &slot),
                     ENGINE_ERROR_INVALID_ARGUMENT);
        CHECK_STATUS(g_api->surface_set_world(surface, world3), ENGINE_OK);
        CHECK_STATUS(FrameAndRead(engine, surface, &pixels), ENGINE_OK);
        for (k = 0; k < 3; ++k)
        {
            ColorAt(surface, &pixels, -2.2f + 2.2f * (float)k, -0.2f, 0.0f, rgb[k]);
        }
        CHECK(rgb[0][0] > rgb[0][1] + 30 && rgb[0][0] > rgb[0][2] + 30);
        CHECK(rgb[1][1] > rgb[1][0] + 30 && rgb[1][1] > rgb[1][2] + 30);
        CHECK(rgb[2][2] > rgb[2][0] + 30 && rgb[2][2] > rgb[2][1] + 30);
        CHECK_STATUS(g_api->surface_state(surface, &state), ENGINE_OK);
        CHECK(state.textures_uploaded >= 3 && state.texture_bytes_uploaded >= 3 * 64);
        /* a newer update shows in the next frame: the first entity turns blue */
        for (j = 0; j < 16; ++j)
        {
            solid[j * 4 + 0] = 0;
            solid[j * 4 + 1] = 0;
            solid[j * 4 + 2] = 255;
        }
        CHECK_STATUS(g_api->texture_update(textures[0], solid, 16, NULL), ENGINE_OK);
        CHECK_STATUS(FrameAndRead(engine, surface, &pixels), ENGINE_OK);
        ColorAt(surface, &pixels, -2.2f, -0.2f, 0.0f, rgb[0]);
        CHECK(rgb[0][2] > rgb[0][0] + 30);
        /* a resize without pixels: the slot shows the material's own value (counted); the next update shows again */
        CHECK_STATUS(g_api->texture_resize(textures[1], 8, 8), ENGINE_OK);
        memset(&fstats, 0, sizeof(fstats));
        fstats.struct_size = sizeof(fstats);
        CHECK_STATUS(g_api->frame(engine, &finfo, &fstats), ENGINE_OK);
        CHECK(fstats.slots_reverted >= 1);
        {
            unsigned char big[8 * 8 * 4];
            for (j = 0; j < 64; ++j)
            {
                big[j * 4 + 0] = 255;
                big[j * 4 + 1] = 255;
                big[j * 4 + 2] = 0;
                big[j * 4 + 3] = 255;
            }
            memset(&uinfo, 0, sizeof(uinfo));
            CHECK_STATUS(g_api->texture_update(textures[1], big, 32, NULL), ENGINE_OK);
        }
        CHECK_STATUS(FrameAndRead(engine, surface, &pixels), ENGINE_OK);
        ColorAt(surface, &pixels, 0.0f, -0.2f, 0.0f, rgb[1]);
        CHECK(rgb[1][0] > rgb[1][2] + 30 && rgb[1][1] > rgb[1][2] + 30); /* yellow */
        /* destroying a texture reverts its slot; the neighbours keep theirs */
        CHECK_STATUS(g_api->texture_destroy(textures[2]), ENGINE_OK);
        CHECK_STATUS(FrameAndRead(engine, surface, &pixels), ENGINE_OK);
        ColorAt(surface, &pixels, 2.2f, -0.2f, 0.0f, rgb[2]);
        CHECK(abs(rgb[2][0] - rgb[2][1]) < 40 && abs(rgb[2][1] - rgb[2][2]) < 40); /* the brick's own colour */
        ColorAt(surface, &pixels, -2.2f, -0.2f, 0.0f, rgb[0]);
        CHECK(rgb[0][2] > rgb[0][0] + 30);
        /* the same texture on a second surface of the same engine is uploaded there too: here only the counters */
        CHECK_STATUS(g_api->surface_set_world(surface, none), ENGINE_OK);
        CHECK_STATUS(g_api->world_destroy(world3), ENGINE_OK);
        CHECK_STATUS(g_api->texture_destroy(textures[0]), ENGINE_OK);
        CHECK_STATUS(g_api->texture_destroy(textures[1]), ENGINE_OK);
        CHECK_STATUS(g_api->surface_set_world(surface, world), ENGINE_OK);
    }

    /* labels (Phase 24 Day 367) */
    {
        engine_handle world4;
        uint64_t tag;
        engine_label label;
        Blob font = ReadFile(LABEL_FONT_DIR, LABEL_FONT_NAME);
        float at[3];
        const float origin[3] = {0.0f, 0.0f, 0.0f};
        int near_count;
        int far_count;
        char longtext[300];
        CHECK(font.data != NULL);
        memset(&world4, 0, sizeof(world4));
        CHECK_STATUS(g_api->world_create(engine, &world4), ENGINE_OK);
        CHECK_STATUS(g_api->entity_create(world4, &tag), ENGINE_OK);
        PlaceAt(world4, tag, 0.0f, 0.0f, 0.0f);
        /* the component: arguments and the round trip */
        CHECK_STATUS(SetLabel(world4, tag, "Hello", 16.0f, ENGINE_LABEL_ANCHOR_CENTER, 0), ENGINE_OK);
        CHECK_STATUS(SetLabel(world4, tag, "Hello", 0.0f, 0, 0), ENGINE_ERROR_INVALID_ARGUMENT);
        CHECK_STATUS(SetLabel(world4, tag, "Hello", 16.0f, 9, 0), ENGINE_ERROR_INVALID_ARGUMENT);
        CHECK_STATUS(SetLabel(world4, tag, "Hello", 16.0f, 0, 0x80), ENGINE_ERROR_INVALID_ARGUMENT);
        memset(longtext, 'a', sizeof(longtext) - 1);
        longtext[sizeof(longtext) - 1] = 0;
        CHECK_STATUS(SetLabel(world4, tag, longtext, 16.0f, 0, 0), ENGINE_ERROR_INVALID_ARGUMENT);
        memset(&label, 0, sizeof(label));
        label.struct_size = sizeof(label);
        label.size = 16.0f;
        label.color[0] = 2.0f;
        CHECK_STATUS(g_api->component_set(world4, tag, ENGINE_COMPONENT_LABEL, &label), ENGINE_ERROR_INVALID_ARGUMENT);
        memset(&label, 0, sizeof(label));
        label.struct_size = sizeof(label);
        CHECK_STATUS(g_api->component_get(world4, tag, ENGINE_COMPONENT_LABEL, &label), ENGINE_OK);
        CHECK(label.text.length == 5 && label.text.data == NULL && label.size == 16.0f);
        CHECK_STATUS(g_api->surface_set_world(surface, world4), ENGINE_OK);
        /* no font yet: counted, nothing drawn */
        CHECK_STATUS(FrameAndRead(engine, surface, &pixels), ENGINE_OK);
        CHECK_STATUS(g_api->surface_state(surface, &state), ENGINE_OK);
        CHECK(state.labels_drawn == 0 && state.labels_refused == 1);
        CHECK_STATUS(g_api->label_font_set(engine, NULL, 10), ENGINE_ERROR_INVALID_ARGUMENT);
        CHECK_STATUS(g_api->label_font_set(engine, font.data, 0), ENGINE_ERROR_INVALID_ARGUMENT);
        CHECK_STATUS(g_api->label_font_set(engine, font.data, font.size), ENGINE_OK);
        CHECK_STATUS(FrameAndRead(engine, surface, &pixels), ENGINE_OK);
        CHECK_STATUS(g_api->surface_state(surface, &state), ENGINE_OK);
        CHECK(state.labels_drawn == 1 && state.labels_refused == 0);
        CHECK_STATUS(g_api->surface_project(surface, origin, at), ENGINE_OK);
        near_count = CountText(&pixels, (int)at[0], (int)at[1], 20);
        CHECK(near_count > 20);
        CHECK(CountText(&pixels, (int)at[0], (int)at[1], 20) ==
              CountText(&pixels, (int)at[0], (int)at[1], 60)); /* only there */
        /* a hidden entity's label is not drawn */
        CHECK_STATUS(SetVisibility(world4, tag, 1), ENGINE_OK);
        CHECK_STATUS(FrameAndRead(engine, surface, &pixels), ENGINE_OK);
        CHECK(CountText(&pixels, (int)at[0], (int)at[1], 60) == 0);
        CHECK_STATUS(SetVisibility(world4, tag, 0), ENGINE_OK);
        /* constant pixel size: the same text when the entity is farther; constant metres: smaller */
        PlaceAt(world4, tag, 0.0f, 0.0f, -20.0f);
        CHECK_STATUS(FrameAndRead(engine, surface, &pixels), ENGINE_OK);
        {
            const float far_point[3] = {0.0f, 0.0f, -20.0f};
            CHECK_STATUS(g_api->surface_project(surface, far_point, at), ENGINE_OK);
            far_count = CountText(&pixels, (int)at[0], (int)at[1], 20);
            CHECK(far_count > near_count * 8 / 10 && far_count < near_count * 12 / 10);
            CHECK_STATUS(SetLabel(world4, tag, "Hello", 3.0f, 0, ENGINE_LABEL_SIZE_METRES), ENGINE_OK);
            CHECK_STATUS(FrameAndRead(engine, surface, &pixels), ENGINE_OK);
            far_count = CountText(&pixels, (int)at[0], (int)at[1], 40);
            PlaceAt(world4, tag, 0.0f, 0.0f, -4.0f);
            CHECK_STATUS(FrameAndRead(engine, surface, &pixels), ENGINE_OK);
            {
                const float close_point[3] = {0.0f, 0.0f, -4.0f};
                CHECK_STATUS(g_api->surface_project(surface, close_point, at), ENGINE_OK);
                near_count = CountText(&pixels, (int)at[0], (int)at[1], 40);
            }
            CHECK(far_count < near_count);
        }
        /* UTF-8, combining marks and malformed bytes are laid out, never a crash */
        CHECK_STATUS(
            SetLabel(world4, tag, "caf\xC3\xA9 e\xCC\x81 \xE6\x97\xA5 \xFF\xFE", 16.0f, ENGINE_LABEL_ANCHOR_LEFT, 0),
            ENGINE_OK);
        CHECK_STATUS(FrameAndRead(engine, surface, &pixels), ENGINE_OK);
        CHECK_STATUS(g_api->component_remove(world4, tag, ENGINE_COMPONENT_LABEL), ENGINE_OK);
        CHECK_STATUS(g_api->component_remove(world4, tag, ENGINE_COMPONENT_LABEL), ENGINE_ERROR_NOT_FOUND);
        CHECK_STATUS(g_api->surface_set_world(surface, world), ENGINE_OK);
        CHECK_STATUS(g_api->world_destroy(world4), ENGINE_OK);
        free(font.data);
    }

    /* Day 368: a scene with a host texture and a label survives a rebind to a new window; the same texture shows on two
       surfaces (each uploads it); a seeded sequence of creates and destroys in every order leaves nothing behind */
    {
        engine_handle world5;
        engine_handle tex;
        engine_handle other_surface;
        uint64_t body;
        engine_texture_info tinfo;
        engine_slot_override slot;
        engine_label label;
        unsigned char solid[4 * 4 * 4];
        unsigned long long before_picture;
        Window fresh;
        Window other;
        engine_surface_info fresh_info = info;
        engine_surface_state first_state;
        engine_surface_state second_state;
        Blob font = ReadFile(LABEL_FONT_DIR, LABEL_FONT_NAME);
        unsigned seed = 12345u;
        int op;
        int j;
        memset(&world5, 0, sizeof(world5));
        memset(&tex, 0, sizeof(tex));
        memset(&tinfo, 0, sizeof(tinfo));
        tinfo.struct_size = sizeof(tinfo);
        tinfo.width = 4;
        tinfo.height = 4;
        CHECK_STATUS(g_api->label_font_set(engine, font.data, font.size), ENGINE_OK);
        CHECK_STATUS(g_api->world_create(engine, &world5), ENGINE_OK);
        CHECK_STATUS(g_api->entity_create(world5, &body), ENGINE_OK);
        PlaceAt(world5, body, 0.0f, 0.0f, 0.0f);
        CHECK_STATUS(SetMeshReference(world5, body, MESH_ID, MATERIAL_ID), ENGINE_OK);
        CHECK_STATUS(g_api->texture_create(engine, &tinfo, &tex), ENGINE_OK);
        for (j = 0; j < 16; ++j)
        {
            solid[j * 4 + 0] = 255;
            solid[j * 4 + 1] = 0;
            solid[j * 4 + 2] = 255;
            solid[j * 4 + 3] = 255;
        }
        CHECK_STATUS(g_api->texture_update(tex, solid, 16, NULL), ENGINE_OK);
        memset(&slot, 0, sizeof(slot));
        slot.struct_size = sizeof(slot);
        slot.kind = ENGINE_SLOT_HOST_TEXTURE;
        slot.host_texture = tex;
        CHECK_STATUS(g_api->entity_set_slot_override(world5, body, Str("albedo_tex"), &slot), ENGINE_OK);
        memset(&label, 0, sizeof(label));
        label.struct_size = sizeof(label);
        label.text = Str("body");
        label.color[0] = label.color[1] = label.color[2] = label.color[3] = 1.0f;
        label.size = 14.0f;
        label.offset[1] = 1.5f;
        CHECK_STATUS(g_api->component_set(world5, body, ENGINE_COMPONENT_LABEL, &label), ENGINE_OK);
        CHECK_STATUS(g_api->surface_set_world(surface, world5), ENGINE_OK);
        CHECK_STATUS(FrameAndRead(engine, surface, &pixels), ENGINE_OK);
        before_picture = Checksum(&pixels);
        CHECK_STATUS(g_api->surface_state(surface, &first_state), ENGINE_OK);
        CHECK(first_state.labels_drawn == 1 && first_state.textures_uploaded >= 1);
        /* rebind to a new window of the same size: the same picture, nothing recreated by the host */
        fresh = XCreateSimpleWindow(display, DefaultRootWindow(display), 0, 0, WIDTH, HEIGHT, 0, 0, 0);
        XMapWindow(display, fresh);
        XSync(display, False);
        fresh_info.window_id = (uint64_t)fresh;
        CHECK_STATUS(g_api->surface_rebind(surface, &fresh_info), ENGINE_OK);
        CHECK_STATUS(FrameAndRead(engine, surface, &pixels), ENGINE_OK);
        CHECK(Checksum(&pixels) == before_picture);
        CHECK_STATUS(g_api->surface_state(surface, &first_state), ENGINE_OK);
        CHECK(first_state.labels_drawn == 1 && first_state.world_status == ENGINE_OK &&
              first_state.textures_uploaded >= 1);
        /* a second surface shows the same texture: it uploads its own copy, and the first keeps drawing */
        other = XCreateSimpleWindow(display, DefaultRootWindow(display), 0, 0, WIDTH, HEIGHT, 0, 0, 0);
        XMapWindow(display, other);
        XSync(display, False);
        fresh_info.window_id = (uint64_t)other;
        memset(&other_surface, 0, sizeof(other_surface));
        CHECK_STATUS(g_api->surface_create(engine, &fresh_info, &other_surface), ENGINE_OK);
        CHECK_STATUS(g_api->surface_set_background(other_surface, background), ENGINE_OK);
        CHECK_STATUS(g_api->surface_set_world(other_surface, world5), ENGINE_OK);
        CHECK_STATUS(FrameAndRead(engine, other_surface, &pixels), ENGINE_OK);
        CHECK(Checksum(&pixels) == before_picture);
        CHECK_STATUS(g_api->surface_state(other_surface, &second_state), ENGINE_OK);
        CHECK(second_state.textures_uploaded >= 1 && second_state.labels_drawn == 1);
        /* destroying one surface leaves the other drawing */
        CHECK_STATUS(g_api->surface_destroy(other_surface), ENGINE_OK);
        CHECK_STATUS(FrameAndRead(engine, surface, &pixels), ENGINE_OK);
        CHECK(Checksum(&pixels) == before_picture);
        XDestroyWindow(display, other);
        /* a seeded random sequence: entities with labels and textures are created and destroyed, textures go before and
           after the entities that name them, worlds are rebound; every frame answers a status */
        {
            uint64_t entities[8];
            engine_handle textures[4];
            int e;
            int t;
            memset(entities, 0, sizeof(entities));
            memset(textures, 0, sizeof(textures));
            for (op = 0; op < 200; ++op)
            {
                seed = seed * 1103515245u + 12345u;
                e = (int)((seed >> 16) % 8u);
                t = (int)((seed >> 8) % 4u);
                switch ((seed >> 4) % 6u)
                {
                case 0:
                    if (entities[e] == 0)
                    {
                        CHECK_STATUS(g_api->entity_create(world5, &entities[e]), ENGINE_OK);
                        PlaceAt(world5, entities[e], (float)e - 4.0f, 0.0f, 0.0f);
                        CHECK_STATUS(SetMeshReference(world5, entities[e], MESH_ID, MATERIAL_ID), ENGINE_OK);
                        CHECK_STATUS(SetLabel(world5, entities[e], "x", 12.0f, 0, 0), ENGINE_OK);
                    }
                    break;
                case 1:
                    if (entities[e] != 0)
                    {
                        CHECK_STATUS(g_api->entity_destroy(world5, entities[e]), ENGINE_OK);
                        entities[e] = 0;
                    }
                    break;
                case 2:
                    if (textures[t].value == 0)
                    {
                        CHECK_STATUS(g_api->texture_create(engine, &tinfo, &textures[t]), ENGINE_OK);
                        CHECK_STATUS(g_api->texture_update(textures[t], solid, 16, NULL), ENGINE_OK);
                    }
                    break;
                case 3:
                    if (textures[t].value != 0)
                    {
                        CHECK_STATUS(g_api->texture_destroy(textures[t]), ENGINE_OK);
                        textures[t].value = 0;
                    }
                    break;
                case 4:
                    if (entities[e] != 0 && textures[t].value != 0)
                    {
                        engine_status result;
                        slot.host_texture = textures[t];
                        result = g_api->entity_set_slot_override(world5, entities[e], Str("albedo_tex"), &slot);
                        CHECK(result == ENGINE_OK);
                    }
                    break;
                default: CHECK_STATUS(FrameAndRead(engine, surface, &pixels), ENGINE_OK); break;
                }
            }
            CHECK_STATUS(FrameAndRead(engine, surface, &pixels), ENGINE_OK);
            for (e = 0; e < 8; ++e)
            {
                if (entities[e] != 0)
                {
                    CHECK_STATUS(g_api->entity_destroy(world5, entities[e]), ENGINE_OK);
                }
            }
            for (t = 0; t < 4; ++t)
            {
                if (textures[t].value != 0)
                {
                    CHECK_STATUS(g_api->texture_destroy(textures[t]), ENGINE_OK);
                }
            }
        }
        /* the world goes before the surface lets go of it, the texture before the engine: nothing is outstanding */
        CHECK_STATUS(g_api->world_destroy(world5), ENGINE_OK);
        CHECK_STATUS(FrameAndRead(engine, surface, &pixels), ENGINE_OK);
        CHECK_STATUS(g_api->surface_state(surface, &first_state), ENGINE_OK);
        CHECK(first_state.world_status == ENGINE_ERROR_STALE_HANDLE);
        CHECK_STATUS(g_api->texture_destroy(tex), ENGINE_OK);
        CHECK_STATUS(g_api->surface_set_world(surface, world), ENGINE_OK);
        XDestroyWindow(display, fresh);
        free(font.data);
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
    {
        int k;
        for (k = 0; k < 6; ++k)
        {
            free(g_extra[k].data);
        }
    }
    if (g_failures != 0)
    {
        printf("host_scene: %d checks failed\n", g_failures);
        return 1;
    }
    printf("host_scene: ok\n");
    return 0;
}
