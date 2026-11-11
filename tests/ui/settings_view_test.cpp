#include <catch2/catch_test_macros.hpp>

#include "font_support.h"
#include "io/path.h"
#include "settings/desc.h"
#include "settings/registry.h"
#include "settings/setting.h"
#include "settings/settings.h"
#include "temp_dir.h"
#include "ui/settings_view.h"
#include "ui/surface.h"
#include "ui/testing/harness.h"
#include "ui/widgets/buttons.h"

using namespace engine_placeholder;
using namespace engine_placeholder::ui;

namespace
{

const char* const kSvQuality[] = {"low", "medium", "high"};

struct SvApp
{
    settings::Registry registry;
    settings::SettingId vsync, width, scale, quality, name, token, hidden, advanced, restart, strict;
    SvApp()
    {
        settings::SettingDesc v = settings::MakeBool("ui.vsync", true);
        v.label = "Vertical sync";
        v.group = "Video";
        vsync = registry.Register(v);
        settings::SettingDesc w = settings::MakeInt("ui.width", 1280, 640, 3840);
        w.label = "Width";
        w.group = "Video";
        width = registry.Register(w);
        scale = registry.Register(settings::MakeFloat("ui.scale", 1.0, 0.5, 2.0));
        quality = registry.Register(settings::MakeEnum("ui.quality", kSvQuality, 3, 1));
        name = registry.Register(settings::MakeText("ui.name", "player"));
        settings::SettingDesc t = settings::MakeText("ui.token", "");
        t.flags = settings::kSecret;
        token = registry.Register(t);
        settings::SettingDesc h = settings::MakeInt("ui.hidden", 1, 0, 9);
        h.flags = settings::kHidden;
        hidden = registry.Register(h);
        settings::SettingDesc a = settings::MakeBool("ui.advanced", false);
        a.flags = settings::kAdvanced;
        advanced = registry.Register(a);
        settings::SettingDesc r = settings::MakeBool("ui.restart", false);
        r.flags = settings::kRestartRequired;
        restart = registry.Register(r);
        strict = registry.Register(settings::MakeInt("ui.strict", 5, 0, 10, settings::RangePolicy::Reject));
    }
};

struct SvRig
{
    SvRig() : settings(app.registry), h(520, 700, 1, StringView(UI_TEST_FONT_FILE))
    {
        const io::Path user = dir / "user.cfg";
        settings::LoadOptions o;
        o.app_name = "ui-test";
        o.user_file = &user;
        o.use_environment = false;
        o.use_policy = false;
        settings.Load(o);
        Build();
    }
    void Build()
    {
        UniquePtr<SettingsView> v = MakeUnique<SettingsView>(&settings, StringView("ui."), StringView("view"));
        view = v.Get();
        LayoutProps fill;
        fill.fill_parent = true;
        view->SetProps(fill);
        view->SetApply(&SvRig::Apply, this);
        h.GetSurface().Root()->AddChild(UniquePtr<Widget>(v.Release()));
        h.Advance(0);
    }
    static bool Apply(void* user, const settings::ChangeEvent&)
    {
        SvRig* self = static_cast<SvRig*>(user);
        ++self->applied;
        return self->accept;
    }
    void ClickControl(settings::SettingId id)
    {
        const Rect r = view->ControlOf(id)->Bounds();
        h.Click(r.x + r.w / 2, r.y + r.h / 2);
    }
    void Focus(Widget* w)
    {
        h.GetSurface().SetFocus(w);
        h.Advance(0);
    }
    io_test::TempDir dir;
    SvApp app;
    settings::Settings settings;
    Harness h;
    SettingsView* view = nullptr;
    int applied = 0;
    bool accept = true;
};

}  // namespace

