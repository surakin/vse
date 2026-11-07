#include <catch2/catch_test_macros.hpp>

#include <atomic>
#include <chrono>
#include <thread>

#include "catch_engine_types.h"
#include "debug/crash_report.h"
#include "debug/crash_state.h"
#include "io/directory.h"
#include "io/file.h"
#include "io/file_system.h"
#include "settings/settings.h"
#include "settings/setting.h"
#include "store_doubles.h"
#include "temp_dir.h"
#include "test_text.h"

using namespace engine_placeholder;
using namespace engine_placeholder::settings;
using namespace settings_test;

namespace {

const char* const kSvModes[] = {"fifo", "mailbox", "immediate"};

struct App {
    Registry registry;
    SettingId width, mode, name, a, b, hdr, rs;
    App() {
        SettingDesc w = MakeInt("renderer.width", 1280, 0, 16384, RangePolicy::Reject);
        w.apply_class = 2;
        width = registry.Register(w);
        SettingDesc m = MakeEnum("renderer.present_mode", kSvModes, 3, 0);
        m.apply_class = 2;
        m.env = "ENGINE_PRESENT_MODE";
        mode = registry.Register(m);
        name = registry.Register(MakeText("renderer.adapter", ""));
        a = registry.Register(MakeInt("pair.a", 50, 0, 100));
        b = registry.Register(MakeInt("pair.b", 50, 0, 100));
        SettingDesc h = MakeEnum("renderer.hdr", kSvModes, 3, 0);
        h.capability = "hdr";
        hdr = registry.Register(h);
        SettingDesc r = MakeBool("renderer.vsync", true);
        r.flags = kRestartRequired;
        r.flag = "vsync";
        rs = registry.Register(r);
    }
};

engine_test::Text SlurpText(const io::Path& path) {
    Vector<u8> bytes;
    if (io::ReadFile(path, &bytes).Failed()) return engine_test::MakeText("<missing>");
    return engine_test::MakeText(StringView(reinterpret_cast<const char*>(bytes.Data()), bytes.Size()));
}

void WriteText(const io::Path& path, StringView text) { REQUIRE(io::WriteFile(path, text.Data(), text.Length()).Ok()); }

LoadOptions Options(const io::Path& user) {
    LoadOptions o;
    o.app_name = "settings-test";
    o.user_file = &user;
    o.use_environment = false;
    return o;
}

struct Recorder {
    Vector<engine_test::Text> log;
    u32 calls = 0;
    u8 highest = 0;
    bool restart = false;
};

void Record(void* user, const ChangeEvent& e) {
    Recorder* r = static_cast<Recorder*>(user);
    ++r->calls;
    r->highest = e.highest_apply_class;
    r->restart = e.restart_required;
    for (usize i = 0; i < e.count; ++i) r->log.Push(engine_test::Format("%.*s:%s", static_cast<int>(e.registry->KeyOf(e.changes[i].id).View().Length()), e.registry->KeyOf(e.changes[i].id).View().Data(), "changed"));
}

}  // namespace

TEST_CASE("Load: every combination of missing, damaged and healthy layers", "[settings][service]") {
    io_test::TempDir dir;
    enum { kMissing, kHealthy, kDamaged };
    const char* const texts[] = {nullptr, "[renderer]\nwidth = 1920\n", "[renderer]\nwidth = 1920\ngarbage here\n"};
    for (int user = 0; user < 3; ++user) {
        for (int project = 0; project < 3; ++project) {
            App app;
            Settings settings(app.registry);
            const io::Path user_path = dir / engine_test::Format("u%d_%d.cfg", user, project).View();
            const io::Path project_path = dir / engine_test::Format("p%d_%d.cfg", user, project).View();
            if (texts[user]) WriteText(user_path, texts[user]);
            if (texts[project]) WriteText(project_path, StringView(texts[project]).Substring(0, StringView(texts[project]).Length()));
            LoadOptions o = Options(user_path);
            o.project_file = &project_path;
            const LoadReport report = settings.Load(o);
            INFO("user " << user << " project " << project);
            REQUIRE(report.layers.Size() == 2);
            const LayerState expected_user = user == kMissing ? LayerState::Missing : user == kHealthy ? LayerState::Loaded : LayerState::Damaged;
            const LayerState expected_project = project == kMissing ? LayerState::Missing : project == kHealthy ? LayerState::Loaded : LayerState::Damaged;
            CHECK(report.layers[0].state == expected_user);
            CHECK(report.layers[1].state == expected_project);
            CHECK(StringView(report.layers[0].name) == "user file");
            CHECK(report.problems.Size() == static_cast<usize>((user == kDamaged ? 1 : 0) + (project == kDamaged ? 1 : 0)));
            CHECK(report.Clean() == (user != kDamaged && project != kDamaged));
            // The readable lines are used whatever happened to the rest; the project layer is the higher one.
            Setting<i64> width(app.registry, app.width);
            CHECK(width.Get() == ((user != kMissing || project != kMissing) ? 1920 : 1280));
        }
    }
}

