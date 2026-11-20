#pragma once

//! The glTF node graph: transforms, parents and the products of a chain of nodes.
//!
//! Phase 22 Day 337. Skeletons (Day 337) and levels (Day 338) read the `nodes` array through this: each node's local
//! matrix, its parent (with cycles and nodes of two parents refused), and the decomposition of a matrix into
//! translation, rotation and scale, which refuses what those three cannot hold (shear, a degenerate scale).

#include "asset_import/gltf_container.h"
#include "containers/array.h"
#include "containers/vector.h"

namespace vse::asset
{

/// The value of a node's parent, mesh or skin when it has none.
inline constexpr u32 kNoGltfIndex = 0xFFFFFFFFu;

/// A 4x4 matrix in double precision, column-major like glTF (`m[column * 4 + row]`).
///
/// - Thread-safety: thread-compatible plain data.
struct GltfMatrix
{
    Array<f64, 16> m = {{1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1}};  ///< The elements.
};

/// A transform as translation, rotation (a unit quaternion x, y, z, w) and scale.
///
/// - Thread-safety: thread-compatible plain data.
struct GltfTrs
{
    Array<f64, 3> translation = {{0, 0, 0}};  ///< Translation.
    Array<f64, 4> rotation = {{0, 0, 0, 1}};  ///< Unit quaternion x, y, z, w.
    Array<f64, 3> scale = {{1, 1, 1}};        ///< Scale (one component may be negative: a reflection).
};

/// One glTF node.
///
/// - Thread-safety: thread-compatible plain data.
/// - Ownership: `name` borrows the document.
struct GltfNode
{
    StringView name;            ///< Its name (may be empty); borrowed from the document.
    GltfMatrix local;           ///< Its local matrix (`matrix`, or the product of `translation`, `rotation`, `scale`).
    u32 parent = kNoGltfIndex;  ///< The index of the node that lists it as a child, or `kNoGltfIndex`.
    u32 mesh = kNoGltfIndex;    ///< Its `mesh`, or `kNoGltfIndex`.
    u32 skin = kNoGltfIndex;    ///< Its `skin`, or `kNoGltfIndex`.
};

/// The nodes of a glTF.
///
/// - Thread-safety: thread-compatible.
/// - Ownership: borrows the document its names point into.
struct GltfNodeGraph
{
    Vector<GltfNode> nodes;  ///< One per `nodes` entry.
};

/// Reads the `nodes` array: transforms, parents, mesh and skin references.
///
/// - Param document: a parsed glTF.
/// - Param out: replaced by the graph.
/// - Param error: receives the sentence naming the node.
/// - Fails: `Invalid` (a transform that is not finite or gives both `matrix` and a TRS member, a child index outside
///   the nodes, a node with two parents, a cycle, a mesh or skin index outside its array).
/// - Ownership: borrows `document`, which must outlive `*out`.
Status ReadNodeGraph(const GltfDocument& document, GltfNodeGraph* out, GltfError* error);

/// The product `a * b` (apply `b` first).
///
/// - Fails: never.
GltfMatrix MultiplyMatrices(const GltfMatrix& a, const GltfMatrix& b);

/// The inverse of an affine matrix (glTF coverage maintenance, 2026-11-20: the skin space of a skinned mesh).
///
/// - Param matrix: the matrix (its last row must be 0 0 0 1).
/// - Param out: receives the inverse.
/// - Returns: false when the matrix has a projective part or its 3x3 part has a determinant of almost 0 (below 1e-300
///   in magnitude), or the inverse is not finite.
/// - Fails: reported by the return value.
/// - Ownership: borrows its arguments for the call only; nothing is retained.
bool InvertAffineMatrix(const GltfMatrix& matrix, GltfMatrix* out);

/// The matrix of a translation, rotation and scale.
///
/// - Fails: never.
GltfMatrix MatrixFromTrs(const GltfTrs& trs);

/// Splits an affine matrix into translation, rotation and scale.
///
/// - Param matrix: the matrix.
/// - Param out: receives the parts.
/// - Returns: false when the matrix has a projective part, a column of zero length, or a shear (columns that are not
///   perpendicular within 1e-6).
/// - Fails: reported by the return value.
/// - Ownership: borrows its arguments for the call only; nothing is retained.
bool DecomposeMatrix(const GltfMatrix& matrix, GltfTrs* out);

}  // namespace vse::asset
