#pragma once

//! The text source form of a level (`.level.src`), the level minter and the formatter that prints a level back as text.
//! Phase 22 Day 327. An offline tool's code: never linked into the engine; its tests compile it directly.
//!
//! Line-oriented text; `#` starts a comment (outside a quoted string); fields are separated by spaces or tabs;
//! indentation means nothing:
//!
//!     id 00000000000000aa
//!     include /levels/sky
//!     entity rock
//!       parent ground
//!       component local_transform
//!         position = 1 0 2
//!         rotation = 0 0 0 1
//!         scale = 1 1 1
//!       component mesh_reference
//!         mesh = /meshes/rock
//!         material = /materials/stone
//!     entity hero key @00000000000000000000000000000007
//!       skeleton /skeletons/hero
//!       animation /clips/idle loop speed 1.5
//!
//! `id` comes first (`asset_mint adopt` writes it). `include PATH` names a level to spawn along with this one; includes
//! come before the first entity and may not form a cycle. `entity NAME` starts an entity whose key is derived from the
//! level's id and its name ([[asset::LevelEntityKey]]); `entity NAME key @HEX32` gives the key and `entity key @HEX32`
//! an unnamed entity. `parent NAME` (or `parent @HEX32`) makes the entity a child. `component TYPE` starts one
//! component record, named as the storage registry names it; the `FIELD = VALUE...` lines that follow set its fields
//! (the names and kinds are the registry's, so a new component needs no change here), and a field left out keeps the
//! value of a default-constructed component. Values: integers and floats as decimal numbers, `true`/`false`, vectors,
//! quaternions, boxes and matrices as their numbers in order, an enum as its integer, a string in double quotes with
//! `\"`, `\\`,
//! `\n`, `\t`, `\r` and `\xHH`, an entity reference as an entity's name or `@HEX32` or `null`, a resource reference as
//! a virtual path or `@HEX16` or `null`. Two lines are shorthand: `skeleton PATH` is a `skeleton_instance` component,
//! and `animation PATH [loop|once] [speed X]` an `animation_player` (the clip, `looping`, `speed`). The formatter
//! always prints the long form.

#include "asset/blob.h"
#include "asset/level.h"
#include "asset/resource_id.h"
#include "asset/status.h"
#include "containers/array.h"
#include "containers/span.h"
#include "containers/vector.h"
#include "ecs/world.h"
#include "mem/heap_allocator.h"
#include "storage/component_registry.h"
#include "storage/identity.h"
#include "string/string.h"
#include "string_view.h"
#include "types.h"

namespace vse::asset
{

/// Where and why a level source was refused.
///
/// - Thread-safety: immutable value.
struct LevelSourceError
{
    u32 line = 0;                   ///< 1-based line number; 0 when the error is about the file as a whole.
    Array<char, 256> message = {};  ///< A sentence naming the entity and field.
};

/// Finds the resources a level source names.
///
/// - Thread-safety: implementation-defined; the minter calls it from one thread.
class LevelReferenceResolver
{
public:
    virtual ~LevelReferenceResolver() = default;

    /// Resolves a virtual path.
    ///
    /// - Param path: the path as written in the source.
    /// - Param id: receives the resource's id.
    /// - Param blob: receives the resource's blob (borrowed from the resolver; valid until it is destroyed).
    /// - Fails: `NotFound` when the project has no such resource; any other error aborts the mint.
    virtual Status ResolvePath(StringView path, ResourceId* id, ByteSpan* blob) const = 0;

    /// Finds a resource by id (an `@HEX16` reference, an included level's own includes, a clip's skeleton).
    ///
    /// - Param id: the id.
    /// - Param blob: receives the resource's blob (borrowed from the resolver; valid until it is destroyed).
    /// - Fails: `NotFound` when the project has no such resource.
    virtual Status ResolveId(ResourceId id, ByteSpan* blob) const = 0;

