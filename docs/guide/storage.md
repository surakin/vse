# Storage: components that survive a save

Status: Built: the component registry and the canonical encoding, versions, upgrades, custom codecs, limits and hostile-input tests
. stable identities and references. Next: the record store (259), the record store (259), saving and loading a world (260-261), the SQL store (262-263). Design:
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
| `storage::EntityRef` | the target's 128-bit key (16 bytes); resolved to an entity after a load |

**A member of any other type does not compile.** There is no silent skip: a type the registry cannot describe is a `static_assert`. C++ has no reflection,
so a member you forget to list is not detected: list every member, and keep the list in step with the struct.

A registration can be refused: the same id, name or type twice (`Exists`), id 0, an empty name, version 0, a component with data and no fields, a tag with
fields, an enum without a range, two fields with one name (`Invalid`). All are `Status` values.

### Derived and skipped components

A component rebuilt from other data after a load (`WorldTransform`, `HierarchyDepth`, `WorldBounds`, `SpatialProxy`) is registered with
`kComponentDerived`; a runtime-only one with `kComponentSkip`. Neither has fields and neither is ever stored. Registering them is how you tell the registry
"I decided". `registry.UnregisteredWithData(world, &out)` lists the component types present on entities of a world that have **no** registration at all, so
a component added next month cannot be forgotten silently; run it in a test of your game's world.

`ecs::Parent` is not stored as a plain component; it is stored as a key (see "Identities and references" below).

## The encoding

`registered->Encode(&component, writer, limits)` appends every field in order, with no framing, padding or alignment; `Decode(reader, &component, limits)`
reads them into a constructed component. A failed `Encode` cuts the writer back to where it was. A failed `Decode` may leave the component partly written:
discard it.

The bytes come from `bytes::ByteWriter` and `bytes::ByteReader` (`foundation/bytes/`), which the network code shares:

- integers little-endian at their own width; floats as bit patterns; `bool` one byte;
- lengths and counts are **unsigned LEB128 varints in their shortest form** (a longer form is refused, so every value has one encoding);
- the reader is bounds-checked: truncation is `Truncated`, a length above the caller's limit is `TooLong`, a malformed value is `Invalid`; a failed read does
  not move the reader and never reads outside the span.

`storage::CodecLimits` bounds every length (a string, an array). The defaults are generous; the settings can change them (`LimitsFromSettings`).

### The format is tested

`tests/storage/encoding_test.cpp` holds a **golden byte string** for a fixed component. A change to the encoding fails it. Changing the stored form of a
field means raising the component's `version` (and registering an upgrade), never editing bytes under an old version.

## The manifest

A store remembers which `(id, name, version)` wrote it. `registry.Manifest(&out)` produces the list; `registry.CheckManifest(recorded)` refuses a registry
in which an id now has another name or a name another id (`Mismatch`), and a recorded version above the registered one (`TooNew`). A recorded component
that is not registered now is fine here; the load policy decides what happens to its data.

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
`LimitExceeded` and nothing is allocated for it. (`max_components_per_entity` is enforced by the entity container.)

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
`WorldStorage` builds the bytes. All errors are `storage::Status` values.

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
`MissingPolicy`), a `Report` of counts, a `ProgressFn` and a `CancelToken` that is checked between steps. `WorldStorage::Save` and `Load` take them.

## Saving and loading a world

`WorldStorage` binds a registry and a store. `Save` writes one record per selected entity, in key order, in one batch; `Load` reads them back into a world.

