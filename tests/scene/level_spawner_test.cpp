// Phase 22 Day 329: a level spawned through the resource manager (entities, shared meshes, materials, includes,
// failure) and, on a real device, rendered like the hand-built scene made of the same loaded meshes and materials.

#include <catch2/catch_test_macros.hpp>

#include "asset/gpu_loaders.h"
#include "asset/memory_backend.h"
#include "backend_fixture.h"
#include "cstr/cstr.h"
#include "cstr/format.h"
#include "cstr/mem_ops.h"
#include "demo_frame.h"
#include "ecs/schedule.h"
#include "ecs/transform.h"
#include "level_source.h"
#include "log/log.h"
#include "os/os.h"
#include "renderer/render_scene.h"
#include "rhi/null/null_device.h"
#include "scene/level_spawner.h"
#include "scene/render_sync.h"
#include "storage/builtin_components.h"

using namespace engine_placeholder;

namespace
{

using asset::ResourceId;

ByteSpan Bytes(const Vector<byte>& v)
{
    return ByteSpan(v.Data(), v.Size());
}

// Resources under paths for the level minter.
class SpResolver final : public asset::LevelReferenceResolver
{
public:
    struct Entry
    {
        Array<char, 48> path = {};
        ResourceId id;
        Vector<byte> blob;
    };
    asset::Status ResolvePath(StringView path, ResourceId* id, ByteSpan* blob) const override
    {
        for (usize i = 0; i < entries.Size(); ++i)
        {
            if (StringView(entries[i].path.Data(), CStrLength(entries[i].path.Data())) == path)
            {
                *id = entries[i].id;
                *blob = Bytes(entries[i].blob);
                return asset::Status::Success();
            }
        }
        return asset::Status::Make(asset::ErrorCode::NotFound, "no such resource");
    }
    asset::Status ResolveId(ResourceId id, ByteSpan* blob) const override
    {
        for (usize i = 0; i < entries.Size(); ++i)
        {
            if (entries[i].id == id)
            {
                *blob = Bytes(entries[i].blob);
                return asset::Status::Success();
            }
        }
        return asset::Status::Make(asset::ErrorCode::NotFound, "no such id");
    }
    asset::Status PathOf(ResourceId, String<HeapAllocator>*) const override
    {
        return asset::Status::Make(asset::ErrorCode::NotFound, "no path");
    }
    void Add(const char* path, u64 id, const Vector<byte>& blob)
    {
        Entry e;
        FormatTo(e.path.Data(), e.path.Size(), "%s", path);
        e.id = ResourceId::FromValue(id);
        for (usize i = 0; i < blob.Size(); ++i)
        {
            e.blob.Push(blob[i]);
        }
        entries.Push(static_cast<Entry&&>(e));
    }
    Vector<Entry> entries;
};

// The blob of a renderer mesh.
Vector<byte> MeshBlob(const asset::TypeRegistry& types, u64 id, const renderer::MeshData& data)
{
    Vector<asset::MeshVertex> vertices;
    for (u32 i = 0; i < data.VertexCount(); ++i)
    {
        const renderer::StandardVertex* v = static_cast<const renderer::StandardVertex*>(data.Vertices()) + i;
        asset::MeshVertex out;
        for (u32 k = 0; k < 3; ++k)
        {
            out.position[k] = v->position[k];
            out.normal[k] = v->normal[k];
        }
        for (u32 k = 0; k < 4; ++k)
        {
            out.tangent[k] = v->tangent[k];
        }
        out.uv[0] = v->uv[0];
        out.uv[1] = v->uv[1];
        vertices.Push(out);
    }
    Vector<u32> indices;
    for (u32 i = 0; i < data.IndexCount(); ++i)
    {
        indices.Push(data.IndexAt(i));
    }
    asset::MeshBuildInput input;
    input.vertices = Span<const asset::MeshVertex>(vertices.Data(), vertices.Size());
    input.indices = Span<const u32>(indices.Data(), indices.Size());
    input.quality = asset::MeshQuality::Full;
    Vector<byte> blob;
    REQUIRE(asset::BuildMeshBlob(types, ResourceId::FromValue(id), input, &blob).Ok());
    return blob;
}

// A G-buffer material of one color.
Vector<byte> MaterialBlob(const asset::TypeRegistry& types, u64 id, f32 r, f32 g, f32 b)
{
    asset::MaterialAssetData data;
    REQUIRE(asset::MakeMaterialName("gbuffer", 7, &data.template_name));
    data.block.Resize(16);
    const f32 color[4] = {r, g, b, 1.0f};
    MemCopy(data.block.Data(), color, sizeof(color));
    asset::MaterialParam param;
    REQUIRE(asset::MakeMaterialName("base_color", 10, &param.name));
    param.type = asset::MaterialParamType::Color4;
    param.size = 16;
    data.params.Push(param);
    Vector<byte> blob;
    REQUIRE(asset::BuildMaterialBlob(types, ResourceId::FromValue(id), data, &blob).Ok());
    return blob;
}

constexpr u64 kCubeId = 0x10;
constexpr u64 kSphereId = 0x11;
constexpr u64 kRedId = 0x20;
constexpr u64 kBlueId = 0x21;
constexpr u64 kLevelId = 0x30;
constexpr u64 kOuterId = 0x31;

struct LsPlacement
{
    f32 x, y, z;
    bool blue;
    bool sphere;
};

const LsPlacement kLsPlacements[] = {
    {-2.0f, 0.0f, 0.0f, false, false}, {0.0f, 0.0f, -1.0f, true, true},  {2.0f, 0.5f, 0.0f, false, true},
    {0.0f, -1.5f, 0.5f, true, false},  {-1.0f, 1.2f, -0.5f, true, true},
};

// The level source of the placements.
String<HeapAllocator> LevelText(u64 id, const char* include)
{
    String<HeapAllocator> text(HeapAllocator::Default());
    char line[256];
    FormatTo(line, sizeof(line), "id %016llx\n", static_cast<unsigned long long>(id));
    text.Append(StringView(line, CStrLength(line)));
    if (include != nullptr)
    {
        FormatTo(line, sizeof(line), "include %s\n", include);
        text.Append(StringView(line, CStrLength(line)));
    }
    u32 n = 0;
    for (const LsPlacement& p : kLsPlacements)
    {
        FormatTo(line, sizeof(line),
                 "entity e%llu_%u\n  component local_transform\n    position = %g %g %g\n  component mesh_reference\n"
                 "    mesh = %s\n    material = %s\n",
                 static_cast<unsigned long long>(id), n++, static_cast<double>(p.x), static_cast<double>(p.y),
                 static_cast<double>(p.z), p.sphere ? "/meshes/sphere" : "/meshes/cube",
                 p.blue ? "/materials/blue" : "/materials/red");
        text.Append(StringView(line, CStrLength(line)));
    }
    return text;
}

// The project: meshes, materials and two levels (the outer one includes the inner one) in a memory backend.
struct Project
{
    asset::TypeRegistry types;
    storage::ComponentRegistry registry;
    asset::HostMemoryBackend backend;
    Vector<byte> level;
    Vector<byte> outer;

