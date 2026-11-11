#include <catch2/catch_test_macros.hpp>

#include "catch_engine_types.h"
#include "io/file.h"
#include "io/file_system.h"
#include "settings/memory_store.h"
#include "settings/migration.h"
#include "settings/setting.h"
#include "settings/settings.h"
#include "temp_dir.h"
#include "test_text.h"

using namespace engine_placeholder;
using namespace engine_placeholder::settings;

namespace
{

const char* const kDemoModes[] = {"vsync", "mailbox", "immediate"};

// The demo section as released in three schemas. Schema 1 had flat keys (width, height, mode = fifo, legacy); schema 2
// moved them under [demo]; schema 3 renamed the mode value fifo to vsync and added a scale. Fixtures: demo_v1.cfg and
// demo_v2.cfg.
struct Demo
{
    Registry registry;
    SettingId width, height, mode, scale;
    Demo()
    {
        width = registry.Register(MakeInt("demo.width", 1280, 0, 16384));
        height = registry.Register(MakeInt("demo.height", 720, 0, 16384));
        mode = registry.Register(MakeEnum("demo.mode", kDemoModes, 3, 0));
        scale = registry.Register(MakeFloat("demo.scale", 1.0, 0.1, 4.0));
        registry.SetSchemaVersion("demo", 3);
    }
    void AddSteps(MigrationSet& migrations)
    {
        const struct
        {
            const char* old_key;
            const char* new_key;
        } renames[] = {{"width", "demo.width"}, {"height", "demo.height"}, {"mode", "demo.mode"}};
        for (const auto& r : renames)
        {
            MigrationStep step;
            step.section = "demo";
            step.from_version = 1;
            step.op = MigrationOp::Rename;
            step.key = r.old_key;
            step.new_key = r.new_key;
            migrations.Add(step);
        }
        MigrationStep drop;
        drop.section = "demo";
        drop.from_version = 1;
        drop.op = MigrationOp::Drop;
        drop.key = "legacy";
        migrations.Add(drop);
        MigrationStep map;
        map.section = "demo";
        map.from_version = 2;
        map.op = MigrationOp::MapValue;
        map.key = "demo.mode";
        map.old_value = "fifo";
        map.new_value = "vsync";
        migrations.Add(map);
        MigrationStep scale_step;  // a function: files with a width over 1280 and no scale get a scale of 1.5
        scale_step.section = "demo";
        scale_step.from_version = 2;
        scale_step.op = MigrationOp::Callback;
        scale_step.key = "demo.scale";
        scale_step.fn = [](void*, ConfigStore& store)
        {
            StoredValue sv;
            if (store.Get("demo.scale", &sv).Ok() || store.Get("demo.width", &sv).Failed())
            {
                return true;
            }
            if (sv.value.AsText().Length() < 4)
            {
                return true;
            }
            StoredValue scale;
            scale.value = Value::FromText("1.5");
            return store.Set("demo.scale", scale).Ok();
        };
        migrations.Add(scale_step);
    }
};

engine_test::Text SlurpMigration(const io::Path& path)
{
    Vector<u8> bytes;
    if (io::ReadFile(path, &bytes).Failed())
    {
        return engine_test::MakeText("<missing>");
    }
    return engine_test::MakeText(StringView(reinterpret_cast<const char*>(bytes.Data()), bytes.Size()));
}

// Copies a fixture into a temp directory so a test may save over it.
io::Path CopyFixture(io_test::TempDir& dir, const char* fixture)
{
    const io::Path from = io::Path(StringView(TEST_FIXTURE_DIR)).Join(StringView(fixture));
    const io::Path to = dir / StringView(fixture);
    REQUIRE(io::Copy(from, to, true).Ok());
    return to;
}

LoadOptions MigrationOptions(const io::Path& user)
{
    LoadOptions o;
    o.user_file = &user;
    o.use_environment = false;
    return o;
}

bool HasNote(const LoadReport& report, const char* needle)
{
    for (usize i = 0; i < report.notes.Size(); ++i)
    {
        if (StringView(report.notes[i].message).Contains(StringView(needle)))
        {
            return true;
        }
    }
    return false;
}

}  // namespace

