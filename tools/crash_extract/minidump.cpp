#include "minidump.h"

#include "cstr/format.h"
#include "types.h"
#include "cstr/mem_ops.h"

namespace crash_extract
{

namespace
{

using engine_placeholder::u8;

constexpr u32 kMinidumpSignature = 0x504d444d;  // "MDMP"
constexpr u32 kModuleListStream = 4;
constexpr u32 kExceptionStream = 6;
constexpr u32 kThreadListStream = 3;
constexpr u32 kSystemInfoStream = 7;
constexpr engine_placeholder::usize kThreadBytes = 48;  // MINIDUMP_THREAD
constexpr u32 kPlatformMacOs = 0x8101;                  // Breakpad's MD_OS_MAC_OS_X
constexpr u32 kArchitectureAmd64 = 9;                   // PROCESSOR_ARCHITECTURE_AMD64
constexpr u32 kContextAmd64 = 0x00100000;               // CONTEXT_AMD64, no register group
constexpr u32 kContextAmd64Written = kContextAmd64 |
                                     0x7;  // CONTROL | INTEGER | SEGMENTS: what Breakpad's macOS writer fills
constexpr engine_placeholder::usize kContextAmd64FlagsOffset = 0x30;
constexpr engine_placeholder::usize kModuleBytes = 108;  // MINIDUMP_MODULE
constexpr u32 kRsds = 0x53445352;                        // "RSDS"
constexpr u32 kBpel = 0x4c457042;                        // "BpEL"

bool Fits(engine_placeholder::usize size, u64 offset, u64 length)
{
    return offset <= size && length <= size - offset;
}

u32 Read32(const char* p)
{
    u32 v = 0;
    engine_placeholder::MemCopy(&v, p, sizeof(v));  // little-endian files on little-endian hosts (every target)
    return v;
}

void Write32(char* p, u32 v)
{
    engine_placeholder::MemCopy(p, &v, sizeof(v));
}

u64 Read64(const char* p)
{
    u64 v = 0;
    engine_placeholder::MemCopy(&v, p, sizeof(v));
    return v;
}

// Appends one code point as UTF-8 if it fits (keeping room for the terminator).
void PutCodePoint(char* out, engine_placeholder::usize capacity, engine_placeholder::usize& n, u32 cp)
{
    char bytes[4];
    engine_placeholder::usize k = 0;
    if (cp < 0x80)
    {
        bytes[k++] = static_cast<char>(cp);
    }
    else if (cp < 0x800)
    {
        bytes[k++] = static_cast<char>(0xC0 | (cp >> 6));
        bytes[k++] = static_cast<char>(0x80 | (cp & 0x3F));
    }
    else if (cp < 0x10000)
    {
        bytes[k++] = static_cast<char>(0xE0 | (cp >> 12));
        bytes[k++] = static_cast<char>(0x80 | ((cp >> 6) & 0x3F));
        bytes[k++] = static_cast<char>(0x80 | (cp & 0x3F));
    }
    else
    {
        bytes[k++] = static_cast<char>(0xF0 | (cp >> 18));
        bytes[k++] = static_cast<char>(0x80 | ((cp >> 12) & 0x3F));
        bytes[k++] = static_cast<char>(0x80 | ((cp >> 6) & 0x3F));
        bytes[k++] = static_cast<char>(0x80 | (cp & 0x3F));
    }
    if (n + k + 1 > capacity)
    {
        return;
    }
    engine_placeholder::MemCopy(out + n, bytes, k);
    n += k;
}

// A MINIDUMP_STRING (a byte length, then UTF-16) at `rva`, as UTF-8; an unpaired surrogate becomes U+FFFD.
bool ReadDumpString(const char* data, engine_placeholder::usize size, u32 rva, char* out,
                    engine_placeholder::usize capacity)
{
    out[0] = '\0';
    if (!Fits(size, rva, 4))
    {
        return false;
    }
    const u32 bytes = Read32(data + rva);
    if (!Fits(size, u64{rva} + 4, bytes))
    {
        return false;
    }
    const char* units = data + rva + 4;
    const u32 count = bytes / 2;
    engine_placeholder::usize n = 0;
    for (u32 i = 0; i < count; ++i)
    {
        u32 cp = static_cast<u8>(units[engine_placeholder::usize{2} * i]) |
                 (u32{static_cast<u8>(units[engine_placeholder::usize{2} * i + 1])} << 8);
        if (cp >= 0xD800 && cp < 0xDC00 && i + 1 < count)
        {
            const u32 low = static_cast<u8>(units[2 * i + 2]) | (u32{static_cast<u8>(units[2 * i + 3])} << 8);
            if (low >= 0xDC00 && low < 0xE000)
            {
                cp = 0x10000 + ((cp - 0xD800) << 10) + (low - 0xDC00);
                ++i;
            }
            else
            {
                cp = 0xFFFD;
            }
        }
        else if (cp >= 0xD800 && cp < 0xE000)
        {
            cp = 0xFFFD;
        }
        PutCodePoint(out, capacity, n, cp);
    }
    out[n] = '\0';
    return true;
}

// A CodeView record ([data, data + length)) as a build id and a debug file name.
void ReadCodeView(const char* record, engine_placeholder::usize length, char* build_id,
                  engine_placeholder::usize id_capacity, char* debug_file, engine_placeholder::usize file_capacity)
{
    build_id[0] = '\0';
    debug_file[0] = '\0';
    if (length < 4)
    {
        return;
    }
    const u32 signature = Read32(record);
    if (signature == kRsds && length >= 24)
    {
        // GUID (Data1 u32, Data2 u16, Data3 u16, Data4 u8[8]), then the age (u32), then the PDB path (NUL-terminated).
        const u32 data1 = Read32(record + 4);
        const u32 data2 = static_cast<u8>(record[8]) | (u32{static_cast<u8>(record[9])} << 8);
        const u32 data3 = static_cast<u8>(record[10]) | (u32{static_cast<u8>(record[11])} << 8);
        engine_placeholder::usize n = static_cast<engine_placeholder::usize>(
            engine_placeholder::FormatTo(build_id, id_capacity, "%08x%04x%04x", data1, data2, data3));
        for (int i = 0; i < 8 && n + 3 <= id_capacity; ++i)
        {
            n += static_cast<engine_placeholder::usize>(
                engine_placeholder::FormatTo(build_id + n, id_capacity - n, "%02x", static_cast<u8>(record[12 + i])));
        }
        engine_placeholder::FormatTo(build_id + n, id_capacity - n, "%x", Read32(record + 20));
        engine_placeholder::usize k = 0;
        for (engine_placeholder::usize i = 24; i < length && record[i] != '\0' && k + 1 < file_capacity; ++i)
        {
            debug_file[k++] = record[i];
        }
        debug_file[k] = '\0';
    }
    else if (signature == kBpel)
    {
        engine_placeholder::usize n = 0;
        for (engine_placeholder::usize i = 4; i < length && n + 3 <= id_capacity; ++i)
        {
            n += static_cast<engine_placeholder::usize>(
                engine_placeholder::FormatTo(build_id + n, id_capacity - n, "%02x", static_cast<u8>(record[i])));
        }
    }
}

// Finds a stream by type in the directory; false if the dump is damaged. `*found` says whether the stream is there.
bool FindStream(const char* data, engine_placeholder::usize size, u32 type, u32* rva, u32* length, bool* found)
{
    *found = false;
    if (data == nullptr || !Fits(size, 0, 32) || Read32(data) != kMinidumpSignature)
    {
        return false;
    }
    const u32 streams = Read32(data + 8);
    const u32 directory = Read32(data + 12);
    if (!Fits(size, directory, u64{streams} * 12))
    {
        return false;
    }
    for (u32 i = 0; i < streams; ++i)
    {
        const char* entry = data + directory + u64{i} * 12;
        if (Read32(entry) != type)
        {
            continue;
        }
        *length = Read32(entry + 4);
        *rva = Read32(entry + 8);
        if (!Fits(size, *rva, *length))
        {
            return false;
        }
        *found = true;
        return true;
    }
    return true;
}

}  // namespace

DumpResult ReadDumpModules(const char* data, engine_placeholder::usize size, DumpModule* out,
                           engine_placeholder::usize capacity, engine_placeholder::usize* count)
{
    *count = 0;
    u32 rva = 0;
    u32 length = 0;
    bool found = false;
    if (!FindStream(data, size, kModuleListStream, &rva, &length, &found))
    {
        return DumpResult::Malformed;
    }
    if (!found)
    {
        return DumpResult::Ok;
    }
    if (length < 4)
    {
        return DumpResult::Malformed;
    }
    const u32 modules = Read32(data + rva);
    if (u64{modules} * kModuleBytes > length - 4)
    {
        return DumpResult::Malformed;
    }
    // Breakpad's macOS writer pads the count to 8 bytes before the array (MDRawModuleList is not packed there); a list
    // exactly 4 bytes longer than its modules has that padding, as Breakpad's own reader assumes (Phase 21 Day 292).
    const u64 header = u64{length} == 8 + u64{modules} * kModuleBytes ? 8 : 4;
    *count = modules;
    for (u32 i = 0; i < modules && i < capacity; ++i)
    {
        const char* m = data + rva + header + u64{i} * kModuleBytes;
        DumpModule& module = out[i];
        module = DumpModule();
        module.base = Read64(m);
        module.size = Read32(m + 8);
        module.timestamp = Read32(m + 16);
        ReadDumpString(data, size, Read32(m + 20), module.name, sizeof(module.name));
        const u32 cv_length = Read32(m + 76);
        const u32 cv_rva = Read32(m + 80);
        if (cv_length != 0 && Fits(size, cv_rva, cv_length))
        {
            ReadCodeView(data + cv_rva, cv_length, module.build_id, sizeof(module.build_id), module.debug_file,
                         sizeof(module.debug_file));
        }
    }
    return DumpResult::Ok;
}

DumpResult ReadDumpException(const char* data, engine_placeholder::usize size, DumpException* out)
{
    *out = DumpException();
    u32 rva = 0;
    u32 length = 0;
    bool found = false;
    if (!FindStream(data, size, kExceptionStream, &rva, &length, &found))
    {
        return DumpResult::Malformed;
    }
    if (!found)
    {
        return DumpResult::Ok;
    }
    if (length < 32)
    {
        return DumpResult::Malformed;  // ThreadId, alignment, then MINIDUMP_EXCEPTION's code, flags, record and address
    }
    const char* stream = data + rva;
    out->present = true;
    out->thread_id = Read32(stream);
    out->code = Read32(stream + 8);
    out->address = Read64(stream + 24);
    return DumpResult::Ok;
}

namespace
{

// Completes one context, given by its location descriptor (size, then RVA, at `location`). A context that is not
// there or does not fit (a dump cut short) is left alone: the others are still worth reading.
void CompleteContext(char* data, engine_placeholder::usize size, const char* location,
                     engine_placeholder::usize* completed)
{
    const u32 context_size = Read32(location);
    const u32 context_rva = Read32(location + 4);
    if (context_size < kContextAmd64FlagsOffset + 4 || !Fits(size, context_rva, context_size))
    {
        return;
    }
    char* flags = data + context_rva + kContextAmd64FlagsOffset;
    if (Read32(flags) == kContextAmd64)
    {
        Write32(flags, kContextAmd64Written);
        ++*completed;
    }
}

}  // namespace

DumpResult CompleteMacContextFlags(char* data, engine_placeholder::usize size, engine_placeholder::usize* completed)
{
    *completed = 0;
    u32 rva = 0;
    u32 length = 0;
    bool found = false;
    if (!FindStream(data, size, kSystemInfoStream, &rva, &length, &found))
    {
        return DumpResult::Malformed;
    }
    if (!found)
    {
        return DumpResult::Ok;  // no system named: not knowably a macOS dump
    }
    if (length < 24)
    {
        return DumpResult::Malformed;
    }
    const u32 architecture = static_cast<u8>(data[rva]) | (u32{static_cast<u8>(data[rva + 1])} << 8);
    if (Read32(data + rva + 20) != kPlatformMacOs || architecture != kArchitectureAmd64)
    {
        return DumpResult::Ok;
    }
    if (!FindStream(data, size, kThreadListStream, &rva, &length, &found))
    {
        return DumpResult::Malformed;
    }
    if (found && length >= 4)
    {
        // Padded to 8 bytes after the count, like the module list (ReadDumpModules). A count longer than the stream (a
        // dump cut short) is read as far as the stream goes.
        const u32 threads = Read32(data + rva);
        const u64 header = u64{length} == 8 + u64{threads} * kThreadBytes ? 8 : 4;
        for (u32 i = 0; i < threads && header + (u64{i} + 1) * kThreadBytes <= length; ++i)
        {
            CompleteContext(data, size, data + rva + header + u64{i} * kThreadBytes + 40, completed);
        }
    }
    if (!FindStream(data, size, kExceptionStream, &rva, &length, &found))
    {
        return DumpResult::Malformed;
    }
    // ThreadId, alignment, MINIDUMP_EXCEPTION (152 bytes), then the context's location.
    if (found && length >= 168)
    {
        CompleteContext(data, size, data + rva + 160, completed);
    }
    return DumpResult::Ok;
}

bool ReadImageInfo(const char* data, engine_placeholder::usize size, ImageInfo* out)
{
    *out = ImageInfo();
    if (data == nullptr || !Fits(size, 0, 64) || data[0] != 'M' || data[1] != 'Z')
    {
        return false;
    }
    const u32 pe = Read32(data + 0x3c);
    if (!Fits(size, pe, 24) || engine_placeholder::MemCompare(data + pe, "PE\0\0", 4) != 0)
    {
        return false;
    }
    const char* coff = data + pe + 4;
    const u32 sections = static_cast<u8>(coff[2]) | (u32{static_cast<u8>(coff[3])} << 8);
    out->timestamp = Read32(coff + 4);
    const u32 optional_size = static_cast<u8>(coff[16]) | (u32{static_cast<u8>(coff[17])} << 8);
    const u64 optional = u64{pe} + 24;
    if (!Fits(size, optional, optional_size) || optional_size < 2)
    {
        return false;
    }
    const char* opt = data + optional;
    const u32 magic = static_cast<u8>(opt[0]) | (u32{static_cast<u8>(opt[1])} << 8);
    const u32 directories_at = magic == 0x20b ? 112 : magic == 0x10b ? 96 : 0;  // PE32+ or PE32
    if (directories_at == 0 || optional_size < directories_at + 7 * 8 || optional_size < 60)
    {
        return false;
    }
    out->size_of_image = Read32(opt + 56);
    const u32 debug_rva = Read32(opt + directories_at + engine_placeholder::usize{6} * 8);  // data directory 6: debug
    const u32 debug_size = Read32(opt + directories_at + engine_placeholder::usize{6} * 8 + 4);
    // The debug directory's file offset, through the section that holds its address.
    const u64 table = optional + optional_size;
    if (!Fits(size, table, u64{sections} * 40))
    {
        return false;
    }
    u64 debug_offset = 0;
    bool mapped = false;
    for (u32 i = 0; i < sections && !mapped; ++i)
    {
        const char* s = data + table + u64{i} * 40;
        const u32 virtual_size = Read32(s + 8);
        const u32 virtual_address = Read32(s + 12);
        const u32 raw_size = Read32(s + 16);
        const u32 raw_offset = Read32(s + 20);
        const u32 extent = virtual_size > raw_size ? virtual_size : raw_size;
        if (debug_rva >= virtual_address && debug_rva - virtual_address < extent)
        {
            debug_offset = u64{raw_offset} + (debug_rva - virtual_address);
            mapped = true;
        }
    }
    if (!mapped || !Fits(size, debug_offset, debug_size))
    {
        return false;
    }
    for (u32 i = 0; i + 28 <= debug_size; i += 28)
    {
        const char* entry = data + debug_offset + i;
        if (Read32(entry + 12) != 2)
        {
            continue;  // IMAGE_DEBUG_TYPE_CODEVIEW
        }
        const u32 cv_size = Read32(entry + 16);
        const u32 cv_offset = Read32(entry + 24);
        if (!Fits(size, cv_offset, cv_size))
        {
            return false;
        }
        ReadCodeView(data + cv_offset, cv_size, out->build_id, sizeof(out->build_id), out->debug_file,
                     sizeof(out->debug_file));
        return out->build_id[0] != '\0';
    }
    return false;
}

const char* BaseName(const char* path)
{
    const char* name = path;
    for (const char* p = path; *p != '\0'; ++p)
    {
        if (*p == '/' || *p == '\\')
        {
            name = p + 1;
        }
    }
    return name;
}

}  // namespace crash_extract