TEST_CASE("Load: values from every layer, provenance, and command-line errors in the report", "[settings][service]") {
    io_test::TempDir dir;
    App app;
    Settings settings(app.registry);
    WriteText(dir / "user.cfg", "# mine\n[renderer]\nwidth = 1920\npresent_mode = immediate\n");
    EnvironmentSnapshot env;
    env.Add("ENGINE_PRESENT_MODE", "mailbox");
    const DefaultEntry shipped[] = {{"renderer.width", "800"}, {"renderer.adapter", "\"shipped gpu\""}};
    const char* argv[] = {"prog", "--set", "renderer.width=2560", "--set", "renderer.widht=1", "--vsync=false", "game.map"};
    const io::Path user = dir / "user.cfg";
    LoadOptions o = Options(user);
    o.environment = &env;
    o.use_environment = true;
    o.shipped = shipped;
    o.shipped_count = 2;
    o.argv = argv;
    o.argc = 7;
    const LoadReport report = settings.Load(o);
    CHECK(Setting<i64>(app.registry, app.width).Get() == 2560);          // command line beats the user's file
    Resolution r = settings.Provenance(app.mode);
    CHECK(r.value.AsEnum() == 1);                                         // the environment beats the file
    CHECK(StringView(r.layer_name) == "environment");
    CHECK(StringView(r.origin) == "$ENGINE_PRESENT_MODE");
    r = settings.Provenance(app.width);
    CHECK(StringView(r.layer_name) == "command line");
    CHECK(StringView(r.origin) == "--set renderer.width=2560");
    char text[16];
    CHECK(TextSetting(app.registry, app.name).Get(text, sizeof(text)) == 11);
    CHECK(StringView(text) == "shipped gpu");
    CHECK_FALSE(Setting<bool>(app.registry, app.rs).Get());
    REQUIRE(report.problems.Size() == 1);                                 // the misspelled key
    CHECK(StringView(report.problems[0].layer) == "command line");
    CHECK(StringView(report.problems[0].message).Contains("did you mean 'renderer.width'"));
    REQUIRE(settings.CommandLine().rest.Size() == 1);
    CHECK(settings.CommandLine().rest[0].View() == "game.map");
    CHECK(report.layers.Size() == 4);                                     // shipped, user, environment, command line
}

TEST_CASE("Set: validation, capability, unknown keys, text form, and no change for an unchanged value", "[settings][service]") {
    io_test::TempDir dir;
    App app;
    Settings settings(app.registry);
    const io::Path user = dir / "u.cfg";
    settings.Load(Options(user));
    CHECK(settings.Set(app.width, Value::FromInt(1920)).Ok());
    CHECK(Setting<i64>(app.registry, app.width).Get() == 1920);
    SetResult r = settings.Set(app.width, Value::FromInt(99999)); // Reject policy
    CHECK(r.error == SetError::BadValue);
    CHECK(r.value == ValueError::OutOfRange);
    CHECK(Setting<i64>(app.registry, app.width).Get() == 1920);
    r = settings.Set(app.width, Value::FromBool(true));
    CHECK(r.error == SetError::BadValue);
    r = settings.Set(app.mode, Value::FromEnum(7));
    CHECK(r.value == ValueError::NotInEnum);
    r = settings.Set(SettingId{999}, Value::FromInt(1));
    CHECK(r.error == SetError::UnknownKey);
    CHECK(settings.SetText("renderer.nope", "1").error == SetError::UnknownKey);
    CHECK(settings.SetText("renderer.width", "abc").error == SetError::BadValue);
    CHECK(settings.SetText("renderer.present_mode", "mailbox").Ok());
    CHECK(settings.Set(app.a, Value::FromInt(500)).Ok()); // Clamp policy: 100
    CHECK(settings.Snapshot()->Get(app.a).AsInt() == 100);
    // Capability: a non-default value needs it, the default does not.
    struct Caps { bool hdr = false; } caps;
    settings.SetCapabilityCheck([](void* u, const char* c) { return StringView(c) == "hdr" ? static_cast<Caps*>(u)->hdr : true; }, &caps);
    CHECK(settings.Set(app.hdr, Value::FromEnum(1)).error == SetError::Unsupported);
    CHECK(settings.Set(app.hdr, Value::FromEnum(0)).Ok());
    caps.hdr = true;
    CHECK(settings.Set(app.hdr, Value::FromEnum(1)).Ok());
    // An unchanged value changes nothing and calls nobody.
    Recorder rec;
    settings.AddListener("", &Record, &rec);
    const SetResult same = settings.Set(app.width, Value::FromInt(1920));
    CHECK(same.Ok());
    CHECK(same.changed == 0);
    CHECK(rec.calls == 0);
}