TEST_CASE("Migration: demo_v1.cfg (schema 1) loads through every step and is rewritten only when saved",
          "[settings][migration]")
{
    io_test::TempDir dir;
    const io::Path user = CopyFixture(dir, "demo_v1.cfg");
    const engine_test::Text original = SlurpMigration(user);
    Demo demo;
    Settings settings(demo.registry);
    demo.AddSteps(settings.Migrations());
    const LoadReport report = settings.Load(MigrationOptions(user));
    CHECK(Setting<i64>(demo.registry, demo.width).Get() == 1280);
    CHECK(Setting<i64>(demo.registry, demo.height).Get() == 720);
    CHECK(Setting<f64>(demo.registry, demo.scale).Get() == 1.5);  // the migration function added a scale
    CHECK(HasNote(report, "width renamed to demo.width"));
    CHECK(HasNote(report, "legacy: dropped"));
    CHECK(HasNote(report, "demo.mode: value changed to vsync"));
    CHECK(HasNote(report, "demo.scale: migration function ran"));
    CHECK(SlurpMigration(user).View() == original.View());  // the file is untouched by loading
    CHECK_FALSE(settings.HasUnsavedChanges());
    REQUIRE(settings.Save().Ok());
    const engine_test::Text saved = SlurpMigration(user);
    CHECK(saved.View().Contains("schema = 3"));
    CHECK(saved.View().Contains("[demo]"));
    CHECK_FALSE(saved.View().Contains("legacy"));
    CHECK(saved.View().Contains("# demo settings, schema 1"));  // the comment stayed
    // Loading the saved file again migrates nothing.
    Demo again;
    Settings reloaded(again.registry);
    again.AddSteps(reloaded.Migrations());
    const LoadReport second = reloaded.Load(MigrationOptions(user));
    CHECK(second.notes.Empty());
}

TEST_CASE("Migration: demo_v2.cfg (schema 2) only runs the steps from version 2", "[settings][migration]")
{
    io_test::TempDir dir;
    const io::Path user = CopyFixture(dir, "demo_v2.cfg");
    Demo demo;
    Settings settings(demo.registry);
    demo.AddSteps(settings.Migrations());
    const LoadReport report = settings.Load(MigrationOptions(user));
    CHECK(settings.Snapshot()->Get(demo.mode).AsEnum() == 0);
    CHECK_FALSE(HasNote(report, "renamed"));  // the v1 steps did not run
    REQUIRE(settings.Save().Ok());
    CHECK(SlurpMigration(user).View().Contains("schema = 3"));
}

