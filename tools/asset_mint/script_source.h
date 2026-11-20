#pragma once

//! The text source form of a script (`.script.src`) and the script minter. Phase 27 Day 462. An offline tool's code:
//! never linked into the engine; its tests compile it directly.
//!
//! Line-oriented text; `#` starts a comment; fields are separated by spaces or tabs:
//!
//!     id 0123456789abcdef
//!     lua player.lua
//!     dep assets.weapon 00000000000000a1
//!
//! `id` comes first. `lua` names the Lua file, relative to the source. Each `dep` names a module as the script writes
//! it in `require("assets.weapon")` and gives the resource id of that module's script. The minter rewrites every
//! `require("name")` to `__require("<id>")`, so a script depends on ids, never on file paths.

#include "asset/resource_id.h"
#include "asset/resource_type.h"
#include "asset/script_blob.h"
#include "asset/status.h"
#include "containers/array.h"
#include "containers/span.h"
#include "containers/vector.h"
#include "mem/heap_allocator.h"
#include "script/backend.h"
#include "string/string.h"
#include "string_view.h"
#include "types.h"

namespace vse::asset
{

/// One `dep` line: a module name and the id of the script that provides it.
///
/// - Thread-safety: thread-compatible plain data.
/// - Ownership: owns the name.
struct ScriptDependency
{
    String<HeapAllocator> name{HeapAllocator::Default()};  ///< The module name as `require` spells it.
    ResourceId id;                                         ///< The resource id of the module's script.
};

/// A parsed `.script.src`.
///
/// - Thread-safety: thread-compatible.
/// - Ownership: owns its strings.
struct ScriptSource
{
    ResourceId id;                                        ///< The resource id from the `id` line.
    String<HeapAllocator> lua{HeapAllocator::Default()};  ///< The Lua file's path as written.
    Vector<ScriptDependency> dependencies;                ///< The `dep` lines, in file order.
};

/// Where and why a script source or script failed to mint.
///
/// - Thread-safety: thread-compatible plain data.
struct ScriptSourceError
{
    u32 line = 0;                   ///< 1-based line number (of the `.script.src` or of the Lua text); 0 for the whole.
    Array<char, 200> message = {};  ///< A sentence.
};

/// Sets `error` to `message` with no line (for loaders).
///
/// - Fails: never.
void SetScriptError(ScriptSourceError* error, const char* message);

/// Parses a `.script.src`.
///
/// - Param text: the file's text; borrowed for the call.
/// - Param out: receives the source; replaced.
/// - Param error: receives the failure.
/// - Fails: returns `Invalid` with the line and a sentence for a malformed line, a repeated or misplaced `id`, a
/// missing `lua` line, a repeated dependency name or id, or a name that is not a dotted identifier.
Status ParseScriptSource(StringView text, ScriptSource* out, ScriptSourceError* error);

/// What a static scan of a Lua text found.
///
/// - Thread-safety: thread-compatible plain data.
/// - Ownership: owns the names.
struct ScriptScan
{
    /// One `require` call with a literal module name.
    struct Require
    {
        String<HeapAllocator> name{HeapAllocator::Default()};  ///< The module name.
        usize begin = 0;                                       ///< Where the call starts in the text.
        usize end = 0;                                         ///< One past the call's last character.
        u32 line = 0;                                          ///< Its line.
    };

    Vector<Require> requires_;                               ///< The literal `require` calls, in text order.
    String<HeapAllocator> banned{HeapAllocator::Default()};  ///< The first banned name used ("" when none).
    u32 banned_line = 0;                                     ///< Its line.
    u32 dynamic_require_line = 0;                            ///< The line of the first `require` with no literal name.
};

/// Scans a Lua text without running it: finds the `require` calls with a literal name, the first use of a library the
/// sandbox does not give (`io`, `os`, `debug`, `package`, `ffi`, `jit`, `load`, `loadstring`, `loadfile`, `dofile`,
/// `setfenv`, `getfenv`, `module`, `newproxy`, `string.dump`) and a `require` whose argument is not a literal. Names
/// after a `.` or `:` are fields, not globals, and are not flagged. Comments and strings are skipped.
///
/// - Param text: the Lua text; borrowed for the call.
/// - Param out: receives the findings; replaced.
/// - Fails: never; a text that does not parse is the compiler's to report.
void ScanScriptText(StringView text, ScriptScan* out);

/// Normalizes a source text: removes a UTF-8 byte order mark, turns `\r\n` and `\r` into `\n`.
///
/// - Param text: the raw bytes; borrowed for the call.
/// - Param out: receives the text; replaced.
/// - Param error: receives the failure.
/// - Fails: returns `Invalid` for a NUL byte, text that is not valid UTF-8, or more than [[kMaxScriptSourceBytes]].
Status NormalizeScriptText(StringView text, String<HeapAllocator>* out, ScriptSourceError* error);

/// What the minter needs besides the source.
///
/// - Thread-safety: thread-compatible.
/// - Ownership: borrows the backend.
struct ScriptMintOptions
{
    /// The language used to validate (and, with `bytecode`, to compile); not null.
    script::Backend* backend = nullptr;
    /// Whether to add the bytecode section (needs a backend that can compile).
    bool bytecode = false;
    /// Whether to run the source once in a scratch state to learn its behaviors and check their callbacks.
    bool check_entries = true;
};

/// Mints a script: normalizes `script_text`, checks it statically, rewrites its `require` calls to ids, compiles it to
/// check the syntax, runs it once in a scratch state with stub `require` and `behavior` to learn the behaviors it
/// defines and to check that every lifecycle callback is a function, and writes the blob.
///
/// - Param source: the parsed `.script.src`.
/// - Param script_text: the contents of the Lua file; borrowed for the call.
/// - Param types: the type registry.
/// - Param options: how to validate.
/// - Param out: receives the blob; replaced.
/// - Param error: receives the failure.
/// - Param target: the target version for the container (0: the current one).
/// - Fails: returns `Invalid` with the line and a sentence for a text that is not valid, uses a banned name, requires
/// an undeclared module (or declares one it never requires), does not compile, fails when run once, or has a lifecycle
/// callback that is not a function.
Status MintScript(const ScriptSource& source, StringView script_text, const TypeRegistry& types,
                  const ScriptMintOptions& options, Vector<byte>* out, ScriptSourceError* error, u32 target = 0);

/// Finds the source of the module `name`, which must have the resource id `expected`.
///
/// - Param context: the caller's pointer.
/// - Param name: the module name from the `dep` line.
/// - Param expected: the id the `dep` line gave.
/// - Param out: receives the dependency's parsed source.
/// - Param error: receives the failure.
/// - Returns: true when the module was found and its id is `expected`.
using ScriptLoader = bool (*)(void* context, StringView name, ResourceId expected, ScriptSource* out,
                              ScriptSourceError* error);

/// Walks the dependencies of `root` through `loader` and fails on a missing dependency or a cycle, printing the chain
/// (`a -> b -> a`) into the error.
///
/// - Param root_name: the name the root is known by in a chain.
/// - Param root: the root source.
/// - Param loader: finds a dependency.
/// - Param context: passed to `loader`.
/// - Param error: receives the failure.
/// - Param order: receives the ids of every script reached (the root last, each after its dependencies), or null.
/// - Fails: returns `Invalid` for a dependency `loader` cannot find (the error names the chain) or a cycle.
Status CheckScriptGraph(StringView root_name, const ScriptSource& root, ScriptLoader loader, void* context,
                        ScriptSourceError* error, Vector<ResourceId>* order);

}  // namespace vse::asset
