#include <catch2/catch_test_macros.hpp>

#include <stdlib.h>

#include "catch_engine_types.h"
#include "io/file.h"
#include "settings/env_source.h"
#include "settings/file_store.h"
#include "settings/layers.h"
#include "settings/memory_store.h"
#include "store_doubles.h"
#include "temp_dir.h"
#include "test_text.h"

using namespace engine_placeholder;
using namespace engine_placeholder::settings;
using namespace settings_test;

namespace {

const char* const kLayerModes[] = {"fifo", "mailbox", "immediate"};

struct Fixture {
    Registry registry;
    SettingId width, mode, gpu, vsync, quality;
    Fixture() {
        width = registry.Register(MakeInt("renderer.width", 1280, 0, 16384));
        SettingDesc m = MakeEnum("renderer.present_mode", kLayerModes, 3, 0);
        m.env = "ENGINE_PRESENT_MODE";
        mode = registry.Register(m);
        SettingDesc g = MakeText("renderer.adapter", "");
        g.env = "ENGINE_RHI_GPU";
        g.flag = "gpu";
        gpu = registry.Register(g);
        SettingDesc v = MakeBool("renderer.vsync", true);
        v.flag = "vsync";
        vsync = registry.Register(v);
        quality = registry.Register(MakeInt("renderer.quality", 2, 0, 3, RangePolicy::Reject));
        registry.Freeze();
    }
};

// A memory store holding `key = value` as durable content (a Set is only staged until Commit, and LoadAll discards staged changes).
UniquePtr<MemoryConfigStore> WithInt(const char* key, i64 v) {
    UniquePtr<MemoryConfigStore> store = MakeUnique<MemoryConfigStore>();
    StoredValue s;
    s.typed = true;
    s.value = Value::FromInt(v);
    store->Set(key, s);
    store->Commit(CommitOptions());
    return store;
}

}  // namespace

TEST_CASE("Layer precedence: for every combination of layers holding a value, the highest one wins", "[settings][layers]") {
    Fixture f;
    const LayerRole roles[] = {LayerRole::System, LayerRole::User, LayerRole::Project, LayerRole::Custom, LayerRole::Environment, LayerRole::CommandLine};
    for (u32 mask = 0; mask < 64; ++mask) {
        LayerStack stack;
        for (u32 r = 0; r < 6; ++r) {
            auto store = (mask & (1u << r)) ? WithInt("renderer.width", 100 + r) : MakeUnique<MemoryConfigStore>();
            stack.AddLayer(LayerRoleName(roles[r]), roles[r], ConfigStorePtr(static_cast<UniquePtr<MemoryConfigStore>&&>(store)));
        }
        stack.LoadAll(nullptr);
        Resolution res;
        stack.Resolve(f.registry, f.width, &res, nullptr);
        if (mask == 0) {
            CHECK(res.layer == -1);
            CHECK(StringView(res.layer_name) == "defaults");
            CHECK(res.value.AsInt() == 1280);
        } else {
            u32 top = 5;
            while (!(mask & (1u << top))) --top;
            INFO("mask " << mask);
            CHECK(res.value.AsInt() == 100 + top);
            CHECK(StringView(res.layer_name) == StringView(LayerRoleName(roles[top])));
        }
    }
}

TEST_CASE("Layers are kept in role order whatever order they were added in", "[settings][layers]") {
    LayerStack stack;
    stack.AddLayer("cli", LayerRole::CommandLine, MakeUnique<MemoryConfigStore>());
    stack.AddLayer("sys", LayerRole::System, MakeUnique<MemoryConfigStore>());
    stack.AddLayer("user", LayerRole::User, MakeUnique<MemoryConfigStore>());
    stack.AddLayer("user2", LayerRole::User, MakeUnique<MemoryConfigStore>());
    REQUIRE(stack.Size() == 4);
    CHECK(stack.Name(0) == "sys");
    CHECK(stack.Name(1) == "user");
    CHECK(stack.Name(2) == "user2"); // a later layer of the same role is higher
    CHECK(stack.Name(3) == "cli");
    CHECK(stack.WriteLayer() == 1);
    stack.SetWriteLayer(2);
    stack.AddLayer("project", LayerRole::Project, MakeUnique<MemoryConfigStore>());
    CHECK(stack.Name(stack.WriteLayer()) == "user2"); // the chosen write layer is followed when layers are inserted below it
}

