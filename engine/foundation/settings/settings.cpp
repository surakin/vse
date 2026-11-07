#include "settings/settings.h"

#include <chrono>
#include <cstdio>
#include <cstring>

#include "debug/crash_state.h"
#include "io/file_system.h"
#include "log/assert.h"
#include "settings/file_store.h"

namespace engine_placeholder::settings {

namespace {

thread_local int g_depth = 0; // how many listener or apply calls this thread is inside

double NowSeconds() { return std::chrono::duration<double>(std::chrono::steady_clock::now().time_since_epoch()).count(); }

bool Under(StringView key, StringView prefix) {
    return prefix.Length() == 0 || key == prefix || (key.Length() > prefix.Length() && key.StartsWith(prefix) && key[prefix.Length()] == '.');
}

void CopyText(char* out, usize capacity, StringView text) {
    if (capacity == 0) return;
    const usize n = text.Length() < capacity - 1 ? text.Length() : capacity - 1;
    for (usize i = 0; i < n; ++i) out[i] = text[i];
    out[n] = '\0';
}

struct Call {
    SettingsListener fn;
    void* user;
    Vector<Change> matching;
};

} // namespace

const Value& SettingsSnapshot::Get(SettingId id) const {
    ENGINE_ASSERT(id.value >= 1 && id.value <= values.Size(), "settings::SettingsSnapshot::Get: unknown id");
    return values[id.value - 1u];
}

struct Settings::Subscription {
    u32 id = 0;
    String<HeapAllocator> prefix = String<HeapAllocator>(HeapAllocator::Default());
    SettingsListener listener = nullptr;
    void* user = nullptr;
};

Settings::Settings(Registry& registry) : registry_(&registry) {
    registry.Freeze();
    std::lock_guard<std::recursive_mutex> op(op_);
    RebuildSnapshotLocked();
}

Settings::~Settings() = default;

void Settings::RebuildSnapshotLocked() {
    auto snap = MakeShared<SettingsSnapshot>();
    for (u32 i = 0; i < registry_->Count(); ++i) snap->values.Push(registry_->Current(SettingId{static_cast<u16>(i + 1)}));
    std::lock_guard<std::mutex> lock(mutex_);
    snap->generation = ++generation_;
    snapshot_ = SharedPtr<const SettingsSnapshot>(static_cast<SharedPtr<SettingsSnapshot>&&>(snap));
}

LoadReport Settings::Load(const LoadOptions& options) {
    CommandLineResult parsed_command_line;
    {
        std::lock_guard<std::recursive_mutex> op(op_);
        stack_.Clear();
        minimal_ = options.minimal_writes;
        command_line_ = CommandLineResult();
        if (options.shipped != nullptr && options.shipped_count != 0) {
            stack_.AddLayer("shipped defaults", LayerRole::System, MakeUnique<ReadOnlyConfigStore>(options.shipped, options.shipped_count));
        }
        FileStoreOptions read_only;
        read_only.read_only = true;
        if (options.system_file != nullptr) stack_.AddLayer("system file", LayerRole::System, MakeUnique<FileConfigStore>(*options.system_file, read_only));
        io::Path user_path;
        if (options.user_file != nullptr) {
            user_path = *options.user_file;
        } else {
            io::Path dir;
            if (io::UserConfigDirectory(StringView(options.app_name), &dir).Ok()) user_path = dir.Join("settings.cfg");
        }
        if (!user_path.IsEmpty()) {
            FileStoreOptions user;
            user.permissions = 0600;
            user.create_directories = true;
            stack_.AddLayer("user file", LayerRole::User, MakeUnique<FileConfigStore>(user_path, user));
        }
        if (options.project_file != nullptr) stack_.AddLayer("project file", LayerRole::Project, MakeUnique<FileConfigStore>(*options.project_file, read_only));
        UniquePtr<CommandLineSource> cli;
        if (options.argv != nullptr && options.argc > 0) {
            cli = MakeUnique<CommandLineSource>(*registry_, options.argc, options.argv);
            const CommandLineResult& r = cli->Result();
            command_line_.list_settings = r.list_settings;
            command_line_.help = r.help;
            command_line_.save_settings = r.save_settings;
            command_line_.save_path = String<HeapAllocator>(HeapAllocator::Default(), r.save_path.View());
            for (usize i = 0; i < r.rest.Size(); ++i) command_line_.rest.Push(String<HeapAllocator>(HeapAllocator::Default(), r.rest[i].View()));
            for (usize i = 0; i < r.errors.Size(); ++i) command_line_.errors.Push(String<HeapAllocator>(HeapAllocator::Default(), r.errors[i].View()));
            for (usize i = 0; i < r.settings_files.Size(); ++i) {
                command_line_.settings_files.Push(String<HeapAllocator>(HeapAllocator::Default(), r.settings_files[i].View()));
                io::Path file;
                if (io::Path::Parse(r.settings_files[i].View(), io::NativePathStyle(), &file)) {
                    stack_.AddLayer(r.settings_files[i].View(), LayerRole::Custom, MakeUnique<FileConfigStore>(file, read_only));
                }
            }
        }
        if (options.use_environment) {
            const EnvironmentSnapshot captured = options.environment == nullptr ? EnvironmentSnapshot::Capture() : EnvironmentSnapshot();
            stack_.AddLayer("environment", LayerRole::Environment, MakeUnique<EnvironmentSource>(*registry_, options.environment != nullptr ? *options.environment : captured));
        }
        if (cli) stack_.AddLayer("command line", LayerRole::CommandLine, ConfigStorePtr(static_cast<UniquePtr<CommandLineSource>&&>(cli)));
    }
    LoadReport report = LoadFromLayers();
    for (usize i = 0; i < command_line_.errors.Size(); ++i) {
        LayerProblem p;
        CopyText(p.layer, sizeof(p.layer), "command line");
        CopyText(p.message, sizeof(p.message), command_line_.errors[i].View());
        report.problems.Push(p);
    }
    return report;
}

LoadReport Settings::LoadFromLayers() {
    LoadReport report;
    std::lock_guard<std::recursive_mutex> op(op_);
    stack_.LoadAll(&report.problems);
    for (usize i = 0; i < stack_.Size(); ++i) {
        LoadReport::LayerInfo info;
        CopyText(info.name, sizeof(info.name), stack_.Name(i));
        info.state = stack_.State(i);
        report.layers.Push(info);
    }
    for (u32 i = 0; i < registry_->Count(); ++i) {
        const SettingId id{static_cast<u16>(i + 1)};
        Resolution r;
        stack_.Resolve(*registry_, id, &r, &report.problems);
        registry_->Publish(id, r.value);
    }
    RebuildSnapshotLocked();
    {
        std::lock_guard<std::mutex> lock(mutex_);
        dirty_ = false;
    }
    UpdateCrashState();
    return report;
}

SharedPtr<const SettingsSnapshot> Settings::Snapshot() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return snapshot_;
}

