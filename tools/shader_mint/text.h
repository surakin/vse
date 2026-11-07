#pragma once

// The text vocabulary of shader_mint outside its JSON boundary: the engine's `String`, `StringView` and `Vector`.
// (reflect_json.cpp is the one file that uses std::string and std::vector, because nlohmann/json is built on them.)

#include <cstdarg>

#include "containers/vector.h"
#include "mem/heap_allocator.h"
#include "string/string.h"
#include "types.h"

namespace engine_placeholder::shader_mint {

using Text = String<HeapAllocator>;

// Makes an owned text on the heap holding a copy of `view`.
inline Text MakeText(StringView view = StringView()) { return Text(HeapAllocator::Default(), view); }

// printf-style append to a text.
void Appendf(Text* out, const char* format, ...) __attribute__((format(printf, 2, 3)));

// Replaces `*out` with the formatted text (used for error messages).
void SetTextf(Text* out, const char* format, ...) __attribute__((format(printf, 2, 3)));

} // namespace engine_placeholder::shader_mint
