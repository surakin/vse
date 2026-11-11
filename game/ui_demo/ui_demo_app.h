#pragma once

// The ui_demo application (Phase 15, Day 204): the widget tree of the demo, its menus, themes and orbit camera. It names no window and no GPU, so the tests build the same tree on a
// ui::Harness; main.cpp puts it in a window.

#include "containers/unique_ptr.h"
#include "containers/vector.h"
#include "string/string.h"
#include "types.h"
#include "ui/layout.h"
#include "ui/surface.h"
#include "ui/surface_input.h"
#include "ui/viewport.h"
#include "ui/widgets/buttons.h"
#include "ui/widgets/combobox.h"
#include "ui/widgets/dialog.h"
#include "ui/widgets/label.h"
#include "ui/widgets/list_box.h"
#include "ui/widgets/menu.h"
#include "ui/widgets/menu_bar.h"
#include "ui/widgets/splitter.h"
#include "ui/widgets/text_field.h"
#include "ui/widgets/value_widgets.h"

namespace ui_demo {

/// The length of what `snprintf` wrote into a buffer of `capacity` bytes: it returns the length it would have written, which can pass the buffer.
inline engine_placeholder::usize FittedLength(int written, engine_placeholder::usize capacity) {
    if (written <= 0) return 0;
    return static_cast<engine_placeholder::usize>(written) < capacity ? static_cast<engine_placeholder::usize>(written) : capacity - 1;
}

using namespace engine_placeholder;

/// An orbit camera around the origin that the viewport's input turns: dragging with the look button changes the angles, the wheel the distance.
///
/// - Thread-safety: owner-thread-only.
class OrbitCamera {
public:
    /// The vertical field of view of the demo's camera, in radians.
    static constexpr f32 kFovY = 0.9f;
    static constexpr f32 kRadiansPerPixel = 0.01f;
    static constexpr f32 kMinDistance = 2.0f;
    static constexpr f32 kMaxDistance = 40.0f;
    static constexpr f32 kZoomPerNotch = 0.9f;
    static constexpr f32 kMaxPitch = 1.45f;

