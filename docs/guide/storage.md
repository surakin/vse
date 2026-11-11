# Storage: components that survive a save

Status: Phase 19. Built: the component registry and the canonical encoding (Day 256), versions, upgrades, custom codecs, limits and hostile-input tests
(Day 257). Next: stable identities (258), the record store (259), saving and loading a world (260-261), the SQL store (262-263). Design:
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
