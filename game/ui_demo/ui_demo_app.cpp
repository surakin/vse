#include "ui_demo_app.h"

#include <cmath>
#include <cstdio>

#include "platform/input.h"
#include "ui/widgets/display.h"
#include "ui/widgets/message_box.h"

namespace ui_demo {

using namespace engine_placeholder::ui;

// ---- OrbitCamera ----

bool OrbitCamera::Apply(ViewportInput& input) {
    switch (input.type) {
        case ViewportInputType::Look:
            yaw_ -= input.delta_x * kRadiansPerPixel;
            pitch_ += input.delta_y * kRadiansPerPixel;
            if (pitch_ > kMaxPitch) pitch_ = kMaxPitch;
            if (pitch_ < -kMaxPitch) pitch_ = -kMaxPitch;
            return true;
        case ViewportInputType::Wheel:
            distance_ *= std::pow(kZoomPerNotch, input.wheel_y);
            if (distance_ < kMinDistance) distance_ = kMinDistance;
            if (distance_ > kMaxDistance) distance_ = kMaxDistance;
            input.handled = true;
            return true;
        default:
            return false;
    }
}

void OrbitCamera::Eye(f32 scene_scale, f32 out[3]) const {
    const f32 cp = std::cos(pitch_);
    const f32 d = distance_ * scene_scale;
    out[0] = cp * std::sin(yaw_) * d;
    out[1] = std::sin(pitch_) * d;
    out[2] = cp * std::cos(yaw_) * d;
}

bool OrbitCamera::MakeRay(f32 x, f32 y, u32 width, u32 height, f32 scene_scale, ui::Ray3* out) const {
    if (width == 0 || height == 0) return false;
    f32 eye[3];
    f32 target[3];
    Eye(scene_scale, eye);
    Target(target);
    auto normalize = [](f32 v[3]) {
        const f32 length = std::sqrt(v[0] * v[0] + v[1] * v[1] + v[2] * v[2]);
        for (int i = 0; i < 3; ++i) v[i] /= length;
    };
    f32 forward[3] = {target[0] - eye[0], target[1] - eye[1], target[2] - eye[2]};
    normalize(forward);
    // right = forward x up(0,1,0)
    f32 right[3] = {-forward[2], 0, forward[0]};
    normalize(right);
    const f32 up[3] = {right[1] * forward[2] - right[2] * forward[1], right[2] * forward[0] - right[0] * forward[2], right[0] * forward[1] - right[1] * forward[0]};
    const f32 half = std::tan(kFovY * 0.5f);
    const f32 aspect = static_cast<f32>(width) / static_cast<f32>(height);
    const f32 nx = (2.0f * x / static_cast<f32>(width) - 1.0f) * half * aspect;
    const f32 ny = (1.0f - 2.0f * y / static_cast<f32>(height)) * half;
    for (int i = 0; i < 3; ++i) {
        out->origin[i] = eye[i];
        out->direction[i] = forward[i] + right[i] * nx + up[i] * ny;
    }
    return true;
}

void OrbitCamera::Set(f32 yaw, f32 pitch, f32 distance) {
    yaw_ = yaw;
    pitch_ = pitch;
    distance_ = distance;
}

// ---- models ----

namespace {

String<HeapAllocator> OwnedText(StringView text) { return String<HeapAllocator>(HeapAllocator::Default(), text); }

} // namespace

ItemModel::ItemModel(usize count) {
    for (usize i = 0; i < count; ++i) {
        char text[32];
        const int n = std::snprintf(text, sizeof(text), "Item %zu", i + 1);
        items_.Push(OwnedText(StringView(text, static_cast<usize>(n))));
    }
}

bool ItemModel::MoveItems(const usize* indices, usize count, usize to) {
    if (count == 0 || to > items_.Size()) return false;
    Vector<String<HeapAllocator>> next;
    Vector<bool> taken;
    taken.Resize(items_.Size());
    for (usize i = 0; i < items_.Size(); ++i) taken[i] = false;
    for (usize i = 0; i < count; ++i) {
        if (indices[i] >= items_.Size() || taken[indices[i]]) return false;
        taken[indices[i]] = true;
    }
    for (usize i = 0; i < to; ++i) {
        if (!taken[i]) next.Push(OwnedText(items_[i].View()));
    }
    for (usize i = 0; i < count; ++i) next.Push(OwnedText(items_[indices[i]].View()));
    for (usize i = to; i < items_.Size(); ++i) {
        if (!taken[i]) next.Push(OwnedText(items_[i].View()));
    }
    items_ = static_cast<Vector<String<HeapAllocator>>&&>(next);
    return true;
}

StringView ShapeModel::ItemText(usize index) {
    static const char* const kNames[] = {"Cube", "Sphere", "Plane"};
    return StringView(kNames[index < 3 ? index : 0]);
}

// ---- the tree ----

struct UiDemoApp::MenuCommand {
    UiDemoApp* app = nullptr;
    void (UiDemoApp::*fn)() = nullptr;
};

// Wraps the left panel: a right click that no control inside took opens the context menu. (A plain widget with the panel as its only child: the toolkit's library is built without
// run-time type information, so a class made here may derive from `Widget` but not from a widget with out-of-line virtuals.)
class UiDemoApp::ContextPanel final : public Widget {
public:
    ContextPanel(UiDemoApp* app, StringView name) : Widget(name), app_(app) {}
    void OnEvent(Event& event) override {
        if (event.type == EventType::PointerDown && event.button == os::kMouseRight) {
            // `event.position` is local to this panel; the menu wants surface coordinates.
            app_->ShowContext(ui::Point{Bounds().x + event.position.x, Bounds().y + event.position.y});
            event.handled = true;
        }
    }

private:
    UiDemoApp* app_;
};

void UiDemoApp::RunCommand(void* user, usize) {
    MenuCommand* c = static_cast<MenuCommand*>(user);
    (c->app->*c->fn)();
}

void UiDemoApp::AddCommand(Menu* menu, const char* text, void (UiDemoApp::*fn)()) {
    UniquePtr<MenuCommand> command = MakeUnique<MenuCommand>();
    command->app = this;
    command->fn = fn;
    menu->AddItem(StringView(text), &UiDemoApp::RunCommand, command.Get());
    commands_.Push(static_cast<UniquePtr<MenuCommand>&&>(command));
}

void UiDemoApp::ToggleToolbarCommand() {
    toolbar_->SetCollapsed(!toolbar_->Collapsed());
    SetStatus(toolbar_->Collapsed() ? StringView("Toolbar hidden") : StringView("Toolbar shown"));
}

void UiDemoApp::Clicked(void* user, Button* button) {
    UiDemoApp* app = static_cast<UiDemoApp*>(user);
    char text[96];
    const int n = std::snprintf(text, sizeof(text), "Clicked %.*s", static_cast<int>(button->Name().Length()), button->Name().Data());
    app->SetStatus(StringView(text, static_cast<usize>(n)));
}

void UiDemoApp::SliderChanged(void* user, Slider* slider) {
    char text[64];
    const int n = std::snprintf(text, sizeof(text), "Speed %.0f", slider->Value());
    static_cast<UiDemoApp*>(user)->SetStatus(StringView(text, static_cast<usize>(n)));
}

void UiDemoApp::ComboChanged(void* user, ComboBox* combo) {
    char text[64];
    const String<HeapAllocator> shown = combo->Text();
    const int n = std::snprintf(text, sizeof(text), "Shape %s", shown.CStr());
    static_cast<UiDemoApp*>(user)->SetStatus(StringView(text, static_cast<usize>(n)));
}

void UiDemoApp::BuildMenus() {
    file_menu_ = MakeUnique<Menu>();
    AddCommand(file_menu_.Get(), "&New", &UiDemoApp::NewCommand);
    AddCommand(file_menu_.Get(), "&Open...", &UiDemoApp::OpenCommand);
    AddCommand(file_menu_.Get(), "&Save", &UiDemoApp::SaveCommand);
    file_menu_->AddSeparator();
    AddCommand(file_menu_.Get(), "&Quit", &UiDemoApp::QuitCommand);
    KeyChord quit;
    quit.key = Key::Q;
    quit.modifiers = os::kModCtrl;
    quit_accelerator_ = surface_->RegisterAccelerator(
        quit, [](void* user) { static_cast<UiDemoApp*>(user)->RequestQuit(); }, this);
    file_menu_->SetItemAccelerator(file_menu_->ItemCount() - 1, quit_accelerator_);

    edit_menu_ = MakeUnique<Menu>();
    edit_first_ = AddEditItems(*edit_menu_, surface_);

    view_menu_ = MakeUnique<Menu>();
    UniquePtr<Menu> theme = MakeUnique<Menu>();
    theme_menu_ = theme.Get();
    {
        UniquePtr<MenuCommand> light = MakeUnique<MenuCommand>();
        light->app = this;
        light->fn = &UiDemoApp::LightCommand;
        theme->AddRadio(StringView("&Light"), true, &UiDemoApp::RunCommand, light.Get());
        commands_.Push(static_cast<UniquePtr<MenuCommand>&&>(light));
        UniquePtr<MenuCommand> dark = MakeUnique<MenuCommand>();
        dark->app = this;
        dark->fn = &UiDemoApp::DarkCommand;
        theme->AddRadio(StringView("&Dark"), false, &UiDemoApp::RunCommand, dark.Get());
        commands_.Push(static_cast<UniquePtr<MenuCommand>&&>(dark));
    }
    view_menu_->AddSubmenu(StringView("&Theme"), static_cast<UniquePtr<Menu>&&>(theme));
    {
        UniquePtr<MenuCommand> tb = MakeUnique<MenuCommand>();
        tb->app = this;
        tb->fn = &UiDemoApp::ToggleToolbarCommand;
        view_menu_->AddCheck(StringView("Tool&bar"), true, &UiDemoApp::RunCommand, tb.Get());
        commands_.Push(static_cast<UniquePtr<MenuCommand>&&>(tb));
    }
    AddCommand(view_menu_.Get(), "&Rendering settings...", &UiDemoApp::RenderingCommand);

    help_menu_ = MakeUnique<Menu>();
    AddCommand(help_menu_.Get(), "&About", &UiDemoApp::AboutCommand);
    KeyChord about;
    about.key = Key::F1;
    about_accelerator_ = surface_->RegisterAccelerator(
        about, [](void* user) { static_cast<UiDemoApp*>(user)->ShowAbout(); }, this);
    help_menu_->SetItemAccelerator(0, about_accelerator_);
}

void UiDemoApp::BuildToolbar(BoxLayout* column) {
    UniquePtr<BoxLayout> bar = MakeUnique<BoxLayout>(Orientation::Horizontal, StringView("toolbar"));
    bar->SetSpacing(4.0f);
    static const char* const kNames[] = {"btn_new", "btn_open", "btn_save"};
    static const char* const kTexts[] = {"New", "Open", "Save"};
    static const char* const kTips[] = {"Start a new scene", "Open a scene", "Save the scene"};
    for (usize i = 0; i < 3; ++i) {
        UniquePtr<Button> b = MakeUnique<Button>(StringView(kNames[i]), StringView(kTexts[i]));
        b->SetTooltip(StringView(kTips[i]));
        b->OnClick().Connect(&UiDemoApp::Clicked, this);
        bar->AddChild(UniquePtr<Widget>(b.Release()));
    }
    UniquePtr<ToggleButton> grid = MakeUnique<ToggleButton>(StringView("tgl_grid"), StringView("Grid"));
    grid->SetTooltip(StringView("Show the grid"));
    bar->AddChild(UniquePtr<Widget>(grid.Release()));
    toolbar_ = bar.Get();
    column->AddChild(UniquePtr<Widget>(bar.Release()));
}

Widget* UiDemoApp::BuildLeftPanel() {
    UniquePtr<ContextPanel> area = MakeUnique<ContextPanel>(this, StringView("left_area"));
    UniquePtr<BoxLayout> panel = MakeUnique<BoxLayout>(Orientation::Vertical, StringView("left_panel"));
    LayoutProps fill_panel;
    fill_panel.fill_parent = true;
    panel->SetProps(fill_panel);
    panel->SetSpacing(6.0f);
    Insets pad;
    pad.left = pad.top = pad.right = pad.bottom = 6.0f;
    panel->SetPadding(pad);
    UniquePtr<Label> title = MakeUnique<Label>(StringView("label_title"), StringView("Scene"));
    title->SetVariant(StringView("heading"));
    panel->AddChild(UniquePtr<Widget>(title.Release()));
    UniquePtr<CheckBox> wire = MakeUnique<CheckBox>(StringView("check_wire"), StringView("Wireframe"));
    wire->SetTooltip(StringView("Draw the cube as lines"));
    panel->AddChild(UniquePtr<Widget>(wire.Release()));
    UniquePtr<RadioGroup> group = MakeUnique<RadioGroup>(StringView("radio_group"));
    static const char* const kSizes[] = {"Small", "Medium", "Large"};
    static const char* const kSizeNames[] = {"radio_small", "radio_medium", "radio_large"};
    for (usize i = 0; i < 3; ++i) {
        UniquePtr<RadioButton> r = MakeUnique<RadioButton>(StringView(kSizeNames[i]), StringView(kSizes[i]));
        RadioButton* raw = r.Get();
        group->AddChild(UniquePtr<Widget>(r.Release()));
        if (i == 1) group->Select(raw);
    }
    panel->AddChild(UniquePtr<Widget>(group.Release()));
    UniquePtr<ComboBox> combo = MakeUnique<ComboBox>(StringView("combo_shape"));
    combo->SetModel(&shapes_);
    combo->Select(0);
    combo->OnSelectionChanged().Connect(&UiDemoApp::ComboChanged, this);
    panel->AddChild(UniquePtr<Widget>(combo.Release()));
    UniquePtr<Slider> slider = MakeUnique<Slider>(StringView("slider_speed"));
    ValueRange speed;
    speed.min = 0;
    speed.max = 100;
    speed.integer = true;
    slider->SetRange(speed);
    slider->SetValue(40);
    slider->OnCommit().Connect(&UiDemoApp::SliderChanged, this);
    slider->SetTooltip(StringView("Rotation speed"));
    panel->AddChild(UniquePtr<Widget>(slider.Release()));
    UniquePtr<SpinBox> spin = MakeUnique<SpinBox>(StringView("spin_count"));
    ValueRange count;
    count.min = 1;
    count.max = 99;
    count.integer = true;
    spin->SetRange(count);
    spin->SetValue(3);
    panel->AddChild(UniquePtr<Widget>(spin.Release()));
    UniquePtr<TextField> name = MakeUnique<TextField>(StringView("text_name"));
    name->SetPlaceholder(StringView("Name"));
    panel->AddChild(UniquePtr<Widget>(name.Release()));
    UniquePtr<ListBox> list = MakeUnique<ListBox>(StringView("list_items"));
    list->SetModel(&items_);
    list->SetReorderable(true);
    LayoutProps grow;
    grow.weight = 1;
    list->SetProps(grow);
    items_view_ = list.Get();
    panel->AddChild(UniquePtr<Widget>(list.Release()));
    area->AddChild(UniquePtr<Widget>(panel.Release()));
    return area.Release();
}

UiDemoApp::UiDemoApp(Surface* surface) : surface_(surface), items_(kItemCount) {
    BuildMenus();
    UniquePtr<BoxLayout> column = MakeUnique<BoxLayout>(Orientation::Vertical, StringView("column"));
    LayoutProps fill;
    fill.fill_parent = true;
    column->SetProps(fill);
    column->SetSpacing(4.0f);

    UniquePtr<MenuBar> bar = MakeUnique<MenuBar>(StringView("menu"));
    bar->AddMenu(StringView("&File"), file_menu_.Get());
    bar->AddMenu(StringView("&Edit"), edit_menu_.Get());
    bar->AddMenu(StringView("&View"), view_menu_.Get());
    bar->AddMenu(StringView("&Help"), help_menu_.Get());
    menu_bar_ = bar.Get();
    column->AddChild(UniquePtr<Widget>(bar.Release()));
    BuildToolbar(column.Get());

    UniquePtr<Splitter> split = MakeUnique<Splitter>(Orientation::Horizontal, StringView("splitter"));
    split->AddPane(UniquePtr<Widget>(BuildLeftPanel()), 1.0f);
    UniquePtr<ui::Viewport> viewport = MakeUnique<ui::Viewport>(StringView("viewport"));
    viewport->SetBinding(this);
    viewport_ = viewport.Get();
    split->AddPane(UniquePtr<Widget>(viewport.Release()), 3.0f);
    split->SetMinSize(0, 160.0f);
    split->SetMinSize(1, 160.0f);
    LayoutProps split_props;
    split_props.weight = 1;
    split->SetProps(split_props);
    column->AddChild(UniquePtr<Widget>(split.Release()));

    // The status bar is dark whatever the window's theme: a subtree override.
    UniquePtr<Panel> status_bar = MakeUnique<Panel>(StringView("status_bar"));
    status_bar->SetThemeOverride(&DefaultDarkTheme());
    UniquePtr<Label> status = MakeUnique<Label>(StringView("status"), StringView(kStatusReady));
    status_ = status.Get();
    status_bar->AddChild(UniquePtr<Widget>(status.Release()));
    status_bar_ = status_bar.Get();
    column->AddChild(UniquePtr<Widget>(status_bar.Release()));

    // A panel under everything paints the theme's background (the surface's root draws nothing).
    UniquePtr<Panel> window = MakeUnique<Panel>(StringView("window"));
    window->SetProps(fill);
    window->AddChild(UniquePtr<Widget>(column.Release()));
    surface_->Root()->AddChild(UniquePtr<Widget>(window.Release()));
    files_connection_ = surface_->OnFilesDropped().Connect(
        [](void* user, Surface*, StringView paths) {
            String<HeapAllocator> text(HeapAllocator::Default(), StringView("Dropped "));
            text.Append(paths);
            static_cast<UiDemoApp*>(user)->SetStatus(text.View());
        },
        this);
    SetDark(false);
}

UiDemoApp::~UiDemoApp() {
    surface_->OnFilesDropped().Disconnect(files_connection_);
    if (viewport_ != nullptr) viewport_->SetBinding(nullptr);
    if (quit_accelerator_ != 0) surface_->UnregisterAccelerator(quit_accelerator_);
    if (about_accelerator_ != 0) surface_->UnregisterAccelerator(about_accelerator_);
}

void UiDemoApp::SetDark(bool dark) {
    dark_ = dark;
    surface_->SetTheme(dark ? &DefaultDarkTheme() : &DefaultLightTheme());
    if (theme_menu_ != nullptr) {
        theme_menu_->SetItemChecked(0, !dark);
        theme_menu_->SetItemChecked(1, dark);
    }
    SetStatus(dark ? StringView("Dark theme") : StringView("Light theme"));
}

void UiDemoApp::ShowAbout() {
    ShowMessageBox(surface_, StringView("About ui_demo"), StringView("ui_demo shows the engine's own widget toolkit: menus, a viewport with a lit 3D scene, themes and drag and drop."), MessageButtons::Ok, nullptr, nullptr);
}

void UiDemoApp::RenderingChanged(void* user, ComboBox* combo) {
    UiDemoApp* app = static_cast<UiDemoApp*>(user);
    if (app->render_host_ == nullptr) return;
    const String<HeapAllocator> value = combo->Text();
    // The combo boxes are named "combo_<setting>".
    const StringView name = combo->Name();
    const StringView key(name.Data() + 6, name.Length() - 6);
    const bool ok = app->render_host_->Change(key, value.View());
    char text[160];
    const StringView cost = app->render_host_->CostOf(key);
    const int n = std::snprintf(text, sizeof(text), "%.*s = %s: %s (%.*s)", static_cast<int>(key.Length()), key.Data(), value.CStr(), ok ? "applied" : "refused", static_cast<int>(cost.Length()), cost.Data());
    app->SetStatus(StringView(text, static_cast<usize>(n)));
}

void UiDemoApp::ShowRenderingSettings() {
    static const char* const kPresent[] = {"fifo", "mailbox", "immediate"};
    static const char* const kScale[] = {"0.5", "0.75", "1.0", "1.5", "2.0"};
    static const char* const kQuality[] = {"low", "medium", "high"};
    static ChoiceModel present_model(kPresent, 3);
    static ChoiceModel scale_model(kScale, 5);
    static ChoiceModel quality_model(kQuality, 3);
    struct Row {
        const char* title;
        const char* key;
        ChoiceModel* model;
        usize selected;
    };
    const Row rows[] = {{"Present mode", "present_mode", &present_model, 0}, {"Render scale", "render_scale", &scale_model, 2}, {"Shader quality", "shader_quality", &quality_model, 2}};
    UniquePtr<Dialog> dialog = MakeUnique<Dialog>(StringView("rendering_dialog"), StringView("Rendering settings"));
    UniquePtr<BoxLayout> column = MakeUnique<BoxLayout>(Orientation::Vertical, StringView("rendering_column"));
    column->SetSpacing(8.0f);
    for (const Row& row : rows) {
        UniquePtr<BoxLayout> line = MakeUnique<BoxLayout>(Orientation::Horizontal, StringView());
        line->SetSpacing(8.0f);
        UniquePtr<Label> title = MakeUnique<Label>(StringView(), StringView(row.title));
        line->AddChild(UniquePtr<Widget>(title.Release()));
        char combo_name[64];
        std::snprintf(combo_name, sizeof(combo_name), "combo_%s", row.key);
        UniquePtr<ComboBox> combo = MakeUnique<ComboBox>(StringView(combo_name));
        combo->SetModel(row.model);
        combo->Select(row.selected);
        combo->OnSelectionChanged().Connect(&UiDemoApp::RenderingChanged, this);
        line->AddChild(UniquePtr<Widget>(combo.Release()));
        char cost_name[64];
        std::snprintf(cost_name, sizeof(cost_name), "cost_%s", row.key);
        const StringView cost = render_host_ != nullptr ? render_host_->CostOf(StringView(row.key)) : StringView("no renderer");
        UniquePtr<Label> cost_label = MakeUnique<Label>(StringView(cost_name), cost);
        line->AddChild(UniquePtr<Widget>(cost_label.Release()));
        column->AddChild(UniquePtr<Widget>(line.Release()));
    }
    dialog->SetContent(UniquePtr<Widget>(column.Release()));
    dialog->AddButton(StringView("Close"), 0, true);
    surface_->ShowModal(UniquePtr<Widget>(dialog.Release()));
}

void UiDemoApp::ShowContext(ui::Point position) {
    UniquePtr<Menu> menu = MakeUnique<Menu>();
    menu->AddItem(StringView("Reset view"), [](void* user, usize) { static_cast<UiDemoApp*>(user)->Camera().Set(0.45f, 0.3f, 3.2f); }, this);
    menu->AddItem(StringView("About"), [](void* user, usize) { static_cast<UiDemoApp*>(user)->ShowAbout(); }, this);
    // The view owns the menu it is given for as long as it is open.
    surface_->ShowContextMenu(MakeUnique<MenuView>(static_cast<UniquePtr<Menu>&&>(menu)), position);
}

void UiDemoApp::SetStatus(StringView text) {
    if (status_ != nullptr) status_->SetText(text);
}

StringView UiDemoApp::Status() const { return status_ != nullptr ? status_->GetText() : StringView(); }

Widget* UiDemoApp::Find(StringView name) {
    if (Widget* w = surface_->Root()->FindByName(name)) return w;
    if (Widget* w = surface_->PopupLayer()->FindByName(name)) return w;
    return surface_->ModalLayer()->FindByName(name);
}

namespace {

// Whether menu text `text` (with `&` mnemonics) reads `wanted`.
bool SameLabel(StringView text, StringView wanted) {
    usize w = 0;
    for (usize i = 0; i < text.Length(); ++i) {
        if (text[i] == '&' && i + 1 < text.Length() && text[i + 1] != '&') continue;
        if (w >= wanted.Length() || wanted[w] != text[i]) return false;
        ++w;
    }
    return w == wanted.Length();
}

} // namespace

usize UiDemoApp::MenuIndex(StringView title) const {
    static const char* const kTitles[] = {"File", "Edit", "View", "Help"};
    for (usize i = 0; i < 4; ++i) {
        if (SameLabel(StringView(kTitles[i]), title)) return i;
    }
    return MenuBar::kNone;
}

bool UiDemoApp::OpenMenu(StringView title) {
    const usize index = MenuIndex(title);
    if (index == MenuBar::kNone) return false;
    menu_bar_->OpenMenu(index);
    return true;
}

bool UiDemoApp::ChooseMenuItem(StringView title, StringView path) {
    const usize index = MenuIndex(title);
    if (index == MenuBar::kNone) return false;
    Menu* menu = index == 0 ? file_menu_.Get() : index == 1 ? edit_menu_.Get() : index == 2 ? view_menu_.Get() : help_menu_.Get();
    while (menu != nullptr) {
        const usize slash = path.Find(StringView("/"));
        const StringView part = path.Substring(0, slash);
        usize found = menu->ItemCount();
        for (usize i = 0; i < menu->ItemCount(); ++i) {
            if (SameLabel(menu->Item(i).text.View(), part)) {
                found = i;
                break;
            }
        }
        if (found == menu->ItemCount()) return false;
        if (slash >= path.Length()) return menu->Choose(found);
        menu = menu->Submenu(found);
        path = path.Substring(slash + 1);
    }
    return false;
}

void UiDemoApp::OnResize(u32 width, u32 height) {
    viewport_w_ = width;
    viewport_h_ = height;
    ++resizes_;
}

void UiDemoApp::Render(const ViewportFrame&) { ++renders_; }

void UiDemoApp::OnInput(ViewportInput& input) {
    if (camera_.Apply(input)) return;
    // The pointer over the in-world screen: a ray from the camera through the pointer; a hit moves and presses the screen's own pointer.
    if (input.type != ViewportInputType::PointerMove && input.type != ViewportInputType::PointerDown && input.type != ViewportInputType::PointerUp) return;
    Ray3 ray;
    SurfaceHit hit;
    if (camera_.MakeRay(input.x, input.y, viewport_w_, viewport_h_, scene_scale_, &ray)) hit = RayToSurfacePoint(ray, WorldScreen::Quad());
    SurfaceInput& screen = world_.Input();
    if (input.type == ViewportInputType::PointerUp) {
        if (hit.hit) screen.PointerButtonUv(hit.u, hit.v, input.button, false, input.modifiers);
        else screen.Lost();
    } else if (hit.hit) {
        if (input.type == ViewportInputType::PointerDown) screen.PointerButtonUv(hit.u, hit.v, input.button, true, input.modifiers);
        else screen.PointerMoveUv(hit.u, hit.v, input.modifiers);
    } else if (input.type == ViewportInputType::PointerMove) {
        screen.Lost();
    }
}

// ---- the in-world screen ----

WorldScreen::WorldScreen() : surface_(kWidth, kHeight, kScale), input_(&surface_) {
    surface_.SetTheme(&DefaultDarkTheme());
    UniquePtr<Panel> panel = MakeUnique<Panel>(StringView("world_panel"));
    LayoutProps fill;
    fill.fill_parent = true;
    panel->SetProps(fill);
    UniquePtr<BoxLayout> column = MakeUnique<BoxLayout>(Orientation::Vertical, StringView("world_column"));
    column->SetSpacing(12.0f);
    UniquePtr<Label> title = MakeUnique<Label>(StringView("world_title"), StringView("In-world screen"));
    column->AddChild(UniquePtr<Widget>(title.Release()));
    UniquePtr<Button> button = MakeUnique<Button>(StringView("btn_counter"), StringView("Count"));
    button->OnClick().Connect(&WorldScreen::Clicked, this);
    button_ = button.Get();
    column->AddChild(UniquePtr<Widget>(button.Release()));
    UniquePtr<Label> label = MakeUnique<Label>(StringView("label_count"), StringView("0 clicks"));
    label_ = label.Get();
    column->AddChild(UniquePtr<Widget>(label.Release()));
    panel->AddChild(UniquePtr<Widget>(column.Release()));
    surface_.Root()->AddChild(UniquePtr<Widget>(panel.Release()));
    input_.SetInteractive(true);
}

SurfaceQuad WorldScreen::Quad() {
    SurfaceQuad quad;
    quad.corner[0] = kWorldScreenCenter[0] - kWorldScreenWidth * 0.5f;
    quad.corner[1] = kWorldScreenCenter[1] + kWorldScreenHeight * 0.5f;
    quad.corner[2] = kWorldScreenCenter[2];
    quad.edge_u[0] = kWorldScreenWidth;
    quad.edge_u[1] = 0;
    quad.edge_u[2] = 0;
    quad.edge_v[0] = 0;
    quad.edge_v[1] = -kWorldScreenHeight;
    quad.edge_v[2] = 0;
    return quad;
}

void WorldScreen::Clicked(void* user, Button*) {
    WorldScreen* self = static_cast<WorldScreen*>(user);
    ++self->clicks_;
    char text[32];
    const int n = std::snprintf(text, sizeof(text), "%u clicks", self->clicks_);
    self->label_->SetText(StringView(text, static_cast<usize>(n)));
}

} // namespace ui_demo
