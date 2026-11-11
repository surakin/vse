#include "storage/world_storage.h"

#include "algorithm.h"
#include "containers/array.h"
#include "bytes/byte_reader.h"
#include "bytes/byte_writer.h"
#include "ecs/transform.h"
#include "storage/builtin_components.h"
#include "storage/component_record.h"

namespace engine_placeholder::storage {

namespace {

constexpr u32 kStoredFormat = 1;
constexpr const char* kManifestName = "manifest";

// Components that are never written by their own record: the key is the record's key, the parent link is made from `ecs::Parent`, the opaque components are
// appended as the records they hold.
bool IsStructural(u32 id) {
    return id == kEntityKeyId || id == kParentLinkId || id == kOpaqueComponentsId;
}

ecs::ComponentId EcsIdOf(const ComponentRegistry& registry, u32 id) {
    const RegisteredComponent* c = registry.FindById(id);
    return c != nullptr ? c->EcsId() : ecs::kInvalidComponent;
}

struct Candidate {
    EntityKey key;
    ecs::Entity entity;
    friend bool operator<(const Candidate& a, const Candidate& b) { return a.key < b.key; }
};

Status CollectCandidates(ecs::World& world, const KeyIndex& index, const Selection& selection, Vector<Candidate>* out, Report* report) {
    if (selection.kind == Selection::Kind::Keys) {
        for (usize i = 0; i < selection.keys.Size(); ++i) {
            const ecs::Entity e = index.Find(selection.keys[i]);
            if (e.IsValid()) {
                out->Push(Candidate{selection.keys[i], e});
            } else {
                ++report->missing_keys;
            }
        }
    } else {
        const ecs::World& view = world;
        world.Query<ecs::Read<EntityKeyComponent>>().ForEach([&](ecs::Entity e, const EntityKeyComponent& c) {
            if (c.key.IsNull() || index.Find(c.key) != e) {
                return; // no identity, or a duplicate the index left out
            }
            if (selection.kind == Selection::Kind::Filter) {
                for (usize i = 0; i < selection.require.Size(); ++i) {
                    if (!view.HasById(e, selection.require[i])) {
                        return;
                    }
                }
                if (selection.filter != nullptr && !selection.filter(view, e, selection.user)) {
                    return;
                }
            }
            out->Push(Candidate{c.key, e});
        });
    }
    Sort(Span<Candidate>(out->Data(), out->Size()));
    // A key named twice is one entity.
    usize kept = 0;
    for (usize i = 0; i < out->Size(); ++i) {
        if (kept == 0 || !((*out)[kept - 1].key == (*out)[i].key)) {
            (*out)[kept++] = (*out)[i];
        }
    }
    out->RemoveRange(kept, out->Size() - kept);
    return Status::Success();
}

struct Piece {
    u32 id;
    u32 version;
    usize start;
    usize length;
};

// The blob of one entity: a count, then the framed records of its components.
Status EncodeEntity(const ComponentRegistry& registry, const ecs::World& world, const KeyIndex& index, ecs::Entity e, const CodecLimits& limits, Report* report,
                    bytes::ByteWriter& blob) {
    bytes::ByteWriter payload;
    Vector<Piece> pieces;
    for (u32 i = 0; i < registry.Count(); ++i) {
        const RegisteredComponent& c = registry.At(i);
        if (IsStructural(c.Id()) || c.EcsId() == ecs::kInvalidComponent || !world.HasById(e, c.EcsId())) {
            continue;
        }
        const bool presence = (c.Flags() & kComponentPresence) != 0;
        if (!c.IsStored() && !presence) {
            continue;
        }
        const usize start = payload.Size();
        if (c.IsStored()) {
            ENGINE_TRY(c.Encode(world.CellOf(e, c.EcsId()), payload, limits));
        }
        pieces.Push(Piece{c.Id(), c.Version(), start, payload.Size() - start});
    }
    // The parent, as the key of the parent entity.
    const ecs::Parent* parent = world.Peek<ecs::Parent>(e);
    if (parent != nullptr) {
        const EntityKey target = index.KeyOf(parent->entity);
        if (target.IsNull()) {
            ++report->unkeyed_parents;
        } else {
            const RegisteredComponent* link = registry.FindById(kParentLinkId);
            if (link != nullptr) {
                const ParentLink value{EntityRef{target}};
                const usize start = payload.Size();
                ENGINE_TRY(link->Encode(&value, payload, limits));
                pieces.Push(Piece{link->Id(), link->Version(), start, payload.Size() - start});
            }
        }
    }
    const OpaqueComponents* opaque = nullptr;
    const ecs::ComponentId opaque_id = EcsIdOf(registry, kOpaqueComponentsId);
    if (opaque_id != ecs::kInvalidComponent) {
        opaque = static_cast<const OpaqueComponents*>(world.CellOf(e, opaque_id));
    }
    const usize total = pieces.Size() + (opaque != nullptr ? opaque->items.Size() : 0);
    if (total > limits.max_components_per_entity) {
        return Status::Make(ErrorCode::LimitExceeded, "an entity has more components than the limit");
    }
    blob.WriteU32(static_cast<u32>(total));
    const ByteSpan bytes = payload.Bytes();
    for (usize i = 0; i < pieces.Size(); ++i) {
        ComponentRecord record;
        record.id = pieces[i].id;
        record.version = pieces[i].version;
        record.bytes = ByteSpan(bytes.Data() + pieces[i].start, pieces[i].length);
        ENGINE_TRY(WriteComponentRecord(record, limits, blob));
    }
    if (opaque != nullptr) {
        for (usize i = 0; i < opaque->items.Size(); ++i) {
            ENGINE_TRY(WriteComponentRecord(opaque->items[i].View(), limits, blob));
        }
    }
    if (blob.Size() > limits.max_record_bytes) {
        return Status::Make(ErrorCode::LimitExceeded, "an entity is larger than the record limit");
    }
    return Status::Success();
}

void AppendNumber(String<HeapAllocator>* text, u32 value) {
    Array<char, 10> digits;
    usize n = 0;
    do {
        digits[n++] = static_cast<char>('0' + value % 10);
        value /= 10;
    } while (value != 0);
    while (n != 0) {
        text->Append(digits[--n]);
    }
}

// One line per registration: id, version, name.
String<HeapAllocator> ManifestText(const ComponentRegistry& registry) {
    String<HeapAllocator> text(HeapAllocator::Default());
    for (u32 i = 0; i < registry.Count(); ++i) {
        const RegisteredComponent& c = registry.At(i);
        AppendNumber(&text, c.Id());
        text.Append(' ');
        AppendNumber(&text, c.Version());
        text.Append(' ');
        text.Append(StringView(c.Name()));
        text.Append('\n');
    }
    return text;
}

bool ParseU32(StringView text, usize* at, u32* out) {
    u64 v = 0;
    const usize start = *at;
    while (*at < text.Length() && text[*at] >= '0' && text[*at] <= '9') {
        v = v * 10 + static_cast<u64>(text[*at] - '0');
        if (v > 0xFFFFFFFFull) {
            return false;
        }
        ++*at;
    }
    *out = static_cast<u32>(v);
    return *at > start;
}

Status CheckHeader(const ComponentRegistry& registry, const Store& store) {
    String<HeapAllocator> value(HeapAllocator::Default());
    Status s = store.GetMetadata(metadata::kFormatVersion, &value);
    if (s.Is(ErrorCode::NotFound)) {
        return Status::Success(); // a store nothing was saved to
    }
    ENGINE_TRY(s);
    usize at = 0;
    u32 format = 0;
    if (!ParseU32(value.View(), &at, &format) || at != value.Length()) {
        return Status::Make(ErrorCode::Corrupt, "the store's format version is not a number");
    }
    if (format > kStoredFormat) {
        return Status::Make(ErrorCode::TooNew, "the store was written by a newer format");
    }
    String<HeapAllocator> manifest(HeapAllocator::Default());
    s = store.GetMetadata(kManifestName, &manifest);
    if (s.Is(ErrorCode::NotFound)) {
        return Status::Success();
    }
    ENGINE_TRY(s);
    const StringView text = manifest.View();
    Vector<ManifestEntry> entries;
    usize pos = 0;
    while (pos < text.Length()) {
        ManifestEntry entry;
        if (!ParseU32(text, &pos, &entry.id) || pos >= text.Length() || text[pos] != ' ') {
            return Status::Make(ErrorCode::Corrupt, "the store's manifest is damaged");
        }
        ++pos;
        if (!ParseU32(text, &pos, &entry.version) || pos >= text.Length() || text[pos] != ' ') {
            return Status::Make(ErrorCode::Corrupt, "the store's manifest is damaged");
        }
        ++pos;
        const usize name_start = pos;
        while (pos < text.Length() && text[pos] != '\n') {
            ++pos;
        }
        if (pos >= text.Length()) {
            return Status::Make(ErrorCode::Corrupt, "the store's manifest is damaged");
        }
        entry.name = StringView(text.Data() + name_start, pos - name_start);
        ++pos;
        entries.Push(entry);
    }
    return registry.CheckManifest(Span<const ManifestEntry>(entries.Data(), entries.Size()));
}

void ReportProgress(ProgressFn fn, usize done, usize total, void* user) {
    if (fn != nullptr) {
        fn(done, total, user);
    }
}

} // namespace

Status WorldStorage::Save(ecs::World& world, KeyIndex& index, const SaveOptions& options, Report* report) {
    Report local;
    Report& r = report != nullptr ? *report : local;
    r = Report();
    if (registry_->Count() == 0) {
        return Status::Make(ErrorCode::Invalid, "no component is registered, so nothing can be stored");
    }
    if (options.mode != SaveMode::Full) {
        return Status::Make(ErrorCode::Invalid, "incremental saves arrive on Day 261");
    }
    if (options.selection.kind == Selection::Kind::Filter && options.selection.filter == nullptr && options.selection.require.Empty()) {
        return Status::Make(ErrorCode::Invalid, "a filter selection needs required components or a filter function");
    }
    ENGINE_TRY(index.Sync(world));

    Vector<Candidate> chosen;
    ENGINE_TRY(CollectCandidates(world, index, options.selection, &chosen, &r));

    // The keys already stored, to count replacements and to find the records of entities that are gone.
    Vector<EntityKey> stored;
    ENGINE_TRY(ListKeys(*store_, KeyRange(), ~usize(0), &stored));

    ENGINE_TRY(store_->Begin());
    const usize step = options.batch_size == 0 ? 1 : options.batch_size;
    Status failure = Status::Success();
    usize s = 0; // position in `stored`
    for (usize i = 0; i < chosen.Size() && failure.Ok(); ++i) {
        if (i % step == 0) {
            if (options.cancel != nullptr && options.cancel->IsCancelled()) {
                failure = Status::Make(ErrorCode::Cancelled, "the save was cancelled");
                break;
            }
            ReportProgress(options.progress, i, chosen.Size(), options.user);
        }
        bytes::ByteWriter blob;
        failure = EncodeEntity(*registry_, world, index, chosen[i].entity, limits_, &r, blob);
        if (failure.Failed()) {
            break;
        }
        while (s < stored.Size() && stored[s] < chosen[i].key) {
            ++s;
        }
        const bool existed = s < stored.Size() && stored[s] == chosen[i].key;
        failure = store_->Put(EntityRecord::Make(chosen[i].key, blob.Bytes()));
        if (failure.Failed()) {
            break;
        }
        ++(existed ? r.replaced : r.created);
        r.bytes += blob.Size();
    }
    if (failure.Ok() && options.selection.kind == Selection::Kind::AllKeyed) {
        // Records of entities that no longer exist (or lost their key). Both lists are in key order.
        usize c = 0;
        for (usize i = 0; i < stored.Size() && failure.Ok(); ++i) {
            while (c < chosen.Size() && chosen[c].key < stored[i]) {
                ++c;
            }
            if (c < chosen.Size() && chosen[c].key == stored[i]) {
                continue;
            }
            failure = store_->Remove(stored[i]);
            ++r.removed;
        }
    }
    if (failure.Ok()) {
        failure = store_->SetMetadata(metadata::kFormatVersion, "1");
    }
    if (failure.Ok()) {
        const String<HeapAllocator> manifest = ManifestText(*registry_);
        failure = store_->SetMetadata(kManifestName, manifest.View());
    }
    if (failure.Ok() && options.label.Length() != 0) {
        failure = store_->SetMetadata(metadata::kLastSaveLabel, options.label.View());
    }
    if (failure.Ok()) {
        failure = store_->Commit();
    }
    if (failure.Failed()) {
        static_cast<void>(store_->Rollback()); // the failure being returned is the cause; a batch we opened is open, so this cannot add to it
        return failure;
    }
    ReportProgress(options.progress, chosen.Size(), chosen.Size(), options.user);
    return Status::Success();
}

namespace {

struct Item {
    enum class Kind : u8 { Decode, Presence, Opaque };
    Kind kind = Kind::Decode;
    ComponentRecord record;
    const RegisteredComponent* component = nullptr;
};

// Decodes `bytes` into a throwaway object, to prove they decode; for a parent link also returns the target.
Status Probe(const RegisteredComponent& c, u32 version, ByteSpan bytes, const CodecLimits& limits, EntityKey* parent_target) {
    const ecs::TypeInfo& info = ecs::ComponentRegistry::Global().Info(c.EcsId());
    if (info.is_tag) {
        return c.DecodeRecord(version, bytes, nullptr, limits);
    }
    if (info.default_construct == nullptr) {
        return Status::Make(ErrorCode::Invalid, "a stored component needs a default constructor to be loaded");
    }
    void* memory = HeapAllocator::Default().Alloc(info.size, info.align);
    if (memory == nullptr) {
        return Status::Make(ErrorCode::Other, "out of memory");
    }
    info.default_construct(memory);
    const Status s = c.DecodeRecord(version, bytes, memory, limits);
    if (s.Ok() && c.Id() == kParentLinkId) {
        *parent_target = static_cast<const ParentLink*>(memory)->parent.key;
    }
    info.destroy(memory);
    HeapAllocator::Default().Free(memory, info.size, info.align);
    return s;
}

Status ParseBlob(ByteSpan blob, const CodecLimits& limits, Vector<ComponentRecord>* out) {
    bytes::ByteReader in(blob);
    u32 count = 0;
    const bytes::ByteError e = in.ReadU32(&count);
    if (e != bytes::ByteError::Ok) {
        return detail::FromByteError(e);
    }
    if (count > limits.max_components_per_entity) {
        return Status::Make(ErrorCode::LimitExceeded, "an entity has more components than the limit");
    }
    for (u32 i = 0; i < count; ++i) {
        ComponentRecord record;
        ENGINE_TRY(ReadComponentRecord(in, limits, &record));
        for (usize j = 0; j < out->Size(); ++j) {
            if ((*out)[j].id == record.id) {
                return Status::Make(ErrorCode::Corrupt, "an entity lists one component twice");
            }
        }
        out->Push(record);
    }
    if (!in.AtEnd()) {
        return Status::Make(ErrorCode::Corrupt, "an entity has bytes left after its last component");
    }
    return Status::Success();
}

struct LoadState {
    const ComponentRegistry& registry;
    const CodecLimits& limits;
    const LoadOptions& options;
    ecs::World& world;
    KeyIndex& index;
    Report& report;
    bool apply = false;
    usize done = 0;
    usize total = 0;
    Vector<EntityKey> targets;                              // parent keys named by the records (validation)
    HashMap<EntityKey, u8, HeapAllocator> loaded;            // keys of the records (validation)
    Vector<ecs::Entity> created;                             // entities made by this load (apply), to undo
    LoadState(const ComponentRegistry& r, const CodecLimits& l, const LoadOptions& o, ecs::World& w, KeyIndex& i, Report& rep)
        : registry(r), limits(l), options(o), world(w), index(i), report(rep), loaded(HeapAllocator::Default()) {}
};

Status LoadOne(LoadState& st, const EntityRecord& rec) {
    Vector<ComponentRecord> records;
    ENGINE_TRY(ParseBlob(ByteSpan(rec.blob.Data(), rec.blob.Size()), st.limits, &records));
    Vector<Item> items;
    ecs::ComponentSet set;
    set.Add(EcsIdOf(st.registry, kEntityKeyId));
    bool any_opaque = false;
    for (usize i = 0; i < records.Size(); ++i) {
        Item item;
        item.record = records[i];
        RecordAction action = RecordAction::Skip;
        ENGINE_TRY(st.registry.Resolve(records[i].id, st.options.unknown, &item.component, &action));
        const RegisteredComponent* known = st.registry.FindById(records[i].id);
        if (action == RecordAction::Skip && known != nullptr && (known->Flags() & kComponentPresence) != 0) {
            if (records[i].bytes.Size() != 0) {
                return Status::Make(ErrorCode::Corrupt, "a presence record has bytes");
            }
            item.kind = Item::Kind::Presence;
            item.component = known;
        } else if (action == RecordAction::Skip) {
            if (!st.apply) {
                ++st.report.skipped;
            }
            continue;
        } else if (action == RecordAction::KeepOpaque) {
            item.kind = Item::Kind::Opaque;
            any_opaque = true;
            if (!st.apply) {
                ++st.report.unknown_kept;
            }
        }
        if (item.kind != Item::Kind::Opaque) {
            if (item.component == nullptr || item.component->EcsId() == ecs::kInvalidComponent) {
                return Status::Make(ErrorCode::Other, "a component has no ECS type");
            }
            set.Add(item.component->EcsId());
        }
        items.Push(item);
    }
    if (any_opaque) {
        set.Add(EcsIdOf(st.registry, kOpaqueComponentsId));
    }

    if (!st.apply) {
        for (usize i = 0; i < items.Size(); ++i) {
            if (items[i].kind == Item::Kind::Decode) {
                EntityKey target;
                ENGINE_TRY(Probe(*items[i].component, items[i].record.version, items[i].record.bytes, st.limits, &target));
                if (items[i].component->Id() == kParentLinkId && !target.IsNull()) {
                    st.targets.Push(target);
                }
            }
        }
        const bool exists = st.index.Find(rec.key).IsValid();
        if (st.options.mode == LoadMode::Create && exists) {
            return Status::Make(ErrorCode::Exists, "a stored entity's key is in the world already");
        }
        if (!(st.options.mode == LoadMode::Merge && exists) && !st.world.EnsureArchetype(set)) {
            return Status::Make(ErrorCode::LimitExceeded, "the world cannot hold this combination of components");
        }
        st.loaded.Insert(rec.key, 1);
        return Status::Success();
    }

    ecs::Entity entity = st.options.mode == LoadMode::Merge ? st.index.Find(rec.key) : ecs::Entity{};
    const bool merging = entity.IsValid();
    if (merging) {
        // The stored components of the entity are replaced, its identity (the entity id) stays.
        for (u32 i = 0; i < st.registry.Count(); ++i) {
            const RegisteredComponent& c = st.registry.At(i);
            if ((c.IsStored() || c.Id() == kOpaqueComponentsId) && c.Id() != kEntityKeyId && c.EcsId() != ecs::kInvalidComponent &&
                st.world.HasById(entity, c.EcsId())) {
                static_cast<void>(st.world.RemoveById(entity, c.EcsId())); // Refused cannot happen: the archetype was checked
            }
        }
        static_cast<void>(st.world.RemoveById(entity, ecs::ComponentOf<ecs::Parent>()));         // MissingComponent: it had none
        static_cast<void>(st.world.RemoveById(entity, ecs::ComponentOf<ecs::HierarchyDepth>())); // likewise
        for (ecs::ComponentId id = set.NextFrom(0); id != ecs::kInvalidComponent; id = set.NextFrom(static_cast<u32>(id) + 1u)) {
            if (!st.world.HasById(entity, id) && st.world.AddDefault(entity, id) != ecs::Status::Ok) {
                return Status::Make(ErrorCode::Other, "the world refused a component");
            }
        }
        ++st.report.replaced;
    } else {
        entity = st.world.CreateFromSet(set);
        if (!entity.IsValid()) {
            return Status::Make(ErrorCode::Other, "the world refused an entity");
        }
        st.created.Push(entity);
        ++st.report.created;
    }
    EntityKeyComponent* key = static_cast<EntityKeyComponent*>(st.world.CellOf(entity, EcsIdOf(st.registry, kEntityKeyId)));
    key->key = rec.key;
    for (usize i = 0; i < items.Size(); ++i) {
        if (items[i].kind == Item::Kind::Decode) {
            ENGINE_TRY(items[i].component->DecodeRecord(items[i].record.version, items[i].record.bytes, st.world.CellOf(entity, items[i].component->EcsId()), st.limits));
        } else if (items[i].kind == Item::Kind::Opaque) {
            OpaqueComponents* opaque = static_cast<OpaqueComponents*>(st.world.CellOf(entity, EcsIdOf(st.registry, kOpaqueComponentsId)));
            opaque->items.Push(OpaqueComponent::From(items[i].record));
        }
    }
    st.report.bytes += rec.blob.Size();
    return Status::Success();
}

class LoadVisitor final : public RecordVisitor {
public:
    explicit LoadVisitor(LoadState& state) : state_(state) {}
    bool Visit(const EntityRecord& record) override {
        if (!state_.apply) {
            if (state_.options.cancel != nullptr && state_.options.cancel->IsCancelled()) {
                status = Status::Make(ErrorCode::Cancelled, "the load was cancelled");
                return false;
            }
            if (state_.done % 256 == 0) {
                ReportProgress(state_.options.progress, state_.done, state_.total, state_.options.user);
            }
        }
        ++state_.done;
        status = LoadOne(state_, record);
        return status.Ok();
    }
    Status status;

private:
    LoadState& state_;
};

} // namespace

Status WorldStorage::Load(ecs::World& world, KeyIndex& index, const LoadOptions& options, Report* report) {
    Report local;
    Report& r = report != nullptr ? *report : local;
    r = Report();
    if (registry_->Count() == 0) {
        return Status::Make(ErrorCode::Invalid, "no component is registered, so nothing can be loaded");
    }
    if (options.selection.kind == Selection::Kind::Filter) {
        return Status::Make(ErrorCode::Invalid, "a load cannot filter by components; select all or a set of keys");
    }
    ENGINE_TRY(index.Sync(world));
    ENGINE_TRY(CheckHeader(*registry_, *store_));

    // The keys to read, in key order.
    Vector<EntityKey> wanted;
    usize total = 0;
    if (options.selection.kind == Selection::Kind::Keys) {
        for (usize i = 0; i < options.selection.keys.Size(); ++i) {
            wanted.Push(options.selection.keys[i]);
        }
        Sort(Span<EntityKey>(wanted.Data(), wanted.Size()));
        total = wanted.Size();
    } else {
        ENGINE_TRY(store_->Count(&total));
    }

    LoadState state(*registry_, limits_, options, world, index, r);
    state.total = total;
    auto walk = [&](bool apply) -> Status {
        state.apply = apply;
        state.done = 0;
        LoadVisitor visitor(state);
        if (options.selection.kind == Selection::Kind::Keys) {
            EntityKey previous;
            for (usize i = 0; i < wanted.Size(); ++i) {
                if (i != 0 && wanted[i] == previous) {
                    continue;
                }
                previous = wanted[i];
                EntityRecord record;
                const Status g = store_->Get(wanted[i], &record);
                if (g.Is(ErrorCode::NotFound)) {
                    if (!apply) {
                        ++r.missing_keys;
                    }
                    continue;
                }
                ENGINE_TRY(g);
                if (!visitor.Visit(record)) {
                    return visitor.status;
                }
            }
            return Status::Success();
        }
        ENGINE_TRY(store_->Scan(KeyRange(), visitor));
        return visitor.status;
    };

    // Pass one: every record is decoded and checked; the world is not touched.
    ENGINE_TRY(walk(false));
    if (options.missing == MissingPolicy::Fail) {
        for (usize i = 0; i < state.targets.Size(); ++i) {
            const bool in_load = state.loaded.Find(state.targets[i]) != nullptr;
            const bool in_world = options.mode != LoadMode::Replace && index.Find(state.targets[i]).IsValid();
            if (!in_load && !in_world) {
                return Status::Make(ErrorCode::MissingReference, "a parent link names an entity that is nowhere");
            }
        }
    }
    if (options.cancel != nullptr && options.cancel->IsCancelled()) {
        return Status::Make(ErrorCode::Cancelled, "the load was cancelled");
    }

    // Pass two: apply. Everything was checked, so what can still fail is the world running out of memory or a limit; the entities made so far are destroyed.
    if (options.mode == LoadMode::Replace) {
        Vector<ecs::Entity> old;
        world.Query<ecs::Read<EntityKeyComponent>>().ForEach([&](ecs::Entity e, const EntityKeyComponent&) { old.Push(e); });
        for (usize i = 0; i < old.Size(); ++i) {
            static_cast<void>(world.Destroy(old[i])); // alive: just collected
        }
        r.removed = old.Size();
    }
    const Status applied = walk(true);
    if (applied.Failed()) {
        for (usize i = 0; i < state.created.Size(); ++i) {
            static_cast<void>(world.Destroy(state.created[i])); // may be dead already if the failure was in its own creation
        }
        return applied;
    }
    ENGINE_TRY(index.Sync(world));
    ParentReport parents;
    ENGINE_TRY(ApplyParents(world, index, options.missing, &parents));
    r.nulled = parents.references.nulled;
    r.parents_rejected = parents.rejected;
    ReportProgress(options.progress, total, total, options.user);
    return Status::Success();
}

Status WorldStorage::Delete(Span<const EntityKey> keys) {
    ENGINE_TRY(store_->Begin());
    for (usize i = 0; i < keys.Size(); ++i) {
        const Status s = store_->Remove(keys[i]);
        if (s.Failed() && !s.Is(ErrorCode::NotFound)) {
            static_cast<void>(store_->Rollback()); // the failure is the result; the rollback of a batch we opened cannot add to it
            return s;
        }
    }
    return store_->Commit();
}

Status WorldStorage::List(const KeyRange& range, usize limit, Vector<EntityKey>* out) const {
    return ListKeys(*store_, range, limit, out);
}

} // namespace engine_placeholder::storage