    Project() : backend(types, u64{8} << 20)
    {
        REQUIRE(storage::RegisterBuiltinComponents(&registry).Ok());
        REQUIRE(asset::RegisterLevelComponents(&registry).Ok());
        renderer::MeshData cube;
        renderer::MeshData sphere;
        REQUIRE(renderer::MakeCubeMesh(1.0f, &cube) == nullptr);
        REQUIRE(renderer::MakeSphereMesh(0.7f, 16, 8, &sphere) == nullptr);
        const Vector<byte> cube_blob = MeshBlob(types, kCubeId, cube);
        const Vector<byte> sphere_blob = MeshBlob(types, kSphereId, sphere);
        const Vector<byte> red = MaterialBlob(types, kRedId, 0.8f, 0.1f, 0.1f);
        const Vector<byte> blue = MaterialBlob(types, kBlueId, 0.1f, 0.2f, 0.8f);
        REQUIRE(backend.Add(ResourceId::FromValue(kCubeId), Bytes(cube_blob)).Ok());
        REQUIRE(backend.Add(ResourceId::FromValue(kSphereId), Bytes(sphere_blob)).Ok());
        REQUIRE(backend.Add(ResourceId::FromValue(kRedId), Bytes(red)).Ok());
        REQUIRE(backend.Add(ResourceId::FromValue(kBlueId), Bytes(blue)).Ok());
        SpResolver resolver;
        resolver.Add("/meshes/cube", kCubeId, cube_blob);
        resolver.Add("/meshes/sphere", kSphereId, sphere_blob);
        resolver.Add("/materials/red", kRedId, red);
        resolver.Add("/materials/blue", kBlueId, blue);
        asset::LevelSourceError error;
        const String<HeapAllocator> inner = LevelText(kLevelId, nullptr);
        const asset::Status minted = asset::MintLevel(inner.View(), resolver, types, registry, &level, &error);
        INFO(error.message.Data());
        REQUIRE(minted.Ok());
        REQUIRE(backend.Add(ResourceId::FromValue(kLevelId), Bytes(level)).Ok());
        resolver.Add("/levels/inner", kLevelId, level);
        const String<HeapAllocator> outer_text = LevelText(kOuterId, "/levels/inner");
        const asset::Status outer_minted = asset::MintLevel(outer_text.View(), resolver, types, registry, &outer,
                                                            &error);
        INFO(error.message.Data());
        REQUIRE(outer_minted.Ok());
        REQUIRE(backend.Add(ResourceId::FromValue(kOuterId), Bytes(outer)).Ok());
    }
};

struct NullRig
{
    Project project;
    rhi::NullDevicePtr device = rhi::CreateNullDevice(rhi::NullDeviceOptions{});
    UniquePtr<renderer::MaterialSystem> materials;
    asset::GpuAssetContext context;
    UniquePtr<asset::ResourceManager> manager;
    NullRig()
    {
        LogRender.SetEnabled(false);
        materials = MakeUnique<renderer::MaterialSystem>(device.Get(), renderer::MaterialSystemDesc{});
        context.device = device.Get();
        context.materials = materials.Get();
        context.templates = renderer::CreateGBufferTemplates(materials.Get());
        asset::ManagerConfig config;
        config.buffer_pool_bytes = u64{16} << 20;
        manager = MakeUnique<asset::ResourceManager>(project.types, config);
        asset::RegisterCpuLoaders(manager.Get());
        asset::RegisterGpuLoaders(manager.Get(), &context);
        REQUIRE(manager->Mount(StringView("/"), &project.backend, 0).Ok());
    }
    ~NullRig()
    {
        manager.Reset();
        materials.Reset();
        LogRender.SetEnabled(true);
    }
};

}  // namespace

