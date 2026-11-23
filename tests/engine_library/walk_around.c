/* Phase 24 Day 371: the walk-around sample, a C host that builds a 3D scene through the C API alone: three robots (a
   skinned mesh with a skeleton; one walks, two stand), three screens that show a pattern each, fed by a thread of their
   own, name tags over the robots' heads, a sun and an ambient term, and a first-person camera the keys and the mouse
   move (W A S D, the mouse looks, Escape quits). It reads the minted blobs of the engine's test data (the meadow and
   sample robot assets are not in this checkout: the day file says so).

   With `--frames N` it is a test: it draws N frames without waiting, stops the screens' threads, reads the picture,
   rebinds the surface to a new window (what a lost device or a moved window does) and checks that the same picture
   comes back without the host making anything again, and that the stats show nothing refused or unresolved. Exit 0 also
   when there is no display or no device. */
#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "host_os.h"
#include "host_window.h"
#include "engine/engine_c.h"

#define WIDTH 320u
#define HEIGHT 240u
#define SCREENS 3

static const engine_api* g_api = NULL;
static int g_failures = 0;

#define CHECK(condition)                                                                                               \
    do                                                                                                                 \
    {                                                                                                                  \
        if (!(condition))                                                                                              \
        {                                                                                                              \
            printf("walk_around: line %d: CHECK failed: %s\n", __LINE__, #condition);                                  \
            ++g_failures;                                                                                              \
        }                                                                                                              \
    } while (0)

#define CHECK_STATUS(call, expected) CHECK((call) == (expected))

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
        printf("walk_around: cannot open %s\n", path);
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

static engine_string Str(const char* text)
{
    engine_string s;
    s.data = text;
    s.length = strlen(text);
    return s;
}

static int AddBlob(engine_handle engine, uint64_t id, const char* name)
{
    engine_resource_add_info info;
    Blob blob = ReadBlob(name);
    if (blob.data == NULL)
    {
        return 0;
    }
    memset(&info, 0, sizeof(info));
    info.struct_size = sizeof(info);
    info.id = id;
    info.bytes = blob.data;
    info.size = blob.size;
    CHECK_STATUS(g_api->resource_add(engine, &info), ENGINE_OK); /* the engine copies the bytes */
    free(blob.data);
    return 1;
}

static void Place(engine_handle world, uint64_t entity, const float* position, const float* rotation)
{
    engine_transform t;
    memset(&t, 0, sizeof(t));
    t.struct_size = sizeof(t);
    t.position[0] = position[0];
    t.position[1] = position[1];
    t.position[2] = position[2];
    t.rotation[3] = 1.0f;
    if (rotation != NULL)
    {
        memcpy(t.rotation, rotation, sizeof(t.rotation));
    }
    t.scale[0] = t.scale[1] = t.scale[2] = 1.0f;
    CHECK_STATUS(g_api->component_set(world, entity, ENGINE_COMPONENT_TRANSFORM, &t), ENGINE_OK);
}

/* ---- the screens: each is a host texture a thread of its own paints */

typedef struct ScreenFeed
{
    engine_handle texture;
    int index;
    int stop;
    host_thread thread;
} ScreenFeed;

static void PaintScreen(void* user)
{
    ScreenFeed* screen = (ScreenFeed*)user;
    unsigned char pixels[64 * 64 * 4];
    unsigned phase = 0;
    while (host_atomic_load(&screen->stop) == 0)
    {
        unsigned x;
        unsigned y;
        for (y = 0; y < 64; ++y)
        {
            for (x = 0; x < 64; ++x)
            {
                unsigned char* p = pixels + (y * 64 + x) * 4;
                p[0] = (unsigned char)(screen->index == 0 ? (x + phase) * 4 : 40);
                p[1] = (unsigned char)(screen->index == 1 ? (y + phase) * 4 : 40);
                p[2] = (unsigned char)(screen->index == 2 ? (x + y + phase) * 2 : 40);
                p[3] = 255;
            }
        }
        g_api->texture_update(screen->texture, pixels, 64 * 4, NULL);
        ++phase;
        host_sleep_ms(20);
    }
    g_api->thread_detach();
}

/* ---- a checksum of the frame, to compare two pictures */

static unsigned long long Checksum(const engine_pixels* pixels)
{
    unsigned long long sum = 1469598103934665603ull;
    size_t i;
    for (i = 0; i < pixels->data.size; ++i)
    {
        sum = (sum ^ ((const unsigned char*)pixels->data.data)[i]) * 1099511628211ull;
    }
    return sum;
}

static int Frame(engine_handle engine, engine_handle surface, engine_pixels* pixels, double seconds)
{
    engine_frame_info frame;
    engine_handle ticket;
    memset(&frame, 0, sizeof(frame));
    frame.struct_size = sizeof(frame);
    frame.delta_seconds = seconds;
    if (pixels == NULL)
    {
        return g_api->frame(engine, &frame, NULL) == ENGINE_OK;
    }
    memset(&ticket, 0, sizeof(ticket));
    pixels->struct_size = sizeof(*pixels);
    return g_api->surface_read_pixels(surface, &ticket) == ENGINE_OK &&
           g_api->frame(engine, &frame, NULL) == ENGINE_OK && g_api->pixels_finish(ticket, pixels) == ENGINE_OK &&
           g_api->pixels_destroy(ticket) == ENGINE_OK;
}

int main(int argc, char** argv)
{
    host_window* window;
    host_window* other;
    const char* why = NULL;
    engine_create_info create;
    engine_handle engine;
    engine_handle world;
    engine_handle surface;
    engine_surface_info info;
    engine_surface_state state;
    engine_info about;
    engine_pixels pixels;
    engine_light light;
    engine_camera camera;
    engine_environment environment;
    engine_texture_info tinfo;
    engine_slot_override slot;
    engine_play_params play;
    engine_label label;
    ScreenFeed screens[SCREENS];
    uint64_t robots[3];
    uint64_t camera_entity;
    uint64_t sun;
    float yaw = 0.0f;
    float pitch = 0.0f;
    float eye[3] = {0.0f, 1.6f, 6.0f};
    int frames_limit = -1;
    int offscreen = 0;
    int shown = 0;
    int acquired = 0;
    int frame_count = 0;
    float eye_before_key = 0.0f;
    int running = 1;
    int keys[4] = {0, 0, 0, 0};
    int last_x = -1;
    int i;
    for (i = 1; i < argc; ++i)
    {
        offscreen = offscreen || strcmp(argv[i], "--offscreen") == 0;
    }
    for (i = 1; i + 1 < argc; ++i)
    {
        if (strcmp(argv[i], "--frames") == 0)
        {
            frames_limit = atoi(argv[i + 1]);
        }
    }
    host_quiet_failures(); /* Windows: report failures on stderr, never in a dialog */
    if (!host_windows_begin(&why))
    {
        printf("walk_around: skipped: %s\n", why);
        return 0;
    }
    if (engine_get_api(ENGINE_ABI_VERSION_MAJOR, ENGINE_ABI_VERSION_MINOR, sizeof(engine_api), &g_api) != ENGINE_OK)
    {
        return 2;
    }
    memset(&create, 0, sizeof(create));
    create.struct_size = sizeof(create);
    create.flags = ENGINE_CREATE_LOG_SUPPRESS_CONSOLE;
    memset(&engine, 0, sizeof(engine));
    CHECK_STATUS(g_api->create(&create, &engine), ENGINE_OK);
    memset(&about, 0, sizeof(about));
    about.struct_size = sizeof(about);
    g_api->get_info(engine, &about);
    if ((about.caps & (offscreen ? ENGINE_CAP_SURFACE_OFFSCREEN : host_window_surface_cap())) == 0 ||
        (about.caps & ENGINE_CAP_WORLD_ON_SURFACE) == 0)
    {
        printf("walk_around: skipped: this build draws no world into this platform's windows\n");
        g_api->destroy(engine);
        host_windows_end();
        return 0;
    }
    window = host_window_open(WIDTH, HEIGHT);
    if (window == NULL)
    {
        printf("walk_around: no window\n");
        g_api->destroy(engine);
        host_windows_end();
        return 1;
    }
    memset(&info, 0, sizeof(info));
    info.struct_size = sizeof(info);
    host_window_describe(window, &info);
    if (offscreen)
    { /* the engine draws into a buffer of its own; the host shows each picture in its window (host_window_show) */
        info.kind = ENGINE_SURFACE_OFFSCREEN;
        info.scale = 1.0f;
        info.display = NULL;
        info.surface = NULL;
        info.window_id = 0;
    }
    info.flags = host_surface_api_flags(); /* Windows: HOST_SURFACE_API, Direct3D 12 or Vulkan, on a window or
                                              off-screen (Day 381); kept by the later describe */
    memset(&surface, 0, sizeof(surface));
    if (g_api->surface_create(engine, &info, &surface) != ENGINE_OK)
    {
        printf("walk_around: skipped: no device can present to this window\n");
        g_api->destroy(engine);
        host_window_close(window);
        host_windows_end();
        return 0;
    }

    /* 1. The content: meshes, a material, a skeleton, a clip and a font, added once. */
    CHECK(AddBlob(engine, 0x0d01ull, "brick.tex.blob") && AddBlob(engine, 0x0e01ull, "brick.mat.blob") &&
          AddBlob(engine, 0x0b03ull, "scene.mesh.blob") && AddBlob(engine, 0xa01ull, "hero.skel.blob") &&
          AddBlob(engine, 0xc01ull, "idle.anim.blob") && AddBlob(engine, 0xb05ull, "skinned.mesh.blob"));
    {
        char path[1024];
        FILE* f;
        snprintf(path, sizeof(path), "%s/%s", LABEL_FONT_DIR, LABEL_FONT_NAME);
        f = fopen(path, "rb");
        if (f != NULL)
        {
            static unsigned char font[1 << 20];
            const size_t size = fread(font, 1, sizeof(font), f);
            fclose(f);
            CHECK_STATUS(g_api->label_font_set(engine, font, size), ENGINE_OK);
        }
    }

    /* 2. The world: a camera, a sun, an ambient term, three robots with name tags, three screens. */
    memset(&world, 0, sizeof(world));
    CHECK_STATUS(g_api->world_create(engine, &world), ENGINE_OK);
    memset(&environment, 0, sizeof(environment));
    environment.struct_size = sizeof(environment);
    environment.ambient_color[0] = environment.ambient_color[1] = environment.ambient_color[2] = 1.0f;
    environment.ambient_intensity = 0.25f;
    environment.sky_color[2] = 0.35f;
    environment.exposure = 1.0f;
    CHECK_STATUS(g_api->world_set_environment(world, &environment), ENGINE_OK);
    CHECK_STATUS(g_api->entity_create(world, &sun), ENGINE_OK);
    {
        const float q[4] = {-0.5f, 0.2f, 0.0f, 0.84f};
        const float at[3] = {0.0f, 4.0f, 0.0f};
        Place(world, sun, at, q);
    }
    memset(&light, 0, sizeof(light));
    light.struct_size = sizeof(light);
    light.kind = ENGINE_LIGHT_DIRECTIONAL;
    light.color[0] = light.color[1] = light.color[2] = 1.0f;
    light.intensity = 1.2f;
    CHECK_STATUS(g_api->component_set(world, sun, ENGINE_COMPONENT_LIGHT, &light), ENGINE_OK);
    CHECK_STATUS(g_api->entity_create(world, &camera_entity), ENGINE_OK);
    memset(&camera, 0, sizeof(camera));
    camera.struct_size = sizeof(camera);
    camera.fov_y = 1.0f;
    camera.near_plane = 0.1f;
    camera.far_plane = 100.0f;
    CHECK_STATUS(g_api->component_set(world, camera_entity, ENGINE_COMPONENT_CAMERA, &camera), ENGINE_OK);
    memset(&play, 0, sizeof(play));
    play.struct_size = sizeof(play);
    play.flags = ENGINE_PLAY_LOOP;
    for (i = 0; i < 3; ++i)
    {
        engine_skeleton_instance skeleton;
        engine_mesh_reference reference;
        const float at[3] = {-3.0f + 3.0f * (float)i, 0.0f, 0.0f};
        static const char* names[3] = {"Walker", "Idle 1", "Idle 2"};
        CHECK_STATUS(g_api->entity_create(world, &robots[i]), ENGINE_OK);
        Place(world, robots[i], at, NULL);
        memset(&reference, 0, sizeof(reference));
        reference.struct_size = sizeof(reference);
        reference.mesh = 0xb05ull;
        CHECK_STATUS(g_api->component_set(world, robots[i], ENGINE_COMPONENT_MESH_REFERENCE, &reference), ENGINE_OK);
        memset(&skeleton, 0, sizeof(skeleton));
        skeleton.struct_size = sizeof(skeleton);
        skeleton.skeleton = 0xa01ull;
        CHECK_STATUS(g_api->component_set(world, robots[i], ENGINE_COMPONENT_SKELETON_INSTANCE, &skeleton), ENGINE_OK);
        play.speed = i == 0 ? 1.0f : 0.0f; /* the first one walks, the others stand */
        CHECK_STATUS(g_api->animation_play(world, robots[i], 0xc01ull, &play), ENGINE_OK);
        memset(&label, 0, sizeof(label));
        label.struct_size = sizeof(label);
        label.text = Str(names[i]);
        label.color[0] = label.color[1] = label.color[2] = label.color[3] = 1.0f;
        label.size = 14.0f;
        label.offset[1] = 2.2f;
        CHECK_STATUS(g_api->component_set(world, robots[i], ENGINE_COMPONENT_LABEL, &label), ENGINE_OK);
    }
    memset(&tinfo, 0, sizeof(tinfo));
    tinfo.struct_size = sizeof(tinfo);
    tinfo.width = 64;
    tinfo.height = 64;
    tinfo.flags = ENGINE_TEXTURE_SRGB;
    memset(&slot, 0, sizeof(slot));
    slot.struct_size = sizeof(slot);
    slot.kind = ENGINE_SLOT_HOST_TEXTURE;
    for (i = 0; i < SCREENS; ++i)
    {
        uint64_t entity;
        engine_mesh_reference reference;
        const float at[3] = {-2.2f + 2.2f * (float)i, 0.0f, -3.0f};
        memset(&screens[i], 0, sizeof(screens[i]));
        screens[i].index = i;
        CHECK_STATUS(g_api->texture_create(engine, &tinfo, &screens[i].texture), ENGINE_OK);
        CHECK_STATUS(g_api->entity_create(world, &entity), ENGINE_OK);
        Place(world, entity, at, NULL);
        memset(&reference, 0, sizeof(reference));
        reference.struct_size = sizeof(reference);
        reference.mesh = 0x0b03ull;
        reference.material_resource = 0x0e01ull;
        CHECK_STATUS(g_api->component_set(world, entity, ENGINE_COMPONENT_MESH_REFERENCE, &reference), ENGINE_OK);
        slot.host_texture = screens[i].texture;
        CHECK_STATUS(g_api->entity_set_slot_override(world, entity, Str("albedo_tex"), &slot), ENGINE_OK);
        CHECK(host_thread_start(&screens[i].thread, PaintScreen, &screens[i]) == 0);
    }
    CHECK_STATUS(g_api->surface_set_world(surface, world), ENGINE_OK);
    CHECK_STATUS(g_api->surface_set_camera(surface, camera_entity), ENGINE_OK);

    /* 3. The loop: input, the camera, one frame. */
    while (running && (frames_limit < 0 || frame_count < frames_limit))
    {
        float forward[3];
        float q[4];
        const float cy = cosf(yaw * 0.5f);
        const float sy = sinf(yaw * 0.5f);
        const float cx = cosf(pitch * 0.5f);
        const float sx = sinf(pitch * 0.5f);
        host_window_event event;
        while (host_window_poll(window, &event))
        {
            if (event.type == HOST_EVENT_KEY)
            {
                keys[0] = event.key == HOST_KEY_W ? event.down : keys[0];
                keys[1] = event.key == HOST_KEY_S ? event.down : keys[1];
                keys[2] = event.key == HOST_KEY_A ? event.down : keys[2];
                keys[3] = event.key == HOST_KEY_D ? event.down : keys[3];
                running = running && !(event.down && event.key == HOST_KEY_ESCAPE);
            }
            else if (event.type == HOST_EVENT_MOTION &&
                     frames_limit < 0) /* a test ignores the pointer: its key check needs yaw 0 */
            {
                if (last_x >= 0)
                {
                    yaw -= (float)(event.x - last_x) * 0.005f;
                }
                last_x = event.x;
            }
        }
        forward[0] = -sinf(yaw);
        forward[1] = 0.0f;
        forward[2] = -cosf(yaw);
        eye[0] += (float)(keys[0] - keys[1]) * forward[0] * 0.08f + (float)(keys[3] - keys[2]) * -forward[2] * 0.08f;
        eye[2] += (float)(keys[0] - keys[1]) * forward[2] * 0.08f + (float)(keys[3] - keys[2]) * forward[0] * 0.08f;
        q[0] = cy * sx;
        q[1] = cx * sy;
        q[2] = -sy * sx;
        q[3] = cy * cx;
        Place(world, camera_entity, eye, q);
        {
            const float at[3] = {-3.0f + 0.02f * (float)frame_count, 0.0f, 0.0f};
            if (frame_count % 400 < 200)
            {
                Place(world, robots[0], at, NULL); /* the walker strolls */
            }
        }
        if (!Frame(engine, surface, NULL, 0.016))
        {
            ++g_failures;
            break;
        }
        if (offscreen)
        { /* the newest finished picture, if there is one yet, into the host's own window */
            engine_frame_image picture;
            memset(&picture, 0, sizeof(picture));
            picture.struct_size = sizeof(picture);
            if (g_api->surface_acquire_frame(surface, &picture) == ENGINE_OK)
            {
                shown += host_window_show(window, picture.data, picture.width, picture.height, picture.stride);
                ++acquired;
                CHECK_STATUS(g_api->surface_release_frame(surface, &picture), ENGINE_OK);
            }
        }
        ++frame_count;
        if (frames_limit < 0)
        {
            host_sleep_ms(16);
        }
        else if (frame_count == 5 || frame_count == 15)
        {
            /* the test's key: W held from frame 5 to frame 15, through the window system's own queue, as a keyboard
             * sends it (Day 370) */
            CHECK(host_window_post_key(window, HOST_KEY_W, frame_count == 5) == 1);
            if (frame_count == 5)
            {
                eye_before_key = eye[2];
            }
        }
    }
    if (frames_limit > 15) /* the release posted after frame 15 is read by the next frame's poll */
    {
        /* the forwarded key moved the camera forward (-z at yaw 0) by about 0.08 per frame it was held */
        if (!(eye[2] < eye_before_key - 0.3f && keys[0] == 0))
        {
            printf("walk_around: the W key did not move the camera (z %.2f before, %.2f after, W %s)\n", eye_before_key,
                   eye[2], keys[0] ? "still down" : "up");
            ++g_failures;
        }
    }

    /* 4. Stop the painters; the picture must come back, identical, on a new window. */
    for (i = 0; i < SCREENS; ++i)
    {
        host_atomic_store(&screens[i].stop, 1);
        host_thread_join(&screens[i].thread);
    }
    if (offscreen)
    { /* no window surface to rebind: the test is that pictures came through (and were shown where the platform can) */
        if (frames_limit >= 0)
        {
            CHECK(acquired >= frame_count / 2);
            CHECK(!host_window_can_show() || shown == acquired); /* every picture taken reached the window (Day 382) */
            printf("walk_around: off-screen: %d of %d frames taken, %d shown in the window\n", acquired, frame_count,
                   shown);
        }
    }
    else if (frames_limit >= 0)
    {
        unsigned long long before;
        CHECK(Frame(engine, surface, &pixels, 0.0));
        CHECK(Frame(engine, surface, &pixels, 0.0));
        before = Checksum(&pixels);
        memset(&state, 0, sizeof(state));
        state.struct_size = sizeof(state);
        CHECK_STATUS(g_api->surface_state(surface, &state), ENGINE_OK);
        CHECK(state.entities_unresolved == 0 && state.objects_drawn >= 6);
        CHECK(state.labels_drawn >= 1 && state.textures_uploaded >= SCREENS);
        engine_buffer_release(&pixels.data);
        other = host_window_open(WIDTH, HEIGHT);
        host_window_describe(other, &info);
        CHECK_STATUS(g_api->surface_rebind(surface, &info), ENGINE_OK);
        CHECK(Frame(engine, surface, &pixels, 0.0));
        CHECK(Checksum(&pixels) == before);
        engine_buffer_release(&pixels.data);
        CHECK_STATUS(g_api->surface_state(surface, &state), ENGINE_OK);
        CHECK(state.world_status == ENGINE_OK && state.entities_unresolved == 0);
        host_window_describe(window, &info);
        CHECK_STATUS(g_api->surface_rebind(surface, &info),
                     ENGINE_OK); /* back on the first window before the other goes */
        host_window_close(other);
    }
    for (i = 0; i < SCREENS; ++i)
    {
        CHECK_STATUS(g_api->texture_destroy(screens[i].texture), ENGINE_OK);
    }
    CHECK_STATUS(g_api->surface_destroy(surface), ENGINE_OK);
    CHECK_STATUS(g_api->world_destroy(world), ENGINE_OK);
    CHECK_STATUS(g_api->destroy(engine), ENGINE_OK);
    host_window_close(window);
    host_windows_end();
    if (g_failures != 0)
    {
        printf("walk_around: %d checks failed\n", g_failures);
        return 1;
    }
    printf("walk_around: ok\n");
    return 0;
}