TEST_CASE("Listeners: order, prefixes, batches, apply classes, restart flags and re-entrancy", "[settings][service]") {
    io_test::TempDir dir;
    App app;
    Settings settings(app.registry);
    settings.Load(Options(dir / "u.cfg"));
    struct Order {
        Vector<int> seen;
    } order;
    struct Tagged {
        Order* order;
        int tag;
    } t1{&order, 1}, t2{&order, 2};
    auto note = [](void* user, const ChangeEvent&) {
        Tagged* t = static_cast<Tagged*>(user);
        t->order->seen.Push(t->tag);
    };
    settings.AddListener("renderer", note, &t1);
    settings.AddListener("", note, &t2);
    Recorder renderer, pair;
    const u32 pair_id = settings.AddListener("pair", &Record, &pair);
    settings.AddListener("renderer", &Record, &renderer);
    REQUIRE(settings.Set(app.width, Value::FromInt(1920)).Ok());
    REQUIRE(order.seen.Size() == 2);
    CHECK(order.seen[0] == 1); // registration order
    CHECK(order.seen[1] == 2);
    CHECK(pair.calls == 0);    // a different prefix
    CHECK(renderer.highest == 2);
    // A batch is announced once, with the changes that match.
    ChangeRequest batch[] = {{app.a, Value::FromInt(10)}, {app.b, Value::FromInt(90)}, {app.rs, Value::FromBool(false)}, {app.width, Value::FromInt(2560)}};
    const SetResult applied = settings.Apply(batch, 4, nullptr, nullptr);
    CHECK(applied.Ok());
    CHECK(applied.changed == 4);
    CHECK(pair.calls == 1);
    CHECK(pair.log.Size() == 2);
    CHECK(renderer.calls == 2);
    CHECK(renderer.log.Size() == 3); // width (first call) plus vsync and width (second)
    CHECK(renderer.restart);         // vsync is marked restart-required
    settings.RemoveListener(pair_id);
    REQUIRE(settings.Set(app.a, Value::FromInt(11)).Ok());
    CHECK(pair.calls == 1);
    // Re-entrancy: a listener may call Set; the nested call is announced too, and runaway recursion stops at depth 8.
    struct Chain {
        Settings* settings;
        SettingId id;
        int depth = 0;
        int max_seen = 0;
        bool saw_reentrant = false;
    } chain{&settings, app.b};
    settings.AddListener("pair.a", [](void* user, const ChangeEvent& e) {
        Chain* c = static_cast<Chain*>(user);
        ++c->depth;
        if (c->depth > c->max_seen) c->max_seen = c->depth;
        const i64 next = e.changes[0].new_value.AsInt() + 1;
        if (c->settings->Set(SettingId{static_cast<u16>(e.changes[0].id.value)}, Value::FromInt(next % 100)).error == SetError::Reentrant) c->saw_reentrant = true; // changes pair.a again: recursion
        --c->depth;
    }, &chain);
    settings.Set(app.a, Value::FromInt(20));
    CHECK(chain.max_seen == 8);
    CHECK(chain.saw_reentrant);
}