```cpp compile run standalone
#include "ecs/transform.h"
#include "log/output.h"
#include "storage/builtin_components.h"
#include "storage/memory_store.h"
#include "storage/world_storage.h"

using namespace vse;
using namespace vse::storage;

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
    out::Print("saved %u, loaded %u, keys %u\n", static_cast<u32>(saved.created), static_cast<u32>(loaded.created), static_cast<u32>(loaded_index.Size()));
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


## Incremental saves

A `SaveSession` remembers one world's saves to one store: the world tick and removal clock of the last save, the content hash of every record written and the key each entity had. Give it to
`SaveOptions::session` with `SaveMode::Incremental` and the save does the work in proportion to what changed:

- **Chunks.** A chunk is visited only when a column of a stored component (or the parent, the key, the unknown components) was written or added after the last save; derived columns do not count. A
  moved or created entity arrives in its chunk with every column stamped, so structural changes are noticed the same way.
- **Records.** The visited entities are encoded and each record is `Put` only when its content hash differs from what the session knows is stored. An entity written with the value it had costs an encode and no write.
- **Removals.** Destroyed entities, entities that lost their key and entities that changed it come from the ECS removal log of the key component (`World::RemovedAt`), which keeps the current and the previous frame
  (`World::EndFrame`). An incremental save at most one frame after the last one reads the log; a later one, a new session, a session of another world or a call to `SaveSession::Reset` has nothing to trust, so it
  **reconciles**: it encodes every keyed entity, compares each hash with what the store holds, writes the differences and removes the records of entities that are gone (`Report::reconciled` is 1). A restarted
  process therefore rewrites nothing that is already stored.

The store after any sequence of incremental saves equals the store after one full save of the same world (the tests compare them after 500 random frames). A cancelled or failed save leaves the store as it was and
the session unchanged, so the next save still sees everything that changed. A `Full` save with a session (the `AllKeyed` selection) starts the session from that save. `Report::encoded`, `chunks_visited` and
`chunks_skipped` show what a save cost; an idle world encodes nothing and visits no chunk.

Limits: a session follows the whole world (`AllKeyed` only); a record of a child is not rewritten when only its parent gains or loses a key (call `Reset`); if something else changes the store, call `Reset`.

## The SQL store

`SqlStore` is the record store on a database connection (`SqlStore::Open(connection, &store)`; the connection is borrowed and outlives the store). It keeps one row per entity,
`entity(key BLOB PRIMARY KEY, seq INTEGER NOT NULL, hash BLOB NOT NULL, data BLOB NOT NULL)` (the key as 16 big-endian bytes so the database's order is key order, the hash as 8
bytes), and the metadata in `meta(key TEXT PRIMARY KEY, value BLOB)`. It uses only the portable statement subset of the design (no `PRAGMA`, no SQLite-only form), prepares each
statement once and binds all data, so another database backend can run it. `Begin`, `Commit` and `Rollback` are database transactions.

The schema version is `Connection::SchemaVersion()` (also kept in the metadata as `schema_version`). `Open` applies the missing migration steps in one transaction (adding a column in place with `ALTER TABLE` when the connection supports `Feature::AddColumn`, otherwise rebuilding the table through a scratch table); a database with a
newer schema is refused with `TooNew` and left as it was. One connection serves the store, so **while a batch is open every call from another thread is `Busy`** (the memory store
shows readers the committed state instead). It passes the same conformance suite as `MemoryStore`, on SQLite in memory and in a file, and a failure injected at every write and sync of
a batch leaves the old or the new state.

```cpp compile run standalone
#include "database/sqlite/sqlite_database.h"
#include "log/output.h"
#include "storage/sql_store.h"

using namespace vse;
using namespace vse::storage;

int main() {
    database::SqliteDatabase db;
    UniquePtr<database::Connection> connection;
    if (db.Open(database::OpenOptions(), &connection).Failed()) return 1; // a private in-memory database
    UniquePtr<SqlStore> store;
    if (SqlStore::Open(*connection, &store).Failed()) return 1;
    const byte bytes[] = {1, 2, 3};
    if (store->Put(EntityRecord::Make(EntityKey{0, 42}, ByteSpan(bytes, 3))).Failed()) return 1;
    usize count = 0;
    if (store->Count(&count).Failed()) return 1;
    out::Print("%s holds %u record\n", store->Name(), static_cast<u32>(count));
    return 0;
}
```
```output
sql holds 1 record
```

### Column layout and queries

A component flagged `kComponentQueryable` in its registration also gets a table of its own when the store is opened with `SqlStoreOptions::columns`: `c_<name>(key, one column per scalar or
vector field)`. Integers, booleans and enums are `INTEGER` (a `u64` as its bit pattern, so ordering above 2^63 is signed), `f32` and `f64` are `REAL` (a NaN is NULL, which no comparison matches),
and a `Vec2`, `Vec3`, `Vec4` or `Quaternion` is one `REAL` column per part, named `<field>_x`, `_y`, `_z`, `_w`. Strings, arrays, matrices, boxes and references stay in the blob only. The
entity row is still the source of truth: a load reads it and nothing else, so both layouts load the same world, and `VerifyColumns` checks the tables against the blobs.

```cpp
SqlStoreOptions options;
options.registry = &registry;
options.columns = true;
const IndexRequest hp_index[] = {{"actor", "hp"}};   // optional: an index for the column
options.indexes = hp_index;
options.index_count = 1;
SqlStore::Open(connection, &store, options);