TEST_CASE("A value falls through a layer that is missing, failing or holds a bad value, with a problem for each", "[settings][layers]") {
    Fixture f;
    LayerStack stack;
    stack.AddLayer("user", LayerRole::User, ConfigStorePtr(WithInt("renderer.width", 640)));
    // Environment layer with a bad number, from a made-up environment.
    EnvironmentSnapshot env;
    env.Add("ENGINE_SETTING_RENDERER__WIDTH", "wide");
    env.Add("ENGINE_RHI_GPU", "llvmpipe");
    stack.AddLayer("environment", LayerRole::Environment, MakeUnique<EnvironmentSource>(f.registry, env));
    // A layer whose reads fail after loading.
    stack.AddLayer("flaky", LayerRole::Project, MakeUnique<FailingConfigStore>(MakeUnique<MemoryConfigStore>(), 2, StoreError::Io, true));
    Vector<LayerProblem> problems;
    stack.LoadAll(&problems);
    Resolution width;
    stack.Resolve(f.registry, f.width, &width, &problems);
    CHECK(width.value.AsInt() == 640); // the bad env value and the failing layer were skipped
    CHECK(StringView(width.layer_name) == "user");
    bool bad_value = false, failing = false;
    for (usize i = 0; i < problems.Size(); ++i) {
        const StringView m(problems[i].message);
        if (StringView(problems[i].layer) == "environment" && m.Contains("bad value") && m.Contains("$ENGINE_SETTING_RENDERER__WIDTH")) bad_value = true;
        if (StringView(problems[i].layer) == "flaky" && m.Contains("could not be read (io)")) failing = true;
    }
    CHECK(bad_value);
    CHECK(failing);
    // The legacy alias supplies a text setting, with the variable named as the origin.
    Resolution gpu;
    stack.Resolve(f.registry, f.gpu, &gpu, nullptr);
    CHECK(gpu.value.AsText() == "llvmpipe");
    CHECK(StringView(gpu.origin) == "$ENGINE_RHI_GPU");
}

TEST_CASE("A layer that cannot load is skipped and the others still resolve", "[settings][layers]") {
    Fixture f;
    LayerStack stack;
    stack.AddLayer("user", LayerRole::User, ConfigStorePtr(WithInt("renderer.width", 800)));
    stack.AddLayer("dead", LayerRole::Project, MakeUnique<FailingConfigStore>(MakeUnique<MemoryConfigStore>(), 1, StoreError::PermissionDenied, true));
    Vector<LayerProblem> problems;
    stack.LoadAll(&problems);
    CHECK(stack.State(1) == LayerState::Failed);
    CHECK(stack.State(0) == LayerState::Loaded);
    REQUIRE(problems.Size() == 1);
    CHECK(StringView(problems[0].message).Contains("permission_denied"));
    Resolution r;
    stack.Resolve(f.registry, f.width, &r, nullptr);
    CHECK(r.value.AsInt() == 800);
}

