#include "spatial/spatial_world.h"

namespace engine_placeholder::spatial {

SpatialWorld::SpatialWorld() {
    CreateSet("render");
    CreateSet("physics");
    CreateSet("audio");
    CreateSet("gameplay");
}

SetId SpatialWorld::CreateSet(StringView name, const SpatialSetConfig& config) {
    if (count_ >= kMaxSpatialSets) {
        ++stats_.refused_sets;
        return kInvalidSetId;
    }
    const SetId id = count_++;
    sets_[id] = MakeUnique<SpatialSet>(config);
    usize length = name.Length() < kMaxSetNameLength ? name.Length() : kMaxSetNameLength;
    for (usize i = 0; i < length; ++i) {
        names_[id][i] = name[i];
    }
    names_[id][length] = '\0';
    return id;
}

SetId SpatialWorld::Find(StringView name) const {
    for (SetId id = 0; id < count_; ++id) {
        if (NameOf(id) == name) {
            return id;
        }
    }
    return kInvalidSetId;
}

StringView SpatialWorld::NameOf(SetId id) const {
    if (id >= count_) {
        return StringView();
    }
    usize length = 0;
    while (names_[id][length] != '\0') {
        ++length;
    }
    return StringView(names_[id].Data(), length);
}

void SpatialWorld::Clear() {
    for (SetId id = 0; id < count_; ++id) {
        sets_[id]->Clear();
    }
}

} // namespace engine_placeholder::spatial
