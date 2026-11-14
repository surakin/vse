// Phase 11 Day 137: the rendering settings in the shared settings system: declarations, loading and layering, the apply
// hook, rollback, saving, and the schema-1 file of Phase 8.
#include <catch2/catch_test_macros.hpp>

#include "cstr/number.h"

#include "catch_engine_types.h"
#include "io/file.h"
#include "io/file_system.h"
#include "renderer/device_resources.h"
#include "renderer/renderer.h"
#include "renderer/shared_settings.h"
#include "rhi/device_host.h"
#include "rhi/rhi_log.h"
#include "settings/setting.h"
#include "temp_dir.h"
#include "test_text.h"

using namespace engine_placeholder;
namespace rs = engine_placeholder::renderer;
namespace st = engine_placeholder::settings;

namespace
{

struct QuietLog
{
    bool saved = rhi::LogRhi.Enabled();
    QuietLog() { rhi::LogRhi.SetEnabled(false); }
    ~QuietLog() { rhi::LogRhi.SetEnabled(saved); }
};

struct Owner final : rs::Recreatable
{
    int fail_next = 0;
    bool live = true;
    void Release() override { live = false; }
    bool Recreate(rhi::Device*) override
    {
        if (fail_next > 0)
        {
            --fail_next;
            return false;
        }
        live = true;
        return true;
    }
    bool Live() const override { return live; }
    const char* RecreatableName() const override { return "owner"; }
};

void Clear(const rs::PassExecuteContext& context, void* user)
{
    const rs::GraphResource* target = static_cast<const rs::GraphResource*>(user);
    rhi::RenderingDesc rd;
    rd.color[0].texture = context.graph->ResolvedTexture(*target);
    rd.color_count = 1;
    context.list->BeginRendering(rd);
    context.list->EndRendering();
}

bool Build(const rs::FrameContext& context, void* user)
{
    rs::GraphResource* target = static_cast<rs::GraphResource*>(user);
    *target = context.target;
    context.graph->AddPass("clear", &Clear, target).Write(*target, rs::GraphUsage::ColorAttachment);
    return true;
}

// A renderer on the null backend whose initial settings come from the shared system.
struct Rig
{
    st::Registry registry;
    rs::DeviceResources resources;
    Owner owner;
    rhi::DeviceHost host;
    rhi::DeviceCreateInfo info;
    rs::GraphResource target;
    UniquePtr<rs::Renderer> renderer;

    Rig() { rs::RegisterSharedSettings(&registry); }
    bool Create(const rs::RenderSettings& initial_in)
    {
        rs::RenderSettings initial = initial_in;
        initial.SetEnum(rs::settings::kBackend, 0);
        info.backend = rhi::Backend::Null;
        host.AddObserver(&resources, 0);
        rhi::SwapchainDesc sc;
        sc.width = 64;
        sc.height = 48;
        if (!host.Create(info, sc))
        {
            return false;
        }
        resources.Register(&owner, rs::tiers::kMaterials);
        rs::RendererDesc rd;
        rd.device = host.GetDevice();
        rd.swapchain = host.GetSwapchain();
        rd.host = &host;
        rd.create_info = info;
        rd.initial_settings = &initial;
        rd.resources = &resources;
        rd.recovery.max_attempts = 0;
        rd.build = &Build;
        rd.build_user = &target;
        renderer = MakeUnique<rs::Renderer>(rd);
        return true;
    }
};

st::LoadOptions Options(const io::Path& user)
{
    st::LoadOptions o;
    o.user_file = &user;
    o.use_environment = false;
    return o;
}

engine_test::Text Slurp(const io::Path& path)
{
    Vector<u8> bytes;
    if (io::ReadFile(path, &bytes).Failed())
    {
        return engine_test::MakeText("<missing>");
    }
    return engine_test::MakeText(StringView(reinterpret_cast<const char*>(bytes.Data()), bytes.Size()));
}

}  // namespace

