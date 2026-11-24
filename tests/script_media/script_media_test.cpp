// The audio and input bindings (Phase 27 Day 468): a script plays and controls sounds through the command buffer, the
// merge posts the commands to the mixer in a fixed order (so the rendered blocks hash the same at any thread count), a
// handle is an error value once its voice is stopped or over, the voice's end reaches the script as an event, and
// scripts read one input snapshot taken at the tick start (which a replay can record and feed back).
#include <catch2/catch_test_macros.hpp>

#include "algorithm.h"
#include "audio/block_hash.h"
#include "audio/components.h"
#include "audio/music.h"
#include "audio/stream.h"
#include "cstr/cstr.h"
#include "ecs/transform.h"
#include "jobs/scheduler.h"
#include "lang/initializer_list.h"
#include "lang/scalar.h"
#include "script/backend_registry.h"
#include "script/bindings.h"
#include "script/command_buffer.h"
#include "script/component_bindings.h"
#include "script/component_catalog.h"
#include "script/entity_bindings.h"
#include "script/luajit/luajit_backend.h"
#include "script/merge.h"
#include "script/merge_services.h"
#include "script/partition_context.h"
#include "script/script_runtime.h"
#include "script_media/audio_bindings.h"
#include "script_media/input_bindings.h"
#include "script_media/replay_log.h"
#include "storage/builtin_components.h"

using namespace vse;
using namespace vse::script;
using namespace vse::script_media;

// A named namespace: the test files of one unity build must not share names through anonymous namespaces.
namespace media_test
{

struct Probe
{
    f32 x = 0.0f;
    f32 y = 0.0f;
    i32 n = 0;
    i32 ended = 0;
};

constexpr u32 kProbeId = 2400;

StringView Text(const char* text)
{
    return StringView(text, CStrLength(text));
}

// A clip of `frames` mono frames of a constant `level`, 48 kHz.
audio::AudioClipPtr MakeClip(u32 frames, f32 level)
{
    Vector<f32> samples;
    for (u32 i = 0; i < frames; ++i)
    {
        samples.Push(level);
    }
    audio::ClipDesc desc;
    desc.format.sample_rate = 48000;
    desc.format.layout = audio::ChannelLayout::Mono;
    desc.format.format = audio::SampleFormat::F32;
    desc.frame_count = frames;
    audio::AudioClipPtr clip;
    REQUIRE(audio::AudioClip::CreateCopy(desc, Span<const f32>(samples.Data(), samples.Size()), "test", &clip) ==
            audio::ClipStatus::Ok);
    return clip;
}

// The host's clip table.
struct Clips
{
    audio::AudioClipPtr beep = MakeClip(768, 0.5f);  // three blocks
    audio::AudioClipPtr loud = MakeClip(2048, 0.25f);

    static audio::AudioClipConstPtr Resolve(void* user, StringView name)
    {
        Clips* self = static_cast<Clips*>(user);
        if (name == StringView("beep"))
        {
            return self->beep;
        }
        if (name == StringView("loud"))
        {
            return self->loud;
        }
        return audio::AudioClipConstPtr();
    }
};

audio::MixerConfig MixerSetup()
{
    audio::MixerConfig config;
    config.layout = audio::ChannelLayout::Mono;
    config.max_voices = 1024;  // a tick that stops a voice and starts another needs both until the next block
    config.ramp_frames = 0;
    config.lead_blocks = 0;
    return config;
}

// One block of output, hashed into `hash` and returned in `peak` (the largest sample).
void RenderBlock(audio::Mixer& mixer, audio::RenderHash* hash, f32* peak = nullptr)
{
    f32 block[audio::kBlockFrames];
    mixer.RenderBlock(Span<f32>(block, audio::kBlockFrames));
    if (hash != nullptr)
    {
        hash->AddBlock(Span<const f32>(block, audio::kBlockFrames));
    }
    if (peak != nullptr)
    {
        *peak = 0.0f;
        for (f32 s : block)
        {
            *peak = Max(*peak, Abs(s));
        }
    }
}

// ------------------------------------------------------------------ one state

struct StateRig
{
    storage::ComponentRegistry registry;
    ComponentCatalog catalog;
    BindingRegistry bindings;
    UniquePtr<Backend> backend = luajit::CreateLuaJitBackend();
    UniquePtr<State> state = backend->CreateState(0);
    UniquePtr<Installation> installed;
    ecs::World world;
    audio::Mixer mixer;
    Clips clips;
    VoiceTable voices;
    AudioScriptServices audio;
    InputMap map;
    InputSnapshot snapshot;
    InputScriptServices input;
    CommandKindTable kinds;
    CommandBuffer buffer;
    Overlay overlay;
    PartitionContext context;
    MergeServices merge_services;
    ecs::Entity self;

    StateRig() : catalog(&registry)
    {
        REQUIRE(storage::RegisterBuiltinComponents(&registry).Ok());
        REQUIRE(RegisterAudioComponents(&registry));
        RegisterAudioValidators(&catalog);
        REQUIRE(RegisterStandardBindings(&bindings));
        REQUIRE(RegisterAudioBindings(&bindings));
        REQUIRE(RegisterInputBindings(&bindings));
        Outcome error;
        installed = bindings.Install(state.Get(), InstallProfile::Partition(ContextEntityCheck()), &error);
        REQUIRE(installed.Get() != nullptr);
        REQUIRE(RegisterEntityCommandKinds(&kinds));
        REQUIRE(RegisterComponentCommandKinds(&kinds));
        REQUIRE(RegisterAudioCommandKinds(&kinds));
        REQUIRE(mixer.Init(MixerSetup()) == audio::MixerStatus::Ok);
        audio::BusId music;
        REQUIRE(mixer.CreateBus("sfx", audio::kMasterBus, &music) == audio::MixerStatus::Ok);
        audio.mixer = &mixer;
        audio.clips.resolve = &Clips::Resolve;
        audio.clips.user = &clips;
        audio.voices = &voices;
        input.snapshot = &snapshot;
        merge_services.components = &catalog;
        merge_services.extensions[kServiceAudio] = &audio;
        merge_services.extensions[kServiceInput] = &input;
        buffer.Begin(0);
        self = ecs::CreateTransformEntity(world, ecs::LocalTransform());
        context.world = &world;
        context.commands = &buffer;
        context.overlay = &overlay;
        context.components = &catalog;
        context.current_entity_key = self.ToU64();
        context.extensions[kServiceAudio] = &audio;
        context.extensions[kServiceInput] = &input;
        context.tick = 1;
        catalog.Freeze();
    }

