#pragma once

//! Names, paths, ids and report lines shared by the importers of each resource kind.
//!
//! Phase 22 Days 336-337. Meshes, skeletons and clips each get a resource path `<prefix>/<kind>/<name>` made from the
//! glTF object's name, an id that an earlier import's manifest keeps, and report lines that name the glTF object.

#include "asset_import/gltf_import.h"
#include "containers/span.h"
#include "containers/vector.h"

namespace vse::asset
{

/// Cleans a glTF name into one resource-path segment: `/`, `\`, control characters become `_`, `.` and `..` become `_`.
///
/// - Param name: the name as written.
/// - Param out: replaced by the segment (empty for an empty name).
/// - Ownership: borrows its arguments for the call only; nothing is retained.
/// - Fails: never.
void CleanPathSegment(StringView name, String<HeapAllocator>* out);

/// Makes the names distinct: a name seen before gets `_<position>` appended, and what still clashes is settled one by
/// one, so the result is always distinct.
///
/// - Param names: changed in place.
/// - Ownership: borrows its arguments for the call only; nothing is retained.
/// - Fails: never.
void MakeNamesUnique(Vector<String<HeapAllocator>>* names);

/// The prefix the resource paths of an import start with: `options.path_prefix` (default `/gltf`) without a trailing
/// `/`.
///
/// - Ownership: the result is owned by the caller.
/// - Fails: never.
String<HeapAllocator> ImportPathPrefix(const ImportOptions& options);

/// The ids of a set of objects of one kind.
///
/// The manifest's where it has the object, a new one from the id source (`options.ids`, or random ids when null) for
/// the rest.
///
/// - Param options: the manifest and the id source.
/// - Param kind: the kind of every object.
/// - Param objects: the objects, by index in the glTF and name.
/// - Param ids: replaced by one id per object, in order.
/// - Ownership: borrows its arguments for the call only; nothing is retained.
/// - Fails: never.
void AssignIds(const ImportOptions& options, ImportKind kind, Span<const ManifestObject> objects,
               Vector<ResourceId>* ids);

/// Appends a warning to a report: counts it and keeps its sentence (the first 256 lines are kept).
///
/// - Param report: the report.
/// - Param where: the glTF object.
/// - Param what: what was dropped or approximated.
/// - Ownership: borrows its arguments for the call only; nothing is retained.
/// - Fails: never.
void AddImportWarning(ImportReport* report, StringView where, StringView what);

/// Appends a line to a report without counting a warning (a rename, a measured error).
///
/// - Param report: the report.
/// - Param where: the glTF object.
/// - Param what: the line.
/// - Ownership: borrows its arguments for the call only; nothing is retained.
/// - Fails: never.
void AddImportNote(ImportReport* report, StringView where, StringView what);

}  // namespace vse::asset
