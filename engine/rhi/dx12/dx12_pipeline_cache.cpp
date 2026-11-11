// The Direct3D 12 pipeline cache file's rules (Phase 13 Day 167).

#include "rhi/dx12/dx12_pipeline_cache.h"

#include "cstr/mem_ops.h"
#include "cstr/cstr.h"

namespace engine_placeholder::rhi::dx12
{

namespace
{

constexpr byte kMagic[8] = {'E', 'N', 'G', 'P', 'L', 'I', 'B', '1'};
constexpr u32 kFormatVersion = 2;  // 2: the key section

void PutU32(byte* p, u32 v)
{
    for (u32 i = 0; i < 4; ++i)
    {
        p[i] = static_cast<byte>(v >> (8 * i));
    }
}
void PutU64(byte* p, u64 v)
{
    for (u32 i = 0; i < 8; ++i)
    {
        p[i] = static_cast<byte>(v >> (8 * i));
    }
}
u32 GetU32(const byte* p)
{
    u32 v = 0;
    for (u32 i = 0; i < 4; ++i)
    {
        v |= static_cast<u32>(p[i]) << (8 * i);
    }
    return v;
}
u64 GetU64(const byte* p)
{
    u64 v = 0;
    for (u32 i = 0; i < 8; ++i)
    {
        v |= static_cast<u64>(p[i]) << (8 * i);
    }
    return v;
}

bool Append(char* out, usize capacity, usize* pos, const char* text, bool dots_to_underscores = false)
{
    for (; *text != '\0'; ++text)
    {
        if (*pos + 1 >= capacity)
        {
            return false;
        }
        const char c = *text;
        out[(*pos)++] = dots_to_underscores && (c == '.' || c == '/' || c == '\\' || c == ':') ? '_' : c;
    }
    return true;
}

}  // namespace

u64 Fnv1a(const void* data, usize size, u64 seed)
{
    const byte* p = static_cast<const byte*>(data);
    u64 h = seed;
    for (usize i = 0; i < size; ++i)
    {
        h ^= p[i];
        h *= 0x100000001b3ull;
    }
    return h;
}

u64 PipelineLibraryIdentity(const char* adapter_id, const char* driver)
{
    u64 h = Fnv1a(adapter_id, CStrLength(adapter_id));
    const byte separator = 0;
    h = Fnv1a(&separator, 1, h);
    return Fnv1a(driver, CStrLength(driver), h);
}

void WritePipelineLibraryHeader(byte* out, u64 identity, u32 key_count, const void* payload, usize payload_size)
{
    // magic (8), format version (4), key count (4), identity (8), payload size (8), payload FNV-1a (8): 40 bytes,
    // little-endian.
    MemCopy(out, kMagic, 8);
    PutU32(out + 8, kFormatVersion);
    PutU32(out + 12, key_count);
    PutU64(out + 16, identity);
    PutU64(out + 24, payload_size);
    PutU64(out + 32, Fnv1a(payload, payload_size));
}

const char* CheckPipelineLibraryFile(const void* data, usize size, u64 identity, PipelineLibraryParts* parts)
{
    const byte* p = static_cast<const byte*>(data);
    if (size < kPipelineLibraryHeaderBytes)
    {
        return "shorter than its header";
    }
    if (MemCompare(p, kMagic, 8) != 0)
    {
        return "not a pipeline library file";
    }
    if (GetU32(p + 8) != kFormatVersion)
    {
        return "written by another format version";
    }
    if (GetU64(p + 16) != identity)
    {
        return "for another adapter or driver";
    }
    if (GetU64(p + 24) != size - kPipelineLibraryHeaderBytes)
    {
        return "truncated, or longer than its header says";
    }
    if (GetU64(p + 32) != Fnv1a(p + kPipelineLibraryHeaderBytes, size - kPipelineLibraryHeaderBytes))
    {
        return "damaged (its contents do not match their hash)";
    }
    const u32 key_count = GetU32(p + 12);
    const u64 key_bytes = u64{key_count} * 8;
    if (key_bytes >= size - kPipelineLibraryHeaderBytes)
    {
        return "its key section leaves no room for the library";
    }
    if (parts != nullptr)
    {
        parts->keys = p + kPipelineLibraryHeaderBytes;
        parts->key_count = key_count;
        parts->library = p + kPipelineLibraryHeaderBytes + key_bytes;
        parts->library_size = size - kPipelineLibraryHeaderBytes - static_cast<usize>(key_bytes);
    }
    return nullptr;
}

u64 PipelineLibraryKey(const PipelineLibraryParts& parts, u32 index)
{
    return GetU64(parts.keys + static_cast<usize>(index) * 8);
}

namespace
{

usize LowerBound(const Vector<u64>& keys, u64 key)
{
    usize lo = 0, hi = keys.Size();
    while (lo < hi)
    {
        const usize mid = lo + (hi - lo) / 2;
        if (keys[mid] < key)
        {
            lo = mid + 1;
        }
        else
        {
            hi = mid;
        }
    }
    return lo;
}

}  // namespace

bool InsertSortedKey(Vector<u64>* keys, u64 key)
{
    const usize at = LowerBound(*keys, key);
    if (at < keys->Size() && (*keys)[at] == key)
    {
        return false;
    }
    keys->Push(key);
    for (usize i = keys->Size() - 1; i > at; --i)
    {
        (*keys)[i] = (*keys)[i - 1];
    }
    (*keys)[at] = key;
    return true;
}

bool HasSortedKey(const Vector<u64>& keys, u64 key)
{
    const usize at = LowerBound(keys, key);
    return at < keys.Size() && keys[at] == key;
}

bool BuildPipelineLibraryPath(const char* dir, const char* adapter_id, const char* driver, char* out, usize capacity)
{
    if (capacity == 0)
    {
        return false;
    }
    usize pos = 0;
    bool ok = Append(out, capacity, &pos, dir);
    if (ok && pos > 0 && out[pos - 1] != '/' && out[pos - 1] != '\\')
    {
        ok = Append(out, capacity, &pos, "/");
    }
    ok = ok && Append(out, capacity, &pos, "pipelines_") && Append(out, capacity, &pos, adapter_id, true) &&
         Append(out, capacity, &pos, "_") &&
         Append(out, capacity, &pos, driver[0] != '\0' ? driver : "nodriver", true) &&
         Append(out, capacity, &pos, ".bin");
    out[ok ? pos : 0] = '\0';
    return ok;
}

void PipelineLibraryName(u64 key, wchar_t (&out)[18])
{
    static const char kHex[] = "0123456789abcdef";
    out[0] = L'p';
    for (u32 i = 0; i < 16; ++i)
    {
        out[1 + i] = static_cast<wchar_t>(kHex[(key >> (4 * (15 - i))) & 0xfu]);
    }
    out[17] = L'\0';
}

}  // namespace engine_placeholder::rhi::dx12
