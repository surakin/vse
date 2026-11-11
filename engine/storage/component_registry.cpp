#include "storage/component_registry.h"

#include "cstr/cstr.h"
#include "ecs/archetype.h"
#include "log/assert.h"

namespace engine_placeholder::storage {

Status RegisteredComponent::Encode(const void* component, bytes::ByteWriter& out, const CodecLimits& limits) const {
    if (!IsStored()) {
        return Status::Make(ErrorCode::Invalid, "the component is derived or skipped and has no stored form");
    }
    const usize start = out.Size();
    for (usize i = 0; i < fields_.Size(); ++i) {
        const FieldDesc& f = fields_[i];
        const Status s = f.encode(f, component, out, limits);
        if (s.Failed()) {
            out.Truncate(start);
            return s;
        }
    }
    return Status::Success();
}

Status RegisteredComponent::Decode(bytes::ByteReader& in, void* component, const CodecLimits& limits) const {
    if (!IsStored()) {
        return Status::Make(ErrorCode::Invalid, "the component is derived or skipped and has no stored form");
    }
    for (usize i = 0; i < fields_.Size(); ++i) {
        const FieldDesc& f = fields_[i];
        const Status s = f.decode(f, in, component, limits);
        if (s.Failed()) {
            return s;
        }
    }
    return Status::Success();
}

Status ComponentRegistry::RegisterEntry(const ComponentDesc& desc, ecs::ComponentId ecs_id, bool is_tag) {
    if (desc.id == 0) {
        return Status::Make(ErrorCode::Invalid, "a component id must not be 0");
    }
    if (desc.name == nullptr || desc.name[0] == '\0') {
        return Status::Make(ErrorCode::Invalid, "a component needs a name");
    }
    if (desc.version == 0) {
        return Status::Make(ErrorCode::Invalid, "a component version starts at 1");
    }
    const bool stored = (desc.flags & (kComponentDerived | kComponentSkip)) == 0;
    const usize field_count = desc.fields.size();
    if (!stored && field_count != 0) {
        return Status::Make(ErrorCode::Invalid, "a derived or skipped component has no fields");
    }
    if (stored && !is_tag && field_count == 0) {
        return Status::Make(ErrorCode::Invalid, "a stored component with data needs fields (or flag it derived or skip)");
    }
    if (is_tag && field_count != 0) {
        return Status::Make(ErrorCode::Invalid, "a tag has no fields");
    }
    usize index = 0;
    for (const FieldDesc& f : desc.fields) {
        if (f.name == nullptr || f.name[0] == '\0' || f.encode == nullptr || f.decode == nullptr) {
            return Status::Make(ErrorCode::Invalid, "a field needs a name and codec functions (make it with storage::Field)");
        }
        if (f.needs_range && !f.options.has_range) {
            return Status::Make(ErrorCode::Invalid, "an enum field needs a declared range (FieldOptions::Enum)");
        }
        usize other = 0;
        for (const FieldDesc& g : desc.fields) {
            if (other < index && CStrCompare(f.name, g.name) == 0) {
                return Status::Make(ErrorCode::Invalid, "two fields have one name");
            }
            ++other;
        }
        ++index;
    }
    for (usize i = 0; i < components_.Size(); ++i) {
        const RegisteredComponent& c = *components_[i];
        if (c.id_ == desc.id) {
            return Status::Make(ErrorCode::Exists, "the component id is registered already");
        }
        if (CStrCompare(c.name_, desc.name) == 0) {
            return Status::Make(ErrorCode::Exists, "the component name is registered already");
        }
        if (ecs_id != ecs::kInvalidComponent && c.ecs_id_ == ecs_id) {
            return Status::Make(ErrorCode::Exists, "the component type is registered already");
        }
    }
    UniquePtr<RegisteredComponent> entry = MakeUnique<RegisteredComponent>();
    entry->id_ = desc.id;
    entry->name_ = desc.name;
    entry->version_ = desc.version;
    entry->flags_ = desc.flags;
    entry->ecs_id_ = ecs_id;
    entry->is_tag_ = is_tag;
    for (const FieldDesc& f : desc.fields) {
        entry->fields_.Push(f);
    }
    components_.Push(Move(entry));
    return Status::Success();
}

const RegisteredComponent* ComponentRegistry::FindById(u32 id) const {
    for (usize i = 0; i < components_.Size(); ++i) {
        if (components_[i]->id_ == id) {
            return components_[i].Get();
        }
    }
    return nullptr;
}

const RegisteredComponent* ComponentRegistry::FindByName(StringView name) const {
    for (usize i = 0; i < components_.Size(); ++i) {
        if (StringView(components_[i]->name_, CStrLength(components_[i]->name_)) == name) {
            return components_[i].Get();
        }
    }
    return nullptr;
}

const RegisteredComponent* ComponentRegistry::FindByEcsId(ecs::ComponentId id) const {
    if (id == ecs::kInvalidComponent) {
        return nullptr;
    }
    for (usize i = 0; i < components_.Size(); ++i) {
        if (components_[i]->ecs_id_ == id) {
            return components_[i].Get();
        }
    }
    return nullptr;
}

const RegisteredComponent& ComponentRegistry::At(u32 index) const {
    ENGINE_ASSERT(index < components_.Size(), "ComponentRegistry::At: the index is out of range");
    return *components_[index];
}

void ComponentRegistry::Manifest(Vector<ManifestEntry>* out) const {
    for (usize i = 0; i < components_.Size(); ++i) {
        const RegisteredComponent& c = *components_[i];
        ManifestEntry e;
        e.id = c.id_;
        e.name = StringView(c.name_, CStrLength(c.name_));
        e.version = c.version_;
        out->Push(e);
    }
}

Status ComponentRegistry::CheckManifest(Span<const ManifestEntry> recorded) const {
    for (usize i = 0; i < recorded.Size(); ++i) {
        const ManifestEntry& r = recorded[i];
        const RegisteredComponent* by_id = FindById(r.id);
        const RegisteredComponent* by_name = FindByName(r.name);
        if (by_id != nullptr && StringView(by_id->name_, CStrLength(by_id->name_)) != r.name) {
            return Status::Make(ErrorCode::Mismatch, "a recorded component id now has another name");
        }
        if (by_name != nullptr && by_name->id_ != r.id) {
            return Status::Make(ErrorCode::Mismatch, "a recorded component name now has another id");
        }
        if (by_id != nullptr && r.version > by_id->version_) {
            return Status::Make(ErrorCode::TooNew, "a component was recorded with a newer version than is registered");
        }
    }
    return Status::Success();
}

void ComponentRegistry::UnregisteredWithData(const ecs::World& world, Vector<ecs::ComponentId>* out) const {
    ecs::ComponentSet seen;
    for (u32 a = 0; a < world.ArchetypeCount(); ++a) {
        const ecs::Archetype& archetype = world.ArchetypeAt(a);
        if (archetype.EntityCount() == 0) {
            continue;
        }
        const ecs::ComponentSet& set = archetype.Components();
        for (ecs::ComponentId id = set.NextFrom(0); id != ecs::kInvalidComponent; id = set.NextFrom(static_cast<u32>(id) + 1)) {
            if (seen.Has(id)) {
                continue;
            }
            seen.Add(id);
            if (FindByEcsId(id) == nullptr) {
                out->Push(id);
            }
        }
    }
}

} // namespace engine_placeholder::storage
