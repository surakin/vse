// Maintenance (2026-11-24): scripts override the material of a mesh slot by the slot's name (`mesh.set_slot`,
// `mesh.clear_slot`, `mesh.has_slot`), with the host textures found by the name they were made with. The test imports a
// quad whose material is called "Screen", spawns it, and drives a behavior on it with events; the overrides the merge
// made are read from the world's own component.
#include <catch2/catch_test_macros.hpp>

#include "engine/engine_c.h"
#include "cstr/cstr.h"
#include "cstr/mem_ops.h"
#include "engine_c/runtime.h"
#include "engine_c/engine_internal.h"
#include "sync/mutex.h"

using namespace vse;
using namespace vse::engine_c;

namespace
{

// A quad (two triangles) with the material "Screen"; the node is "panel".
const char SlotskPanel[] =
    "{\"asset\":{\"version\":\"2.0\"},\"scene\":0,\"scenes\":[{\"nodes\":[0]}],"
    "\"nodes\":[{\"name\":\"panel\",\"mesh\":0}],"
    "\"materials\":[{\"name\":\"Screen\"}],"
    "\"meshes\":[{\"name\":\"quad\",\"primitives\":[{\"attributes\":{\"POSITION\":0},\"indices\":1,\"material\":0}]}],"
    "\"accessors\":["
    "{\"bufferView\":0,\"componentType\":5126,\"count\":4,\"type\":\"VEC3\",\"min\":[-1,-1,0],\"max\":[1,1,0]},"
    "{\"bufferView\":1,\"componentType\":5123,\"count\":6,\"type\":\"SCALAR\"}],"
    "\"bufferViews\":[{\"buffer\":0,\"byteOffset\":0,\"byteLength\":48},{\"buffer\":0,\"byteOffset\":48,"
    "\"byteLength\":12}],"
    "\"buffers\":[{\"byteLength\":60,\"uri\":\"data:application/octet-stream;base64,"
    "AACAvwAAgL8AAAAAAACAPwAAgL8AAAAAAACAPwAAgD8AAAAAAACAvwAAgD8AAAAAAAACAAEAAAADAAIA\"}]}";

const char SlotskScript[] =
    "local function fails(f, ...)\n"
    "  local ok, message = pcall(f, ...)\n"
    "  return (not ok) and tostring(message) or ''\n"
    "end\n"
    "local function has(message, text) return string.find(message, text, 1, true) ~= nil and 1 or 0 end\n"
    "behavior('slots', {\n"
    "  on_create = function(self)\n"
    "    local me = entity.self()\n"
    "    event.on('set', function()\n"
    "      mesh.set_slot(me, 'Screen', 'albedo_tex', 'feed')\n"
    "      mesh.set_slot(me, 'Screen', 'base_color', vec4.new(1, 0.5, 0.25, 1))\n"
    "      mesh.set_slot(me, 'Screen', 'emissive_color', {r = 0, g = 1, b = 0})\n"
    "      mesh.set_slot(me, 'Screen', 'roughness', 0.3)\n"
    "      mesh.set_slot(me, 'Screen', 'albedo_uv', vec4.new(2, 2, 0.5, 0.5))\n"
    "    end)\n"
    "    event.on('clear', function() mesh.clear_slot(me, 'Screen', 'albedo_tex') end)\n"
    "    event.on('texture', function() mesh.set_slot(me, 'Screen', 'albedo_tex', 'feed') end)\n"
    "    event.on('probe', function()\n"
    "      local a = fails(mesh.set_slot, me, 'Nope', 'roughness', 1)\n"
    "      local b = fails(mesh.set_slot, me, 'Screen', 'albedo_tex', 'nofeed')\n"
    "      local c = fails(mesh.set_slot, me, 'Screen', 'roughness', 'rough')\n"
    "      local d = fails(mesh.set_slot, me, 'Screen', 'bogus', 1)\n"
    "      local e = fails(mesh.clear_slot, me, 'Nope', 'roughness')\n"
    "      local big = fails(mesh.set_slot, me, 'Screen', 'roughness', 1e39)\n"
    "      local bigc = fails(mesh.set_slot, me, 'Screen', 'base_color', {r = 1, g = 1e39, b = 1})\n"
    "      host.report(1, has(a, 'not_found') + has(a, 'Nope'), has(b, 'not_found') + has(b, 'nofeed'),\n"
    "                  has(c, 'type_mismatch') + has(c, 'roughness'), has(d, 'bogus'), has(e, 'Nope'),\n"
    "                  mesh.has_slot(me, 'Screen') and 1 or 0, mesh.has_slot(me, 'Nope') and 1 or 0,\n"
    "                  mesh.has_slot(entity.find('bare'), 'Screen') and 1 or 0, has(big, 'finite') + has(bigc, 'finite'))\n"
    "    end)\n"
    "  end,\n"
    "})\n";

double Slotsg_report[10] = {};
bool Slotsg_reported = false;

engine_status SlotsOnReport(void*, const engine_value_pool* args, engine_value_pool* results)
{
    for (u32 i = 1; i < args->root_count && i <= 9; ++i)
    {
        const engine_value& v = args->items[i];
        Slotsg_report[i - 1] = v.tag == ENGINE_VALUE_INT ? static_cast<double>(v.i) : v.f;
    }
    Slotsg_reported = true;
    results->root_count = 0;
    results->item_count = 0;
    results->byte_count = 0;
    return ENGINE_OK;
}

engine_string SlotStr(const char* text)
{
    return engine_string{text, CStrLength(text)};
}

void SlotsFrames(const engine_api* api, engine_handle engine, int count)
{
    for (int i = 0; i < count; ++i)
    {
        engine_frame_info frame = {};
        frame.struct_size = sizeof(frame);
        frame.delta_seconds = 1.0 / 60.0;
        REQUIRE(api->frame(engine, &frame, nullptr) == ENGINE_OK);
    }
}

void SlotsEmit(const engine_api* api, engine_handle world, const char* name, u64 target)
{
    engine_value_pool pool = {};
    pool.struct_size = sizeof(pool);
    REQUIRE(api->script_event_emit(world, SlotStr(name), &pool, target) == ENGINE_OK);
}

engine_handle SlotsMakeTexture(const engine_api* api, engine_handle engine, const char* name)
{
    engine_texture_info info = {};
    info.struct_size = sizeof(info);
    info.width = 8;
    info.height = 8;
    info.name = SlotStr(name);
    engine_handle texture = {};
    REQUIRE(api->texture_create(engine, &info, &texture) == ENGINE_OK);
    return texture;
}

// The overrides the world holds on `key`.
SlotOverrides SlotsOverridesOf(engine_handle engine, engine_handle world, u64 key)
{
    Runtime* runtime = nullptr;
    REQUIRE(ResolveRuntime(engine, &runtime) == ENGINE_OK);
    LockGuard<Mutex> lock(runtime->world_mutex);
    WorldObject* object = FindWorld(*runtime, world);
    REQUIRE(object != nullptr);
    const SlotOverrides* have = object->world.Peek<SlotOverrides>(ecs::Entity::FromU64(key));
    return have != nullptr ? *have : SlotOverrides();
}

const SlotOverrides::Item* SlotsItemOf(const SlotOverrides& overrides, const char* name)
{
    for (u32 i = 0; i < overrides.count; ++i)
    {
        if (StringView(overrides.items[i].name) == StringView(name))
        {
            return &overrides.items[i];
        }
    }
    return nullptr;
}

}  // namespace

