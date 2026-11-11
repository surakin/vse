
#include <catch2/catch_test_macros.hpp>

#include "font_support.h"
#include "ui_demo_app.h"
#include "ui/testing/harness.h"

using namespace engine_placeholder;
using namespace engine_placeholder::ui;

namespace
{

struct DemoRig
{
    DemoRig() : h(1280, 720, 1, StringView(UI_TEST_FONT_FILE)), app(&h.GetSurface()) { h.Advance(0); }
    Harness h;
    ui_demo::UiDemoApp app;
};

}  // namespace

TEST_CASE("The demo builds its tree and the viewport gets a size", "[ui][ui_demo]")
{
    DemoRig rig;
    REQUIRE(rig.h.Valid());
    CHECK(rig.app.Find(StringView("menu")) != nullptr);
    CHECK(rig.app.Find(StringView("toolbar")) != nullptr);
    CHECK(rig.app.Find(StringView("left_panel")) != nullptr);
    CHECK(rig.app.Find(StringView("viewport")) != nullptr);
    CHECK(rig.app.Find(StringView("status")) != nullptr);
    CHECK(rig.app.Items()->Shown());
    CHECK(rig.app.ItemsModel().ItemCount() == 1000);
    CHECK(rig.app.ViewportWidth() > 0);
    CHECK(rig.app.ViewportHeight() > 0);
    CHECK(rig.app.ViewportRenders() > 0);
}

// ---- the script and the behaviour of the tree ----

#include "ui_script.h"
#include "cstr/mem_ops.h"

namespace
{

class DemoHost final : public ui_demo::ScriptHost
{
public:
    void Resize(u32 width, u32 height) override
    {
        w = width;
        h = height;
    }
    void Screenshot(StringView) override { ++shots; }
    u32 w = 0;
    u32 h = 0;
    int shots = 0;
};

// Runs a script to its end, a frame per word at most.
bool RunScript(DemoRig& rig, const char* text, DemoHost* host = nullptr)
{
    ui_demo::UiScript script;
    script.Parse(StringView(text));
    DemoHost local;
    for (long frame = 0; frame < 400 && !script.Done(); ++frame)
    {
        if (!script.Run(&rig.app, &rig.h.GetSurface(), host != nullptr ? host : &local, frame))
        {
            FAIL(script.LastError().Data());
            return false;
        }
        rig.h.Advance(0.016f);
    }
    rig.h.Advance(0.016f);
    return script.Done();
}

}  // namespace

TEST_CASE("The script clicks, types and presses keys", "[ui][ui_demo]")
{
    DemoRig rig;
    REQUIRE(rig.h.Valid());
    REQUIRE(RunScript(rig, "click:btn_save"));
    CHECK(rig.app.Status() == StringView("Clicked btn_save"));
    REQUIRE(RunScript(rig, "click:text_name type:Ada_Lovelace"));
    CHECK(static_cast<TextField*>(rig.app.Find(StringView("text_name")))->Text() == StringView("Ada Lovelace"));
    REQUIRE(RunScript(rig, "click:check_wire"));
    CHECK(static_cast<CheckBox*>(rig.app.Find(StringView("check_wire")))->Checked());
    REQUIRE(RunScript(rig, "click:radio_large"));
    CHECK(static_cast<RadioButton*>(rig.app.Find(StringView("radio_large")))->Checked());
    CHECK_FALSE(static_cast<RadioButton*>(rig.app.Find(StringView("radio_medium")))->Checked());
}

