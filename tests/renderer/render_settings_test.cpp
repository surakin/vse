// Phase 8 Day 104: the settings registry, RenderSettings, DiffSettings, ResolveSettings and the text format.
#include <catch2/catch_test_macros.hpp>

#include "cstr/format.h"
#include "lang/limits.h"
#include "test_text.h"

#include "renderer/render_settings.h"
#include "rhi/rhi.h"
#include "cstr/cstr.h"

using namespace engine_placeholder;
using namespace engine_placeholder::renderer;

namespace
{

Capabilities AllCapabilities()
{
    Capabilities c;
    c.Add("fullscreen");
    c.Add("hdr");
    return c;
}

const char* const kLevelNames[] = {"off", "on"};

}  // namespace

TEST_CASE("built-in settings have the ids, classes and defaults of the plan", "[render_settings]")
{
    const SettingsRegistry& r = SettingsRegistry::Builtin();
    REQUIRE(r.Count() == 14);
    struct Row
    {
        SettingId id;
        const char* name;
        CostClass cost;
    };
    const Row rows[] = {
        {settings::kBackend, "backend", CostClass::DeviceReset},
        {settings::kAdapter, "adapter", CostClass::DeviceReset},
        {settings::kValidation, "validation", CostClass::DeviceReset},
        {settings::kSyncValidation, "sync_validation", CostClass::DeviceReset},
        {settings::kFramesInFlight, "frames_in_flight", CostClass::DeviceReset},
        {settings::kPresentMode, "present_mode", CostClass::Swapchain},
        {settings::kWidth, "width", CostClass::Swapchain},
        {settings::kHeight, "height", CostClass::Swapchain},
        {settings::kFullscreen, "fullscreen", CostClass::Swapchain},
        {settings::kSwapchainFormat, "swapchain_format", CostClass::Swapchain},
        {settings::kHdr, "hdr", CostClass::Swapchain},
        {settings::kShaderQuality, "shader_quality", CostClass::PipelineRebuild},
        {settings::kGBufferDebugLayer, "gbuffer_debug_layer", CostClass::Dynamic},
        {settings::kRenderScale, "render_scale", CostClass::Dynamic},
    };
    for (const Row& row : rows)
    {
        const SettingDesc* d = r.Find(row.id);
        REQUIRE(d != nullptr);
        CHECK(CStrCompare(d->name, row.name) == 0);
        CHECK(d->cost == row.cost);
        CHECK(r.FindByName(row.name) == row.id);
    }
    RenderSettings s;
    CHECK(s.GetEnum(settings::kBackend) == static_cast<u32>(rhi::Backend::Auto));  // auto
    CHECK(s.GetInt(settings::kFramesInFlight) == 2);
    CHECK(s.GetFloat(settings::kRenderScale) == 1.0f);
    CHECK(CStrCompare(s.GetText(settings::kAdapter), "") == 0);
    CHECK_FALSE(s.GetBool(settings::kFullscreen));
    CHECK(r.Find(settings::kRenderScale)->mechanism_only);
    CHECK_FALSE(r.Find(settings::kBackend)->mechanism_only);
    CHECK(CStrCompare(CostClassName(CostClass::PipelineRebuild), "pipeline-rebuild") == 0);
}

TEST_CASE("registration rejects bad declarations", "[render_settings]")
{
    SettingsRegistry r;
    SettingDesc d;
    d.name = "ok_name";
    d.type = SettingType::Int;
    d.default_int = 5;
    d.min_int = 0;
    d.max_int = 10;
    CHECK(r.Register(d).IsValid());
    CHECK_FALSE(r.Register(d).IsValid());  // duplicate
    SettingDesc bad = d;
    bad.name = "Upper";
    CHECK_FALSE(r.Register(bad).IsValid());
    bad.name = nullptr;
    CHECK_FALSE(r.Register(bad).IsValid());
    bad.name = "a_name_that_is_far_too_long_for_a_key";
    CHECK_FALSE(r.Register(bad).IsValid());
    bad = d;
    bad.name = "empty_range";
    bad.min_int = 3;
    bad.max_int = 2;
    CHECK_FALSE(r.Register(bad).IsValid());
    bad = d;
    bad.name = "default_outside";
    bad.default_int = 11;
    CHECK_FALSE(r.Register(bad).IsValid());
    bad = SettingDesc{};
    bad.name = "enum_without_names";
    bad.type = SettingType::Enum;
    CHECK_FALSE(r.Register(bad).IsValid());
    bad.enum_names = kLevelNames;
    bad.enum_count = 2;
    bad.default_int = 2;
    CHECK_FALSE(r.Register(bad).IsValid());
    bad = SettingDesc{};
    bad.name = "nan_float";
    bad.type = SettingType::Float;
    bad.max_float = Limits<f32>::Infinity();
    CHECK_FALSE(r.Register(bad).IsValid());
    CHECK(r.Count() == 1);
}