TEST_CASE("Provenance names the layer and, for a file, the line", "[settings][layers]") {
    Fixture f;
    io_test::TempDir dir;
    REQUIRE(io::WriteFile(dir / "user.cfg", "# top\n[renderer]\nwidth = 1920\n", 29).Ok());
    LayerStack stack;
    stack.AddLayer("user file", LayerRole::User, MakeUnique<FileConfigStore>(dir / "user.cfg"));
    stack.AddLayer("project file", LayerRole::Project, MakeUnique<FileConfigStore>(dir / "missing.cfg"));
    stack.LoadAll(nullptr);
    CHECK(stack.State(0) == LayerState::Loaded);
    CHECK(stack.State(1) == LayerState::Missing);
    Resolution r;
    stack.Resolve(f.registry, f.width, &r, nullptr);
    CHECK(r.value.AsInt() == 1920);
    CHECK(StringView(r.layer_name) == "user file");
    CHECK(StringView(r.origin) == "line 3");
}

TEST_CASE("Set writes the write layer; MinimalWrites drops what a lower layer already gives; Unset shows the lower layer again", "[settings][layers]") {
    Fixture f;
    LayerStack stack;
    stack.AddLayer("system", LayerRole::System, ConfigStorePtr(WithInt("renderer.width", 1000)));
    stack.AddLayer("user", LayerRole::User, MakeUnique<MemoryConfigStore>());
    stack.LoadAll(nullptr);
    Resolution r;
    REQUIRE(stack.Set(f.registry, f.width, Value::FromInt(2000), true).Ok());
    stack.Resolve(f.registry, f.width, &r, nullptr);
    CHECK(r.value.AsInt() == 2000);
    CHECK(StringView(r.layer_name) == "user");
    // Setting it back to what the system layer gives removes the entry instead of writing it.
    REQUIRE(stack.Set(f.registry, f.width, Value::FromInt(1000), true).Ok());
    StoredValue sv;
    CHECK(stack.Store(1).Get("renderer.width", &sv).Is(StoreError::NotFound));
    stack.Resolve(f.registry, f.width, &r, nullptr);
    CHECK(StringView(r.layer_name) == "system");
    // Without MinimalWrites the same value is written.
    REQUIRE(stack.Set(f.registry, f.width, Value::FromInt(1000), false).Ok());
    CHECK(stack.Store(1).Get("renderer.width", &sv).Ok());
    // A default value that no layer sets is not written either.
    REQUIRE(stack.Set(f.registry, f.quality, Value::FromInt(2), true).Ok());
    CHECK(stack.Store(1).Get("renderer.quality", &sv).Is(StoreError::NotFound));
    // Unset.
    REQUIRE(stack.Set(f.registry, f.width, Value::FromInt(3000), true).Ok());
    REQUIRE(stack.Unset(f.registry, f.width).Ok());
    stack.Resolve(f.registry, f.width, &r, nullptr);
    CHECK(r.value.AsInt() == 1000);
    CHECK(stack.Unset(f.registry, f.width).Ok()); // already gone
    // No write layer: an error, not a crash.
    LayerStack none;
    CHECK(none.Set(f.registry, f.width, Value::FromInt(1), true).Is(StoreError::NotFound));
}