TEST_CASE("The View menu switches the theme at run time and the status bar stays dark", "[ui][ui_demo]")
{
    DemoRig rig;
    REQUIRE(rig.h.Valid());
    RasterImage light;
    rig.h.Render(&light);
    const Rect status = rig.app.StatusBar()->Bounds();
    const u32 sx = static_cast<u32>(status.x + 4);
    const u32 sy = static_cast<u32>(status.y + status.h - 3);
    const u8* before_status = light.Pixel(sx, sy);
    const int before_status_sum = before_status[0] + before_status[1] + before_status[2];
    const Rect button = rig.app.Find(StringView("btn_new"))->Bounds();
    const u32 bx = static_cast<u32>(button.x + 3);
    const u32 by = static_cast<u32>(button.y + button.h / 2);
    const u8* before_panel = light.Pixel(bx, by);
    const int light_panel = before_panel[0] + before_panel[1] + before_panel[2];
    REQUIRE(RunScript(rig, "menu:View/Theme/Dark"));
    CHECK(rig.app.IsDark());
    RasterImage dark;
    rig.h.Render(&dark);
    const u8* dark_panel = dark.Pixel(bx, by);
    CHECK(light_panel > dark_panel[0] + dark_panel[1] + dark_panel[2] + 150);  // the whole tree restyled
    const u8* after_status = dark.Pixel(sx, sy);
    CHECK(after_status[0] + after_status[1] + after_status[2] == before_status_sum);  // the status bar was dark already
    REQUIRE(RunScript(rig, "theme:light"));
    CHECK_FALSE(rig.app.IsDark());
}

TEST_CASE("Menus open and choose, the accelerators work and the About box is modal", "[ui][ui_demo]")
{
    DemoRig rig;
    REQUIRE(rig.h.Valid());
    REQUIRE(RunScript(rig, "menu:File/New"));
    CHECK(rig.app.Status() == StringView("New"));
    REQUIRE(RunScript(rig, "openmenu:Edit"));
    CHECK(rig.app.GetMenuBar()->MenuOpen());
    REQUIRE(RunScript(rig, "key:Escape"));
    CHECK_FALSE(rig.app.GetMenuBar()->MenuOpen());
    CHECK_FALSE(rig.app.ChooseMenuItem(StringView("File"), StringView("Nothing")));
    REQUIRE(RunScript(rig, "menu:Help/About"));
    CHECK(rig.h.GetSurface().ModalLayer()->ChildCount() > 0);
    REQUIRE(RunScript(rig, "key:Enter"));
    CHECK(rig.h.GetSurface().ModalLayer()->ChildCount() == 0);
    CHECK_FALSE(rig.app.QuitRequested());
    REQUIRE(RunScript(rig, "key:ctrl+q"));
    CHECK(rig.app.QuitRequested());
}

TEST_CASE("F1 opens the About box", "[ui][ui_demo]")
{
    DemoRig rig;
    REQUIRE(rig.h.Valid());
    REQUIRE(RunScript(rig, "key:F1"));
    CHECK(rig.h.GetSurface().ModalLayer()->ChildCount() > 0);
}

TEST_CASE("Tab moves the focus through the controls in order and wraps", "[ui][ui_demo]")
{
    DemoRig rig;
    REQUIRE(rig.h.Valid());
    Vector<Widget*> seen;
    for (int i = 0; i < 40; ++i)
    {
        rig.h.Key(Key::Tab);
        Widget* f = rig.h.GetSurface().Focus();
        REQUIRE(f != nullptr);
        for (usize k = 0; k < seen.Size(); ++k)
        {
            if (seen[k] == f)
            {
                goto wrapped;
            }
        }
        seen.Push(f);
    }
wrapped:
    CHECK(seen.Size() >= 12);  // the buttons, the controls, the list box, the viewport
    bool has_list = false;
    bool has_text = false;
    for (usize k = 0; k < seen.Size(); ++k)
    {
        has_list = has_list || seen[k] == rig.app.Items();
        has_text = has_text || seen[k] == rig.app.Find(StringView("text_name"));
    }
    CHECK(has_list);
    CHECK(has_text);
}