TEST_CASE("the registry reports full", "[render_settings]")
{
    SettingsRegistry r;
    char names[kMaxSettings + 1][8];
    for (u32 i = 0; i < kMaxSettings; ++i)
    {
        FormatTo(names[i], sizeof(names[i]), "s%u", i);
        SettingDesc d;
        d.name = names[i];
        REQUIRE(r.Register(d).IsValid());
    }
    SettingDesc extra;
    extra.name = "one_more";
    CHECK_FALSE(r.Register(extra).IsValid());
    CHECK(r.Count() == kMaxSettings);
}

TEST_CASE("typed setters validate type, range and text", "[render_settings]")
{
    RenderSettings s;
    CHECK(s.SetInt(settings::kFramesInFlight, 3) == SetStatus::Ok);
    CHECK(s.GetInt(settings::kFramesInFlight) == 3);
    CHECK(s.SetInt(settings::kFramesInFlight, 9) == SetStatus::Clamped);  // clamp policy
    CHECK(s.GetInt(settings::kFramesInFlight) == 3);
    CHECK(s.SetInt(settings::kFramesInFlight, -4) == SetStatus::Clamped);
    CHECK(s.GetInt(settings::kFramesInFlight) == 1);
    CHECK(s.SetEnum(settings::kPresentMode, 2) == SetStatus::Ok);
    CHECK(s.SetEnum(settings::kPresentMode, 3) == SetStatus::OutOfRange);  // an enum never clamps
    CHECK(s.GetEnum(settings::kPresentMode) == 2);
    CHECK(s.SetBool(settings::kPresentMode, true) == SetStatus::WrongType);
    CHECK(s.SetBool(SettingId{200}, true) == SetStatus::UnknownId);
    CHECK(s.SetBool(SettingId{}, true) == SetStatus::UnknownId);
    CHECK(s.SetFloat(settings::kRenderScale, 5.0f) == SetStatus::Clamped);
    CHECK(s.GetFloat(settings::kRenderScale) == 2.0f);
    CHECK(s.SetFloat(settings::kRenderScale, Limits<f32>::QuietNaN()) == SetStatus::OutOfRange);
    CHECK(s.SetFloat(settings::kRenderScale, Limits<f32>::Infinity()) == SetStatus::OutOfRange);
    CHECK(s.GetFloat(settings::kRenderScale) == 2.0f);
    CHECK(s.SetText(settings::kAdapter, "10de:2504") == SetStatus::Ok);
    CHECK(CStrCompare(s.GetText(settings::kAdapter), "10de:2504") == 0);
    CHECK(s.SetText(settings::kAdapter, "tab\there") == SetStatus::BadText);
    CHECK(s.SetText(settings::kAdapter, "line\nbreak") == SetStatus::BadText);
    const engine_test::Text too_long = engine_test::Repeated(kSettingTextBytes, 'x');
    CHECK(s.SetText(settings::kAdapter, too_long.CStr()) == SetStatus::BadText);
    const engine_test::Text longest = engine_test::Repeated(kSettingTextBytes - 1, 'x');
    CHECK(s.SetText(settings::kAdapter, longest.CStr()) == SetStatus::Ok);
    CHECK(s.SetText(settings::kAdapter, nullptr) == SetStatus::Ok);
    CHECK(CStrCompare(s.GetText(settings::kAdapter), "") == 0);
    // Wrong-type reads return the neutral value.
    CHECK(s.GetInt(settings::kPresentMode) == 0);
    CHECK(CStrCompare(s.GetText(settings::kWidth), "") == 0);
}

TEST_CASE("a reject-policy range refuses instead of clamping", "[render_settings]")
{
    SettingsRegistry r;
    SettingDesc d;
    d.name = "strict";
    d.type = SettingType::Int;
    d.default_int = 1;
    d.min_int = 0;
    d.max_int = 2;
    d.range_policy = RangePolicy::Reject;
    const SettingId id = r.Register(d);
    RenderSettings s(r);
    CHECK(s.SetInt(id, 7) == SetStatus::OutOfRange);
    CHECK(s.GetInt(id) == 1);
    CHECK(s.SetInt(id, 2) == SetStatus::Ok);
}