TEST_CASE("The settings screen makes a row per setting, skips hidden ones and collapses advanced ones",
          "[ui][settings]")
{
    SvRig rig;
    REQUIRE(rig.h.Valid());
    SettingsView& v = *rig.view;
    CHECK(v.RowOf(rig.app.hidden) == nullptr);
    CHECK(v.RowCount() == 9);
    CHECK(v.SpinOf(rig.app.width) != nullptr);
    CHECK(v.SpinOf(rig.app.scale) != nullptr);
    CHECK(v.SpinOf(rig.app.vsync) == nullptr);
    CHECK(v.RestartNoteOf(rig.app.restart) != nullptr);
    CHECK(v.RestartNoteOf(rig.app.vsync) == nullptr);
    REQUIRE(v.RowOf(rig.app.advanced) != nullptr);
    CHECK(v.RowOf(rig.app.advanced)->Collapsed());
    REQUIRE(v.AdvancedBox() != nullptr);
    const Rect box = v.AdvancedBox()->Bounds();
    rig.h.Click(box.x + box.w / 2, box.y + box.h / 2);
    CHECK_FALSE(v.RowOf(rig.app.advanced)->Collapsed());
    const Rect moved = v.AdvancedBox()->Bounds();
    rig.h.Click(moved.x + moved.w / 2, moved.y + moved.h / 2);
    CHECK(v.RowOf(rig.app.advanced)->Collapsed());
    CHECK(static_cast<TextField*>(v.ControlOf(rig.app.token))->Password());
    CHECK_FALSE(static_cast<TextField*>(v.ControlOf(rig.app.name))->Password());
}

TEST_CASE("A check box change reaches the settings through Apply once", "[ui][settings]")
{
    SvRig rig;
    REQUIRE(rig.h.Valid());
    rig.ClickControl(rig.app.vsync);
    CHECK(rig.applied == 1);
    CHECK_FALSE(settings::Setting<bool>(rig.app.registry, rig.app.vsync).Get());
}

TEST_CASE("A slider and its spin box show the same value and commit once", "[ui][settings]")
{
    SvRig rig;
    REQUIRE(rig.h.Valid());
    Slider* slider = static_cast<Slider*>(rig.view->ControlOf(rig.app.width));
    rig.Focus(slider);
    rig.h.Key(Key::Right);
    CHECK(rig.applied == 1);
    const i64 now = settings::Setting<i64>(rig.app.registry, rig.app.width).Get();
    CHECK(now > 1280);
    CHECK(static_cast<i64>(rig.view->SpinOf(rig.app.width)->Value()) == now);
    CHECK(static_cast<i64>(slider->Value()) == now);
}

TEST_CASE("An enum row is a combo box and a text row commits on Enter", "[ui][settings]")
{
    SvRig rig;
    REQUIRE(rig.h.Valid());
    ComboBox* combo = static_cast<ComboBox*>(rig.view->ControlOf(rig.app.quality));
    CHECK(combo->Selected() == 1);
    rig.Focus(combo);
    rig.h.Key(Key::Down);
    CHECK(rig.settings.Provenance(rig.app.quality).value.AsEnum() == 2);
    TextField* field = static_cast<TextField*>(rig.view->ControlOf(rig.app.name));
    rig.Focus(field);
    field->SetText(StringView("zed"));
    rig.h.Key(Key::Enter);
    CHECK(rig.settings.Provenance(rig.app.name).value.AsText() == StringView("zed"));
}

TEST_CASE("A rejected change goes back to the value in force and says why", "[ui][settings]")
{
    SvRig rig;
    REQUIRE(rig.h.Valid());
    rig.accept = false;
    rig.ClickControl(rig.app.vsync);
    CHECK(rig.applied == 1);
    CHECK(settings::Setting<bool>(rig.app.registry, rig.app.vsync).Get());
    CHECK(static_cast<CheckBox*>(rig.view->ControlOf(rig.app.vsync))->Checked());
    CHECK(rig.view->Message().Length() > 0);
    rig.accept = true;
    rig.ClickControl(rig.app.vsync);
    CHECK(rig.view->Message().Length() == 0);
}

TEST_CASE("A restart-required change is flagged", "[ui][settings]")
{
    SvRig rig;
    REQUIRE(rig.h.Valid());
    int heard = 0;
    rig.view->OnRestartNeeded().Connect([](void* user, SettingsView*) { ++*static_cast<int*>(user); }, &heard);
    CHECK_FALSE(rig.view->RestartPending());
    rig.ClickControl(rig.app.vsync);
    CHECK_FALSE(rig.view->RestartPending());
    rig.ClickControl(rig.app.restart);
    CHECK(rig.view->RestartPending());
    CHECK(heard == 1);
}