TEST_CASE("Dropped text, a dropped file, a paste and the context menu reach their targets", "[ui][ui_demo]")
{
    DemoRig rig;
    REQUIRE(rig.h.Valid());
    TextField* name = static_cast<TextField*>(rig.app.Find(StringView("text_name")));
    REQUIRE(RunScript(rig, "drop:text:Dropped_words"));
    CHECK(name->Text().Length() > 0);
    REQUIRE(RunScript(rig, "drop:file:/tmp/scene.dat"));
    CHECK(rig.app.Status() == StringView("Dropped /tmp/scene.dat"));
    REQUIRE(RunScript(rig, "click:text_name key:ctrl+a paste:pasted"));
    CHECK(name->Text() == StringView("pasted"));
    REQUIRE(RunScript(rig, "context"));
    CHECK(rig.h.GetSurface().PopupLayer()->ChildCount() > 0);
    REQUIRE(RunScript(rig, "key:Escape"));
    CHECK(rig.h.GetSurface().PopupLayer()->ChildCount() == 0);
}

TEST_CASE("The list reorders by dragging a row and the wheel zooms the camera", "[ui][ui_demo]")
{
    DemoRig rig;
    REQUIRE(rig.h.Valid());
    const Rect b = rig.app.Items()->Bounds();
    const f32 x = b.x + 30;
    const f32 y = b.y + 12;  // the first row
    CHECK(rig.app.ItemsModel().ItemText(0) == StringView("Item 1"));
    rig.h.PointerMove(x, y);
    rig.h.PointerDown(x, y);
    for (int i = 1; i <= 10; ++i)
    {
        rig.h.PointerMove(x, y + 8.0f * static_cast<f32>(i));
    }
    rig.h.PointerUp(x, y + 80);
    CHECK(rig.app.ItemsModel().ItemText(0) != StringView("Item 1"));  // the row moved down
    ui::ViewportInput wheel;
    wheel.type = ui::ViewportInputType::Wheel;
    wheel.wheel_y = 2;
    const f32 before = rig.app.Camera().Distance();
    rig.app.OnInput(wheel);
    CHECK(wheel.handled);
    CHECK(rig.app.Camera().Distance() < before);
    ui::ViewportInput look;
    look.type = ui::ViewportInputType::Look;
    look.delta_x = 50;
    look.delta_y = 20;
    const f32 yaw = rig.app.Camera().Yaw();
    rig.app.OnInput(look);
    CHECK(rig.app.Camera().Yaw() < yaw);
}

TEST_CASE("The items model moves rows as a drag asks", "[ui][ui_demo]")
{
    ui_demo::ItemModel model(6);
    const usize moved[] = {0, 1};
    CHECK(model.MoveItems(moved, 2, 4));
    CHECK(model.ItemText(0) == StringView("Item 3"));
    CHECK(model.ItemText(1) == StringView("Item 4"));
    CHECK(model.ItemText(2) == StringView("Item 1"));
    CHECK(model.ItemText(3) == StringView("Item 2"));
    CHECK(model.ItemText(4) == StringView("Item 5"));
    const usize bad[] = {9};
    CHECK_FALSE(model.MoveItems(bad, 1, 0));
}

TEST_CASE("The viewport follows the window size and the script resizes", "[ui][ui_demo]")
{
    DemoRig rig;
    REQUIRE(rig.h.Valid());
    const u32 w = rig.app.ViewportWidth();
    rig.h.GetSurface().Resize(900, 600, 1.0f);
    rig.h.Advance(0.016f);
    CHECK(rig.app.ViewportWidth() < w);
    DemoHost host;
    REQUIRE(RunScript(rig, "resize:800x500 shot:out.ppm", &host));
    CHECK(host.w == 800);
    CHECK(host.h == 500);
    CHECK(host.shots == 1);
}