TEST_CASE("Migration: rename chains, value maps, functions, drops and idempotence on a store", "[settings][migration]")
{
    Registry registry;
    registry.Register(MakeInt("chain.z", 0, 0, 100));
    registry.SetSchemaVersion("chain", 4);
    MigrationSet set;
    auto step = [&](u32 from, MigrationOp op, const char* key, const char* to)
    {
        MigrationStep s;
        s.section = "chain";
        s.from_version = from;
        s.op = op;
        s.key = key;
        s.new_key = to;
        set.Add(s);
    };
    step(1, MigrationOp::Rename, "chain.x", "chain.y");  // a -> b -> c across two versions
    step(2, MigrationOp::Rename, "chain.y", "chain.z");
    step(3, MigrationOp::Drop, "chain.gone", nullptr);
    MemoryConfigStore store;
    auto put = [&](const char* k, const char* v)
    {
        StoredValue sv;
        sv.value = Value::FromText(v);
        REQUIRE(store.Set(k, sv).Ok());
    };
    put("chain.x", "7");
    put("chain.gone", "1");
    MigrationReport r1;
    set.Migrate(registry, store, &r1);
    StoredValue sv;
    REQUIRE(store.Get("chain.z", &sv).Ok());
    CHECK(sv.value.AsText() == "7");
    CHECK(store.Get("chain.x", &sv).Is(StoreError::NotFound));
    CHECK(store.Get("chain.y", &sv).Is(StoreError::NotFound));
    CHECK(store.Get("chain.gone", &sv).Is(StoreError::NotFound));
    CHECK(r1.steps_applied == 3);
    CHECK(r1.sections_upgraded == 1);
    REQUIRE(store.Get("chain.schema", &sv).Ok());
    CHECK(sv.value.AsText() == "4");
    // Migrating twice equals migrating once.
    Vector<Key> before;
    store.Enumerate(StringView(), &before);
    MigrationReport r2;
    set.Migrate(registry, store, &r2);
    CHECK(r2.steps_applied == 0);
    CHECK(r2.notes.Empty());
    Vector<Key> after;
    store.Enumerate(StringView(), &after);
    CHECK(before.Size() == after.Size());
    // An old value does not overwrite a newer key.
    MemoryConfigStore both;
    sv.value = Value::FromText("1");
    both.Set("chain.x", sv);
    sv.value = Value::FromText("2");
    both.Set("chain.y", sv);
    MigrationReport r3;
    set.Migrate(registry, both, &r3);
    REQUIRE(both.Get("chain.z", &sv).Ok());
    CHECK(sv.value.AsText() == "2");
    CHECK(both.Get("chain.x", &sv).Is(StoreError::NotFound));
}

TEST_CASE("Migration: a file from the future is read in compatibility mode and never rewritten in an older format",
          "[settings][migration]")
{
    io_test::TempDir dir;
    const io::Path user = dir / "future.cfg";
    const char* text =
        "version = 9\ndemo.schema = 7\n\n[demo]\nwidth = 1920\nhyperdrive = on   # a newer setting\n\n[future]\nthing = 1\n";
    REQUIRE(io::WriteFile(user, text, StringView(text).Length()).Ok());
    Demo demo;
    Settings settings(demo.registry);
    demo.AddSteps(settings.Migrations());
    const LoadReport report = settings.Load(MigrationOptions(user));
    CHECK(Setting<i64>(demo.registry, demo.width).Get() == 1920);  // the known settings apply
    CHECK(settings.UserFileIsNewer());
    CHECK(HasNote(report, "written by a newer version"));
    CHECK(HasNote(report, "unknown key 'demo.hyperdrive'"));
    CHECK(HasNote(report, "unknown key 'future.thing'"));
    REQUIRE(settings.Set(demo.height, Value::FromInt(1080)).Ok());
    CHECK(settings.Save().Is(StoreError::TooNew));
    CHECK(SlurpMigration(user).View() == StringView(text));
    SaveOptions replace;
    replace.replace_newer = true;
    REQUIRE(settings.Save(replace).Ok());
    CHECK(SlurpMigration(dir / "future.cfg.bak").View() == StringView(text));
    // The unknown keys survive the save even then.
    const engine_test::Text saved = SlurpMigration(user);
    CHECK(saved.View().Contains("hyperdrive = on   # a newer setting"));
    CHECK(saved.View().Contains("thing = 1"));
}

TEST_CASE("Unknown keys are kept verbatim and mentioned once, with a suggestion", "[settings][migration]")
{
    io_test::TempDir dir;
    const io::Path user = dir / "u.cfg";
    const char* text = "[demo]\nwidht = 5\nscale = 2.0\nzzzzzzzz = 1\n";
    REQUIRE(io::WriteFile(user, text, StringView(text).Length()).Ok());
    Registry registry;
    registry.Register(MakeInt("demo.width", 1280, 0, 16384));
    registry.Register(MakeFloat("demo.scale", 1.0, 0.1, 4.0));
    Settings settings(registry);
    const LoadReport report = settings.Load(MigrationOptions(user));
    CHECK(HasNote(report, "unknown key 'demo.widht'; did you mean 'demo.width'?"));
    CHECK(HasNote(report, "unknown key 'demo.zzzzzzzz' (kept as it is)"));
    CHECK(report.notes.Size() == 2);
    CHECK(report.Clean());  // an unknown key is not a problem
    REQUIRE(settings.Set(registry.Find("demo.width"), Value::FromInt(1920)).Ok());
    REQUIRE(settings.Save().Ok());
    CHECK(SlurpMigration(user).View().Contains("widht = 5"));
    CHECK(SlurpMigration(user).View().Contains("zzzzzzzz = 1"));
}

