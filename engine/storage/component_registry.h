#pragma once

//! [[storage::ComponentRegistry]] (Phase 19 Day 256): how each component type is stored. A component is registered once
//! with a stable numeric id written in code, a unique name, a version and a list of fields ([[storage::Field]]); the
//! registry encodes and decodes one component value to and from the canonical bytes, finds a registration by id, name
//! or ECS type, and lists the component types a world holds that nobody registered.

#include "containers/span.h"
#include "containers/unique_ptr.h"
#include "containers/vector.h"
#include "ecs/component.h"
#include "ecs/world.h"
#include "lang/initializer_list.h"
#include "storage/field.h"
#include "storage/status.h"
#include "string_view.h"
#include "types.h"

namespace engine_placeholder::storage
{

/// How the registry treats a component type.
enum ComponentFlags : u32
{
    kComponentStored = 0,     ///< Its fields are written and read.
    kComponentDerived = 1,    ///< Rebuilt after a load from other data (a world transform, a spatial proxy): never
                              ///< stored.
    kComponentSkip = 2,       ///< Runtime-only and never stored, by explicit choice.
    kComponentQueryable = 8,  ///< With the stored kind: a SQL store in column layout also keeps its scalar and vector
                              ///< fields in a table of their own, so that tools can query them (strings, arrays,
                              ///< matrices, boxes and references stay in the blob).
    kComponentPresence = 4,   ///< With `kComponentDerived`: whether an entity has it is stored (an empty record) and a
                              ///< load adds it default-constructed, for the system that fills it.
};

/// One step of an upgrade chain: turns the bytes of version `from_version` into the bytes of `from_version + 1`.
///
/// `run` reads the whole old record from `in` and writes the whole new record to `out`; the registry checks that `in`
/// is used up, bounds the size of `out`, and chains the steps. A step is a plain function (it has no state) and must
/// treat `in` as untrusted: every read returns an error to pass on.
///
/// - Thread-safety: thread-compatible plain data.
struct UpgradeStep
{
    u32 from_version = 0;  ///< The version this step reads.
    /// Converts one record. Returns a failed status (`Corrupt` for bad input) or success.
    Status (*run)(bytes::ByteReader& in, bytes::ByteWriter& out, const CodecLimits& limits) = nullptr;
};

/// A hand-written save and load for a type whose members the field list cannot describe (a handle that stores a name, a
/// union). It follows the same rules as a field codec: the reader is bounds-checked and every length it reads is held
/// to the [[storage::CodecLimits]].
///
/// - Thread-safety: thread-compatible plain data.
struct CustomCodec
{
    /// Writes the component at `component`. On failure the registry cuts the writer back.
    Status (*save)(const void* component, bytes::ByteWriter& out, const CodecLimits& limits) = nullptr;
    /// Reads into the constructed component at `component`. On failure the component may be partly written.
    Status (*load)(bytes::ByteReader& in, void* component, const CodecLimits& limits) = nullptr;
};

/// The registration of one component type: pass it to [[storage::ComponentRegistry::Register]].
///
/// - Thread-safety: thread-compatible plain data.
/// - Ownership: `name` is borrowed (a string literal); `fields` is read during the `Register` call only.
struct ComponentDesc
{
    u32 id = 0;  ///< The stable id, written in code and never reused. Must be nonzero and unique. Ids below 1000 belong
                 ///< to the engine's own components.
    const char* name = "";   ///< The stable name, unique, not empty, a string literal.
    u32 version = 1;         ///< The version of the stored form; raise it when the fields change, and add an
                             ///< [[storage::UpgradeStep]] for the old one.
    u32 oldest_version = 1;  ///< The oldest stored version that still loads. `upgrades` must hold one step for each
                             ///< version from here up to `version - 1`.
    u32 flags = kComponentStored;  ///< [[storage::ComponentFlags]].
    // Not `= {}`: MSVC does not implement list initialization of an InitializerList in a default member initializer
    // (C2797).
    InitializerList<FieldDesc> fields = InitializerList<FieldDesc>();  ///< The fields in storage order. Empty for a
                                                                       ///< tag, a derived or a skipped component, or
                                                                       ///< one with a `custom` codec.
    InitializerList<UpgradeStep>
        upgrades = InitializerList<UpgradeStep>();  ///< The steps from `oldest_version` to
                                                    ///< `version`, in any order, one per version.
    CustomCodec custom = {};  ///< Set both functions for a type the fields cannot describe; then `fields` is empty.
};

/// A registered component: read-only after registration.
///
/// - Thread-safety: thread-compatible; read-only after registration, so safe to share once registration is done.
/// - Ownership: owned by the registry; references stay valid for the registry's life.
class RegisteredComponent
{
public:
    /// The stable id.
    u32 Id() const { return id_; }
    /// The stable name.
    ///
    /// - Ownership: the string is the one passed at registration (a literal).
    /// - Fails: never.
    const char* Name() const { return name_; }
    /// The version of the stored form.
    u32 Version() const { return version_; }
    /// The oldest stored version that loads (through the upgrade chain).
    u32 OldestVersion() const { return oldest_version_; }
    /// The [[storage::ComponentFlags]].
    u32 Flags() const { return flags_; }
    /// Returns true when the fields are written and read (not derived, not skipped).
    /// - Fails: never.
    bool IsStored() const { return (flags_ & (kComponentDerived | kComponentSkip)) == 0; }
    /// Returns true when the component is stored and flagged `kComponentQueryable`.
    /// - Fails: never.
    bool IsQueryable() const { return IsStored() && (flags_ & kComponentQueryable) != 0; }
    /// The ECS type this registration describes ([[ecs::kInvalidComponent]] when the ECS registry was full).
    ecs::ComponentId EcsId() const { return ecs_id_; }
    /// Returns true when the ECS type is a tag (no data, only presence).
    /// - Fails: never.
    bool IsTag() const { return is_tag_; }
    /// The fields in storage order.
    Span<const FieldDesc> Fields() const { return Span<const FieldDesc>(fields_.Data(), fields_.Size()); }

