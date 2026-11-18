// The settings bindings (Phase 27 Day 454): typed reads from the tick-start snapshot, validated writes through the
// command buffer, secrets, the allow list and the change notifications.
#include <catch2/catch_test_macros.hpp>

#include "cstr/cstr.h"
#include "script/backend_registry.h"
#include "script/bindings.h"
#include "script/luajit/luajit_backend.h"
#include "script/merge.h"
#include "script/partition_context.h"
#include "script/settings_bindings.h"
#include "settings/memory_store.h"

using namespace engine_placeholder;
using namespace engine_placeholder::script;

namespace
{

const char* const kModes[] = {"low", "medium", "high"};

struct SettingsRig
{
    settings::Registry registry;
    UniquePtr<settings::Settings> settings;
    SettingsPolicy policy;
    BindingRegistry bindings;
    UniquePtr<Backend> backend = luajit::CreateLuaJitBackend();
    UniquePtr<State> state = backend->CreateState(0);
    UniquePtr<Installation> installed;
    ecs::World world;
    CommandKindTable kinds;
    CommandBuffer buffer;
    Overlay overlay;
    SettingsAccess access;
    PartitionContext context;

    SettingsRig()
    {
        registry.Register(settings::MakeInt("game.volume", 5, 0, 10, settings::RangePolicy::Reject));
        registry.Register(settings::MakeFloat("game.gain", 1.0, 0.0, 2.0));
        registry.Register(settings::MakeBool("game.subtitles", false));
        registry.Register(settings::MakeText("game.name", "player"));
        registry.Register(settings::MakeEnum("game.quality", kModes, 3, 1));
        registry.Register(settings::MakeInt("renderer.msaa", 4, 1, 16));
        registry.Register(settings::MakeInt("script.partitions_copy", 64, 1, 1024));
        settings::SettingDesc token = settings::MakeText("game.token", "");
        token.flags = settings::kSecret;
        registry.Register(token);
        settings = MakeUnique<settings::Settings>(registry);
        settings->Layers().AddLayer("user", settings::LayerRole::User, MakeUnique<settings::MemoryConfigStore>());
        settings->LoadFromLayers();
        REQUIRE(RegisterStandardBindings(&bindings));
        Outcome error;
        installed = bindings.Install(state.Get(), InstallProfile::Partition(ContextEntityCheck()), &error);
        REQUIRE(installed.Get() != nullptr);
        REQUIRE(RegisterSettingsCommandKind(&kinds));
        buffer.Begin(0);
        Snapshot();
        context.world = &world;
        context.commands = &buffer;
        context.overlay = &overlay;
        context.settings = &access;
        context.current_entity_key = 77;
    }

    void Snapshot()
    {
        access.registry = &registry;
        access.snapshot = settings->Snapshot();
        access.policy = &policy;
    }

    // Runs `body` (the body of a function) as the script `name` with the context published.
    Outcome Run(const char* name, const char* body, CallOutput* out)
    {
        String<HeapAllocator> source(HeapAllocator::Default());
        source.Append("function run() ");
        source.Append(StringView(body));
        source.Append(" end");
        REQUIRE(state->Load(StringView(name), source.View()).IsOk());
        ScopedPartitionContext scope(&context);
        return state->Call("run", Span<const Value>(), Budget(), out);
    }

    MergeReport Merge()
    {
        CommandBuffer* buffers[] = {&buffer};
        SettingsMergeServices services;
        services.settings = settings.Get();
        return script::Merge(&world, Span<CommandBuffer* const>(buffers, 1), kinds, ConflictPolicy::LastWins,
                             &services);
    }

    const settings::Value& Current(const char* key)
    {
        return *settings->Snapshot()->values[registry.Find(key).value - 1];
    }
};

}  // namespace

TEST_CASE("Settings bindings: typed reads of every kind", "[script][settings]")
{
    SettingsRig rig;
    CallOutput out;
    REQUIRE(rig.Run("reader",
                    "return settings.get('game.volume'), settings.get('game.gain'), settings.get('game.subtitles'), "
                    "settings.get('game.name'), settings.get('game.quality'), settings.exists('game.volume'), "
                    "settings.exists('nope.nothing')",
                    &out)
                .IsOk());
    REQUIRE(out.results.Size() == 7);
    CHECK(out.results[0].Equals(Value::FromInt(5)));
    CHECK(out.results[1].ToDouble() == 1.0);
    CHECK(out.results[2].Equals(Value::FromBool(false)));
    CHECK(out.results[3].Text() == StringView("player"));
    CHECK(out.results[4].Text() == StringView("medium"));
    CHECK(out.results[5].Equals(Value::FromBool(true)));
    CHECK(out.results[6].Equals(Value::FromBool(false)));
}

