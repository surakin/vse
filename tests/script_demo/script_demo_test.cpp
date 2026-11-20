// The script demo (Phase 27 Day 475): the whole game (a crowd with Lua behaviors, a player, pickups, a planner that
// searches a field with task.parallel_for) run for a few hundred frames at several thread counts, with the same state
// hash each time, and hot reload of its script in the middle of a run.
#include <catch2/catch_test_macros.hpp>

#include "asset/cpu_loaders.h"
#include "asset/resource_manager.h"
#include "audio/mixer.h"
#include "cstr/cstr.h"
#include "demo_world.h"
#include "io/file_system.h"
#include "jobs/scheduler.h"
#include "sync/thread.h"
#include "temp_dir.h"

using namespace vse;
using namespace script_demo;

namespace
{

jobs::Scheduler::Config ConfigFor(usize participants)
{
    jobs::Scheduler::Config config;
    config.participants = participants;
    config.pin = false;
    return config;
}

DemoOptions SmallOptions()
{
    DemoOptions options;
    options.entities = 300;
    options.partitions = 16;
    options.field_cells = 4096;
    return options;
}

struct Result
{
    u64 hash = 0;
    u64 pickups = 0;
    u64 errors = 0;
    u64 updates = 0;
    Vec3 marker;
};

Result Run(usize threads, u32 frames)
{
    jobs::Scheduler scheduler(ConfigFor(threads));
    DemoWorld world(SmallOptions());
    for (u32 f = 0; f < frames; ++f)
    {
        world.Frame(&scheduler);
    }
    Result r;
    r.hash = world.StateHash();
    r.pickups = world.PickupsCollected();
    r.errors = world.ScriptStats().errors;
    r.updates = world.ScriptStats().updates;
    r.marker = world.MarkerPosition();
    return r;
}

// The script with the wander force changed.
String<HeapAllocator> Edited(const char* from, const char* to)
{
    String<HeapAllocator> text(HeapAllocator::Default(), StringView(BuiltInScript()));
    const StringView whole = text.View();
    const StringView needle(from, CStrLength(from));
    usize at = whole.Length();
    for (usize i = 0; i + needle.Length() <= whole.Length(); ++i)
    {
        if (whole.Substring(i, needle.Length()) == needle)
        {
            at = i;
            break;
        }
    }
    REQUIRE(at != whole.Length());
    String<HeapAllocator> out(HeapAllocator::Default(), whole.Substring(0, at));
    out.Append(StringView(to, CStrLength(to)));
    out.Append(whole.Substring(at + needle.Length(), whole.Length() - at - needle.Length()));
    return out;
}

Result RunWithReload(usize threads, const String<HeapAllocator>& text, bool* applied)
{
    jobs::Scheduler scheduler(ConfigFor(threads));
    DemoWorld world(SmallOptions());
    for (u32 f = 0; f < 40; ++f)
    {
        world.Frame(&scheduler);
    }
    *applied = world.Reload(text.View());
    for (u32 f = 0; f < 40; ++f)
    {
        world.Frame(&scheduler);
    }
    Result r;
    r.hash = world.StateHash();
    r.errors = world.ScriptStats().errors;
    r.updates = world.ScriptStats().updates;
    return r;
}

}  // namespace

TEST_CASE("Script demo: the game runs, and its state hash is the same at 1, 2 and 16 threads",
          "[script_demo][parallel]")
{
    const Result one = Run(1, 150);
    CHECK(one.hash != 0);
    CHECK(one.errors == 0);
    CHECK(one.updates > 150u * 300u);  // every body ran its behavior every frame
    for (usize threads : {usize{2}, usize{16}})
    {
        INFO("threads " << threads);
        const Result r = Run(threads, 150);
        CHECK(r.hash == one.hash);
        CHECK(r.pickups == one.pickups);
        CHECK(r.errors == 0);
    }
}

// The state hash of the small game after 150 frames, taken on the Windows PC (MSVC and clang-cl, Debug and Release;
// Phase 27 Day 473). The scripts use `+ - * /`, `sqrt` and `floor` only and physics is bit-exact by design, so every
// machine must give this value: the cross-machine check of the scripting phase. A machine that differs has found a real
// divergence (KNOWN-ISSUES.md names the machines that have run it).
constexpr u64 kCrossMachineHash = 0xCD4A4433E698FAD7ull;