TEST_CASE(
    "The 15 rendering settings are declared under renderer. with the same types, defaults, ranges and apply classes",
    "[shared_settings]")
{
    Rig rig;
    const rs::SettingsRegistry& builtin = rs::SettingsRegistry::Builtin();
    REQUIRE(rig.registry.Count() == builtin.Count());
    CHECK(rig.registry.Count() == 15);
    CHECK(rig.registry.SchemaVersion("renderer") == rs::kRendererSchemaVersion);
    for (u32 i = 1; i <= builtin.Count(); ++i)
    {
        const rs::SettingDesc& old = *builtin.Find(rs::SettingId{static_cast<u16>(i)});
        const engine_test::Text key = engine_test::Format("renderer.%s", old.name);
        const st::SettingId id = rig.registry.Find(key.View());
        INFO(old.name);
        REQUIRE(id.IsValid());
        const st::SettingDesc& d = *rig.registry.Desc(id);
        CHECK(d.apply_class == static_cast<u8>(old.cost));
        CHECK(((d.flags & st::kRestartRequired) != 0) == old.restart_required);  // renderer.backend: chosen at start
        CHECK(((d.capability == nullptr) == (old.capability == nullptr)));
        // The text of the default is what the old registry prints for it.
        rs::RenderSettings defaults;
        char old_text[128];
        defaults.ValueToString(rs::SettingId{static_cast<u16>(i)}, old_text, sizeof(old_text));
        char new_text[st::kMaxValueTextBytes];
        REQUIRE(st::FormatValue(d, d.default_value, new_text, sizeof(new_text)) > 0);
        if (old.type == rs::SettingType::Text)
        {
            CHECK(StringView(new_text) == engine_test::Concat("\"", StringView(old_text), "\"").View());
        }
        else if (old.type == rs::SettingType::Float)
        {
            CHECK(CStrToDouble(new_text, nullptr) == CStrToDouble(old_text, nullptr));
        }
        else
        {
            CHECK(StringView(new_text) == StringView(old_text));
        }
    }
}

TEST_CASE("A file with [renderer] keys loads, layers, applies at start-up and saves a changed value in place",
          "[shared_settings]")
{
    QuietLog quiet;
    io_test::TempDir dir;
    const io::Path user = dir / "settings.cfg";
    const char* text =
        "# my renderer\nversion = 1\nrenderer.schema = 2\n\n[renderer]\npresent_mode = mailbox   # smooth\nwidth = 96\nheight = 64\n";
    REQUIRE(io::WriteFile(user, text, StringView(text).Length()).Ok());
    Rig rig;
    st::Settings settings(rig.registry);
    rs::AddRendererMigrations(&settings.Migrations());
    const st::LoadReport report = settings.Load(Options(user));
    CHECK(report.Clean());
    const rs::RenderSettings from_file = rs::RenderSettingsFromSnapshot(rig.registry, *settings.Snapshot());
    CHECK(from_file.GetEnum(rs::settings::kPresentMode) == 1);
    CHECK(from_file.GetInt(rs::settings::kWidth) == 96);
    REQUIRE(rig.Create(from_file));
    // The rig runs the null backend while the file leaves the default (auto): the backend takes effect at start, so the
    // sync keeps auto.
    CHECK_FALSE(rs::SyncSharedFromRenderer(rig.renderer.Get(), &settings));
    CHECK(rig.renderer->Settings().GetInt(rs::settings::kWidth) == 96);  // applied at start-up
    settings.SetCapabilityCheck(&rs::RendererCapability, rig.renderer.Get());
    // A Dynamic change through Settings reaches the renderer with the right cost class.
    const st::SettingId layer = rig.registry.Find("renderer.gbuffer_debug_layer");
    st::ChangeRequest request{layer, st::Value::FromEnum(2)};
    const st::SetResult set = settings.Apply(&request, 1, &rs::ApplySharedChange, rig.renderer.Get());
    CHECK(set.Ok());
    CHECK(rig.renderer->Settings().GetEnum(rs::settings::kGBufferDebugLayer) == 2);
    CHECK(rig.renderer->LastApply().performed == rs::CostClass::Dynamic);
    // A Swapchain change as well.
    st::ChangeRequest size{rig.registry.Find("renderer.width"), st::Value::FromInt(128)};
    CHECK(settings.Apply(&size, 1, &rs::ApplySharedChange, rig.renderer.Get()).Ok());
    CHECK(rig.renderer->LastApply().performed == rs::CostClass::Swapchain);
    // Saving changes only the changed lines of the file.
    REQUIRE(settings.Save().Ok());
    CHECK(
        Slurp(user).View() ==
        "# my renderer\nversion = 1\nrenderer.schema = 2\n\n[renderer]\npresent_mode = mailbox   # smooth\nwidth = 128\nheight = 64\ngbuffer_debug_layer = normal\n");  // no backend line: the rig's null backend is not written back
}

