#pragma once

//! The level blob (type `LEVL`, version 2: entity positions are double precision): entities as the storage module's
//! records, the table of every resource they name, and the list of levels it includes. Built by
//! [[asset::BuildLevelBlob]], read by
//! [[asset::ParseLevelBlob]] and put into a world by [[asset::LoadLevelBlob]].
//!
//! A level is the same data a save is: one [[storage::EntityRecord]] per entity (the entity container of
//! `storage::WorldStorage`: a count, then framed component records encoded by the [[storage::ComponentRegistry]]), so
//! there is one set of field codecs for saves and levels. What a level adds is what a save has no use for: the
//! references. Every field declared with [[storage::FieldOptions::Reference]] (or typed [[storage::ResourceRef]]) names
//! a resource by id; the blob lists each such use in a table and each distinct resource as a dependency, so the manager
//! can load the closure before a single entity exists.
//!
//! Sections (ids from [[asset::kFirstUserSection]]): 16 the header (64 bytes: the entity, record, reference and include
//! counts), 17 the entity table (32 bytes per entity: key high, key low, offset into section 18, size, record count;
//! keys strictly increasing, the offsets tiling section 18), 18 the entity containers one after another, 19 the
//! reference table (24 bytes each: entity index, component id, field index, type tag, id; in entity, record and field
//! order; null references are not listed). The standard dependency section lists every distinct referenced resource and
//! every included level (role bits [[asset::kLevelRoleReference]] and [[asset::kLevelRoleInclude]]).
//!
//! A stored component is read only at its registered version; a level written with an older one is refused with
//! `UnsupportedVersion` and fixed by re-minting from its source, never converted.

#include "asset/blob.h"
#include "asset/resource_id.h"
#include "asset/resource_type.h"
#include "asset/status.h"
#include "containers/span.h"
#include "containers/vector.h"
#include "ecs/world.h"
#include "math/vector.h"
#include "mem/heap_allocator.h"
#include "storage/component_registry.h"
#include "storage/entity_ref.h"
#include "storage/identity.h"
#include "storage/store.h"
#include "string/string.h"
#include "types.h"

