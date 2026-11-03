#include "source_file.h"

#include "os/file_util.h"

namespace docgen {

bool LoadSourceFile(StringView path, ArenaAllocator& arena, StringView& out) {
    usize size = 0;
    engine_placeholder::u8* data = engine_placeholder::os::ReadFileToArena(path, arena, size);
    if (data == nullptr) return false;
    out = StringView(reinterpret_cast<const char*>(data), size);
    return true;
}

} // namespace docgen
