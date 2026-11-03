#pragma once

#include "diagnostics.h"

namespace docgen {

// Reads the whole file at `path` into `arena` (which must have enough
// reserved capacity). Returns false on failure.
//
// Ownership: the bytes live in `arena` until it is reset or destroyed.
bool LoadSourceFile(StringView path, ArenaAllocator& arena, StringView& out);

} // namespace docgen
