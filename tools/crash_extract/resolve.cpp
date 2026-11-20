#include "resolve.h"

#include "cstr/format.h"
#include "cstr/cstr.h"
#include "build_defs.h"
#include "types.h"
#include "cstr/mem_ops.h"

namespace crash_extract
{

namespace
{

bool IsHex(char c)
{
    return (c >= '0' && c <= '9') || (c >= 'a' && c <= 'f') || (c >= 'A' && c <= 'F');
}

char Upper(char c)
{
    return c >= 'a' && c <= 'z' ? static_cast<char>(c - 'a' + 'A') : c;
}

bool SameNameIgnoringCase(const char* a, const char* b)
{
    for (; *a != '\0' && *b != '\0'; ++a, ++b)
    {
        if (Upper(*a) != Upper(*b))
        {
            return false;
        }
    }
    return *a == *b;
}

}  // namespace

bool ParseFrame(const char* line, vse::usize length, unsigned* number, char* module, vse::usize module_capacity,
                u64* offset)
{
    if (length < 4 || line[0] != '#')
    {
        return false;
    }
    vse::usize i = 1;
    unsigned n = 0;
    vse::usize digits = 0;
    while (i < length && line[i] >= '0' && line[i] <= '9' && digits < 6)
    {
        n = n * 10 + static_cast<unsigned>(line[i] - '0');
        ++i;
        ++digits;
    }
    if (digits == 0 || i + 4 > length || line[i] != ' ' || line[i + 1] != ' ' || line[i + 2] != '0' ||
        line[i + 3] != 'x')
    {
        return false;
    }
    i += 4;
    while (i < length && IsHex(line[i]))
    {
        ++i;
    }
    if (i + 2 > length || line[i] != ' ' || line[i + 1] != ' ')
    {
        return false;
    }
    i += 2;
    // The module text runs to the last "+0x"; the offset after it is hex to the end of the line.
    vse::usize plus = length;
    for (vse::usize k = i; k + 3 <= length; ++k)
    {
        if (line[k] == '+' && line[k + 1] == '0' && line[k + 2] == 'x')
        {
            plus = k;
        }
    }
    if (plus == length || plus == i || plus + 3 == length)
    {
        return false;
    }
    u64 value = 0;
    for (vse::usize k = plus + 3; k < length; ++k)
    {
        if (!IsHex(line[k]) || k - plus - 3 >= 16)
        {
            return false;
        }
        const char c = line[k];
        value = value * 16 + static_cast<u64>(c <= '9' ? c - '0' : Upper(c) - 'A' + 10);
    }
    const vse::usize module_length = plus - i;
    if (module_length + 1 > module_capacity)
    {
        return false;
    }
    vse::MemCopy(module, line + i, module_length);
    module[module_length] = '\0';
    *number = n;
    *offset = value;
    return true;
}

const DumpModule* FindModule(const char* module_text, const DumpModule* modules, vse::usize count)
{
    const char* wanted = BaseName(module_text);
    for (vse::usize i = 0; i < count; ++i)
    {
        if (SameNameIgnoringCase(BaseName(modules[i].name), wanted))
        {
            return &modules[i];
        }
    }
    return nullptr;
}

bool SafeFileName(const char* name)
{
    if (name == nullptr || name[0] == '\0' || vse::CStrCompare(name, ".") == 0 || vse::CStrCompare(name, "..") == 0)
    {
        return false;
    }
    for (const char* p = name; *p != '\0'; ++p)
    {
        const vse::u8 c = static_cast<vse::u8>(*p);
        if (c < 0x20 || c == '/' || c == '\\' || c == ':' || c == '*' || c == '?' || c == '"' || c == '<' || c == '>' ||
            c == '|')
        {
            return false;
        }
    }
    return true;
}

bool StorePdbPath(const char* store, const DumpModule& module, char* out, vse::usize capacity)
{
    const char* pdb = BaseName(module.debug_file);
    if (!SafeFileName(pdb) || module.build_id[0] == '\0')
    {
        return false;
    }
    char id[sizeof(module.build_id)];
    vse::usize n = 0;
    for (const char* p = module.build_id; *p != '\0' && n + 1 < sizeof(id); ++p)
    {
        if (!IsHex(*p))
        {
            return false;
        }
        id[n++] = Upper(*p);
    }
    id[n] = '\0';
    const int written = vse::FormatTo(out, capacity, "%s\\%s\\%s\\%s", store, pdb, id, pdb);
    return written > 0 && static_cast<vse::usize>(written) < capacity;
}

#if !ENGINE_PLATFORM_WINDOWS
int ResolveReport(const char*, vse::usize, const DumpModule*, vse::usize, const char*)
{
    return -1;  // Linux names frames with lldb or minidump-stackwalk (scripts/crash-report.sh)
}
#endif

}  // namespace crash_extract