Vector<EntityKey> hurt;
store->FindKeys("actor", "hp", CompareOp::Less, database::ValueView::Integer(10), &hurt);  // keys in key order, no blob read
```

`FindKeys` and `CountWhere` answer from the table; a component that is not queryable is `Unsupported`, a name nobody registered is `NotFound`. When a registration gains a field, `Open` adds the column
with `ALTER TABLE ADD COLUMN` if the connection can and rebuilds the table otherwise, then refills it from the blobs (through the component's upgrade chain) inside one transaction. A database last
written without columns is refilled the same way the next time it is opened with them. Saves go to the store in groups (`Store::PutMany`); the SQL store sends sixteen rows per statement.

On 100,000 entities with ten fields each (file database, debug build): blob only saves in about 0.45 s, with columns in about 0.93 s (1.0 s with an index), and both load in about 0.6 s; a query for
one integer field answers 18,268 hits in 6.8 ms without an index and 0.5 ms with one.

## Settings and opening the storage

Every knob is a setting (`RegisterStorageSettings`, with `database::RegisterDatabaseSettings` for the connection): `storage.path` (the database file; empty keeps a private in-memory database),
`storage.layout` (`blob` or `columns`), `storage.autosave_seconds` (0 is off), the four limits `storage.max_record_bytes`, `storage.max_string_bytes`, `storage.max_array_elements` and
`storage.max_components_per_entity`, and the `database.*` group. Each has an environment variable (`ENGINE_STORAGE_*`) and a flag (`--storage-*`). `LocalStorage::OpenFromSettings(backend, settings,
components, &storage)` opens the connection, the SQL store and the `WorldStorage` from them; `storage` itself names no database backend, so the program passes the one it links.

## Asynchronous saves and loads

`WorldStorage::SaveAsync(scheduler, world, index, options, &handle)` is the same save as `Save`, split in two at the point where the world stops being read:

1. **The snapshot** runs before the call returns, on the calling thread (which must be the main thread of the scheduler): the entities to write (all of them, or for an incremental save the changed ones) are
   encoded into memory by the scheduler's workers, in rounds of `batch_size` entities, each worker filling its own slots. The stored state therefore does not depend on the number of workers or on how
   they were scheduled: records are written in key order, and the tests compare an asynchronous save with a synchronous one byte for byte over 100 random worlds with 1 to 8 participants.
2. **The write** is one job: it opens the store's batch, writes the records, removes the records of entities that are gone, writes the metadata and commits. It is the only user of the store until it
   has finished, so a second `SaveAsync` (or `Save`) on the same `WorldStorage` returns `Busy` until the first handle has been collected.

Because the world is not read after the call returns, **it may change freely while the writer works**: what is stored is the world as the call found it (a test changes the world five times while the
writer runs, under ThreadSanitizer). The handle is collected with `Wait` (main thread; it helps run jobs) or `Finish` once `IsDone` is true; `Finish` is also where an incremental save's `SaveSession`
learns the outcome, so call it before the next save with that session. The clocks of the session are those of the snapshot, and the world is advanced at the snapshot, so a write made after the call is
seen by the next incremental save. A handle destroyed without `Finish` waits for the writer and drops the session update: the next incremental save then does extra work, never less.

`SaveOptions::cancel` is checked between rounds of the snapshot and between groups of the write; a cancelled save leaves the store as it was (a cancel that arrives after the commit is a success).
`progress` is called from the calling thread during the snapshot and once from the writer job at the end.

`LoadAsync(scheduler, options, &handle)` runs the part that waits for the disk on a job: it copies the stored records (and the metadata the check needs) into memory. `AsyncLoad::Finish(world, index,
&report)` then decodes them and fills the world exactly as `Load` does, on the caller's thread, which owns the world; **the world is not touched until `Finish`**, and a failed or cancelled load leaves it as
it was. (The decoding itself stays on the calling thread: it creates entities.)

### Autosave

`Autosaver autosaver(storage, scheduler, world, index, seconds)` and `autosaver.Update(dt)` once a frame, from the main thread, at a point where no system is writing components. Every `seconds` it
starts an incremental `SaveAsync` with its own session; the cost on the frame is the encoding of the entities written since the last save (nothing for an idle world), and the writing happens on a job.
`Update` collects the previous save when it has finished and returns its error, if any; the next save is tried at the next interval. `SaveNow` and `Flush` start and collect one explicitly. The interval
is the caller's: read `storage.autosave_seconds` (`LocalStorage::AutosaveSeconds`), where 0 means the program does not autosave.

The tutorial [Save a World Without Stalling the Frame](../tutorials/saving-a-world.md) walks through all of this, and `game/storage_demo` runs it on a city with the spatial sets and compares the loaded world with the original.

## Errors

Every failure is a `storage::Status` value; nothing aborts on input. The codes and what a caller does:

| Code | Meaning | Usual response |
|---|---|---|
| `NotFound` | a component id or name, or a key, is not there | a registration bug, or a key to skip |
| `Exists`, `Duplicate` | an id, name or type registered twice; two entities with one key | fix the registration or the data |
| `Corrupt` | stored bytes are damaged (truncated, out-of-range value, bad UTF-8, a hash that does not match) | the file is not trusted; report it, keep the old one |
| `TooNew`, `TooOld` | the file was written by a newer format or component version, or one older than the registry can upgrade | update the program, or migrate offline |
| `LimitExceeded` | a length or count is above a `storage.max_*` limit | raise the limit only if the file is trusted |
| `Mismatch` | the registry disagrees with the manifest of the file | a renumbered or renamed component |
| `Invalid` | the caller broke a rule | fix the call |
| `Busy` | a batch or an asynchronous save is running | try again after it finishes |
| `Io` | the medium failed | the store holds the previous state; retry or tell the player |
| `Cancelled` | a `CancelToken` was set | nothing committed was undone, the old state stands |
| `MissingReference` | a reference names a key that is nowhere | load with `MissingPolicy::NullReference` if that is acceptable |
| `Unsupported` | the store or component does not offer it | check the layout |

## Untrusted input

**A save game that comes from outside the program (a download, another player, a file the user picked) is untrusted input.** The load path treats it so: the container is
bounds-checked byte by byte, every length and count is compared with the limits before memory is reserved (`storage.max_record_bytes`, `max_string_bytes`, `max_array_elements`,
`max_components_per_entity`), every record's hash is checked against its bytes, enums are range-checked, strings must be UTF-8, a parent chain is limited in depth, a load decodes and checks
everything before it touches the world, and any failure leaves the world as it was. `tests/storage/hostile_file_test.cpp` flips bytes, truncates, swaps and zeroes pages of a saved SQLite
file, offers other schemas, text and noise, and a record far above the limit: each case ends in a `Status` under ASan. What the library cannot do: tell a *valid* file with silly contents
(an entity at 1e30) from a good one, or make a file confidential. Set `database.integrity_check` to `quick` or `full` to have the database engine check its own structure when a file is opened
(`full` reads the whole file). Keep the limits low for files you do not control. Another process must not write the file while it is open (the file layer has no file locks, KNOWN-ISSUES.md).

## Adding a store

A store implements `storage::Store`: `Put`, `Get`, `Remove`, `Scan` (key order), `Count`, `Begin`/`Commit`/`Rollback` (all or nothing), `GetMetadata`/`SetMetadata`, and may override `PutMany` for a faster
group write. Nothing in `WorldStorage` changes. Run `tests/storage/store_conformance.h` against it (the same cases run on `MemoryStore` and `SqlStore`); set its capability switches honestly
(`kInjectsFailure`, `kCanCorrupt`, `kReadersSeeCommitted`). The writer is single; a reader on another thread may get `Busy` while a batch is open if the backend cannot show committed state.

## Adding a component

Register it once, at start-up, with a permanent `id`, a `name` and `fields` (`Field<&T::member>("name")`); never reuse or renumber an id. A change to the layout is a new `version` plus an
`upgrades` entry that turns the old bytes into the new; a record written by a newer version is `TooNew`, by one older than the oldest upgrade `TooOld`. A component that is derived each frame
(world transform) is flagged `kComponentDerived` and stored as presence only; one that must not be stored is `kComponentSkip`; one a query should find by field is `kComponentQueryable`. The
manifest of the file records what was registered, so a later program that renumbers by mistake gets `Mismatch` instead of wrong data.

## Threads

`Save` and `Load` use the calling thread. `SaveAsync` must be called from the scheduler's main thread and returns a handle; at most one save per `WorldStorage`. A `Store` is called by the
writer job and by nobody else while a save runs; `SqlStore` serializes on one connection and gives `Busy` to other threads while a batch is open. A world is read by one thread at a time
except in the snapshot of `SaveAsync`, where workers read it concurrently and nobody writes it.

## Measured

Release build, one machine (see the spec for the table and the pass conditions): 1,000,000 entities with six components save to SQLite in about 2.9 s and load in about 3.4 s; the column layout
saves in 2.1x the blob time; an idle incremental save takes under 1.1 ms; an incremental save of 1 percent in neighbouring entities takes 128 ms, but 1 percent spread at random touches almost every
chunk (1.2 s: the stamps are per chunk); `FindKeys` on an indexed field answers in 1.3 ms; the snapshot of an asynchronous save is 4.6x faster on 8 participants than on 1.

