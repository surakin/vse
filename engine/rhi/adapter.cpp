#include "rhi/adapter.h"

#include <cstring>
#include <mutex>

namespace engine_placeholder::rhi {

namespace {

char Lower(char c) { return (c >= 'A' && c <= 'Z') ? static_cast<char>(c - 'A' + 'a') : c; }

bool ContainsIgnoreCase(const char* haystack, const char* needle) {
    for (usize i = 0; haystack[i] != '\0'; ++i) {
        usize j = 0;
        while (needle[j] != '\0' && haystack[i + j] != '\0' && Lower(haystack[i + j]) == Lower(needle[j])) {
            ++j;
        }
        if (needle[j] == '\0') {
            return true;
        }
    }
    return false;
}

bool EqualIgnoreCase(const char* a, const char* b) {
    usize i = 0;
    for (; a[i] != '\0' && b[i] != '\0'; ++i) {
        if (Lower(a[i]) != Lower(b[i])) {
            return false;
        }
    }
    return a[i] == b[i];
}

// Exactly 16 hexadecimal digits: the text form of a LUID.
bool IsLuidText(const char* text) {
    usize n = 0;
    for (; text[n] != '\0'; ++n) {
        const char c = Lower(text[n]);
        if (!((c >= '0' && c <= '9') || (c >= 'a' && c <= 'f')) || n == kAdapterLuidBytes - 1) {
            return false;
        }
    }
    return n == kAdapterLuidBytes - 1;
}

// Parses a non-empty all-digit string; returns -1 if it is not one.
long ParseIndex(const char* text) {
    long value = 0;
    if (text[0] == '\0') {
        return -1;
    }
    for (usize i = 0; text[i] != '\0'; ++i) {
        if (text[i] < '0' || text[i] > '9' || value > 100000) {
            return -1;
        }
        value = value * 10 + (text[i] - '0');
    }
    return value;
}

std::mutex g_null_adapters_mutex;
bool g_null_adapters_set = false;
AdapterList g_null_adapters;

void SetText(char* dst, usize capacity, const char* text) {
    std::strncpy(dst, text, capacity - 1);
    dst[capacity - 1] = '\0';
}

} // namespace

int MatchAdapter(const AdapterInfo* items, usize count, const char* request) {
    if (request == nullptr || request[0] == '\0') {
        return -1;
    }
    if (count > kMaxAdapters) {
        count = kMaxAdapters;
    }
    for (usize i = 0; i < count; ++i) {
        if (std::strcmp(items[i].id, request) == 0) {
            return static_cast<int>(i);
        }
    }
    if (IsLuidText(request)) {
        for (usize i = 0; i < count; ++i) {
            if (items[i].luid[0] != '\0' && EqualIgnoreCase(items[i].luid, request)) {
                return static_cast<int>(i);
            }
        }
        // A LUID changes on reboot: a stale one must fail, not fall through to the index rule ("0000000000000001" is all digits).
        return -1;
    }
    const long index = ParseIndex(request);
    if (index >= 0 && static_cast<usize>(index) < count) {
        return static_cast<int>(index);
    }
    for (usize i = 0; i < count; ++i) {
        if (ContainsIgnoreCase(items[i].name, request)) {
            return static_cast<int>(i);
        }
    }
    return -1;
}

void DefaultNullAdapters(AdapterList* out) {
    *out = AdapterList{};
    AdapterInfo& discrete = out->items[0];
    SetText(discrete.id, sizeof(discrete.id), "null-discrete");
    SetText(discrete.name, sizeof(discrete.name), "Null Discrete GPU");
    SetText(discrete.luid, sizeof(discrete.luid), "00000000000a0001");
    discrete.kind = AdapterKind::Discrete;
    discrete.dedicated_memory_bytes = 8ull << 30;
    SetText(discrete.driver, sizeof(discrete.driver), "null 1.0");
    discrete.suitable = true;
    AdapterInfo& integrated = out->items[1];
    SetText(integrated.id, sizeof(integrated.id), "null-integrated");
    SetText(integrated.name, sizeof(integrated.name), "Null Integrated GPU");
    SetText(integrated.luid, sizeof(integrated.luid), "00000000000a0002");
    integrated.kind = AdapterKind::Integrated;
    integrated.dedicated_memory_bytes = 0;
    SetText(integrated.driver, sizeof(integrated.driver), "null 1.0");
    integrated.suitable = true;
    out->count = 2;
}

void SetNullAdapters(const AdapterList* list) {
    std::lock_guard<std::mutex> lock(g_null_adapters_mutex);
    if (list == nullptr) {
        g_null_adapters_set = false;
        return;
    }
    g_null_adapters = *list;
    if (g_null_adapters.count > kMaxAdapters) {
        g_null_adapters.count = static_cast<u32>(kMaxAdapters);
    }
    g_null_adapters_set = true;
}

namespace detail {

// The null backend's enumeration; create_device.cpp dispatches here for Backend::Null.
void NullAdapters(AdapterList* out) {
    std::lock_guard<std::mutex> lock(g_null_adapters_mutex);
    if (g_null_adapters_set) {
        *out = g_null_adapters;
    } else {
        DefaultNullAdapters(out);
    }
}

} // namespace detail

} // namespace engine_placeholder::rhi