TEST_CASE("A change the renderer cannot apply is rolled back through the settings system", "[shared_settings]")
{
    QuietLog quiet;
    io_test::TempDir dir;
    Rig rig;
    st::Settings settings(rig.registry);
    settings.Load(Options(dir / "settings.cfg"));
    REQUIRE(rig.Create(rs::RenderSettings()));
    rs::SyncSharedFromRenderer(rig.renderer.Get(), &settings);
    REQUIRE(settings.Save().Ok());
    settings.SetCapabilityCheck(&rs::RendererCapability, rig.renderer.Get());
    rig.owner.fail_next = 1;  // the device reset below cannot rebuild its owner
    const st::SettingId frames = rig.registry.Find("renderer.frames_in_flight");
    const i64 before = st::Setting<i64>(rig.registry, frames).Get();
    st::ChangeRequest request{frames, st::Value::FromInt(3)};  // DeviceReset
    const st::SetResult r = settings.Apply(&request, 1, &rs::ApplySharedChange, rig.renderer.Get());
    CHECK(r.error == st::SetError::ApplyFailed);
    CHECK(st::Setting<i64>(rig.registry, frames).Get() == before);  // the shared value is back
    CHECK(rig.renderer->LastApply().rolled_back);
    CHECK_FALSE(settings.HasUnsavedChanges());  // the rejected change left nothing behind
    // The next attempt works.
    CHECK(settings.Apply(&request, 1, &rs::ApplySharedChange, rig.renderer.Get()).Ok());
    CHECK(st::Setting<i64>(rig.registry, frames).Get() == 3);
    CHECK(rig.renderer->Settings().GetInt(rs::settings::kFramesInFlight) == 3);
}

TEST_CASE("A setting that needs a missing capability is refused by the settings system", "[shared_settings]")
{
    QuietLog quiet;
    io_test::TempDir dir;
    Rig rig;
    st::Settings settings(rig.registry);
    settings.Load(Options(dir / "settings.cfg"));
    REQUIRE(rig.Create(rs::RenderSettings()));
    rs::SyncSharedFromRenderer(rig.renderer.Get(), &settings);
    settings.SetCapabilityCheck(&rs::RendererCapability, rig.renderer.Get());
    const bool has_hdr = rig.renderer->GetCapabilities().Has("hdr");
    const st::SetResult r = settings.SetText("renderer.hdr", "scrgb");
    CHECK(r.Ok() == has_hdr);
    if (!has_hdr)
    {
        CHECK(r.error == st::SetError::Unsupported);
    }
}

TEST_CASE(
    "The Phase 8 file (schema 1: flat keys, version = 1) loads through the migration and matches what the old parser reads",
    "[shared_settings]")
{
    io_test::TempDir dir;
    const io::Path fixture = io::Path(StringView(SETTINGS_FIXTURE_DIR)).Join("renderer_v1.cfg");
    const io::Path user = dir / "renderer_v1.cfg";
    REQUIRE(io::Copy(fixture, user, true).Ok());
    const engine_test::Text text = Slurp(user);
    Rig rig;
    st::Settings settings(rig.registry);
    rs::AddRendererMigrations(&settings.Migrations());
    const st::LoadReport report = settings.Load(Options(user));
    CHECK(report.Clean());
    CHECK(report.notes.Size() >= 14);  // one rename per key
    const rs::RenderSettings shared = rs::RenderSettingsFromSnapshot(rig.registry, *settings.Snapshot());
    rs::RenderSettings old_parse;
    rs::SettingsParseReport old_report;
    REQUIRE(rs::ParseSettings(text.CStr(), text.Length(), &old_parse, &old_report));
    CHECK(shared == old_parse);
    CHECK(shared.GetInt(rs::settings::kWidth) == 1920);
    CHECK(shared.GetEnum(rs::settings::kPresentMode) == 1);
    // The file is rewritten in the new schema only when saved, comments kept.
    CHECK(Slurp(user).View() == text.View());
    REQUIRE(settings.Save().Ok());
    const engine_test::Text saved = Slurp(user);
    CHECK(saved.View().Contains("# engine render settings"));
    CHECK(saved.View().Contains("schema = 2"));
    CHECK(saved.View().Contains("[renderer]"));
    // The whole file: the old comment, the stamps, and the migrated keys under [renderer] in the order the old file had
    // them.
    CHECK(saved.View().Contains("[renderer]\nbackend = vulkan\n"));
    CHECK(saved.View().Contains("\npresent_mode = mailbox\nwidth = 1920\nheight = 1080\n"));
    CHECK_FALSE(saved.View().Contains("\nwidth = 1920\nheight = 1080\nfullscreen") == false);
}

