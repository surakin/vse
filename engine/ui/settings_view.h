#pragma once

#include "sync/atomic.h"

#include "containers/unique_ptr.h"
#include "containers/vector.h"
#include "settings/registry.h"
#include "settings/settings.h"
#include "string/string.h"
#include "types.h"
#include "ui/callback.h"
#include "ui/layout.h"
#include "ui/widgets/buttons.h"
#include "ui/widgets/combobox.h"
#include "ui/widgets/label.h"
#include "ui/widgets/list_box.h"
#include "ui/widgets/text_field.h"
#include "ui/widgets/value_widgets.h"

//! A settings screen generated from the registry (Phase 15, Day 203): one labelled row per setting, the widget chosen
//! by its type, changes written through `Settings::Apply`.

namespace engine_placeholder::ui
{

/// A settings screen: groups of rows, built from the metadata of a [[settings::Registry]].
///
/// Rows are made for every setting whose key starts with the prefix, in key order, grouped by `SettingDesc::group` (a
/// setting with no group is under "General"). `kHidden` settings are skipped; `kAdvanced` ones are in rows that stay
/// collapsed until the "Show advanced settings" box is ticked. The widget follows the type: a bool is a check box; an
/// int or float with a range is a slider and a spin box that show the same value; an enum is a combo box over its
/// names; text is a text field (a password field for `kSecret`). Ranges, steps and the clamp-or-reject policy come from
/// the declaration. A `kRestartRequired` setting has a note "restart required" in its row, and `RestartPending` says
/// one was changed.
///
/// A change goes through `Settings::Apply` (with the apply function given to `SetApply`). If it is refused, the widget
/// goes back to the value in force and `Message` says why. A value that an environment variable, the command line or a
/// policy layer decides is shown disabled with its source as a tooltip. The widgets show the current values, refreshed
/// when a settings listener reports a change (from any thread) and never by polling: an idle screen reads nothing from
/// the settings.
///
/// - Thread-safety: owner-thread-only; the settings listener only sets a flag, so settings may change on any thread.
/// - Ownership: borrows the settings, which must outlive the view; owns its rows.
class SettingsView : public BoxLayout
{
public:
    /// The heading of a setting that names no group.
    static constexpr char kDefaultGroup[] = "General";
    /// The text of the box that shows the advanced rows.
    static constexpr char kAdvancedText[] = "Show advanced settings";
    /// The text of the note beside a setting that needs a restart.
    static constexpr char kRestartText[] = "restart required";
    /// The width of the label column in logical units.
    static constexpr f32 kLabelWidth = 180.0f;
    /// The gap between rows and between the label and the control.
    static constexpr f32 kGap = 6.0f;
    /// The width the sliders ask for.
    static constexpr f32 kSliderWidth = 140.0f;
    /// The decimals a float spin box shows when the declaration gives no step.
    static constexpr int kFloatDecimals = 2;

    /// Makes the screen and builds its rows.
    /// - Param settings: the settings; borrowed.
    /// - Param prefix: only settings whose key starts with it get a row (empty for all).
    /// - Param name: the widget name.
    /// - Ownership: borrows `settings`.
    SettingsView(settings::Settings* settings, StringView prefix = StringView(), StringView name = StringView());
    /// Removes the settings listener.
    ~SettingsView() override;
    SettingsView(const SettingsView&) = delete;
    SettingsView& operator=(const SettingsView&) = delete;