TEST_CASE("A level spawns entities that share the manager's meshes and materials", "[scene][level][asset]")
{
    NullRig rig;
    ecs::World world;
    storage::KeyIndex index;
    scene::SpawnedLevel spawned;
    REQUIRE(
        scene::SpawnLevel(*rig.manager, ResourceId::FromValue(kLevelId), rig.project.registry, world, index, &spawned)
            .Ok());
    CHECK(spawned.entities.Size() == 5);
    usize drawn = 0;
    world.Query<ecs::Read<scene::MeshRenderer>>().ForEach(
        [&](ecs::Entity, const scene::MeshRenderer& mr)
        {
            REQUIRE(mr.mesh);
            REQUIRE(mr.mesh->shared);
            CHECK(mr.mesh->Geometry().Valid());
            CHECK(mr.mesh->mesh.Valid() == false);  // the holder owns nothing: the manager does
            ++drawn;
        });
    CHECK(drawn == 5);
    // The mesh is the manager's: one GPU mesh per distinct mesh, however many entities share it.
    asset::Handle<asset::GpuMeshAsset> cube;
    REQUIRE(rig.manager->Load<asset::GpuMeshAsset>(ResourceId::FromValue(kCubeId), &cube).Ok());
    usize shared = 0;
    world.Query<ecs::Read<scene::MeshRenderer>>().ForEach(
        [&](ecs::Entity, const scene::MeshRenderer& mr)
        { shared += mr.mesh->shared.Get() == cube.Get()->mesh.Get() ? 1u : 0u; });
    CHECK(shared == 2);
    CHECK(rig.manager->Stats().fallbacks_used == 0);
    CHECK(rig.manager->Stats().load_conversions == 0);

    // Destroying the level removes its entities and, once nothing else holds them, unloads what they used.
    cube.Reset();
    scene::DestroyLevel(&spawned, world, index);
    usize left = 0;
    world.Query<ecs::Read<storage::EntityKeyComponent>>().ForEach([&](ecs::Entity, const storage::EntityKeyComponent&)
                                                                  { ++left; });
    CHECK(left == 0);
    CHECK(rig.manager->Stats().resident == 0);
}

