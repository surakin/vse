// render_features_demo: a terrain (a 1024 x 1024 procedural height map as a height-field collider) with a sphere and a
// box dropped onto it from fixed positions. `--headless` steps the physics and prints the hash of its state, which is
// the same at every thread count.
//
//   render_features_demo --headless [--steps N] [--threads N]
#include "containers/vector.h"
#include "containers/span.h"
#include "jobs/scheduler.h"
#include "lang/scalar.h"
#include "log/output.h"
#include "mem/heap_allocator.h"
#include "os/command_line.h"
#include "cstr/number.h"
#include "physics/height_field.h"
#include "physics/world.h"

using namespace vse;
using namespace vse::physics;

namespace
{

constexpr u32 kSide = 1024;

// Rolling hills from an integer hash: the same samples on every machine.
Vector<u16> MakeSamples()
{
    Vector<u16> samples;
    samples.Reserve(static_cast<usize>(kSide) * kSide);
    for (u32 z = 0; z < kSide; ++z)
    {
        for (u32 x = 0; x < kSide; ++x)
        {
            u32 h = (x / 16) * 73856093u ^ (z / 16) * 19349663u;
            h ^= h >> 13;
            h *= 0x5bd1e995u;
            h ^= h >> 15;
            const u32 ridge = (x * 64u / kSide + z * 64u / kSide) * 400u;
            samples.Push(static_cast<u16>((h % 12000u) + ridge));
        }
    }
    return samples;
}

BodyId Drop(PhysicsWorld& world, const Shape& shape, const Vec3& at)
{
    BodyDesc desc;
    desc.settings.type = BodyType::Dynamic;
    desc.position = at;
    const BodyId id = world.CreateBody(desc);
    Collider c;
    c.shape = shape;
    world.AddCollider(id, c);
    return id;
}

int Usage()
{
    out::PrintError("usage: render_features_demo --headless [--steps N] [--threads N]\n");
    return 2;
}

}  // namespace

int main(int argc, char** argv)
{
    static const os::CommandLineOption kOptions[] = {{"headless", false}, {"steps", true}, {"threads", true}};
    const os::CommandLine<HeapAllocator> line(HeapAllocator::Default(), argc, argv,
                                              Span<const os::CommandLineOption>(kOptions, 3));
    if (!line.Ok() || line.ArgCount() != 0 || !line.HasFlag("headless"))
    {
        return Usage();
    }
    u32 steps = 300;
    u32 threads = 1;
    StringView value;
    if (line.TryGetOption("steps", value))
    {
        const long n = CStrToLong(value.Data());
        if (n < 1 || n > 1000000)
        {
            return Usage();
        }
        steps = static_cast<u32>(n);
    }
    if (line.TryGetOption("threads", value))
    {
        const long n = CStrToLong(value.Data());
        if (n < 1 || n > 64)
        {
            return Usage();
        }
        threads = static_cast<u32>(n);
    }
    const Vector<u16> samples = MakeSamples();
    Validation v = Validation::Ok;
    const HeightFieldConstPtr field = HeightField::CreateFromSamples(Span<const u16>(samples.Data(), samples.Size()),
                                                                     kSide, kSide, 0.0f, 30.0f, 1.0f, &v);
    if (!field)
    {
        out::PrintError("render_features_demo: the height field was refused\n");
        return 1;
    }
    PhysicsWorld world;
    StepConfig config = world.GetStepConfig();
    config.threads = threads;
    world.SetStepConfig(config);
    BodyDesc ground;
    ground.settings.type = BodyType::Static;
    const BodyId floor = world.CreateBody(ground);
    Collider fc;
    fc.shape = Shape::MakeHeightField(world.HeightFields().Add(field), field->Extent());
    world.AddCollider(floor, fc);
    const f32 sx = 100.0f;
    const f32 sz = 60.0f;
    const f32 base = field->HeightAt(sx, sz);
    const BodyId sphere = Drop(world, Shape::MakeSphere(0.5f), Vec3(sx, base + 6.0f, sz));
    const BodyId box = Drop(world, Shape::MakeBox(Vec3(0.5f, 0.5f, 0.5f)),
                            Vec3(sx + 3.0f, field->HeightAt(sx + 3.0f, sz) + 8.0f, sz));
    jobs::Scheduler::Config sc;
    sc.participants = threads;
    sc.pin = false;
    jobs::Scheduler scheduler(sc);
    world.SetScheduler(&scheduler);
    for (u32 i = 0; i < steps; ++i)
    {
        world.Step();
    }
    bool valid = false;
    const u64 hash = world.StateHash(&valid);
    const Vec3 p = world.Find(sphere)->position.Narrow();
    const Vec3 q = world.Find(box)->position.Narrow();
    const bool on_ground = p.y > field->HeightAt(p.x, p.z) - 0.1f && q.y > field->HeightAt(q.x, q.z) - 0.1f;
    out::Print("render_features_demo: %u steps, %u threads, state hash 0x%016llx %s\n", steps, threads,
               static_cast<unsigned long long>(hash), valid && on_ground ? "ok" : "FAILED");
    return valid && on_ground ? 0 : 1;
}
