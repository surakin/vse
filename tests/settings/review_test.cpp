// Regression tests for the Phase 11 end-of-phase code review (Day 139): each case fails without its fix.
#include <catch2/catch_test_macros.hpp>

#include <atomic>
#include <thread>

#include "catch_engine_types.h"
#include "io/file.h"
#include "settings/config_text.h"
#include "settings/file_store.h"
#include "settings/memory_store.h"
#include "settings/migration.h"
#include "settings/report.h"
#include "settings/settings.h"
#include "temp_dir.h"
#include "test_text.h"

using namespace engine_placeholder;
using namespace engine_placeholder::settings;

namespace {

engine_test::Text RvSlurp(const io::Path& path) {
    Vector<u8> bytes;
    if (io::ReadFile(path, &bytes).Failed()) return engine_test::MakeText("<missing>");
    return engine_test::MakeText(StringView(reinterpret_cast<const char*>(bytes.Data()), bytes.Size()));
}

void RvWrite(const io::Path& path, StringView text) { REQUIRE(io::WriteFile(path, text.Data(), text.Length()).Ok()); }

struct RvListenerState {
    Settings* settings = nullptr;
    u32 second_id = 0;
    int first_calls = 0;
    int second_calls = 0;
};

} // namespace

TEST_CASE("A listener removed by an earlier listener is not called for the same change", "[settings][review]") {
    Registry registry;
    const SettingId id = registry.Register(MakeInt("rv.a", 1, 0, 10));
    Settings settings(registry);
    settings.Layers().AddLayer("user", LayerRole::User, MakeUnique<MemoryConfigStore>());
    settings.LoadFromLayers();
    RvListenerState state;
    state.settings = &settings;
    settings.AddListener("rv", [](void* user, const ChangeEvent&) {
        RvListenerState* s = static_cast<RvListenerState*>(user);
        ++s->first_calls;
        s->settings->RemoveListener(s->second_id);
    }, &state);
    state.second_id = settings.AddListener("rv", [](void* user, const ChangeEvent&) { ++static_cast<RvListenerState*>(user)->second_calls; }, &state);
    REQUIRE(settings.Set(id, Value::FromInt(5)).Ok());
    CHECK(state.first_calls == 1);
    CHECK(state.second_calls == 0);
}

TEST_CASE("Unsetting or minimally writing a renamed setting also removes the deprecated name", "[settings][review]") {
    Registry registry;
    SettingDesc d = MakeInt("rv.new_name", 7, 0, 100);
    const char* const old[] = {"rv.old_name"};
    d.deprecated_keys = old;
    d.deprecated_count = 1;
    const SettingId id = registry.Register(d);
    LayerStack stack;
    stack.AddLayer("user", LayerRole::User, MakeUnique<MemoryConfigStore>());
    StoredValue sv;
    sv.typed = true;
    sv.value = Value::FromInt(5);
    stack.Store(0).Set("rv.old_name", sv);
    stack.Store(0).Commit(CommitOptions());
    stack.LoadAll(nullptr);
    Resolution r;
    stack.Resolve(registry, id, &r, nullptr);
    REQUIRE(r.value.AsInt() == 5);
    REQUIRE(stack.Set(*&registry, id, Value::FromInt(7), true).Ok()); // back to the default: minimal writing removes the value
    stack.Resolve(registry, id, &r, nullptr);
    CHECK(r.value.AsInt() == 7);
    stack.Store(0).Set("rv.old_name", sv);
    REQUIRE(stack.Unset(registry, id).Ok());
    stack.Resolve(registry, id, &r, nullptr);
    CHECK(r.value.AsInt() == 7);
}

TEST_CASE("Reserved keys and keys that share an environment variable cannot be registered", "[settings][review]") {
    Registry registry;
    RegisterError error = RegisterError::None;
    SettingDesc d = MakeInt("rv.x", 0, 0, 1);
    const char* const reserved[] = {"version"};
    d.deprecated_keys = reserved;
    d.deprecated_count = 1;
    CHECK_FALSE(registry.Register(d, &error).IsValid());
    CHECK(error == RegisterError::BadKey);
    CHECK_FALSE(registry.Register(MakeInt("rv.a__b", 0, 0, 1), &error).IsValid());
    CHECK(error == RegisterError::BadKey);
}

TEST_CASE("Text must be UTF-8 and its stored form must fit", "[settings][review]") {
    const SettingDesc text = MakeText("rv.text", "");
    Value v;
    CHECK(ParseValue(text, "\"\\xe9\"", &v) == ValueError::BadEscape);        // a lone Latin-1 byte
    CHECK(ParseValue(text, "\"caf\xc3\xa9\"", &v) == ValueError::None);
    engine_test::Text backslashes = engine_test::MakeText();
    for (int i = 0; i < 300; ++i) backslashes.Append('\\');                    // 300 bytes, 602 once escaped and quoted
    CHECK(ValidateValue(text, Value::FromText(backslashes.View()), &v) == ValueError::TooLong);
    engine_test::Text plain = engine_test::MakeText();
    for (int i = 0; i < 300; ++i) plain.Append('a');
    CHECK(ValidateValue(text, Value::FromText(plain.View()), &v) == ValueError::None);
}