TEST_CASE("A level spawns the levels it includes first", "[scene][level][asset]")
{
    NullRig rig;
    ecs::World world;
    storage::KeyIndex index;
    scene::SpawnedLevel spawned;
    REQUIRE(
        scene::SpawnLevel(*rig.manager, ResourceId::FromValue(kOuterId), rig.project.registry, world, index, &spawned)
            .Ok());
    CHECK(spawned.entities.Size() == 10);
    CHECK(spawned.levels.Size() == 2);
    scene::DestroyLevel(&spawned, world, index);
    CHECK(rig.manager->Stats().resident == 0);
}

TEST_CASE("A level whose resource is missing spawns nothing", "[scene][level][asset]")
{
    NullRig rig;
    // The blue material goes; the level cannot load as a whole.
    REQUIRE(rig.project.backend.Remove(ResourceId::FromValue(kBlueId)).Ok());
    ecs::World world;
    storage::KeyIndex index;
    scene::SpawnedLevel spawned;
    asset::Status s = scene::SpawnLevel(*rig.manager, ResourceId::FromValue(kLevelId), rig.project.registry, world,
                                        index, &spawned);
    CHECK(s.Is(asset::ErrorCode::MissingDependency));
    CHECK(spawned.entities.Size() == 0);
    usize count = 0;
    world.Query<ecs::Read<storage::EntityKeyComponent>>().ForEach([&](ecs::Entity, const storage::EntityKeyComponent&)
                                                                  { ++count; });
    CHECK(count == 0);
    CHECK(rig.manager->Stats().resident == 0);
    // Spawning twice into one world is refused by the keys.
    REQUIRE(rig.project.backend
                .Add(ResourceId::FromValue(kBlueId), Bytes(MaterialBlob(rig.project.types, kBlueId, 0.1f, 0.2f, 0.8f)))
                .Ok());
    REQUIRE(
        scene::SpawnLevel(*rig.manager, ResourceId::FromValue(kLevelId), rig.project.registry, world, index, &spawned)
            .Ok());
    scene::SpawnedLevel again;
    s = scene::SpawnLevel(*rig.manager, ResourceId::FromValue(kLevelId), rig.project.registry, world, index, &again);
    CHECK(s.Failed());
    CHECK(again.entities.Size() == 0);
    CHECK(spawned.entities.Size() == 5);  // the first spawn is untouched
}