namespace vse::asset
{

/// Stable ids of the level components in the storage registry (the engine's own range, below 1000).
enum LevelComponentIds : u32
{
    kMeshReferenceId = 50,      ///< [[asset::MeshReference]]: stored.
    kEntityNameId = 51,         ///< [[asset::EntityName]]: stored.
    kMeshSlotMaterialsId = 52,  ///< [[asset::MeshSlotMaterials]]: stored.
    kStaticColliderId = 53,     ///< [[asset::StaticCollider]]: stored.
};

/// The shape of a [[asset::StaticCollider]]; the values are stored.
enum class StaticColliderShape : u8
{
    Mesh = 0,     ///< A triangle mesh: the `MCOL` named by `mesh`.
    Box = 1,      ///< A box: `half_extents`.
    Sphere = 2,   ///< A ball: `radius`.
    Capsule = 3,  ///< A capsule along Y: `radius` and `half_height` (of the straight part).
};

/// The stored collision of a level entity: one static collider at the entity, sized in the
/// entity's own space. The sizes are unscaled: whoever makes the physics body applies the entity's world scale when the
/// body is made (the C API's `level_spawn`). A mesh shape names an `MCOL` resource, so the
/// level's closure (its reference table) includes it.
///
/// - Thread-safety: thread-compatible plain data.
struct StaticCollider
{
    StaticColliderShape shape = StaticColliderShape::Mesh;  ///< Which shape.
    storage::ResourceRef mesh;                              ///< The `MCOL` of a mesh shape; unset for the others.
    Vec3 half_extents = Vec3(0.0f, 0.0f, 0.0f);             ///< A box's half sizes, unscaled.
    f32 radius = 0.0f;                                      ///< A sphere's or a capsule's radius, unscaled.
    f32 half_height = 0.0f;                                 ///< Half the straight part of a capsule, along Y, unscaled.
    f32 friction = 0.5f;                                    ///< The surface's friction, at least 0 (physics' default).
    f32 restitution = 0.0f;                                 ///< The surface's bounciness, 0 to 1 (physics' default).
};

/// The most mesh slots a level gives a material of their own.
inline constexpr u32 kMaxLevelSlotMaterials = 16;

/// The stored form of what draws an entity: a mesh and a material named by id. The level spawner turns it
/// into a `scene::MeshRenderer` with the loaded geometry; the level never holds a pointer.
///
/// - Thread-safety: thread-compatible plain data.
struct MeshReference
{
    storage::ResourceRef mesh;      ///< The mesh (`MESH`); required.
    storage::ResourceRef material;  ///< The material (`MATL`); 0 for the renderer's default.
    u32 flags = 0;                  ///< Extra `renderer::ObjectFlags` bits.
};

/// The materials of an entity's mesh slots (the primitives of a glTF mesh), named by id: slot `i` of the mesh draws
/// with `slot[i]`, or with the entity's [[asset::MeshReference::material]] when `slot[i]` is 0. An entity without this
/// component draws all of its mesh with the one material. It is a component of its own, so no level
/// written before it changes.
///
/// - Thread-safety: thread-compatible plain data.
struct MeshSlotMaterials
{
    storage::ResourceRef s0;   ///< The material of slot 0; unset keeps the entity's own.
    storage::ResourceRef s1;   ///< The material of slot 1; unset keeps the entity's own.
    storage::ResourceRef s2;   ///< The material of slot 2; unset keeps the entity's own.
    storage::ResourceRef s3;   ///< The material of slot 3; unset keeps the entity's own.
    storage::ResourceRef s4;   ///< The material of slot 4; unset keeps the entity's own.
    storage::ResourceRef s5;   ///< The material of slot 5; unset keeps the entity's own.
    storage::ResourceRef s6;   ///< The material of slot 6; unset keeps the entity's own.
    storage::ResourceRef s7;   ///< The material of slot 7; unset keeps the entity's own.
    storage::ResourceRef s8;   ///< The material of slot 8; unset keeps the entity's own.
    storage::ResourceRef s9;   ///< The material of slot 9; unset keeps the entity's own.
    storage::ResourceRef s10;  ///< The material of slot 10; unset keeps the entity's own.
    storage::ResourceRef s11;  ///< The material of slot 11; unset keeps the entity's own.
    storage::ResourceRef s12;  ///< The material of slot 12; unset keeps the entity's own.
    storage::ResourceRef s13;  ///< The material of slot 13; unset keeps the entity's own.
    storage::ResourceRef s14;  ///< The material of slot 14; unset keeps the entity's own.
    storage::ResourceRef s15;  ///< The material of slot 15; unset keeps the entity's own.