TEST_CASE("A secret given on the command line is not written by Save", "[settings][review]") {
    io_test::TempDir dir;
    Registry registry;
    SettingDesc token = MakeText("rv.token", "");
    token.flags = kSecret;
    registry.Register(token);
    registry.Register(MakeInt("rv.n", 1, 0, 10));
    Settings settings(registry);
    const io::Path user = dir / "u.cfg";
    LoadOptions options;
    options.app_name = "rv";
    options.user_file = &user;
    options.use_environment = false;
    const char* argv[] = {"prog", "--set", "rv.token=hunter2", "--set", "rv.n=4"};
    options.argc = 5;
    options.argv = argv;
    settings.Load(options);
    SaveOptions save;
    save.include_command_line = true;
    REQUIRE(settings.Save(save).Ok());
    const engine_test::Text text = RvSlurp(user);
    CHECK(text.View().Contains("n = 4"));
    CHECK_FALSE(text.View().Contains("hunter2"));
}

TEST_CASE("A --settings file that does not exist is a reported problem", "[settings][review]") {
    io_test::TempDir dir;
    Registry registry;
    registry.Register(MakeInt("rv.n", 1, 0, 10));
    Settings settings(registry);
    const io::Path user = dir / "u.cfg";
    LoadOptions options;
    options.app_name = "rv";
    options.user_file = &user;
    options.use_environment = false;
    const char* argv[] = {"prog", "--settings", "/nonexistent-dir-for-test/missing.cfg"};
    options.argc = 3;
    options.argv = argv;
    const LoadReport report = settings.Load(options);
    bool found = false;
    for (usize i = 0; i < report.problems.Size(); ++i) found = found || StringView(report.problems[i].message).Contains("no such file");
    CHECK(found);
}

TEST_CASE("A value map matches a text setting stored quoted", "[settings][review]") {
    Registry registry;
    registry.Register(MakeText("rv.mode", ""));
    registry.SetSchemaVersion("rv", 2);
    MigrationSet set;
    MigrationStep step;
    step.section = "rv";
    step.from_version = 1;
    step.op = MigrationOp::MapValue;
    step.key = "rv.mode";
    step.old_value = "legacy";
    step.new_value = "modern";
    set.Add(step);
    MemoryConfigStore store;
    StoredValue sv;
    sv.typed = false;
    sv.value = Value::FromText("\"legacy\"");
    store.Set("rv.mode", sv);
    sv.value = Value::FromText("1");
    store.Set("rv.schema", sv);
    MigrationReport report;
    set.Migrate(registry, store, &report);
    StoredValue after;
    REQUIRE(store.Get("rv.mode", &after).Ok());
    CHECK(after.value.AsText() == StringView("\"modern\""));
}

TEST_CASE("Removing the only entry of a section the class added removes the section again", "[settings][review]") {
    ConfigDocument doc;
    ConfigReport report;
    doc.Parse("a = 1\n[old]\nb = 2\n", &report);
    Vector<char> before;
    doc.Serialize(&before);
    REQUIRE(doc.Set("fresh.value", "3"));
    REQUIRE(doc.Remove("fresh.value"));
    Vector<char> after;
    doc.Serialize(&after);
    REQUIRE(after.Size() == before.Size());
    CHECK(StringView(after.Data(), after.Size()) == StringView(before.Data(), before.Size()));
}

TEST_CASE("The backup keeps the file as it was found, not the previous save", "[settings][review]") {
    io_test::TempDir dir;
    const io::Path path = dir / "s.cfg";
    RvWrite(path, "n = 1\n");
    FileConfigStore store(path);
    REQUIRE(store.Load().Ok());
    StoredValue sv;
    sv.value = Value::FromText("2");
    REQUIRE(store.Set("n", sv).Ok());
    REQUIRE(store.Commit(CommitOptions()).Ok());
    sv.value = Value::FromText("3");
    REQUIRE(store.Set("n", sv).Ok());
    REQUIRE(store.Commit(CommitOptions()).Ok());
    CHECK(RvSlurp(dir / "s.cfg.bak").View() == "n = 1\n");
    CHECK(RvSlurp(path).View() == "n = 3\n");
}

TEST_CASE("Saving a session does not downgrade the version stamps of a newer file", "[settings][review]") {
    io_test::TempDir dir;
    Registry registry;
    registry.Register(MakeInt("rv.n", 1, 0, 10));
    Settings settings(registry);
    settings.Layers().AddLayer("user", LayerRole::User, MakeUnique<MemoryConfigStore>());
    settings.LoadFromLayers();
    const io::Path path = dir / "newer.cfg";
    RvWrite(path, "version = 99\n");
    CHECK(SaveSettingsTo(settings, path).Is(StoreError::TooNew));
    CHECK(RvSlurp(path).View() == "version = 99\n");
}

TEST_CASE("LastSaveStatus can be read while another thread saves", "[settings][review][thread]") {
    io_test::TempDir dir;
    Registry registry;
    const SettingId id = registry.Register(MakeInt("rv.n", 1, 0, 1000));
    Settings settings(registry);
    const io::Path user = dir / "u.cfg";
    LoadOptions options;
    options.app_name = "rv";
    options.user_file = &user;
    options.use_environment = false;
    settings.Load(options);
    std::atomic<bool> stop{false};
    std::atomic<int> reads{0};
    std::thread reader([&] {
        while (!stop.load()) {
            const Status s = settings.LastSaveStatus();
            (void)s;
            reads.fetch_add(1);
        }
    });
    for (int i = 0; i < 50; ++i) {
        settings.Set(id, Value::FromInt(2 + i));
        settings.Save();
    }
    stop.store(true);
    reader.join();
    CHECK(reads.load() > 0);
}