TEST_CASE("A value fixed by the command line is shown disabled with its source", "[ui][settings]")
{
    io_test::TempDir dir;
    SvApp app;
    settings::Settings s(app.registry);
    const io::Path user = dir / "user.cfg";
    settings::LoadOptions o;
    o.app_name = "ui-test";
    o.user_file = &user;
    o.use_environment = false;
    o.use_policy = false;
    const char* argv[] = {"prog", "--set", "ui.width=2000"};
    o.argv = argv;
    o.argc = 3;
    s.Load(o);
    Harness h(520, 700, 1, StringView(UI_TEST_FONT_FILE));
    REQUIRE(h.Valid());
    UniquePtr<SettingsView> v = MakeUnique<SettingsView>(&s, StringView("ui."), StringView("view"));
    SettingsView* view = v.Get();
    h.GetSurface().Root()->AddChild(UniquePtr<Widget>(v.Release()));
    h.Advance(0);
    CHECK_FALSE(view->ControlOf(app.width)->Enabled());
    CHECK(view->ControlOf(app.width)->Tooltip().Length() > 0);
    CHECK(view->ControlOf(app.vsync)->Enabled());
}

TEST_CASE("A change made elsewhere shows up without polling", "[ui][settings]")
{
    SvRig rig;
    REQUIRE(rig.h.Valid());
    const u64 before = rig.view->Refreshes();
    for (int i = 0; i < 100; ++i)
    {
        rig.h.Advance(0.016f);
    }
    CHECK(rig.view->Refreshes() == before);  // an idle screen reads nothing
    CHECK(rig.settings.Set(rig.app.width, settings::Value::FromInt(2000)).error == settings::SetError::None);
    rig.h.Advance(0.016f);
    CHECK(rig.view->Refreshes() == before + 1);
    CHECK(static_cast<i64>(rig.view->SpinOf(rig.app.width)->Value()) == 2000);
    for (int i = 0; i < 100; ++i)
    {
        rig.h.Advance(0.016f);
    }
    CHECK(rig.view->Refreshes() == before + 1);
}

TEST_CASE("A text edit in progress survives a refresh and is committed when the focus leaves", "[ui][settings]")
{
    SvRig rig;
    REQUIRE(rig.h.Valid());
    TextField* field = static_cast<TextField*>(rig.view->ControlOf(rig.app.name));
    rig.Focus(field);
    field->SetText(StringView("draft"));
    CHECK(rig.settings.Set(rig.app.width, settings::Value::FromInt(2000)).error == settings::SetError::None);
    rig.h.Advance(0.016f);
    CHECK(field->Text() == StringView("draft"));
    rig.Focus(rig.view->ControlOf(rig.app.vsync));
    CHECK(rig.settings.Provenance(rig.app.name).value.AsText() == StringView("draft"));
}

namespace
{
void SvRebuildFromClick(void* user, Button*)
{
    static_cast<SettingsView*>(user)->Rebuild();
}
}  // namespace

TEST_CASE("Rebuild from a handler during a dispatch ends, and the old controls do nothing", "[ui][settings]")
{
    SvRig rig;
    REQUIRE(rig.h.Valid());
    UniquePtr<Button> button = MakeUnique<Button>(StringView("rebuild"), StringView("Rebuild"));
    Button* raw = button.Get();
    button->OnClick().Connect(&SvRebuildFromClick, rig.view);
    Rect where;
    where.x = 330;
    where.y = 0;
    where.w = 90;
    where.h = 24;
    button->SetBounds(where);
    rig.h.GetSurface().Root()->AddChild(UniquePtr<Widget>(button.Release()));
    rig.h.Advance(0);
    const usize rows = rig.view->RowCount();
    // Without the fix this click never returned: the loop that emptied the view waited for children whose removal was
    // queued.
    rig.h.Click(where.x + where.w / 2, where.y + where.h / 2);
    CHECK(raw->Name() == StringView("rebuild"));
    CHECK(rig.view->RowCount() == rows);
    rig.ClickControl(rig.app.vsync);
    CHECK(rig.applied == 1);
}
