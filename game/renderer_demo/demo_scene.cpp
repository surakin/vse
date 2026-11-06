#include "demo_scene.h"

#include <cmath>
#include <cstdio>
#include <cstring>

#include "demo_assets.h"
#include "rhi/rhi_log.h"

namespace renderer_demo {

using namespace engine_placeholder::renderer;

namespace {

// A small deterministic generator (an LCG): the scene must be the same on every machine.
struct Random {
    u32 state;
    explicit Random(u32 seed) : state(seed * 2654435761u + 12345u) {}
    u32 Next() {
        state = state * 1664525u + 1013904223u;
        return state >> 8;
    }
    f32 Unit() { return static_cast<f32>(Next()) / static_cast<f32>(1u << 24); }
    f32 Range(f32 lo, f32 hi) { return lo + (hi - lo) * Unit(); }
};

constexpr u32 kGroundTilesPerSide = 7;
constexpr u32 kGroundTiles = kGroundTilesPerSide * kGroundTilesPerSide;
constexpr f32 kTileSize = 20.0f;
constexpr f32 kSpacing = 2.4f;

enum MaterialSlot : u32 {
    kGround,
    kPlastic,
    kChecker,
    kStripes,
    kBumps,
    kGold,
    kCopper,
    kEmissive,
    kVertexColor,
    kMaterialCount,
};

Mat4 Transform(const Vec3& position, f32 yaw, f32 scale) {
    const f32 c = std::cos(yaw) * scale;
    const f32 s = std::sin(yaw) * scale;
    return Mat4::FromBasis(Vec3(c, 0, -s), Vec3(0, scale, 0), Vec3(s, 0, c), position);
}

// Produces the image of one demo file: read from asset_dir when possible, procedural otherwise. Called at creation and again
// at every device reset, so a texture never keeps its pixels in memory.
bool LoadTextureImage(void* user, TextureImage* out) {
    const auto* info = static_cast<const TextureLoadInfo*>(user);
    const DemoTexture& texture = *info->texture;
    if (info->asset_dir != nullptr) {
        char path[1024];
        std::snprintf(path, sizeof(path), "%s/%s", info->asset_dir, texture.file);
        demo::RtexImage image;
        if (demo::ReadRtex(path, &image) && image.format == texture.format) {
            out->desc.width = image.width;
            out->desc.height = image.height;
            out->desc.mip_levels = image.mip_count;
            out->desc.format = image.format;
            out->desc.usage = rhi::TextureUsage::Sampled;
            out->pixels.Resize(image.data.size());
            std::memcpy(out->pixels.Data(), image.data.data(), image.data.size());
            return true;
        }
        rhi::RhiLogV(LogVerbosity::Warn, "renderer_demo: %s could not be loaded; using the procedural texture", path);
    }
    const demo::Pixels pixels = texture.make();
    out->desc.width = pixels.width;
    out->desc.height = pixels.height;
    out->desc.mip_levels = demo::FullMipCount(pixels.width, pixels.height);
    out->desc.format = texture.format;
    out->desc.usage = rhi::TextureUsage::Sampled;
    out->pixels.Resize(pixels.rgba.size());
    std::memcpy(out->pixels.Data(), pixels.rgba.data(), pixels.rgba.size());
    out->generate_mips = true;
    return true;
}

struct Placement {
    u32 mesh;     // 0 sphere, 1 cube, 2 colored sphere, 3 ground tile
    u32 material; // MaterialSlot
    Mat4 transform;
};

} // namespace

DemoScene::DemoScene() = default;
DemoScene::~DemoScene() { Destroy(); }

void DemoScene::Destroy() {
    if (device_ == nullptr) return;
    device_->WaitIdle();
    scene_.Reset();
    sphere_.Destroy();
    colored_sphere_.Destroy();
    cube_.Destroy();
    tile_.Destroy();
    for (renderer::GpuTexture& t : textures_) {
        t.Destroy();
    }
    light_ids_.Reset();
    light_base_.Reset();
    light_phase_.Reset();
    device_ = nullptr;
    materials_ = nullptr;
}

bool DemoScene::Create(rhi::Device* device, MaterialSystem* materials, const GBufferTemplates& templates, const SceneOptions& options) {
    if (device_ != nullptr) Destroy();
    if (options.objects < kGroundTiles + 15) {
        rhi::RhiLogV(LogVerbosity::Error, "renderer_demo: need at least %u objects", kGroundTiles + 15);
        return false;
    }
    device_ = device;
    materials_ = materials;

    // Meshes.
    MeshData sphere, cube, tile, colored;
    const char* error = MakeSphereMesh(1.0f, 32, 16, &sphere);
    if (error == nullptr) error = MakeCubeMesh(1.5f, &cube);
    if (error == nullptr) error = MakePlaneMesh(kTileSize, kTileSize, 1, 1, &tile);
    if (error == nullptr) {
        // A gradient over the sphere's height: sRGB bytes per vertex.
        Vector<u8> colors;
        colors.Resize(static_cast<usize>(sphere.VertexCount()) * 4);
        for (u32 i = 0; i < sphere.VertexCount(); ++i) {
            const f32 y = sphere.PositionAt(i)[1] * 0.5f + 0.5f;
            const f32 x = sphere.PositionAt(i)[0] * 0.5f + 0.5f;
            colors[i * 4 + 0] = static_cast<u8>(40 + 200 * y);
            colors[i * 4 + 1] = static_cast<u8>(220 - 160 * y);
            colors[i * 4 + 2] = static_cast<u8>(60 + 180 * x);
            colors[i * 4 + 3] = 255;
        }
        error = MakeColoredMesh(sphere, colors.Data(), sphere.VertexCount(), &colored);
    }
    if (error != nullptr) {
        rhi::RhiLogV(LogVerbosity::Error, "renderer_demo: building a mesh failed: %s", error);
        Destroy();
        return false;
    }
    if (!sphere_.Create(device, sphere, "sphere") || !colored_sphere_.Create(device, colored, "colored sphere") || !cube_.Create(device, cube, "cube") ||
        !tile_.Create(device, tile, "ground tile")) {
        rhi::RhiLogV(LogVerbosity::Error, "renderer_demo: uploading a mesh failed");
        Destroy();
        return false;
    }
    if (options.resources != nullptr) {
        // Keep the geometry so the meshes can be uploaded again after a device reset.
        if (!sphere_.AdoptSource(options.resources, static_cast<MeshData&&>(sphere), "sphere") ||
            !colored_sphere_.AdoptSource(options.resources, static_cast<MeshData&&>(colored), "colored sphere") ||
            !cube_.AdoptSource(options.resources, static_cast<MeshData&&>(cube), "cube") ||
            !tile_.AdoptSource(options.resources, static_cast<MeshData&&>(tile), "ground tile")) {
            rhi::RhiLogV(LogVerbosity::Error, "renderer_demo: registering the meshes failed");
            Destroy();
            return false;
        }
    }
    {
        // Barriers that move the buffers out of their upload state, in one submission.
        device->BeginFrame();
        rhi::CommandList* list = device->CreateCommandList();
        if (list == nullptr) {
            device->EndFrame();
            Destroy();
            return false;
        }
        list->Begin();
        sphere_.RecordPrepare(list);
        colored_sphere_.RecordPrepare(list);
        cube_.RecordPrepare(list);
        tile_.RecordPrepare(list);
        list->End();
        rhi::CommandList* lists[] = {list};
        const bool submitted = device->Submit(lists, 1) != 0;
        device->EndFrame();
        device->WaitIdle();
        if (!submitted) {
            rhi::RhiLogV(LogVerbosity::Error, "renderer_demo: submitting the mesh uploads failed");
            Destroy();
            return false;
        }
        sphere_.MarkReady();
        colored_sphere_.MarkReady();
        cube_.MarkReady();
        tile_.MarkReady();
    }

    // Textures and materials.
    u32 texture_count = 0;
    const DemoTexture* texture_table = DemoTextures(&texture_count);
    for (u32 i = 0; i < texture_count && i < 3; ++i) {
        if (options.asset_dir != nullptr) {
            std::snprintf(asset_dir_, sizeof(asset_dir_), "%s", options.asset_dir);
        }
        load_info_[i].asset_dir = options.asset_dir != nullptr ? asset_dir_ : nullptr;
        load_info_[i].texture = &texture_table[i];
        if (!textures_[i].CreateFromLoader(device, &LoadTextureImage, &load_info_[i], options.resources, texture_table[i].file)) {
            rhi::RhiLogV(LogVerbosity::Error, "renderer_demo: could not create the texture %s", texture_table[i].file);
            Destroy();
            return false;
        }
    }
    MaterialId ids[kMaterialCount];
    auto make = [&](MaterialSlot slot, const char* name, GBufferOptions go, Vec4 color, f32 roughness, f32 metallic, f32 emissive) {
        ids[slot] = CreateGBufferMaterial(materials, templates, go, name);
        if (!ids[slot].IsValid()) return false;
        materials->SetVec4(ids[slot], "base_color", color);
        materials->SetFloat(ids[slot], "roughness", roughness);
        materials->SetFloat(ids[slot], "metallic", metallic);
        materials->SetFloat(ids[slot], "emissive_scale", emissive);
        return true;
    };
    GBufferOptions plain;
    GBufferOptions bumped;
    bumped.normal_map = true;
    GBufferOptions vertex_colored;
    vertex_colored.vertex_color = true;
    bool ok = make(kGround, "ground", plain, Vec4(1, 1, 1, 1), 0.85f, 0.0f, 0.0f) && make(kPlastic, "plastic", plain, Vec4(0.8f, 0.8f, 0.82f, 1), 0.5f, 0.0f, 0.0f) &&
              make(kChecker, "checker", plain, Vec4(1, 1, 1, 1), 0.6f, 0.0f, 0.0f) && make(kStripes, "stripes", plain, Vec4(1, 1, 1, 1), 0.35f, 0.0f, 0.0f) &&
              make(kBumps, "bumps", bumped, Vec4(0.9f, 0.45f, 0.2f, 1), 0.5f, 0.0f, 0.0f) &&
              make(kGold, "gold", plain, Vec4(1.0f, 0.77f, 0.34f, 1), 0.2f, 1.0f, 0.0f) && make(kCopper, "copper", plain, Vec4(0.95f, 0.5f, 0.35f, 1), 0.6f, 1.0f, 0.0f) &&
              make(kEmissive, "emissive", plain, Vec4(0.1f, 0.8f, 0.7f, 1), 0.8f, 0.0f, 0.9f) && make(kVertexColor, "vertex color", vertex_colored, Vec4(1, 1, 1, 1), 0.5f, 0.0f, 0.0f);
    if (ok) {
        ok = materials->SetTexture(ids[kGround], "albedo_tex", textures_[0].Handle()) && materials->SetTexture(ids[kChecker], "albedo_tex", textures_[0].Handle()) &&
             materials->SetTexture(ids[kStripes], "albedo_tex", textures_[1].Handle()) && materials->SetTexture(ids[kBumps], "normal_tex", textures_[2].Handle());
    }
    if (!ok) {
        rhi::RhiLogV(LogVerbosity::Error, "renderer_demo: creating the materials failed");
        Destroy();
        return false;
    }

    // Placements: the ground tiles, then the shapes on a grid.
    Random random(options.seed);
    Vector<Placement> placements;
    placements.Reserve(options.objects);
    const f32 half_tiles = static_cast<f32>(kGroundTilesPerSide) * 0.5f;
    for (u32 z = 0; z < kGroundTilesPerSide; ++z) {
        for (u32 x = 0; x < kGroundTilesPerSide; ++x) {
            placements.Push(Placement{3, kGround, Transform(Vec3((static_cast<f32>(x) + 0.5f - half_tiles) * kTileSize, 0, (static_cast<f32>(z) + 0.5f - half_tiles) * kTileSize), 0, 1)});
        }
    }
    const u32 shapes = options.objects - kGroundTiles;
    const u32 columns = static_cast<u32>(std::ceil(std::sqrt(static_cast<f32>(shapes) * 1.6f)));
    const u32 rows = (shapes + columns - 1) / columns;
    const f32 width = static_cast<f32>(columns - 1) * kSpacing;
    const f32 depth = static_cast<f32>(rows - 1) * kSpacing;
    for (u32 i = 0; i < shapes; ++i) {
        const u32 cx = i % columns;
        const u32 cz = i / columns;
        const f32 scale = random.Range(0.55f, 0.85f);
        const bool is_cube = (cx + cz) % 3 == 0;
        const u32 slot = kPlastic + (i * 7 + cz) % (kMaterialCount - kPlastic);
        u32 mesh = is_cube ? 1u : 0u;
        if (slot == kVertexColor) mesh = 2; // only the colored sphere has per-vertex colors
        const f32 rest = mesh == 1 ? 0.75f * scale : scale; // sit on the ground
        // Each random value is drawn in its own statement: the order of evaluation of function arguments is unspecified, and the
        // scene must be the same whatever the compiler.
        const f32 jitter_x = random.Range(-0.25f, 0.25f);
        const f32 jitter_z = random.Range(-0.25f, 0.25f);
        const f32 yaw = random.Range(0.0f, 6.2831853f);
        const Vec3 position(static_cast<f32>(cx) * kSpacing - width * 0.5f + jitter_x, rest, static_cast<f32>(cz) * kSpacing - depth * 0.5f + jitter_z);
        placements.Push(Placement{mesh, slot, Transform(position, yaw, scale)});
    }
    radius_ = std::sqrt(width * width + depth * depth) * 0.5f + 2.0f;

    // Add the objects, in their natural order or in a shuffled one (a Fisher-Yates shuffle with its own generator).
    Vector<u32> order;
    order.Resize(placements.Size());
    for (u32 i = 0; i < placements.Size(); ++i) order[i] = i;
    if (options.add_order_seed != 0) {
        Random shuffle(options.add_order_seed);
        for (usize i = order.Size() - 1; i > 0; --i) {
            const usize j = shuffle.Next() % (i + 1);
            const u32 t = order[i];
            order[i] = order[j];
            order[j] = t;
        }
    }
    light_capacity_ = options.lights + 2;
    RenderSceneDesc sd;
    sd.max_objects = options.objects;
    sd.max_lights = light_capacity_ < kMaxLights ? light_capacity_ : kMaxLights;
    scene_ = MakeUnique<RenderScene>(sd);
    const GpuMesh* meshes[4] = {&sphere_, &cube_, &colored_sphere_, &tile_};
    for (usize i = 0; i < order.Size(); ++i) {
        const Placement& p = placements[order[i]];
        RenderObjectDesc od;
        od.mesh = meshes[p.mesh];
        od.material = ids[p.material].index;
        od.transform = p.transform;
        if (!scene_->AddObject(od).IsValid()) {
            rhi::RhiLogV(LogVerbosity::Error, "renderer_demo: the scene is full");
            Destroy();
            return false;
        }
    }

    // Lights: one sun, a little ambient, and the point lights scattered over the shapes at low height.
    Light sun;
    sun.type = LightType::Directional;
    sun.direction = Vec3(0.35f, 0.8f, 0.45f);
    sun.color = Vec3(1.0f, 0.95f, 0.85f);
    sun.intensity = 0.9f;
    scene_->AddLight(sun);
    Light ambient;
    ambient.type = LightType::Ambient;
    ambient.color = Vec3(0.35f, 0.4f, 0.5f);
    ambient.intensity = 0.15f;
    scene_->AddLight(ambient);
    Random lr(options.seed ^ 0x9e3779b9u);
    for (u32 i = 0; i < options.lights; ++i) {
        const f32 base_x = lr.Range(-width * 0.5f, width * 0.5f);
        const f32 base_y = lr.Range(0.8f, 2.2f);
        const f32 base_z = lr.Range(-depth * 0.5f, depth * 0.5f);
        const Vec3 base(base_x, base_y, base_z);
        Light l;
        l.type = LightType::Point;
        l.position = base;
        l.radius = lr.Range(3.0f, 5.5f);
        l.intensity = lr.Range(2.0f, 6.0f);
        const f32 hue = lr.Unit();
        l.color = Vec3(0.35f + 0.65f * std::fabs(std::sin(hue * 6.2831853f)), 0.35f + 0.65f * std::fabs(std::sin(hue * 6.2831853f + 2.0f)),
                       0.35f + 0.65f * std::fabs(std::sin(hue * 6.2831853f + 4.0f)));
        const LightId id = scene_->AddLight(l);
        if (!id.IsValid()) {
            rhi::RhiLogV(LogVerbosity::Error, "renderer_demo: the scene cannot hold %u lights", options.lights);
            Destroy();
            return false;
        }
        light_ids_.Push(id);
        light_base_.Push(base);
        const f32 phase = lr.Range(0.0f, 6.2831853f);
        light_phase_.Push(phase);
    }
    return true;
}

void DemoScene::Animate(f32 seconds) {
    for (usize i = 0; i < light_ids_.Size(); ++i) {
        Light l = *scene_->FindLight(light_ids_[i]);
        const f32 a = seconds * 0.5f + light_phase_[i];
        l.position = Vec3(light_base_[i].x + 1.2f * std::cos(a), light_base_[i].y + 0.3f * std::sin(a * 1.7f), light_base_[i].z + 1.2f * std::sin(a));
        scene_->SetLight(light_ids_[i], l);
    }
}

Camera DemoScene::OverviewCamera(f32 aspect) const {
    Camera cam = MakeLookAtCamera(Vec3(radius_ * 0.55f, radius_ * 0.5f, radius_ * 0.85f), Vec3(0, 0.5f, 0), Vec3(0, 1, 0));
    cam.aspect = aspect;
    cam.fov_y = 0.9f;
    cam.near_plane = 0.1f;
    cam.far_plane = radius_ * 6.0f + 60.0f;
    return cam;
}

} // namespace renderer_demo
