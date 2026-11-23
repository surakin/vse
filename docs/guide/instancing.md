# Instancing

An instanced mesh is many copies of one mesh and one material, each with its own place, held by one owner instead of one entity each. The pieces are in the renderer module; this page is the map between them.

## The instance set

`renderer::InstanceSet` (`renderer/instance_set.h`) holds the instances. It is made by `InstanceSet::Create` and owned by a `UniquePtr` (`InstanceSetPtr`); a description (`InstanceSetDesc`) says whether every instance carries data, the most instances (at most `kMaxInstances`, 2^24, so an index is exact in an `f32`), the size of a Morton cell and how far an instance may move before it changes chunk.

An instance is a position (`f64`, in the world), a unit rotation and a scale on each axis (`InstanceTransform`). It is stored as a 40-byte `InstanceRecord`: the position as an `f32` offset from its chunk's `f64` origin, the rotation and the scale. A set made with data also keeps, parallel to the records, an `InstanceExtra` per instance: a colour tint and a `u32` custom value (20 bytes).

Instances live in chunks of up to 256 (`kInstanceChunkSize`). A chunk has an `InstanceChunkInfo`: its origin, the exact box of its instances' positions (offsets from the origin), the largest scale component, the count, the index of its first record (`chunk * 256`) and a version. The version is a number from one counter for the whole set, set on every edit of the chunk, so an upload copies exactly the chunks whose version differs from the one it saw. A culler inflates a chunk's box by the mesh's radius times `max_scale`.

Because the origin is an `f64` and the offsets are `f32`, a set placed 10,000 km from the world origin stores the same offsets as one at the origin; a chunk is exact as long as its instances are near one another (the box is in offsets, never in world coordinates).

## Edits

Every edit returns an `InstanceStatus` and never aborts on a bad value:

- `Add(transform, data, &id)` puts the instance in the chunk with the nearest Morton key (a new chunk when that one and its neighbour are full). `Full` at the set's capacity, `InvalidValue` for a value that is not finite, a coordinate beyond `kMaxInstanceCoordinate` or a scale not above 0.
- `Remove(id)` moves the chunk's last instance into the hole and refits the box; an empty chunk is released and reused. Other ids stay valid.
- `SetTransform(id, t)` keeps the instance in its chunk and grows (or, if it was on the box's edge, refits) the box, unless the new place is beyond the box by more than the move threshold: then it joins the chunk its new key selects.
- `SetData(id, data)` changes the tint and the custom value (`NoData` for a set without them).
- `Rebuild()` sorts everything by (Morton key, id) with a stable sort and cuts new full chunks; ids and contents are unchanged and the layout depends only on the content. `Clear()` removes everything.

An `InstanceId` is a table index and a generation: removing an instance, or clearing the set, makes its id `StaleId` for good, even when the table entry is reused.

## Cost

Edits never repack chunks: a chunk emptied by removals is reused, but one left with a few instances stays, and an instance moved far from every other starts a chunk of its own. Each chunk reserves room for 256 records, so call `Rebuild` after heavy editing of a scattered set to pack them again.

A record is 40 bytes; with the id table entry and the back reference a set costs about 60 bytes an instance without data and 80 with (`MemoryBytes()` reports the reserved bytes). Moving an instance within its chunk refits at most 256 records.