    /// Appends the encoding of the component at `component` (every field in order, no framing) to `out`.
    ///
    /// - Param component: a live object of the registered type.
    /// - Param out: receives the bytes; on failure it is cut back to its size at entry.
    /// - Param limits: the string and array bounds.
    /// - Ownership: `component` is read during the call only.
    /// - Fails: `Invalid` (an enum value outside its range, a string that is not UTF-8); `LimitExceeded` (a string or
    /// array over its limit); the component is not stored: `Invalid`.
    Status Encode(const void* component, bytes::ByteWriter& out, const CodecLimits& limits) const;
    /// Reads the fields from `in` into the live object at `component`.
    ///
    /// - Param in: positioned at the first field; on success it is left after the last.
    /// - Param component: a constructed object of the registered type; on failure it may be partly written and must be
    /// discarded.
    /// - Param limits: the string and array bounds.
    /// - Ownership: `component` is written during the call only.
    /// - Fails: `Corrupt` (truncated, an invalid value, a count that does not match, an enum outside its range, text
    /// that is not UTF-8); `LimitExceeded`; the component is not stored: `Invalid`.
    Status Decode(bytes::ByteReader& in, void* component, const CodecLimits& limits) const;
    /// Loads a whole stored record: checks the version, runs the upgrade chain from `stored_version` to the current
    /// layout, decodes, and requires that every byte was used.
    ///
    /// - Param stored_version: the version the record was written with.
    /// - Param bytes: the record's bytes (untrusted).
    /// - Param component: a constructed object of the registered type; on failure it may be partly written and must be
    /// discarded.
    /// - Param limits: the record, string and array bounds.
    /// - Ownership: `bytes` and `component` are used during the call only.
    /// - Fails: `TooNew` (above the registered version); `TooOld` (below `OldestVersion`, or 0); `LimitExceeded` (the
    /// record, or an upgrade step's output, is above `max_record_bytes`; a string or array above its limit); `Corrupt`
    /// (the bytes do not decode, or bytes are left over); anything an upgrade step returns; `Invalid` (the component is
    /// not stored).
    Status DecodeRecord(u32 stored_version, ByteSpan bytes, void* component, const CodecLimits& limits) const;

private:
    friend class ComponentRegistry;
    u32 id_ = 0;
    const char* name_ = "";
    u32 version_ = 1;
    u32 oldest_version_ = 1;
    u32 flags_ = 0;
    CustomCodec custom_;
    Vector<UpgradeStep> upgrades_;
    ecs::ComponentId ecs_id_ = ecs::kInvalidComponent;
    bool is_tag_ = false;
    Vector<FieldDesc> fields_;
};

/// What to do with a stored record whose component id is not registered in this process.
enum class UnknownPolicy : u8
{
    Fail,           ///< The load fails with `NotFound`.
    SkipAndReport,  ///< The record is dropped and counted; the caller reports the count.
    KeepOpaque,     ///< The record's bytes are kept as they are and written back on the next save, so an older program
                    ///< does not destroy a newer program's data.
};

/// What the loader does with one record, as decided by [[storage::ComponentRegistry::Resolve]].
enum class RecordAction : u8
{
    Decode,      ///< Decode it with the returned registration.
    Skip,        ///< Drop it (a derived or skipped component, or an unknown one under `SkipAndReport`).
    KeepOpaque,  ///< Keep its bytes unchanged.
};

/// A recorded (id, name, version) triple: what a store remembers about the registrations that wrote it. See
/// [[storage::ComponentRegistry::CheckManifest]].
///
/// - Thread-safety: thread-compatible plain data.
/// - Ownership: `name` borrows its text.
struct ManifestEntry
{
    u32 id = 0;       ///< The component's id.
    StringView name;  ///< The component's name.
    u32 version = 0;  ///< The version it was written with.
};

/// The table of stored component types.
///
/// - Thread-safety: thread-compatible. Register everything at start-up, before other threads read; after that every
/// const function is safe from any thread.
/// - Ownership: owns its registrations; lookups return pointers that stay valid for the registry's life.
class ComponentRegistry
{
public:
    /// Creates an empty registry.
    ComponentRegistry() = default;
    ComponentRegistry(const ComponentRegistry&) = delete;
    ComponentRegistry& operator=(const ComponentRegistry&) = delete;

