#include "asset/level.h"

#include "asset/content_hash.h"
#include "bytes/byte_reader.h"
#include "bytes/byte_writer.h"
#include "containers/array.h"
#include "cstr/mem_ops.h"
#include "lang/status_try.h"
#include "storage/builtin_components.h"
#include "storage/component_record.h"
#include "storage/field_walk.h"
#include "storage/memory_store.h"
#include "storage/world_storage.h"

namespace engine_placeholder::asset
{
namespace
{

constexpr u32 kLevelSectionHeader = 16;
constexpr u32 kLevelSectionEntities = 17;
constexpr u32 kLevelSectionContainers = 18;
constexpr u32 kLevelSectionReferences = 19;
constexpr usize kLevelHeaderBytes = 64;
constexpr usize kLevelEntityEntryBytes = 32;
constexpr usize kLevelReferenceEntryBytes = 24;

static_assert(storage::ResourceTag('M', 'E', 'S', 'H') == types::kMesh.Tag(),
              "the storage tag packing is the asset one");

// The status of a failure that is the blob's fault when reading it and the caller's when building it.
Status Bad(bool parsing, const char* message)
{
    return Status::Make(parsing ? ErrorCode::Corrupt : ErrorCode::Invalid, message);
}

// A storage status as an asset status of the nearest meaning, keeping its message.
Status FromStorage(const storage::Status& s)
{
    switch (s.Code())
    {
    case storage::ErrorCode::Ok: return Status::Success();
    case storage::ErrorCode::NotFound: return Status::Make(ErrorCode::NotFound, s.Message());
    case storage::ErrorCode::Corrupt:
    case storage::ErrorCode::Duplicate:
    case storage::ErrorCode::MissingReference: return Status::Make(ErrorCode::Corrupt, s.Message());
    case storage::ErrorCode::TooNew:
    case storage::ErrorCode::TooOld: return Status::Make(ErrorCode::UnsupportedVersion, s.Message());
    case storage::ErrorCode::LimitExceeded: return Status::Make(ErrorCode::LimitExceeded, s.Message());
    case storage::ErrorCode::Cancelled: return Status::Make(ErrorCode::Cancelled, s.Message());
    case storage::ErrorCode::Exists:
    case storage::ErrorCode::Mismatch:
    case storage::ErrorCode::Invalid:
    case storage::ErrorCode::Busy:
    case storage::ErrorCode::Io:
    case storage::ErrorCode::Unsupported:
    case storage::ErrorCode::Other: break;
    }
    return Status::Make(ErrorCode::Invalid, s.Message());
}

bool NameChar(char c)
{
    return (c >= '0' && c <= '9') || (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || c == '_' || c == '.' ||
           c == ':' || c == '/' || c == '-';
}

// The framed records of one entity container, checked against the registry; the references found are appended.
Status AnalyzeEntity(const storage::ComponentRegistry& registry, const storage::CodecLimits& limits, bool parsing,
                     u32 entity_index, const storage::EntityRecord& entity, u32* record_count,
                     Vector<LevelReference>* references)
{
    bytes::ByteReader in(ByteSpan(entity.blob.Data(), entity.blob.Size()));
    u32 count = 0;
    if (in.ReadU32(&count) != bytes::ByteError::Ok)
    {
        return Bad(parsing, "an entity container is truncated");
    }
    if (count > limits.max_components_per_entity)
    {
        return Status::Make(ErrorCode::LimitExceeded, "an entity has more components than the limit");
    }
    Vector<u32> seen;
    Vector<storage::ReferenceValue> found;
    for (u32 i = 0; i < count; ++i)
    {
        storage::ComponentRecord record;
        const storage::Status read = storage::ReadComponentRecord(in, limits, &record);
        if (read.Failed())
        {
            return parsing ? FromStorage(read) : Status::Make(ErrorCode::Invalid, read.Message());
        }
        const storage::RegisteredComponent* component = nullptr;
        storage::RecordAction action = storage::RecordAction::Decode;
        const storage::Status resolved = registry.Resolve(record.id, storage::UnknownPolicy::Fail, &component, &action);
        if (resolved.Failed())
        {
            return Status::Make(parsing ? ErrorCode::NotFound : ErrorCode::Invalid,
                                "a record names a component the registry does not know");
        }
        if (action != storage::RecordAction::Decode || component == nullptr)
        {
            return Bad(parsing, "a level holds a record of a component that is not stored");
        }
        if (record.id == storage::kEntityKeyId)
        {
            return Bad(parsing, "an entity's key is in the entity table, not in a record");
        }
        if (record.version != component->Version())
        {
            return Status::Make(
                ErrorCode::UnsupportedVersion,
                "a component was stored at another version; re-mint the level from its source with asset_mint mint-level");
        }
        for (usize s = 0; s < seen.Size(); ++s)
        {
            if (seen[s] == record.id)
            {
                return Bad(parsing, "an entity holds one component twice");
            }
        }
        seen.Push(record.id);
        found.Clear();
        const storage::Status walked = storage::ReadReferences(*component, record.bytes, limits, &found);
        if (walked.Failed())
        {
            return parsing ? FromStorage(walked) : Status::Make(ErrorCode::Invalid, walked.Message());
        }
        for (usize r = 0; r < found.Size(); ++r)
        {
            if (found[r].id == 0)
            {
                if (!found[r].field->options.nullable)
                {
                    return Bad(parsing, "a reference is null in a field that is not nullable");
                }
                continue;
            }
            LevelReference reference;
            reference.entity = entity_index;
            reference.component_id = record.id;
            reference.field = found[r].field_index;
            reference.type_tag = found[r].field->options.ref_type;
            reference.id = ResourceId::FromValue(found[r].id);
            references->Push(reference);
        }
    }
    if (in.Remaining() != 0)
    {
        return Bad(parsing, "an entity container has bytes after its last record");
    }
    *record_count = count;
    return Status::Success();
}

Status AnalyzeEntities(const storage::ComponentRegistry& registry, const storage::CodecLimits& limits, bool parsing,
                       const Vector<storage::EntityRecord>& entities, Vector<LevelReference>* references,
                       u32* total_records, Vector<u32>* counts = nullptr)
{
    if (registry.FindById(storage::kEntityKeyId) == nullptr || registry.FindById(storage::kParentLinkId) == nullptr)
    {
        return Status::Make(ErrorCode::Invalid, "the registry lacks the builtin components (entity_key, parent_link)");
    }
    if (entities.Size() > kMaxLevelEntities)
    {
        return Status::Make(ErrorCode::LimitExceeded, "the level has more entities than the limit");
    }
    *total_records = 0;
    for (usize i = 0; i < entities.Size(); ++i)
    {
        if (entities[i].key.IsNull())
        {
            return Bad(parsing, "an entity has the null key");
        }
        if (i != 0 && !(entities[i - 1].key < entities[i].key))
        {
            return Bad(parsing, "the entities are not in strictly increasing key order");
        }
        u32 records = 0;
        ENGINE_TRY(AnalyzeEntity(registry, limits, parsing, static_cast<u32>(i), entities[i], &records, references));
        *total_records += records;
        if (counts != nullptr)
        {
            counts->Push(records);
        }
    }
    return Status::Success();
}

const LevelResource* FindResource(const Vector<LevelResource>& resources, ResourceId id)
{
    usize low = 0;
    usize high = resources.Size();
    while (low < high)
    {
        const usize mid = low + (high - low) / 2;
        if (resources[mid].id == id)
        {
            return &resources[mid];
        }
        if (resources[mid].id < id)
        {
            low = mid + 1;
        }
        else
        {
            high = mid;
        }
    }
    return nullptr;
}

// The references against the resources: each is listed with the right type, each listed resource is used.
Status CheckResources(bool parsing, ResourceId self, const TypeRegistry& types, const LevelData& data)
{
    Vector<u8> used;
    used.Resize(data.resources.Size());
    for (usize i = 0; i < data.resources.Size(); ++i)
    {
        const LevelResource& r = data.resources[i];
        if (r.id.IsNone() || r.type.Tag() == 0 || r.type_version == 0)
        {
            return Bad(parsing, "a listed resource has no id, type or version");
        }
        if (i != 0 && !(data.resources[i - 1].id < r.id))
        {
            return Bad(parsing, "the resources are not in strictly increasing id order");
        }
        if (r.id == self)
        {
            return Bad(parsing, "a level cannot name itself");
        }
        if (r.roles == 0 || (r.roles & ~(kLevelRoleReference | kLevelRoleInclude)) != 0)
        {
            return Bad(parsing, "a listed resource has no valid role");
        }
        TypeInfo info;
        if (!types.Find(r.type, &info))
        {
            return Status::Make(ErrorCode::TypeMismatch, "a listed resource has a type that is not registered");
        }
        if ((r.roles & kLevelRoleInclude) != 0 && r.type != types::kLevel)
        {
            return Bad(parsing, "an included resource is not a level");
        }
    }
    for (usize i = 0; i < data.references.Size(); ++i)
    {
        const LevelReference& ref = data.references[i];
        const LevelResource* resource = FindResource(data.resources, ref.id);
        if (resource == nullptr || (resource->roles & kLevelRoleReference) == 0)
        {
            return Bad(parsing, "a reference names a resource that the level does not list");
        }
        if (ref.type_tag != 0 && resource->type.Tag() != ref.type_tag)
        {
            return Status::Make(parsing ? ErrorCode::Corrupt : ErrorCode::DependencyTypeMismatch,
                                "a reference names a resource of another type than its field declares");
        }
        used[static_cast<usize>(resource - data.resources.Data())] = 1;
    }
    for (usize i = 0; i < data.resources.Size(); ++i)
    {
        if ((data.resources[i].roles & kLevelRoleReference) != 0 && used[i] == 0)
        {
            return Bad(parsing, "a listed resource is named by no component field");
        }
    }
    return Status::Success();
}

}  // namespace

storage::Status RegisterLevelComponents(storage::ComponentRegistry* registry)
{
    ENGINE_TRY(registry->Register<MeshReference>(
        {.id = kMeshReferenceId,
         .name = "mesh_reference",
         .fields = {storage::Field<&MeshReference::mesh>(
                        "mesh", storage::FieldOptions::Reference(storage::ResourceTag('M', 'E', 'S', 'H'), false)),
                    storage::Field<&MeshReference::material>(
                        "material", storage::FieldOptions::Reference(storage::ResourceTag('M', 'A', 'T', 'L'), true)),
                    storage::Field<&MeshReference::flags>("flags")}}));
    ENGINE_TRY(registry->Register<EntityName>(
        {.id = kEntityNameId,
         .name = "entity_name",
         .fields = {storage::Field<&EntityName::name>("name", storage::FieldOptions::Text(kMaxLevelNameBytes))}}));
    return storage::Status::Success();
}

bool LevelNameValid(StringView name)
{
    if (name.Length() == 0 || name.Length() > kMaxLevelNameBytes || name == StringView("key"))
    {
        return false;
    }
    for (usize i = 0; i < name.Length(); ++i)
    {
        if (!NameChar(name[i]))
        {
            return false;
        }
    }
    return true;
}

storage::EntityKey LevelEntityKey(ResourceId level, StringView name)
{
    Vector<byte> bytes;
    for (u32 i = 0; i < 8; ++i)
    {
        bytes.Push(static_cast<byte>(level.Value() >> (8 * i)));
    }
    for (usize i = 0; i < name.Length(); ++i)
    {
        bytes.Push(static_cast<byte>(name[i]));
    }
    const ContentHash hash = HashBytes(ByteSpan(bytes.Data(), bytes.Size()));
    storage::EntityKey key;
    for (u32 i = 0; i < 8; ++i)
    {
        key.high |= static_cast<u64>(hash.bytes[i]) << (8 * i);
        key.low |= static_cast<u64>(hash.bytes[8 + i]) << (8 * i);
    }
    if (key.IsNull())
    {
        key.low = 1;  // a hash of exactly zero is as likely as any other value; the null key is not a key
    }
    return key;
}

Status BuildLevelBlob(const TypeRegistry& types, const storage::ComponentRegistry& registry, ResourceId id,
                      const LevelData& data, Vector<byte>* out, u32 target, const storage::CodecLimits& limits)
{
    if (id.IsNone())
    {
        return Status::Make(ErrorCode::Invalid, "a level needs a non-null id");
    }
    LevelData checked;
    u32 total_records = 0;
    ENGINE_TRY(AnalyzeEntities(registry, limits, false, data.entities, &checked.references, &total_records));
    for (usize i = 0; i < data.resources.Size(); ++i)
    {
        checked.resources.Push(data.resources[i]);
    }
    ENGINE_TRY(CheckResources(false, id, types, checked));

    BlobWriter writer(types, types::kLevel, id, target);
    u32 includes = 0;
    for (usize i = 0; i < data.resources.Size(); ++i)
    {
        const LevelResource& r = data.resources[i];
        Dependency dependency;
        dependency.id = r.id;
        dependency.type = r.type;
        dependency.type_version = r.type_version;
        dependency.role = r.roles;
        writer.AddDependency(dependency);
        includes += (r.roles & kLevelRoleInclude) != 0 ? 1u : 0u;
    }

    bytes::ByteWriter header;
    header.WriteU32(static_cast<u32>(data.entities.Size()));
    header.WriteU32(total_records);
    header.WriteU32(static_cast<u32>(checked.references.Size()));
    header.WriteU32(includes);
    while (header.Size() < kLevelHeaderBytes)
    {
        header.WriteU8(0);
    }

    bytes::ByteWriter table;
    bytes::ByteWriter containers;
    for (usize i = 0; i < data.entities.Size(); ++i)
    {
        const storage::EntityRecord& e = data.entities[i];
        u32 count = 0;
        bytes::ByteReader peek(ByteSpan(e.blob.Data(), e.blob.Size()));
        static_cast<void>(peek.ReadU32(&count));  // AnalyzeEntities read it already
        table.WriteU64(e.key.high);
        table.WriteU64(e.key.low);
        table.WriteU64(containers.Size());
        table.WriteU32(static_cast<u32>(e.blob.Size()));
        table.WriteU32(count);
        containers.WriteBytes(ByteSpan(e.blob.Data(), e.blob.Size()));
    }

    bytes::ByteWriter refs;
    for (usize i = 0; i < checked.references.Size(); ++i)
    {
        const LevelReference& r = checked.references[i];
        refs.WriteU32(r.entity);
        refs.WriteU32(r.component_id);
        refs.WriteU32(r.field);
        refs.WriteU32(r.type_tag);
        refs.WriteU64(r.id.Value());
    }

    ENGINE_TRY(writer.AddSection(kLevelSectionHeader, header.Bytes()));
    ENGINE_TRY(writer.AddSection(kLevelSectionEntities, table.Bytes()));
    ENGINE_TRY(writer.AddSection(kLevelSectionContainers, containers.Bytes()));
    ENGINE_TRY(writer.AddSection(kLevelSectionReferences, refs.Bytes()));
    return writer.Finish(out);
}

Status ReadLevelSummary(ByteSpan bytes, const TypeRegistry& types, LevelSummary* out)
{
    BlobReader reader;
    ENGINE_TRY(reader.Open(bytes, types));
    if (reader.Type() != types::kLevel)
    {
        return Status::Make(ErrorCode::TypeMismatch, "the blob is not a level");
    }
    ByteSpan header;
    ByteSpan table;
    ByteSpan containers;
    ByteSpan refs;
    if (!reader.FindSection(kLevelSectionHeader, &header) || !reader.FindSection(kLevelSectionEntities, &table) ||
        !reader.FindSection(kLevelSectionContainers, &containers) ||
        !reader.FindSection(kLevelSectionReferences, &refs) || header.Size() != kLevelHeaderBytes)
    {
        return Status::Make(ErrorCode::Corrupt, "the level lacks one of its sections");
    }
    bytes::ByteReader h(header);
    LevelSummary summary;
    u32 include_count = 0;
    if (h.ReadU32(&summary.entity_count) != bytes::ByteError::Ok ||
        h.ReadU32(&summary.record_count) != bytes::ByteError::Ok ||
        h.ReadU32(&summary.reference_count) != bytes::ByteError::Ok ||
        h.ReadU32(&include_count) != bytes::ByteError::Ok)
    {
        return Status::Make(ErrorCode::Corrupt, "the level's header is truncated");
    }
    if (summary.record_count >
        static_cast<u64>(summary.entity_count) * storage::CodecLimits().max_components_per_entity)
    {
        return Status::Make(ErrorCode::Corrupt, "the level's record count is more than its entities can hold");
    }
    if (summary.entity_count > kMaxLevelEntities ||
        table.Size() != static_cast<u64>(summary.entity_count) * kLevelEntityEntryBytes ||
        refs.Size() != static_cast<u64>(summary.reference_count) * kLevelReferenceEntryBytes)
    {
        return Status::Make(ErrorCode::Corrupt, "a level section does not have the size its counts give");
    }
    // The entity table must tile the containers section: its last entry ends where the section ends.
    u64 expected_end = 0;
    if (summary.entity_count != 0)
    {
        bytes::ByteReader last(
            ByteSpan(table.Data() + (summary.entity_count - 1) * kLevelEntityEntryBytes, kLevelEntityEntryBytes));
        u64 key_high = 0;
        u64 key_low = 0;
        u64 offset = 0;
        u32 size = 0;
        if (last.ReadU64(&key_high) != bytes::ByteError::Ok || last.ReadU64(&key_low) != bytes::ByteError::Ok ||
            last.ReadU64(&offset) != bytes::ByteError::Ok || last.ReadU32(&size) != bytes::ByteError::Ok)
        {
            return Status::Make(ErrorCode::Corrupt, "the level's entity table is truncated");
        }
        expected_end = offset + size;
    }
    if (expected_end != containers.Size())
    {
        return Status::Make(ErrorCode::Corrupt, "the entity containers do not fill their section");
    }
    u32 includes = 0;
    for (usize i = 0; i < reader.DependencyCount(); ++i)
    {
        const Dependency d = reader.DependencyAt(i);
        LevelResource resource;
        resource.id = d.id;
        resource.type = d.type;
        resource.type_version = d.type_version;
        resource.roles = d.role;
        summary.resources.Push(resource);
        includes += (d.role & kLevelRoleInclude) != 0 ? 1u : 0u;
    }
    if (includes != include_count)
    {
        return Status::Make(ErrorCode::Corrupt, "the include count disagrees with the dependency list");
    }
    summary.bytes = bytes;
    *out = static_cast<LevelSummary&&>(summary);
    return Status::Success();
}

Status ParseLevelBlob(ByteSpan bytes, const TypeRegistry& types, const storage::ComponentRegistry& registry,
                      LevelData* out, ResourceId* id_out, const storage::CodecLimits& limits)
{
    BlobReader reader;
    ENGINE_TRY(reader.Open(bytes, types));
    if (reader.Type() != types::kLevel)
    {
        return Status::Make(ErrorCode::TypeMismatch, "the blob is not a level");
    }
    ByteSpan header;
    ByteSpan table;
    ByteSpan containers;
    ByteSpan refs;
    if (!reader.FindSection(kLevelSectionHeader, &header) || !reader.FindSection(kLevelSectionEntities, &table) ||
        !reader.FindSection(kLevelSectionContainers, &containers) ||
        !reader.FindSection(kLevelSectionReferences, &refs))
    {
        return Status::Make(ErrorCode::Corrupt, "the level lacks one of its sections");
    }
    if (header.Size() != kLevelHeaderBytes || reader.SectionCount() != 5)
    {
        return Status::Make(ErrorCode::Corrupt, "the level's header section has the wrong size");
    }
    bytes::ByteReader h(header);
    u32 entity_count = 0;
    u32 record_count = 0;
    u32 reference_count = 0;
    u32 include_count = 0;
    static_cast<void>(h.ReadU32(&entity_count));
    static_cast<void>(h.ReadU32(&record_count));
    static_cast<void>(h.ReadU32(&reference_count));
    static_cast<void>(h.ReadU32(&include_count));
    for (usize i = 16; i < kLevelHeaderBytes; ++i)
    {
        if (header[i] != 0)
        {
            return Status::Make(ErrorCode::Corrupt, "the level's header has reserved bytes set");
        }
    }
    if (entity_count > kMaxLevelEntities)
    {
        return Status::Make(ErrorCode::LimitExceeded, "the level has more entities than the limit");
    }
    if (table.Size() != static_cast<u64>(entity_count) * kLevelEntityEntryBytes ||
        refs.Size() != static_cast<u64>(reference_count) * kLevelReferenceEntryBytes)
    {
        return Status::Make(ErrorCode::Corrupt, "a level section does not have the size its counts give");
    }

    LevelData data;
    data.entities.Reserve(entity_count);
    bytes::ByteReader t(table);
    u64 expected_offset = 0;
    u64 table_records = 0;
    Vector<u32> table_counts;
    for (u32 i = 0; i < entity_count; ++i)
    {
        storage::EntityKey key;
        u64 offset = 0;
        u32 size = 0;
        u32 count = 0;
        static_cast<void>(t.ReadU64(&key.high));
        static_cast<void>(t.ReadU64(&key.low));
        static_cast<void>(t.ReadU64(&offset));
        static_cast<void>(t.ReadU32(&size));
        static_cast<void>(t.ReadU32(&count));
        if (offset != expected_offset || size > containers.Size() || offset > containers.Size() - size)
        {
            return Status::Make(ErrorCode::Corrupt, "an entity container lies outside its section or out of order");
        }
        expected_offset += size;
        table_records += count;
        table_counts.Push(count);
        data.entities.Push(storage::EntityRecord::Make(key, ByteSpan(containers.Data() + offset, size)));
    }
    if (expected_offset != containers.Size())
    {
        return Status::Make(ErrorCode::Corrupt, "the entity containers do not fill their section");
    }
    u32 total_records = 0;
    Vector<u32> found_counts;
    ENGINE_TRY(AnalyzeEntities(registry, limits, true, data.entities, &data.references, &total_records, &found_counts));
    for (usize i = 0; i < found_counts.Size(); ++i)
    {
        if (found_counts[i] != table_counts[i])
        {
            return Status::Make(ErrorCode::Corrupt,
                                "an entity's record count in the table is not the count in its container");
        }
    }
    if (total_records != record_count || table_records != record_count)
    {
        return Status::Make(ErrorCode::Corrupt, "the record counts of the level disagree");
    }

    if (data.references.Size() != reference_count)
    {
        return Status::Make(ErrorCode::Corrupt, "the reference table does not list the references the records hold");
    }
    bytes::ByteReader r(refs);
    for (u32 i = 0; i < reference_count; ++i)
    {
        LevelReference stored;
        u64 value = 0;
        static_cast<void>(r.ReadU32(&stored.entity));
        static_cast<void>(r.ReadU32(&stored.component_id));
        static_cast<void>(r.ReadU32(&stored.field));
        static_cast<void>(r.ReadU32(&stored.type_tag));
        static_cast<void>(r.ReadU64(&value));
        const LevelReference& found = data.references[i];
        if (stored.entity != found.entity || stored.component_id != found.component_id || stored.field != found.field ||
            stored.type_tag != found.type_tag || value != found.id.Value())
        {
            return Status::Make(ErrorCode::Corrupt, "the reference table disagrees with the records");
        }
    }

    u32 includes = 0;
    for (usize i = 0; i < reader.DependencyCount(); ++i)
    {
        const Dependency d = reader.DependencyAt(i);
        LevelResource resource;
        resource.id = d.id;
        resource.type = d.type;
        resource.type_version = d.type_version;
        resource.roles = d.role;
        data.resources.Push(resource);
        includes += (d.role & kLevelRoleInclude) != 0 ? 1u : 0u;
    }
    if (includes != include_count)
    {
        return Status::Make(ErrorCode::Corrupt, "the include count disagrees with the dependency list");
    }
    ENGINE_TRY(CheckResources(true, reader.Id(), types, data));
    if (id_out != nullptr)
    {
        *id_out = reader.Id();
    }
    *out = static_cast<LevelData&&>(data);
    return Status::Success();
}

Status LoadLevelBlob(ByteSpan bytes, const TypeRegistry& types, const storage::ComponentRegistry& registry,
                     ecs::World& world, storage::KeyIndex& index, LevelData* data, const storage::CodecLimits& limits)
{
    LevelData parsed;
    ENGINE_TRY(ParseLevelBlob(bytes, types, registry, &parsed, nullptr, limits));
    storage::MemoryStore store;
    for (usize i = 0; i < parsed.entities.Size(); ++i)
    {
        ENGINE_TRY(FromStorage(store.Put(parsed.entities[i])));
    }
    storage::WorldStorage storage(registry, store);
    storage.SetLimits(limits);
    storage::LoadOptions options;
    options.mode = storage::LoadMode::Create;
    options.unknown = storage::UnknownPolicy::Fail;
    options.missing = storage::MissingPolicy::Fail;
    ENGINE_TRY(FromStorage(storage.Load(world, index, options, nullptr)));
    if (data != nullptr)
    {
        *data = static_cast<LevelData&&>(parsed);
    }
    return Status::Success();
}

}  // namespace engine_placeholder::asset