TEST_CASE("The environment: standard names, legacy aliases, which wins, and the real process environment", "[settings][layers][env]") {
    Fixture f;
    Key k;
    REQUIRE(Key::Parse("renderer.present_mode", &k) == KeyError::None);
    char name[kMaxEnvNameBytes];
    EnvironmentVariableFor(k, name, sizeof(name));
    CHECK(StringView(name) == "ENGINE_SETTING_RENDERER__PRESENT_MODE");
    EnvironmentSnapshot env;
    env.Add("ENGINE_PRESENT_MODE", "immediate");
    EnvironmentSource only_alias(f.registry, env);
    StoredValue sv;
    REQUIRE(only_alias.Get("renderer.present_mode", &sv).Ok());
    CHECK(sv.value.AsText() == "immediate");
    env.Add("ENGINE_SETTING_RENDERER__PRESENT_MODE", "mailbox");
    EnvironmentSource both(f.registry, env);
    REQUIRE(both.Get("renderer.present_mode", &sv).Ok());
    CHECK(sv.value.AsText() == "mailbox"); // the standard name wins
    char origin[64];
    both.Origin("renderer.present_mode", origin, sizeof(origin));
    CHECK(StringView(origin) == "$ENGINE_SETTING_RENDERER__PRESENT_MODE");
    CHECK(both.Get("renderer.width", &sv).Is(StoreError::NotFound));
    CHECK(both.Set("renderer.width", sv).Is(StoreError::ReadOnly));
    CHECK(both.Commit(CommitOptions()).Is(StoreError::ReadOnly));
    Vector<Key> keys;
    REQUIRE(both.Enumerate(StringView(), &keys).Ok());
    CHECK(keys.Size() == 1);
    // Resolved through a stack the text converts by the setting's type.
    LayerStack stack;
    stack.AddLayer("environment", LayerRole::Environment, MakeUnique<EnvironmentSource>(f.registry, env));
    stack.LoadAll(nullptr);
    Resolution r;
    stack.Resolve(f.registry, f.mode, &r, nullptr);
    CHECK(r.value.AsEnum() == 1);
    // The process environment is captured, scrubbed by the test itself.
    ::setenv("ENGINE_SETTING_RENDERER__WIDTH", "4321", 1);
    const EnvironmentSnapshot real = EnvironmentSnapshot::Capture();
    ::unsetenv("ENGINE_SETTING_RENDERER__WIDTH");
    REQUIRE(real.Find("ENGINE_SETTING_RENDERER__WIDTH") != nullptr);
    CHECK(real.Find("ENGINE_SETTING_RENDERER__WIDTH")->View() == "4321");
    CHECK(EnvironmentSnapshot::Capture().Find("ENGINE_SETTING_RENDERER__WIDTH") == nullptr);
}

TEST_CASE("Command line: --set, flag aliases, bool flags, files, the options for the application, and what is left over", "[settings][layers][cli]") {
    Fixture f;
    const char* argv[] = {"prog", "--set", "renderer.width=1920", "--set=renderer.adapter=a=b", "--set", "renderer.width=2560", "--gpu", "llvmpipe", "--no-vsync-x",
                          "--no-vsync", "positional", "--unknown-flag", "--settings", "my.cfg", "--settings=other.cfg", "--list-settings", "--save-settings=out.cfg", "-h", "--", "--set", "x=1", "--help"};
    CommandLineSource cli(f.registry, static_cast<int>(sizeof(argv) / sizeof(argv[0])), argv);
    const CommandLineResult& r = cli.Result();
    CHECK(r.errors.Size() == 0);
    REQUIRE(r.rest.Size() == 7);
    CHECK(r.rest[0].View() == "--no-vsync-x");
    CHECK(r.rest[1].View() == "positional");
    CHECK(r.rest[2].View() == "--unknown-flag");
    CHECK(r.rest[3].View() == "--"); // the end of the options: the rest belongs to the application, not to the settings system
    CHECK(r.rest[4].View() == "--set");
    CHECK(r.rest[5].View() == "x=1");
    CHECK(r.rest[6].View() == "--help");
    REQUIRE(r.settings_files.Size() == 2);
    CHECK(r.settings_files[0].View() == "my.cfg");
    CHECK(r.list_settings);
    CHECK(r.save_settings);
    CHECK(r.save_path.View() == "out.cfg");
    CHECK(r.help);
    StoredValue sv;
    REQUIRE(cli.Get("renderer.width", &sv).Ok());
    CHECK(sv.value.AsText() == "2560"); // a repeated --set: the last one wins
    REQUIRE(cli.Get("renderer.adapter", &sv).Ok());
    CHECK(sv.value.AsText() == "llvmpipe"); // --gpu came after --set=renderer.adapter=a=b
    REQUIRE(cli.Get("renderer.vsync", &sv).Ok());
    CHECK(sv.value.AsText() == "false");
    char origin[64];
    cli.Origin("renderer.width", origin, sizeof(origin));
    CHECK(StringView(origin) == "--set renderer.width=2560");
    cli.Origin("renderer.vsync", origin, sizeof(origin));
    CHECK(StringView(origin) == "--no-vsync");
}

