#include "renderer/shader_permutation.h"

#include <cstdio>
#include <cstring>

namespace engine_placeholder::renderer {

const char* ValidateShaderPermutations(const ShaderPermutations& t) {
    if (t.option_count > kMaxPermutationOptions || t.entry_count > kMaxPermutationKeys) {
        return "permutations: more options or keys than the limits allow";
    }
    if (t.entry_count == 0 || t.module_count == 0 || t.modules == nullptr || t.entries == nullptr ||
        (t.option_count != 0 && t.options == nullptr)) {
        return "permutations: the table is empty or has a null array";
    }
    if (t.module_count > t.entry_count) {
        return "permutations: more modules than keys";
    }
    for (u32 i = 0; i < t.option_count; ++i) {
        if (t.options[i] == nullptr || t.options[i][0] == '\0') {
            return "permutations: an option name is null or empty";
        }
        for (u32 j = 0; j < i; ++j) {
            if (std::strcmp(t.options[i], t.options[j]) == 0) {
                return "permutations: two options have the same name";
            }
        }
    }
    const u32 valid_bits = t.option_count == 32 ? 0xffffffffu : (1u << t.option_count) - 1u;
    for (u32 i = 0; i < t.entry_count; ++i) {
        const PermutationEntry& e = t.entries[i];
        if ((e.key_bits & ~valid_bits) != 0) {
            return "permutations: a key switches on an option the shader does not declare";
        }
        if (e.module >= t.module_count) {
            return "permutations: a key names a module that does not exist";
        }
        if (i > 0 && t.entries[i - 1].key_bits >= e.key_bits) {
            return "permutations: keys are not sorted and unique";
        }
    }
    if (t.entries[0].key_bits != 0 || t.entries[0].module != 0) {
        return "permutations: the base key (no option on) must be present and use module 0";
    }
    for (u32 m = 0; m < t.module_count; ++m) {
        if (t.modules[m].words == nullptr || t.modules[m].word_count == 0 || t.modules[m].reflection == nullptr ||
            t.modules[m].reflection_size == 0) {
            return "permutations: a module has no SPIR-V or no reflection";
        }
    }
    return nullptr;
}

bool MakePermutationKey(const ShaderPermutations& table, const char* const* names, u32 count, PermutationKey* out,
                        ReflectionError* error) {
    *out = PermutationKey{};
    for (u32 n = 0; n < count; ++n) {
        bool found = false;
        for (u32 i = 0; i < table.option_count && i < kMaxPermutationOptions; ++i) {
            if (std::strcmp(table.options[i], names[n]) == 0) {
                out->bits |= 1u << i;
                found = true;
                break;
            }
        }
        if (!found) {
            if (error != nullptr) {
                std::snprintf(error->message, kReflectionErrorLength, "shader '%s' has no option '%s'", table.shader, names[n]);
            }
            return false;
        }
    }
    return true;
}

void DescribePermutationKey(const ShaderPermutations& table, PermutationKey key, char* out, usize capacity) {
    if (capacity == 0) {
        return;
    }
    usize used = 0;
    auto put = [&](const char* text) {
        for (; *text != '\0' && used + 1 < capacity; ++text) {
            out[used++] = *text;
        }
    };
    put("{");
    bool first = true;
    for (u32 i = 0; i < kMaxPermutationOptions; ++i) {
        if (!key.Has(i)) {
            continue;
        }
        if (!first) {
            put(", ");
        }
        first = false;
        if (i < table.option_count && table.options != nullptr) {
            put(table.options[i]);
        } else {
            put("?");
        }
    }
    put("}");
    out[used] = '\0';
}

const PermutationModule* FindPermutation(const ShaderPermutations& table, PermutationKey key, ReflectionError* error) {
    const u32 count = table.entry_count < kMaxPermutationKeys ? table.entry_count : kMaxPermutationKeys;
    const bool usable = table.entries != nullptr && table.modules != nullptr;
    for (u32 i = 0; usable && i < count; ++i) {
        if (table.entries[i].key_bits == key.bits) {
            if (table.entries[i].module < table.module_count) {
                return &table.modules[table.entries[i].module];
            }
            if (error != nullptr) {
                std::snprintf(error->message, kReflectionErrorLength, "shader '%s': the permutation table is corrupt (a key names module %u of %u)",
                              table.shader, table.entries[i].module, table.module_count);
            }
            return nullptr;
        }
    }
    if (error != nullptr) {
        char described[160];
        DescribePermutationKey(table, key, described, sizeof(described));
        std::snprintf(error->message, kReflectionErrorLength, "shader '%s' has no permutation %s (add the key to its .perm manifest)",
                      table.shader, described);
    }
    return nullptr;
}

} // namespace engine_placeholder::renderer