TEST_CASE("A second change made before the first was applied does not replace it", "[shared_settings]")
{
    QuietLog quiet;
    io_test::TempDir dir;
    Rig rig;
    st::Settings settings(rig.registry);
    settings.Load(Options(dir / "settings.cfg"));
    REQUIRE(rig.Create(rs::RenderSettings()));
    rs::SyncSharedFromRenderer(rig.renderer.Get(), &settings);
    // Two changes made inside one frame: the renderer queues the second over the first, so the second must carry the
    // first's value.
    struct InFrame
    {
        Rig* rig;
        st::Settings* settings;
        bool done = false;
    } in_frame{&rig, &settings};
    // Build callback replaced for this test: the rig's renderer was created with Build, so make the changes between
    // frames instead, which the renderer treats as immediate; the queued path is exercised by asking from inside a
    // frame below.
    st::ChangeRequest w{rig.registry.Find("renderer.width"), st::Value::FromInt(96)};
    st::ChangeRequest p{rig.registry.Find("renderer.present_mode"), st::Value::FromEnum(1)};
    CHECK(settings.Apply(&w, 1, &rs::ApplySharedChange, rig.renderer.Get()).Ok());
    CHECK(settings.Apply(&p, 1, &rs::ApplySharedChange, rig.renderer.Get()).Ok());
    CHECK(rig.renderer->Settings().GetInt(rs::settings::kWidth) == 96);  // still there after the second change
    CHECK(rig.renderer->Settings().GetEnum(rs::settings::kPresentMode) == 1);
    (void)in_frame;
}

TEST_CASE("The settings system is brought back in line when a queued change failed at the frame boundary",
          "[shared_settings]")
{
    QuietLog quiet;
    io_test::TempDir dir;
    Rig rig;
    st::Settings settings(rig.registry);
    settings.Load(Options(dir / "settings.cfg"));
    REQUIRE(rig.Create(rs::RenderSettings()));
    // The rig runs the null backend and the settings system holds `auto`: the backend takes effect at start, so the
    // stored value is the one for the next start and the sync leaves it alone (Phase 13 Day 173).
    const st::SettingId backend = rig.registry.Find("renderer.backend");
    CHECK_FALSE(rs::SyncSharedFromRenderer(rig.renderer.Get(), &settings));
    CHECK(settings.Snapshot()->Get(backend).AsEnum() == static_cast<u32>(rhi::Backend::Auto));
    REQUIRE(settings.Set(backend, st::Value::FromEnum(2)).Ok());  // dx12, for the next start
    CHECK_FALSE(rs::SyncSharedFromRenderer(rig.renderer.Get(), &settings));
    CHECK(settings.Snapshot()->Get(backend).AsEnum() == 2);
    CHECK(rig.renderer->Settings().GetEnum(rs::settings::kBackend) == 0);
    // The settings system accepted a value the renderer never applied (as after a queued change that failed).
    const st::SettingId frames = rig.registry.Find("renderer.frames_in_flight");
    REQUIRE(settings.Set(frames, st::Value::FromInt(3)).Ok());  // no apply function: the renderer does not know
    CHECK(rig.renderer->Settings().GetInt(rs::settings::kFramesInFlight) == 2);
    CHECK(rs::SyncSharedFromRenderer(rig.renderer.Get(), &settings));
    CHECK(st::Setting<i64>(rig.registry, frames).Get() == 2);  // the renderer's value wins
    CHECK_FALSE(rs::SyncSharedFromRenderer(rig.renderer.Get(), &settings));
}

TEST_CASE("A text longer than the renderer can hold is refused, not cut", "[shared_settings]")
{
    io_test::TempDir dir;
    Rig rig;
    st::Settings settings(rig.registry);
    settings.Load(Options(dir / "settings.cfg"));
    const engine_test::Text too_long = engine_test::Repeated(rs::kSettingTextBytes, 'g');
    const st::SetResult r = settings.Set(rig.registry.Find("renderer.adapter"), st::Value::FromText(too_long.View()));
    CHECK(r.error == st::SetError::BadValue);
    CHECK(r.value == st::ValueError::TooLong);
    CHECK(settings
              .Set(rig.registry.Find("renderer.adapter"),
                   st::Value::FromText(engine_test::Repeated(rs::kSettingTextBytes - 1, 'g').View()))
              .Ok());
    CHECK(settings.SetText("renderer.adapter", engine_test::Concat("\"", too_long.View(), "\"").View()).value ==
          st::ValueError::TooLong);
}