TEST_CASE("A spawned level keeps its materials when the manager reloads their resource", "[scene][level][asset]")
{
    NullRig rig;
    ecs::World world;
    storage::KeyIndex index;
    scene::SpawnedLevel spawned;
    REQUIRE(
        scene::SpawnLevel(*rig.manager, ResourceId::FromValue(kLevelId), rig.project.registry, world, index, &spawned)
            .Ok());
    Vector<u32> slots;
    world.Query<ecs::Read<scene::MeshRenderer>>().ForEach([&](ecs::Entity, const scene::MeshRenderer& mr)
                                                          { slots.Push(mr.material); });
    REQUIRE(slots.Size() == 5);
    for (usize i = 0; i < slots.Size(); ++i)
    {
        CHECK(rig.materials->MaterialAtIndex(slots[i]).IsValid());
    }
    // The red material's resource changes and is reloaded: the manager gets a new material, the level keeps its own.
    const Vector<byte> other = MaterialBlob(rig.project.types, kRedId, 0.0f, 1.0f, 0.0f);
    REQUIRE(rig.project.backend.Replace(ResourceId::FromValue(kRedId), Bytes(other)).Ok());
    Vector<asset::ReloadResult> results;
    REQUIRE(rig.manager->PollChanges(&results).Ok());
    REQUIRE(results.Size() == 1);
    CHECK(results[0].status.Ok());
    for (usize i = 0; i < slots.Size(); ++i)
    {
        CHECK(rig.materials->MaterialAtIndex(slots[i]).IsValid());  // the slots the entities name are still materials
    }
    scene::DestroyLevel(&spawned, world, index);
    CHECK(rig.manager->Stats().resident == 0);
}

TEST_CASE("A level included by two included levels is spawned once", "[scene][level][asset]")
{
    NullRig rig;
    SpResolver resolver;
    resolver.Add("/meshes/cube", kCubeId,
                 MeshBlob(rig.project.types, kCubeId,
                          []
                          {
                              renderer::MeshData d;
                              REQUIRE(renderer::MakeCubeMesh(1.0f, &d) == nullptr);
                              return d;
                          }()));
    // D has an entity; B and C include D; A includes B and C.
    auto mint = [&](u64 id, const char* include_a, const char* include_b, const char* entity)
    {
        char text[400];
        usize at = static_cast<usize>(
            FormatTo(text, sizeof(text), "id %016llx\n", static_cast<unsigned long long>(id)));
        if (include_a != nullptr)
        {
            at += static_cast<usize>(FormatTo(text + at, sizeof(text) - at, "include %s\n", include_a));
        }
        if (include_b != nullptr)
        {
            at += static_cast<usize>(FormatTo(text + at, sizeof(text) - at, "include %s\n", include_b));
        }
        at += static_cast<usize>(FormatTo(text + at, sizeof(text) - at, "entity %s\n", entity));
        Vector<byte> blob;
        asset::LevelSourceError error;
        const asset::Status s = asset::MintLevel(StringView(text, at), resolver, rig.project.types,
                                                 rig.project.registry, &blob, &error);
        INFO(error.message.Data());
        REQUIRE(s.Ok());
        REQUIRE(rig.project.backend.Add(ResourceId::FromValue(id), Bytes(blob)).Ok());
        return blob;
    };
    const Vector<byte> d = mint(0x40, nullptr, nullptr, "d");
    resolver.Add("/levels/d", 0x40, d);
    const Vector<byte> b = mint(0x41, "/levels/d", nullptr, "b");
    resolver.Add("/levels/b", 0x41, b);
    const Vector<byte> c = mint(0x42, "/levels/d", nullptr, "c");
    resolver.Add("/levels/c", 0x42, c);
    mint(0x43, "/levels/b", "/levels/c", "a");

    ecs::World world;
    storage::KeyIndex index;
    scene::SpawnedLevel spawned;
    const asset::Status s = scene::SpawnLevel(*rig.manager, ResourceId::FromValue(0x43), rig.project.registry, world,
                                              index, &spawned);
    REQUIRE(s.Ok());
    CHECK(spawned.entities.Size() == 4);  // a, b, c and d once
    CHECK(spawned.levels.Size() == 4);
    scene::DestroyLevel(&spawned, world, index);
}