    /// Finds the path of a resource, for the formatter.
    ///
    /// - Param id: the id.
    /// - Param path: receives the virtual path.
    /// - Fails: `NotFound` when no path is known (the formatter then writes `@HEX16`).
    virtual Status PathOf(ResourceId id, String<HeapAllocator>* path) const = 0;
};

/// The level minter: source text to blob bytes, deterministic. Every component and field is checked against the
/// registry; every resource reference is resolved, typed against the field's declared type and recorded in the
/// dependency list; the whole level is then parsed back exactly as the loader will read it.
///
/// - Param text: the source.
/// - Param resolver: finds the resources.
/// - Param types: the registry of resource types.
/// - Param registry: the components (builtin, level and whatever the game registers).
/// - Param out: replaced by the blob.
/// - Param error: receives the line and message of a refusal.
/// - Param target: the target tag recorded in the header.
/// - Fails: `Invalid` with `error` filled in: a missing or misplaced `id` line (the message names `asset_mint adopt`),
///   an unknown record, component or field (the message lists the registry's names), a repeated entity name, key,
///   component or field, a value that does not fit its field, a reference that does not resolve or is of another type
///   than the field declares, a null reference in a field that is not nullable, a `parent` or entity reference to an
///   entity that does not exist, an include that is not a level or that includes this level again, a clip made for
///   another skeleton than the entity's, and the limits of [[asset::BuildLevelBlob]]; `LimitExceeded` when memory
///   runs out.
/// - Ownership: borrows its arguments for the call; `*out` is replaced.
Status MintLevel(StringView text, const LevelReferenceResolver& resolver, const TypeRegistry& types,
                 const storage::ComponentRegistry& registry, Vector<byte>* out, LevelSourceError* error,
                 u32 target = 0);

/// Prints a level as its canonical text: entities ordered by name (unnamed ones last, by key), components in the
/// order they are stored, every field written. Minting the text gives the same blob.
///
/// - Param id: the level's id.
/// - Param entities: the entity records, as stored (key order).
/// - Param includes: the levels included.
/// - Param resolver: gives paths for resource ids (an unknown one is written as `@HEX16`).
/// - Param registry: the components.
/// - Param out: replaced by the text.
/// - Param error: receives the reason of a refusal.
/// - Fails: `Invalid` with `error` filled in: a record of an unknown component, a derived or skipped one is left out,
/// an
///   entity name that is not a valid name, a float that is not finite, a record that does not decode.
/// - Ownership: borrows its arguments for the call; `*out` is replaced.
Status FormatLevelRecords(ResourceId id, Span<const storage::EntityRecord> entities, Span<const ResourceId> includes,
                          const LevelReferenceResolver& resolver, const storage::ComponentRegistry& registry,
                          String<HeapAllocator>* out, LevelSourceError* error);

/// Prints a level blob as text.
///
/// - Param blob: the blob.
/// - Param types: the registry of resource types.
/// - Param registry: the components.
/// - Param resolver: gives paths for resource ids.
/// - Param out: replaced by the text.
/// - Param error: receives the reason of a refusal.
/// - Fails: the errors of [[asset::ParseLevelBlob]] and of [[asset::FormatLevelRecords]].
/// - Ownership: borrows its arguments for the call; `*out` is replaced.
Status FormatLevelBlob(ByteSpan blob, const TypeRegistry& types, const storage::ComponentRegistry& registry,
                       const LevelReferenceResolver& resolver, String<HeapAllocator>* out, LevelSourceError* error);

/// Prints the keyed entities of a world as level text: the world is saved into a scratch store the way
/// `storage::WorldStorage` saves it, and the stored components are printed (derived ones are left out: a level holds
/// what an author wrote).
///
/// - Param id: the level's id.
/// - Param world: the world to read.
/// - Param index: the keys of `world`; synchronized by the call.
/// - Param includes: the levels the level includes.
/// - Param resolver: gives paths for resource ids.
/// - Param registry: the components.
/// - Param out: replaced by the text.
/// - Param error: receives the reason of a refusal.
/// - Fails: the errors of `storage::WorldStorage::Save` and of [[asset::FormatLevelRecords]].
/// - Thread-safety: thread-compatible; reads the world.
/// - Ownership: borrows its arguments for the call; `*out` is replaced.
Status FormatWorld(ResourceId id, ecs::World& world, storage::KeyIndex& index, Span<const ResourceId> includes,
                   const LevelReferenceResolver& resolver, const storage::ComponentRegistry& registry,
                   String<HeapAllocator>* out, LevelSourceError* error);

}  // namespace vse::asset