TEST_CASE("Apply: a consumer that rejects the batch rolls it back, values and file", "[settings][service]") {
    io_test::TempDir dir;
    App app;
    Settings settings(app.registry);
    const io::Path user = dir / "u.cfg";
    WriteText(user, "[renderer]\nwidth = 1000\n");
    settings.Load(Options(user));
    Recorder rec;
    settings.AddListener("", &Record, &rec);
    struct Verdict {
        bool accept = false;
        u8 seen_class = 0;
    } verdict;
    ChangeRequest batch[] = {{app.width, Value::FromInt(2000)}, {app.mode, Value::FromEnum(2)}};
    const SetResult rejected = settings.Apply(batch, 2, [](void* u, const ChangeEvent& e) {
        Verdict* v = static_cast<Verdict*>(u);
        v->seen_class = e.highest_apply_class;
        return v->accept;
    }, &verdict);
    CHECK(rejected.error == SetError::ApplyFailed);
    CHECK(verdict.seen_class == 2);
    CHECK(rejected.changed == 0);
    CHECK(Setting<i64>(app.registry, app.width).Get() == 1000);       // rolled back to what the file said
    CHECK(settings.Snapshot()->Get(app.mode).AsEnum() == 0);
    CHECK(rec.calls == 0);                                             // nobody was told
    REQUIRE(settings.Save().Ok());
    CHECK(SlurpText(user).View() == "[renderer]\nwidth = 1000\n");        // and the file is as it was
    verdict.accept = true;
    CHECK(settings.Apply(batch, 2, [](void* u, const ChangeEvent&) { return static_cast<Verdict*>(u)->accept; }, &verdict).Ok());
    CHECK(Setting<i64>(app.registry, app.width).Get() == 2000);
    CHECK(rec.calls == 1);
}

TEST_CASE("Snapshot: a reader on another thread never sees a half-applied batch", "[settings][service][threads]") {
    io_test::TempDir dir;
    App app;
    Settings settings(app.registry);
    settings.Load(Options(dir / "u.cfg"));
    std::atomic<bool> stop{false};
    std::atomic<int> torn{0};
    std::atomic<u64> reads{0};
    Vector<std::thread> readers;
    readers.Resize(3);
    for (usize t = 0; t < readers.Size(); ++t) {
        readers[t] = std::thread([&] {
            while (!stop.load()) {
                SharedPtr<const SettingsSnapshot> s = settings.Snapshot();
                if (s->Get(app.a).AsInt() + s->Get(app.b).AsInt() != 100) torn++;
                reads++;
            }
        });
    }
    for (int i = 0; i < 10000; ++i) {
        ChangeRequest batch[] = {{app.a, Value::FromInt(i % 101)}, {app.b, Value::FromInt(100 - i % 101)}};
        settings.Apply(batch, 2, nullptr, nullptr);
    }
    stop = true;
    for (usize t = 0; t < readers.Size(); ++t) readers[t].join();
    CHECK(torn.load() == 0);
    CHECK(reads.load() > 0);
}

TEST_CASE("Set from several threads: ten thousand changes, handles always see a value some thread set", "[settings][service][threads]") {
    io_test::TempDir dir;
    App app;
    Settings settings(app.registry);
    settings.Load(Options(dir / "u.cfg"));
    std::atomic<int> bad{0};
    Vector<std::thread> threads;
    threads.Resize(4);
    for (usize t = 0; t < threads.Size(); ++t) {
        threads[t] = std::thread([&, t] {
            Setting<i64> width(app.registry, app.width);
            for (int i = 0; i < 2500; ++i) {
                const i64 v = static_cast<i64>(t) * 3000 + i;
                if (!settings.Set(app.width, Value::FromInt(v)).Ok()) bad++;
                const i64 seen = width.Get();
                if (seen < 0 || seen >= 12000) bad++;
            }
        });
    }
    for (usize t = 0; t < threads.Size(); ++t) threads[t].join();
    CHECK(bad.load() == 0);
    CHECK(settings.Snapshot()->generation >= 1);
}