TEST_CASE("equality ignores generation and compares every type", "[render_settings]")
{
    RenderSettings a;
    RenderSettings b;
    CHECK(a == b);
    b.SetGeneration(41);
    CHECK(a == b);
    CHECK(b.Generation() == 41);
    b.SetText(settings::kAdapter, "x");
    CHECK(a != b);
    b.SetText(settings::kAdapter, "");
    CHECK(a == b);
    b.SetFloat(settings::kRenderScale, 1.5f);
    CHECK(a != b);
    b = a;
    b.SetBool(settings::kFullscreen, true);
    CHECK(a != b);
    b = a;
    b.SetEnum(settings::kPresentMode, 1);
    CHECK(a != b);
    b = a;
    b.SetInt(settings::kWidth, 1);
    CHECK(a != b);
    b.SetInt(settings::kWidth, 0);  // storing the default value explicitly is still equal
    CHECK(a == b);
}

TEST_CASE("the diff picks the cost class of the most expensive change", "[render_settings]")
{
    const Capabilities caps = AllCapabilities();
    const RenderSettings base;
    CHECK(DiffSettings(base, base, caps).cost == CostClass::None);
    CHECK_FALSE(DiffSettings(base, base, caps).Any());

    struct Case
    {
        SettingId id;
        CostClass expected;
    };
    auto changed = [&](SettingId id)
    {
        RenderSettings s = base;
        const SettingDesc* d = base.Registry().Find(id);
        switch (d->type)
        {
        case SettingType::Bool: s.SetBool(id, !base.GetBool(id)); break;
        case SettingType::Enum: s.SetEnum(id, (base.GetEnum(id) + 1) % d->enum_count); break;
        case SettingType::Int: s.SetInt(id, base.GetInt(id) == d->min_int ? d->max_int : d->min_int); break;
        case SettingType::Float: s.SetFloat(id, base.GetFloat(id) == d->min_float ? d->max_float : d->min_float); break;
        case SettingType::Text: s.SetText(id, "changed"); break;
        }
        return s;
    };
    for (u32 i = 1; i <= base.Registry().Count(); ++i)
    {
        const SettingId id{static_cast<u16>(i)};
        const SettingsChange c = DiffSettings(base, changed(id), caps);
        INFO(base.Registry().Find(id)->name);
        if (base.Registry().Find(id)->restart_required)
        {
            // Takes effect at the next start: listed apart, costs nothing now (Phase 13 Day 173).
            CHECK(c.changed_count == 0);
            REQUIRE(c.restart_count == 1);
            CHECK(c.restart[0] == id);
            CHECK(c.cost == CostClass::None);
            continue;
        }
        REQUIRE(c.changed_count == 1);
        CHECK(c.restart_count == 0);
        CHECK(c.changed[0] == id);
        CHECK(c.cost == base.Registry().Find(id)->cost);
    }
    // Combinations: the maximum wins.
    RenderSettings s = base;
    s.SetFloat(settings::kRenderScale, 0.5f);  // Dynamic
    s.SetEnum(settings::kShaderQuality, 0);    // PipelineRebuild
    CHECK(DiffSettings(base, s, caps).cost == CostClass::PipelineRebuild);
    s.SetEnum(settings::kPresentMode, 1);  // Swapchain
    CHECK(DiffSettings(base, s, caps).cost == CostClass::PipelineRebuild);
    s.SetBool(settings::kSyncValidation, true);  // DeviceReset
    const SettingsChange all = DiffSettings(base, s, caps);
    CHECK(all.cost == CostClass::DeviceReset);
    CHECK(all.changed_count == 4);
}