    Outcome Run(const char* body, CallOutput* out, const Value* args = nullptr, usize count = 0)
    {
        String<HeapAllocator> source(HeapAllocator::Default());
        source.Append("function run(a, b, c) ");
        source.Append(StringView(body));
        source.Append(" end");
        REQUIRE(state->Load("media_test", source.View()).IsOk());
        ScopedPartitionContext scope(&context);
        return state->Call("run", Span<const Value>(args, count), Budget(), out);
    }

    // Runs `body`, applies what it recorded and starts the next tick.
    Outcome RunAndMerge(const char* body, CallOutput* out, i64 handle = 0)
    {
        const Value args[1] = {Value::FromInt(handle)};
        Outcome outcome = Run(body, out, args, 1);
        if (outcome.IsOk())
        {
            CommandBuffer* buffers[] = {&buffer};
            Merge(&world, Span<CommandBuffer* const>(buffers, 1), kinds, ConflictPolicy::LastWins, &merge_services);
        }
        buffer.Begin(0);
        context.tick += 1;
        return outcome;
    }
};

}  // namespace media_test

using namespace media_test;

TEST_CASE("Script media: audio.play returns a handle and the merge starts the voice", "[script_media][audio]")
{
    StateRig rig;
    CallOutput out;
    REQUIRE(rig.RunAndMerge("return audio.play('beep', {gain = 0.5})", &out).IsOk());
    REQUIRE(out.results.Size() == 1);
    const i64 handle = out.results[0].AsInt();
    CHECK(handle > 0);
    REQUIRE(rig.voices.Size() == 1);
    const VoiceTable::Entry* entry = rig.voices.Find(static_cast<u64>(handle));
    REQUIRE(entry != nullptr);
    CHECK(entry->owner_key == rig.self.ToU64());
    CHECK(rig.mixer.IsPlaying(entry->voice));
    f32 peak = 0.0f;
    RenderBlock(rig.mixer, nullptr, &peak);
    CHECK(Abs(peak - 0.25f) < 1.0e-4f);  // a constant 0.5 clip at gain 0.5
    CHECK(rig.voices.HandleOf(entry->voice) == static_cast<u64>(handle));
}

TEST_CASE("Script media: gain, pitch and a bus are options, and a bad option is an error", "[script_media][audio]")
{
    StateRig rig;
    CallOutput out;
    REQUIRE(
        rig.RunAndMerge("return audio.play('beep', {bus = 'sfx', loop = true, priority = 3, pitch = 1})", &out).IsOk());
    const i64 live = out.results[0].AsInt();
    struct Case
    {
        const char* body;
        const char* reason;
    };
    const Case cases[] = {
        {"return audio.play('nope', nil)", "no clip is called"},
        {"return audio.play('', nil)", "empty or too long"},
        {"return audio.play('beep', 5)", "a table"},
        {"return audio.play('beep', {volume = 1})", "unknown option `volume`"},
        {"return audio.play('beep', {gain = 'loud'})", "must be a number"},
        {"return audio.play('beep', {gain = 99})", "out of range"},
        {"return audio.play('beep', {pitch = 0})", "out of range"},
        {"return audio.play('beep', {pan = 2})", "out of range"},
        {"return audio.play('beep', {loop = 1})", "true or false"},
        {"return audio.play('beep', {bus = 'void'})", "no bus is called"},
        {"return audio.play('beep', {bus = 4})", "a bus name"},
        {"audio.set_bus_gain('void', 1)", "no bus is called"},
        {"audio.set_bus_gain('sfx', -1)", "out of range"},
        {"audio.stop(0, nil)", "not a voice handle"},
        {"audio.stop(a, -1)", "from 0 to 600 seconds"},
        {"audio.stop(a, 'x')", "number of seconds"},
        {"audio.play_music('theme', nil)", "no music player"},
    };
    for (const Case& c : cases)
    {
        INFO(c.body);
        CallOutput bad;
        const Outcome outcome = rig.RunAndMerge(c.body, &bad,
                                                StringView(c.body) == StringView("audio.stop(0, nil)") ? 0 : live);
        REQUIRE_FALSE(outcome.IsOk());
        INFO(outcome.message.CStr());
        CHECK(outcome.message.View().Contains(Text(c.reason)));
    }
    CHECK(rig.voices.Size() == 1);  // only the good play made a voice
}

TEST_CASE("Script media: a stopped or finished voice's handle is an error value", "[script_media][audio]")
{
    StateRig rig;
    CallOutput out;
    REQUIRE(rig.RunAndMerge("return audio.play('beep', nil)", &out).IsOk());
    const i64 handle = out.results[0].AsInt();
    // The handle can be used in a later tick while the voice plays.
    CallOutput read;
    REQUIRE(rig.RunAndMerge("return audio.is_playing(a)", &read, handle).IsOk());
    CHECK(read.results[0].AsBool());
    REQUIRE(
        rig.RunAndMerge("audio.set_gain(a, 0.25) audio.set_pitch(a, 1) audio.pause(a) audio.resume(a)", &read, handle)
            .IsOk());
    f32 peak = 0.0f;
    RenderBlock(rig.mixer, nullptr, &peak);
    CHECK(Abs(peak - 0.125f) < 1.0e-4f);
    REQUIRE(rig.RunAndMerge("audio.stop(a, 0)", &read, handle).IsOk());
    // Stopped: using it again is an error, and is_playing says no.
    const Outcome again = rig.RunAndMerge("audio.set_gain(a, 1)", &read, handle);
    REQUIRE_FALSE(again.IsOk());
    CHECK(again.message.View().Contains(Text("was stopped")));
    REQUIRE(rig.RunAndMerge("return audio.is_playing(a)", &read, handle).IsOk());
    CHECK_FALSE(read.results[0].AsBool());
    // A voice that ran off its end: the pump forgets it, and the handle is an error.
    CallOutput second;
    REQUIRE(rig.RunAndMerge("return audio.play('beep', nil)", &second).IsOk());
    const i64 other = second.results[0].AsInt();
    for (u32 i = 0; i < 5; ++i)
    {
        RenderBlock(rig.mixer, nullptr);
    }
    ScriptRuntimeConfig config;
    config.backend = rig.backend.Get();
    config.bindings = &rig.bindings;
    config.components = &rig.catalog;
    ScriptRuntime runtime(config);
    const AudioEventReport report = PumpAudioEvents(&rig.mixer, &rig.voices, &runtime);
    CHECK(report.polled >= 1);
    CHECK(rig.voices.Find(static_cast<u64>(other)) == nullptr);
    const Outcome gone = rig.RunAndMerge("audio.set_gain(a, 1)", &read, other);
    REQUIRE_FALSE(gone.IsOk());
    CHECK(gone.message.View().Contains(Text("has ended")));
}

