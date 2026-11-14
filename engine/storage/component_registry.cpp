#include "storage/component_registry.h"

#include "cstr/cstr.h"
#include "ecs/archetype.h"
#include "log/assert.h"

namespace engine_placeholder::storage
{

Status RegisteredComponent::Encode(const void* component, bytes::ByteWriter& out, const CodecLimits& limits) const
{
    if (!IsStored())
    {
        return Status::Make(ErrorCode::Invalid, "the component is derived or skipped and has no stored form");
    }
    const usize start = out.Size();
    if (custom_.save != nullptr)
    {
        const Status s = custom_.save(component, out, limits);
        if (s.Failed())
        {
            out.Truncate(start);
        }
        return s;
    }
    for (usize i = 0; i < fields_.Size(); ++i)
    {
        const FieldDesc& f = fields_[i];
        const Status s = f.encode(f, component, out, limits);
        if (s.Failed())
        {
            out.Truncate(start);
            return s;
        }
    }
    return Status::Success();
}

Status RegisteredComponent::Decode(bytes::ByteReader& in, void* component, const CodecLimits& limits) const
{
    if (!IsStored())
    {
        return Status::Make(ErrorCode::Invalid, "the component is derived or skipped and has no stored form");
    }
    if (custom_.load != nullptr)
    {
        return custom_.load(in, component, limits);
    }
    for (usize i = 0; i < fields_.Size(); ++i)
    {
        const FieldDesc& f = fields_[i];
        const Status s = f.decode(f, in, component, limits);
        if (s.Failed())
        {
            return s;
        }
    }
    return Status::Success();
}

Status RegisteredComponent::DecodeRecord(u32 stored_version, ByteSpan bytes, void* component,
                                         const CodecLimits& limits) const
{
    if (!IsStored())
    {
        return Status::Make(ErrorCode::Invalid, "the component is derived or skipped and has no stored form");
    }
    if (stored_version > version_)
    {
        return Status::Make(ErrorCode::TooNew, "the record was written by a newer version of the component");
    }
    if (stored_version < oldest_version_ || stored_version == 0)
    {
        return Status::Make(ErrorCode::TooOld, "the record is older than the oldest version that can be upgraded");
    }
    if (bytes.Size() > limits.max_record_bytes)
    {
        return Status::Make(ErrorCode::LimitExceeded, "the record is larger than the record limit");
    }
    ByteSpan current = bytes;
    bytes::ByteWriter buffers[2];
    u32 next = 0;
    for (u32 v = stored_version; v < version_; ++v)
    {
        const UpgradeStep* step = nullptr;
        for (usize i = 0; i < upgrades_.Size(); ++i)
        {
            if (upgrades_[i].from_version == v)
            {
                step = &upgrades_[i];
            }
        }
        if (step == nullptr)
        {  // registration guarantees a step for every version; this is a defensive refusal
            return Status::Make(ErrorCode::Other, "the upgrade chain has a gap");
        }
        bytes::ByteReader in(current);
        bytes::ByteWriter& out = buffers[next];
        out.Clear();
        const Status s = step->run(in, out, limits);
        if (s.Failed())
        {
            return s;
        }
        if (!in.AtEnd())
        {
            return Status::Make(ErrorCode::Corrupt, "an upgrade step left bytes unread");
        }
        if (out.Size() > limits.max_record_bytes)
        {
            return Status::Make(ErrorCode::LimitExceeded, "an upgrade step produced more than the record limit");
        }
        current = out.Bytes();
        next ^= 1;
    }
    bytes::ByteReader in(current);
    const Status s = Decode(in, component, limits);
    if (s.Failed())
    {
        return s;
    }
    if (!in.AtEnd())
    {
        return Status::Make(ErrorCode::Corrupt, "the record has bytes left after its last field");
    }
    return Status::Success();
}

Status ComponentRegistry::RegisterEntry(const ComponentDesc& desc, ecs::ComponentId ecs_id, bool is_tag)
{
    if (desc.id == 0)
    {
        return Status::Make(ErrorCode::Invalid, "a component id must not be 0");
    }
    if (desc.name == nullptr || desc.name[0] == '\0')
    {
        return Status::Make(ErrorCode::Invalid, "a component needs a name");
    }
    for (const char* c = desc.name; *c != '\0'; ++c)
    {
        if (static_cast<u8>(*c) < 0x20)
        {  // the manifest is one line per component
            return Status::Make(ErrorCode::Invalid, "a component name must not hold a control character");
        }
    }
    if (desc.version == 0)
    {
        return Status::Make(ErrorCode::Invalid, "a component version starts at 1");
    }
    const bool stored = (desc.flags & (kComponentDerived | kComponentSkip)) == 0;
    const usize field_count = desc.fields.size();
    const bool has_custom = desc.custom.save != nullptr || desc.custom.load != nullptr;
    if (has_custom && (desc.custom.save == nullptr || desc.custom.load == nullptr))
    {
        return Status::Make(ErrorCode::Invalid, "a custom codec needs both save and load");
    }
    if (has_custom && (field_count != 0 || !stored || is_tag))
    {
        return Status::Make(ErrorCode::Invalid,
                            "a custom codec belongs to a stored component with data and no field list");
    }
    if (desc.oldest_version == 0 || desc.oldest_version > desc.version)
    {
        return Status::Make(ErrorCode::Invalid, "the oldest version must be between 1 and the version");
    }
    if (desc.upgrades.size() != desc.version - desc.oldest_version)
    {
        return Status::Make(
            ErrorCode::Invalid,
            "the upgrade chain needs exactly one step for each version from the oldest up to the current one");
    }
    for (const UpgradeStep& step : desc.upgrades)
    {
        if (step.run == nullptr || step.from_version < desc.oldest_version || step.from_version >= desc.version)
        {
            return Status::Make(ErrorCode::Invalid, "an upgrade step has no function or a version outside the chain");
        }
        usize same = 0;
        for (const UpgradeStep& other : desc.upgrades)
        {
            same += other.from_version == step.from_version ? 1 : 0;
        }
        if (same != 1)
        {
            return Status::Make(ErrorCode::Invalid, "two upgrade steps read one version (the chain has a gap)");
        }
    }
    if (!stored && field_count != 0)
    {
        return Status::Make(ErrorCode::Invalid, "a derived or skipped component has no fields");
    }
    if (stored && !is_tag && field_count == 0 && !has_custom)
    {
        return Status::Make(ErrorCode::Invalid,
                            "a stored component with data needs fields (or flag it derived or skip)");
    }
    if (is_tag && field_count != 0)
    {
        return Status::Make(ErrorCode::Invalid, "a tag has no fields");
    }
    usize index = 0;
    for (const FieldDesc& f : desc.fields)
    {
        if (f.name == nullptr || f.name[0] == '\0' || f.encode == nullptr || f.decode == nullptr)
        {
            return Status::Make(ErrorCode::Invalid,
                                "a field needs a name and codec functions (make it with storage::Field)");
        }
        if (f.needs_range && !f.options.has_range)
        {
            return Status::Make(ErrorCode::Invalid, "an enum field needs a declared range (FieldOptions::Enum)");
        }
        if (f.options.is_reference && f.kind != FieldKind::ResourceRef)
        {
            return Status::Make(ErrorCode::Invalid,
                                "FieldOptions::Reference applies only to a u64 or a ResourceRef member");
        }
        if (f.kind == FieldKind::Array && f.element_kind == FieldKind::ResourceRef)
        {
            return Status::Make(ErrorCode::Invalid, "an array of resource references is not supported");
        }
        usize other = 0;
        for (const FieldDesc& g : desc.fields)
        {
            if (other < index && CStrCompare(f.name, g.name) == 0)
            {
                return Status::Make(ErrorCode::Invalid, "two fields have one name");
            }
            ++other;
        }
        ++index;
    }
    for (usize i = 0; i < components_.Size(); ++i)
    {
        const RegisteredComponent& c = *components_[i];
        if (c.id_ == desc.id)
        {
            return Status::Make(ErrorCode::Exists, "the component id is registered already");
        }
        if (CStrCompare(c.name_, desc.name) == 0)
        {
            return Status::Make(ErrorCode::Exists, "the component name is registered already");
        }
        if (ecs_id != ecs::kInvalidComponent && c.ecs_id_ == ecs_id)
        {
            return Status::Make(ErrorCode::Exists, "the component type is registered already");
        }
    }
    UniquePtr<RegisteredComponent> entry = MakeUnique<RegisteredComponent>();
    entry->id_ = desc.id;
    entry->name_ = desc.name;
    entry->version_ = desc.version;
    entry->oldest_version_ = desc.oldest_version;
    entry->custom_ = desc.custom;
    for (const UpgradeStep& step : desc.upgrades)
    {
        entry->upgrades_.Push(step);
    }
    entry->flags_ = desc.flags;
    entry->ecs_id_ = ecs_id;
    entry->is_tag_ = is_tag;
    for (const FieldDesc& f : desc.fields)
    {
        entry->fields_.Push(f);
    }
    components_.Push(Move(entry));
    return Status::Success();
}

Status ComponentRegistry::Resolve(u32 id, UnknownPolicy policy, const RegisteredComponent** component,
                                  RecordAction* action) const
{
    const RegisteredComponent* found = FindById(id);
    *component = nullptr;
    *action = RecordAction::Skip;  // defined on every path, including a failure
    if (found != nullptr)
    {
        if (found->IsStored())
        {
            *component = found;
            *action = RecordAction::Decode;
        }
        else
        {
            *action = RecordAction::Skip;  // a derived or skipped component is rebuilt or ignored, whatever an old file
                                           // holds
        }
        return Status::Success();
    }
    switch (policy)
    {
    case UnknownPolicy::Fail:
        return Status::Make(ErrorCode::NotFound, "a record names a component that is not registered");
    case UnknownPolicy::SkipAndReport: *action = RecordAction::Skip; return Status::Success();
    case UnknownPolicy::KeepOpaque: *action = RecordAction::KeepOpaque; return Status::Success();
    }
    return Status::Make(ErrorCode::Invalid, "an unknown policy");
}

const RegisteredComponent* ComponentRegistry::FindById(u32 id) const
{
    for (usize i = 0; i < components_.Size(); ++i)
    {
        if (components_[i]->id_ == id)
        {
            return components_[i].Get();
        }
    }
    return nullptr;
}

const RegisteredComponent* ComponentRegistry::FindByName(StringView name) const
{
    for (usize i = 0; i < components_.Size(); ++i)
    {
        if (StringView(components_[i]->name_, CStrLength(components_[i]->name_)) == name)
        {
            return components_[i].Get();
        }
    }
    return nullptr;
}

const RegisteredComponent* ComponentRegistry::FindByEcsId(ecs::ComponentId id) const
{
    if (id == ecs::kInvalidComponent)
    {
        return nullptr;
    }
    for (usize i = 0; i < components_.Size(); ++i)
    {
        if (components_[i]->ecs_id_ == id)
        {
            return components_[i].Get();
        }
    }
    return nullptr;
}

const RegisteredComponent& ComponentRegistry::At(u32 index) const
{
    ENGINE_ASSERT(index < components_.Size(), "ComponentRegistry::At: the index is out of range");
    return *components_[index];
}

void ComponentRegistry::Manifest(Vector<ManifestEntry>* out) const
{
    for (usize i = 0; i < components_.Size(); ++i)
    {
        const RegisteredComponent& c = *components_[i];
        ManifestEntry e;
        e.id = c.id_;
        e.name = StringView(c.name_, CStrLength(c.name_));
        e.version = c.version_;
        out->Push(e);
    }
}

Status ComponentRegistry::CheckManifest(Span<const ManifestEntry> recorded) const
{
    for (usize i = 0; i < recorded.Size(); ++i)
    {
        const ManifestEntry& r = recorded[i];
        const RegisteredComponent* by_id = FindById(r.id);
        const RegisteredComponent* by_name = FindByName(r.name);
        if (by_id != nullptr && StringView(by_id->name_, CStrLength(by_id->name_)) != r.name)
        {
            return Status::Make(ErrorCode::Mismatch, "a recorded component id now has another name");
        }
        if (by_name != nullptr && by_name->id_ != r.id)
        {
            return Status::Make(ErrorCode::Mismatch, "a recorded component name now has another id");
        }
        if (by_id != nullptr && r.version > by_id->version_)
        {
            return Status::Make(ErrorCode::TooNew, "a component was recorded with a newer version than is registered");
        }
    }
    return Status::Success();
}

void ComponentRegistry::UnregisteredWithData(const ecs::World& world, Vector<ecs::ComponentId>* out) const
{
    ecs::ComponentSet seen;
    for (u32 a = 0; a < world.ArchetypeCount(); ++a)
    {
        const ecs::Archetype& archetype = world.ArchetypeAt(a);
        if (archetype.EntityCount() == 0)
        {
            continue;
        }
        const ecs::ComponentSet& set = archetype.Components();
        for (ecs::ComponentId id = set.NextFrom(0); id != ecs::kInvalidComponent;
             id = set.NextFrom(static_cast<u32>(id) + 1))
        {
            if (seen.Has(id))
            {
                continue;
            }
            seen.Add(id);
            if (FindByEcsId(id) == nullptr)
            {
                out->Push(id);
            }
        }
    }
}

}  // namespace engine_placeholder::storage