TEST_CASE("A script word that cannot be done is reported", "[ui][ui_demo]")
{
    DemoRig rig;
    REQUIRE(rig.h.Valid());
    ui_demo::UiScript script;
    script.Parse(StringView("click:no_such_widget"));
    DemoHost host;
    CHECK_FALSE(script.Run(&rig.app, &rig.h.GetSurface(), &host, 0));
    CHECK(script.LastError().Find(StringView("no_such_widget")) != script.LastError().Length());
}

// ---- the in-world screen ----

namespace
{

// Finds the viewport pixel whose ray meets the world screen at (u, v); false when none does.
bool PixelAt(DemoRig& rig, f32 u, f32 v, f32* px, f32* py)
{
    const u32 w = rig.app.ViewportWidth();
    const u32 h = rig.app.ViewportHeight();
    f32 best = 1e9f;
    for (u32 y = 0; y < h; y += 2)
    {
        for (u32 x = 0; x < w; x += 2)
        {
            Ray3 ray;
            if (!rig.app.Camera().MakeRay(static_cast<f32>(x), static_cast<f32>(y), w, h, rig.app.SceneScale(), &ray))
            {
                return false;
            }
            const SurfaceHit hit = RayToSurfacePoint(ray, ui_demo::WorldScreen::Quad());
            if (!hit.hit)
            {
                continue;
            }
            const f32 d = (hit.u - u) * (hit.u - u) + (hit.v - v) * (hit.v - v);
            if (d < best)
            {
                best = d;
                *px = static_cast<f32>(x);
                *py = static_cast<f32>(y);
            }
        }
    }
    return best < 0.0004f;
}

void Pointer(DemoRig& rig, ViewportInputType type, f32 x, f32 y)
{
    ViewportInput input;
    input.type = type;
    input.button = os::kMouseLeft;
    input.x = x;
    input.y = y;
    rig.app.OnInput(input);
}

}  // namespace

TEST_CASE("A click on the viewport reaches the in-world screen by a ray cast", "[ui][ui_demo]")
{
    DemoRig rig;
    ui_demo::WorldScreen& world = rig.app.World();
    world.GetSurface().SetFontProvider(&rig.h);
    rig.app.Camera().Set(0.3f, 0.2f, 2.5f);  // in front of the screen, close
    world.GetSurface().Frame(0);
    const ui::Rect button = world.GetSurface().Root()->FindByName(StringView("btn_counter"))->Bounds();
    const f32 u = (button.x + button.w * 0.5f) / ui_demo::WorldScreen::kWidth;
    const f32 v = (button.y + button.h * 0.5f) / ui_demo::WorldScreen::kHeight;
    f32 px = 0;
    f32 py = 0;
    REQUIRE(PixelAt(rig, u, v, &px, &py));
    CHECK(world.Clicks() == 0);
    Pointer(rig, ViewportInputType::PointerMove, px, py);
    Pointer(rig, ViewportInputType::PointerDown, px, py);
    Pointer(rig, ViewportInputType::PointerUp, px, py);
    world.GetSurface().Frame(0);
    CHECK(world.Clicks() == 1);
    CHECK(world.CounterLabel()->GetText() == StringView("1 clicks"));
    // A click that misses the screen does nothing.
    Pointer(rig, ViewportInputType::PointerMove, 2, 2);
    Pointer(rig, ViewportInputType::PointerDown, 2, 2);
    Pointer(rig, ViewportInputType::PointerUp, 2, 2);
    world.GetSurface().Frame(0);
    CHECK(world.Clicks() == 1);
}

TEST_CASE("The in-world screen has a theme of its own", "[ui][ui_demo]")
{
    DemoRig rig;
    rig.app.SetDark(false);
    CHECK(rig.app.World().GetSurface().Themes().GetTheme() != rig.h.GetSurface().Themes().GetTheme());
}

// ---- the rendering settings dialog ----

