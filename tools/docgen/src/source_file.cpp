#include "source_file.h"

#include "io/file.h"

namespace docgen {

bool LoadSourceFile(StringView path, ArenaAllocator& arena, StringView& out) {
    usize size = 0;
    engine_placeholder::u8* data = nullptr;
    engine_placeholder::io::Path file_path;
    if (!engine_placeholder::io::Path::Parse(path, engine_placeholder::io::NativePathStyle(), &file_path)) return false;
    if (engine_placeholder::io::ReadFile(file_path, arena, &data, &size).Failed()) return false;
    out = StringView(reinterpret_cast<const char*>(data), size);
    return true;
}

} // namespace docgen