Resolution Settings::Provenance(SettingId id) {
    std::lock_guard<std::recursive_mutex> op(op_);
    Resolution r;
    stack_.Resolve(*registry_, id, &r, nullptr);
    const Value current = registry_->Current(id);
    if (!(r.value == current)) { // a runtime change that a higher layer's value would hide
        r.value = current;
        r.layer = -1;
        CopyText(r.layer_name, sizeof(r.layer_name), "runtime");
        CopyText(r.origin, sizeof(r.origin), "Settings::Set");
    }
    return r;
}

SetResult Settings::Set(SettingId id, const Value& value) {
    ChangeRequest request;
    request.id = id;
    request.value = value;
    return DoApply(&request, 1, nullptr, nullptr);
}

SetResult Settings::SetText(StringView key, StringView text) {
    const SettingId id = registry_->Find(key);
    SetResult result;
    if (!id.IsValid()) {
        result.error = SetError::UnknownKey;
        return result;
    }
    Value value;
    const ValueError parsed = ParseValue(*registry_->Desc(id), text, &value);
    if (parsed != ValueError::None && parsed != ValueError::Clamped) {
        result.error = SetError::BadValue;
        result.value = parsed;
        return result;
    }
    return Set(id, value);
}

SetResult Settings::Apply(const ChangeRequest* requests, usize count, ApplyFn apply, void* user) { return DoApply(requests, count, apply, user); }