    /// Applies one viewport input event; returns whether it changed the camera.
    bool Apply(ui::ViewportInput& input);
    f32 Yaw() const { return yaw_; }
    f32 Pitch() const { return pitch_; }
    f32 Distance() const { return distance_; }
    void Set(f32 yaw, f32 pitch, f32 distance);
    /// Writes the eye position (the orbit distance times `scene_scale` from the point looked at).
    void Eye(f32 scene_scale, f32 out[3]) const;
    /// The point the camera looks at.
    static void Target(f32 out[3]) {
        out[0] = 0;
        out[1] = 0.5f;
        out[2] = 0;
    }
    /// Makes the world ray through the point (`x`, `y`) of a `width` by `height` picture.
    /// - Returns: false when the size is 0.
    bool MakeRay(f32 x, f32 y, u32 width, u32 height, f32 scene_scale, ui::Ray3* out) const;

private:
    f32 yaw_ = 0.45f;
    f32 pitch_ = 0.3f;
    f32 distance_ = 3.2f;
};

/// Where the in-world screen stands (the scene puts its quad there and the ray cast aims at it), in world units: its middle, its size, facing +z.
constexpr f32 kWorldScreenCenter[3] = {0.0f, 2.1f, -3.6f};
constexpr f32 kWorldScreenWidth = 4.8f;
constexpr f32 kWorldScreenHeight = 3.0f;

/// The second surface of the demo: a small screen (a counter button and a label, a theme of its own) that the 3D scene shows on a quad and the viewport's pointer reaches by a ray cast.
///
/// - Thread-safety: owner-thread-only.
/// - Ownership: owns its surface, its widgets and its input.
class WorldScreen {
public:
    static constexpr f32 kWidth = 320.0f;  ///< The surface's width in logical units.
    static constexpr f32 kHeight = 200.0f; ///< The surface's height in logical units.
    static constexpr f32 kScale = 2.0f;    ///< Pixels per logical unit.
    WorldScreen();
    ~WorldScreen() = default;
    WorldScreen(const WorldScreen&) = delete;
    WorldScreen& operator=(const WorldScreen&) = delete;
    ui::Surface& GetSurface() { return surface_; }
    ui::SurfaceInput& Input() { return input_; }
    /// The quad the surface is shown on.
    static ui::SurfaceQuad Quad();
    /// How many times the counter button was clicked.
    u32 Clicks() const { return clicks_; }
    ui::Button* CounterButton() const { return button_; }
    ui::Label* CounterLabel() const { return label_; }

private:
    static void Clicked(void* user, ui::Button* button);
    ui::Surface surface_;
    ui::SurfaceInput input_;
    ui::Button* button_ = nullptr;
    ui::Label* label_ = nullptr;
    u32 clicks_ = 0;
};

/// The rows of the demo's list box: "Item 1" to "Item N", reorderable.
class ItemModel final : public ui::ListModel {
public:
    explicit ItemModel(usize count);
    usize ItemCount() const override { return items_.Size(); }
    StringView ItemText(usize index) override { return items_[index].View(); }
    bool MoveItems(const usize* indices, usize count, usize to) override;

private:
    Vector<String<HeapAllocator>> items_;
};

/// What the program does with a change made in the rendering settings dialog (the renderer's `ApplySettings` through the settings system), and what each setting costs.
///
/// - Thread-safety: owner-thread-only.
class RenderSettingsHost {
public:
    virtual ~RenderSettingsHost() = default;
    /// Applies one setting (`present_mode`, `render_scale` or `shader_quality`) from its text; returns whether it was applied.
    virtual bool Change(StringView key, StringView value) = 0;
    /// Returns the name of the cost class of a setting (`dynamic`, `swapchain`, `pipeline-rebuild`, `device-reset`).
    virtual StringView CostOf(StringView key) = 0;
};

/// A fixed list of choices for a combo box.
class ChoiceModel final : public ui::ListModel {
public:
    ChoiceModel(const char* const* names, usize count) : names_(names), count_(count) {}
    usize ItemCount() const override { return count_; }
    StringView ItemText(usize index) override { return StringView(names_[index < count_ ? index : 0]); }

private:
    const char* const* names_;
    usize count_;
};

/// The names of the choices of the demo's combo box.
class ShapeModel final : public ui::ListModel {
public:
    usize ItemCount() const override { return 3; }
    StringView ItemText(usize index) override;
};

/// The demo: builds its tree under the root of a surface.
///
/// The tree: a menu bar (File, Edit, View, Help), a toolbar, a splitter with a left panel of controls and the viewport, and a status bar that stays dark whatever the theme. The viewport
/// is bound to this object, which keeps the size the viewport asks for and an orbit camera; the renderer's side reads them each frame.
///
/// - Thread-safety: owner-thread-only.
/// - Ownership: borrows the surface; owns its menus and models; the surface owns the widgets.
class UiDemoApp final : public ui::ViewportBinding {
public:
    static constexpr usize kItemCount = 1000;
    static constexpr const char kStatusReady[] = "Ready";

    /// Builds the tree under `surface`'s root.
    /// - Ownership: borrows `surface`, which must outlive this object.
    explicit UiDemoApp(ui::Surface* surface);
    ~UiDemoApp() override;
    UiDemoApp(const UiDemoApp&) = delete;
    UiDemoApp& operator=(const UiDemoApp&) = delete;

    /// Switches between the light and dark theme of the whole tree (the status bar is dark in both).
    void SetDark(bool dark);
    bool IsDark() const { return dark_; }
    /// Shows the About box.
    void ShowAbout();
    /// Opens the context menu at `position` (surface coordinates).
    void ShowContext(ui::Point position);
    /// Sets the status text.
    void SetStatus(StringView text);
    StringView Status() const;
    bool QuitRequested() const { return quit_; }
    /// Asks the program to end.
    void RequestQuit() { quit_ = true; }
    /// Returns the widget called `name`, from the root or the popup layer, or null.
    ui::Widget* Find(StringView name);
    /// Chooses an item of the menu titled `title` (as the user would by clicking it): `path` is the item's text, or `Submenu/Item` for one in a submenu (mnemonic `&`s left out); returns whether there was one.
    bool ChooseMenuItem(StringView title, StringView path);
    /// Opens the menu titled `title` (without choosing); returns whether there was one.
    bool OpenMenu(StringView title);