TEST_CASE("Script demo: the state hash after 150 frames is the same on every machine", "[script_demo][cross_machine]")
{
    const Result r = Run(2, 150);
    CHECK(r.hash == kCrossMachineHash);
}

TEST_CASE("Script demo: the planner's parallel search puts the marker in the world", "[script_demo][tasks]")
{
    const Result r = Run(4, 60);
    CHECK(r.errors == 0);
    CHECK(r.marker.y == 1.5f);  // the planner set it; it starts at the origin
    CHECK((r.marker.x != 0.0f || r.marker.z != 0.0f));
    // The same cells at one thread: the answer does not depend on the thread count.
    const Result one = Run(1, 60);
    CHECK(one.marker.x == r.marker.x);
    CHECK(one.marker.z == r.marker.z);
}

TEST_CASE("Script demo: the crowd reaches the pickups", "[script_demo]")
{
    const Result r = Run(4, 240);
    CHECK(r.errors == 0);
    CHECK(r.pickups > 0);
}

TEST_CASE("Script demo: hot reload changes the running game, and the result is the same at any thread count",
          "[script_demo][reload]")
{
    const String<HeapAllocator> stronger = Edited("d * 6.0", "d * 9.0");
    bool applied_one = false;
    bool applied_many = false;
    const Result one = RunWithReload(1, stronger, &applied_one);
    const Result many = RunWithReload(16, stronger, &applied_many);
    CHECK(applied_one);
    CHECK(applied_many);
    CHECK(one.errors == 0);
    CHECK(one.hash == many.hash);
    // Without the edit the run is different: the new text ran.
    jobs::Scheduler scheduler(ConfigFor(1));
    DemoWorld plain(SmallOptions());
    for (u32 f = 0; f < 80; ++f)
    {
        plain.Frame(&scheduler);
    }
    CHECK(plain.StateHash() != one.hash);
}

TEST_CASE("Script demo: a script that does not compile is refused and the old one keeps running",
          "[script_demo][reload]")
{
    jobs::Scheduler scheduler(ConfigFor(2));
    DemoWorld world(SmallOptions());
    for (u32 f = 0; f < 10; ++f)
    {
        world.Frame(&scheduler);
    }
    const u64 before = world.ScriptStats().updates;
    CHECK_FALSE(world.Reload(StringView("this is not lua (")));
    for (u32 f = 0; f < 10; ++f)
    {
        world.Frame(&scheduler);
    }
    CHECK(world.ScriptStats().updates > before);
    CHECK(world.ScriptStats().errors == 0);
}

namespace
{

// A resource manager over the build's minted script (game/script_demo/minted).
struct MintedScripts
{
    asset::TypeRegistry types;
    UniquePtr<asset::ResourceManager> manager;

    MintedScripts()
    {
        asset::ManagerConfig config;
        config.buffer_pool_bytes = u64{4} << 20;
        manager = MakeUnique<asset::ResourceManager>(types, config);
        asset::RegisterCpuLoaders(manager.Get());
        io::Path dir;
        REQUIRE(io::Path::Parse(StringView(SCRIPT_DEMO_MINTED_DIR), io::NativePathStyle(), &dir));
        REQUIRE(manager->MountPath(StringView("/"), dir, 0).Ok());
    }
};

}  // namespace

TEST_CASE("Script demo: the minted script runs the same game as the built-in copy", "[script_demo][minted]")
{
    MintedScripts minted;
    DemoOptions options = SmallOptions();
    options.minted = minted.manager.Get();
    jobs::Scheduler scheduler(ConfigFor(2));
    DemoWorld world(options);
    REQUIRE(world.Ok());
    for (u32 f = 0; f < 150; ++f)
    {
        world.Frame(&scheduler);
    }
    CHECK(world.ScriptStats().errors == 0);
    CHECK(world.StateHash() == kCrossMachineHash);
}