namespace media_test
{

audio::StreamBufferPtr MakeStream(const audio::AudioClipPtr& clip)
{
    UniquePtr<audio::StreamSource> source;
    REQUIRE(audio::MakeMemoryStream(clip, &source) == audio::StreamStatus::Ok);
    audio::StreamConfig config;
    config.capacity_frames = 4096;
    config.chunk_frames = 1024;
    config.low_water_frames = 2048;
    audio::StreamBufferPtr buffer;
    REQUIRE(audio::StreamBuffer::Create(Move(source), config, &buffer) == audio::StreamStatus::Ok);
    buffer->Refill();
    return buffer;
}

struct Streams
{
    audio::StreamBufferPtr theme = MakeStream(MakeClip(8192, 0.5f));
    audio::StreamBufferPtr battle = MakeStream(MakeClip(8192, 0.25f));

    static audio::StreamBufferPtr Resolve(void* user, StringView name)
    {
        Streams* self = static_cast<Streams*>(user);
        return name == StringView("theme")    ? self->theme
               : name == StringView("battle") ? self->battle
                                              : audio::StreamBufferPtr();
    }
};

}  // namespace media_test

TEST_CASE("Script media: a handle survives the trip through a Lua number at any tick", "[script_media][audio]")
{
    // Scripts hold numbers as doubles, which keep integers exactly up to 2^53: a handle made late in a long session
    // must still name its voice after the round trip.
    const u64 ticks[] = {1, 1000000, 2147483646, 5000000000ull};
    for (u64 tick : ticks)
    {
        INFO("tick " << tick);
        StateRig rig;
        rig.context.tick = tick;
        CallOutput out;
        REQUIRE(rig.RunAndMerge("return audio.play('beep', {loop = true})", &out).IsOk());
        const i64 handle = out.results[0].AsInt();
        CHECK(handle > 0);
        CHECK(handle < (i64{1} << 53));
        REQUIRE(rig.voices.Size() == 1);
        CHECK(rig.voices.Find(static_cast<u64>(handle)) != nullptr);
        CallOutput read;
        REQUIRE(rig.RunAndMerge("return audio.is_playing(a)", &read, handle).IsOk());
        CHECK(read.results[0].AsBool());
    }
}

TEST_CASE("Script media: music plays, crossfades and stops through the player", "[script_media][audio][music]")
{
    StateRig rig;
    Streams streams;
    audio::MusicPlayer player(&rig.mixer, audio::kMasterBus);
    CallOutput out;
    // No player yet, no stream table: refused at the call.
    CHECK_FALSE(rig.RunAndMerge("audio.play_music('theme', nil)", &out).IsOk());
    rig.audio.music = &player;
    rig.audio.streams.resolve = &Streams::Resolve;
    rig.audio.streams.user = &streams;
    const Outcome unknown = rig.RunAndMerge("audio.play_music('lullaby', nil)", &out);
    REQUIRE_FALSE(unknown.IsOk());
    CHECK(unknown.message.View().Contains(Text("no music stream is called")));
    const Outcome slow = rig.RunAndMerge("audio.crossfade_music('theme', 9999)", &out);
    REQUIRE_FALSE(slow.IsOk());
    CHECK(slow.message.View().Contains(Text("0 to 600 seconds")));
    REQUIRE(rig.RunAndMerge("audio.play_music('theme', nil)", &out).IsOk());
    f32 peak = 0.0f;
    for (u32 i = 0; i < 4; ++i)
    {
        player.Update();
        RenderBlock(rig.mixer, nullptr, &peak);
    }
    CHECK(Abs(peak - 0.5f) < 1.0e-3f);  // the theme, no fade
    REQUIRE(rig.RunAndMerge("audio.crossfade_music('battle', 0.01)", &out).IsOk());
    for (u32 i = 0; i < 8; ++i)
    {
        player.Update();
        RenderBlock(rig.mixer, nullptr, &peak);
    }
    CHECK(Abs(peak - 0.25f) < 1.0e-3f);  // the battle theme, after 480 frames of crossfade
    REQUIRE(rig.RunAndMerge("audio.stop_music(0)", &out).IsOk());
    for (u32 i = 0; i < 3; ++i)
    {
        player.Update();
        RenderBlock(rig.mixer, nullptr, &peak);
    }
    CHECK(peak == 0.0f);
}

TEST_CASE("Script media: the listener component is registered and checked", "[script_media][audio]")
{
    StateRig rig;
    CallOutput out;
    REQUIRE(rig.RunAndMerge("component.add(a, 'audio_listener', {gain = 2, active = true})", &out,
                            static_cast<i64>(0))
                .IsOk() == false);  // `a` is a number here: the call is an error
    const ecs::Entity e = rig.self;
    const Value args[1] = {Value::FromEntity(e)};
    REQUIRE(rig.Run("component.add(a, 'audio_listener', {gain = 2, active = true})", &out, args, 1).IsOk());
    CommandBuffer* buffers[] = {&rig.buffer};
    Merge(&rig.world, Span<CommandBuffer* const>(buffers, 1), rig.kinds, ConflictPolicy::LastWins, &rig.merge_services);
    rig.buffer.Begin(0);
    const audio::AudioListener* listener = rig.world.Get<audio::AudioListener>(e);
    REQUIRE(listener != nullptr);
    CHECK(listener->gain == 2.0f);
    const Outcome loud = rig.Run("component.set(a, 'audio_listener', {gain = 9})", &out, args, 1);
    REQUIRE_FALSE(loud.IsOk());
    CHECK(loud.message.View().Contains(Text("gain")));
}

TEST_CASE("Script media: a world without audio or input says so", "[script_media][audio]")
{
    StateRig rig;
    rig.context.extensions[kServiceAudio] = nullptr;
    rig.context.extensions[kServiceInput] = nullptr;
    CallOutput out;
    const Outcome audio = rig.Run("return audio.play('beep', nil)", &out);
    REQUIRE_FALSE(audio.IsOk());
    CHECK(audio.message.View().Contains(Text("no audio")));
    const Outcome input = rig.Run("return input.key_down('a')", &out);
    REQUIRE_FALSE(input.IsOk());
    CHECK(input.message.View().Contains(Text("no input")));
}

// ------------------------------------------------------------------ input

