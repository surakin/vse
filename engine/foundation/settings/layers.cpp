#include "settings/layers.h"

#include <cstdio>
#include <cstring>

#include "log/assert.h"

namespace engine_placeholder::settings {

namespace {

void CopyTo(char* out, usize capacity, StringView text) {
    if (capacity == 0) return;
    const usize n = text.Length() < capacity - 1 ? text.Length() : capacity - 1;
    for (usize i = 0; i < n; ++i) out[i] = text[i];
    out[n] = '\0';
}

void Report(Vector<LayerProblem>* problems, StringView layer, StringView key, const char* format, const char* a = "", const char* b = "") {
    if (problems == nullptr) return;
    LayerProblem p;
    CopyTo(p.layer, sizeof(p.layer), layer);
    CopyTo(p.key, sizeof(p.key), key);
    std::snprintf(p.message, sizeof(p.message), format, a, b);
    problems->Push(p);
}

} // namespace

const char* LayerRoleName(LayerRole role) {
    switch (role) {
    case LayerRole::System: return "system";
    case LayerRole::User: return "user";
    case LayerRole::Project: return "project";
    case LayerRole::Custom: return "custom";
    case LayerRole::Environment: return "environment";
    case LayerRole::CommandLine: return "command-line";
    }
    return "unknown";
}

LayerStack::LayerStack() = default;
LayerStack::~LayerStack() = default;

usize LayerStack::AddLayer(StringView name, LayerRole role, ConfigStorePtr store) {
    usize at = layers_.Size();
    while (at > 0 && static_cast<u8>(layers_[at - 1].role) > static_cast<u8>(role)) --at;
    Layer layer;
    layer.name = String<HeapAllocator>(HeapAllocator::Default(), name);
    layer.role = role;
    layer.store = static_cast<ConfigStorePtr&&>(store);
    layers_.Insert(at, static_cast<Layer&&>(layer));
    if (write_layer_ != static_cast<usize>(-1) && write_layer_ >= at) ++write_layer_;
    return at;
}

void LayerStack::Clear() {
    layers_.Clear();
    write_layer_ = static_cast<usize>(-1);
}

usize LayerStack::Find(LayerRole role) const {
    for (usize i = 0; i < layers_.Size(); ++i) {
        if (layers_[i].role == role) return i;
    }
    return layers_.Size();
}

void LayerStack::LoadAll(Vector<LayerProblem>* problems) {
    for (usize i = 0; i < layers_.Size(); ++i) {
        Layer& layer = layers_[i];
        const Status s = layer.store->Load();
        if (s.Is(StoreError::Damaged)) {
            layer.state = LayerState::Damaged;
            Report(problems, layer.name.View(), StringView(), "damaged: some lines could not be understood; the rest is used");
        } else if (s.Failed()) {
            layer.state = LayerState::Failed;
            Report(problems, layer.name.View(), StringView(), "could not be read (%s); skipped", StoreErrorName(s.error));
        } else {
            layer.state = layer.store->IsPresent() ? LayerState::Loaded : LayerState::Missing;
        }
    }
}

bool LayerStack::ResolveFrom(const Registry& registry, SettingId id, usize below, Resolution* out, Vector<LayerProblem>* problems) {
    const SettingDesc& desc = *registry.Desc(id);
    const StringView key = registry.KeyOf(id).View();
    for (usize i = below; i-- > 0;) {
        Layer& layer = layers_[i];
        if (layer.state == LayerState::Failed) continue;
        StoredValue stored;
        Status got = layer.store->Get(key, &stored);
        StringView found_key = key;
        // A file written by an older version may still use a key the setting was renamed from.
        for (u32 d = 0; got.Is(StoreError::NotFound) && d < desc.deprecated_count; ++d) {
            found_key = StringView(desc.deprecated_keys[d]);
            got = layer.store->Get(found_key, &stored);
        }
        if (got.Is(StoreError::NotFound)) continue;
        if (found_key != key) Report(problems, layer.name.View(), key, "uses the deprecated key '%s'; it is now '%s'", String<HeapAllocator>(HeapAllocator::Default(), found_key).CStr(), String<HeapAllocator>(HeapAllocator::Default(), key).CStr());
        char origin[96];
        layer.store->Origin(found_key, origin, sizeof(origin));
        if (got.Failed()) {
            Report(problems, layer.name.View(), key, "could not be read (%s); the next lower layer is used", StoreErrorName(got.error));
            continue;
        }
        Value value;
        const ValueError converted = FromStored(desc, stored, &value);
        if (converted != ValueError::None && converted != ValueError::Clamped) {
            char what[200];
            std::snprintf(what, sizeof(what), "bad value (%s) in %s; the next lower layer is used", ValueErrorName(converted), origin[0] != '\0' ? origin : "the layer");
            Report(problems, layer.name.View(), key, "%s", what);
            continue;
        }
        if (converted == ValueError::Clamped) Report(problems, layer.name.View(), key, "value outside the range was clamped (%s)", origin);
        out->value = value;
        out->layer = static_cast<i32>(i);
        out->note = converted;
        CopyTo(out->layer_name, sizeof(out->layer_name), layer.name.View());
        CopyTo(out->origin, sizeof(out->origin), StringView(origin));
        return true;
    }
    return false;
}

void LayerStack::Resolve(const Registry& registry, SettingId id, Resolution* out, Vector<LayerProblem>* problems) {
    ENGINE_ASSERT(registry.Desc(id) != nullptr, "settings::LayerStack::Resolve: unknown setting");
    *out = Resolution();
    if (ResolveFrom(registry, id, layers_.Size(), out, problems)) return;
    out->value = registry.Desc(id)->default_value;
    out->layer = -1;
    CopyTo(out->layer_name, sizeof(out->layer_name), "defaults");
}

void LayerStack::ResolveBelow(const Registry& registry, SettingId id, usize layer, Resolution* out) {
    *out = Resolution();
    if (ResolveFrom(registry, id, layer, out, nullptr)) return;
    out->value = registry.Desc(id)->default_value;
    out->layer = -1;
    CopyTo(out->layer_name, sizeof(out->layer_name), "defaults");
}

usize LayerStack::WriteLayer() const {
    if (write_layer_ != static_cast<usize>(-1)) return write_layer_;
    return Find(LayerRole::User);
}

void LayerStack::SetWriteLayer(usize layer) {
    ENGINE_ASSERT(layer < layers_.Size(), "settings::LayerStack::SetWriteLayer: no such layer");
    write_layer_ = layer;
}

Status LayerStack::Set(const Registry& registry, SettingId id, const Value& value, bool minimal) {
    const usize w = WriteLayer();
    if (w >= layers_.Size()) return Status::Of(StoreError::NotFound);
    ConfigStore& store = *layers_[w].store;
    const SettingDesc& desc = *registry.Desc(id);
    const StringView key = registry.KeyOf(id).View();
    if (minimal) {
        Resolution lower;
        ResolveBelow(registry, id, w, &lower);
        if (lower.value == value) {
            const Status removed = store.Remove(key);
            return removed.Is(StoreError::NotFound) ? Status() : removed;
        }
    }
    StoredValue stored;
    const StoreError converted = ToStored(desc, value, (store.Capabilities() & kCapTypedValues) != 0, &stored);
    if (converted != StoreError::None) return Status::Of(converted);
    return store.Set(key, stored);
}

Status LayerStack::Unset(const Registry& registry, SettingId id) {
    const usize w = WriteLayer();
    if (w >= layers_.Size()) return Status::Of(StoreError::NotFound);
    const Status removed = layers_[w].store->Remove(registry.KeyOf(id).View());
    return removed.Is(StoreError::NotFound) ? Status() : removed;
}

bool SuggestKey(const Registry& registry, StringView text, Key* out) {
    if (text.Length() == 0 || text.Length() > 200) return false;
    usize best = ~usize{0};
    SettingId best_id;
    Vector<usize> previous, current; // the rows of the distance table, reused for every candidate
    previous.Resize(text.Length() + 1);
    current.Resize(text.Length() + 1);
    for (u32 i = 0; i < registry.Count(); ++i) {
        const SettingId id = registry.SortedAt(i);
        const StringView candidate = registry.KeyOf(id).View();
        // Levenshtein distance with two rows.
        const usize n = candidate.Length(), m = text.Length();
        if ((n > m ? n - m : m - n) > 8) continue;
        for (usize j = 0; j <= m; ++j) previous[j] = j;
        for (usize a = 1; a <= n; ++a) {
            current[0] = a;
            for (usize b = 1; b <= m; ++b) {
                const usize substitution = previous[b - 1] + (candidate[a - 1] == text[b - 1] ? 0 : 1);
                usize v = substitution;
                if (previous[b] + 1 < v) v = previous[b] + 1;
                if (current[b - 1] + 1 < v) v = current[b - 1] + 1;
                current[b] = v;
            }
            for (usize b = 0; b <= m; ++b) previous[b] = current[b];
        }
        if (previous[m] < best) {
            best = previous[m];
            best_id = id;
        }
    }
    const usize limit = text.Length() / 3 > 3 ? text.Length() / 3 : 3;
    if (!best_id.IsValid() || best > limit || best >= text.Length()) return false; // a suggestion must share something with what was typed
    *out = registry.KeyOf(best_id);
    return true;
}

} // namespace engine_placeholder::settings