TEST_CASE("Settings bindings: an unknown key and a secret are errors; describe lists the declaration",
          "[script][settings]")
{
    SettingsRig rig;
    REQUIRE(rig.settings->SetText("game.token", "hunter2").Ok());
    rig.Snapshot();
    CallOutput out;
    Outcome o = rig.Run("reader", "return settings.get('game.nothing')", &out);
    CHECK_FALSE(o.IsOk());
    CHECK(o.message.View().Contains("game.nothing"));
    o = rig.Run("reader", "return settings.get('game.token')", &out);
    CHECK_FALSE(o.IsOk());
    CHECK(o.message.View().Contains("secret"));
    CHECK_FALSE(o.message.View().Contains("hunter2"));
    o = rig.Run("reader", "return settings.describe('game.token')", &out);
    CHECK_FALSE(o.IsOk());
    REQUIRE(rig.Run("reader", "local d = settings.describe('game.volume') return d.type, d.min, d.max, d.default", &out)
                .IsOk());
    REQUIRE(out.results.Size() == 4);
    CHECK(out.results[0].Text() == StringView("int"));
    CHECK(out.results[1].AsInt() == 0);
    CHECK(out.results[2].AsInt() == 10);
    CHECK(out.results[3].AsInt() == 5);
    REQUIRE(rig.Run("reader", "return #settings.describe('game.quality').names", &out).IsOk());
    CHECK(out.results[0].AsInt() == 3);
}

TEST_CASE("Settings bindings: a write is a command, applied at the merge, and reads keep the tick-start value",
          "[script][settings]")
{
    SettingsRig rig;
    CallOutput out;
    REQUIRE(
        rig.Run("writer",
                "settings.set('game.volume', 8) settings.set('game.quality', 'high') settings.set('game.name', 'ada') "
                "settings.set('game.gain', 1) settings.set('game.subtitles', true) return settings.get('game.volume')",
                &out)
            .IsOk());
    CHECK(out.results[0].AsInt() == 5);  // the tick-start value
    CHECK(rig.buffer.Size() == 5);
    CHECK(rig.Current("game.volume").AsInt() == 5);  // nothing stored yet
    const MergeReport report = rig.Merge();
    CHECK(report.applied == 5);
    CHECK(rig.Current("game.volume").AsInt() == 8);
    CHECK(rig.Current("game.quality").AsEnum() == 2);
    CHECK(rig.Current("game.name").AsText() == StringView("ada"));
    CHECK(rig.Current("game.gain").AsFloat() == 1.0);
    CHECK(rig.Current("game.subtitles").AsBool());
}

TEST_CASE("Settings bindings: a value the registry refuses is an error and stores nothing", "[script][settings]")
{
    SettingsRig rig;
    CallOutput out;
    const char* bad[] = {"settings.set('game.volume', 11)",        // out of range (reject)
                         "settings.set('game.volume', 'loud')",    // wrong type
                         "settings.set('game.volume', 2.5)",       // a float for an int
                         "settings.set('game.quality', 'ultra')",  // not an enum name
                         "settings.set('game.quality', 7)",        // not an enum index
                         "settings.set('game.subtitles', 1)",      // a number for a bool
                         "settings.set('game.name', 5)",           // a number for text
                         "settings.set('game.missing', 1)"};       // unknown key
    for (const char* body : bad)
    {
        const Outcome o = rig.Run("writer", body, &out);
        CHECK_FALSE(o.IsOk());
        CHECK(o.message.View().Contains("game."));
    }
    CHECK(rig.buffer.Size() == 0);
    // A clamped number is accepted at the clamped value.
}

TEST_CASE("Settings bindings: the allow list decides what a script may set", "[script][settings]")
{
    SettingsRig rig;
    CallOutput out;
    Outcome o = rig.Run("writer", "settings.set('renderer.msaa', 8)", &out);
    CHECK_FALSE(o.IsOk());
    CHECK(o.message.View().Contains("renderer.msaa"));
    o = rig.Run("writer", "settings.set('script.partitions_copy', 8)", &out);
    CHECK_FALSE(o.IsOk());
    CHECK(rig.buffer.Size() == 0);
    // Reads are not restricted.
    REQUIRE(rig.Run("writer", "return settings.get('renderer.msaa')", &out).IsOk());
    CHECK(out.results[0].AsInt() == 4);

    // One script gets its own list; the others keep the default.
    const StringView renderer("renderer.*");
    rig.policy.SetScriptWritable("trusted", Span<const StringView>(&renderer, 1));
    REQUIRE(rig.Run("trusted", "settings.set('renderer.msaa', 8)", &out).IsOk());
    CHECK(rig.buffer.Size() == 1);
    o = rig.Run("trusted", "settings.set('game.volume', 1)", &out);  // its own list replaced the default
    CHECK_FALSE(o.IsOk());
    o = rig.Run("writer", "settings.set('renderer.msaa', 2)", &out);
    CHECK_FALSE(o.IsOk());
    const MergeReport merged = rig.Merge();
    INFO("applied " << merged.applied << " skipped " << merged.skipped << " unknown " << merged.unknown << " refused "
                    << merged.refused);
    CHECK(rig.Current("renderer.msaa").AsInt() == 8);

    // A prefix is a dotted prefix, not a string prefix.
    SettingsPolicy narrow;
    const StringView game("game");
    narrow.SetDefaultWritable(Span<const StringView>(&game, 1));
    CHECK(narrow.CanWrite("x", "game.volume"));
    CHECK(narrow.CanWrite("x", "game"));
    CHECK_FALSE(narrow.CanWrite("x", "gamepad.x"));
    narrow.SetDefaultWritable(Span<const StringView>());
    CHECK_FALSE(narrow.CanWrite("x", "game.volume"));
}