    ui::Viewport* GetViewport() const { return viewport_; }
    ui::MenuBar* GetMenuBar() const { return menu_bar_; }
    ui::ListBox* Items() const { return items_view_; }
    ItemModel& ItemsModel() { return items_; }
    OrbitCamera& Camera() { return camera_; }
    const OrbitCamera& Camera() const { return camera_; }
    /// The size the viewport last asked for, in physical pixels.
    u32 ViewportWidth() const { return viewport_w_; }
    u32 ViewportHeight() const { return viewport_h_; }
    /// The image the viewport shows; the renderer's side sets it once its target exists.
    void SetViewportImage(ui::ImageId image) { image_ = image; }
    u64 ViewportRenders() const { return renders_; }
    u64 ViewportResizes() const { return resizes_; }
    /// Gives the program's side of the rendering settings dialog (View > Rendering settings...); null removes it (the dialog then reports that it has no renderer).
    /// - Ownership: borrowed until it is replaced; must outlive its use here.
    void SetRenderSettingsHost(RenderSettingsHost* host) { render_host_ = host; }
    /// Opens the rendering settings dialog: present mode, render scale and shader quality, each with the cost class of changing it beside the control.
    void ShowRenderingSettings();

    /// The in-world screen.
    WorldScreen& World() { return world_; }
    /// The factor of the scene's size the orbit distance is multiplied by (set from the scene by the program; 1 by default).
    void SetSceneScale(f32 scale) { scene_scale_ = scale; }
    f32 SceneScale() const { return scene_scale_; }
    /// The status bar's own theme override.
    ui::Widget* StatusBar() const { return status_bar_; }

    // ui::ViewportBinding
    void OnResize(u32 width, u32 height) override;
    void Render(const ui::ViewportFrame& frame) override;
    ui::ImageId Image() const override { return image_; }
    void OnInput(ui::ViewportInput& input) override;

private:
    class ContextPanel;
    struct MenuCommand;
    usize MenuIndex(StringView title) const;
    void BuildMenus();
    void BuildToolbar(ui::BoxLayout* column);
    ui::Widget* BuildLeftPanel();
    void AddCommand(ui::Menu* menu, const char* text, void (UiDemoApp::*fn)());
    void NewCommand() { SetStatus(StringView("New")); }
    void OpenCommand() { SetStatus(StringView("Open")); }
    void SaveCommand() { SetStatus(StringView("Saved")); }
    void QuitCommand() { RequestQuit(); }
    void LightCommand() { SetDark(false); }
    void DarkCommand() { SetDark(true); }
    void AboutCommand() { ShowAbout(); }
    void RenderingCommand() { ShowRenderingSettings(); }
    static void RenderingChanged(void* user, ui::ComboBox* combo);
    void ToggleToolbarCommand();
    static void RunCommand(void* user, usize index);
    static void Clicked(void* user, ui::Button* button);
    static void SliderChanged(void* user, ui::Slider* slider);
    static void ComboChanged(void* user, ui::ComboBox* combo);

    ui::Surface* surface_;
    WorldScreen world_;
    f32 scene_scale_ = 1.0f;
    bool dark_ = false;
    bool quit_ = false;
    ItemModel items_;
    ShapeModel shapes_;
    RenderSettingsHost* render_host_ = nullptr;
    usize rendering_selected_[3] = {0, 2, 2}; // the rendering dialog's choices, kept for the next time it opens (present mode, render scale, shader quality)
    OrbitCamera camera_;
    UniquePtr<ui::Menu> file_menu_;
    UniquePtr<ui::Menu> edit_menu_;
    UniquePtr<ui::Menu> view_menu_;
    UniquePtr<ui::Menu> help_menu_;
    ui::Menu* theme_menu_ = nullptr;
    usize edit_first_ = 0;
    Vector<UniquePtr<MenuCommand>> commands_;
    ui::MenuBar* menu_bar_ = nullptr;
    ui::BoxLayout* toolbar_ = nullptr;
    ui::Viewport* viewport_ = nullptr;
    ui::ListBox* items_view_ = nullptr;
    ui::Label* status_ = nullptr;
    ui::Widget* status_bar_ = nullptr;
    u32 viewport_w_ = 0;
    u32 viewport_h_ = 0;
    ui::ImageId image_ = 0;
    u64 renders_ = 0;
    u64 resizes_ = 0;
    ui::ConnectionId files_connection_ = 0;
    u32 quit_accelerator_ = 0;
    u32 about_accelerator_ = 0;
};

} // namespace ui_demo