SetResult Settings::DoApply(const ChangeRequest* requests, usize count, ApplyFn apply, void* user) {
    SetResult result;
    if (g_depth >= 8) {
        result.error = SetError::Reentrant;
        return result;
    }
    // The whole operation, apply function and listeners included, is one critical section on a recursive lock: changes are announced in
    // the order they were made, and a rejected batch is undone before anyone else can change anything.
    std::lock_guard<std::recursive_mutex> op(op_);
    struct Pending {
        Change change;
        bool had_write = false;
        StoredValue write_before; // exactly what the write layer held, restored as it was
    };
    Vector<Pending> pending;
    for (usize i = 0; i < count; ++i) {
        const SettingDesc* desc = registry_->Desc(requests[i].id);
        if (desc == nullptr) {
            result.error = SetError::UnknownKey;
            return result;
        }
        Value checked;
        const ValueError v = ValidateValue(*desc, requests[i].value, &checked);
        if (v != ValueError::None && v != ValueError::Clamped) {
            result.error = SetError::BadValue;
            result.value = v;
            return result;
        }
        if (desc->capability != nullptr && capability_ != nullptr && !(checked == desc->default_value) && !capability_(capability_user_, desc->capability)) {
            result.error = SetError::Unsupported;
            return result;
        }
        bool merged = false;
        for (usize k = 0; k < pending.Size(); ++k) { // the same setting twice in a batch: the last one wins
            if (pending[k].change.id == requests[i].id) {
                pending[k].change.new_value = checked;
                merged = true;
            }
        }
        if (merged) continue;
        Pending p;
        p.change.id = requests[i].id;
        p.change.old_value = registry_->Current(requests[i].id);
        p.change.new_value = checked;
        pending.Push(static_cast<Pending&&>(p));
    }
    for (usize k = pending.Size(); k-- > 0;) { // drop what does not change
        if (pending[k].change.old_value == pending[k].change.new_value) pending.Remove(k);
    }
    if (pending.Empty()) return result;

    // Write the write layer, remembering what was there. With no write layer the change is in memory only.
    const usize w = stack_.WriteLayer();
    auto restore = [&](usize count_done) {
        for (usize back = count_done; back-- > 0;) {
            const StringView key = registry_->KeyOf(pending[back].change.id).View();
            if (pending[back].had_write) stack_.Store(w).Set(key, pending[back].write_before);
            else stack_.Unset(*registry_, pending[back].change.id);
        }
    };
    if (w < stack_.Size()) {
        for (usize k = 0; k < pending.Size(); ++k) {
            Pending& p = pending[k];
            p.had_write = stack_.Store(w).Get(registry_->KeyOf(p.change.id).View(), &p.write_before).Ok();
            const Status written = stack_.Set(*registry_, p.change.id, p.change.new_value, minimal_);
            if (written.Failed()) {
                restore(k);
                result.error = SetError::Store;
                result.store = written.error;
                return result;
            }
        }
    }

    Vector<Change> changes;
    u8 highest = 0;
    bool restart = false;
    for (usize k = 0; k < pending.Size(); ++k) {
        changes.Push(pending[k].change);
        const SettingDesc& d = *registry_->Desc(pending[k].change.id);
        if (d.apply_class > highest) highest = d.apply_class;
        if (d.flags & kRestartRequired) restart = true;
    }
    ChangeEvent event;
    event.registry = registry_;
    event.changes = changes.Data();
    event.count = changes.Size();
    event.highest_apply_class = highest;
    event.restart_required = restart;

    ++g_depth;
    const bool applied = apply == nullptr || apply(user, event);
    if (!applied) {
        --g_depth;
        if (w < stack_.Size()) restore(pending.Size());
        result.error = SetError::ApplyFailed;
        return result;
    }
    // Accepted: publish, then announce.
    for (usize k = 0; k < pending.Size(); ++k) registry_->Publish(pending[k].change.id, pending[k].change.new_value);
    RebuildSnapshotLocked();
    {
        std::lock_guard<std::mutex> lock(mutex_);
        dirty_ = true;
        last_change_ = NowSeconds();
    }
    UpdateCrashState();
    result.changed = static_cast<u32>(pending.Size());

    Vector<Call> calls;
    for (usize s = 0; s < subscriptions_.Size(); ++s) {
        Call call;
        call.fn = subscriptions_[s]->listener;
        call.user = subscriptions_[s]->user;
        for (usize k = 0; k < changes.Size(); ++k) {
            if (Under(registry_->KeyOf(changes[k].id).View(), subscriptions_[s]->prefix.View())) call.matching.Push(changes[k]);
        }
        if (!call.matching.Empty()) calls.Push(static_cast<Call&&>(call));
    }
    for (usize c = 0; c < calls.Size(); ++c) {
        ChangeEvent e = event;
        e.changes = calls[c].matching.Data();
        e.count = calls[c].matching.Size();
        e.highest_apply_class = 0;
        e.restart_required = false;
        for (usize k = 0; k < calls[c].matching.Size(); ++k) {
            const SettingDesc& d = *registry_->Desc(calls[c].matching[k].id);
            if (d.apply_class > e.highest_apply_class) e.highest_apply_class = d.apply_class;
            if (d.flags & kRestartRequired) e.restart_required = true;
        }
        calls[c].fn(calls[c].user, e);
    }
    --g_depth;
    return result;
}