TEST_CASE("Settings bindings: without a settings context every call is an error", "[script][settings]")
{
    SettingsRig rig;
    rig.context.settings = nullptr;
    CallOutput out;
    CHECK_FALSE(rig.Run("reader", "return settings.get('game.volume')", &out).IsOk());
    CHECK_FALSE(rig.Run("writer", "settings.set('game.volume', 1)", &out).IsOk());
}

TEST_CASE("Settings bindings: change notifications come in partition, subscription, key order", "[script][settings]")
{
    SettingsRig rig;
    SettingsAccess other;  // a second partition
    other.registry = &rig.registry;
    other.snapshot = rig.settings->Snapshot();
    other.policy = &rig.policy;
    CallOutput out;
    // Partition 1's entity subscribes first in time, partition 0's later: the order is by partition, not by time.
    {
        PartitionContext second = rig.context;
        second.settings = &other;
        second.current_entity_key = 900;
        ScopedPartitionContext scope(&second);
        REQUIRE(rig.state->Load("sub1", "function run() settings.on_change('game', 'on_game') end").IsOk());
        REQUIRE(rig.state->Call("run", Span<const Value>(), Budget(), &out).IsOk());
    }
    REQUIRE(rig.Run("sub0",
                    "settings.on_change('game.volume', 'on_volume') settings.on_change('renderer.*', 'on_renderer')",
                    &out)
                .IsOk());
    REQUIRE(rig.access.subscriptions.Size() == 2);
    REQUIRE(other.subscriptions.Size() == 1);
    CHECK(rig.access.subscriptions[1].prefix.View() == StringView("renderer"));  // `.*` is dropped

    const SharedPtr<const settings::SettingsSnapshot> before = rig.settings->Snapshot();
    REQUIRE(rig.settings->SetText("game.volume", "9").Ok());
    REQUIRE(rig.settings->SetText("game.name", "bob").Ok());
    REQUIRE(rig.settings->SetText("renderer.msaa", "2").Ok());
    const SharedPtr<const settings::SettingsSnapshot> after = rig.settings->Snapshot();
    SettingsAccess* partitions[] = {&rig.access, &other};
    const Vector<SettingNotification> notes = CollectSettingChanges(rig.registry, *before, *after,
                                                                    Span<SettingsAccess* const>(partitions, 2));
    REQUIRE(notes.Size() == 4);
    CHECK(notes[0].partition == 0);
    CHECK(notes[0].function.View() == StringView("on_volume"));
    CHECK(notes[0].key.View() == StringView("game.volume"));
    CHECK(notes[1].function.View() == StringView("on_renderer"));
    CHECK(notes[1].key.View() == StringView("renderer.msaa"));
    CHECK(notes[2].partition == 1);
    CHECK(notes[2].function.View() == StringView("on_game"));
    CHECK(notes[2].key.View() == StringView("game.name"));  // key order: game.name before game.volume
    CHECK(notes[3].key.View() == StringView("game.volume"));
    CHECK(notes[3].entity_key == 900);

    // No change, no notification; a destroyed entity's subscriptions go.
    CHECK(CollectSettingChanges(rig.registry, *after, *after, Span<SettingsAccess* const>(partitions, 2)).Size() == 0);
    CHECK(RemoveSettingsSubscriptions(&other, 900) == 1);
    CHECK(other.subscriptions.Size() == 0);
    CHECK(RemoveSettingsSubscriptions(&rig.access, 12345) == 0);
}

TEST_CASE("Settings bindings: a script cannot subscribe without limit", "[script][settings]")
{
    SettingsRig rig;
    CallOutput out;
    const Outcome o = rig.Run("greedy", "for i = 1, 100 do settings.on_change('game', 'f' .. i) end", &out);
    CHECK_FALSE(o.IsOk());
    CHECK(o.message.View().Contains("too many"));
    CHECK(rig.access.subscriptions.Size() == kMaxSettingsSubscriptionsPerEntity);
}
