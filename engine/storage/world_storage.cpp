#include "storage/world_storage.h"

#include "algorithm.h"
#include "containers/array.h"
#include "bytes/byte_reader.h"
#include "bytes/byte_writer.h"
#include "ecs/transform.h"
#include "storage/builtin_components.h"
#include "storage/component_record.h"
#include "jobs/parallel_for.h"
#include "storage/save_plan.h"
#include "storage/save_session.h"

namespace engine_placeholder::storage {

using detail::Chosen;
using detail::Encoded;

namespace {

// Collects records and hands them to the store in groups, so that a store with a faster multi-record write can use it.
class PutQueue {
public:
    explicit PutQueue(Store& store) : store_(&store) {}
    Status Add(EntityKey key, ByteSpan bytes) {
        pending_.Push(EntityRecord::Make(key, bytes));
        return pending_.Size() >= kGroup ? Flush() : Status::Success();
    }
    Status Flush() {
        if (pending_.Empty()) {
            return Status::Success();
        }
        const Status s = store_->PutMany(Span<const EntityRecord>(pending_.Data(), pending_.Size()));
        pending_.Clear();
        return s;
    }

private:
    static constexpr usize kGroup = 256;
    Store* store_;
    Vector<EntityRecord> pending_;
};

constexpr u32 kStoredFormat = 1;
constexpr const char* kManifestName = metadata::kManifest;

// Components that are never written by their own record: the key is the record's key, the parent link is made from `ecs::Parent`, the opaque components are
// appended as the records they hold.
bool IsStructural(u32 id) {
    return id == kEntityKeyId || id == kParentLinkId || id == kOpaqueComponentsId;
}

ecs::ComponentId EcsIdOf(const ComponentRegistry& registry, u32 id) {
    const RegisteredComponent* c = registry.FindById(id);
    return c != nullptr ? c->EcsId() : ecs::kInvalidComponent;
}

Status CollectChosens(ecs::World& world, const KeyIndex& index, const Selection& selection, Vector<Chosen>* out, Report* report) {
    if (selection.kind == Selection::Kind::Keys) {
        for (usize i = 0; i < selection.keys.Size(); ++i) {
            const ecs::Entity e = index.Find(selection.keys[i]);
            if (e.IsValid()) {
                out->Push(Chosen{selection.keys[i], e});
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
            out->Push(Chosen{c.key, e});
        });
    }
    Sort(Span<Chosen>(out->Data(), out->Size()));
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

namespace {

// Collects the key and stored hash of every record, in key order.
class StoredHashes final : public RecordVisitor {
public:
    bool Visit(const EntityRecord& record) override {
        keys.Push(record.key);
        hashes.Push(record.hash);
        return true;
    }
    Vector<EntityKey> keys;
    Vector<u64> hashes;
};

// The largest removal number the key component's log holds.
u64 LargestRemovalSeq(const ecs::World& world, u64 floor) {
    u64 largest = floor;
    const usize n = world.RemovedCount<EntityKeyComponent>();
    for (usize i = 0; i < n; ++i) {
        const u64 seq = world.RemovedAt<EntityKeyComponent>(i).seq;
        largest = seq > largest ? seq : largest;
    }
    return largest;
}

} // namespace

namespace {

// Encodes the entities `entities[begin, end)` into `out[begin, end)`, on the workers of `scheduler` when there is one. Each slot is written by one task only, so
// the result does not depend on the schedule.
struct EncodeJob {
    const ComponentRegistry* registry;
    const ecs::World* world;
    const KeyIndex* index;
    const CodecLimits* limits;
    const Chosen* chosen;
    Encoded* out;
    usize offset;
    void operator()(usize from, usize to) const {
        for (usize i = from; i < to; ++i) {
            Encoded& slot = out[offset + i];
            Report local;
            slot.status = EncodeEntity(*registry, *world, *index, chosen[offset + i].entity, *limits, &local, slot.blob);
            slot.unkeyed = local.unkeyed_parents;
            if (slot.status.Ok()) {
                slot.hash = HashBytes(slot.blob.Bytes());
            }
        }
    }
};

void EncodeSlots(const EncodeJob& job, usize count, jobs::Scheduler* scheduler) {
    if (count == 0) {
        return;
    }
    if (scheduler == nullptr) {
        job(0, count);
        return;
    }
    EncodeJob copy = job;
    const usize workers = scheduler->ParticipantCount();
    usize chunk = count / (workers * 4);
    chunk = chunk < 16 ? 16 : chunk;
    jobs::ParallelForBlocking(*scheduler, count, chunk, copy);
}

} // namespace

Status WorldStorage::Plan(ecs::World& world, KeyIndex& index, const SaveOptions& options, jobs::Scheduler* scheduler, detail::SavePlan* plan) {
    Report& r = plan->report;
    r = Report();
    if (registry_->Count() == 0) {
        return Status::Make(ErrorCode::Invalid, "no component is registered, so nothing can be stored");
    }
    const bool incremental = options.mode == SaveMode::Incremental;
    if (incremental && options.session == nullptr) {
        return Status::Make(ErrorCode::Invalid, "an incremental save needs a SaveSession");
    }
    if ((incremental || options.session != nullptr) && options.selection.kind != Selection::Kind::AllKeyed) {
        return Status::Make(ErrorCode::Invalid, "a save session follows the whole world: use the AllKeyed selection");
    }
    if (options.selection.kind == Selection::Kind::Filter && options.selection.filter == nullptr && options.selection.require.Empty()) {
        return Status::Make(ErrorCode::Invalid, "a filter selection needs required components or a filter function");
    }
    SaveSession* session = options.session;
    if (session != nullptr && session->baseline_ && session->world_id_ != world.Id()) {
        session->Reset();
    }
    ENGINE_TRY(index.Sync(world));
    plan->step = options.batch_size == 0 ? 1 : options.batch_size;
    plan->label = String<HeapAllocator>(HeapAllocator::Default(), options.label.View());
    plan->cancel = options.cancel;
    plan->progress = options.progress;
    plan->user = options.user;
    plan->track_session = session != nullptr;
    plan->all_keyed = options.selection.kind == Selection::Kind::AllKeyed;
    plan->world_id = world.Id();
    if (incremental && session->baseline_ && world.RemovalEpoch() <= session->removal_epoch_ + 1) {
        return PlanChanged(world, index, options, scheduler, plan);
    }
    // A full save, or an incremental one with nothing to go on (a new session, or removals the log no longer holds): compare with the store by hash.
    plan->reconcile = incremental;
    r.reconciled = incremental ? 1 : 0;

    ENGINE_TRY(CollectChosens(world, index, options.selection, &plan->chosen, &r));
    const usize total = plan->chosen.Size();
    plan->encoded.Resize(total);
    const ecs::World& view = world;
    const EncodeJob job{registry_, &view, &index, &limits_, plan->chosen.Data(), plan->encoded.Data(), 0};
    for (usize begin = 0; begin < total; begin += plan->step) {
        if (options.cancel != nullptr && options.cancel->IsCancelled()) {
            return Status::Make(ErrorCode::Cancelled, "the save was cancelled");
        }
        ReportProgress(options.progress, begin, total, options.user);
        const usize count = total - begin < plan->step ? total - begin : plan->step;
        EncodeJob batch = job;
        batch.offset = begin;
        EncodeSlots(batch, count, scheduler);
        for (usize i = begin; i < begin + count; ++i) {
            r.unkeyed_parents += plan->encoded[i].unkeyed;
            ENGINE_TRY(plan->encoded[i].status);
            ++r.encoded;
        }
    }
    if (plan->track_session) {
        plan->tick = world.Tick();
        plan->removal_epoch = world.RemovalEpoch();
        plan->largest_removal_seq = LargestRemovalSeq(world, 0);
        world.Advance(); // a write after this snapshot is stamped later than the save
    }
    return Status::Success();
}

Status WorldStorage::Write(detail::SavePlan& plan) {
    Report& r = plan.report;
    const auto cancelled = [&]() { return plan.cancel != nullptr && plan.cancel->IsCancelled(); };
    if (cancelled()) {
        return Status::Make(ErrorCode::Cancelled, "the save was cancelled");
    }
    // The records already stored, to count replacements, to skip what is unchanged (reconcile) and to find the records of entities that are gone.
    StoredHashes stored;
    if (!plan.changed_style) {
        ENGINE_TRY(store_->Scan(KeyRange(), stored));
    }
    ENGINE_TRY(store_->Begin());
    Status failure = Status::Success();
    PutQueue queue(*store_);
    if (plan.changed_style) {
        for (usize i = 0; i < plan.puts.Size() && failure.Ok(); ++i) {
            if (i % plan.step == 0 && cancelled()) {
                failure = Status::Make(ErrorCode::Cancelled, "the save was cancelled");
                break;
            }
            failure = queue.Add(plan.puts[i].key, plan.puts[i].blob.Bytes());
        }
        if (failure.Ok()) {
            failure = queue.Flush();
        }
        for (usize i = 0; i < plan.removes.Size() && failure.Ok(); ++i) {
            failure = store_->Remove(plan.removes[i]);
            if (failure.Failed() && failure.Code() == ErrorCode::NotFound) {
                failure = Status::Success(); // somebody else took it
            }
            ++r.removed;
        }
    } else {
        usize s = 0; // position in `stored`
        for (usize i = 0; i < plan.chosen.Size() && failure.Ok(); ++i) {
            if (i % plan.step == 0 && cancelled()) {
                failure = Status::Make(ErrorCode::Cancelled, "the save was cancelled");
                break;
            }
            const Chosen& c = plan.chosen[i];
            detail::Encoded& e = plan.encoded[i];
            while (s < stored.keys.Size() && stored.keys[s] < c.key) {
                ++s;
            }
            const bool existed = s < stored.keys.Size() && stored.keys[s] == c.key;
            if (plan.reconcile && existed && stored.hashes[s] == e.hash) {
                continue;
            }
            failure = queue.Add(c.key, e.blob.Bytes());
            if (failure.Failed()) {
                break;
            }
            ++(existed ? r.replaced : r.created);
            r.bytes += e.blob.Size();
            e.blob = bytes::ByteWriter(); // the queue holds a copy; give the memory back as the write goes on
        }
        if (failure.Ok()) {
            failure = queue.Flush();
        }
        if (failure.Ok() && plan.all_keyed) {
            // Records of entities that no longer exist (or lost their key). Both lists are in key order.
            usize c = 0;
            for (usize i = 0; i < stored.keys.Size() && failure.Ok(); ++i) {
                while (c < plan.chosen.Size() && plan.chosen[c].key < stored.keys[i]) {
                    ++c;
                }
                if (c < plan.chosen.Size() && plan.chosen[c].key == stored.keys[i]) {
                    continue;
                }
                failure = store_->Remove(stored.keys[i]);
                ++r.removed;
            }
        }
    }
    if (failure.Ok()) {
        failure = store_->SetMetadata(metadata::kFormatVersion, "1");
    }
    if (failure.Ok()) {
        const String<HeapAllocator> manifest = ManifestText(*registry_);
        failure = store_->SetMetadata(kManifestName, manifest.View());
    }
    if (failure.Ok() && plan.label.Length() != 0) {
        failure = store_->SetMetadata(metadata::kLastSaveLabel, plan.label.View());
    }
    if (failure.Ok()) {
        failure = store_->Commit();
    }
    if (failure.Failed()) {
        static_cast<void>(store_->Rollback()); // the failure being returned is the cause; a batch we opened is open, so this cannot add to it
        return failure;
    }
    ReportProgress(plan.progress, r.encoded, r.encoded, plan.user);
    return Status::Success();
}

void WorldStorage::Adopt(SaveSession& session, detail::SavePlan& plan) {
    if (plan.changed_style) {
        for (usize i = 0; i < plan.removes.Size(); ++i) {
            session.hash_of_->Remove(plan.removes[i]);
        }
        for (usize i = 0; i < plan.forgotten.Size(); ++i) {
            session.key_of_->Remove(plan.forgotten[i]);
        }
        for (usize i = 0; i < plan.puts.Size(); ++i) {
            session.hash_of_->Insert(plan.puts[i].key, plan.puts[i].hash);
        }
        for (usize i = 0; i < plan.rekeyed.Size(); ++i) {
            session.key_of_->Insert(plan.rekeyed[i].entity, plan.rekeyed[i].key);
        }
    } else {
        session.Reset();
        for (usize i = 0; i < plan.chosen.Size(); ++i) {
            session.hash_of_->Insert(plan.chosen[i].key, plan.encoded[i].hash);
            session.key_of_->Insert(plan.chosen[i].entity, plan.chosen[i].key);
        }
        session.baseline_ = true;
        session.world_id_ = plan.world_id;
    }
    session.since_ = plan.tick;
    session.removal_epoch_ = plan.removal_epoch;
    session.last_removal_seq_ = plan.largest_removal_seq;
}

Status WorldStorage::Save(ecs::World& world, KeyIndex& index, const SaveOptions& options, Report* report) {
    Report local;
    Report& r = report != nullptr ? *report : local;
    r = Report();
    if (!BeginWriter()) {
        return Status::Make(ErrorCode::Busy, "an asynchronous save is still running");
    }
    detail::SavePlan plan;
    Status s = Plan(world, index, options, nullptr, &plan);
    if (s.Ok()) {
        s = Write(plan);
    }
    if (s.Ok() && options.session != nullptr) {
        Adopt(*options.session, plan);
    }
    EndWriter();
    r = plan.report;
    return s;
}

Status WorldStorage::PlanChanged(ecs::World& world, KeyIndex& index, const SaveOptions& options, jobs::Scheduler* scheduler, detail::SavePlan* plan) {
    SaveSession& session = *options.session;
    Report& r = plan->report;
    plan->changed_style = true;
    const ecs::World& view = world;
    const ecs::ComponentId key_column = ecs::ComponentOf<EntityKeyComponent>();

    // The columns whose change changes a record: every stored component, and the ones that are written as something else (the parent, the unknown ones).
    Vector<ecs::ComponentId> triggers;
    for (u32 i = 0; i < registry_->Count(); ++i) {
        const RegisteredComponent& c = registry_->At(i);
        if ((c.Flags() & kComponentDerived) == 0) {
            triggers.Push(c.EcsId());
        }
    }

    HashMap<EntityKey, u32, HeapAllocator> queued(HeapAllocator::Default());
    const auto queue_remove = [&](EntityKey key) {
        if (session.hash_of_->Find(key) != nullptr && queued.Find(key) == nullptr) {
            queued.Insert(key, 1);
            plan->removes.Push(key);
        }
    };

    // Entities destroyed or stripped of their key since the last save.
    u64 largest = session.last_removal_seq_;
    const usize logged = view.RemovedCount<EntityKeyComponent>();
    for (usize i = 0; i < logged; ++i) {
        const ecs::RemovedComponent<EntityKeyComponent> entry = view.RemovedAt<EntityKeyComponent>(i);
        if (entry.seq <= session.last_removal_seq_) {
            continue;
        }
        largest = entry.seq > largest ? entry.seq : largest;
        plan->forgotten.Push(entry.entity);
        if (!entry.value.key.IsNull() && !index.Find(entry.value.key).IsValid()) {
            queue_remove(entry.value.key);
        }
    }

    // The entities in the chunks written since the last save.
    const u64 since = session.since_;
    for (u32 a = 0; a < view.ArchetypeCount(); ++a) {
        const ecs::Archetype& archetype = view.ArchetypeAt(a);
        if (!archetype.Has(key_column)) {
            continue;
        }
        for (u32 c = 0; c < archetype.ChunkCount(); ++c) {
            ecs::Chunk* chunk = archetype.ChunkAt(c);
            bool dirty = false;
            for (usize t = 0; t < triggers.Size() && !dirty; ++t) {
                dirty = archetype.Has(triggers[t]) && (archetype.WrittenTick(chunk, triggers[t]) > since || archetype.AddedTick(chunk, triggers[t]) > since);
            }
            if (!dirty) {
                ++r.chunks_skipped;
                continue;
            }
            ++r.chunks_visited;
            const Span<ecs::Entity> entities = archetype.Entities(chunk);
            for (usize row = 0; row < entities.Size(); ++row) {
                const ecs::Entity e = entities[row];
                const EntityKey key = index.KeyOf(e);
                if (key.IsNull() || index.Find(key) != e) {
                    continue;
                }
                plan->chosen.Push(Chosen{key, e});
            }
        }
    }

    const usize total = plan->chosen.Size();
    plan->encoded.Resize(total);
    const EncodeJob job{registry_, &view, &index, &limits_, plan->chosen.Data(), plan->encoded.Data(), 0};
    for (usize begin = 0; begin < total; begin += plan->step) {
        if (options.cancel != nullptr && options.cancel->IsCancelled()) {
            return Status::Make(ErrorCode::Cancelled, "the save was cancelled");
        }
        ReportProgress(options.progress, begin, total, options.user);
        const usize count = total - begin < plan->step ? total - begin : plan->step;
        EncodeJob batch = job;
        batch.offset = begin;
        EncodeSlots(batch, count, scheduler);
        for (usize i = begin; i < begin + count; ++i) {
            r.unkeyed_parents += plan->encoded[i].unkeyed;
            ENGINE_TRY(plan->encoded[i].status);
            ++r.encoded;
        }
    }
    // What changed: a record whose hash differs from the one the session wrote.
    for (usize i = 0; i < total; ++i) {
        const Chosen& c = plan->chosen[i];
        detail::Encoded& e = plan->encoded[i];
        const u64* old = session.hash_of_->Find(c.key);
        if (old == nullptr || *old != e.hash) {
            ++(old != nullptr ? r.replaced : r.created);
            r.bytes += e.blob.Size();
            detail::PutOp put;
            put.key = c.key;
            put.entity = c.entity;
            put.hash = e.hash;
            put.blob = Move(e.blob);
            plan->puts.Push(Move(put));
        }
        const EntityKey* previous = session.key_of_->Find(c.entity);
        if (previous != nullptr && !(*previous == c.key) && !previous->IsNull() && !index.Find(*previous).IsValid()) {
            queue_remove(*previous); // the entity changed its key: the record under the old one goes
        }
        plan->rekeyed.Push(detail::KeyedEntity{c.key, c.entity});
    }
    plan->encoded.Clear();
    plan->chosen.Clear();
    plan->tick = world.Tick();
    plan->removal_epoch = world.RemovalEpoch();
    plan->largest_removal_seq = largest;
    world.Advance(); // a write after this snapshot is stamped later than the save
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
