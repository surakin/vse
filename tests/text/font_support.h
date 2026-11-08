#pragma once

#include "containers/vector.h"
#include "io/file.h"
#include "io/path.h"
#include "text/font.h"

// Shared helpers of the text tests: the shipped font's path and bytes.
namespace text_test {

inline engine_placeholder::io::Path FontPath() {
    return engine_placeholder::io::Path(engine_placeholder::StringView(TEXT_TEST_FONT_FILE));
}

/// The shipped font file's bytes (read once per call; the font is small).
inline engine_placeholder::Vector<engine_placeholder::byte> FontBytes() {
    engine_placeholder::Vector<engine_placeholder::byte> bytes;
    (void)engine_placeholder::io::ReadFile(FontPath(), &bytes);
    return bytes;
}

} // namespace text_test