namespace
{

constexpr u32 kLsSize = 96;

bool RenderLsScene(renderer_demo::DemoFrame& frame, rhi::Device* device, const renderer::RenderScene& scene,
                   const renderer::Camera& camera, Vector<u8>* pixels)
{
    rhi::TextureDesc td;
    td.width = kLsSize;
    td.height = kLsSize;
    td.format = rhi::Format::RGBA8_SRGB;
    td.usage = rhi::TextureUsage::ColorAttachment | rhi::TextureUsage::CopySrc;
    const rhi::TextureHandle target = device->CreateTexture(td);
    if (!target.IsValid())
    {
        return false;
    }
    renderer::FrameTarget ft;
    ft.texture = target;
    ft.width = ft.height = kLsSize;
    ft.format = rhi::Format::RGBA8_SRGB;
    ft.final_state = rhi::ResourceState::CopySrc;
    renderer::FrameStatus status = renderer::FrameStatus::Failed;
    for (int i = 0; i < 3; ++i)
    {
        status = frame.Render(&scene, &camera, &ft);
    }
    device->WaitIdle();
    pixels->Resize(kLsSize * kLsSize * 4);
    const bool ok = status == renderer::FrameStatus::Ok &&
                    device->ReadbackTexture(target, 0, 0, pixels->Data(), pixels->Size(), rhi::ResourceState::CopySrc);
    device->DestroyTexture(target);
    return ok;
}

}  // namespace

