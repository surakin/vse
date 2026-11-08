// The child d3d12_conventions_test.cpp starts through shader_mint's RunProcess: writes each argument after the first, one per line,
// into the file the first argument names; `--exit-3` alone exits with status 3 instead.
#include <cstring>

#include "io/file.h"
#include "io/file_system.h"
#include "io/path.h"
#include "string/string.h"
#include "mem/heap_allocator.h"

using namespace engine_placeholder;

int main(int argc, char** argv) {
    if (argc == 2 && std::strcmp(argv[1], "--exit-3") == 0) {
        return 3;
    }
    if (argc < 2) {
        return 2;
    }
    String<HeapAllocator> text(HeapAllocator::Default(), StringView());
    for (int i = 2; i < argc; ++i) {
        text.Append(StringView(argv[i]));
        text.Append('\n');
    }
    io::Path out;
    if (!io::Path::Parse(StringView(argv[1]), io::NativePathStyle(), &out)) {
        return 2;
    }
    return io::WriteFile(out, AsByteSpan(text.View())).Ok() ? 0 : 1;
}