TEST_CASE("Command line: bool flags take =value, never a separate value; --save-settings leaves a following argument alone", "[settings][layers][cli]") {
    Fixture f;
    const char* argv[] = {"prog", "--vsync", "false", "--save-settings", "level1.map", "--no-vsync=1"};
    CommandLineSource cli(f.registry, 6, argv);
    StoredValue sv;
    REQUIRE(cli.Get("renderer.vsync", &sv).Ok());
    CHECK(sv.value.AsText() == "true"); // "false" is a positional argument, not the flag's value
    REQUIRE(cli.Result().rest.Size() == 2);
    CHECK(cli.Result().rest[0].View() == "false");
    CHECK(cli.Result().rest[1].View() == "level1.map");
    CHECK(cli.Result().save_settings);
    CHECK(cli.Result().save_path.Length() == 0);
    REQUIRE(cli.Result().errors.Size() == 1);
    CHECK(cli.Result().errors[0].View() == "--no-vsync takes no value");
}

TEST_CASE("Text sources: a value over 511 bytes is TooLong, an empty variable is unset", "[settings][layers][env]") {
    Fixture f;
    EnvironmentSnapshot env;
    env.Add("ENGINE_RHI_GPU", engine_test::Repeated(600, 'g').View());
    env.Add("ENGINE_SETTING_RENDERER__WIDTH", "");
    EnvironmentSource source(f.registry, env);
    StoredValue sv;
    CHECK(source.Get("renderer.adapter", &sv).Is(StoreError::TooLong));
    CHECK(source.Get("renderer.width", &sv).Is(StoreError::NotFound));
    LayerStack stack;
    stack.AddLayer("environment", LayerRole::Environment, MakeUnique<EnvironmentSource>(f.registry, env));
    Vector<LayerProblem> problems;
    stack.LoadAll(&problems);
    Resolution r;
    stack.Resolve(f.registry, f.gpu, &r, &problems);
    CHECK(r.layer == -1);
    REQUIRE(problems.Size() == 1);
    CHECK(StringView(problems[0].message).Contains("too_long"));
}

TEST_CASE("A file that still uses a deprecated key is read, with a note", "[settings][layers]") {
    Registry registry;
    SettingDesc d = MakeInt("renderer.width", 1280, 0, 16384);
    const char* const old[] = {"renderer.res"};
    d.deprecated_keys = old;
    d.deprecated_count = 1;
    const SettingId id = registry.Register(d);
    LayerStack stack;
    stack.AddLayer("user", LayerRole::User, MakeUnique<MemoryConfigStore>());
    StoredValue sv;
    sv.typed = true;
    sv.value = Value::FromInt(1920);
    stack.Store(0).Set("renderer.res", sv);
    stack.Store(0).Commit(CommitOptions());
    stack.LoadAll(nullptr);
    Vector<LayerProblem> problems;
    Resolution r;
    stack.Resolve(registry, id, &r, &problems);
    CHECK(r.value.AsInt() == 1920);
    REQUIRE(problems.Size() == 1);
    CHECK(StringView(problems[0].message) == "uses the deprecated key 'renderer.res'; it is now 'renderer.width'");
}

TEST_CASE("Command line: a value may contain '=' or quotes, a bool flag may take =value, and the stack converts by type", "[settings][layers][cli]") {
    Fixture f;
    const char* argv[] = {"prog", "--set", "renderer.adapter=\"NVIDIA = RTX\"", "--vsync=false", "--set", "renderer.present_mode=immediate"};
    CommandLineSource cli(f.registry, 6, argv);
    CHECK(cli.Result().errors.Size() == 0);
    LayerStack stack;
    stack.AddLayer("command line", LayerRole::CommandLine, MakeUnique<CommandLineSource>(f.registry, 6, argv));
    stack.LoadAll(nullptr);
    Resolution r;
    stack.Resolve(f.registry, f.gpu, &r, nullptr);
    CHECK(r.value.AsText() == "NVIDIA = RTX");
    stack.Resolve(f.registry, f.vsync, &r, nullptr);
    CHECK_FALSE(r.value.AsBool());
    stack.Resolve(f.registry, f.mode, &r, nullptr);
    CHECK(r.value.AsEnum() == 2);
}