TEST_CASE("a setting registered by someone else takes part in the diff", "[render_settings]")
{
    SettingsRegistry r;
    RegisterBuiltinSettings(&r);
    SettingDesc d;
    d.name = "shadow_quality";
    d.type = SettingType::Enum;
    d.cost = CostClass::PipelineRebuild;
    d.enum_names = kLevelNames;
    d.enum_count = 2;
    const SettingId shadow = r.Register(d);
    SettingDesc w;
    w.name = "ui_scale";
    w.type = SettingType::Float;
    w.cost = CostClass::None;
    w.default_float = 1.0f;
    w.min_float = 0.5f;
    w.max_float = 3.0f;
    const SettingId ui = r.Register(w);
    REQUIRE(shadow.IsValid());
    REQUIRE(ui.IsValid());

    RenderSettings from(r);
    RenderSettings to(r);
    to.SetFloat(ui, 2.0f);
    SettingsChange c = DiffSettings(from, to, AllCapabilities());
    CHECK(c.cost == CostClass::None);
    CHECK(c.changed_count == 1);
    to.SetEnum(shadow, 1);
    c = DiffSettings(from, to, AllCapabilities());
    CHECK(c.cost == CostClass::PipelineRebuild);
    CHECK(c.changed_count == 2);
    // Settings made before the registration read as the default for the new id.
    CHECK(from.GetEnum(shadow) == 0);
    CHECK(from.GetFloat(ui) == 1.0f);
    // And the text round trip carries them.
    char text[2048];
    REQUIRE(SerializeSettings(to, text, sizeof(text)) < sizeof(text));
    RenderSettings parsed(r);
    SettingsParseReport report;
    REQUIRE(ParseSettings(text, CStrLength(text), &parsed, &report));
    CHECK(parsed == to);
}

TEST_CASE("a setting registered after settings were created reads as its default", "[render_settings]")
{
    SettingsRegistry r;
    RegisterBuiltinSettings(&r);
    RenderSettings early(r);
    early.SetInt(settings::kWidth, 800);
    SettingDesc d;
    d.name = "late";
    d.type = SettingType::Int;
    d.default_int = 7;
    d.max_int = 10;
    const SettingId late = r.Register(d);
    CHECK(early.GetInt(late) == 7);
    CHECK(early.SetInt(late, 9) == SetStatus::Ok);
    CHECK(early.GetInt(late) == 9);
    CHECK(early.GetInt(settings::kWidth) == 800);
}

TEST_CASE("unsupported requests are reported, not applied", "[render_settings]")
{
    Capabilities none;
    const RenderSettings base;
    RenderSettings want = base;
    want.SetBool(settings::kFullscreen, true);
    want.SetEnum(settings::kHdr, 1);
    want.SetEnum(settings::kPresentMode, 1);
    const SettingsChange c = DiffSettings(base, want, none);
    CHECK(c.changed_count == 1);
    CHECK(c.changed[0] == settings::kPresentMode);
    CHECK(c.cost == CostClass::Swapchain);
    REQUIRE(c.unsupported_count == 2);
    CHECK(c.unsupported[0].id == settings::kFullscreen);
    CHECK(CStrCompare(c.unsupported[0].capability, "fullscreen") == 0);
    CHECK(c.unsupported[1].id == settings::kHdr);

    // Only unsupported changes: no action at all.
    RenderSettings only = base;
    only.SetBool(settings::kFullscreen, true);
    const SettingsChange none_change = DiffSettings(base, only, none);
    CHECK_FALSE(none_change.Any());
    CHECK(none_change.cost == CostClass::None);
    CHECK(none_change.unsupported_count == 1);

    const RenderSettings resolved = ResolveSettings(base, want, none);
    CHECK_FALSE(resolved.GetBool(settings::kFullscreen));
    CHECK(resolved.GetEnum(settings::kHdr) == 0);
    CHECK(resolved.GetEnum(settings::kPresentMode) == 1);
    // With the capabilities present everything passes through.
    CHECK(ResolveSettings(base, want, AllCapabilities()) == want);
}

TEST_CASE("capabilities hold names and are bounded", "[render_settings]")
{
    Capabilities c;
    CHECK(c.Has(nullptr));
    CHECK_FALSE(c.Has("hdr"));
    CHECK(c.Add("hdr"));
    CHECK(c.Add("hdr"));  // duplicate is fine
    CHECK(c.Has("hdr"));
    char names[kMaxCapabilities + 1][8];
    bool full = false;
    for (u32 i = 0; i < kMaxCapabilities + 1; ++i)
    {
        FormatTo(names[i], sizeof(names[i]), "c%u", i);
        if (!c.Add(names[i]))
        {
            full = true;
        }
    }
    CHECK(full);
}

