#include <catch2/catch_test_macros.hpp>

#include "catch_engine_types.h"
#include "log/log.h"
#include "settings/engine_settings.h"
#include "settings/memory_store.h"
#include "settings/report.h"
#include "settings/setting.h"
#include "test_text.h"
#include "temp_dir.h"
#include "io/file.h"
#include "build_defs.h"

using namespace engine_placeholder;
using namespace engine_placeholder::settings;

namespace
{
LogChannel g_test_channel("TestSettingsChannel", LogVerbosity::Warn);
}

TEST_CASE("The engine settings are declared with their old environment variables", "[settings][engine]")
{
    Registry registry;
    RegisterEngineSettings(&registry);
    const char* const keys[] = {"platform.window_backend", "rhi.pipeline_cache", "rhi.pipeline_cache_dir"};
    for (const char* k : keys)
    {
        CHECK(registry.Find(k).IsValid());
    }
    CHECK(registry.Count() == 3);  // only settings something reads
    EnvironmentSnapshot env;
    env.Add("ENGINE_WINDOW_BACKEND", "x11");
    env.Add("ENGINE_RHI_CACHE_DIR", "/tmp/cache");
    registry.Freeze();
    Settings settings(registry);
    LoadOptions o;
    o.environment = &env;
    o.argc = 0;
    settings.Layers().AddLayer("user", LayerRole::User, MakeUnique<MemoryConfigStore>());
    settings.Layers().AddLayer("environment", LayerRole::Environment, MakeUnique<EnvironmentSource>(registry, env));
    const LoadReport report = settings.LoadFromLayers();
    CHECK(report.Clean());
    CHECK(settings.Snapshot()->Get(registry.Find("platform.window_backend")).AsEnum() == 2);
    CHECK(settings.Snapshot()->Get(registry.Find("rhi.pipeline_cache_dir")).AsText() == "/tmp/cache");
    const Resolution r = settings.Provenance(registry.Find("rhi.pipeline_cache_dir"));
    CHECK(StringView(r.origin) == "$ENGINE_RHI_CACHE_DIR");
}

TEST_CASE("Log channels are settings: the file sets them at load and a change reaches the channel",
          "[settings][engine][log]")
{
    Registry registry;
    RegisterLogSettings(&registry);
    const SettingId verbosity = registry.Find("log.testsettingschannel.verbosity");
    const SettingId enabled = registry.Find("log.testsettingschannel.enabled");
    REQUIRE(verbosity.IsValid());
    REQUIRE(enabled.IsValid());
    CHECK(registry.Desc(verbosity)->default_value.AsEnum() ==
          static_cast<u32>(LogVerbosity::Warn));  // the channel's own default
    io_test::TempDir dir;
    const char* text = "[log.testsettingschannel]\nverbosity = debug\nenabled = false\n";
    const io::Path user = dir / "u.cfg";
    REQUIRE(io::WriteFile(user, text, StringView(text).Length()).Ok());
    Settings settings(registry);
    LoadOptions o;
    o.user_file = &user;
    o.use_environment = false;
    settings.Load(o);
    AttachLogSettings(&settings);
    CHECK(g_test_channel.Verbosity() == LogVerbosity::Debug);  // from the file
    CHECK_FALSE(g_test_channel.Enabled());
    REQUIRE(settings.SetText("log.testsettingschannel.verbosity", "error").Ok());
    REQUIRE(settings.SetText("log.testsettingschannel.enabled", "true").Ok());
    CHECK(g_test_channel.Verbosity() == LogVerbosity::Error);  // through the listener
    CHECK(g_test_channel.Enabled());
    g_test_channel.SetVerbosity(LogVerbosity::Warn);
    g_test_channel.SetEnabled(true);
}

TEST_CASE("The listing and the help are generated from the registry", "[settings][engine]")
{
    Registry registry;
    RegisterEngineSettings(&registry);
    SettingDesc secret = MakeText("net.token", "");
    secret.flags = kSecret;
    secret.description = "an access token";
    registry.Register(secret);
    SettingDesc hidden = MakeBool("internal.flag", false);
    hidden.flags = kHidden;
    registry.Register(hidden);
    SettingDesc g = MakeText("renderer.adapter", "");
    g.flag = "gpu";
    g.env = "ENGINE_RHI_GPU";
    g.description = "the GPU";
    registry.Register(g);
    Settings settings(registry);
    settings.Layers().AddLayer("user", LayerRole::User, MakeUnique<MemoryConfigStore>());
    settings.LoadFromLayers();
    REQUIRE(settings.SetText("net.token", "\"hunter2\"").Ok());
    String<HeapAllocator> listing(HeapAllocator::Default());
    FormatSettingsListing(settings, &listing);
    CHECK(listing.View().Contains("rhi.pipeline_cache_dir"));
    CHECK(listing.View().Contains("<secret>"));
    CHECK_FALSE(listing.View().Contains("hunter2"));
    CHECK_FALSE(listing.View().Contains("internal.flag"));
    String<HeapAllocator> help(HeapAllocator::Default());
    FormatSettingsHelp(registry, &help);
    CHECK(help.View().Contains("--gpu TEXT"));
    CHECK(help.View().Contains("renderer.adapter: the GPU"));
    CHECK(help.View().Contains("ENGINE_RHI_GPU"));
    CHECK(help.View().Contains("--backend NAME"));
    CHECK(help.View().Contains("ENGINE_SETTING_<KEY>"));
}

