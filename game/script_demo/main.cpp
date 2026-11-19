// script_demo: a game written in Lua that uses every core (Phase 27 Day 475). A crowd of bodies wander, flock and flee,
// a player walks a square, pickups count who touches them, and a planner searches a field with `task.parallel_for`. It
// runs headless and prints, every N frames, the script time per frame, the physics time, the pickups and the state
// hash; the hash is the same whatever --threads says. See README.md.
//
//   script_demo [--threads N]     worker threads (default: the machine's; 1 runs on the calling thread)
//               [--partitions P]  script partitions (default 16)
//               [--entities N]    bodies with a behavior (default 2000)
//               [--frames N]      frames to run (default 600)
//               [--report N]      print a line every N frames (default 100)
#include "cstr/cstr.h"
#include "cstr/number.h"
#include "debug/program_setup.h"
#include "demo_world.h"
#include "jobs/scheduler.h"
#include "os/command_line.h"
#include "os/console.h"
#include "os/os.h"

using namespace engine_placeholder;

namespace
{

// Reads `--name N`; returns false (after saying so) when it is not a number from 1 to `max`.
bool Count(const os::CommandLine<HeapAllocator>& line, const char* name, long fallback, long max, long* out)
{
    *out = fallback;
    StringView value;
    if (!line.TryGetOption(StringView(name, CStrLength(name)), value))
    {
        return true;
    }
    const long parsed = CStrToLong(value.Data());
    if (parsed < 1 || parsed > max)
    {
        os::PrintError("script_demo: --%s needs a number from 1 to %ld\n", name, max);
        return false;
    }
    *out = parsed;
    return true;
}

}  // namespace

int main(int argc, char** argv)
{
    static const os::CommandLineOption kCliOptions[] = {
        {"threads", true}, {"partitions", true},       {"entities", true},     {"frames", true},
        {"report", true},  debug::kCrashHandlerOption, debug::kCrashTestOption};
    const os::CommandLine<HeapAllocator> line(HeapAllocator::Default(), argc, argv,
                                              Span<const os::CommandLineOption>(kCliOptions, 7));
    if (!line.Ok() || line.ArgCount() != 0)
    {
        os::PrintError("usage: %s [--threads N] [--partitions P] [--entities N] [--frames N] [--report N]\n",
                       line.ProgramName().Data());
        return 2;
    }
    long threads = 0, partitions = 0, entities = 0, frames = 0, report = 0;
    if (!Count(line, "threads", static_cast<long>(jobs::DefaultParticipantCount()), 256, &threads) ||
        !Count(line, "partitions", 16, 1024, &partitions) || !Count(line, "entities", 2000, 200000, &entities) ||
        !Count(line, "frames", 600, 10000000, &frames) || !Count(line, "report", 100, 10000000, &report))
    {
        return 2;
    }
    script_demo::DemoOptions demo;
    demo.entities = static_cast<u32>(entities);
    demo.partitions = static_cast<u32>(partitions);
    jobs::Scheduler::Config config;
    config.participants = static_cast<usize>(threads);
    config.pin = false;
    jobs::Scheduler scheduler(config);
    script_demo::DemoWorld world(demo);
    os::Print("script_demo: %u bodies, %u partitions, %zu threads\n", world.EntityCount(), demo.partitions,
              static_cast<size_t>(scheduler.ParticipantCount()));
    for (long f = 1; f <= frames; ++f)
    {
        world.Frame(&scheduler);
        if (f % report == 0 || f == frames)
        {
            os::Print("frame %ld: scripts %.2f ms, physics %.2f ms, pickups %llu, errors %llu, hash %016llx\n", f,
                      world.LastScriptSeconds() * 1000.0, world.LastPhysicsSeconds() * 1000.0,
                      static_cast<unsigned long long>(world.PickupsCollected()),
                      static_cast<unsigned long long>(world.ScriptStats().errors),
                      static_cast<unsigned long long>(world.StateHash()));
        }
    }
    return world.ScriptStats().errors == 0 ? 0 : 1;
}