TEST_CASE("the text format is stable and round trips", "[render_settings]")
{
    RenderSettings s;
    s.SetEnum(settings::kBackend, 0);
    s.SetText(settings::kAdapter, "10de:2504:abcd");
    s.SetInt(settings::kWidth, 1920);
    s.SetFloat(settings::kRenderScale, 0.7f);
    s.SetBool(settings::kFullscreen, true);
    char first[2048];
    char second[2048];
    const usize n = SerializeSettings(s, first, sizeof(first));
    REQUIRE(n < sizeof(first));
    CHECK(SerializeSettings(s, second, sizeof(second)) == n);
    CHECK(CStrCompare(first, second) == 0);  // same input, same bytes
    CHECK(SerializeSettings(s, nullptr, 0) == n);
    CHECK(CStrFind(first, "version = 1\n") != nullptr);
    CHECK(CStrFind(first, "adapter = 10de:2504:abcd\n") != nullptr);
    CHECK(CStrFind(first, "backend = null\n") != nullptr);

    RenderSettings parsed;
    SettingsParseReport report;
    REQUIRE(ParseSettings(first, n, &parsed, &report));
    CHECK(report.error_count == 0);
    CHECK(report.warning_count == 0);
    CHECK(report.version == 1);
    CHECK(parsed == s);
    CHECK(parsed.GetFloat(settings::kRenderScale) == 0.7f);  // %.9g round trips an f32 exactly

    // And again byte-for-byte.
    char third[2048];
    SerializeSettings(parsed, third, sizeof(third));
    CHECK(CStrCompare(first, third) == 0);

    // A short buffer truncates and terminates, reporting the full length.
    char tiny[16];
    CHECK(SerializeSettings(s, tiny, sizeof(tiny)) == n);
    CHECK(CStrLength(tiny) == sizeof(tiny) - 1);
}

TEST_CASE("parsing reports unknown keys, bad values and line numbers", "[render_settings]")
{
    const char* text = "# comment\n"
                       "\n"
                       "version = 1\r\n"
                       "  width   =   640  \n"
                       "colour = red\n"
                       "height = lots\n"
                       "present_mode = sideways\n"
                       "just a line\n"
                       "width = 7\n";
    RenderSettings s;
    s.SetInt(settings::kHeight, 99);
    SettingsParseReport report;
    CHECK_FALSE(ParseSettings(text, CStrLength(text), &s, &report));
    CHECK(report.warning_count == 1);
    CHECK(report.warnings[0].line == 5);
    CHECK(CStrFind(report.warnings[0].message, "colour") != nullptr);
    CHECK(report.error_count == 4);
    CHECK(report.errors[0].line == 6);  // height = lots
    CHECK(report.errors[1].line == 7);  // present_mode
    CHECK(report.errors[2].line == 8);  // not key = value
    CHECK(report.errors[3].line == 9);  // width repeated
    // Nothing was applied.
    CHECK(s.GetInt(settings::kWidth) == 0);
    CHECK(s.GetInt(settings::kHeight) == 99);
}

TEST_CASE("a clean parse applies only the keys present and keeps the generation", "[render_settings]")
{
    RenderSettings s;
    s.SetInt(settings::kWidth, 100);
    s.SetGeneration(9);
    const char* text = "version = 1\nheight = 480\nrender_scale = 99\n";
    SettingsParseReport report;
    REQUIRE(ParseSettings(text, CStrLength(text), &s, &report));
    CHECK(s.GetInt(settings::kWidth) == 100);
    CHECK(s.GetInt(settings::kHeight) == 480);
    CHECK(s.GetFloat(settings::kRenderScale) == 2.0f);  // clamped
    CHECK(report.warning_count == 1);                   // and said so
    CHECK(s.Generation() == 9);
}

TEST_CASE("versions: newer is refused, older is migrated, none is accepted", "[render_settings]")
{
    RenderSettings s;
    SettingsParseReport report;
    const char* newer = "version = 2\nwidth = 5\n";
    CHECK_FALSE(ParseSettings(newer, CStrLength(newer), &s, &report));
    CHECK(report.error_count == 1);
    CHECK(s.GetInt(settings::kWidth) == 0);

    const char* none = "width = 5\n";
    CHECK(ParseSettings(none, CStrLength(none), &s, &report));
    CHECK(report.version == 0);
    CHECK(s.GetInt(settings::kWidth) == 5);

    const char* twice = "version = 1\nversion = 1\n";
    CHECK_FALSE(ParseSettings(twice, CStrLength(twice), &s, &report));
    const char* junk = "version = x\n";
    CHECK_FALSE(ParseSettings(junk, CStrLength(junk), &s, &report));

    // A key written by an older version is renamed by the hook.
    auto migrate = [](u32 file_version, const char* key) -> const char*
    { return file_version == 0 && CStrCompare(key, "vsync_mode") == 0 ? "present_mode" : nullptr; };
    const char* old = "vsync_mode = immediate\n";
    RenderSettings m;
    CHECK(ParseSettings(old, CStrLength(old), &m, &report, migrate));
    CHECK(m.GetEnum(settings::kPresentMode) == 2);
    RenderSettings unmigrated;
    CHECK(ParseSettings(old, CStrLength(old), &unmigrated, &report));  // without the hook the key is unknown: a warning
    CHECK(report.warning_count == 1);
    CHECK(unmigrated.GetEnum(settings::kPresentMode) == 0);
}