namespace media_test
{

os::InputState Keys(InitializerList<os::Key> down)
{
    os::InputState state;
    for (os::Key key : down)
    {
        state.keyboard.SetDown(key, true);
    }
    return state;
}

void FillMap(InputMap* map)
{
    const InputBinding space[] = {InputBinding::OfKey(os::Key::Space)};
    const InputBinding fire[] = {InputBinding::OfMouse(os::kMouseLeft), InputBinding::OfKey(os::Key::Enter)};
    REQUIRE(map->AddAction("jump", Span<const InputBinding>(space, 1)));
    REQUIRE(map->AddAction("fire", Span<const InputBinding>(fire, 2)));
    const InputBinding left[] = {InputBinding::OfKey(os::Key::A), InputBinding::OfKey(os::Key::Left)};
    const InputBinding right[] = {InputBinding::OfKey(os::Key::D), InputBinding::OfKey(os::Key::Right)};
    REQUIRE(map->AddAxis("horizontal", Span<const InputBinding>(left, 2), Span<const InputBinding>(right, 2)));
}

}  // namespace media_test

TEST_CASE("Script media: scripts read keys, edges, the mouse, actions, axes and text from the snapshot",
          "[script_media][input]")
{
    StateRig rig;
    FillMap(&rig.map);
    os::InputState state = Keys({os::Key::W});
    state.mouse_x = 100.0;
    state.mouse_y = 50.0;
    state.pointer_inside = true;
    rig.snapshot.Capture(1, state, StringView(), rig.map);
    CallOutput out;
    REQUIRE(rig.Run("return input.key_down('w'), input.key_pressed('w'), input.key_released('w'), "
                    "input.key_down('s'), input.action('jump'), input.axis('horizontal'), input.pointer_inside()",
                    &out)
                .IsOk());
    REQUIRE(out.results.Size() == 7);
    CHECK(out.results[0].AsBool());
    CHECK(out.results[1].AsBool());
    CHECK_FALSE(out.results[2].AsBool());
    CHECK_FALSE(out.results[3].AsBool());
    CHECK_FALSE(out.results[4].AsBool());
    CHECK(out.results[5].ToDouble() == 0.0);
    CHECK(out.results[6].AsBool());
    // The next tick: W is up, Space and D are down, the mouse moved, the left button went down, text was typed.
    state = Keys({os::Key::Space, os::Key::D, os::Key::LeftShift});
    state.keyboard.modifiers = os::kModShift;
    state.mouse_x = 110.0;
    state.mouse_y = 45.0;
    state.mouse_buttons = os::kMouseLeft;
    state.scroll_dy = -20.0;
    rig.snapshot.Capture(2, state, Text("hi"), rig.map);
    REQUIRE(rig.Run("local d = input.mouse_delta() local p = input.mouse_position() local s = input.scroll() "
                    "return input.key_released('w'), input.action_pressed('jump'), input.action_released('jump'), "
                    "input.axis('horizontal'), input.mouse_down('left'), input.mouse_pressed('left'), "
                    "input.mouse_released('right'), d.x, d.y, p.x, s.y, input.text(), input.modifier('shift'), "
                    "input.modifier('ctrl'), input.action('fire')",
                    &out)
                .IsOk());
    REQUIRE(out.results.Size() == 15);
    CHECK(out.results[0].AsBool());
    CHECK(out.results[1].AsBool());
    CHECK_FALSE(out.results[2].AsBool());
    CHECK(out.results[3].ToDouble() == 1.0);
    CHECK(out.results[4].AsBool());
    CHECK(out.results[5].AsBool());
    CHECK_FALSE(out.results[6].AsBool());
    CHECK(out.results[7].ToDouble() == 10.0);
    CHECK(out.results[8].ToDouble() == -5.0);
    CHECK(out.results[9].ToDouble() == 110.0);
    CHECK(out.results[10].ToDouble() == -20.0);
    CHECK(out.results[11].Text() == Text("hi"));
    CHECK(out.results[12].AsBool());
    CHECK_FALSE(out.results[13].AsBool());
    CHECK(out.results[14].AsBool());  // the left mouse button fires
    // Unknown names are errors.
    const char* const bad[] = {"return input.key_down('hyper')", "return input.mouse_down('thumb')",
                               "return input.axis('vertical')", "return input.action('duck')",
                               "return input.modifier('fn')"};
    for (const char* body : bad)
    {
        INFO(body);
        CHECK_FALSE(rig.Run(body, &out).IsOk());
    }
}

TEST_CASE("Script media: a host axis value adds to the axis and a map refuses bad names", "[script_media][input]")
{
    InputMap map;
    FillMap(&map);
    const InputBinding one[] = {InputBinding::OfKey(os::Key::A)};
    CHECK_FALSE(map.AddAction("jump", Span<const InputBinding>(one, 1)));  // taken
    CHECK_FALSE(map.AddAction("", Span<const InputBinding>(one, 1)));
    CHECK_FALSE(map.AddAction("none", Span<const InputBinding>()));
    CHECK_FALSE(map.AddAxis("vertical", Span<const InputBinding>(), Span<const InputBinding>()));
    const InputBinding unknown[] = {InputBinding::OfKey(os::Key::Unknown)};
    CHECK_FALSE(map.AddAction("bad", Span<const InputBinding>(unknown, 1)));
    InputSnapshot snapshot;
    const ExternalAxis stick[] = {{StringView("horizontal"), 0.5f}, {StringView("look"), -2.0f}};
    snapshot.Capture(1, Keys({os::Key::D}), StringView(), map, Span<const ExternalAxis>(stick, 2));
    f32 value = 0.0f;
    CHECK(snapshot.Axis(StringView("horizontal"), &value));
    CHECK(value == 1.0f);  // 1 + 0.5 clamped
    CHECK(snapshot.Axis(StringView("look"), &value));
    CHECK(value == -1.0f);
    CHECK_FALSE(snapshot.Axis(StringView("nothing"), &value));
}

TEST_CASE("Script media: text over the limit is cut between characters", "[script_media][input]")
{
    InputMap map;
    String<HeapAllocator> typed(HeapAllocator::Default());
    for (u32 i = 0; i < kMaxInputTextBytes - 1; ++i)
    {
        typed.Append('a');
    }
    typed.Append(StringView("\xC3\xA9"));  // the two bytes of one letter straddle the limit
    InputSnapshot snapshot;
    snapshot.Capture(1, os::InputState(), typed.View(), map);
    CHECK(snapshot.Text().Length() == kMaxInputTextBytes - 1);
    String<HeapAllocator> short_text(HeapAllocator::Default());
    short_text.Append(StringView("\xC3\xA9"));
    snapshot.Capture(2, os::InputState(), short_text.View(), map);
    CHECK(snapshot.Text().Length() == 2);
}