TEST_CASE("mesh.set_slot overrides a mesh slot's material by name, with host textures found by name",
          "[engine_c][script][slots]")
{
    const engine_api* api = nullptr;
    REQUIRE(engine_get_api(ENGINE_ABI_VERSION_MAJOR, ENGINE_ABI_VERSION_MINOR, sizeof(engine_api), &api) == ENGINE_OK);
    engine_create_info create = {};
    create.struct_size = sizeof(create);
    create.flags = ENGINE_CREATE_LOG_SUPPRESS_CONSOLE;
    engine_handle engine = {};
    REQUIRE(api->create(&create, &engine) == ENGINE_OK);
    engine_info about = {};
    about.struct_size = sizeof(about);
    REQUIRE(api->get_info(engine, &about) == ENGINE_OK);
    if ((about.caps & ENGINE_CAP_SCRIPT) == 0)
    {
        REQUIRE(api->destroy(engine) == ENGINE_OK);
        return;
    }

    engine_import_options options = {};
    options.struct_size = sizeof(options);
    options.flags = ENGINE_IMPORT_EMIT_LEVEL;
    options.id_seed = 7;
    engine_import_result result = {};
    result.struct_size = sizeof(result);
    REQUIRE(api->import_gltf(engine, SlotskPanel, sizeof(SlotskPanel) - 1, &options, &result) == ENGINE_OK);
    u64 level = 0;
    for (u32 i = 0; i < result.resource_count; ++i)
    {
        u32 type = 0;
        MemCopy(&type, static_cast<const char*>(result.types.data) + i * sizeof(type), sizeof(type));
        if (type == ENGINE_RESOURCE_LEVEL)
        {
            MemCopy(&level, static_cast<const char*>(result.ids.data) + i * sizeof(level), sizeof(level));
        }
    }
    REQUIRE(level != 0);

    engine_handle world = {};
    REQUIRE(api->world_create(engine, &world) == ENGINE_OK);
    u64 root = 0;
    REQUIRE(api->level_spawn(world, level, nullptr, &root, nullptr, 0, nullptr) == ENGINE_OK);
    u64 panel = 0;
    REQUIRE(api->entity_find(world, SlotStr("panel"), &panel) == ENGINE_OK);
    u64 bare = 0;
    REQUIRE(api->entity_create(world, &bare) == ENGINE_OK);
    REQUIRE(api->entity_name_set(world, bare, SlotStr("bare")) == ENGINE_OK);
    REQUIRE(api->script_register_function(world, SlotStr("host.report"), 10, 0, &SlotsOnReport, nullptr) == ENGINE_OK);
    engine_script_load load = {};
    load.struct_size = sizeof(load);
    load.name = SlotStr("slots");
    load.source = SlotStr(SlotskScript);
    u64 script = 0;
    REQUIRE(api->script_load(world, &load, &script) == ENGINE_OK);
    engine_script_component component = {};
    component.struct_size = sizeof(component);
    component.enabled = 1;
    MemCopy(component.behavior, "slots", 6);
    REQUIRE(api->component_set(world, panel, ENGINE_COMPONENT_SCRIPT, &component) == ENGINE_OK);
    const engine_handle feed = SlotsMakeTexture(api, engine, "feed");
    SlotsFrames(api, engine, 2);

    SECTION("the overrides reach the world, a texture by its name")
    {
        SlotsEmit(api, world, "set", panel);
        SlotsFrames(api, engine, 3);
        const SlotOverrides have = SlotsOverridesOf(engine, world, panel);
        REQUIRE(have.count == 5);
        const SlotOverrides::Item* tex = SlotsItemOf(have, "albedo_tex");
        REQUIRE(tex != nullptr);
        CHECK(tex->kind == ENGINE_SLOT_HOST_TEXTURE);
        CHECK(tex->host_texture == feed.value);
        CHECK(tex->mesh_slot == 0);  // "Screen" is the mesh's first slot
        const SlotOverrides::Item* colour = SlotsItemOf(have, "base_color");
        REQUIRE(colour != nullptr);
        CHECK(colour->kind == ENGINE_SLOT_COLOR);
        CHECK(colour->value[1] == 0.5f);
        const SlotOverrides::Item* emissive = SlotsItemOf(have, "emissive_color");
        REQUIRE(emissive != nullptr);
        CHECK(emissive->value[1] == 1.0f);
        CHECK(emissive->value[3] == 1.0f);  // alpha left out is 1
        CHECK(SlotsItemOf(have, "roughness")->value[0] == 0.3f);
        CHECK(SlotsItemOf(have, "albedo_uv")->value[2] == 0.5f);

        // a texture made again under the name is found again (the newest one of that name)
        REQUIRE(api->texture_destroy(feed) == ENGINE_OK);
        const engine_handle again = SlotsMakeTexture(api, engine, "feed");
        SlotsEmit(api, world, "texture", panel);
        SlotsFrames(api, engine, 3);
        CHECK(SlotsItemOf(SlotsOverridesOf(engine, world, panel), "albedo_tex")->host_texture == again.value);

        // clear_slot takes one field away, and nothing else
        SlotsEmit(api, world, "clear", panel);
        SlotsFrames(api, engine, 3);
        const SlotOverrides after = SlotsOverridesOf(engine, world, panel);
        CHECK(after.count == 4);
        CHECK(SlotsItemOf(after, "albedo_tex") == nullptr);
        CHECK(SlotsItemOf(after, "roughness") != nullptr);
        REQUIRE(api->texture_destroy(again) == ENGINE_OK);
    }

    SECTION("errors name the slot, the field and the texture; has_slot answers")
    {
        SlotsEmit(api, world, "probe", panel);
        SlotsFrames(api, engine, 3);
        REQUIRE(Slotsg_reported);
        CHECK(Slotsg_report[0] == 2.0);  // not_found, naming the slot
        CHECK(Slotsg_report[1] == 2.0);  // not_found, naming the texture
        CHECK(Slotsg_report[2] == 2.0);  // type_mismatch, naming the field
        CHECK(Slotsg_report[3] == 1.0);  // the bogus field is named
        CHECK(Slotsg_report[4] == 1.0);  // clear_slot of a slot the mesh lacks
        CHECK(Slotsg_report[5] == 1.0);  // has_slot: the mesh has "Screen"
        CHECK(Slotsg_report[6] == 0.0);  // ...and not "Nope"
        CHECK(Slotsg_report[7] == 0.0);  // an entity with no mesh has none
        CHECK(Slotsg_report[8] == 2.0);  // a number or a colour beyond the float range is refused, not cast
        CHECK(SlotsOverridesOf(engine, world, panel).count == 0);  // an error changes nothing
        REQUIRE(api->texture_destroy(feed) == ENGINE_OK);
    }

    engine_buffer_release(&result.ids);
    engine_buffer_release(&result.types);
    engine_buffer_release(&result.paths);
    engine_buffer_release(&result.warnings);
    engine_buffer_release(&result.manifest);
    engine_buffer_release(&result.summary);
    REQUIRE(api->world_destroy(world) == ENGINE_OK);
    REQUIRE(api->destroy(engine) == ENGINE_OK);
}