TEST_CASE("Command line: unknown keys suggest the closest one, bad forms are errors, and a bad value is reported when resolved", "[settings][layers][cli]") {
    Fixture f;
    const char* argv[] = {"prog", "--set", "renderer.widht=1", "--set", "nonsense.key=1", "--set", "novalue", "--set", "=5", "--gpu"};
    CommandLineSource cli(f.registry, 10, argv);
    const CommandLineResult& r = cli.Result();
    REQUIRE(r.errors.Size() == 5);
    CHECK(r.errors[0].View() == "--set renderer.widht: no such setting; did you mean 'renderer.width'?");
    CHECK(r.errors[1].View() == "--set nonsense.key: no such setting");
    CHECK(r.errors[2].View() == "--set novalue: expected key=value");
    CHECK(r.errors[3].View() == "--set =5: expected key=value");
    CHECK(r.errors[4].View() == "--gpu needs a value");
    const char* lone[] = {"prog", "--set"};
    CommandLineSource lone_set(f.registry, 2, lone);
    REQUIRE(lone_set.Result().errors.Size() == 1);
    CHECK(lone_set.Result().errors[0].View() == "--set needs key=value");
    Key suggestion;
    CHECK(SuggestKey(f.registry, "renderer.vsnc", &suggestion));
    CHECK(suggestion.View() == "renderer.vsync");
    CHECK_FALSE(SuggestKey(f.registry, "completely.different.thing", &suggestion));
    CHECK_FALSE(SuggestKey(f.registry, "zzz", &suggestion)); // nothing in common with any key
    // A value of the wrong type is only found when the layer is read through the registry.
    const char* bad[] = {"prog", "--set", "renderer.quality=9"};
    LayerStack stack;
    stack.AddLayer("command line", LayerRole::CommandLine, MakeUnique<CommandLineSource>(f.registry, 3, bad));
    Vector<LayerProblem> problems;
    stack.LoadAll(&problems);
    Resolution res;
    stack.Resolve(f.registry, f.quality, &res, &problems); // the quality setting rejects out-of-range values
    CHECK(res.value.AsInt() == 2);
    REQUIRE(problems.Size() == 1);
    CHECK(StringView(problems[0].message).Contains("out_of_range"));
    CHECK(StringView(problems[0].message).Contains("--set renderer.quality=9"));
}

TEST_CASE("Shipped defaults: a read-only layer from an array and from a file next to the program", "[settings][layers]") {
    Fixture f;
    io_test::TempDir dir;
    REQUIRE(io::WriteFile(dir / "defaults.cfg", "[renderer]\nwidth = 1600\n", 24).Ok());
    FileStoreOptions ro;
    ro.read_only = true;
    LayerStack stack;
    stack.AddLayer("shipped", LayerRole::System, MakeUnique<FileConfigStore>(dir / "defaults.cfg", ro));
    stack.AddLayer("user", LayerRole::User, MakeUnique<MemoryConfigStore>());
    stack.LoadAll(nullptr);
    Resolution r;
    stack.Resolve(f.registry, f.width, &r, nullptr);
    CHECK(r.value.AsInt() == 1600);
    CHECK(StringView(r.layer_name) == "shipped");
    StoredValue text;
    text.value = Value::FromText("1");
    CHECK(stack.Store(0).Set("renderer.width", text).Is(StoreError::ReadOnly));
    CHECK(stack.Store(0).Commit(CommitOptions()).Is(StoreError::ReadOnly));
}
