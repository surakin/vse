#include "ui/settings_view.h"

#include <cstdio>
#include <cstring>
#include "sync/atomic.h"

namespace engine_placeholder::ui {

namespace {

bool StartsWith(StringView text, StringView prefix) {
    if (prefix.Length() > text.Length()) return false;
    for (usize i = 0; i < prefix.Length(); ++i) {
        if (text[i] != prefix[i]) return false;
    }
    return true;
}

StringView View(const char* text) { return text != nullptr ? StringView(text, std::strlen(text)) : StringView(); }

String<HeapAllocator> Owned(StringView text) { return String<HeapAllocator>(HeapAllocator::Default(), text); }

const char* SetErrorText(settings::SetError error) {
    switch (error) {
        case settings::SetError::UnknownKey: return "no such setting";
        case settings::SetError::BadValue: return "the value is not accepted";
        case settings::SetError::Unsupported: return "this device does not support it";
        case settings::SetError::Store: return "it could not be saved";
        case settings::SetError::ApplyFailed: return "the change was rejected and undone";
        case settings::SetError::Reentrant: return "too many nested changes";
        case settings::SetError::Enforced: return "an administrator decides this value";
        case settings::SetError::None: break;
    }
    return "";
}

// Whether a layer role fixes a value so that the screen cannot change it (the user's file is written below these).
bool Pins(settings::LayerRole role) {
    return role == settings::LayerRole::Environment || role == settings::LayerRole::CommandLine || role == settings::LayerRole::Policy;
}

} // namespace

// The names of an enum setting as a list model; the names are the registry's.
class SettingsView::EnumModel final : public ListModel {
public:
    explicit EnumModel(const settings::SettingDesc& desc) : desc_(&desc) {}
    usize ItemCount() const override { return desc_->enum_count; }
    StringView ItemText(usize index) override { return index < desc_->enum_count ? View(desc_->enum_names[index]) : StringView(); }

private:
    const settings::SettingDesc* desc_;
};

SettingsView::SettingsView(settings::Settings* settings, StringView prefix, StringView name)
    : BoxLayout(Orientation::Vertical, name), settings_(settings), prefix_(Owned(prefix)), message_(Owned(StringView())) {
    SetSpacing(kGap);
    // The listener runs on whichever thread changed a setting: it only raises a flag, and the next `Update` on the UI thread reads the values.
    // A listener's prefix is a key or a group of keys without the closing dot.
    StringView listened = prefix;
    if (listened.Length() > 0 && listened[listened.Length() - 1] == '.') listened = listened.Substring(0, listened.Length() - 1);
    listener_ = settings_->AddListener(listened, &SettingsView::Heard, this);
    Rebuild();
}

SettingsView::~SettingsView() {
    if (listener_ != 0) settings_->RemoveListener(listener_);
}

void SettingsView::Heard(void* user, const settings::ChangeEvent&) { static_cast<SettingsView*>(user)->changed_.Store(true, MemoryOrder::Relaxed); }

void SettingsView::SetApply(settings::ApplyFn apply, void* user) {
    apply_ = apply;
    apply_user_ = user;
}

SettingsView::Link* SettingsView::MakeLink(usize row) {
    UniquePtr<Link> link = MakeUnique<Link>();
    link->view = this;
    link->row = row;
    Link* raw = link.Get();
    links_.Push(static_cast<UniquePtr<Link>&&>(link));
    return raw;
}

void SettingsView::Rebuild() {
    // While the surface dispatches (Rebuild from a button's handler), RemoveChild only queues the removal: the old controls live until the dispatch ends and may still be clicked
    // before then. Their links are cut (a callback finds a null view and does nothing) and kept, with the models the combo boxes read, until the next Rebuild.
    Vector<Widget*> old_children;
    for (usize i = 0; i < ChildCount(); ++i) old_children.Push(ChildAt(i));
    for (usize i = old_children.Size(); i > 0; --i) RemoveChild(old_children[i - 1]);
    rows_.Clear();
    for (usize i = 0; i < links_.Size(); ++i) {
        links_[i]->view = nullptr;
        retired_links_.Push(static_cast<UniquePtr<Link>&&>(links_[i]));
    }
    links_.Clear();
    for (usize i = 0; i < models_.Size(); ++i) retired_models_.Push(static_cast<UniquePtr<EnumModel>&&>(models_[i]));
    models_.Clear();
    advanced_box_ = nullptr;
    message_label_ = nullptr;
    restart_pending_ = false;
    message_ = Owned(StringView());
    const settings::Registry& registry = settings_->GetRegistry();
    // The settings that get a row, grouped in the order the groups first appear.
    Vector<settings::SettingId> ids;
    for (u32 i = 0; i < registry.Count(); ++i) {
        const settings::SettingId id = registry.SortedAt(i);
        const settings::SettingDesc* desc = registry.Desc(id);
        if (desc == nullptr || (desc->flags & settings::kHidden) != 0) continue;
        if (!StartsWith(View(desc->key), prefix_.View())) continue;
        ids.Push(id);
    }
    Vector<StringView> groups;
    for (usize i = 0; i < ids.Size(); ++i) {
        const settings::SettingDesc* desc = registry.Desc(ids[i]);
        const StringView group = desc->group != nullptr ? View(desc->group) : StringView(kDefaultGroup, sizeof(kDefaultGroup) - 1);
        bool known = false;
        for (usize g = 0; g < groups.Size(); ++g) known = known || groups[g] == group;
        if (!known) groups.Push(group);
    }
    bool any_advanced = false;
    for (usize g = 0; g < groups.Size(); ++g) {
        UniquePtr<Label> heading = MakeUnique<Label>(StringView("group"), groups[g]);
        AddChild(UniquePtr<Widget>(heading.Release()));
        UniquePtr<BoxLayout> box = MakeUnique<BoxLayout>(Orientation::Vertical, StringView("rows"));
        box->SetSpacing(kGap);
        BoxLayout* raw = box.Get();
        AddChild(UniquePtr<Widget>(box.Release()));
        for (usize i = 0; i < ids.Size(); ++i) {
            const settings::SettingDesc* desc = registry.Desc(ids[i]);
            const StringView group = desc->group != nullptr ? View(desc->group) : StringView(kDefaultGroup, sizeof(kDefaultGroup) - 1);
            if (group != groups[g]) continue;
            any_advanced = any_advanced || (desc->flags & settings::kAdvanced) != 0;
            AddRow(*desc, ids[i], raw);
        }
    }
    if (any_advanced) {
        UniquePtr<CheckBox> box = MakeUnique<CheckBox>(StringView("advanced"), StringView(kAdvancedText, sizeof(kAdvancedText) - 1));
        advanced_box_ = box.Get();
        advanced_box_->OnChanged().Connect(&SettingsView::AdvancedToggled, this);
        AddChild(UniquePtr<Widget>(box.Release()));
    }
    UniquePtr<Label> message = MakeUnique<Label>(StringView("message"));
    message_label_ = message.Get();
    message_label_->SetCollapsed(true);
    AddChild(UniquePtr<Widget>(message.Release()));
    ShowAdvanced(false);
    Refresh();
}

void SettingsView::AddRow(const settings::SettingDesc& desc, settings::SettingId id, BoxLayout* group) {
    const usize index = rows_.Size();
    Row row;
    row.id = id;
    row.type = desc.type;
    row.advanced = (desc.flags & settings::kAdvanced) != 0;
    UniquePtr<BoxLayout> line = MakeUnique<BoxLayout>(Orientation::Horizontal, StringView("row"));
    line->SetSpacing(kGap);
    row.row = line.Get();
    UniquePtr<Label> label = MakeUnique<Label>(StringView("label"), desc.label != nullptr ? View(desc.label) : View(desc.key));
    LayoutProps label_props;
    label_props.min.w = kLabelWidth;
    label_props.max.w = kLabelWidth;
    label->SetProps(label_props);
    if (desc.description != nullptr) label->SetTooltip(View(desc.description));
    line->AddChild(UniquePtr<Widget>(label.Release()));
    Link* link = MakeLink(index);
    LayoutProps grow;
    grow.weight = 1;
    switch (desc.type) {
        case settings::Type::Bool: {
            UniquePtr<CheckBox> box = MakeUnique<CheckBox>(StringView("control"));
            row.check = box.Get();
            box->OnChanged().Connect(&SettingsView::BoolChanged, link);
            line->AddChild(UniquePtr<Widget>(box.Release()));
            break;
        }
        case settings::Type::Int:
        case settings::Type::Float: {
            ValueRange range;
            const bool is_int = desc.type == settings::Type::Int;
            range.min = is_int ? static_cast<f64>(desc.min_int) : desc.min_float;
            range.max = is_int ? static_cast<f64>(desc.max_int) : desc.max_float;
            range.integer = is_int;
            range.policy = desc.policy == settings::RangePolicy::Reject ? RangePolicy::Reject : RangePolicy::Clamp;
            UniquePtr<Slider> slider = MakeUnique<Slider>(StringView("control"));
            slider->SetRange(range);
            row.slider = slider.Get();
            slider->OnChanged().Connect(&SettingsView::SliderMoved, link);
            slider->OnCommit().Connect(&SettingsView::SliderCommitted, link);
            LayoutProps slider_props = grow;
            slider_props.min.w = kSliderWidth;
            slider->SetProps(slider_props);
            line->AddChild(UniquePtr<Widget>(slider.Release()));
            UniquePtr<SpinBox> spin = MakeUnique<SpinBox>(StringView("spin"));
            spin->SetRange(range);
            if (!is_int) spin->SetDecimals(kFloatDecimals);
            row.spin = spin.Get();
            spin->OnChanged().Connect(&SettingsView::SpinChanged, link);
            line->AddChild(UniquePtr<Widget>(spin.Release()));
            break;
        }
        case settings::Type::Enum: {
            UniquePtr<EnumModel> model = MakeUnique<EnumModel>(desc);
            UniquePtr<ComboBox> combo = MakeUnique<ComboBox>(StringView("control"));
            combo->SetModel(model.Get());
            models_.Push(static_cast<UniquePtr<EnumModel>&&>(model));
            row.combo = combo.Get();
            combo->OnSelectionChanged().Connect(&SettingsView::ComboChanged, link);
            combo->SetProps(grow);
            line->AddChild(UniquePtr<Widget>(combo.Release()));
            break;
        }
        case settings::Type::Text: {
            UniquePtr<TextField> field = MakeUnique<TextField>(StringView("control"));
            if ((desc.flags & settings::kSecret) != 0) field->SetPassword(true);
            if (desc.max_text > 0) field->SetMaxLength(desc.max_text);
            row.field = field.Get();
            field->OnSubmit().Connect(&SettingsView::TextSubmitted, link);
            field->OnFocusLost().Connect(&SettingsView::TextLeft, link);
            field->SetProps(grow);
            line->AddChild(UniquePtr<Widget>(field.Release()));
            break;
        }
    }
    if ((desc.flags & settings::kRestartRequired) != 0) {
        UniquePtr<Label> note = MakeUnique<Label>(StringView("restart"), StringView(kRestartText, sizeof(kRestartText) - 1));
        row.note = note.Get();
        line->AddChild(UniquePtr<Widget>(note.Release()));
    }
    group->AddChild(UniquePtr<Widget>(line.Release()));
    rows_.Push(row);
}

void SettingsView::ShowAdvanced(bool show) {
    for (usize i = 0; i < rows_.Size(); ++i) {
        if (rows_[i].advanced) rows_[i].row->SetCollapsed(!show);
    }
}

void SettingsView::AdvancedToggled(void* user, CheckBox* box) {
    static_cast<SettingsView*>(user)->ShowAdvanced(box->GetCheckState() == CheckState::Checked);
}

void SettingsView::ShowMessage() {
    if (message_label_ == nullptr) return;
    message_label_->SetText(message_.View());
    message_label_->SetCollapsed(message_.Length() == 0);
}

void SettingsView::Update(f32) {
    if (changed_.Exchange(false, MemoryOrder::Relaxed)) Refresh();
}

void SettingsView::Refresh() {
    ++refreshes_;
    changed_.Store(false, MemoryOrder::Relaxed);
    const SharedPtr<const settings::SettingsSnapshot> snapshot = settings_->Snapshot();
    for (usize i = 0; i < rows_.Size(); ++i) ShowRow(i, *snapshot);
}

void SettingsView::ShowRow(usize index, const settings::SettingsSnapshot& snapshot) {
    Row& row = rows_[index];
    const settings::Value& value = snapshot.Get(row.id);
    syncing_ = true;
    switch (row.type) {
        case settings::Type::Bool:
            row.check->SetCheckState(value.AsBool() ? CheckState::Checked : CheckState::Unchecked);
            break;
        case settings::Type::Int:
            row.slider->SetValue(static_cast<f64>(value.AsInt()));
            row.spin->SetValue(static_cast<f64>(value.AsInt()));
            break;
        case settings::Type::Float:
            row.slider->SetValue(value.AsFloat());
            row.spin->SetValue(value.AsFloat());
            break;
        case settings::Type::Enum:
            row.combo->Select(value.AsEnum());
            break;
        case settings::Type::Text:
            // A field being edited keeps its draft: it is committed with Enter or when the focus leaves, and shows the value in force after that.
            if ((row.field->State() & kStateFocused) == 0 && row.field->Text() != value.AsText()) row.field->SetText(value.AsText());
            break;
    }
    syncing_ = false;
    // A value that the environment, the command line or a policy decides cannot be changed here; its source is the tooltip.
    const settings::Resolution where = settings_->Provenance(row.id);
    const bool pinned = where.layer >= 0 && Pins(settings_->Layers().Role(static_cast<usize>(where.layer)));
    Widget* control = row.check != nullptr ? static_cast<Widget*>(row.check)
                      : row.slider != nullptr ? static_cast<Widget*>(row.slider)
                      : row.combo != nullptr ? static_cast<Widget*>(row.combo)
                                             : static_cast<Widget*>(row.field);
    control->SetEnabled(!pinned);
    if (row.spin != nullptr) row.spin->SetEnabled(!pinned);
    if (pinned) {
        char tip[320];
        std::snprintf(tip, sizeof(tip), "Set by %s %s", where.layer_name, where.origin);
        control->SetTooltip(View(tip));
    } else {
        control->SetTooltip(StringView());
    }
}

void SettingsView::Commit(usize index, const settings::Value& value) {
    if (syncing_) return;
    const settings::SettingDesc* desc = settings_->GetRegistry().Desc(rows_[index].id);
    settings::ChangeRequest request;
    request.id = rows_[index].id;
    request.value = value;
    const settings::SetResult result = settings_->Apply(&request, 1, apply_, apply_user_);
    if (!result.Ok()) {
        char line[320];
        const char* why = result.error == settings::SetError::BadValue ? settings::ValueErrorName(result.value) : SetErrorText(result.error);
        std::snprintf(line, sizeof(line), "%s: %s", desc->label != nullptr ? desc->label : desc->key, why);
        message_ = Owned(View(line));
    } else {
        message_ = Owned(StringView());
        if ((desc->flags & settings::kRestartRequired) != 0 && result.changed > 0 && !restart_pending_) {
            restart_pending_ = true;
            on_restart_.Invoke(this);
        }
    }
    ShowMessage();
    ShowRow(index, *settings_->Snapshot()); // the widget shows what is in force: the new value, the clamped one, or the old one after a refusal
}

void SettingsView::BoolChanged(void* user, CheckBox* box) {
    Link* link = static_cast<Link*>(user);
    if (link->view == nullptr) return; // the control outlived a Rebuild
    link->view->Commit(link->row, settings::Value::FromBool(box->GetCheckState() == CheckState::Checked));
}

void SettingsView::SliderMoved(void* user, Slider* slider) {
    Link* link = static_cast<Link*>(user);
    if (link->view == nullptr) return; // the control outlived a Rebuild
    if (link->view->syncing_) return;
    link->view->syncing_ = true;
    link->view->rows_[link->row].spin->SetValue(slider->Value()); // follows the thumb; nothing is applied until it is released
    link->view->syncing_ = false;
}

namespace {

settings::Value NumberValue(settings::Type type, f64 number) {
    return type == settings::Type::Int ? settings::Value::FromInt(static_cast<i64>(number < 0 ? number - 0.5 : number + 0.5)) : settings::Value::FromFloat(number);
}

} // namespace

void SettingsView::SliderCommitted(void* user, Slider* slider) {
    Link* link = static_cast<Link*>(user);
    if (link->view == nullptr) return; // the control outlived a Rebuild
    link->view->Commit(link->row, NumberValue(link->view->rows_[link->row].type, slider->Value()));
}

void SettingsView::SpinChanged(void* user, SpinBox* spin) {
    Link* link = static_cast<Link*>(user);
    if (link->view == nullptr) return; // the control outlived a Rebuild
    if (link->view->syncing_) return;
    link->view->Commit(link->row, NumberValue(link->view->rows_[link->row].type, spin->Value()));
}

void SettingsView::ComboChanged(void* user, ComboBox* combo) {
    Link* link = static_cast<Link*>(user);
    if (link->view == nullptr) return; // the control outlived a Rebuild
    if (combo->Selected() == ComboBox::kNone) return;
    link->view->Commit(link->row, settings::Value::FromEnum(static_cast<u32>(combo->Selected())));
}

void SettingsView::TextSubmitted(void* user, TextField* field) {
    Link* link = static_cast<Link*>(user);
    if (link->view == nullptr) return; // the control outlived a Rebuild
    link->view->Commit(link->row, settings::Value::FromText(field->Text()));
}

void SettingsView::TextLeft(void* user, TextField* field) {
    Link* link = static_cast<Link*>(user);
    if (link->view == nullptr) return; // the control outlived a Rebuild
    if (field->Text() != link->view->settings_->Snapshot()->Get(link->view->rows_[link->row].id).AsText()) TextSubmitted(user, field);
}

Widget* SettingsView::RowOf(settings::SettingId id) const {
    for (usize i = 0; i < rows_.Size(); ++i) {
        if (rows_[i].id == id) return rows_[i].row;
    }
    return nullptr;
}

Widget* SettingsView::ControlOf(settings::SettingId id) const {
    for (usize i = 0; i < rows_.Size(); ++i) {
        if (!(rows_[i].id == id)) continue;
        if (rows_[i].check != nullptr) return rows_[i].check;
        if (rows_[i].slider != nullptr) return rows_[i].slider;
        if (rows_[i].combo != nullptr) return rows_[i].combo;
        return rows_[i].field;
    }
    return nullptr;
}

SpinBox* SettingsView::SpinOf(settings::SettingId id) const {
    for (usize i = 0; i < rows_.Size(); ++i) {
        if (rows_[i].id == id) return rows_[i].spin;
    }
    return nullptr;
}

Label* SettingsView::RestartNoteOf(settings::SettingId id) const {
    for (usize i = 0; i < rows_.Size(); ++i) {
        if (rows_[i].id == id) return rows_[i].note;
    }
    return nullptr;
}

} // namespace engine_placeholder::ui
