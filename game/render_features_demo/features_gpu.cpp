#include "features_gpu.h"

#include "asset/gpu_loaders.h"
#include "asset/height_map.h"
#include "containers/shared_bytes.h"
#include "log/output.h"
#include "renderer/sky.h"
#include "renderer/terrain_source.h"
#include "renderer/texture.h"

namespace render_features
{

using namespace vse;
using namespace vse::renderer;

namespace
{

constexpr u32 kSkyFaceTexels = 64;

// The direction of texel (i, j) of cube face `face`: the convention of the three graphics APIs
// (asset::CubeFaceDirection).
Vec3 FaceDirection(u32 face, f32 u, f32 v)
{
    switch (face)
    {
    case 0: return Vec3(1.0f, -v, -u);
    case 1: return Vec3(-1.0f, -v, u);
    case 2: return Vec3(u, 1.0f, v);
    case 3: return Vec3(u, -1.0f, -v);
    case 4: return Vec3(u, -v, 1.0f);
    default: return Vec3(-u, -v, -1.0f);
    }
}

u8 ToByte(f32 v)
{
    const f32 c = v < 0.0f ? 0.0f : (v > 1.0f ? 1.0f : v);
    return static_cast<u8>(c * 255.0f + 0.5f);
}

Mat4 Placed(const Vec3d& p, const Quaternion& q, const Vec3& scale)
{
    const Mat3 r = q.ToMat3();
    return Mat4::FromBasis(r.col[0] * scale.x, r.col[1] * scale.y, r.col[2] * scale.z, p.Narrow());
}

}  // namespace

FeaturesGpu::FeaturesGpu() = default;

FeaturesGpu::~FeaturesGpu()
{
    Destroy();
}

bool FeaturesGpu::Fail(const char* what)
{
    error_ = what;
    return false;
}

void FeaturesGpu::Destroy()
{
    if (device_ != nullptr)
    {
        device_->WaitIdle();
    }
    scene_ = nullptr;
    sky_cube_ = nullptr;
    sky_.cube = nullptr;
    body_objects_.Clear();
    for (GpuMesh& m : tree_meshes_)
    {
        m.Destroy();
    }
    rock_mesh_.Destroy();
    sphere_.Destroy();
    cube_.Destroy();
    device_ = nullptr;
}

// The sky as the build would mint it: a gradient from the ground through the horizon to the zenith with the sun, one
// 64 x 64 texel image a face, tone-mapped into 8 bits.
GpuTexturePtr FeaturesGpu::MakeSkyCube()
{
    SkySettings gradient;
    gradient.mode = SkyMode::Procedural;
    gradient.sun_direction = sky_.sun_direction;
    gradient.sun_color = sky_.sun_color;
    gradient.sun_size = sky_.sun_size;
    Vector<byte> bytes;
    bytes.Resize(static_cast<usize>(6) * kSkyFaceTexels * kSkyFaceTexels * 4);
    usize at = 0;
    for (u32 face = 0; face < 6; ++face)
    {
        for (u32 j = 0; j < kSkyFaceTexels; ++j)
        {
            for (u32 i = 0; i < kSkyFaceTexels; ++i)
            {
                const f32 u = (static_cast<f32>(i) + 0.5f) / static_cast<f32>(kSkyFaceTexels) * 2.0f - 1.0f;
                const f32 v = (static_cast<f32>(j) + 0.5f) / static_cast<f32>(kSkyFaceTexels) * 2.0f - 1.0f;
                const Vec3 c = SkyColor(gradient, FaceDirection(face, u, v));
                bytes[at++] = static_cast<byte>(ToByte(c.x / (1.0f + c.x * 0.15f)));
                bytes[at++] = static_cast<byte>(ToByte(c.y / (1.0f + c.y * 0.15f)));
                bytes[at++] = static_cast<byte>(ToByte(c.z / (1.0f + c.z * 0.15f)));
                bytes[at++] = static_cast<byte>(255);
            }
        }
    }
    rhi::TextureDesc desc;
    desc.dimension = rhi::TextureDimension::Cube;
    desc.width = kSkyFaceTexels;
    desc.height = kSkyFaceTexels;
    desc.array_layers = 6;
    desc.format = rhi::Format::RGBA8_UNorm;
    desc.usage = rhi::TextureUsage::Sampled;
    Vector<TextureLevelSpan> levels;
    const u64 face_bytes = static_cast<u64>(kSkyFaceTexels) * kSkyFaceTexels * 4;
    for (u32 face = 0; face < 6; ++face)
    {
        TextureLevelSpan span;
        span.offset = face * face_bytes;
        span.bytes = face_bytes;
        span.row_pitch = kSkyFaceTexels * 4;
        span.mip = 0;
        span.layer = face;
        levels.Push(span);
    }
    GpuTexturePtr cube = MakeShared<GpuTexture>();
    SharedBytesPtr region = MakeShared<OwnedBytes>(Move(bytes));
    if (!cube->CreateFromSharedLevels(device_, desc, region, Span<const TextureLevelSpan>(levels.Data(), levels.Size()),
                                      resources_, "sky cube"))
    {
        return GpuTexturePtr();
    }
    return cube;
}

// Two layers: grass and rock, 2 x 2 texels each.
GpuTexturePtr FeaturesGpu::MakeLayerArray()
{
    const u8 colors[2][3] = {{70, 120, 50}, {110, 105, 98}};
    rhi::TextureDesc desc;
    desc.width = 2;
    desc.height = 2;
    desc.array_layers = 2;
    desc.format = rhi::Format::RGBA8_SRGB;
    desc.usage = rhi::TextureUsage::Sampled;
    Vector<byte> bytes;
    Vector<TextureLevelSpan> spans;
    for (u32 l = 0; l < 2; ++l)
    {
        TextureLevelSpan span;
        span.offset = bytes.Size();
        span.bytes = 16;
        span.mip = 0;
        span.layer = l;
        spans.Push(span);
        for (u32 t = 0; t < 4; ++t)
        {
            const u32 shade = (t == 1 || t == 2) ? 8 : 0;  // a faint checker so the tiling reads
            bytes.Push(static_cast<byte>(colors[l][0] + shade));
            bytes.Push(static_cast<byte>(colors[l][1] + shade));
            bytes.Push(static_cast<byte>(colors[l][2] + shade));
            bytes.Push(static_cast<byte>(255));
        }
    }
    GpuTexturePtr texture = MakeShared<GpuTexture>();
    SharedBytesPtr region = MakeShared<OwnedBytes>(Move(bytes));
    if (!texture->CreateFromSharedLevels(device_, desc, region,
                                         Span<const TextureLevelSpan>(spans.Data(), spans.Size()), resources_,
                                         "terrain layers"))
    {
        return GpuTexturePtr();
    }
    return texture;
}

bool FeaturesGpu::PrepareMeshes()
{
    Vector<GpuMesh*> meshes;
    for (GpuMesh& m : tree_meshes_)
    {
        meshes.Push(&m);
    }
    meshes.Push(&rock_mesh_);
    meshes.Push(&sphere_);
    meshes.Push(&cube_);
    device_->BeginFrame();
    rhi::CommandList* list = device_->CreateCommandList();
    if (list == nullptr)
    {
        device_->EndFrame();
        return false;
    }
    list->Begin();
    for (GpuMesh* m : meshes)
    {
        m->RecordPrepare(list);
    }
    list->End();
    const Array<rhi::CommandList*, 1> lists = {list};
    const bool submitted = device_->Submit(lists.Data(), 1) != 0;
    device_->EndFrame();
    device_->WaitIdle();
    if (submitted)
    {
        for (GpuMesh* m : meshes)
        {
            m->MarkReady();
        }
    }
    return submitted;
}

bool FeaturesGpu::Create(rhi::Device* device, MaterialSystem* materials, const GBufferTemplates& templates,
                         FeaturesWorld* world, const GpuOptions& options)
{
    device_ = device;
    world_ = world;
    resources_ = options.resources;

    // Meshes.
    const char* names[kTreeMeshCount] = {"trunk",      "trunk far", "branch",       "twig",
                                         "leaf cards", "crown",     "tree stand-in"};
    for (u32 i = 0; i < kTreeMeshCount; ++i)
    {
        const MeshData& data = world->TreeMeshes()[i];
        if (!tree_meshes_[i].Create(device, data, names[i]) ||
            !tree_meshes_[i].MakeRecreatable(resources_, data, names[i]))
        {
            return Fail("a tree mesh could not be made");
        }
    }
    if (!rock_mesh_.Create(device, world->RockMesh(), "rock") ||
        !rock_mesh_.MakeRecreatable(resources_, world->RockMesh(), "rock"))
    {
        return Fail("the rock mesh could not be made");
    }
    MeshData sphere, cube;
    if (MakeSphereMesh(1.0f, 24, 12, &sphere) != nullptr || MakeCubeMesh(1.0f, &cube) != nullptr ||
        !sphere_.Create(device, sphere, "body sphere") || !sphere_.MakeRecreatable(resources_, sphere, "body sphere") ||
        !cube_.Create(device, cube, "body box") || !cube_.MakeRecreatable(resources_, cube, "body box"))
    {
        return Fail("a body mesh could not be made");
    }
    if (!PrepareMeshes())
    {
        return Fail("the meshes could not be prepared");
    }

    // Materials: one plain, one for instances (the nodes' tints colour it).
    const MaterialId plain = CreateGBufferMaterial(materials, templates, GBufferOptions{}, "demo material");
    GBufferOptions instanced;
    instanced.instanced = true;
    const MaterialId inst = CreateGBufferMaterial(materials, templates, instanced, "demo instanced material");
    if (!plain.IsValid() || !inst.IsValid() ||
        !materials->SetVec4(plain, "base_color", Vec4(0.85f, 0.45f, 0.20f, 1.0f)) ||
        !materials->SetVec4(inst, "base_color", Vec4(1.0f, 1.0f, 1.0f, 1.0f)))
    {
        return Fail("the materials could not be made");
    }
    default_material_ = plain.index;
    instanced_material_ = inst.index;

    // The scene.
    RenderSceneDesc desc;
    desc.max_objects = 256;
    desc.max_lights = 8;
    desc.max_terrains = 2;
    desc.max_instanced = 8;
    scene_ = MakeUnique<RenderScene>(desc);

    // The terrain: the height map as an asset blob, its renderer source, a grass and rock material.
    asset::TypeRegistry types;
    asset::HeightMapHeader header;
    header.width = world->Side();
    header.height = world->Side();
    header.range_min = 0.0f;
    header.range_max = kHeightRange;
    header.cell_size = world->CellSize();
    const Vector<u16>& samples = world->Samples();
    Vector<byte> blob;
    if (!asset::BuildHeightMapBlob(types, asset::ResourceId::FromValue(0x7e44a1), header,
                                   ByteSpan(reinterpret_cast<const byte*>(samples.Data()), samples.Size() * 2), &blob)
             .Ok())
    {
        return Fail("the height map blob could not be built");
    }
    asset::HeightMapView view;
    if (!view.Open(CopyShared(blob), types).Ok())
    {
        return Fail("the height map blob was refused");
    }
    TerrainSourcePtr source;
    if (!asset::MakeTerrainSource(device, view, resources_, &source))
    {
        return Fail("the terrain's height texture could not be made");
    }
    TerrainObject terrain;
    terrain.source = source;
    terrain.material.layer_count = 2;
    terrain.material.layers[0].tiling = 6.0f;
    terrain.material.layers[1].tiling = 10.0f;
    terrain.material.slope_layer = 1;
    terrain.material.slope_start = 0.12f;
    terrain.material.slope_end = 0.3f;
    terrain.material.fade_start = 200.0f;
    terrain.material.fade_end = 500.0f;
    terrain.material.layer_array = MakeLayerArray();
    if (!terrain.material.layer_array || !scene_->AddTerrain(terrain).IsValid())
    {
        return Fail("the terrain could not be added");
    }

    // The forest and the rocks.
    InstancedObject forest;
    forest.tree = world->Forest();
    for (u32 i = 0; i < kTreeMeshCount; ++i)
    {
        forest.tree_meshes[i] = &tree_meshes_[i];
    }
    for (u32 i = 0; i < kMaxSceneTreeMeshes; ++i)
    {
        forest.tree_materials[i] = instanced_material_;
    }
    forest.rotation = Quaternion::Identity();
    if (!scene_->AddInstanced(forest).IsValid())
    {
        return Fail("the forest could not be added");
    }
    InstancedObject rocks;
    rocks.set = world->Rocks();
    rocks.mesh = &rock_mesh_;
    rocks.material = instanced_material_;
    rocks.rotation = Quaternion::Identity();
    rocks.max_distance = 500.0f;
    if (!scene_->AddInstanced(rocks).IsValid())
    {
        return Fail("the rocks could not be added");
    }

    // The character and the bodies.
    RenderObjectDesc character;
    character.mesh = &sphere_;
    character.material = default_material_;
    character_object_ = scene_->AddObject(character);
    for (u32 i = 0; i < world->Bodies().Size(); ++i)
    {
        RenderObjectDesc body;
        body.mesh = (i & 1) == 0 ? &sphere_ : &cube_;
        body.material = default_material_;
        body_objects_.Push(scene_->AddObject(body));
    }

    // Lights and sky.
    Light sun;
    sun.type = LightType::Directional;
    sun.direction = Vec3(0.4f, 0.6f, 0.3f);
    sun.color = Vec3(1.0f, 0.95f, 0.85f);
    sun.intensity = 2.5f;
    scene_->AddLight(sun);
    Light ambient;
    ambient.type = LightType::Ambient;
    ambient.color = Vec3(0.5f, 0.6f, 0.8f);
    ambient.intensity = 0.25f;
    scene_->AddLight(ambient);
    sky_.sun_direction = sun.direction;
    sky_.sun_color = Vec3(6.0f, 5.4f, 4.4f);
    sky_.ambient_intensity = 0.6f;
    sky_cube_ = MakeSkyCube();
    SetCubeSky(options.cube_sky);
    Sync();
    return true;
}

void FeaturesGpu::SetCubeSky(bool cube)
{
    if (cube && sky_cube_)
    {
        sky_.mode = SkyMode::Cube;
        sky_.cube = sky_cube_;
    }
    else
    {
        sky_.mode = SkyMode::Procedural;
        sky_.cube = GpuTexturePtr();
    }
}

void FeaturesGpu::Sync()
{
    const physics::PhysicsWorld& physics = world_->Physics();
    for (usize i = 0; i < body_objects_.Size(); ++i)
    {
        const physics::Body* body = physics.Find(world_->Bodies()[i]);
        if (body != nullptr && body_objects_[i].IsValid())
        {
            const Vec3 half = (i & 1) == 0 ? Vec3(0.5f, 0.5f, 0.5f) : Vec3(1.0f, 1.0f, 1.0f);
            scene_->SetTransform(body_objects_[i], Placed(body->position, body->orientation, half));
        }
    }
    if (character_object_.IsValid())
    {
        const Vec3 p = world_->CharacterPosition();
        scene_->SetTransform(character_object_,
                             Placed(Vec3d(p.x, p.y, p.z), Quaternion::Identity(), Vec3(0.4f, 0.9f, 0.4f)));
    }
}

}  // namespace render_features