TEST_CASE("Script media: a snapshot round-trips through its record and a bad record is refused",
          "[script_media][input]")
{
    InputMap map;
    FillMap(&map);
    InputSnapshot first;
    os::InputState state = Keys({os::Key::Space, os::Key::D});
    state.mouse_x = 12.5;
    state.mouse_buttons = os::kMouseLeft | os::kMouseRight;
    first.Capture(7, Keys({os::Key::A}), StringView(), map);
    first.Capture(8, state, Text("typed"), map);
    bytes::ByteWriter writer;
    first.Serialize(&writer);
    InputSnapshot back;
    bytes::ByteReader reader(writer.Bytes());
    REQUIRE(back.Deserialize(&reader));
    CHECK(reader.AtEnd());
    CHECK(back.Equals(first));
    CHECK(back.Tick() == 8);
    // Truncations and a wrong version fail and leave an empty snapshot.
    for (usize cut = 0; cut < writer.Size(); cut += 7)
    {
        bytes::ByteReader shortened(ByteSpan(writer.Bytes().Data(), cut));
        InputSnapshot partial;
        CHECK_FALSE(partial.Deserialize(&shortened));
    }
    Vector<byte> wrong;
    for (usize i = 0; i < writer.Size(); ++i)
    {
        wrong.Push(writer.Bytes()[i]);
    }
    wrong[0] = static_cast<byte>(99);
    bytes::ByteReader versioned(ByteSpan(wrong.Data(), wrong.Size()));
    InputSnapshot refused;
    CHECK_FALSE(refused.Deserialize(&versioned));
}

// ------------------------------------------------------------------ the whole runtime

namespace media_test
{

const char* const kScripts = R"LUA(
-- Starts a sound every few ticks, adjusts it, stops it, and counts the ends it is told about.
behavior('player', {
  on_create = function(self)
    self.last = nil
    self.age = 0
    event.on('on_voice_end', function(me, e)
      me.last = nil
      me.ended = (me.ended or 0) + 1
      component.set(me.entity, 'probe', { ended = me.ended })
    end)
  end,
  on_update = function(self, dt)
    self.age = self.age + 1
    if self.last then
      audio.set_gain(self.last, 0.05 + random.float() * 0.2)
      if self.age % 5 == 0 then
        audio.stop(self.last, 0)
        self.last = nil
      end
    end
    if not self.last then
      self.last = audio.play(random.float() < 0.5 and 'beep' or 'loud', { gain = 0.01 + random.float() * 0.1,
                                                                       pitch = 0.5 + random.float() })
    end
  end,
})
-- Placed sounds: one at a point, one on the script's own entity, and the refused forms.
behavior('placer', {
  on_create = function(self)
    event.on('on_voice_end', function(me, e)
      me.ended = (me.ended or 0) + 1
      component.set(me.entity, 'probe', { ended = me.ended, n = (e.reason == 'finished') and 1 or 2 })
    end)
  end,
  on_update = function(self, dt)
    self.age = (self.age or 0) + 1
    if self.age == 1 then self.h = audio.play('loud', { position = vec3.new(6, 0, 0) }) end
  end,
})
behavior('attacher', {
  on_create = function(self)
    event.on('on_voice_end', function(me, e)
      me.ended = (me.ended or 0) + 1
      component.set(me.entity, 'probe', { ended = me.ended, n = (e.reason == 'stopped') and 1 or 2 })
    end)
  end,
  on_update = function(self, dt)
    self.age = (self.age or 0) + 1
    if self.age == 1 then self.h = audio.play('loud', { entity = self.entity, loop = true }) end
    if self.age == 10 then audio.set_gain(self.h, 0.5) end
    if self.age == 14 then audio.stop(self.h, nil) end
  end,
})
behavior('bad_both', { on_update = function(self, dt) audio.play('loud', { position = vec3.new(1, 0, 0), entity = self.entity }) end })
behavior('bad_pan', { on_update = function(self, dt) audio.play('loud', { position = vec3.new(1, 0, 0), pan = 0.5 }) end })
behavior('bad_pause', {
  on_update = function(self, dt)
    self.age = (self.age or 0) + 1
    if self.age == 1 then self.h = audio.play('loud', { position = vec3.new(1, 0, 0), loop = true }) end
    if self.age == 3 then audio.pause(self.h) end
  end,
})
-- Reads the snapshot every tick and writes what it saw.
behavior('reader', {
  on_update = function(self, dt)
    local p = component.get(self.entity, 'probe')
    local x = p.x + input.axis('horizontal') * 2
    if input.key_down('w') then x = x + 1 end
    local n = p.n
    if input.action_pressed('jump') then n = n + 1 end
    local d = input.mouse_delta()
    component.set(self.entity, 'probe', { x = x, y = p.y + d.x - d.y, n = n })
  end,
})
-- Counts the input events it hears.
behavior('listener', {
  on_create = function(self)
    self.heard = 0
    event.on('on_input', function(me, e)
      me.heard = me.heard + 1
      component.set(me.entity, 'probe', { n = me.heard })
    end)
  end,
})
)LUA";

jobs::Scheduler::Config ConfigFor(usize participants)
{
    jobs::Scheduler::Config config;
    config.participants = participants;
    config.pin = false;
    return config;
}

struct RuntimeRig
{
    storage::ComponentRegistry registry;
    ComponentCatalog catalog;
    BindingRegistry bindings;
    UniquePtr<Backend> backend = luajit::CreateLuaJitBackend();
    ecs::World world;
    audio::Mixer mixer;
    Clips clips;
    VoiceTable voices;
    AudioScriptServices audio;
    InputMap map;
    InputSnapshot snapshot;
    InputScriptServices input;
    UniquePtr<ScriptRuntime> runtime;
    audio::RenderHash hash;
    audio::SpatialAudio spatial;
    u64 tick = 0;