TEST_CASE("A spawned level renders the same pixels as the hand-built scene of the same loaded resources",
          "[scene][level][render][gpu]")
{
    FOR_EACH_GPU_BACKEND(backend)
    {
        gpu_test::BackendValidation validation(backend);
        rhi::DeviceCreateInfo info;
        info.backend = backend;
        info.validation = rhi::ValidationSetting::On;
        info.pipeline_cache = false;
        rhi::DevicePtr device = rhi::CreateDevice(info);
        if (device.Get() == nullptr)
        {
            const char* required = os::GetEnvironment("ENGINE_RHI_REQUIRE_GPU");
            if (required != nullptr && required[0] == '1')
            {
                FAIL("ENGINE_RHI_REQUIRE_GPU=1 but no " << gpu_test::BackendTag(backend) << " device could be created");
            }
            SKIP("no " << gpu_test::BackendTag(backend) << " device on this machine");
        }
        {
            renderer_demo::FrameSetup setup;
            setup.device = device.Get();
            setup.max_objects = 64;
            setup.max_lights = 8;
            renderer_demo::DemoFrame frame;
            REQUIRE(frame.Create(setup));

            Project project;
            asset::GpuAssetContext context;
            context.device = device.Get();
            context.materials = frame.Materials();
            context.templates = frame.Templates();
            asset::ManagerConfig config;
            config.buffer_pool_bytes = u64{16} << 20;
            asset::ResourceManager manager(project.types, config);
            asset::RegisterCpuLoaders(&manager);
            asset::RegisterGpuLoaders(&manager, &context);
            REQUIRE(manager.Mount(StringView("/"), &project.backend, 0).Ok());

            ecs::World world;
            storage::KeyIndex index;
            renderer::RenderScene mirrored(renderer::RenderSceneDesc{64, 8});
            world.SetResource(ecs::TransformReport{});
            world.SetResource(scene::SceneLink{&mirrored});
            world.SetResource(scene::SceneSyncReport{});
            ecs::Schedule schedule;
            schedule.Add(ecs::MakeTransformSystem("transforms"));
            schedule.Add(scene::MakeRenderSyncSystem("render_sync").InStage(ecs::kRender));
            REQUIRE(schedule.Build().ok);

            scene::SpawnedLevel spawned;
            REQUIRE(
                scene::SpawnLevel(manager, ResourceId::FromValue(kLevelId), project.registry, world, index, &spawned)
                    .Ok());
            // The lights, by hand in both.
            renderer::Light sun;
            sun.type = renderer::LightType::Directional;
            sun.direction = Vec3(0.3f, 0.8f, 0.5f).Normalized();
            sun.intensity = 2.0f;
            scene::LightSource sun_source;
            sun_source.type = renderer::LightType::Directional;
            sun_source.direction = sun.direction;
            sun_source.intensity = 2.0f;
            world.Create(ecs::LocalTransform{}, ecs::WorldTransform{}, sun_source);
            scene::LightSource ambient_source;
            ambient_source.type = renderer::LightType::Ambient;
            ambient_source.intensity = 0.2f;
            world.Create(ecs::LocalTransform{}, ecs::WorldTransform{}, ambient_source);
            renderer::Light ambient;
            ambient.type = renderer::LightType::Ambient;
            ambient.intensity = 0.2f;

            // The meshes the level loaded: barriers out of their upload state, once.
            asset::Handle<asset::GpuMeshAsset> cube;
            asset::Handle<asset::GpuMeshAsset> sphere;
            asset::Handle<asset::GpuMaterialAsset> red;
            asset::Handle<asset::GpuMaterialAsset> blue;
            REQUIRE(manager.Load<asset::GpuMeshAsset>(ResourceId::FromValue(kCubeId), &cube).Ok());
            REQUIRE(manager.Load<asset::GpuMeshAsset>(ResourceId::FromValue(kSphereId), &sphere).Ok());
            REQUIRE(manager.Load<asset::GpuMaterialAsset>(ResourceId::FromValue(kRedId), &red).Ok());
            REQUIRE(manager.Load<asset::GpuMaterialAsset>(ResourceId::FromValue(kBlueId), &blue).Ok());
            {
                device->BeginFrame();
                rhi::CommandList* list = device->CreateCommandList();
                REQUIRE(list != nullptr);
                list->Begin();
                cube.Get()->mesh->RecordPrepare(list);
                sphere.Get()->mesh->RecordPrepare(list);
                list->End();
                rhi::CommandList* lists[] = {list};
                REQUIRE(device->Submit(lists, 1) != 0);
                device->EndFrame();
                device->WaitIdle();
                cube.Get()->mesh->MarkReady();
                sphere.Get()->mesh->MarkReady();
            }

            renderer::Camera camera;
            camera.SetLookAt(Vec3(0, 1.5f, 6), Vec3(0, 0, 0), Vec3(0, 1, 0));
            camera.aspect = 1.0f;
            camera.near_plane = 0.5f;
            camera.far_plane = 50.0f;

            renderer::RenderScene by_hand(renderer::RenderSceneDesc{64, 8});
            for (const LsPlacement& p : kLsPlacements)
            {
                renderer::RenderObjectDesc od;
                od.mesh = p.sphere ? sphere.Get()->mesh.Get() : cube.Get()->mesh.Get();
                od.material = (p.blue ? blue : red).Get()->material->id.index;
                od.transform = Mat4::FromBasis(Vec3(1, 0, 0), Vec3(0, 1, 0), Vec3(0, 0, 1), Vec3(p.x, p.y, p.z));
                REQUIRE(by_hand.AddObject(od).IsValid());
            }
            REQUIRE(by_hand.AddLight(sun).IsValid());
            REQUIRE(by_hand.AddLight(ambient).IsValid());

            schedule.Run(world, 0.016f);
            schedule.Run(world, 0.016f);
            REQUIRE(mirrored.ObjectCount() == by_hand.ObjectCount());
            REQUIRE(mirrored.LightCount() == by_hand.LightCount());

            Vector<u8> expected;
            Vector<u8> actual;
            REQUIRE(RenderLsScene(frame, device.Get(), by_hand, camera, &expected));
            REQUIRE(RenderLsScene(frame, device.Get(), mirrored, camera, &actual));
            REQUIRE(expected.Size() == actual.Size());
            CHECK(MemCompare(expected.Data(), actual.Data(), expected.Size()) == 0);
            // And it drew something: not an empty frame.
            bool any = false;
            for (usize i = 0; i + 4 <= actual.Size(); i += 4)
            {
                any = any || actual[i] != actual[0] || actual[i + 1] != actual[1] || actual[i + 2] != actual[2];
            }
            CHECK(any);
            CHECK(manager.Stats().fallbacks_used == 0);

            scene::DestroyLevel(&spawned, world, index);
            device->WaitIdle();
        }
    }
}
