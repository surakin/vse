# Storage: components that survive a save

Status: Phase 19. Built: the component registry and the canonical encoding (Day 256), versions, upgrades, custom codecs, limits and hostile-input tests
(Day 257). stable identities and references (Day 258). Next: the record store (259), the record store (259), saving and loading a world (260-261), the SQL store (262-263). Design:
`docs/superpowers/specs/2026-11-11-storage-design.md`.

**Audience:** engine and game programmers who make a component type saveable, and anyone who reads or writes the bytes.

The module `engine/storage` (namespace `storage`) describes how each component type is stored. It sits above `ecs` and `spatial` and below nothing; `ecs`
does not know it exists.

## Registering a component

A component is registered once, with a **stable numeric id written in code**, a **unique name**, a **version** and its **fields**. The id is never the
in-memory ECS id (that depends on the order types were first used); it is what the stored bytes are filed under, so it is never changed or reused. Ids below
1000 belong to the engine (`storage/builtin_components.h`); a game starts at 1000.

```cpp
struct Health {
    i32 current = 0;
    i32 max = 100;
};

storage::ComponentRegistry registry;
storage::RegisterBuiltinComponents(&registry);
registry.Register<Health>({.id = 1100,
                           .name = "health",
                           .fields = {storage::Field<&Health::current>("current"), storage::Field<&Health::max>("max")}});
```

`Field<&T::member>("name")` takes the member as a template argument, so the encode and decode functions are plain function pointers with the member
baked in. The member's type decides the stored form:

| Member type | Stored as |
|---|---|
| `u8`..`u64`, `i8`..`i64` | fixed width, little-endian |
| `f32`, `f64` | the bit pattern (a NaN payload, `-0` and denormals survive) |
| `bool` | one byte, 0 or 1 (anything else is `Corrupt` on read) |
| `Vec2`, `Vec3`, `Vec4`, `Quaternion`, `Mat4`, `Aabb` | their `f32`s in order (a matrix column by column) |
| an enum | its underlying integer, **checked against a range you declare**: `Field<&T::kind>("kind", FieldOptions::Enum(0, 2))` |
| `String<HeapAllocator>` | a varint length and UTF-8; validated on write and on read; bounded by `FieldOptions::Text(n)` and `CodecLimits::max_string_bytes` |
| `Array<T, N>` of any of these | the count `N` as a varint, then the elements; a count that is not `N` is `Corrupt` |
| `storage::EntityRef` | the target's 128-bit key (16 bytes); resolved to an entity after a load (Day 258) |

**A member of any other type does not compile.** There is no silent skip: a type the registry cannot describe is a `static_assert`. C++ has no reflection,
so a member you forget to list is not detected: list every member, and keep the list in step with the struct.

A registration can be refused: the same id, name or type twice (`Exists`), id 0, an empty name, version 0, a component with data and no fields, a tag with
fields, an enum without a range, two fields with one name (`Invalid`). All are `Status` values.

### Derived and skipped components

A component rebuilt from other data after a load (`WorldTransform`, `HierarchyDepth`, `WorldBounds`, `SpatialProxy`) is registered with
`kComponentDerived`; a runtime-only one with `kComponentSkip`. Neither has fields and neither is ever stored. Registering them is how you tell the registry
"I decided". `registry.UnregisteredWithData(world, &out)` lists the component types present on entities of a world that have **no** registration at all, so
a component added next month cannot be forgotten silently; run it in a test of your game's world.

`ecs::Parent` is skipped for now; Day 258 stores it as an `EntityRef`.

## The encoding

`registered->Encode(&component, writer, limits)` appends every field in order, with no framing, padding or alignment; `Decode(reader, &component, limits)`
reads them into a constructed component. A failed `Encode` cuts the writer back to where it was. A failed `Decode` may leave the component partly written:
discard it.

The bytes come from `bytes::ByteWriter` and `bytes::ByteReader` (`foundation/bytes/`), which Phase 20's network code shares:

- integers little-endian at their own width; floats as bit patterns; `bool` one byte;
- lengths and counts are **unsigned LEB128 varints in their shortest form** (a longer form is refused, so every value has one encoding);
- the reader is bounds-checked: truncation is `Truncated`, a length above the caller's limit is `TooLong`, a malformed value is `Invalid`; a failed read does
  not move the reader and never reads outside the span.

`storage::CodecLimits` bounds every length (a string, an array). The defaults are generous; Day 264 reads them from the settings.

### The format is tested

`tests/storage/encoding_test.cpp` holds a **golden byte string** for a fixed component. A change to the encoding fails it. Changing the stored form of a
field means raising the component's `version` (and, from Day 257, registering an upgrade), never editing bytes under an old version.

## The manifest

A store remembers which `(id, name, version)` wrote it. `registry.Manifest(&out)` produces the list; `registry.CheckManifest(recorded)` refuses a registry
in which an id now has another name or a name another id (`Mismatch`), and a recorded version above the registered one (`TooNew`). A recorded component
that is not registered now is fine here; the load policy (Day 260) decides what happens to its data.