TEST_CASE("Script demo: editing the script's file reloads it in the running game", "[script_demo][reload][watch]")
{
    io_test::TempDir dir;
    String<HeapAllocator> name(HeapAllocator::Default(), StringView(kDemoScriptKey));
    name.Append(StringView(".lua"));
    const io::Path file = dir.Path().Join(name.View());
    const StringView original(BuiltInScript());
    REQUIRE(io::WriteFile(file, original.Data(), original.Length()).Ok());

    jobs::Scheduler scheduler(ConfigFor(4));
    DemoWorld world(SmallOptions());
    REQUIRE(world.WatchDirectory(dir.Path().View()));
    for (u32 f = 0; f < 40; ++f)
    {
        world.Frame(&scheduler);
        CHECK(world.PollReload() == 0);  // the file holds the running text: nothing to do
    }
    // The edit a user makes in the editor: the wander push is ten times stronger.
    const String<HeapAllocator> edited = Edited("physics.add_force(self.entity, d * 6.0)",
                                                "physics.add_force(self.entity, d * 60.0)");
    REQUIRE(io::WriteFile(file, edited.View().Data(), edited.View().Length()).Ok());
    u32 applied = 0;
    for (u32 tries = 0; tries < 80 && applied == 0; ++tries)
    {
        ThisThread::SleepForMilliseconds(25);
        applied += world.PollReload();
    }
    REQUIRE(applied == 1);
    CHECK(world.FileReloads() == 1);
    for (u32 f = 0; f < 40; ++f)
    {
        world.Frame(&scheduler);
        CHECK(world.PollReload() == 0);  // applied once: the same bytes are not reported again
    }
    CHECK(world.ScriptStats().errors == 0);
    // The file reload is the text reload: the same game as an in-memory reload at the same frame.
    bool in_memory = false;
    const Result expected = RunWithReload(4, edited, &in_memory);
    REQUIRE(in_memory);
    CHECK(world.StateHash() == expected.hash);

    // A text that does not compile is refused and the game runs on.
    const char broken[] = "behavior('wander', { on_update = function(self dt) end })";
    REQUIRE(io::WriteFile(file, broken, sizeof(broken) - 1).Ok());
    for (u32 tries = 0; tries < 20; ++tries)
    {
        ThisThread::SleepForMilliseconds(25);
        CHECK(world.PollReload() == 0);
    }
    world.Frame(&scheduler);
    CHECK(world.ScriptStats().errors == 0);
    CHECK(world.FileReloads() == 1);
}

TEST_CASE("Script demo: the keys drive the player and a pickup chimes when it walks in", "[script_demo][media]")
{
    audio::Mixer mixer;
    audio::MixerConfig mix;
    mix.layout = audio::ChannelLayout::Mono;
    mix.max_voices = 64;
    REQUIRE(mixer.Init(mix) == audio::MixerStatus::Ok);
    DemoOptions options = SmallOptions();
    options.input = true;
    options.mixer = &mixer;
    options.sample_rate = mix.sample_rate;
    jobs::Scheduler scheduler(ConfigFor(4));
    DemoWorld world(options);
    Vec3 start;
    world.ForEachVisual(
        [&](VisualKind kind, const Vec3& p)
        {
            if (kind == VisualKind::Player)
            {
                start = p;
            }
        });
    // W held for two seconds: the player walks toward -z along x = -8, through the pickups at z = 3, -3 and -9.
    os::InputState keys;
    keys.keyboard.SetDown(os::Key::W, true);
    Array<f32, audio::kBlockFrames> block;
    for (u32 f = 0; f < 120; ++f)
    {
        world.Frame(&scheduler, &keys);
        mixer.RenderBlock(Span<f32>(block.Data(), block.Size()));
    }
    Vec3 end;
    world.ForEachVisual(
        [&](VisualKind kind, const Vec3& p)
        {
            if (kind == VisualKind::Player)
            {
                end = p;
            }
        });
    CHECK(end.z < start.z - 15.0f);  // 10 m/s for 2 s, clamped to the floor
    CHECK(end.x == start.x);
    CHECK(world.ScriptStats().errors == 0);
    const audio::MixerStats stats = mixer.Stats();
    INFO("commands " << stats.commands_applied << ", finished " << stats.voices_finished);
    CHECK(stats.commands_applied >= 2);  // at least two chimes started
    // The same keys at another thread count give the same game.
    audio::Mixer mixer2;
    REQUIRE(mixer2.Init(mix) == audio::MixerStatus::Ok);
    options.mixer = &mixer2;
    jobs::Scheduler one(ConfigFor(1));
    DemoWorld again(options);
    for (u32 f = 0; f < 120; ++f)
    {
        again.Frame(&one, &keys);
        mixer2.RenderBlock(Span<f32>(block.Data(), block.Size()));
    }
    CHECK(again.StateHash() == world.StateHash());
}
