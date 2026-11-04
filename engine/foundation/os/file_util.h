#pragma once

#include "mem/arena_allocator.h"
#include "string/string.h"
#include "types.h"

//! File utilities built only on the portable primitives of `os/os.h`.

namespace engine_placeholder::os {

/// Reads the whole file at `path` into a buffer allocated from `arena`.
/// Param path: UTF-8 file path.
/// Param arena: arena the buffer is allocated from.
/// Param out_size: set to the file's byte length on success; untouched on failure.
/// Returns: the buffer holding the file contents, or `nullptr` on failure.
/// Fails: returns `nullptr` if the file cannot be opened, a seek or read fails, or the arena is full.
/// Ownership: the buffer belongs to `arena` and lives until the arena is reset or destroyed.
u8* ReadFileToArena(StringView path, ArenaAllocator& arena, usize& out_size);

} // namespace engine_placeholder::os
