#include "features_world.h"

#include "containers/hash.h"
#include "lang/scalar.h"
#include "math/geometry.h"
#include "math/quaternion.h"
#include "physics/query.h"
#include "renderer/instance_scatter.h"

namespace render_features
{

using namespace vse;
using namespace vse::physics;
using namespace vse::renderer;

namespace
{

// ---- terrain samples ------------------------------------------------------------------------------------------------

u32 LatticeHash(u32 x, u32 z, u32 octave)
{
    u32 h = x * 73856093u ^ z * 19349663u ^ (octave + 1u) * 83492791u;
    h ^= h >> 13;
    h *= 0x5bd1e995u;
    h ^= h >> 15;
    h *= 0x27d4eb2du;
    h ^= h >> 16;
    return h;
}

f32 Lattice(u32 x, u32 z, u32 octave)
{
    return static_cast<f32>(LatticeHash(x, z, octave) >> 8) * (1.0f / 16777216.0f);
}

f32 Smooth(f32 t)
{
    return t * t * (3.0f - 2.0f * t);
}

// Value noise of one octave at integer sample (x, z) with lattice cells of `cell` samples.
f32 ValueNoise(u32 x, u32 z, u32 cell, u32 octave)
{
    const u32 ix = x / cell;
    const u32 iz = z / cell;
    const f32 fx = Smooth(static_cast<f32>(x % cell) / static_cast<f32>(cell));
    const f32 fz = Smooth(static_cast<f32>(z % cell) / static_cast<f32>(cell));
    const f32 a = Lattice(ix, iz, octave);
    const f32 b = Lattice(ix + 1, iz, octave);
    const f32 c = Lattice(ix, iz + 1, octave);
    const f32 d = Lattice(ix + 1, iz + 1, octave);
    const f32 top = a + (b - a) * fx;
    const f32 bottom = c + (d - c) * fx;
    return top + (bottom - top) * fz;
}

// ---- geometry -------------------------------------------------------------------------------------------------------

struct Geometry
{
    Vector<StandardVertex> vertices;
    Vector<u32> indices;
};

void PushVertex(Geometry* g, const Vec3& p, const Vec3& n, const Vec3& t, f32 u, f32 v)
{
    StandardVertex sv = {};
    sv.position[0] = p.x;
    sv.position[1] = p.y;
    sv.position[2] = p.z;
    sv.normal[0] = n.x;
    sv.normal[1] = n.y;
    sv.normal[2] = n.z;
    sv.tangent[0] = t.x;
    sv.tangent[1] = t.y;
    sv.tangent[2] = t.z;
    sv.tangent[3] = 1.0f;
    sv.uv[0] = u;
    sv.uv[1] = v;
    g->vertices.Push(sv);
}

// A quad seen from both sides: corners counter-clockwise from the side `normal` points to.
void AddCard(Geometry* g, const Vec3& a, const Vec3& b, const Vec3& c, const Vec3& d, const Vec3& normal)
{
    const Vec3 tangent = (b - a).Normalized();
    const u32 base = static_cast<u32>(g->vertices.Size());
    PushVertex(g, a, normal, tangent, 0, 0);
    PushVertex(g, b, normal, tangent, 1, 0);
    PushVertex(g, c, normal, tangent, 1, 1);
    PushVertex(g, d, normal, tangent, 0, 1);
    const u32 front[6] = {0, 1, 2, 0, 2, 3};
    for (u32 i : front)
    {
        g->indices.Push(base + i);
    }
    const u32 back_base = static_cast<u32>(g->vertices.Size());
    PushVertex(g, a, Vec3(-normal.x, -normal.y, -normal.z), tangent, 0, 0);
    PushVertex(g, b, Vec3(-normal.x, -normal.y, -normal.z), tangent, 1, 0);
    PushVertex(g, c, Vec3(-normal.x, -normal.y, -normal.z), tangent, 1, 1);
    PushVertex(g, d, Vec3(-normal.x, -normal.y, -normal.z), tangent, 0, 1);
    const u32 back[6] = {0, 2, 1, 0, 3, 2};
    for (u32 i : back)
    {
        g->indices.Push(back_base + i);
    }
}

void AddBox(Geometry* g, const Vec3& center, const Vec3& half)
{
    struct Face
    {
        Vec3 n, u, v;
    };
    const Face faces[6] = {
        {Vec3(0, 0, 1), Vec3(1, 0, 0), Vec3(0, 1, 0)},  {Vec3(1, 0, 0), Vec3(0, 1, 0), Vec3(0, 0, 1)},
        {Vec3(0, 1, 0), Vec3(0, 0, 1), Vec3(1, 0, 0)},  {Vec3(0, 0, -1), Vec3(0, 1, 0), Vec3(1, 0, 0)},
        {Vec3(-1, 0, 0), Vec3(0, 0, 1), Vec3(0, 1, 0)}, {Vec3(0, -1, 0), Vec3(1, 0, 0), Vec3(0, 0, 1)},
    };
    const f32 corner[4][2] = {{-1, -1}, {1, -1}, {1, 1}, {-1, 1}};
    for (const Face& f : faces)
    {
        const u32 base = static_cast<u32>(g->vertices.Size());
        for (u32 c = 0; c < 4; ++c)
        {
            const Vec3 offset = f.n + f.u * corner[c][0] + f.v * corner[c][1];
            PushVertex(g, center + Vec3(offset.x * half.x, offset.y * half.y, offset.z * half.z), f.n, f.u,
                       corner[c][0] * 0.5f + 0.5f, corner[c][1] * 0.5f + 0.5f);
        }
        const u32 order[6] = {0, 1, 2, 0, 2, 3};
        for (u32 i : order)
        {
            g->indices.Push(base + i);
        }
    }
}

const char* ToMesh(const Geometry& g, MeshData* out)
{
    return out->Set(StandardVertexLayout(), 0, g.vertices.Data(), static_cast<u32>(g.vertices.Size()),
                    rhi::IndexType::UInt32, g.indices.Data(), static_cast<u32>(g.indices.Size()));
}

// `src` (the standard layout) with its positions scaled and moved; normals follow the inverse transpose.
const char* Reshape(const MeshData& src, const Vec3& scale, const Vec3& offset, MeshData* out)
{
    Geometry g;
    const StandardVertex* in = static_cast<const StandardVertex*>(src.Vertices());
    for (u32 i = 0; i < src.VertexCount(); ++i)
    {
        StandardVertex v = in[i];
        v.position[0] = v.position[0] * scale.x + offset.x;
        v.position[1] = v.position[1] * scale.y + offset.y;
        v.position[2] = v.position[2] * scale.z + offset.z;
        const Vec3 n = Vec3(v.normal[0] / scale.x, v.normal[1] / scale.y, v.normal[2] / scale.z).Normalized();
        v.normal[0] = n.x;
        v.normal[1] = n.y;
        v.normal[2] = n.z;
        const Vec3 t = Vec3(v.tangent[0] * scale.x, v.tangent[1] * scale.y, v.tangent[2] * scale.z).Normalized();
        v.tangent[0] = t.x;
        v.tangent[1] = t.y;
        v.tangent[2] = t.z;
        g.vertices.Push(v);
    }
    for (u32 i = 0; i < src.IndexCount(); ++i)
    {
        g.indices.Push(src.IndexAt(i));
    }
    return ToMesh(g, out);
}

Aabb BoundsOf(const MeshData& mesh)
{
    Vec3 lo(1e30f, 1e30f, 1e30f);
    Vec3 hi(-1e30f, -1e30f, -1e30f);
    for (u32 i = 0; i < mesh.VertexCount(); ++i)
    {
        const f32* p = mesh.PositionAt(i);
        lo = Vec3(FMin(lo.x, p[0]), FMin(lo.y, p[1]), FMin(lo.z, p[2]));
        hi = Vec3(FMax(hi.x, p[0]), FMax(hi.y, p[1]), FMax(hi.z, p[2]));
    }
    return Aabb(lo, hi);
}

// ---- the forest -----------------------------------------------------------------------------------------------------

struct SplitMix
{
    u64 state;
    u64 Next()
    {
        u64 z = (state += 0x9E3779B97F4A7C15ull);
        z = (z ^ (z >> 30)) * 0xBF58476D1CE4E5B9ull;
        z = (z ^ (z >> 27)) * 0x94D049BB133111EBull;
        return z ^ (z >> 31);
    }
    f32 Unit() { return static_cast<f32>(Next() >> 40) * (1.0f / 16777216.0f); }
};

// Yaw about y in steps of 45 degrees, from exact constants (no trigonometry in the scene's structure).
Quaternion Yaw8(u32 k)
{
    const f32 s22 = 0.38268343f;
    const f32 c22 = 0.92387953f;
    const f32 h = 0.70710678f;
    const f32 table[8][2] = {{0, 1}, {s22, c22}, {h, h}, {c22, s22}, {1, 0}, {c22, -s22}, {h, -h}, {s22, -c22}};
    return Quaternion(0.0f, table[k & 7][0], 0.0f, table[k & 7][1]);
}

Quaternion TiltZ(f32 half_sin, f32 half_cos)
{
    return Quaternion(0.0f, 0.0f, half_sin, half_cos);
}

struct TreeHeightUser
{
    const PhysicsWorld* world;
};

bool TerrainHeightByRay(void* user, f64 x, f64 z, f64* height)
{
    const PhysicsWorld* world = static_cast<TreeHeightUser*>(user)->world;
    PhysicsHit hit;
    if (!TerrainRayCast(*world, Vec3d(x, 500.0, z), Vec3(0.0f, -1.0f, 0.0f), 600.0f, PhysicsQueryFilter(), &hit))
    {
        return false;
    }
    *height = hit.point.y;
    return true;
}

u64 Fnv(u64 h, u32 v)
{
    h ^= v;
    return h * kFnv1a64Prime;
}

}  // namespace

Vector<u16> MakeTerrainSamples(u32 side)
{
    Vector<u16> samples;
    samples.Reserve(static_cast<usize>(side) * side);
    const u32 cells[4] = {256, 128, 48, 16};
    const f32 amplitude[4] = {0.62f, 0.24f, 0.10f, 0.04f};
    for (u32 z = 0; z < side; ++z)
    {
        for (u32 x = 0; x < side; ++x)
        {
            f32 n = 0.0f;
            for (u32 o = 0; o < 4; ++o)
            {
                n += ValueNoise(x, z, cells[o], o) * amplitude[o];
            }
            samples.Push(static_cast<u16>(n * 60000.0f));
        }
    }
    return samples;
}

FeaturesWorld::~FeaturesWorld() = default;

bool FeaturesWorld::Fail(const char* what)
{
    error_ = what;
    return false;
}

const char* FeaturesWorld::BuildMeshes()
{
    const char* error = nullptr;
    const auto keep = [&error](const char* result)
    {
        if (error == nullptr)
        {
            error = result;
        }
    };
    MeshData sphere_hi, sphere_lo, cube;
    keep(MakeSphereMesh(1.0f, 8, 6, &sphere_hi));
    keep(MakeSphereMesh(1.0f, 6, 4, &sphere_lo));
    keep(MakeCubeMesh(1.0f, &cube));
    // 0 trunk and 1 its far form, 2 a branch, 3 a twig: spindles standing on their origin.
    keep(Reshape(sphere_hi, Vec3(0.28f, 2.6f, 0.28f), Vec3(0.0f, 2.6f, 0.0f), &tree_meshes_[kMeshTrunk]));
    keep(Reshape(cube, Vec3(0.45f, 5.2f, 0.45f), Vec3(0.0f, 2.6f, 0.0f), &tree_meshes_[kMeshTrunkLow]));
    keep(Reshape(sphere_lo, Vec3(0.12f, 1.1f, 0.12f), Vec3(0.0f, 1.1f, 0.0f), &tree_meshes_[kMeshBranch]));
    keep(Reshape(sphere_lo, Vec3(0.07f, 0.7f, 0.07f), Vec3(0.0f, 0.7f, 0.0f), &tree_meshes_[kMeshTwig]));
    // 4 the leaf cards: eight crossed quads around the crown's centre, seen from both sides; 5 the crown as one blob.
    Geometry cards;
    const f32 half_size = 1.0f;
    const f32 s22 = 0.38268343f;
    const f32 c22 = 0.92387953f;
    for (u32 k = 0; k < 4; ++k)
    {
        const f32 cx = k == 0 ? 1.0f : k == 1 ? c22 : k == 2 ? 0.0f : -c22;
        const f32 cz = k == 0 ? 0.0f : k == 1 ? s22 : k == 2 ? 1.0f : s22;
        const Vec3 along(cx * half_size, 0.0f, cz * half_size);
        const Vec3 normal(-cz, 0.0f, cx);
        for (u32 tier = 0; tier < 2; ++tier)
        {
            const f32 y0 = tier == 0 ? -0.9f : -0.1f;
            const f32 y1 = y0 + 1.6f;
            AddCard(&cards, Vec3(-along.x, y0, -along.z), Vec3(along.x, y0, along.z), Vec3(along.x, y1, along.z),
                    Vec3(-along.x, y1, -along.z), normal);
        }
    }
    keep(ToMesh(cards, &tree_meshes_[kMeshLeaves]));
    keep(Reshape(sphere_lo, Vec3(1.5f, 1.2f, 1.5f), Vec3(0.0f, 0.0f, 0.0f), &tree_meshes_[kMeshLeavesLow]));
    // 6 the stand-in for a whole far tree: a box for the trunk and a box for the crown.
    Geometry standin;
    AddBox(&standin, Vec3(0.0f, 2.6f, 0.0f), Vec3(0.25f, 2.6f, 0.25f));
    AddBox(&standin, Vec3(0.0f, 5.6f, 0.0f), Vec3(1.5f, 1.4f, 1.5f));
    keep(ToMesh(standin, &tree_meshes_[kMeshStandin]));
    // The rock: a squashed sphere.
    MeshData rock;
    keep(MakeSphereMesh(1.0f, 12, 8, &rock));
    keep(Reshape(rock, Vec3(1.0f, 0.7f, 0.9f), Vec3(0.0f, 0.2f, 0.0f), &rock_mesh_));
    return error;
}

bool FeaturesWorld::BuildForest(jobs::Scheduler* scheduler)
{
    TreeMeshInfo infos[kTreeMeshCount];
    for (u32 i = 0; i < kTreeMeshCount; ++i)
    {
        infos[i].bounds = BoundsOf(tree_meshes_[i]);
    }
    InstanceTreeBuilder builder(Span<const TreeMeshInfo>(infos, kTreeMeshCount));
    SplitMix rng{0x7265656573ull};
    const f64 extent = static_cast<f64>(options_.side - 1) * CellSize();
    u32 placed = 0;
    const u32 attempts_max = options_.trees * 3 + 16;
    for (u32 attempt = 0; attempt < attempts_max && placed < options_.trees; ++attempt)
    {
        const f64 x = 6.0 + static_cast<f64>(rng.Unit()) * (extent - 12.0);
        const f64 z = 6.0 + static_cast<f64>(rng.Unit()) * (extent - 12.0);
        const f64 dx = x - extent * 0.5;
        const f64 dz = z - extent * 0.5;
        if (dx * dx + dz * dz < 40.0 * 40.0)
        {
            continue;  // a clearing where the character starts
        }
        PhysicsHit hit;
        if (!TerrainRayCast(world_, Vec3d(x, 500.0, z), Vec3(0.0f, -1.0f, 0.0f), 600.0f, PhysicsQueryFilter(), &hit) ||
            hit.normal.y < 0.88f)
        {
            continue;  // off the map or too steep to stand on
        }
        const f32 size = 0.8f + 0.5f * rng.Unit();
        const f32 shade = 0.8f + 0.4f * rng.Unit();
        const Quaternion yaw = Yaw8(static_cast<u32>(rng.Next() >> 61));

        TreeNodeDesc root;
        root.position = Vec3d(x, hit.point.y, z);
        root.rotation = yaw;
        root.scale = Vec3(size, size, size);
        root.mesh = kMeshTrunk;
        root.extra.tint = Vec4(0.30f * shade, 0.20f * shade, 0.12f * shade, 1.0f);
        root.has_lod = true;
        root.lod.level_count = 2;
        root.lod.mesh[1] = kMeshTrunkLow;
        root.lod.switch_distance[0] = 80.0f;
        root.lod.cull_distance = 700.0f;
        root.lod.cull_children_beyond = true;
        root.lod.standin_mesh = kMeshStandin;
        root.lod.standin_distance = 220.0f;
        root.lod.hysteresis = 4.0f;
        TreeNodeId root_id;
        if (builder.AddRoot(root, &root_id) != TreeStatus::Ok)
        {
            return Fail("the forest refused a root");
        }
        struct Branch
        {
            Quaternion tilt;
            f32 height;
            Quaternion twig_tilt;
        };
        const Quaternion flip(0.0f, 1.0f, 0.0f, 0.0f);
        const Branch branches[2] = {{TiltZ(0.42261826f, 0.90630779f), 3.4f, TiltZ(-0.38268343f, 0.92387953f)},
                                    {flip * TiltZ(0.34202014f, 0.93969262f), 4.2f, TiltZ(0.38268343f, 0.92387953f)}};
        for (const Branch& b : branches)
        {
            TreeNodeDesc branch;
            branch.position = Vec3d(0.0, b.height, 0.0);
            branch.rotation = b.tilt;
            branch.mesh = kMeshBranch;
            branch.extra.tint = root.extra.tint;
            branch.has_lod = true;
            branch.lod.cull_distance = 160.0f;
            branch.lod.cull_children_beyond = true;
            TreeNodeId branch_id;
            if (builder.AddChild(root_id, branch, &branch_id) != TreeStatus::Ok)
            {
                return Fail("the forest refused a branch");
            }
            TreeNodeDesc twig;
            twig.position = Vec3d(0.0, 2.0, 0.0);
            twig.rotation = b.twig_tilt;
            twig.mesh = kMeshTwig;
            twig.extra.tint = root.extra.tint;
            twig.has_lod = true;
            twig.lod.cull_distance = 110.0f;
            twig.lod.cull_children_beyond = true;
            TreeNodeId twig_id;
            if (builder.AddChild(branch_id, twig, &twig_id) != TreeStatus::Ok)
            {
                return Fail("the forest refused a twig");
            }
        }
        TreeNodeDesc leaves;
        leaves.position = Vec3d(0.0, 5.0, 0.0);
        leaves.mesh = kMeshLeaves;
        leaves.extra.tint = Vec4(0.14f * shade, 0.42f * shade, 0.10f * shade, 1.0f);
        leaves.has_lod = true;
        leaves.lod.level_count = 2;
        leaves.lod.mesh[1] = kMeshLeavesLow;
        leaves.lod.switch_distance[0] = 60.0f;
        leaves.lod.cull_distance = 300.0f;
        leaves.lod.cull_children_beyond = true;
        TreeNodeId leaves_id;
        if (builder.AddChild(root_id, leaves, &leaves_id) != TreeStatus::Ok)
        {
            return Fail("the forest refused the leaves");
        }
        ++placed;
    }
    trees_placed_ = placed;
    forest_ = builder.Finish(scheduler);
    if (!forest_)
    {
        return Fail("the forest could not be finished");
    }
    return true;
}

bool FeaturesWorld::Build(const WorldOptions& options, jobs::Scheduler* scheduler)
{
    options_ = options;
    if (options.side < 64 || options.side > 8192)
    {
        return Fail("the terrain side is out of range");
    }
    samples_ = MakeTerrainSamples(options.side);
    Validation v = Validation::Ok;
    field_ = HeightField::CreateFromSamples(Span<const u16>(samples_.Data(), samples_.Size()), options.side,
                                            options.side, 0.0f, kHeightRange, CellSize(), &v);
    if (!field_)
    {
        return Fail("the height field was refused");
    }
    StepConfig config = world_.GetStepConfig();
    config.threads = options.threads == 0 ? 1 : options.threads;
    world_.SetStepConfig(config);
    if (scheduler != nullptr)
    {
        world_.SetScheduler(scheduler);
    }
    const f64 half = static_cast<f64>(options.side - 1) * CellSize() * 0.5;
    BodyDesc ground;
    ground.settings.type = BodyType::Static;
    ground.position = Vec3d(half, 0.0, half);
    floor_ = world_.CreateBody(ground);
    Collider fc;
    fc.shape = Shape::MakeHeightField(world_.HeightFields().Add(field_), field_->Extent());
    world_.AddCollider(floor_, fc);
    world_.Step();  // builds the query index the ray casts use

    if (const char* mesh_error = BuildMeshes())
    {
        return Fail(mesh_error);
    }
    if (!BuildForest(scheduler))
    {
        return false;
    }
    rocks_ = InstanceSet::Create(InstanceSetDesc{true, options.rocks == 0 ? 1u : options.rocks, 4.0, 8.0f});
    if (options.rocks != 0)
    {
        ScatterRule rule;
        rule.seed = 99;
        rule.count = options.rocks;
        rule.min_x = rule.min_z = 20.0;
        rule.max_x = rule.max_z = static_cast<f64>(options.side - 1) * CellSize() - 20.0;
        rule.min_scale = 0.7f;
        rule.max_scale = 2.4f;
        rule.lift = -0.1f;
        InstanceExtra grey;
        grey.tint = Vec4(0.46f, 0.44f, 0.41f, 1.0f);
        TreeHeightUser user{&world_};
        const AddManyResult r = Scatter(rocks_.Get(), rule, &grey, &TerrainHeightByRay, &user);
        if (r.status != InstanceStatus::Ok)
        {
            return Fail("the rocks were refused");
        }
    }

    // The character stands in the middle of the map; the bodies are dropped around it.
    const f32 cx = static_cast<f32>(half);
    const f32 cz = static_cast<f32>(half);
    CharacterConfig cc;
    if (character_.Create(&world_, cc, Vec3d(cx, field_->HeightAtMap(cx, cz) + 1.2f, cz)) != BodyStatus::Ok)
    {
        return Fail("the character was refused");
    }
    for (u32 i = 0; i < options.bodies; ++i)
    {
        const f32 x = cx + 4.0f + static_cast<f32>(i % 4) * 2.5f;
        const f32 z = cz + 3.0f + static_cast<f32>(i / 4) * 2.5f;
        BodyDesc desc;
        desc.settings.type = BodyType::Dynamic;
        desc.position = Vec3d(x, field_->HeightAtMap(x, z) + 4.0f + static_cast<f32>(i), z);
        const BodyId id = world_.CreateBody(desc);
        Collider c;
        c.shape = (i & 1) == 0 ? Shape::MakeSphere(BodyRadius()) : Shape::MakeBox(Vec3(0.5f, 0.5f, 0.5f));
        world_.AddCollider(id, c);
        bodies_.Push(id);
    }
    return true;
}

void FeaturesWorld::Step()
{
    // Eight headings in turn, a few seconds each, on exact components.
    const f32 h = 0.70710678f;
    const f32 dirs[8][2] = {{1, 0}, {h, h}, {0, 1}, {-h, h}, {-1, 0}, {-h, -h}, {0, -1}, {h, -h}};
    const u32 heading = (steps_ / 180) % 8;
    const f32 speed = 4.0f;
    character_.Move(&world_, Vec3(dirs[heading][0] * speed, 0.0f, dirs[heading][1] * speed), kStepSeconds);
    world_.Step();
    ++steps_;
}

u64 FeaturesWorld::PhysicsHash(bool* valid) const
{
    return world_.StateHash(valid);
}

Vec3 FeaturesWorld::CharacterPosition() const
{
    const Body* body = world_.Find(character_.BodyHandle());
    return body != nullptr ? body->position.Narrow() : Vec3(0.0f, 0.0f, 0.0f);
}

Camera FeaturesWorld::ChaseCamera(f32 aspect) const
{
    const Vec3 at = CharacterPosition();
    const Vec3 eye(at.x - 9.0f, at.y + 12.0f, at.z - 22.0f);
    Camera camera = MakeLookAtCamera(eye, Vec3(at.x, at.y + 1.0f, at.z + 18.0f), Vec3(0.0f, 1.0f, 0.0f));
    camera.aspect = aspect;
    camera.far_plane = 1500.0f;
    return camera;
}

VisibleSummary FeaturesWorld::Visible(const Camera& camera, jobs::Scheduler* scheduler)
{
    VisibleSummary summary;
    const Frustum frustum = Frustum::FromViewProjection(camera.ViewProjection());
    TreeCullParams tp;
    tp.frustum = frustum;
    tp.render_origin = Vec3d(0.0, 0.0, 0.0);
    tp.camera_position = camera.position;
    CullInstanceTree(*forest_, tp, &lod_state_, scheduler, &tree_cull_);
    summary.tree = tree_cull_.stats;
    u64 h = kFnv1a64Basis;
    for (const TreeVisibleNode& n : tree_cull_.nodes)
    {
        h = Fnv(h, n.node);
        h = Fnv(h, n.mesh);
        h = Fnv(h, n.level);
    }
    InstanceCullParams rp;
    rp.frustum = frustum;
    rp.render_origin = Vec3d(0.0, 0.0, 0.0);
    rp.camera_position = camera.position;
    rp.mesh_radius = 1.0f;
    rp.max_distance = 500.0f;
    CullInstances(*rocks_, rp, scheduler, &rock_cull_);
    summary.rocks = rock_cull_.stats;
    for (const InstanceRun& r : rock_cull_.runs)
    {
        h = Fnv(h, r.chunk);
        h = Fnv(h, r.first);
        h = Fnv(h, r.count);
    }
    summary.hash = h;
    return summary;
}

}  // namespace render_features