u32 Settings::AddListener(StringView prefix, SettingsListener listener, void* user) {
    std::lock_guard<std::recursive_mutex> op(op_);
    UniquePtr<Subscription> s = MakeUnique<Subscription>();
    s->id = next_listener_++;
    s->prefix = String<HeapAllocator>(HeapAllocator::Default(), prefix);
    s->listener = listener;
    s->user = user;
    const u32 id = s->id;
    subscriptions_.Push(static_cast<UniquePtr<Subscription>&&>(s));
    return id;
}

void Settings::RemoveListener(u32 id) {
    std::lock_guard<std::recursive_mutex> op(op_);
    for (usize i = 0; i < subscriptions_.Size(); ++i) {
        if (subscriptions_[i]->id == id) {
            subscriptions_.Remove(i);
            return;
        }
    }
}

void Settings::SetCapabilityCheck(CapabilityFn check, void* user) {
    std::lock_guard<std::recursive_mutex> op(op_);
    capability_ = check;
    capability_user_ = user;
}

Status Settings::Save(const SaveOptions& options) {
    std::lock_guard<std::recursive_mutex> op(op_);
    const usize w = stack_.WriteLayer();
    if (w >= stack_.Size()) return Status::Of(StoreError::NotFound);
    struct Undo {
        SettingId id;
        bool had = false;
        StoredValue before;
    };
    Vector<Undo> undo;
    if (options.include_command_line) {
        const usize cli = stack_.Find(LayerRole::CommandLine);
        if (cli < stack_.Size()) {
            Vector<Key> keys;
            stack_.Store(cli).Enumerate(StringView(), &keys);
            for (usize i = 0; i < keys.Size(); ++i) {
                const SettingId id = registry_->Find(keys[i].View());
                if (!id.IsValid()) continue;
                Undo u;
                u.id = id;
                u.had = stack_.Store(w).Get(keys[i].View(), &u.before).Ok();
                undo.Push(static_cast<Undo&&>(u));
                stack_.Set(*registry_, id, registry_->Current(id), false);
            }
        }
    }
    CommitOptions commit;
    commit.replace_damaged = options.replace_damaged;
    const Status s = stack_.Store(w).Commit(commit);
    if (s.Ok()) {
        std::lock_guard<std::mutex> lock(mutex_);
        dirty_ = false;
    } else {
        for (usize i = undo.Size(); i-- > 0;) { // a failed save leaves no command-line values behind in the write layer
            if (undo[i].had) stack_.Store(w).Set(registry_->KeyOf(undo[i].id).View(), undo[i].before);
            else stack_.Unset(*registry_, undo[i].id);
        }
    }
    return s;
}