    explicit RuntimeRig(u32 partitions) : catalog(&registry)
    {
        REQUIRE(storage::RegisterBuiltinComponents(&registry).Ok());
        REQUIRE(RegisterAudioComponents(&registry));
        REQUIRE(
            registry
                .Register<Probe>({.id = kProbeId,
                                  .name = "probe",
                                  .fields = {storage::Field<&Probe::x>("x"), storage::Field<&Probe::y>("y"),
                                             storage::Field<&Probe::n>("n"), storage::Field<&Probe::ended>("ended")}})
                .Ok());
        REQUIRE(RegisterStandardBindings(&bindings));
        REQUIRE(RegisterAudioBindings(&bindings));
        REQUIRE(RegisterInputBindings(&bindings));
        FillMap(&map);
        REQUIRE(mixer.Init(MixerSetup()) == audio::MixerStatus::Ok);
        audio.mixer = &mixer;
        audio.clips.resolve = &Clips::Resolve;
        audio.clips.user = &clips;
        audio.voices = &voices;
        spatial.Init(&mixer, audio::SpatialAudioConfig());
        audio.spatial = &spatial;
        input.snapshot = &snapshot;
        ScriptRuntimeConfig config;
        config.backend = backend.Get();
        config.bindings = &bindings;
        config.partitions = partitions;
        config.components = &catalog;
        config.random_seed = 5;
        runtime = MakeUnique<ScriptRuntime>(config);
        runtime->Services().extensions[kServiceAudio] = &audio;
        runtime->Services().extensions[kServiceInput] = &input;
        REQUIRE(RegisterAudioCommandKinds(&runtime->Kinds()));
        runtime->AddScript("media_scripts",
                           CopyShared(ByteSpan(reinterpret_cast<const byte*>(kScripts), CStrLength(kScripts))));
    }

    ecs::Entity Spawn(StringView behavior)
    {
        const ecs::Entity e = world.Create(ecs::LocalTransform(), Probe());
        REQUIRE(world.Add(e, ScriptComponent(behavior, Value::MakeMap())) == ecs::Status::Ok);
        return e;
    }

    // One frame: the input is captured, the scripts run and merge, a block renders, the voice events are pumped.
    void Frame(const os::InputState& state, jobs::Scheduler* scheduler = nullptr, StringView text = StringView())
    {
        ++tick;
        snapshot.Capture(tick, state, text, map);
        EmitInputEvents(snapshot, runtime.Get());
        runtime->Tick(&world, 1.0 / 60.0, scheduler);
        RenderBlock(mixer, &hash);
        PumpAudioEvents(&mixer, &voices, runtime.Get());
    }

    // One frame with placed sounds: the scripts run and merge, the transforms are made (the propagation system's job in
    // a game), the spatial system places the sounds, a block renders and both pumps run. Returns the block's peak.
    f32 PlacedFrame()
    {
        ++tick;
        runtime->Tick(&world, 1.0 / 60.0, nullptr);
        Vector<ecs::Entity> placed;
        Vector<ecs::LocalTransform> locals;
        world.Query<ecs::Read<ecs::LocalTransform>>().ForEach(
            [&](ecs::Entity e, const ecs::LocalTransform& local)
            {
                placed.Push(e);
                locals.Push(local);
            });
        for (usize i = 0; i < placed.Size(); ++i)  // after the query: `Set` that adds moves rows
        {
            world.Set<ecs::WorldTransform>(placed[i], ecs::WorldTransform::FromMatrix(locals[i].ToMatrix()));
        }
        spatial.Update(world, 1.0f / 60.0f);
        f32 peak = 0.0f;
        RenderBlock(mixer, nullptr, &peak);
        PumpAudioEvents(&mixer, &voices, runtime.Get());
        PumpPlacedSounds(&world, &spatial, &voices, runtime.Get());
        return peak;
    }

    u32 SourceCount()
    {
        u32 count = 0;
        world.Query<ecs::Read<audio::AudioSource>>().ForEach([&](ecs::Entity, const audio::AudioSource&) { ++count; });
        return count;
    }

    ecs::Entity Listener()
    {
        return world.Create(audio::AudioListener(), ecs::WorldTransform::FromMatrix(ecs::LocalTransform().ToMatrix()));
    }

    // One frame from a recorded snapshot: the platform's state is not touched.
    void Replay(InputSnapshot recorded, jobs::Scheduler* scheduler = nullptr)
    {
        ++tick;
        snapshot = Move(recorded);
        EmitInputEvents(snapshot, runtime.Get());
        runtime->Tick(&world, 1.0 / 60.0, scheduler);
        RenderBlock(mixer, &hash);
        PumpAudioEvents(&mixer, &voices, runtime.Get());
    }

    // The world's probes and every rendered block so far, as one number.
    u64 StateHash()
    {
        u64 sum = 0;
        world.Query<ecs::Read<Probe>>().ForEach(
            [&](ecs::Entity e, const Probe& p)
            {
                u32 bits[2];
                MemCopy(&bits[0], &p.x, 4);
                MemCopy(&bits[1], &p.y, 4);
                u64 h = e.ToU64() * 0x9E3779B97F4A7C15ull;
                h = (h ^ bits[0]) * 0x100000001B3ull;
                h = (h ^ bits[1]) * 0x100000001B3ull;
                h = (h ^ static_cast<u64>(p.n)) * 0x100000001B3ull;
                h = (h ^ static_cast<u64>(p.ended)) * 0x100000001B3ull;
                sum += h;
            });
        return sum ^ (hash.Valid() ? hash.Value() * 31 : 0);
    }
};

// A scene of many players; returns the hash of the rendered blocks.
u64 RunAudioScene(usize threads, u32 partitions, u32 players, u32 ticks, u32* ended = nullptr)
{
    jobs::Scheduler scheduler(ConfigFor(threads));
    RuntimeRig rig(partitions);
    Vector<ecs::Entity> entities;
    for (u32 i = 0; i < players; ++i)
    {
        entities.Push(rig.Spawn(StringView("player")));
    }
    for (u32 t = 0; t < ticks; ++t)
    {
        rig.Frame(os::InputState(), &scheduler);
    }
    REQUIRE(rig.runtime->Stats().errors == 0);
    if (ended != nullptr)
    {
        u32 total = 0;
        for (usize i = 0; i < entities.Size(); ++i)
        {
            total += static_cast<u32>(rig.world.Get<Probe>(entities[i])->ended);
        }
        *ended = total;
    }
    REQUIRE(rig.hash.Valid());
    return rig.hash.Value();
}

}  // namespace media_test

TEST_CASE("Script media: a voice's end reaches the script that started it", "[script_media][audio][events]")
{
    RuntimeRig rig(2);
    const ecs::Entity player = rig.Spawn(StringView("player"));
    for (u32 t = 0; t < 12; ++t)
    {
        rig.Frame(os::InputState());
    }
    REQUIRE(rig.runtime->Stats().errors == 0);
    // The clips are three and eight blocks long and the script stops its voice every fifth tick: it heard ends.
    CHECK(rig.world.Get<Probe>(player)->ended >= 1);
    CHECK(rig.voices.Size() <= 3);
}