TEST_CASE("parsing tolerates empty and null input and over-long lines", "[render_settings]")
{
    RenderSettings s;
    CHECK(ParseSettings(nullptr, 0, &s, nullptr));
    CHECK(ParseSettings("", 0, &s, nullptr));
    CHECK(ParseSettings("no newline at the end = x", 0, &s, nullptr));  // length 0 reads nothing
    const engine_test::Text ones = engine_test::Repeated(400, '1');
    const engine_test::Text long_line = engine_test::Concat("width = ", ones.View(), "\n");
    SettingsParseReport report;
    CHECK_FALSE(ParseSettings(long_line.CStr(), long_line.Length(), &s, &report));
    CHECK(report.error_count == 1);
    const char* last = "width = 33";  // final line without a newline
    CHECK(ParseSettings(last, CStrLength(last), &s, &report));
    CHECK(s.GetInt(settings::kWidth) == 33);
}

TEST_CASE("many errors are counted but only the first are recorded", "[render_settings]")
{
    engine_test::Text text = engine_test::MakeText();
    for (int i = 0; i < 20; ++i)
    {
        text.Append("nonsense\n");
    }
    RenderSettings s;
    SettingsParseReport report;
    CHECK_FALSE(ParseSettings(text.CStr(), text.Length(), &s, &report));
    CHECK(report.error_count == 20);
    CHECK(report.errors[kMaxParseProblems - 1].line == kMaxParseProblems);
}

TEST_CASE("random pairs: the diff class is the maximum over single changes", "[render_settings]")
{
    const Capabilities caps = AllCapabilities();
    u32 seed = 12345;
    auto next = [&seed]()
    {
        seed = seed * 1664525u + 1013904223u;
        return seed >> 8;
    };
    auto randomize = [&](RenderSettings* s)
    {
        for (u32 i = 1; i <= s->Registry().Count(); ++i)
        {
            const SettingId id{static_cast<u16>(i)};
            const SettingDesc* d = s->Registry().Find(id);
            if (next() % 2 == 0)
            {
                continue;  // leave some at the default
            }
            switch (d->type)
            {
            case SettingType::Bool: s->SetBool(id, next() % 2 != 0); break;
            case SettingType::Enum: s->SetEnum(id, next() % d->enum_count); break;
            case SettingType::Int:
                s->SetInt(id, d->min_int + static_cast<i64>(next() % static_cast<u32>(d->max_int - d->min_int + 1)));
                break;
            case SettingType::Float:
                s->SetFloat(id,
                            d->min_float + (d->max_float - d->min_float) * static_cast<f32>(next() % 1000) / 1000.0f);
                break;
            case SettingType::Text: s->SetText(id, next() % 2 ? "a" : "bb"); break;
            }
        }
    };
    for (int iter = 0; iter < 1000; ++iter)
    {
        RenderSettings a;
        RenderSettings b;
        randomize(&a);
        randomize(&b);
        const SettingsChange ab = DiffSettings(a, b, caps);
        CHECK(DiffSettings(a, a, caps).cost == CostClass::None);
        CHECK(DiffSettings(a, a, caps).changed_count == 0);
        CHECK((ab.changed_count == 0 && ab.restart_count == 0) == (a == b));
        CostClass expected = CostClass::None;
        for (u32 i = 0; i < ab.changed_count; ++i)
        {
            // Each reported change is a real single-setting difference whose class bounds the result.
            RenderSettings one = a;
            char value[kSettingTextBytes];
            b.ValueToString(ab.changed[i], value, sizeof(value));
            one.SetFromString(ab.changed[i], value);
            const SettingsChange single = DiffSettings(a, one, caps);
            REQUIRE(single.changed_count == 1);
            if (single.cost > expected)
            {
                expected = single.cost;
            }
        }
        CHECK(ab.cost == expected);
        // The reverse diff touches the same settings.
        CHECK(DiffSettings(b, a, caps).changed_count == ab.changed_count);
        // Text round trip of the random settings.
        char text[2048];
        SerializeSettings(a, text, sizeof(text));
        RenderSettings back;
        REQUIRE(ParseSettings(text, CStrLength(text), &back, nullptr));
        CHECK(back == a);
    }
}