Status Settings::SaveIfChanged() {
    if (!HasUnsavedChanges()) return Status();
    return Save();
}

bool Settings::HasUnsavedChanges() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return dirty_;
}

void Settings::SetAutosaveQuietSeconds(double seconds) {
    std::lock_guard<std::mutex> lock(mutex_);
    autosave_quiet_ = seconds;
}

bool Settings::PollAutosave() {
    {
        std::lock_guard<std::mutex> lock(mutex_);
        if (autosave_quiet_ <= 0 || !dirty_ || NowSeconds() - last_change_ < autosave_quiet_) return false;
    }
    const bool saved = Save().Ok();
    if (!saved) { // try again after another quiet period, not on every call
        std::lock_guard<std::mutex> lock(mutex_);
        last_change_ = NowSeconds();
    }
    return saved;
}

void Settings::UpdateCrashState() {
    // "settings" holds the count; "settings 1" ... "settings 5" hold the non-default settings as "key=value" (a secret shows no value), packed
    // into the crash report's 127-byte entries. Only entries that were used are cleared again.
    constexpr int kEntries = 5;
    char entries[kEntries][debug::kCrashStateEntryTextBytes] = {};
    int e = 0;
    usize used = 0;
    u32 non_default = 0;
    for (u32 i = 0; i < registry_->Count(); ++i) {
        const SettingId id{static_cast<u16>(i + 1)};
        const SettingDesc& d = *registry_->Desc(id);
        const Value v = registry_->Current(id);
        if (v == d.default_value) continue;
        ++non_default;
        if (e >= kEntries) continue;
        char text[kMaxValueTextBytes];
        if ((d.flags & kSecret) != 0) std::snprintf(text, sizeof(text), "<secret>");
        else if (FormatValue(d, v, text, sizeof(text)) == 0) continue;
        char item[debug::kCrashStateEntryTextBytes];
        const int n = std::snprintf(item, sizeof(item), "%.*s=%s ", static_cast<int>(registry_->KeyOf(id).View().Length()), registry_->KeyOf(id).View().Data(), text);
        if (n <= 0) continue;
        const usize len = static_cast<usize>(n) < sizeof(item) - 1 ? static_cast<usize>(n) : sizeof(item) - 1;
        if (used + len >= sizeof(entries[0])) {
            if (++e >= kEntries) continue;
            used = 0;
        }
        std::memcpy(entries[e] + used, item, len);
        used += len;
        entries[e][used] = '\0';
    }
    char head[debug::kCrashStateEntryTextBytes];
    std::snprintf(head, sizeof(head), "%u non-default", static_cast<unsigned>(non_default));
    debug::SetCrashStateText("settings", head);
    int now_used = 0;
    for (int i = 0; i < kEntries; ++i) {
        if (entries[i][0] != '\0') now_used = i + 1;
    }
    const int top = now_used > crash_entries_used_ ? now_used : crash_entries_used_;
    for (int i = 0; i < top; ++i) {
        char name[16];
        std::snprintf(name, sizeof(name), "settings %d", i + 1);
        debug::SetCrashStateText(name, entries[i][0] != '\0' ? entries[i] : nullptr);
    }
    crash_entries_used_ = now_used;
}

} // namespace engine_placeholder::settings