TEST_CASE("Script media: audio.play with a position places a sound, and its entity goes when it ends",
          "[script_media][audio][placed]")
{
    RuntimeRig rig(2);
    rig.Listener();
    const ecs::Entity player = rig.Spawn(StringView("placer"));
    f32 loudest = 0.0f;
    u32 most_sources = 0;
    for (u32 t = 0; t < 40; ++t)
    {
        loudest = Max(loudest, rig.PlacedFrame());
        most_sources = Max(most_sources, rig.SourceCount());
    }
    REQUIRE(rig.runtime->Stats().errors == 0);
    CHECK(most_sources == 1);
    CHECK(loudest > 0.0f);
    CHECK(loudest < 0.25f * 0.5f);  // six units away: quieter than the flat clip (0.25) by a good part
    CHECK(rig.SourceCount() == 0);  // the transient entity is gone
    CHECK(rig.voices.Size() == 0);
    CHECK(rig.world.Get<Probe>(player)->ended == 1);
    CHECK(rig.world.Get<Probe>(player)->n == 1);  // `finished`
}

TEST_CASE("Script media: audio.play on an entity follows it, takes set_gain and stop", "[script_media][audio][placed]")
{
    RuntimeRig rig(2);
    rig.Listener();
    const ecs::Entity player = rig.Spawn(StringView("attacher"));
    rig.world.Get<ecs::LocalTransform>(player)->position = Vec3d(2, 0, 0);
    f32 before_stop = 0.0f;
    for (u32 t = 0; t < 13; ++t)
    {
        before_stop = rig.PlacedFrame();
    }
    CHECK(before_stop > 0.0f);
    CHECK(rig.SourceCount() == 1);
    CHECK(rig.world.Has<audio::AudioSource>(player));
    CHECK(rig.world.Get<audio::AudioSource>(player)->gain == 0.5f);  // set_gain on a placed sound edits the source
    for (u32 t = 0; t < 12; ++t)
    {
        rig.PlacedFrame();
    }
    REQUIRE(rig.runtime->Stats().errors == 0);
    CHECK(!rig.world.Has<audio::AudioSource>(player));  // stop removed the source, not the entity
    CHECK(rig.world.Exists(player));
    CHECK(rig.world.Get<Probe>(player)->ended == 1);
    CHECK(rig.world.Get<Probe>(player)->n == 1);  // `stopped`
    CHECK(rig.voices.Size() == 0);
    CHECK(rig.PlacedFrame() == 0.0f);
}

TEST_CASE("Script media: the refused forms of a placed sound are script errors", "[script_media][audio][placed]")
{
    const char* const behaviors[] = {"bad_both", "bad_pan", "bad_pause"};
    for (const char* behavior : behaviors)
    {
        RuntimeRig rig(1);
        rig.Listener();
        rig.Spawn(StringView(behavior));
        for (u32 t = 0; t < 4; ++t)
        {
            rig.PlacedFrame();
        }
        INFO(behavior);
        CHECK(rig.runtime->Stats().errors >= 1);
    }
    // A world with no spatial system refuses `position`.
    RuntimeRig bare(1);
    bare.audio.spatial = nullptr;
    bare.Spawn(StringView("placer"));
    bare.runtime->Tick(&bare.world, 1.0 / 60.0, nullptr);
    CHECK(bare.runtime->Stats().errors >= 1);
    CHECK(bare.SourceCount() == 0);
}

TEST_CASE("Script media: 256 players give the same rendered blocks at 1 and 16 threads", "[script_media][parallel]")
{
    u32 base_ended = 0;
    const u64 base = RunAudioScene(1, 32, 128, 20, &base_ended);
    CHECK(base != 0);
    CHECK(base_ended > 0);
    const usize counts[] = {2, 3, 7, 16};
    for (usize threads : counts)
    {
        INFO("threads " << threads);
        u32 ended = 0;
        CHECK(RunAudioScene(threads, 32, 128, 20, &ended) == base);
        CHECK(ended == base_ended);
    }
}

TEST_CASE("Script media: the same scene twice gives the same blocks (it is not an accident of timing)",
          "[script_media][parallel]")
{
    CHECK(RunAudioScene(8, 16, 64, 12) == RunAudioScene(8, 16, 64, 12));
}

namespace media_test
{

// The scripted input of a session: keys and the mouse as a function of the tick.
os::InputState LiveInput(u32 tick)
{
    os::InputState state;
    state.keyboard.SetDown(os::Key::W, (tick / 3) % 2 == 0);
    state.keyboard.SetDown(os::Key::Space, tick % 4 == 1);
    state.keyboard.SetDown((tick / 5) % 2 == 0 ? os::Key::D : os::Key::A, true);
    state.mouse_x = static_cast<f64>(tick * 3 % 17);
    state.mouse_y = static_cast<f64>(tick * 5 % 11);
    return state;
}

struct Decisions
{
    f32 x = 0.0f;
    f32 y = 0.0f;
    i32 n = 0;
};

Decisions ReadDecisions(RuntimeRig& rig, ecs::Entity e)
{
    const Probe* p = rig.world.Get<Probe>(e);
    return Decisions{p->x, p->y, p->n};
}

}  // namespace media_test

TEST_CASE("Script media: 64 partitions all read the one snapshot", "[script_media][input][parallel]")
{
    jobs::Scheduler scheduler(ConfigFor(8));
    RuntimeRig rig(64);
    Vector<ecs::Entity> readers;
    for (u32 i = 0; i < 256; ++i)
    {
        readers.Push(rig.Spawn(StringView("reader")));
    }
    for (u32 t = 1; t <= 10; ++t)
    {
        rig.Frame(LiveInput(t), &scheduler);
    }
    REQUIRE(rig.runtime->Stats().errors == 0);
    const Decisions first = ReadDecisions(rig, readers[0]);
    CHECK(first.x != 0.0f);
    CHECK(first.n > 0);
    for (usize i = 1; i < readers.Size(); ++i)
    {
        const Decisions other = ReadDecisions(rig, readers[i]);
        CHECK(other.x == first.x);
        CHECK(other.y == first.y);
        CHECK(other.n == first.n);
    }
}

