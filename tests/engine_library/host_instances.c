/* Instanced meshes through the C API: a C program makes a set of 10,000 instances of a mesh it added and a tree of
   nodes, edits them (set_transform, remove, set_local), checks every refusal the API documents and, when a device can
   be made, draws them with an off-screen surface and looks at pixels. Plain C. */
#include <math.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "engine/engine_c.h"
#include "host_surface_api.h"

#define W 64u
#define H 48u
#define MESH_ID 0x0b03ull /* the id inside scene.mesh.blob */

static int g_failures = 0;
static const engine_api* g_api = NULL;

#define CHECK(condition)                                                                                               \
    do                                                                                                                 \
    {                                                                                                                  \
        if (!(condition))                                                                                              \
        {                                                                                                              \
            printf("host_instances: line %d: CHECK failed: %s\n", __LINE__, #condition);                               \
            ++g_failures;                                                                                              \
        }                                                                                                              \
    } while (0)

#define CHECK_STATUS(call, expected)                                                                                   \
    do                                                                                                                 \
    {                                                                                                                  \
        const engine_status got_ = (call);                                                                             \
        if (got_ != (expected))                                                                                        \
        {                                                                                                              \
            printf("host_instances: line %d: %s returned %d, expected %d\n", __LINE__, #call, (int)got_,               \
                   (int)(expected));                                                                                   \
            ++g_failures;                                                                                              \
        }                                                                                                              \
    } while (0)

static void Frames(engine_handle engine, int count)
{
    engine_frame_info frame;
    int i;
    memset(&frame, 0, sizeof(frame));
    frame.struct_size = sizeof(frame);
    frame.delta_seconds = 1.0 / 60.0;
    for (i = 0; i < count; ++i)
    {
        CHECK_STATUS(g_api->frame(engine, &frame, NULL), ENGINE_OK);
    }
}

static engine_instance_placement Placement(double x, double y, double z)
{
    engine_instance_placement p;
    memset(&p, 0, sizeof(p));
    p.struct_size = sizeof(p);
    p.position[0] = x;
    p.position[1] = y;
    p.position[2] = z;
    p.rotation[3] = 1.0f;
    p.scale = 1.0f;
    return p;
}

static engine_instance Item(double x, double y, double z, float scale, float r, float g, float b)
{
    engine_instance item;
    memset(&item, 0, sizeof(item));
    item.position[0] = x;
    item.position[1] = y;
    item.position[2] = z;
    item.rotation[3] = 1.0f;
    item.scale[0] = item.scale[1] = item.scale[2] = scale;
    item.tint[0] = r;
    item.tint[1] = g;
    item.tint[2] = b;
    item.tint[3] = 1.0f;
    return item;
}

static engine_tree_node Node(double x, double y, double z, int parent, uint32_t mesh)
{
    engine_tree_node n;
    memset(&n, 0, sizeof(n));
    n.position[0] = x;
    n.position[1] = y;
    n.position[2] = z;
    n.rotation[3] = 1.0f;
    n.scale[0] = n.scale[1] = n.scale[2] = 1.0f;
    n.tint[0] = n.tint[1] = n.tint[2] = n.tint[3] = 1.0f;
    n.parent = parent;
    n.mesh = mesh;
    return n;
}

static void Api(engine_handle engine)
{
    engine_handle world;
    engine_instance_placement p = Placement(0, 0, 0);
    engine_instance* items = (engine_instance*)malloc(10000 * sizeof(engine_instance));
    uint64_t* ids = (uint64_t*)malloc(10000 * sizeof(uint64_t));
    uint64_t set = 0;
    uint64_t tree = 0;
    uint64_t other = 0;
    uint32_t i;
    engine_instance item = Item(1, 2, 3, 1, 1, 1, 1);
    engine_tree_node nodes[4];
    uint64_t meshes[1] = {MESH_ID};
    float bounds[6] = {-1, -1, -1, 1, 1, 1};

    memset(&world, 0, sizeof(world));
    CHECK_STATUS(g_api->world_create(engine, &world), ENGINE_OK);
    for (i = 0; i < 10000; ++i)
    {
        items[i] = Item((double)(i % 100) * 2.0, 0.0, (double)(i / 100) * 2.0, 1.0f, 1, 1, 1);
    }
    /* a set: create, 10,000 added in one call, one moved, one removed */
    CHECK_STATUS(g_api->instance_set_create(world, &p, MESH_ID, 0, &set), ENGINE_OK);
    CHECK(set != 0);
    CHECK_STATUS(g_api->instance_set_add_many(world, set, items, 10000, sizeof(engine_instance), ids), ENGINE_OK);
    CHECK_STATUS(g_api->instance_set_set_transform(world, set, ids[5], &item, sizeof(item)), ENGINE_OK);
    CHECK_STATUS(g_api->instance_set_remove(world, set, ids[7]), ENGINE_OK);
    CHECK_STATUS(g_api->instance_set_remove(world, set, ids[7]), ENGINE_ERROR_NOT_FOUND);
    CHECK_STATUS(g_api->instance_set_set_transform(world, set, ids[7], &item, sizeof(item)), ENGINE_ERROR_NOT_FOUND);
    /* the refusals */
    CHECK_STATUS(g_api->instance_set_add_many(world, set, items, 10, sizeof(engine_instance) - 8, NULL),
                 ENGINE_ERROR_STRUCT_SIZE);
    CHECK_STATUS(g_api->instance_set_add_many(world, 999999, items, 10, sizeof(engine_instance), NULL),
                 ENGINE_ERROR_NOT_FOUND);
    CHECK_STATUS(g_api->instance_set_add_many(world, set, NULL, 10, sizeof(engine_instance), NULL),
                 ENGINE_ERROR_INVALID_ARGUMENT);
    item.scale[1] = -1.0f;
    CHECK_STATUS(g_api->instance_set_add_many(world, set, &item, 1, sizeof(engine_instance), NULL),
                 ENGINE_ERROR_INVALID_ARGUMENT);
    item.scale[1] = 1.0f;
    item.rotation[3] = 0.0f; /* not a unit rotation */
    CHECK_STATUS(g_api->instance_set_set_transform(world, set, ids[5], &item, sizeof(item)),
                 ENGINE_ERROR_INVALID_ARGUMENT);
    p.struct_size = 8;
    CHECK_STATUS(g_api->instance_set_create(world, &p, MESH_ID, 0, &other), ENGINE_ERROR_STRUCT_SIZE);
    p = Placement(0, 0, 0);
    p.scale = 0.0f;
    CHECK_STATUS(g_api->instance_set_create(world, &p, MESH_ID, 0, &other), ENGINE_ERROR_INVALID_ARGUMENT);
    p = Placement(1e13, 0, 0);
    CHECK_STATUS(g_api->instance_set_update(world, set, &p, MESH_ID), ENGINE_ERROR_INVALID_ARGUMENT);
    p = Placement(0, 0, 0);
    CHECK_STATUS(g_api->instance_set_create(world, &p, MESH_ID, 3, &other), ENGINE_OK);
    CHECK_STATUS(g_api->instance_set_add_many(world, other, items, 4, sizeof(engine_instance), NULL),
                 ENGINE_ERROR_LIMIT_EXCEEDED);
    CHECK_STATUS(g_api->instance_destroy(world, other), ENGINE_OK);
    CHECK_STATUS(g_api->instance_destroy(world, other), ENGINE_ERROR_NOT_FOUND);

    /* a tree: a root and three children */
    nodes[0] = Node(0, 0, 0, -1, 0);
    nodes[1] = Node(0, 1, 0, 0, 0);
    nodes[2] = Node(0, 1, 0, 1, 0);
    nodes[3] = Node(1, 0, 0, 0, 0xFFFFFFFFu);
    CHECK_STATUS(g_api->instance_tree_create(world, &p, meshes, bounds, 1, nodes, 4, sizeof(engine_tree_node), &tree),
                 ENGINE_OK);
    CHECK(tree != 0 && tree != set);
    item = Item(0, 2, 0, 1, 1, 1, 1);
    CHECK_STATUS(g_api->instance_tree_set_local(world, tree, 1, &item, sizeof(item)), ENGINE_OK);
    CHECK_STATUS(g_api->instance_tree_set_local(world, tree, 4, &item, sizeof(item)), ENGINE_ERROR_NOT_FOUND);
    CHECK_STATUS(g_api->instance_tree_set_local(world, set, 1, &item, sizeof(item)), ENGINE_ERROR_NOT_FOUND);
    nodes[2].parent = 3; /* not an earlier node */
    CHECK_STATUS(g_api->instance_tree_create(world, &p, meshes, bounds, 1, nodes, 4, sizeof(engine_tree_node), &other),
                 ENGINE_ERROR_INVALID_ARGUMENT);
    nodes[2].parent = 1;
    nodes[3].mesh = 1; /* a mesh index at the mesh count */
    CHECK_STATUS(g_api->instance_tree_create(world, &p, meshes, bounds, 1, nodes, 4, sizeof(engine_tree_node), &other),
                 ENGINE_ERROR_INVALID_ARGUMENT);
    nodes[3].mesh = 0;
    CHECK_STATUS(g_api->instance_tree_create(world, &p, meshes, bounds, 1, nodes, 4, 40, &other),
                 ENGINE_ERROR_STRUCT_SIZE);
    CHECK_STATUS(g_api->instance_tree_create(world, &p, meshes, bounds, 0, nodes, 4, sizeof(engine_tree_node), &other),
                 ENGINE_ERROR_INVALID_ARGUMENT);
    CHECK_STATUS(g_api->instance_tree_update(world, tree, &p), ENGINE_OK);
    CHECK_STATUS(g_api->instance_tree_update(world, set, &p), ENGINE_ERROR_NOT_FOUND);
    Frames(engine, 2); /* no surface: nothing is drawn, nothing fails */
    CHECK_STATUS(g_api->instance_destroy(world, tree), ENGINE_OK);
    CHECK_STATUS(g_api->instance_destroy(world, set), ENGINE_OK);
    CHECK_STATUS(g_api->world_destroy(world), ENGINE_OK);
    CHECK_STATUS(g_api->instance_destroy(world, set), ENGINE_ERROR_STALE_HANDLE);
    free(items);
    free(ids);
}

static void Pixel(engine_handle engine, engine_handle surface, unsigned x, unsigned y, int rgb[3])
{
    engine_handle ticket;
    engine_pixels pixels;
    const unsigned char* p;
    Frames(engine, 10);
    memset(&ticket, 0, sizeof(ticket));
    CHECK_STATUS(g_api->surface_read_pixels(surface, &ticket), ENGINE_OK);
    Frames(engine, 1);
    memset(&pixels, 0, sizeof(pixels));
    pixels.struct_size = sizeof(pixels);
    CHECK_STATUS(g_api->pixels_finish(ticket, &pixels), ENGINE_OK);
    CHECK_STATUS(g_api->pixels_destroy(ticket), ENGINE_OK);
    p = (const unsigned char*)pixels.data.data + (size_t)y * pixels.stride + (size_t)x * 4;
    rgb[0] = p[0];
    rgb[1] = p[1];
    rgb[2] = p[2];
    engine_buffer_release(&pixels.data);
}

static int ReadBlob(const char* name, unsigned char** data, size_t* size)
{
    char path[1024];
    FILE* f;
    snprintf(path, sizeof(path), "%s/%s", RES_DATA_DIR, name);
    f = fopen(path, "rb");
    if (f == NULL)
    {
        printf("host_instances: cannot open %s\n", path);
        ++g_failures;
        return 0;
    }
    fseek(f, 0, SEEK_END);
    *size = (size_t)ftell(f);
    fseek(f, 0, SEEK_SET);
    *data = (unsigned char*)malloc(*size);
    if (fread(*data, 1, *size, f) != *size)
    {
        ++g_failures;
    }
    fclose(f);
    return 1;
}

static void Drawing(engine_handle engine)
{
    engine_info about;
    engine_surface_info info;
    engine_handle surface;
    engine_handle world;
    engine_camera camera;
    engine_transform at;
    engine_environment env;
    engine_surface_state state;
    engine_resource_add_info add;
    unsigned char* blob = NULL;
    size_t blob_size = 0;
    uint64_t camera_entity = 0;
    uint64_t sun = 0;
    uint64_t set = 0;
    uint64_t tree = 0;
    uint64_t id = 0;
    int sky[3];
    int red[3];
    int blue[3];
    int gone[3];
    engine_instance_placement p = Placement(0, 0, 0);
    engine_instance item = Item(0, 0, 0, 3.0f, 1.0f, 0.0f, 0.0f);
    engine_tree_node nodes[2];
    /* the mesh is a triangle facing +z: turned about x by -90 degrees it lies flat, facing the camera above */
    item.rotation[0] = -0.70710678f;
    item.rotation[3] = 0.70710678f;
    uint64_t meshes[1] = {MESH_ID};
    float bounds[6] = {-3, -3, -3, 3, 3, 3};

    memset(&about, 0, sizeof(about));
    about.struct_size = sizeof(about);
    CHECK_STATUS(g_api->get_info(engine, &about), ENGINE_OK);
    if ((about.caps & ENGINE_CAP_SURFACE_OFFSCREEN) == 0 || getenv("HOST_INSTANCES_SKIP") != NULL)
    {
        printf("host_instances: drawing skipped: no off-screen surfaces, or no GPU in this flavor\n");
        return;
    }
    memset(&info, 0, sizeof(info));
    info.struct_size = sizeof(info);
    info.kind = ENGINE_SURFACE_OFFSCREEN;
    info.width = W;
    info.height = H;
    info.scale = 1.0f;
    info.flags = host_surface_api_flags();
    memset(&surface, 0, sizeof(surface));
    if (g_api->surface_create(engine, &info, &surface) != ENGINE_OK)
    {
        printf("host_instances: drawing skipped: no device could be made\n");
        return;
    }
    if (!ReadBlob("scene.mesh.blob", &blob, &blob_size))
    {
        return;
    }
    memset(&add, 0, sizeof(add));
    add.struct_size = sizeof(add);
    add.id = MESH_ID;
    add.bytes = blob;
    add.size = blob_size;
    CHECK_STATUS(g_api->resource_add(engine, &add), ENGINE_OK);
    free(blob);
    memset(&world, 0, sizeof(world));
    CHECK_STATUS(g_api->world_create(engine, &world), ENGINE_OK);
    CHECK_STATUS(g_api->surface_set_world(surface, world), ENGINE_OK);
    memset(&env, 0, sizeof(env));
    env.struct_size = sizeof(env);
    env.sky_color[1] = 0.5f;
    env.exposure = 1.0f;
    CHECK_STATUS(g_api->world_set_environment(world, &env), ENGINE_OK);
    {
        engine_light light;
        engine_transform turned = {0};
        CHECK_STATUS(g_api->entity_create(world, &sun), ENGINE_OK);
        turned.struct_size = sizeof(turned);
        turned.rotation[0] = -0.70710678f;
        turned.rotation[3] = 0.70710678f;
        turned.scale[0] = turned.scale[1] = turned.scale[2] = 1.0f;
        CHECK_STATUS(g_api->component_set(world, sun, ENGINE_COMPONENT_TRANSFORM, &turned), ENGINE_OK);
        memset(&light, 0, sizeof(light));
        light.struct_size = (uint32_t)offsetof(engine_light, inner_cone);
        light.kind = ENGINE_LIGHT_DIRECTIONAL;
        light.color[0] = light.color[1] = light.color[2] = 1.0f;
        light.intensity = 3.0f;
        CHECK_STATUS(g_api->component_set(world, sun, ENGINE_COMPONENT_LIGHT, &light), ENGINE_OK);
    }
    /* the camera 20 m above the origin looking straight down */
    CHECK_STATUS(g_api->entity_create(world, &camera_entity), ENGINE_OK);
    memset(&at, 0, sizeof(at));
    at.struct_size = sizeof(at);
    at.position[1] = 20.0f;
    at.rotation[0] = -0.70710678f;
    at.rotation[3] = 0.70710678f;
    at.scale[0] = at.scale[1] = at.scale[2] = 1.0f;
    CHECK_STATUS(g_api->component_set(world, camera_entity, ENGINE_COMPONENT_TRANSFORM, &at), ENGINE_OK);
    memset(&camera, 0, sizeof(camera));
    camera.struct_size = sizeof(camera);
    camera.flags = ENGINE_CAMERA_FIXED_ASPECT;
    camera.fov_y = 1.0471976f;
    camera.near_plane = 0.1f;
    camera.far_plane = 200.0f;
    camera.aspect = (float)W / (float)H;
    CHECK_STATUS(g_api->component_set(world, camera_entity, ENGINE_COMPONENT_CAMERA, &camera), ENGINE_OK);
    CHECK_STATUS(g_api->surface_set_camera(surface, camera_entity), ENGINE_OK);

    Pixel(engine, surface, W / 2, H / 2, sky);
    CHECK(sky[1] > 60 && sky[0] < 10);

    /* a set with one big red instance at the origin */
    CHECK_STATUS(g_api->instance_set_create(world, &p, MESH_ID, 0, &set), ENGINE_OK);
    CHECK_STATUS(g_api->instance_set_add_many(world, set, &item, 1, sizeof(item), &id), ENGINE_OK);
    Pixel(engine, surface, W / 2, H / 2, red);
    printf("host_instances: pixel with the set %d %d %d, without %d %d %d\n", red[0], red[1], red[2], sky[0], sky[1],
           sky[2]);
    CHECK(red[0] > sky[0] + 25 && red[0] > red[2] + 15); /* drawn, and red */
    memset(&state, 0, sizeof(state));
    state.struct_size = sizeof(state);
    CHECK_STATUS(g_api->surface_state(surface, &state), ENGINE_OK);
    CHECK(state.entities_unresolved == 0);
    /* moved away, the centre is sky again; back, it is red */
    item = Item(500, 0, 500, 3.0f, 1, 0, 0);
    item.rotation[0] = -0.70710678f;
    item.rotation[3] = 0.70710678f;
    CHECK_STATUS(g_api->instance_set_set_transform(world, set, id, &item, sizeof(item)), ENGINE_OK);
    Pixel(engine, surface, W / 2, H / 2, gone);
    CHECK(gone[1] > 60 && gone[0] < 10);
    CHECK_STATUS(g_api->instance_destroy(world, set), ENGINE_OK);

    /* a tree whose root is a blue mesh at the origin; set_local sends it far away */
    nodes[0] = Node(0, 0, 0, -1, 0);
    nodes[0].scale[0] = nodes[0].scale[1] = nodes[0].scale[2] = 3.0f;
    nodes[0].rotation[0] = -0.70710678f;
    nodes[0].rotation[3] = 0.70710678f;
    nodes[0].tint[0] = 0.0f;
    nodes[0].tint[1] = 0.0f;
    nodes[1] = Node(0, 40, 0, 0, 0xFFFFFFFFu);
    CHECK_STATUS(g_api->instance_tree_create(world, &p, meshes, bounds, 1, nodes, 2, sizeof(engine_tree_node), &tree),
                 ENGINE_OK);
    Pixel(engine, surface, W / 2, H / 2, blue);
    printf("host_instances: pixel with the tree %d %d %d\n", blue[0], blue[1], blue[2]);
    CHECK(blue[2] > sky[2] + 25 && blue[2] > blue[0] + 15);
    item = Item(500, 0, 500, 3.0f, 1, 1, 1);
    CHECK_STATUS(g_api->instance_tree_set_local(world, tree, 0, &item, sizeof(item)), ENGINE_OK);
    Pixel(engine, surface, W / 2, H / 2, gone);
    CHECK(gone[1] > 60 && gone[2] < 10);
    CHECK_STATUS(g_api->instance_destroy(world, tree), ENGINE_OK);

    CHECK_STATUS(g_api->surface_destroy(surface), ENGINE_OK);
    CHECK_STATUS(g_api->world_destroy(world), ENGINE_OK);
}

int main(void)
{
    engine_create_info create;
    engine_handle engine;

    if (engine_get_api(ENGINE_ABI_VERSION_MAJOR, ENGINE_ABI_VERSION_MINOR, sizeof(engine_api), &g_api) != ENGINE_OK)
    {
        printf("host_instances: no function table\n");
        return 2;
    }
    memset(&create, 0, sizeof(create));
    create.struct_size = sizeof(create);
    create.flags = ENGINE_CREATE_LOG_SUPPRESS_CONSOLE;
    memset(&engine, 0, sizeof(engine));
    CHECK_STATUS(g_api->create(&create, &engine), ENGINE_OK);
    Api(engine);
    Drawing(engine);
    CHECK_STATUS(g_api->destroy(engine), ENGINE_OK);
    if (g_failures == 0)
    {
        printf("host_instances: ok\n");
        return 0;
    }
    printf("host_instances: %d checks failed\n", g_failures);
    return 1;
}
