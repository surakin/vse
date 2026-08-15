#pragma once

#include "types.h"

// Hand-written replacements for the C library's mem/string primitives
// (PLAN.md assumption #2 — kept hand-rolled regardless of the libc
// linkage decision). Declared with C linkage and the standard
// signatures so the compiler's implicitly generated calls (struct
// copies, array init, etc.) resolve to these instead of libc's.
extern "C" {

void* memcpy(void* dest, const void* src, engine_placeholder::usize n);
void* memset(void* dest, int c, engine_placeholder::usize n);
void* memmove(void* dest, const void* src, engine_placeholder::usize n);
engine_placeholder::usize strlen(const char* s);

} // extern "C"