namespace
{

class FakeRenderer final : public ui_demo::RenderSettingsHost
{
public:
    bool Change(StringView key, StringView value) override
    {
        last_key = String<HeapAllocator>(HeapAllocator::Default(), key);
        last_value = String<HeapAllocator>(HeapAllocator::Default(), value);
        ++changes;
        return accept;
    }
    StringView CostOf(StringView key) override
    {
        if (key == StringView("present_mode"))
        {
            return StringView("swapchain");
        }
        if (key == StringView("render_scale"))
        {
            return StringView("dynamic");
        }
        return StringView("pipeline-rebuild");
    }
    String<HeapAllocator> last_key = String<HeapAllocator>(HeapAllocator::Default(), StringView());
    String<HeapAllocator> last_value = String<HeapAllocator>(HeapAllocator::Default(), StringView());
    int changes = 0;
    bool accept = true;
};

}  // namespace

TEST_CASE("The rendering settings dialog shows each setting's cost class and applies a change through the host",
          "[ui][ui_demo]")
{
    DemoRig rig;
    FakeRenderer renderer;
    rig.app.SetRenderSettingsHost(&renderer);
    CHECK(rig.app.ChooseMenuItem(StringView("View"), StringView("Rendering settings...")));
    rig.h.Advance(0.016f);
    Label* cost = static_cast<Label*>(rig.app.Find(StringView("cost_present_mode")));
    REQUIRE(cost != nullptr);
    CHECK(cost->GetText() == StringView("swapchain"));
    CHECK(static_cast<Label*>(rig.app.Find(StringView("cost_render_scale")))->GetText() == StringView("dynamic"));
    CHECK(static_cast<Label*>(rig.app.Find(StringView("cost_shader_quality")))->GetText() ==
          StringView("pipeline-rebuild"));
    ComboBox* combo = static_cast<ComboBox*>(rig.app.Find(StringView("combo_shader_quality")));
    REQUIRE(combo != nullptr);
    rig.h.GetSurface().SetFocus(combo);
    rig.h.Key(Key::Up);
    CHECK(renderer.changes == 1);
    CHECK(renderer.last_key.View() == StringView("shader_quality"));
    CHECK(renderer.last_value.View() == StringView("medium"));  // it starts at "high"
    CHECK(rig.app.Status().Length() > 0);
}

TEST_CASE("The rendering settings dialog opens again showing the choice made in it", "[ui][ui_demo]")
{
    DemoRig rig;
    FakeRenderer renderer;
    rig.app.SetRenderSettingsHost(&renderer);
    REQUIRE(rig.app.ChooseMenuItem(StringView("View"), StringView("Rendering settings...")));
    rig.h.Advance(0.016f);
    ComboBox* combo = static_cast<ComboBox*>(rig.app.Find(StringView("combo_shader_quality")));
    REQUIRE(combo != nullptr);
    rig.h.GetSurface().SetFocus(combo);
    rig.h.Key(Key::Up);  // high -> medium
    REQUIRE(renderer.changes == 1);
    rig.h.Key(Key::Escape);
    rig.h.Advance(0.016f);
    REQUIRE(rig.app.ChooseMenuItem(StringView("View"), StringView("Rendering settings...")));
    rig.h.Advance(0.016f);
    combo = static_cast<ComboBox*>(rig.app.Find(StringView("combo_shader_quality")));
    REQUIRE(combo != nullptr);
    CHECK(combo->Text().View() == StringView("medium"));  // it used to show "high" again, the first value of every open
}

TEST_CASE("A script word that cannot be done is reported whole, however long it is", "[ui][ui_demo]")
{
    DemoRig rig;
    ui_demo::UiScript script;
    char word[400];
    MemSet(word, 'x', sizeof(word) - 1);
    word[sizeof(word) - 1] = '\0';
    script.Parse(StringView(word));
    DemoHost host;
    CHECK_FALSE(script.Run(&rig.app, &rig.h.GetSurface(), &host, 0));
    CHECK(script.LastError().Length() < 256);  // the message is cut to its buffer; it used to read past the end of it
    CHECK(script.LastError().Length() > 200);
}