TEST_CASE("Save: only the changed lines of the user's file change, the command line is not saved unless asked, .bak is kept", "[settings][service][save]") {
    io_test::TempDir dir;
    App app;
    Settings settings(app.registry);
    const io::Path user = dir / "u.cfg";
    const char* original = "# my file\n[renderer]\nwidth = 1000   # px\npresent_mode = fifo\n";
    WriteText(user, original);
    const char* argv[] = {"prog", "--set", "renderer.width=3000"};
    LoadOptions o = Options(user);
    o.argv = argv;
    o.argc = 3;
    settings.Load(o);
    CHECK_FALSE(settings.HasUnsavedChanges());
    CHECK(settings.SaveIfChanged().Ok()); // nothing to do
    CHECK(SlurpText(user).View() == StringView(original));
    REQUIRE(settings.SetText("renderer.present_mode", "mailbox").Ok());
    REQUIRE(settings.SetText("renderer.adapter", "\"llvmpipe\"").Ok());
    CHECK(settings.HasUnsavedChanges());
    REQUIRE(settings.Save().Ok());
    CHECK_FALSE(settings.HasUnsavedChanges());
    CHECK(SlurpText(user).View() == "# my file\n[renderer]\nwidth = 1000   # px\npresent_mode = mailbox\nadapter = \"llvmpipe\"\n"); // --set width=3000 is not in the file
    CHECK(SlurpText(dir / "u.cfg.bak").View() == StringView(original));
    CHECK(Setting<i64>(app.registry, app.width).Get() == 3000); // but it still applies this session
    // On request the command line's values are saved too.
    REQUIRE(settings.Save(SaveOptions{true, false}).Ok());
    CHECK(SlurpText(user).View().Contains("width = 3000   # px"));
}

TEST_CASE("Save: a read-only directory is a status, not a crash, and memory is untouched", "[settings][service][save]") {
    if (::geteuid() == 0) {
        SUCCEED("root writes anywhere");
        return;
    }
    io_test::TempDir dir;
    REQUIRE(io::CreateDirectory(dir / "ro").Ok());
    const io::Path user = dir / "ro/u.cfg";
    WriteText(user, "[renderer]\nwidth = 1000\n");
    App app;
    Settings settings(app.registry);
    settings.Load(Options(user));
    REQUIRE(io::SetPermissions(dir / "ro", 0500).Ok());
    REQUIRE(settings.Set(app.width, Value::FromInt(2000)).Ok());
    const Status s = settings.Save();
    CHECK((s.Is(StoreError::PermissionDenied) || s.Is(StoreError::ReadOnly)));
    CHECK(Setting<i64>(app.registry, app.width).Get() == 2000);
    CHECK(settings.HasUnsavedChanges());
    REQUIRE(io::SetPermissions(dir / "ro", 0700).Ok());
    CHECK(settings.Save().Ok());
    CHECK(SlurpText(user).View() == "[renderer]\nwidth = 2000\n");
}

TEST_CASE("A damaged user file: values load, Save refuses, and the explicit replace keeps a backup", "[settings][service][save]") {
    io_test::TempDir dir;
    App app;
    Settings settings(app.registry);
    const io::Path user = dir / "u.cfg";
    const char* damaged = "[renderer]\nwidth = 1500\nthis is broken\n";
    WriteText(user, damaged);
    const LoadReport report = settings.Load(Options(user));
    CHECK(report.layers[0].state == LayerState::Damaged);
    CHECK(Setting<i64>(app.registry, app.width).Get() == 1500);
    REQUIRE(settings.Set(app.width, Value::FromInt(1600)).Ok());
    CHECK(settings.Save().Is(StoreError::Damaged));
    CHECK(SlurpText(user).View() == StringView(damaged));
    SaveOptions replace;
    replace.replace_damaged = true;
    CHECK(settings.Save(replace).Ok());
    CHECK(SlurpText(dir / "u.cfg.bak").View() == StringView(damaged));
    CHECK(SlurpText(user).View() == "[renderer]\nwidth = 1600\n");
}

TEST_CASE("A failing user layer: the report says so, the other layers resolve, Save reports the error", "[settings][service]") {
    App app;
    Settings settings(app.registry);
    settings.Layers().AddLayer("shipped", LayerRole::System, [] {
        auto s = MakeUnique<MemoryConfigStore>();
        StoredValue v;
        v.typed = true;
        v.value = Value::FromInt(800);
        s->Set("renderer.width", v);
        s->Commit(CommitOptions());
        return ConfigStorePtr(static_cast<UniquePtr<MemoryConfigStore>&&>(s));
    }());
    settings.Layers().AddLayer("user", LayerRole::User, MakeUnique<FailingConfigStore>(MakeUnique<MemoryConfigStore>(), 1, StoreError::Io, true));
    const LoadReport report = settings.LoadFromLayers();
    CHECK(report.layers[1].state == LayerState::Failed);
    CHECK(Setting<i64>(app.registry, app.width).Get() == 800);
    const SetResult set = settings.Set(app.width, Value::FromInt(900));
    CHECK(set.error == SetError::Store); // the write layer refused: the value is not published
    CHECK(set.store == StoreError::Io);
    CHECK(Setting<i64>(app.registry, app.width).Get() == 800);
}

