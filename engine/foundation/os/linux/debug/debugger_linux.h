#pragma once

#include "types.h"

// Linux-only piece of debugger detection, exposed so the parser can be tested on its own.
namespace engine_placeholder::debug::detail {

// True if the text of /proc/self/status has a `TracerPid:` line with a non-zero pid.
bool StatusShowsTracer(const char* text, usize length);

} // namespace engine_placeholder::debug::detail