TEST_CASE("Script media: a recorded session replays to the same decisions", "[script_media][input][replay]")
{
    // The live run records each tick's snapshot; the replay never touches the platform's state: it rebuilds the
    // snapshots from the log and feeds them to a fresh world.
    bytes::ByteWriter log;
    Decisions live;
    {
        RuntimeRig rig(4);
        const ecs::Entity reader = rig.Spawn(StringView("reader"));
        for (u32 t = 1; t <= 24; ++t)
        {
            rig.Frame(LiveInput(t));
            rig.snapshot.Serialize(&log);
        }
        REQUIRE(rig.runtime->Stats().errors == 0);
        live = ReadDecisions(rig, reader);
    }
    RuntimeRig replay(4);
    const ecs::Entity reader = replay.Spawn(StringView("reader"));
    bytes::ByteReader in(log.Bytes());
    u32 frames = 0;
    while (!in.AtEnd())
    {
        InputSnapshot recorded;
        REQUIRE(recorded.Deserialize(&in));
        replay.snapshot = Move(recorded);
        ++replay.tick;
        replay.runtime->Tick(&replay.world, 1.0 / 60.0, nullptr);
        ++frames;
    }
    CHECK(frames == 24);
    REQUIRE(replay.runtime->Stats().errors == 0);
    const Decisions again = ReadDecisions(replay, reader);
    CHECK(again.x == live.x);
    CHECK(again.y == live.y);
    CHECK(again.n == live.n);
    CHECK(live.n > 0);
}

TEST_CASE("Script media: input events reach the scripts that listen", "[script_media][input][events]")
{
    RuntimeRig rig(2);
    const ecs::Entity listener = rig.Spawn(StringView("listener"));
    rig.Frame(os::InputState());                         // nothing changed
    rig.Frame(LiveInput(1), nullptr, StringView("ab"));  // keys down, text typed
    rig.Frame(LiveInput(1));                             // the same keys: no new key events
    rig.Frame(os::InputState());                         // everything released
    REQUIRE(rig.runtime->Stats().errors == 0);
    // Frame 2 delivers its events at the start of its own tick (they were emitted before the tick).
    CHECK(rig.world.Get<Probe>(listener)->n >= 4);
}

namespace media_test
{

constexpr u32 kReplayTicks = 40;

// Spawns the scene a replay runs on: players (they use the audio and the random streams) and readers (the input).
void SpawnReplayScene(RuntimeRig* rig)
{
    for (u32 i = 0; i < 24; ++i)
    {
        rig->Spawn(StringView("player"));
    }
    for (u32 i = 0; i < 24; ++i)
    {
        rig->Spawn(StringView("reader"));
    }
}

// Runs the session live and records it.
void Record(usize threads, ReplayLog* log)
{
    jobs::Scheduler scheduler(ConfigFor(threads));
    RuntimeRig rig(16);
    SpawnReplayScene(&rig);
    log->Begin(5, 1, 0);
    for (u32 t = 1; t <= kReplayTicks; ++t)
    {
        rig.Frame(LiveInput(t), &scheduler);
        log->Record(t, rig.snapshot, rig.StateHash());
    }
    REQUIRE(rig.runtime->Stats().errors == 0);
}

// Runs the log on a fresh world and returns how many ticks matched their recorded hash.
u32 Replay(const ReplayLog& log, usize threads)
{
    jobs::Scheduler scheduler(ConfigFor(threads));
    RuntimeRig rig(16);
    SpawnReplayScene(&rig);
    u32 matched = 0;
    for (usize i = 0; i < log.Size(); ++i)
    {
        InputSnapshot recorded;
        REQUIRE(log.Snapshot(i, &recorded));
        rig.Replay(Move(recorded), &scheduler);
        matched += rig.StateHash() == log.At(i).hash ? 1 : 0;
    }
    REQUIRE(rig.runtime->Stats().errors == 0);
    return matched;
}

}  // namespace media_test

TEST_CASE("Script media: a session recorded at one thread count replays to the same hashes at the others",
          "[script_media][replay][parallel]")
{
    const usize counts[] = {1, 2, 3, 7, 16};
    ReplayLog first;
    Record(16, &first);
    REQUIRE(first.Size() == kReplayTicks);
    CHECK(first.At(kReplayTicks - 1).hash != first.At(0).hash);
    for (usize threads : counts)
    {
        INFO("replayed at " << threads << " threads");
        CHECK(Replay(first, threads) == kReplayTicks);
    }
    // The other way round: recorded on one thread, replayed on sixteen; and the two logs are the same.
    ReplayLog second;
    Record(1, &second);
    CHECK(second.Equals(first));
    CHECK(Replay(second, 16) == kReplayTicks);
}

TEST_CASE("Script media: a log survives its bytes and a damaged log is refused", "[script_media][replay]")
{
    ReplayLog log;
    Record(2, &log);
    bytes::ByteWriter writer;
    log.Serialize(&writer);
    ReplayLog back;
    bytes::ByteReader reader(writer.Bytes());
    REQUIRE(back.Deserialize(&reader));
    CHECK(reader.AtEnd());
    CHECK(back.Equals(log));
    CHECK(Replay(back, 3) == kReplayTicks);

    // Every cut is refused and leaves the log alone.
    for (usize cut = 0; cut < writer.Size(); cut += 97)
    {
        bytes::ByteReader shortened(ByteSpan(writer.Bytes().Data(), cut));
        ReplayLog partial;
        CHECK_FALSE(partial.Deserialize(&shortened));
        CHECK(partial.Size() == 0);
    }
    Vector<byte> bad;
    for (usize i = 0; i < writer.Size(); ++i)
    {
        bad.Push(writer.Bytes()[i]);
    }
    SECTION("an unknown version")
    {
        bad[0] = static_cast<byte>(9);
        bytes::ByteReader r(ByteSpan(bad.Data(), bad.Size()));
        ReplayLog out;
        CHECK_FALSE(out.Deserialize(&r));
    }
    SECTION("a count far above the bytes")
    {
        // version (4), seed (4), start tick (8), start hash (8), then the count's varint
        bad[24] = static_cast<byte>(0xFF);
        bad[25] = static_cast<byte>(0xFF);
        bad[26] = static_cast<byte>(0x7F);
        bytes::ByteReader r(ByteSpan(bad.Data(), bad.Size()));
        ReplayLog out;
        CHECK_FALSE(out.Deserialize(&r));
    }
    SECTION("a tick that does not follow the last")
    {
        bad[27 + 1] = static_cast<byte>(bad[27 + 1] + 1);  // the first entry's tick
        bytes::ByteReader r(ByteSpan(bad.Data(), bad.Size()));
        ReplayLog out;
        CHECK_FALSE(out.Deserialize(&r));
    }
}
