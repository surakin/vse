#pragma once

//! The reason an import step refused its input.
//!
//! Every function of the importer that reads a file
//! returns a [[asset::Status]] and fills a [[asset::GltfError]] with a sentence that names the glTF object
//! (`meshes[2].primitives[0].attributes: ...`), so a host can show it.

#include "asset/status.h"
#include "containers/array.h"
#include "string_view.h"
#include "types.h"

namespace engine_placeholder::asset
{

/// The reason an import refused its input, with the glTF object it names.
///
/// - Thread-safety: thread-compatible plain data.
struct GltfError
{
    /// A sentence: `OBJECT: what is wrong`, NUL-terminated (cut when it does not fit).
    Array<char, 192> message = {};

    /// Sets the message to `where: what` (or to `what` alone when `where` is empty).
    void Set(StringView where, StringView what);
    /// Sets the message to `what` followed by a number (`... 3` for the offending value).
    void SetNumber(StringView where, StringView what, u64 number);
};

/// The text of a refusal, for a log: `error` when set, else the status's own message.
///
/// - Ownership: the result is borrowed from the arguments.
/// - Fails: never.
inline const char* GltfMessage(const GltfError& error, const Status& status)
{
    return error.message[0] != '\0' ? error.message.Data() : status.Message();
}

}  // namespace engine_placeholder::asset