TEST_CASE("SaveSettingsTo writes only what differs from the defaults and the file reproduces the session",
          "[settings][engine]")
{
    io_test::TempDir dir;
    Registry registry;
    RegisterEngineSettings(&registry);
    Settings settings(registry);
    settings.Layers().AddLayer("user", LayerRole::User, MakeUnique<MemoryConfigStore>());
    settings.LoadFromLayers();
    REQUIRE(settings.SetText("rhi.pipeline_cache", "false").Ok());
    REQUIRE(settings.SetText("platform.window_backend", "x11").Ok());
    const io::Path path = dir / "sub/session.cfg";
    REQUIRE(SaveSettingsTo(settings, path).Ok());
    Vector<u8> bytes;
    REQUIRE(io::ReadFile(path, &bytes).Ok());
    const StringView text(reinterpret_cast<const char*>(bytes.Data()), bytes.Size());
    CHECK(text.Contains("pipeline_cache = false"));
    CHECK(text.Contains("window_backend = x11"));
    CHECK_FALSE(text.Contains("pipeline_cache_dir"));  // a default is not written
    CHECK(text.Contains("version = 1"));
    Registry again;
    RegisterEngineSettings(&again);
    Settings reloaded(again);
    LoadOptions o;
    o.user_file = &path;
    o.use_environment = false;
    reloaded.Load(o);
    CHECK_FALSE(reloaded.Snapshot()->Get(again.Find("rhi.pipeline_cache")).AsBool());
    CHECK(reloaded.Snapshot()->Get(again.Find("platform.window_backend")).AsEnum() == 2);
}

TEST_CASE("A secret is not shown by --set provenance and not saved to a session file; the file is private",
          "[settings][engine][regression]")
{
    io_test::TempDir dir;
    Registry registry;
    SettingDesc token = MakeText("net.token", "");
    token.flags = kSecret;
    const SettingId id = registry.Register(token);
    Settings settings(registry);
    const char* argv[] = {"prog", "--set", "net.token=hunter2"};
    LoadOptions o;
    o.use_environment = false;
    o.argv = argv;
    o.argc = 3;
    const io::Path user = dir / "u.cfg";
    o.user_file = &user;
    settings.Load(o);
    CHECK(settings.Snapshot()->Get(id).AsText() == "hunter2");
    String<HeapAllocator> listing(HeapAllocator::Default());
    FormatSettingsListing(settings, &listing);
    CHECK(listing.View().Contains("<secret>"));
    CHECK_FALSE(listing.View().Contains("hunter2"));
    CHECK(listing.View().Contains("from command line"));
    const io::Path path = dir / "session.cfg";
    REQUIRE(SaveSettingsTo(settings, path).Ok());
    Vector<u8> bytes;
    REQUIRE(io::ReadFile(path, &bytes).Ok());
    CHECK_FALSE(StringView(reinterpret_cast<const char*>(bytes.Data()), bytes.Size()).Contains("hunter2"));
#if !ENGINE_PLATFORM_WINDOWS  // Windows has no file modes: a file takes its directory's ACL, private in the user's own
                              // folders (io/BACKEND.md, CreateTempFile)
    io::FileInfo info;
    REQUIRE(io::Stat(path, &info).Ok());
    CHECK((info.permissions & 0777) == 0600);
#endif
}

TEST_CASE("SaveSettingsTo does not overwrite a file it cannot read", "[settings][engine][regression]")
{
    io_test::TempDir dir;
    Registry registry;
    RegisterEngineSettings(&registry);
    Settings settings(registry);
    settings.Layers().AddLayer("user", LayerRole::User, MakeUnique<MemoryConfigStore>());
    settings.LoadFromLayers();
    REQUIRE(settings.SetText("rhi.pipeline_cache", "false").Ok());
    const io::Path path = dir / "big.cfg";
    const engine_test::Text huge = engine_test::Repeated(9 * 1024 * 1024, '#');  // over the store's 8 MiB read limit
    REQUIRE(io::WriteFile(path, huge.CStr(), huge.Length()).Ok());
    CHECK(SaveSettingsTo(settings, path).Is(StoreError::TooLong));
    io::FileInfo info;
    REQUIRE(io::Stat(path, &info).Ok());
    CHECK(info.size == huge.Length());
}

TEST_CASE(
    "Log settings: registering again reuses the key strings, and a channel set in code keeps its setting unless a layer says otherwise",
    "[settings][engine][log]")
{
    Registry first, second;
    RegisterLogSettings(&first);
    const char* key_before = first.KeyOf(first.Find("log.testsettingschannel.verbosity")).View().Data();
    RegisterLogSettings(&second);
    CHECK(second.Find("log.testsettingschannel.verbosity").IsValid());
    CHECK(first.KeyOf(first.Find("log.testsettingschannel.verbosity")).View().Data() ==
          key_before);                                // the first registry still reads valid text
    g_test_channel.SetVerbosity(LogVerbosity::Info);  // configured in code after the defaults were captured
    io_test::TempDir dir;
    Settings settings(second);
    LoadOptions o;
    const io::Path user = dir / "none.cfg";
    o.user_file = &user;
    o.use_environment = false;
    settings.Load(o);
    AttachLogSettings(&settings);
    CHECK(g_test_channel.Verbosity() == LogVerbosity::Info);  // no layer set it: the code's choice stands
    g_test_channel.SetVerbosity(LogVerbosity::Warn);
}
