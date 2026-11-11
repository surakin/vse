#include "storage/world_storage.h"

namespace engine_placeholder::storage {

Status WorldStorage::Save(ecs::World&, KeyIndex&, const SaveOptions&, Report*) {
    if (registry_->Count() == 0) {
        return Status::Make(ErrorCode::Invalid, "no component is registered, so nothing can be stored");
    }
    return Status::Make(ErrorCode::Other, "WorldStorage::Save is implemented on Day 260");
}

Status WorldStorage::Load(ecs::World&, KeyIndex&, const LoadOptions&, Report*) {
    return Status::Make(ErrorCode::Other, "WorldStorage::Load is implemented on Day 260");
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