    /// Sets the apply function every change is passed to (the renderer's `ApplySettings` glue), or null for none.
    /// Returning false from it rejects the change.
    /// - Param user: passed to it; borrowed.
    /// - Ownership: `user` is borrowed until it is replaced.
    void SetApply(settings::ApplyFn apply, void* user);
    /// Removes the rows and makes them again from the registry; the message and the restart note are cleared.
    void Rebuild();
    /// Shows the values now in force in every row.
    void Refresh();
    /// Returns why the last change was refused, empty when it was not (cleared by the next accepted change).
    ///
    /// - Fails: never.
    StringView Message() const { return message_.View(); }
    /// Returns whether a setting that needs a restart was changed since the screen was built.
    ///
    /// - Fails: never.
    bool RestartPending() const { return restart_pending_; }
    /// Returns the number of rows (shown or collapsed).
    ///
    /// - Fails: never.
    usize RowCount() const { return rows_.Size(); }
    /// Returns the row of a setting, or null for a setting with none (hidden, or outside the prefix).
    ///
    /// - Ownership: borrowed; the view keeps it and it stays valid until `Rebuild`.
    /// - Fails: returns null when there is none.
    Widget* RowOf(settings::SettingId id) const;
    /// Returns the widget that edits a setting (the slider for a number, the check box, combo box or text field), or
    /// null.
    ///
    /// - Ownership: borrowed; the view keeps it and it stays valid until `Rebuild`.
    /// - Fails: returns null when there is none.
    Widget* ControlOf(settings::SettingId id) const;
    /// Returns the spin box of a numeric setting, or null.
    ///
    /// - Ownership: borrowed; the view keeps it and it stays valid until `Rebuild`.
    /// - Fails: returns null when there is none.
    SpinBox* SpinOf(settings::SettingId id) const;
    /// Returns the restart note of a setting, or null when it needs none.
    ///
    /// - Ownership: borrowed; the view keeps it and it stays valid until `Rebuild`.
    /// - Fails: returns null when there is none.
    Label* RestartNoteOf(settings::SettingId id) const;
    /// Returns the check box that shows the advanced rows, or null when there are none.
    ///
    /// - Ownership: borrowed; the view keeps it and it stays valid until `Rebuild`.
    /// - Fails: returns null when there is none.
    CheckBox* AdvancedBox() const { return advanced_box_; }
    /// Returns how many times the rows were refreshed from the settings.
    ///
    /// - Fails: never.
    u64 Refreshes() const { return refreshes_; }
    /// Called after a change that needs a restart is accepted.
    Callback<SettingsView*>& OnRestartNeeded() { return on_restart_; }
    /// Refreshes the rows when the listener saw a change.
    void Update(f32 dt) override;
    /// Returns true only while a change waits to be shown, so an idle screen costs nothing.
    ///
    /// - Fails: never.
    bool WantsUpdate() const override { return changed_.Load(MemoryOrder::Relaxed); }

private:
    struct Row
    {
        settings::SettingId id;
        settings::Type type = settings::Type::Bool;
        Widget* row = nullptr;
        CheckBox* check = nullptr;
        Slider* slider = nullptr;
        SpinBox* spin = nullptr;
        ComboBox* combo = nullptr;
        TextField* field = nullptr;
        Label* note = nullptr;
        bool advanced = false;
    };
    class EnumModel;
    struct Link
    {
        SettingsView* view = nullptr;
        usize row = 0;
    };

    void AddRow(const settings::SettingDesc& desc, settings::SettingId id, BoxLayout* group);
    void Commit(usize row, const settings::Value& value);
    void ShowRow(usize row, const settings::SettingsSnapshot& snapshot);
    static void TextLeft(void* user, TextField* field);
    Link* MakeLink(usize row);
    static void Heard(void* user, const settings::ChangeEvent& event);
    static void BoolChanged(void* user, CheckBox* box);
    static void SliderMoved(void* user, Slider* slider);
    static void SliderCommitted(void* user, Slider* slider);
    static void SpinChanged(void* user, SpinBox* spin);
    static void ComboChanged(void* user, ComboBox* combo);
    static void TextSubmitted(void* user, TextField* field);
    static void AdvancedToggled(void* user, CheckBox* box);
    void ShowAdvanced(bool show);
    void ShowMessage();

    settings::Settings* settings_;
    String<HeapAllocator> prefix_;
    settings::ApplyFn apply_ = nullptr;
    void* apply_user_ = nullptr;
    u32 listener_ = 0;
    Atomic<bool> changed_{false};
    bool syncing_ = false;
    bool restart_pending_ = false;
    u64 refreshes_ = 0;
    String<HeapAllocator> message_;
    Label* message_label_ = nullptr;
    CheckBox* advanced_box_ = nullptr;
    Vector<Row> rows_;
    Vector<UniquePtr<Link>> links_;
    Vector<UniquePtr<EnumModel>> models_;
    Vector<UniquePtr<Link>>
        retired_links_;  // of controls a Rebuild during a dispatch left alive until the dispatch ended
    Vector<UniquePtr<EnumModel>> retired_models_;
    Callback<SettingsView*> on_restart_;
};

}  // namespace engine_placeholder::ui
