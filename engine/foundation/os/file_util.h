#pragma once

#include "mem/arena_allocator.h"
#include "string/string.h"
#include "types.h"

// Higher-level file utilities built purely from os.h's portable
// primitives (Open/Lseek/Read/Close) — unlike os.h/os/linux/, this file
// needs no per-platform implementation, so it's declared and defined
// once (Phase 1 Day 16).
namespace engine_placeholder::os {

// Reads the whole file at `path` into a buffer allocated from `arena`.
// Returns nullptr on any failure (missing file, seek/read error, or the
// arena running out of space) and leaves `out_size` untouched; on
// success `out_size` is set to the file's byte length.
u8* ReadFileToArena(StringView path, ArenaAllocator& arena, usize& out_size);

} // namespace engine_placeholder::os