    /// Returns slot `i` (below `kMaxLevelSlotMaterials`).
    /// - Aborts: `i` is out of range.
    storage::ResourceRef& At(u32 i);
    /// Returns slot `i` (below `kMaxLevelSlotMaterials`).
    /// - Aborts: `i` is out of range.
    const storage::ResourceRef& At(u32 i) const;
};

/// The name an entity has in its level's text form (`entity NAME`); also what tools show.
///
/// - Thread-safety: thread-compatible; owns its text.
struct EntityName
{
    String<HeapAllocator> name = String<HeapAllocator>(HeapAllocator::Default());  ///< The name (`LevelNameValid`).
};

/// Registers [[asset::MeshReference]], [[asset::EntityName]], [[asset::MeshSlotMaterials]] and
/// [[asset::StaticCollider]] with `registry` (the engine's other components are
/// registered by `storage::RegisterBuiltinComponents` and `animation::RegisterAnimationComponents`).
///
/// - Param registry: the registry to fill.
/// - Ownership: borrows `registry` for the call.
/// - Fails: `Exists` when an id, a name or a type is registered already.
storage::Status RegisterLevelComponents(storage::ComponentRegistry* registry);

/// The most entities a level holds.
inline constexpr u32 kMaxLevelEntities = 1u << 20;
/// The longest entity name in bytes.
inline constexpr usize kMaxLevelNameBytes = 128;
/// The dependency role of a resource a component field names.
inline constexpr u32 kLevelRoleReference = 1;
/// The dependency role of a level the level includes (the manager loads it with this one).
inline constexpr u32 kLevelRoleInclude = 2;

/// Returns true when `name` is a valid entity name: 1 to [[asset::kMaxLevelNameBytes]] characters from letters, digits
/// and `_ . : / -`, and not the word `key` (the text form uses `#` for comments and `@` for literal ids and keys, so
/// neither is a name character).
///
/// - Fails: returns false as described.
bool LevelNameValid(StringView name);

/// The key of the entity called `name` in the level `level`: the first 16 bytes of the content hash of the level id
/// (8 bytes, little-endian) followed by the name, so the same text always gives the same keys and two levels may use
/// one name.
///
/// - Param level: the level's own id.
/// - Param name: a valid name.
/// - Returns: a non-null key.
/// - Fails: never.
storage::EntityKey LevelEntityKey(ResourceId level, StringView name);

/// One use of a resource by a component field.
///
/// - Thread-safety: thread-compatible plain data.
struct LevelReference
{
    u32 entity = 0;        ///< The index of the entity in the level's key order.
    u32 component_id = 0;  ///< The component's stable id.
    u32 field = 0;         ///< The index of the field in the component's field list.
    u32 type_tag = 0;      ///< The type the field declares (0 when it accepts any).
    ResourceId id;         ///< The resource named (never null in the table).
};

/// One distinct resource a level depends on.
///
/// - Thread-safety: thread-compatible plain data.
struct LevelResource
{
    ResourceId id;         ///< The resource.
    ResourceType type;     ///< Its type.
    u32 type_version = 0;  ///< The version of that type it was minted at (above 0).
    u32 roles = 0;         ///< [[asset::kLevelRoleReference]] and [[asset::kLevelRoleInclude]], or both.
};

/// The CPU description of a level: what the minter builds and the loader returns.
///
/// - Thread-safety: thread-compatible.
/// - Ownership: owns its vectors.
struct LevelData
{
    Vector<storage::EntityRecord> entities;  ///< One record per entity, in strictly increasing key order.
    Vector<LevelResource> resources;         ///< Every distinct resource named, in increasing id order.
    Vector<LevelReference> references;       ///< Filled by the builder and the parser: every non-null use.
};

/// What the resource manager needs to know about a level without reading its records: the counts and the list of
/// resources it depends on.
///
/// - Thread-safety: thread-compatible plain data.
/// - Ownership: owns its vector; `bytes` borrows the blob it was read from.
struct LevelSummary
{
    u32 entity_count = 0;             ///< Entities in the level.
    u32 record_count = 0;             ///< Component records over all entities.
    u32 reference_count = 0;          ///< Entries of the reference table.
    Vector<LevelResource> resources;  ///< Every distinct resource, in increasing id order.
    ByteSpan bytes;  ///< The whole blob, for [[asset::LoadLevelBlob]]; borrowed from the caller's buffer.
};

/// Reads the header and the dependency list of a level blob, checking only the container and the header's own
/// consistency (the sizes of the sections against the counts, the entity table against the containers' section); the
/// records are not read. The loader of the resource
/// manager uses it so that loading a level copies nothing; spawning the level ([[asset::LoadLevelBlob]]) validates the
/// records.
///
/// - Param bytes: the blob.
/// - Param types: the registry of resource types.
/// - Param out: replaced by the summary.
/// - Fails: `Corrupt` for an inconsistent header or section; `UnsupportedVersion` (the message names `asset_mint
///   remint`) for another container or type version; `TypeMismatch` for a blob of another type; `LimitExceeded` when
///   memory runs out.
/// - Ownership: borrows `bytes` for the call; `*out` is replaced.
Status ReadLevelSummary(ByteSpan bytes, const TypeRegistry& types, LevelSummary* out);

/// Builds a level blob for `id`: validates every entity container against `registry`, finds the references and checks
/// them against `data.resources`, and writes the sections.
///
/// - Param types: the registry of resource types.
/// - Param registry: the components; it must hold the builtin components (`entity_key`, `parent_link`).
/// - Param id: the level's id (never null).
/// - Param data: the entities and the resources; `references` is ignored and recomputed.
/// - Param out: replaced by the blob.
/// - Param target: the target tag recorded in the header.
/// - Param limits: the bounds the records are held to.
/// - Fails: `Invalid` for a null id, entities out of key order or with a null key, a container that does not parse, a
///   record of an unknown, derived, skipped or structural component (`entity_key`), a component twice in one entity, a
///   null id in a field that is not nullable, a reference whose resource is not listed or has another type than the
///   field declares, a listed resource nothing names, a resource listed twice or out of order, more than
///   [[asset::kMaxLevelEntities]] entities; `UnsupportedVersion` for a record not at its registered version;
///   `LimitExceeded` when memory runs out.
/// - Ownership: borrows its arguments for the call; `*out` is replaced.
Status BuildLevelBlob(const TypeRegistry& types, const storage::ComponentRegistry& registry, ResourceId id,
                      const LevelData& data, Vector<byte>* out, u32 target = 0,
                      const storage::CodecLimits& limits = storage::CodecLimits());

/// Reads a level blob into a description, with every check [[asset::BuildLevelBlob]] makes plus the blob's own layout:
/// the table against the sections, the reference table against the references found in the records, and the dependency
/// list against both.
///
/// - Param bytes: the blob.
/// - Param types: the registry of resource types.
/// - Param registry: the components.
/// - Param out: replaced by the description.
/// - Param id_out: receives the blob's own id when not null.
/// - Param limits: the bounds the records are held to.
/// - Fails: `Corrupt` for any inconsistency; `UnsupportedVersion` (the message names `asset_mint remint`) for another
///   container, type or component version; `TypeMismatch` for a blob of another type; `NotFound` for a record of a
///   component the registry does not know; `LimitExceeded` when memory runs out.
/// - Ownership: borrows `bytes` for the call; `*out` is replaced.
Status ParseLevelBlob(ByteSpan bytes, const TypeRegistry& types, const storage::ComponentRegistry& registry,
                      LevelData* out, ResourceId* id_out = nullptr,
                      const storage::CodecLimits& limits = storage::CodecLimits());

/// Creates the level's entities in `world`: parses the blob, then loads its records the way `storage::WorldStorage`
/// does, so the world is untouched when the blob or any record is bad. References are left as the ids the components
/// hold; the resource manager resolves them. Derived state (transforms, bounds) is not stored: run the
/// systems once afterwards.
///
/// - Param bytes: the blob.
/// - Param types: the registry of resource types.
/// - Param registry: the components.
/// - Param world: receives the entities.
/// - Param index: the keys of `world`; updated.
/// - Param data: receives the parsed description (the resources and references the manager needs) when not null.
/// - Param limits: the bounds the records are held to.
/// - Fails: the errors of [[asset::ParseLevelBlob]]; `Exists` when an entity key is in the world already; the errors
///   of `storage::WorldStorage::Load` (`Corrupt` for a record that does not decode, `MissingReference` for a parent
///   that is not in the level); on any failure the world is as it was.
/// - Ownership: borrows its arguments for the call; `*data` is replaced.
Status LoadLevelBlob(ByteSpan bytes, const TypeRegistry& types, const storage::ComponentRegistry& registry,
                     ecs::World& world, storage::KeyIndex& index, LevelData* data = nullptr,
                     const storage::CodecLimits& limits = storage::CodecLimits());

}  // namespace vse::asset