TEST_CASE("only the backend waits for the next start, and resolving puts it back", "[render_settings]")
{
    const SettingsRegistry& r = SettingsRegistry::Builtin();
    for (u32 i = 1; i <= r.Count(); ++i)
    {
        const SettingId id{static_cast<u16>(i)};
        INFO(r.Find(id)->name);
        CHECK(r.Find(id)->restart_required == (id == settings::kBackend));
    }
    const RenderSettings current;
    RenderSettings wanted = current;
    wanted.SetEnum(settings::kBackend, 2);        // dx12
    wanted.SetInt(settings::kFramesInFlight, 3);  // a device reset, applied now
    const SettingsChange c = DiffSettings(current, wanted, AllCapabilities());
    CHECK(c.cost == CostClass::DeviceReset);
    REQUIRE(c.changed_count == 1);
    CHECK(c.changed[0] == settings::kFramesInFlight);
    REQUIRE(c.restart_count == 1);
    CHECK(c.restart[0] == settings::kBackend);
    const RenderSettings resolved = ResolveSettings(current, wanted, AllCapabilities());
    CHECK(resolved.GetEnum(settings::kBackend) == current.GetEnum(settings::kBackend));
    CHECK(resolved.GetInt(settings::kFramesInFlight) == 3);
}

TEST_CASE("putting a gated setting back to its default needs no capability", "[render_settings]")
{
    Capabilities none;
    RenderSettings on;
    on.SetBool(settings::kFullscreen, true);  // set while the capability existed
    RenderSettings off;                       // the request: back to the default
    const SettingsChange c = DiffSettings(on, off, none);
    CHECK(c.changed_count == 1);
    CHECK(c.unsupported_count == 0);
    CHECK(c.cost == CostClass::Swapchain);
    CHECK_FALSE(ResolveSettings(on, off, none).GetBool(settings::kFullscreen));
    // Turning it on without the capability is still refused.
    CHECK(DiffSettings(off, on, none).unsupported_count == 1);
}

TEST_CASE("text with edge blanks and enum names the format cannot carry are refused", "[render_settings]")
{
    RenderSettings s;
    CHECK(s.SetText(settings::kAdapter, " gpu") == SetStatus::BadText);
    CHECK(s.SetText(settings::kAdapter, "gpu ") == SetStatus::BadText);
    CHECK(s.SetText(settings::kAdapter, "a b") == SetStatus::Ok);  // inner blanks are fine
    char text[2048];
    SerializeSettings(s, text, sizeof(text));
    RenderSettings back;
    REQUIRE(ParseSettings(text, CStrLength(text), &back, nullptr));
    CHECK(back == s);

    const char* const with_blank[] = {"high quality"};
    const char* const with_equals[] = {"a=b"};
    const char* const with_hash[] = {"#x"};
    const char* const empty_name[] = {""};
    const char* const null_name[] = {nullptr};
    const char* const* lists[] = {with_blank, with_equals, with_hash, empty_name, null_name};
    SettingsRegistry r;
    for (const char* const* list : lists)
    {
        SettingDesc d;
        d.name = "e";
        d.type = SettingType::Enum;
        d.enum_names = list;
        d.enum_count = 1;
        CHECK_FALSE(r.Register(d).IsValid());
    }
}

TEST_CASE("multi_adapter is a capability from two suitable adapters on", "[render_settings][adapter]")
{
    Capabilities c;
    CHECK(c.AdapterCount() == 0);
    c.SetAdapterCount(1);
    CHECK(c.AdapterCount() == 1);
    CHECK_FALSE(c.Has("multi_adapter"));
    c.SetAdapterCount(2);
    CHECK(c.AdapterCount() == 2);
    CHECK(c.Has("multi_adapter"));
    c.SetAdapterCount(1);
    CHECK_FALSE(c.Has("multi_adapter"));  // a smaller count takes it away again
}