    /// Registers component type `T`.
    ///
    /// - Param desc: the id, name, version, flags and fields (see [[storage::ComponentDesc]]).
    /// - Ownership: nothing in `desc` is kept by reference except `name`.
    /// - Fails: `Exists` (the id, the name or the type `T` is registered already); `Invalid` (id 0, an empty name,
    /// version 0, a stored component of a non-tag type with no fields, fields on a tag or a derived or skipped
    /// component, an enum field with no range, two fields with one name).
    template <class T> Status Register(const ComponentDesc& desc)
    {
        return RegisterEntry(desc, ecs::ComponentOf<T>(), IsEmpty<T>);
    }

    /// Finds a registration by id.
    ///
    /// - Ownership: the pointer is owned by the registry and valid for its life.
    /// - Fails: returns null when there is none.
    const RegisteredComponent* FindById(u32 id) const;
    /// Finds a registration by name.
    ///
    /// - Ownership: the pointer is owned by the registry and valid for its life.
    /// - Fails: returns null when there is none.
    const RegisteredComponent* FindByName(StringView name) const;
    /// Finds the registration of an ECS component type.
    ///
    /// - Ownership: the pointer is owned by the registry and valid for its life.
    /// - Fails: returns null when there is none.
    const RegisteredComponent* FindByEcsId(ecs::ComponentId id) const;
    /// Finds the registration of `T`.
    ///
    /// - Ownership: the pointer is owned by the registry and valid for its life.
    /// - Fails: returns null when there is none.
    template <class T> const RegisteredComponent* Find() const { return FindByEcsId(ecs::ComponentOf<T>()); }
    /// The number of registered components.
    u32 Count() const { return static_cast<u32>(components_.Size()); }
    /// The registration at `index` (registration order). Aborts: `index` is not below `Count()`.
    const RegisteredComponent& At(u32 index) const;

    /// Decides what to do with a stored record of component `id`.
    ///
    /// - Param id: the id read from the record.
    /// - Param policy: what to do when `id` is not registered.
    /// - Param component: receives the registration for `Decode`, null for the other actions.
    /// - Param action: receives the decision.
    /// - Ownership: `component` points into the registry and stays valid for its life.
    /// - Fails: `NotFound` (unknown id under `Fail`). A registered but derived or skipped component is `Skip` and never
    /// an error.
    Status Resolve(u32 id, UnknownPolicy policy, const RegisteredComponent** component, RecordAction* action) const;

    /// Appends one manifest entry per registration, in registration order, to `out`: the record a store keeps of what
    /// wrote it.
    ///
    /// - Param out: receives the entries; their names borrow the registry's strings.
    /// - Ownership: the entries are valid while the registry lives.
    void Manifest(Vector<ManifestEntry>* out) const;

    /// Checks the registrations against a manifest recorded earlier. A recorded component that is not registered now is
    /// fine (the load policy decides what to do with its data); one that is registered must keep its id and name.
    ///
    /// - Param recorded: the entries read from a store.
    /// - Fails: `Mismatch` (an id now has another name, or a name another id); `TooNew` (a recorded version is above
    /// the registered one).
    Status CheckManifest(Span<const ManifestEntry> recorded) const;

    /// Appends to `out` the ECS component types that exist on at least one entity of `world` and have no registration
    /// here at all (a stored, derived or skipped registration all count as decided). A forgotten component is found by
    /// this check, not by a lost save. Tags count: their presence is data.
    ///
    /// - Param world: the world to inspect (read only).
    /// - Param out: receives the ECS ids; the names are in `ecs::ComponentRegistry::Global().Info(id).name`.
    /// - Thread-safety: thread-compatible. Reads the world; do not call while another thread writes it.
    /// - Ownership: `out` is appended to during the call only.
    void UnregisteredWithData(const ecs::World& world, Vector<ecs::ComponentId>* out) const;

private:
    Status RegisterEntry(const ComponentDesc& desc, ecs::ComponentId ecs_id, bool is_tag);

    Vector<UniquePtr<RegisteredComponent>> components_;
};

}  // namespace engine_placeholder::storage
