/* Phase 24 Day 383: the acceptance scene of the off-screen path, a C host with no window system that stands for
   a host application: it imports the meadow and the robot (glTF files in fixtures/) at run time, spawns the meadow and
   three robots (one walks, two stand), gives each robot's `Screen` slot its own host texture (and nothing else of the
   robot), draws 1280x720 into an off-screen surface and takes the pictures with surface_acquire_frame. It checks that
   the screens show three different patterns while the robots' other slots keep their colours, that the walker moves,
   that a resize mid-run and a lost device leave the same picture, and that a host that takes only every third frame
   gets the newest one. Exit 0 also when no device exists. */
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "engine/engine_c.h"
#include "engine_c/engine_test_extension.h"
#include "host_surface_api.h"

#define WIDTH 1280u
#define HEIGHT 720u
/* Pictures of one still scene agree but for dither and the filtering of a resized target: under half a percent of
 * 1280x720 pixels differ by more than 4 levels. */
static const size_t kSame = 4600;

static const engine_api* g_api = NULL;
static int g_failures = 0;

#define CHECK(condition)                                                                                               \
    do                                                                                                                 \
    {                                                                                                                  \
        if (!(condition))                                                                                              \
        {                                                                                                              \
            printf("host_acceptance_scene: line %d: CHECK failed: %s\n", __LINE__, #condition);                        \
            ++g_failures;                                                                                              \
        }                                                                                                              \
    } while (0)

#define CHECK_STATUS(call, expected)                                                                                   \
    do                                                                                                                 \
    {                                                                                                                  \
        const engine_status got_ = (call);                                                                             \
        if (got_ != (expected))                                                                                        \
        {                                                                                                              \
            printf("host_acceptance_scene: line %d: %s returned %d, expected %d\n", __LINE__, #call, (int)got_,        \
                   (int)(expected));                                                                                   \
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

typedef struct Blob
{
    unsigned char* data;
    size_t size;
} Blob;

static Blob ReadFixture(const char* name)
{
    char path[1024];
    Blob blob = {NULL, 0};
    FILE* f;
    snprintf(path, sizeof(path), "%s/%s", FIXTURE_DIR, name);
    f = fopen(path, "rb");
    if (f == NULL)
    {
        printf("host_acceptance_scene: cannot open %s\n", path);
        return blob;
    }
    fseek(f, 0, SEEK_END);
    blob.size = (size_t)ftell(f);
    fseek(f, 0, SEEK_SET);
    blob.data = (unsigned char*)malloc(blob.size);
    if (fread(blob.data, 1, blob.size, f) != blob.size)
    {
        free(blob.data);
        blob.data = NULL;
    }
    fclose(f);
    return blob;
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

/* The id of the resource whose path line contains `needle` and whose type is `type` (0 if none). */
static uint64_t FindResource(const engine_import_result* r, uint32_t type, const char* needle)
{
    const char* p = (const char*)r->paths.data;
    const char* end = p + r->paths.size;
    uint32_t i = 0;
    while (p < end && i < r->resource_count)
    {
        const char* eol = memchr(p, '\n', (size_t)(end - p));
        size_t length = eol != NULL ? (size_t)(eol - p) : (size_t)(end - p);
        size_t n = strlen(needle);
        size_t k;
        if (TypeAt(r, i) == type)
        {
            for (k = 0; k + n <= length; ++k)
            {
                if (memcmp(p + k, needle, n) == 0)
                {
                    return IdAt(r, i);
                }
            }
        }
        p += length + 1;
        ++i;
    }
    return 0;
}

static int verbose_dump(void)
{
    return getenv("HOST_VERBOSE") != NULL;
}

static void PrintPaths(const char* title, const engine_import_result* r)
{
    printf("%s: %u resources\n%.*s", title, r->resource_count, (int)r->paths.size, (const char*)r->paths.data);
}

static int Frame(engine_handle engine)
{
    engine_frame_info frame;
    memset(&frame, 0, sizeof(frame));
    frame.struct_size = sizeof(frame);
    frame.delta_seconds = 1.0 / 30.0;
    return g_api->frame(engine, &frame, NULL) == ENGINE_OK;
}

/* Draws frames until the surface has a finished picture to take. */
static int Acquire(engine_handle engine, engine_handle surface, engine_frame_image* image)
{
    int tries;
    memset(image, 0, sizeof(*image));
    image->struct_size = sizeof(*image);
    for (tries = 0; tries < 300; ++tries)
    {
        engine_status status;
        if (!Frame(engine))
        {
            return 0;
        }
        status = g_api->surface_acquire_frame(surface, image);
        if (status == ENGINE_OK)
        {
            return 1;
        }
        if (status != ENGINE_ERROR_NOT_READY)
        {
            return 0;
        }
    }
    return 0;
}

static unsigned long long Checksum(const engine_frame_image* image)
{
    unsigned long long sum = 1469598103934665603ull;
    size_t i;
    const unsigned char* p = (const unsigned char*)image->data;
    for (i = 0; i < (size_t)image->stride * image->height; ++i)
    {
        sum = (sum ^ p[i]) * 1099511628211ull;
    }
    return sum;
}

/* How many pixels of `image` differ from the copy `base` of an earlier picture by more than the dither (4 levels). */
static size_t Differing(const engine_frame_image* image, const unsigned char* base)
{
    size_t count = 0;
    size_t i;
    const unsigned char* p = (const unsigned char*)image->data;
    for (i = 0; i < (size_t)image->width * image->height; ++i)
    {
        int c;
        int worst = 0;
        for (c = 0; c < 3; ++c)
        {
            const int d = (int)p[i * 4 + c] - (int)base[i * 4 + c];
            worst = worst < (d < 0 ? -d : d) ? (d < 0 ? -d : d) : worst;
        }
        count += worst > 4 ? 1 : 0;
    }
    return count;
}

static engine_status StateOf(engine_handle surface, engine_surface_state* state)
{
    memset(state, 0, sizeof(*state));
    state->struct_size = sizeof(*state);
    return g_api->surface_state(surface, state);
}

static const unsigned char* PixelOf(const engine_frame_image* image, unsigned x, unsigned y)
{
    return (const unsigned char*)image->data + (size_t)y * image->stride + (size_t)x * 4;
}

static void ReleaseResult(engine_import_result* r)
{
    engine_buffer_release(&r->ids);
    engine_buffer_release(&r->types);
    engine_buffer_release(&r->paths);
    engine_buffer_release(&r->warnings);
    engine_buffer_release(&r->manifest);
}

static void Place(engine_handle world, uint64_t entity, float x, float y, float z)
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

/* A host texture of one flat colour with a darker frame, standing for a person's picture. */
static engine_handle MakeSizedTexture(engine_handle engine, const char* name, int W, int H, unsigned char r,
                                      unsigned char g, unsigned char b)
{
    unsigned char* pixels = (unsigned char*)malloc((size_t)W * (size_t)H * 4);
    engine_texture_info info;
    engine_update_info update;
    engine_handle texture;
    int x;
    int y;
    for (y = 0; y < H; ++y)
    {
        for (x = 0; x < W; ++x)
        {
            unsigned char* p = pixels + (y * W + x) * 4;
            const int frame = x < 2 || y < 2 || x >= W - 2 || y >= H - 2;
            p[0] = frame ? r / 2 : r;
            p[1] = frame ? g / 2 : g;
            p[2] = frame ? b / 2 : b;
            p[3] = 255;
        }
    }
    memset(&info, 0, sizeof(info));
    info.struct_size = sizeof(info);
    info.flags = ENGINE_TEXTURE_SRGB;
    info.width = W;
    info.height = H;
    info.name = S(name);
    memset(&texture, 0, sizeof(texture));
    CHECK_STATUS(g_api->texture_create(engine, &info, &texture), ENGINE_OK);
    memset(&update, 0, sizeof(update));
    update.struct_size = sizeof(update);
    CHECK_STATUS(g_api->texture_update(texture, pixels, (uint32_t)W * 4, &update), ENGINE_OK);
    free(pixels);
    return texture;
}

static engine_handle MakeScreenTexture(engine_handle engine, const char* name, unsigned char r, unsigned char g,
                                       unsigned char b)
{
    return MakeSizedTexture(engine, name, 64, 36, r, g, b);
}

/* The colour classes of the fitting test, told apart by hue so that the lighting does not matter. */
enum
{
    kOther = 0,
    kPicture = 1, /* yellow: the test picture and its darker frame */
    kBar = 2      /* magenta: the colour outside the picture */
};

static int ClassOf(const unsigned char* p)
{
    const int r = p[0];
    const int g = p[1];
    const int b = p[2];
    if (r > b + 30 && g > b + 30)
    {
        return kPicture;
    }
    if (r > g + 30 && b > g + 30)
    {
        return kBar;
    }
    return kOther;
}

/* Scans the row through the screen at world point (wx, wy, wz) outward from that pixel while the pixels are picture or
 * bar, and reports the widths: bars on the left, picture, bars on the right, and `broken` when the order is not bars,
 * picture, bars (a bleed shows as a picture pixel inside a bar). Returns 0 when the point is off the image. */
static int ScanScreenRow(engine_handle surface, const engine_frame_image* image, float wx, float wy, float wz,
                         int* left_bar, int* picture, int* right_bar, int* broken)
{
    const double point[3] = {wx, wy, wz};
    float out[3] = {0, 0, 0};
    int cx;
    int y;
    int x0;
    int x1;
    int x;
    int phase = 0; /* 0 left bar, 1 picture, 2 right bar */
    *left_bar = *picture = *right_bar = *broken = 0;
    if (g_api->surface_project(surface, point, out) != ENGINE_OK)
    {
        return 0;
    }
    cx = (int)out[0];
    y = (int)out[1];
    if (cx < 1 || y < 0 || cx >= (int)image->width - 1 || y >= (int)image->height)
    {
        return 0;
    }
    x0 = cx;
    while (x0 > 0 && ClassOf(PixelOf(image, (unsigned)(x0 - 1), (unsigned)y)) != kOther)
    {
        --x0;
    }
    x1 = cx;
    while (x1 < (int)image->width - 1 && ClassOf(PixelOf(image, (unsigned)(x1 + 1), (unsigned)y)) != kOther)
    {
        ++x1;
    }
    for (x = x0; x <= x1; ++x)
    {
        const int c = ClassOf(PixelOf(image, (unsigned)x, (unsigned)y));
        if (c == kBar)
        {
            if (phase == 1)
            {
                phase = 2;
            }
            *(phase == 0 ? left_bar : right_bar) += 1;
        }
        else
        {
            if (phase == 2)
            {
                *broken = 1; /* picture again after the right bar */
            }
            phase = 1;
            *picture += 1;
        }
    }
    return 1;
}

/* The picture's pixel where the world point is, or 0 when it is off the picture. */
static int PixelAt(engine_handle surface, const engine_frame_image* image, float wx, float wy, float wz,
                   unsigned char* rgb)
{
    const double point[3] = {wx, wy, wz};
    float out[3] = {0, 0, 0};
    int x;
    int y;
    if (g_api->surface_project(surface, point, out) != ENGINE_OK)
    {
        return 0;
    }
    x = (int)out[0];
    y = (int)out[1];
    if (x < 0 || y < 0 || x >= (int)image->width || y >= (int)image->height)
    {
        return 0;
    }
    memcpy(rgb, PixelOf(image, (unsigned)x, (unsigned)y), 3);
    return 1;
}

/* Sets "albedo_tex" to `texture` and the fit of a picture of aspect `aspect` on the 16:9 screen with magenta bars. */
static void SetFit(engine_handle world, uint64_t key, engine_handle texture, float aspect, int clear_fit)
{
    engine_slot_override value;
    const float screen = 16.0f / 9.0f;
    memset(&value, 0, sizeof(value));
    value.struct_size = sizeof(value);
    value.kind = ENGINE_SLOT_HOST_TEXTURE;
    value.host_texture = texture;
    CHECK_STATUS(g_api->entity_set_mesh_slot_override(world, key, S("Screen"), S("albedo_tex"), &value), ENGINE_OK);
    if (clear_fit)
    {
        CHECK_STATUS(g_api->entity_clear_slot_override(world, key, S("albedo_uv")), ENGINE_OK);
        CHECK_STATUS(g_api->entity_clear_slot_override(world, key, S("albedo_uv_outside")), ENGINE_OK);
        return;
    }
    memset(&value, 0, sizeof(value));
    value.struct_size = sizeof(value);
    value.kind = ENGINE_SLOT_VEC4;
    value.value[0] = 1.0f;
    value.value[1] = 1.0f;
    if (aspect < screen)
    {
        value.value[0] = screen / aspect;
        value.value[2] = (1.0f - screen / aspect) / 2.0f;
    }
    else if (aspect > screen)
    {
        value.value[1] = aspect / screen;
        value.value[3] = (1.0f - aspect / screen) / 2.0f;
    }
    CHECK_STATUS(g_api->entity_set_mesh_slot_override(world, key, S("Screen"), S("albedo_uv"), &value), ENGINE_OK);
    memset(&value, 0, sizeof(value));
    value.struct_size = sizeof(value);
    value.kind = ENGINE_SLOT_COLOR;
    value.value[0] = 0.8f;
    value.value[2] = 0.8f;
    value.value[3] = 1.0f;
    CHECK_STATUS(g_api->entity_set_mesh_slot_override(world, key, S("Screen"), S("albedo_uv_outside"), &value),
                 ENGINE_OK);
}

/* Draws until the changes made so far show (the frames in flight were drawn before them), and takes a picture. */
static void Settle(engine_handle engine, engine_handle surface, engine_frame_image* image)
{
    int i;
    for (i = 0; i < 5; ++i)
    {
        CHECK(Acquire(engine, surface, image));
        CHECK_STATUS(g_api->surface_release_frame(surface, image), ENGINE_OK);
    }
    CHECK(Acquire(engine, surface, image));
}

/* A 4:3 and a 9:16 picture fitted into the 16:9 screen of the middle robot with a texture-coordinate transform and a
 * colour outside the picture: the picture is centred and as wide as its aspect says, the bars are the colour with no
 * picture pixel inside them, and nothing else of the scene changes. Ends with the robot as it was. */
static void RunFitting(engine_handle engine, engine_handle surface, engine_handle world, const uint64_t* screen_keys,
                       const float* xs, unsigned char (*before)[3], engine_handle own_texture,
                       engine_frame_image* final_image)
{
    engine_frame_image image;
    engine_handle wide = {0};
    engine_handle tall = {0};
    unsigned char others[3][3];
    int left;
    int picture;
    int right;
    int broken;
    int total;
    int i;
    int k;
    wide = MakeSizedTexture(engine, "fit 4:3", 64, 48, 230, 220, 30);
    tall = MakeSizedTexture(engine, "fit 9:16", 36, 64, 230, 220, 30);

    SetFit(world, screen_keys[1], wide, 4.0f / 3.0f, 0);
    Settle(engine, surface, &image);
    CHECK(ScanScreenRow(surface, &image, xs[1], 1.06f, 0.25f, &left, &picture, &right, &broken));
    total = left + picture + right;
    if (verbose_dump())
    {
        printf("4:3 on 16:9: %d bar, %d picture, %d bar\n", left, picture, right);
    }
    CHECK(total > 40 && !broken);
    CHECK(left > 0 && right > 0 && picture > 0);
    CHECK(left - right > -3 && left - right < 3);                    /* centred */
    CHECK(picture * 100 > total * 70 && picture * 100 < total * 80); /* 0.75 of the width */
    /* the other robots' screens and the bodies are as before */
    for (i = 0; i < 3; i += 2)
    {
        CHECK(PixelAt(surface, &image, xs[i], 1.06f, 0.25f, others[i]));
        for (k = 0; k < 3; ++k)
        {
            const int d = (int)others[i][k] - (int)before[i][k];
            CHECK(d > -12 && d < 12);
        }
    }
    CHECK_STATUS(g_api->surface_release_frame(surface, &image), ENGINE_OK);

    SetFit(world, screen_keys[1], tall, 9.0f / 16.0f, 0);
    Settle(engine, surface, &image);
    CHECK(ScanScreenRow(surface, &image, xs[1], 1.06f, 0.25f, &left, &picture, &right, &broken));
    total = left + picture + right;
    if (verbose_dump())
    {
        printf("9:16 on 16:9: %d bar, %d picture, %d bar\n", left, picture, right);
    }
    CHECK(total > 40 && !broken);
    CHECK(left - right > -3 && left - right < 3);
    CHECK(picture * 100 > total * 27 && picture * 100 < total * 36); /* 0.316 of the width */
    CHECK_STATUS(g_api->surface_release_frame(surface, &image), ENGINE_OK);

    /* clearing the transform shows the picture stretched over the whole screen, with no bar */
    SetFit(world, screen_keys[1], tall, 9.0f / 16.0f, 1);
    Settle(engine, surface, &image);
    CHECK(ScanScreenRow(surface, &image, xs[1], 1.06f, 0.25f, &left, &picture, &right, &broken));
    CHECK(left == 0 && right == 0 && picture > 40);
    CHECK_STATUS(g_api->surface_release_frame(surface, &image), ENGINE_OK);

    /* a destroyed host texture reverts the slot to the material's own value (counted), the transform stays */
    SetFit(world, screen_keys[1], tall, 9.0f / 16.0f, 0);
    CHECK_STATUS(g_api->texture_destroy(tall), ENGINE_OK);
    {
        engine_frame_info finfo;
        engine_frame_stats fstats;
        memset(&finfo, 0, sizeof(finfo));
        finfo.struct_size = sizeof(finfo);
        finfo.delta_seconds = 1.0 / 30.0;
        memset(&fstats, 0, sizeof(fstats));
        fstats.struct_size = sizeof(fstats);
        CHECK_STATUS(g_api->frame(engine, &finfo, &fstats), ENGINE_OK);
        CHECK(fstats.slots_reverted >= 1);
    }
    Settle(engine, surface, &image);
    CHECK_STATUS(g_api->surface_release_frame(surface, &image), ENGINE_OK);

    /* the robot as it was */
    SetFit(world, screen_keys[1], own_texture, 16.0f / 9.0f, 1);
    CHECK_STATUS(g_api->texture_destroy(wide), ENGINE_OK);
    Settle(engine, surface, final_image);
}

static void RunScene(engine_handle engine, const engine_import_result* robot, const engine_import_result* field)
{
    engine_handle world;
    engine_handle surface;
    engine_handle textures[3];
    engine_surface_info info;
    engine_environment environment;
    engine_light light;
    engine_camera camera;
    engine_play_params play;
    engine_spawn_params place;
    engine_frame_image image;
    engine_surface_state state;
    uint64_t camera_entity = 0;
    uint64_t sun = 0;
    uint64_t robot_level = IdAt(robot, robot->resource_count - 1);
    uint64_t field_level = IdAt(field, field->resource_count - 1);
    uint64_t walk = FindResource(robot, ENGINE_RESOURCE_ANIMATION, "Walk");
    uint64_t idle = FindResource(robot, ENGINE_RESOURCE_ANIMATION, "Idle");
    uint64_t root = 0;
    uint64_t keys[64];
    size_t total = 0;
    uint64_t roots[3];
    int robots_screens[3] = {0, 0, 0};
    uint64_t walker_key = 0;
    uint64_t screen_keys[3] = {0, 0, 0};
    uint64_t walker_keys[64];
    size_t walker_count = 0;
    int played_count = 0;
    unsigned char colors[3][3] = {{230, 30, 30}, {30, 220, 30}, {40, 40, 240}};
    unsigned char rgb[3][3];
    unsigned char body[3][3];
    unsigned char* still_copy = NULL;
    unsigned long long moving;
    int i;
    int k;
    const float xs[3] = {-1.2f, 0.0f, 1.2f};

    CHECK(walk != 0 && idle != 0 && robot_level != 0 && field_level != 0);
    memset(&surface, 0, sizeof(surface));
    memset(&info, 0, sizeof(info));
    info.struct_size = sizeof(info);
    info.kind = ENGINE_SURFACE_OFFSCREEN;
    info.width = WIDTH;
    info.height = HEIGHT;
    info.scale = 1.0f;
    info.flags = host_surface_api_flags(); /* Windows: Direct3D 12 or Vulkan (Day 381) */
    if (g_api->surface_create(engine, &info, &surface) != ENGINE_OK)
    {
        printf("host_acceptance_scene: skipped: no device could be made\n");
        return;
    }
    memset(&world, 0, sizeof(world));
    CHECK_STATUS(g_api->world_create(engine, &world), ENGINE_OK);
    memset(&environment, 0, sizeof(environment));
    environment.struct_size = sizeof(environment);
    environment.ambient_color[0] = environment.ambient_color[1] = environment.ambient_color[2] = 1.0f;
    environment.ambient_intensity = 0.35f;
    environment.sky_color[0] = 0.45f;
    environment.sky_color[1] = 0.65f;
    environment.sky_color[2] = 0.9f;
    environment.exposure = 1.0f;
    CHECK_STATUS(g_api->world_set_environment(world, &environment), ENGINE_OK);
    CHECK_STATUS(g_api->entity_create(world, &sun), ENGINE_OK);
    {
        engine_transform t;
        memset(&t, 0, sizeof(t));
        t.struct_size = sizeof(t);
        t.position[1] = 10.0f;
        t.rotation[0] = -0.38f;
        t.rotation[1] = 0.0f;
        t.rotation[3] = 0.92f;
        t.scale[0] = t.scale[1] = t.scale[2] = 1.0f;
        CHECK_STATUS(g_api->component_set(world, sun, ENGINE_COMPONENT_TRANSFORM, &t), ENGINE_OK);
    }
    memset(&light, 0, sizeof(light));
    light.struct_size = sizeof(light);
    light.kind = ENGINE_LIGHT_DIRECTIONAL;
    light.color[0] = light.color[1] = light.color[2] = 1.0f;
    light.intensity = 1.5f;
    CHECK_STATUS(g_api->component_set(world, sun, ENGINE_COMPONENT_LIGHT, &light), ENGINE_OK);
    CHECK_STATUS(g_api->entity_create(world, &camera_entity), ENGINE_OK);
    Place(world, camera_entity, 0.0f, 1.1f, 2.6f);
    memset(&camera, 0, sizeof(camera));
    camera.struct_size = sizeof(camera);
    camera.fov_y = 1.0f;
    camera.near_plane = 0.1f;
    camera.far_plane = 200.0f;
    CHECK_STATUS(g_api->component_set(world, camera_entity, ENGINE_COMPONENT_CAMERA, &camera), ENGINE_OK);
    CHECK_STATUS(g_api->surface_set_camera(surface, camera_entity), ENGINE_OK);
    CHECK_STATUS(g_api->surface_set_world(surface, world), ENGINE_OK);

    /* the meadow, and three robots */
    CHECK_STATUS(g_api->level_spawn(world, field_level, NULL, &root, keys, 0, &total), ENGINE_OK);
    CHECK(total > 100); /* the ground and 316 trees */
    memset(&play, 0, sizeof(play));
    play.struct_size = sizeof(play);
    play.flags = ENGINE_PLAY_LOOP;
    for (i = 0; i < 3; ++i)
    {
        size_t spawned = 0;
        char prefix[8];
        memset(&place, 0, sizeof(place));
        place.struct_size = sizeof(place);
        place.position[0] = xs[i];
        place.rotation[3] = 1.0f;
        place.scale = 1.0f;
        snprintf(prefix, sizeof(prefix), "r%d", i);
        place.name_prefix = S(prefix);
        CHECK_STATUS(g_api->level_spawn(world, robot_level, &place, &roots[i], keys, 64, &spawned), ENGINE_OK);
        CHECK(spawned > 1 && spawned <= 64);
        textures[i] = MakeScreenTexture(engine,
                                        i == 0   ? "screen red"
                                        : i == 1 ? "screen green"
                                                 : "screen blue",
                                        colors[i][0], colors[i][1], colors[i][2]);
        for (k = 0; k < (int)spawned && k < 64; ++k)
        {
            engine_slot_override value;
            int played;
            memset(&value, 0, sizeof(value));
            value.struct_size = sizeof(value);
            value.kind = ENGINE_SLOT_HOST_TEXTURE;
            value.host_texture = textures[i];
            if (g_api->entity_set_mesh_slot_override(world, keys[k], S("Screen"), S("albedo_tex"), &value) == ENGINE_OK)
            {
                ++robots_screens[i];
                screen_keys[i] = keys[k];
            }
            play.speed = i == 0 ? 1.0f : 0.0f; /* the first walks, the others stand */
            played = g_api->animation_play(world, keys[k], i == 0 ? walk : idle, &play) == ENGINE_OK;
            played_count += played ? 1 : 0;
            if (played && i == 0)
            {
                walker_key = keys[k];
                walker_keys[walker_count++] = keys[k];
            }
        }
        CHECK(robots_screens[i] == 1); /* exactly one entity of each robot has a Screen slot */
    }

    /* the pictures of a moving walker differ from frame to frame */
    CHECK(Acquire(engine, surface, &image));
    CHECK(image.width == WIDTH && image.height == HEIGHT && image.stride == WIDTH * 4);
    moving = Checksum(&image);
    CHECK_STATUS(g_api->surface_release_frame(surface, &image), ENGINE_OK);
    for (i = 0; i < 5; ++i)
    {
        CHECK(Frame(engine));
    }
    CHECK(Acquire(engine, surface, &image));
    CHECK(Checksum(&image) != moving);
    CHECK_STATUS(g_api->surface_release_frame(surface, &image), ENGINE_OK);

    /* everybody stands still: the same picture every time from here on */
    CHECK(walker_key != 0);
    for (k = 0; k < (int)walker_count; ++k)
    {
        CHECK_STATUS(g_api->animation_set_speed(world, walker_keys[k], 0.0f), ENGINE_OK);
    }
    if (verbose_dump())
    {
        printf("animations started: %d, on the walker: %d\n", played_count, (int)walker_count);
    }
    for (i = 0; i < 4; ++i)
    { /* the frames in flight were drawn before the walker stopped */
        CHECK(Acquire(engine, surface, &image));
        CHECK_STATUS(g_api->surface_release_frame(surface, &image), ENGINE_OK);
    }
    CHECK(Acquire(engine, surface, &image));
    still_copy = (unsigned char*)malloc((size_t)image.stride * image.height);
    memcpy(still_copy, image.data, (size_t)image.stride * image.height);
    CHECK(Differing(&image, still_copy) == 0);

    /* each screen shows its own colour; the robots' other slots do not */
    for (i = 0; i < 3; ++i)
    {
        CHECK(PixelAt(surface, &image, xs[i], 1.06f, 0.25f, rgb[i]));
        CHECK(PixelAt(surface, &image, xs[i], 0.8f, 0.12f, body[i]));
        if (verbose_dump())
        {
            printf("screen %d: %u %u %u, body %u %u %u\n", i, rgb[i][0], rgb[i][1], rgb[i][2], body[i][0], body[i][1],
                   body[i][2]);
        }
    }
    CHECK(rgb[0][0] > rgb[0][1] + 40 && rgb[0][0] > rgb[0][2] + 40);
    CHECK(rgb[1][1] > rgb[1][0] + 40 && rgb[1][1] > rgb[1][2] + 40);
    CHECK(rgb[2][2] > rgb[2][0] + 40 && rgb[2][2] > rgb[2][1] + 40);
    for (i = 1; i < 3; ++i)
    {
        for (k = 0; k < 3; ++k)
        {
            const int d = (int)body[i][k] - (int)body[0][k];
            CHECK(d > -12 && d < 12); /* the bodies are one material, not painted with a screen */
        }
    }
    if (getenv("HOST_DUMP") != NULL)
    { /* a PPM of the picture, to look at */
        FILE* out = fopen(getenv("HOST_DUMP"), "wb");
        if (out != NULL)
        {
            unsigned px;
            fprintf(out, "P6\n%u %u\n255\n", image.width, image.height);
            for (px = 0; px < image.width * image.height; ++px)
            {
                fwrite((const unsigned char*)image.data + px * 4, 1, 3, out);
            }
            fclose(out);
        }
    }
    CHECK_STATUS(g_api->surface_release_frame(surface, &image), ENGINE_OK);
    CHECK_STATUS(StateOf(surface, &state), ENGINE_OK);
    if (verbose_dump())
    {
        printf("drawn %u unresolved %llu\n", state.objects_drawn, (unsigned long long)state.entities_unresolved);
    }
    CHECK(state.entities_unresolved == 0 && state.objects_drawn > 100);
    RunFitting(engine, surface, world, screen_keys, xs, rgb, textures[1], &image);
    /* the idle animations moved on while the fitting was drawn: the pictures from here on are compared with this one */
    memcpy(still_copy, image.data, (size_t)image.stride * image.height);
    CHECK(PixelAt(surface, &image, xs[1], 1.06f, 0.25f, rgb[1]));
    CHECK(rgb[1][1] > rgb[1][0] + 40 && rgb[1][1] > rgb[1][2] + 40); /* the robot's own green screen is back */
    CHECK_STATUS(g_api->surface_release_frame(surface, &image), ENGINE_OK);
    /* a resize mid-run: the pictures follow, and the old size comes back as it was */
    CHECK_STATUS(g_api->surface_resized(surface, 640, 360, 1.0f), ENGINE_OK);
    for (k = 0; k < 20; ++k)
    {
        CHECK(Acquire(engine, surface, &image));
        i = (int)image.width;
        CHECK_STATUS(g_api->surface_release_frame(surface, &image), ENGINE_OK);
        if (i == 640)
        {
            break;
        }
    }
    CHECK(i == 640);
    CHECK_STATUS(g_api->surface_resized(surface, (uint32_t)WIDTH, (uint32_t)HEIGHT, 1.0f), ENGINE_OK);
    for (k = 0; k < 20; ++k)
    {
        CHECK(Acquire(engine, surface, &image));
        i = (int)image.width;
        if (i == (int)WIDTH)
        {
            break;
        }
        CHECK_STATUS(g_api->surface_release_frame(surface, &image), ENGINE_OK);
    }
    if (verbose_dump())
    {
        printf("after the resize: %d wide, %u pixels differ\n", i, (unsigned)Differing(&image, still_copy));
    }
    CHECK(i == (int)WIDTH && Differing(&image, still_copy) < kSame); /* dither and nothing else */
    CHECK_STATUS(g_api->surface_release_frame(surface, &image), ENGINE_OK);

    /* a lost device: a new one, and the same picture again without the host making anything */
    {
        const engine_string name = {"engine.test_state", sizeof("engine.test_state") - 1};
        const struct engine_test_extension* hook = (const struct engine_test_extension*)g_api->get_extension(name);
        if (hook != NULL && hook->size >= offsetof(struct engine_test_extension, surface_lose_device) + sizeof(void*))
        {
            CHECK(hook->surface_lose_device(surface.value) == 1);
            CHECK(Frame(engine));
            CHECK_STATUS(g_api->surface_rebind(surface, &info), ENGINE_OK);
            CHECK(Acquire(engine, surface, &image));
            CHECK(Differing(&image, still_copy) < kSame);
            CHECK_STATUS(g_api->surface_release_frame(surface, &image), ENGINE_OK);
        }
    }

    /* a host that looks only now: many frames passed, it gets the newest and the rest are counted */
    for (i = 0; i < 9; ++i)
    {
        CHECK(Frame(engine));
    }
    CHECK(Acquire(engine, surface, &image));
    CHECK_STATUS(StateOf(surface, &state), ENGINE_OK);
    CHECK(image.frame + 4 >= state.frames_rendered && state.frames_dropped + state.frames_skipped_slow_host >= 5);
    CHECK_STATUS(g_api->surface_release_frame(surface, &image), ENGINE_OK);
    CHECK_STATUS(g_api->surface_destroy(surface), ENGINE_OK);
    for (i = 0; i < 3; ++i)
    {
        CHECK_STATUS(g_api->texture_destroy(textures[i]), ENGINE_OK);
    }
    free(still_copy);
    CHECK_STATUS(g_api->world_destroy(world), ENGINE_OK);
}

int main(void)
{
    Blob robot_file = ReadFixture("robot.glb");
    Blob field_file = ReadFixture("green_field.glb");
    engine_create_info create;
    engine_handle engine;
    engine_info about;
    engine_import_options options;
    engine_import_result robot = {0};
    engine_import_result field = {0};
    int verbose = getenv("HOST_VERBOSE") != NULL;

    if (robot_file.data == NULL || field_file.data == NULL)
    {
        return 2;
    }
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
    if ((about.caps & ENGINE_CAP_SURFACE_OFFSCREEN) == 0 || (about.caps & ENGINE_CAP_IMPORT_GLTF) == 0)
    {
        printf("host_acceptance_scene: skipped: no off-screen surfaces or no importer in this build\n");
        CHECK_STATUS(g_api->destroy(engine), ENGINE_OK);
        return 0;
    }

    memset(&options, 0, sizeof(options));
    options.struct_size = sizeof(options);
    options.flags = ENGINE_IMPORT_EMIT_LEVEL;
    options.id_seed = 20261117;
    options.path_prefix = S("/robot");
    robot.struct_size = sizeof(robot);
    CHECK_STATUS(g_api->import_gltf(engine, robot_file.data, robot_file.size, &options, &robot), ENGINE_OK);
    options.path_prefix = S("/field");
    options.id_seed = 20261118;
    field.struct_size = sizeof(field);
    CHECK_STATUS(g_api->import_gltf(engine, field_file.data, field_file.size, &options, &field), ENGINE_OK);
    if (verbose)
    {
        PrintPaths("robot", &robot);
        PrintPaths("field", &field);
    }
    CHECK(robot.warning_count == 0 && field.warning_count == 0);
    RunScene(engine, &robot, &field);
    ReleaseResult(&robot);
    ReleaseResult(&field);
    CHECK_STATUS(g_api->destroy(engine), ENGINE_OK);
    free(robot_file.data);
    free(field_file.data);
    if (g_failures == 0)
    {
        printf("host_acceptance_scene: ok\n");
        return 0;
    }
    printf("host_acceptance_scene: %d failure(s)\n", g_failures);
    return 1;
}
