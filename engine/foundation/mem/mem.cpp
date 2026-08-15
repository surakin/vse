#include "mem/mem.h"

using engine_placeholder::u8;
using engine_placeholder::usize;

extern "C" void* memcpy(void* dest, const void* src, usize n) {
    auto* d = static_cast<u8*>(dest);
    const auto* s = static_cast<const u8*>(src);
    for (usize i = 0; i < n; ++i) {
        d[i] = s[i];
    }
    return dest;
}

extern "C" void* memset(void* dest, int c, usize n) {
    auto* d = static_cast<u8*>(dest);
    const u8 value = static_cast<u8>(c);
    for (usize i = 0; i < n; ++i) {
        d[i] = value;
    }
    return dest;
}

extern "C" void* memmove(void* dest, const void* src, usize n) {
    auto* d = static_cast<u8*>(dest);
    const auto* s = static_cast<const u8*>(src);
    if (d == s || n == 0) {
        return dest;
    }
    if (d < s) {
        for (usize i = 0; i < n; ++i) {
            d[i] = s[i];
        }
    } else {
        for (usize i = n; i > 0; --i) {
            d[i - 1] = s[i - 1];
        }
    }
    return dest;
}

extern "C" usize strlen(const char* s) {
    usize len = 0;
    while (s[len] != '\0') {
        ++len;
    }
    return len;
}