## Versions and upgrades

A component has a `version` (starts at 1). When its fields change, **raise the version and add an `UpgradeStep` that turns the old bytes into the new
layout**; never edit the bytes an old version wrote.

```cpp
Status V1ToV2(bytes::ByteReader& in, bytes::ByteWriter& out, const CodecLimits&) {
    f32 x = 0, y = 0;
    // read the old record completely (every read returns an error to pass on) ...
    // ... then write the new one: the old fields and a default for the new z
}

registry.Register<Mover>({.id = 3000, .name = "mover", .version = 2,
                          .fields = {Field<&Mover::x>("x"), Field<&Mover::y>("y"), Field<&Mover::z>("z")},
                          .upgrades = {{1, &V1ToV2}}});
```

`registered->DecodeRecord(stored_version, bytes, &component, limits)` loads a whole stored record: it refuses a version above the registered one (`TooNew`)
or below `oldest_version` (`TooOld`), runs the steps from the stored version up to the current one, decodes, and requires that every byte was used. The
chain must be complete: `Register` fails with `Invalid` unless there is exactly one step for each version from `oldest_version` to `version - 1`. A step
reads its whole input (the registry checks) and its output is bounded by `max_record_bytes`.

### Records the process does not know

A stored record is `ComponentRecord { id, version, bytes }`, framed by `WriteComponentRecord` / `ReadComponentRecord` (4-byte id, 4-byte version, a varint
count, the bytes). `registry.Resolve(id, policy, &component, &action)` tells a loader what to do with one:

| `UnknownPolicy` | An unregistered id |
|---|---|
| `Fail` | `NotFound` |
| `SkipAndReport` | `RecordAction::Skip`; the loader counts it |
| `KeepOpaque` | `RecordAction::KeepOpaque`; keep an `OpaqueComponent` and write it back unchanged on the next save, so an older program does not destroy a newer program's data |

A registered but derived or skipped component is always `Skip` (an old file may hold one; it is rebuilt or ignored).

### A hand-written codec

For a type the field list cannot describe (a handle that stores a name, a union) register `.custom = {&Save, &Load}` instead of fields. The functions get the
same `CodecLimits` and a bounds-checked reader; the same rules apply (every length bounded, every error returned).

## Limits

`RegisterStorageSettings(&registry)` declares `storage.max_record_bytes`, `storage.max_string_bytes`, `storage.max_array_elements` and
`storage.max_components_per_entity`; `LimitsFromSettings(registry)` turns them into the `CodecLimits` every encode and decode takes. A length above a limit is
`LimitExceeded` and nothing is allocated for it. (`max_components_per_entity` is enforced by the entity container of Day 260.)

## Hostile input is tested

`tests/storage/registry_fuzz_test.cpp` mutates valid encodings of every field kind (bit flips, truncation, length-byte overwrites, duplication, random bytes),
100,000 cases per kind in the plain and AddressSanitizer runs. Each result must be a `Corrupt` or `LimitExceeded` status or a value whose re-encoding equals
the bytes consumed (the encoding is canonical); a planted bug (a decoder that accepts a bool of 2) fails it.

## Identities and references

An entity that is saved needs a name that survives the process: an `EntityKey` (128 bits). Give an entity one with `KeyIndex::Assign(world, entity, key)`, which refuses a
key another entity has, or set an `EntityKeyComponent` yourself. `KeyGenerator` draws keys from the operating system; `KeyGenerator::Seeded(seed)` repeats the same
keys every run (tests). `ToText()` / `FromText()` give 32 hexadecimal digits.

`KeyIndex` maps key to entity and back. `Sync(world)` reads what changed (the change stamps) and what was removed (the removal log), so it costs what changed;
call it once a frame and before a lookup that must see this frame. Two entities with one key are never both indexed: `Sync` returns `Duplicate` and
`DuplicateCount()` says how many are left out, until one of them changes key or goes.

A reference between entities is stored as the target's key (`EntityRef`, the null key for none). After a load every entity exists before any reference is
resolved: `ResolveReference(index, key, policy, &report, &entity)` turns a key into an entity. A missing target is `MissingReference` under
`MissingPolicy::Fail`, or a null reference counted in the `ReferenceReport` under `NullReference`; the report keeps the first 8 missing keys.

`ecs::Parent` holds an entity id, which means nothing in a file, so its stored form is `ParentLink` (a key). `CaptureParents(world, index)` writes the links before a
save; `ApplyParents(world, index, policy)` sets the parents after a load, parents first, with the cycle and depth checks of `ecs::SetParent` (a cycle is refused and counted in
`ParentReport::rejected`), and removes the links. Under `Fail` nothing changes when any parent is missing.

## The record store