TEST_CASE("A deprecated key still resolves, with a warning naming its replacement", "[settings][migration]")
{
    io_test::TempDir dir;
    const io::Path user = dir / "u.cfg";
    const char* text = "[demo]\nres = 1600\n";
    REQUIRE(io::WriteFile(user, text, StringView(text).Length()).Ok());
    Registry registry;
    SettingDesc d = MakeInt("demo.width", 1280, 0, 16384);
    const char* const old[] = {"demo.res"};
    d.deprecated_keys = old;
    d.deprecated_count = 1;
    const SettingId id = registry.Register(d);
    Settings settings(registry);
    const LoadReport report = settings.Load(MigrationOptions(user));
    CHECK(Setting<i64>(registry, id).Get() == 1600);
    REQUIRE(report.problems.Size() == 1);
    CHECK(StringView(report.problems[0].message) == "uses the deprecated key 'demo.res'; it is now 'demo.width'");
    CHECK(report.notes.Empty());  // a deprecated key is known: not an unknown key
}

TEST_CASE("Replacing a newer file leaves a current file: it reloads clean and saves normally",
          "[settings][migration][regression]")
{
    io_test::TempDir dir;
    const io::Path user = dir / "future.cfg";
    const char* text = "version = 9\ndemo.schema = 7\n[demo]\nwidth = 1920\n";
    REQUIRE(io::WriteFile(user, text, StringView(text).Length()).Ok());
    Demo demo;
    Settings settings(demo.registry);
    demo.AddSteps(settings.Migrations());
    settings.Load(MigrationOptions(user));
    REQUIRE(settings.UserFileIsNewer());
    REQUIRE(settings.Set(demo.height, Value::FromInt(1080)).Ok());
    CHECK(settings.Save().Is(StoreError::TooNew));
    CHECK(settings.LastSaveStatus().Is(StoreError::TooNew));  // the application can show why saving is off
    SaveOptions replace;
    replace.replace_newer = true;
    REQUIRE(settings.Save(replace).Ok());
    CHECK_FALSE(settings.UserFileIsNewer());
    CHECK(settings.LastSaveStatus().Ok());
    REQUIRE(settings.Set(demo.height, Value::FromInt(900)).Ok());
    CHECK(settings.Save().Ok());  // a plain save works again
    Demo again;
    Settings reloaded(again.registry);
    again.AddSteps(reloaded.Migrations());
    reloaded.Load(MigrationOptions(user));
    CHECK_FALSE(reloaded.UserFileIsNewer());  // the stamps were overwritten with this program's versions
    CHECK(SlurpMigration(user).View().Contains("version = 1"));
    CHECK(SlurpMigration(user).View().Contains("schema = 3"));
}