TEST_CASE("Autosave: off by default, saves after the quiet time, and SaveOnExit writes at the end of a scope", "[settings][service][save]") {
    io_test::TempDir dir;
    App app;
    Settings settings(app.registry);
    const io::Path user = dir / "u.cfg";
    settings.Load(Options(user));
    REQUIRE(settings.Set(app.width, Value::FromInt(1111)).Ok());
    CHECK_FALSE(settings.PollAutosave()); // off
    settings.SetAutosaveQuietSeconds(0.05);
    CHECK_FALSE(settings.PollAutosave()); // too soon
    std::this_thread::sleep_for(std::chrono::milliseconds(80));
    CHECK(settings.PollAutosave());
    CHECK(SlurpText(user).View() == "[renderer]\nwidth = 1111\n");
    CHECK_FALSE(settings.PollAutosave()); // nothing new
    {
        SaveOnExit guard(settings);
        REQUIRE(settings.Set(app.width, Value::FromInt(2222)).Ok());
    }
    CHECK(SlurpText(user).View() == "[renderer]\nwidth = 2222\n");
}

TEST_CASE("The crash report carries the settings that are not at their default", "[settings][service][crash]") {
    debug::ResetCrashStateForTesting();
    io_test::TempDir dir;
    App app;
    Settings settings(app.registry);
    settings.Load(Options(dir / "u.cfg"));
    REQUIRE(settings.Set(app.width, Value::FromInt(1920)).Ok());
    REQUIRE(settings.SetText("renderer.adapter", "\"llvmpipe\"").Ok());
    struct Collected {
        engine_test::Text text = engine_test::MakeText();
    } collected;
    debug::CrashReportSink sink;
    sink.write = [](void* c, const char* data, usize n) { static_cast<Collected*>(c)->text.Append(StringView(data, n)); };
    sink.context = &collected;
    debug::WriteCrashContext(sink);
    CHECK(collected.text.View().Contains("settings: 2 non-default"));
    CHECK(collected.text.View().Contains("settings 1: "));
    CHECK(collected.text.View().Contains("renderer.width=1920"));
    CHECK(collected.text.View().Contains("renderer.adapter=\"llvmpipe\""));
    debug::ResetCrashStateForTesting();
}

TEST_CASE("A secret setting is not written to the crash report", "[settings][service][crash]") {
    debug::ResetCrashStateForTesting();
    Registry registry;
    SettingDesc token = MakeText("net.token", "");
    token.flags = kSecret;
    const SettingId id = registry.Register(token);
    Settings settings(registry);
    settings.Layers().AddLayer("user", LayerRole::User, MakeUnique<MemoryConfigStore>());
    settings.LoadFromLayers();
    REQUIRE(settings.Set(id, Value::FromText("hunter2")).Ok());
    engine_test::Text text = engine_test::MakeText();
    debug::CrashReportSink sink;
    sink.write = [](void* c, const char* data, usize n) { static_cast<engine_test::Text*>(c)->Append(StringView(data, n)); };
    sink.context = &text;
    debug::WriteCrashContext(sink);
    CHECK(text.View().Contains("net.token=<secret>"));
    CHECK_FALSE(text.View().Contains("hunter2"));
    debug::ResetCrashStateForTesting();
}

TEST_CASE("An accepted change is published only after the apply function accepts it, and a rejected one leaves nothing behind", "[settings][service][regression]") {
    io_test::TempDir dir;
    App app;
    Settings settings(app.registry);
    const io::Path user = dir / "u.cfg";
    const char* original = "[renderer]\nwidth = 99999\n"; // out of the Reject range: the default is used and the line is kept as it is
    WriteText(user, original);
    const LoadReport report = settings.Load(Options(user));
    CHECK(report.problems.Size() == 1);
    CHECK(Setting<i64>(app.registry, app.width).Get() == 1280);
    struct Seen {
        const Registry* registry;
        SettingId id;
        i64 during = -1;
        bool accept = false;
    } seen{&app.registry, app.width};
    ChangeRequest request{app.width, Value::FromInt(2000)};
    SetResult r = settings.Apply(&request, 1, [](void* u, const ChangeEvent& e) {
        Seen* s = static_cast<Seen*>(u);
        s->during = Setting<i64>(*s->registry, s->id).Get(); // handles still show the old value while the consumer decides
        return s->accept;
    }, &seen);
    CHECK(r.error == SetError::ApplyFailed);
    CHECK(seen.during == 1280);
    CHECK(Setting<i64>(app.registry, app.width).Get() == 1280);
    CHECK_FALSE(settings.HasUnsavedChanges()); // a rejected change is not unsaved work
    REQUIRE(settings.Save().Ok());
    CHECK(SlurpText(user).View() == StringView(original)); // the user's original line was put back exactly
    seen.accept = true;
    r = settings.Apply(&request, 1, [](void* u, const ChangeEvent&) { return static_cast<Seen*>(u)->accept; }, &seen);
    CHECK(r.Ok());
    CHECK(Setting<i64>(app.registry, app.width).Get() == 2000);
}