A `Store` keeps one `EntityRecord` (key, container bytes, `HashBytes` of the bytes) per `EntityKey`, in key order: `Put`, `Get`, `Remove`, `Scan(range, visitor)`,
`Count`, `GetMetadata` / `SetMetadata` (the well-known names are in `storage::metadata`) and `Begin` / `Commit` / `Rollback`. It knows nothing of components;
`WorldStorage` (Day 260) builds the bytes. All errors are `storage::Status` values.

A batch is all or nothing: what is `Put`, `Remove`d or set after `Begin` becomes visible together at `Commit`, and not at all after `Rollback`. A mutation that
fails inside a batch leaves it open for the caller to roll back. One thread writes at a time; while its batch is open another thread's mutation is `Busy`. Readers
on any thread see the last committed state (the batch's owner sees its own changes).

```cpp
MemoryStore store;
store.Begin();
store.Put(EntityRecord::Make(key, bytes));
store.SetMetadata(metadata::kLastSaveLabel, "autosave");
store.Commit();
```

`MemoryStore` is the reference: a sorted vector, a batch works on a copy and replaces the committed set at `Commit`. `FailAfter(n)` makes the nth next operation
return `Io`, which is how the conformance suite (`tests/storage/store_conformance.h`, used by every store) checks that a failure at any step leaves the old state.
A record whose bytes no longer match their hash is `Corrupt` on `Get` and `Scan`.

## Options

`SaveOptions` (mode `Full` or `Incremental`, a `Selection`, a label, batch size), `LoadOptions` (`Create`, `Replace` or `Merge`, the `UnknownPolicy`, the
`MissingPolicy`), a `Report` of counts, a `ProgressFn` and a `CancelToken` that is checked between steps. `WorldStorage::Save` and `Load` take them and arrive on
Day 260; `Delete` and `List` work now.

## Saving and loading a world

`WorldStorage` binds a registry and a store. `Save` writes one record per selected entity, in key order, in one batch; `Load` reads them back into a world.

```cpp compile run standalone
#include "ecs/transform.h"
#include "os/console.h"
#include "storage/builtin_components.h"
#include "storage/memory_store.h"
#include "storage/world_storage.h"

using namespace engine_placeholder;
using namespace engine_placeholder::storage;

int main() {
    ComponentRegistry registry;
    if (RegisterBuiltinComponents(&registry).Failed()) return 1;

    // A world of three keyed entities with a transform each.
    ecs::World world;
    KeyIndex index;
    KeyGenerator keys = KeyGenerator::Seeded(1);
    for (u32 i = 0; i < 3; ++i) {
        ecs::LocalTransform t;
        t.position = Vec3(static_cast<f32>(i), 0, 0);
        world.Create(EntityKeyComponent{keys.Next()}, t);
    }

    MemoryStore store;
    WorldStorage storage(registry, store);
    Report saved;
    if (storage.Save(world, index, SaveOptions(), &saved).Failed()) return 1;

    // Into an empty world: entities appear in key order, with the stored components.
    ecs::World loaded_world;
    KeyIndex loaded_index;
    Report loaded;
    if (storage.Load(loaded_world, loaded_index, LoadOptions(), &loaded).Failed()) return 1;
    os::Print("saved %u, loaded %u, keys %u\n", static_cast<u32>(saved.created), static_cast<u32>(loaded.created), static_cast<u32>(loaded_index.Size()));
    return 0;
}
```
```output
saved 3, loaded 3, keys 3
```

**What is stored.** The stored components of the entity (`kComponentStored`), its parent as a key (`ParentLink`), the presence of derived components that ask for it
(`WorldTransform`, `WorldBounds`: stored as an empty record and added back default-constructed) and the unknown components it carries (`OpaqueComponents`). The key
is the record's key. Nothing else: a resource, a spatial proxy and the hierarchy depth are rebuilt, so **run the transform and spatial systems once after a load**.

**Selections.** `Selection::All()` saves every keyed entity and a `Full` save also removes the records of entities that are gone. `Selection::Keys` (a key set) and
`Selection::Filter` (required ECS components plus an optional callback) save only those and remove nothing. A load takes `All` or `Keys`.

**A load is all or nothing for the world.** Pass one decodes and checks every record (version and upgrades, unknown components by policy, limits, parent targets,
the manifest and format version in the store); the world is not touched. Pass two creates the entities in key order, grouped by archetype, and sets the parents.
`Create` refuses a key that is in the world (`Exists`), `Replace` destroys the world's keyed entities first, `Merge` replaces the stored components of an entity
that has the key and keeps its entity id. Cancellation is checked in pass one only: once the world is being changed the load finishes.

**References.** A parent whose entity is not in the load or the world is `MissingReference` under `MissingPolicy::Fail` and a root plus a count in `Report::nulled`
under `NullReference`. An `EntityRef` field of your own component keeps the key; look the entity up with `KeyIndex::Find`.