TEST_CASE("A failed migration step does not advance the schema, and an unreadable version is treated as newer",
          "[settings][migration][regression]")
{
    Registry registry;
    registry.Register(MakeInt("sec.value", 0, 0, 100));
    registry.SetSchemaVersion("sec", 2);
    struct Counter
    {
        int calls = 0;
        bool succeed = false;
    } counter;
    MigrationSet set;
    MigrationStep step;
    step.section = "sec";
    step.from_version = 1;
    step.op = MigrationOp::Callback;
    step.key = "sec.value";
    step.user = &counter;
    step.fn = [](void* u, ConfigStore&)
    {
        Counter* c = static_cast<Counter*>(u);
        ++c->calls;
        return c->succeed;
    };
    set.Add(step);
    MemoryConfigStore store;
    StoredValue sv;
    sv.value = Value::FromText("5");
    store.Set("sec.value", sv);
    MigrationReport first;
    set.Migrate(registry, store, &first);
    CHECK(counter.calls == 1);
    CHECK(first.sections_upgraded == 0);
    CHECK(StoredSchemaVersion(store, "sec") == 1);  // still the old schema: the next load tries again
    counter.succeed = true;
    MigrationReport second;
    set.Migrate(registry, store, &second);
    CHECK(counter.calls == 2);
    CHECK(StoredSchemaVersion(store, "sec") == 2);
    // A schema or format version that is not a number counts as newer: no step runs, nothing is rewritten.
    MemoryConfigStore garbage;
    sv.value = Value::FromText("abc");
    garbage.Set("sec.schema", sv);
    sv.value = Value::FromText("99999999999");
    garbage.Set("version", sv);
    MigrationReport g;
    counter.calls = 0;
    set.Migrate(registry, garbage, &g);
    CHECK(counter.calls == 0);
    CHECK(g.newer_format);
    CHECK(g.newer_sections.Size() == 1);
    // A value that is not text cannot be renamed: said so, and the schema stays.
    MemoryConfigStore typed;
    StoredValue number;
    number.typed = true;
    number.value = Value::FromInt(5);
    typed.Set("sec.old", number);
    MigrationSet renames;
    MigrationStep rename;
    rename.section = "sec";
    rename.from_version = 1;
    rename.op = MigrationOp::Rename;
    rename.key = "sec.old";
    rename.new_key = "sec.value";
    renames.Add(rename);
    MigrationReport t;
    renames.Migrate(registry, typed, &t);
    CHECK(t.sections_upgraded == 0);
    CHECK(t.notes.Size() == 2);
    CHECK(StoredSchemaVersion(typed, "sec") == 1);
}

TEST_CASE("A new install is stamped, not migrated; reserved keys cannot be settings; an unknown key is mentioned once",
          "[settings][migration][regression]")
{
    io_test::TempDir dir;
    struct Counter
    {
        int calls = 0;
    } counter;
    Demo demo;
    Settings settings(demo.registry);
    MigrationStep step;
    step.section = "demo";
    step.from_version = 1;
    step.op = MigrationOp::Callback;
    step.key = "demo.width";
    step.user = &counter;
    step.fn = [](void* u, ConfigStore&)
    {
        ++static_cast<Counter*>(u)->calls;
        return true;
    };
    settings.Migrations().Add(step);
    const io::Path user = dir / "fresh.cfg";  // does not exist
    const LoadReport report = settings.Load(MigrationOptions(user));
    CHECK(counter.calls == 0);
    CHECK(report.notes.Empty());
    REQUIRE(settings.Set(demo.width, Value::FromInt(1920)).Ok());
    REQUIRE(settings.Save().Ok());
    CHECK(SlurpMigration(user).View().Contains("version = 1"));
    CHECK(SlurpMigration(user).View().Contains("schema = 3"));
    // Reserved names.
    Registry registry;
    RegisterError e = RegisterError::None;
    CHECK_FALSE(registry.Register(MakeInt("version", 0, 0, 1), &e).IsValid());
    CHECK(e == RegisterError::BadKey);
    CHECK_FALSE(registry.Register(MakeInt("renderer.schema", 0, 0, 1), &e).IsValid());
    CHECK(e == RegisterError::BadKey);
    // The same unknown key in two files: one note.
    const char* a = "[x]\nmystery = 1\n";
    REQUIRE(io::WriteFile(dir / "a.cfg", a, StringView(a).Length()).Ok());
    REQUIRE(io::WriteFile(dir / "b.cfg", a, StringView(a).Length()).Ok());
    Registry reg2;
    reg2.Register(MakeInt("x.known", 0, 0, 1));
    Settings s2(reg2);
    const io::Path project = dir / "b.cfg";
    const io::Path first_file = dir / "a.cfg";
    LoadOptions o = MigrationOptions(first_file);
    o.project_file = &project;
    const LoadReport r2 = s2.Load(o);
    CHECK(r2.notes.Size() == 1);
}