TEST_CASE("Without a write layer a change is in memory only; callbacks may read the snapshot", "[settings][service][regression]") {
    App app;
    Settings settings(app.registry); // no Load, no layers
    struct Probe {
        Settings* settings;
        i64 width_seen = -1;
    } probe{&settings};
    settings.SetCapabilityCheck([](void* u, const char*) {
        Probe* p = static_cast<Probe*>(u);
        p->width_seen = p->settings->Snapshot()->generation; // would deadlock if a callback ran under the data lock
        return true;
    }, &probe);
    CHECK(settings.Set(app.hdr, Value::FromEnum(1)).Ok());
    CHECK(probe.width_seen >= 0);
    const SetResult r = settings.Set(app.width, Value::FromInt(1920));
    CHECK(r.Ok());
    CHECK(Setting<i64>(app.registry, app.width).Get() == 1920);
    CHECK(settings.Save().Is(StoreError::NotFound)); // nothing to write to
}

TEST_CASE("Save with the command line's values: they join the user's file, and a failed save takes them out again", "[settings][service][regression]") {
    if (::geteuid() == 0) {
        SUCCEED("root writes anywhere");
        return;
    }
    io_test::TempDir dir;
    REQUIRE(io::CreateDirectory(dir / "ro").Ok());
    const io::Path user = dir / "ro/u.cfg";
    WriteText(user, "[renderer]\nwidth = 1000\n");
    App app;
    Settings settings(app.registry);
    const char* argv[] = {"prog", "--set", "renderer.width=3000"};
    LoadOptions o = Options(user);
    o.argv = argv;
    o.argc = 3;
    settings.Load(o);
    REQUIRE(io::SetPermissions(dir / "ro", 0500).Ok());
    SaveOptions with_cli;
    with_cli.include_command_line = true;
    CHECK(settings.Save(with_cli).Failed());
    StoredValue sv;
    const usize w = settings.Layers().WriteLayer();
    REQUIRE(settings.Layers().Store(w).Get("renderer.width", &sv).Ok());
    CHECK(sv.value.AsText() == "1000"); // the user's own value, not the command line's 3000
    REQUIRE(io::SetPermissions(dir / "ro", 0700).Ok());
}

TEST_CASE("Autosave does not retry a failing save on every poll", "[settings][service][regression]") {
    App app;
    Settings settings(app.registry);
    auto failing = MakeUnique<FailingConfigStore>(MakeUnique<MemoryConfigStore>(), 1000000, StoreError::Io);
    FailingConfigStore* raw = failing.Get();
    settings.Layers().AddLayer("user", LayerRole::User, ConfigStorePtr(static_cast<UniquePtr<FailingConfigStore>&&>(failing)));
    settings.LoadFromLayers();
    REQUIRE(settings.Set(app.width, Value::FromInt(1500)).Ok());
    settings.SetAutosaveQuietSeconds(0.05);
    std::this_thread::sleep_for(std::chrono::milliseconds(80));
    raw->FailFromNextCall();
    const u32 before = raw->calls();
    CHECK_FALSE(settings.PollAutosave()); // the save is attempted and fails
    const u32 after_first = raw->calls();
    CHECK(after_first > before);
    for (int i = 0; i < 5; ++i) CHECK_FALSE(settings.PollAutosave());
    CHECK(raw->calls() == after_first); // not retried until another quiet period has passed
    CHECK(settings.HasUnsavedChanges());
    std::this_thread::sleep_for(std::chrono::milliseconds(80));
    CHECK_FALSE(settings.PollAutosave());
    CHECK(raw->calls() > after_first);
}
