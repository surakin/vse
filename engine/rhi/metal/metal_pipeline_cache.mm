// The Metal pipeline cache (Phase 26 Day 319; metal_pipeline_cache.h).
//
// Metal reads and writes a binary archive only through a file URL, so the archive's bytes live in a second file beside
// the cache file: written through the file layer from the checked cache file when the device opens, and serialized by
// Metal when it saves, then read back and written into the cache file atomically (WritePipelineCacheFile) with its
// header.

#include "rhi/metal/metal_pipeline_cache.h"

#include "cstr/cstr.h"
#include "cstr/format.h"
#include "cstr/mem_ops.h"

#include "io/file.h"
#include "io/file_system.h"
#include "io/path.h"
#include "rhi/pipeline_cache_file.h"
#include "rhi/rhi_log.h"

#import <Foundation/Foundation.h>

namespace engine_placeholder::rhi::metal
{

namespace
{

constexpr char kMagic[8] = {'E', 'N', 'G', 'M', 'T', 'L', 'A', '1'};
constexpr u32 kVersion = 1;

void PutU32(u8* p, u32 v)
{
    for (int i = 0; i < 4; ++i)
    {
        p[i] = static_cast<u8>(v >> (8 * i));
    }
}

void PutU64(u8* p, u64 v)
{
    for (int i = 0; i < 8; ++i)
    {
        p[i] = static_cast<u8>(v >> (8 * i));
    }
}

u32 GetU32(const u8* p)
{
    u32 v = 0;
    for (int i = 0; i < 4; ++i)
    {
        v |= static_cast<u32>(p[i]) << (8 * i);
    }
    return v;
}

u64 GetU64(const u8* p)
{
    u64 v = 0;
    for (int i = 0; i < 8; ++i)
    {
        v |= static_cast<u64>(p[i]) << (8 * i);
    }
    return v;
}

io::Path PathOf(const char* text)
{
    io::Path path;
    io::Path::Parse(StringView(text), io::NativePathStyle(), &path);
    return path;
}

}  // namespace

u64 MetalArchiveFnv(const void* data, usize size, u64 seed)
{
    const u8* p = static_cast<const u8*>(data);
    u64 h = seed;
    for (usize i = 0; i < size; ++i)
    {
        h ^= p[i];
        h *= 0x100000001b3ull;
    }
    return h;
}

u64 MetalArchiveIdentity(const char* adapter_id, const char* driver)
{
    const u8 separator = 0;
    u64 h = MetalArchiveFnv(adapter_id, CStrLength(adapter_id));
    h = MetalArchiveFnv(&separator, 1, h);
    return MetalArchiveFnv(driver, CStrLength(driver), h);
}

void WriteMetalArchiveHeader(u8* out, u64 identity, const void* archive, u64 size)
{
    MemCopy(out, kMagic, 8);
    PutU32(out + 8, kVersion);
    PutU32(out + 12, 0);
    PutU64(out + 16, identity);
    // The archive's FNV-1a hash seeded with its size: a file cut short or grown fails it as surely as a changed byte.
    PutU64(out + 24, MetalArchiveFnv(archive, static_cast<usize>(size), size));
}

bool CheckMetalArchiveFile(const u8* file, usize file_size, u64 identity, const u8** archive, usize* size,
                           const char** why)
{
    if (file_size < kMetalArchiveHeaderBytes || MemCompare(file, kMagic, 8) != 0)
    {
        *why = "not a Metal pipeline cache";
        return false;
    }
    if (GetU32(file + 8) != kVersion)
    {
        *why = "another format version";
        return false;
    }
    if (GetU64(file + 16) != identity)
    {
        *why = "made for another adapter or macOS build";
        return false;
    }
    const usize payload = file_size - kMetalArchiveHeaderBytes;
    if (payload == 0 ||
        GetU64(file + 24) != MetalArchiveFnv(file + kMetalArchiveHeaderBytes, payload, static_cast<u64>(payload)))
    {
        *why = "damaged (its hash does not match)";
        return false;
    }
    *archive = file + kMetalArchiveHeaderBytes;
    *size = payload;
    return true;
}

MetalPipelineCache::~MetalPipelineCache()
{
    archive_ = nil;
    if (archive_path_[0] != '\0')
    {
        io::Remove(PathOf(archive_path_));
    }
}

void MetalPipelineCache::Open(id<MTLDevice> device, const DeviceCreateInfo& info, const AdapterInfo& adapter,
                              u32 serial)
{
    LockGuard<Mutex> lock(mutex_);
    device_ = device;
    char dir[400] = {};
    if (!info.pipeline_cache || !ResolvePipelineCacheDir(info, dir, sizeof(dir)))
    {
        return;
    }
    stats_.enabled = true;
    identity_ = MetalArchiveIdentity(adapter.id, adapter.driver);
    FormatTo(path_, sizeof(path_), "%s/metal-%s.bin", dir, adapter.id);
    // The archive copy and the serialization file are this device's alone: the process id and the device serial.
    const int pid = [NSProcessInfo processInfo].processIdentifier;
    FormatTo(archive_path_, sizeof(archive_path_), "%s.%d-%u.archive", path_, pid, serial);
    FormatTo(temp_path_, sizeof(temp_path_), "%s.%d-%u.serializing", path_, pid, serial);
    MTLBinaryArchiveDescriptor* descriptor = [[MTLBinaryArchiveDescriptor alloc] init];
    Vector<byte> file;
    if (ReadPipelineCacheFile(path_, &file))
    {
        const u8* archive = nullptr;
        usize size = 0;
        const char* why = nullptr;
        if (!CheckMetalArchiveFile(reinterpret_cast<const u8*>(file.Data()), file.Size(), identity_, &archive, &size,
                                   &why))
        {
            RhiLog(LogVerbosity::Warn, "Metal: the pipeline cache %s is %s; discarded, starting empty", path_, why);
            stats_.discarded = true;
        }
        else if (!WritePipelineCacheFile(archive_path_, archive, size))
        {
            // The file is good, but its copy for Metal could not be written (a full or read-only disk): not damage.
            RhiLog(LogVerbosity::Warn, "Metal: the pipeline cache %s could not be copied to %s; starting empty", path_,
                   archive_path_);
        }
        else
        {
            descriptor.url = [NSURL fileURLWithPath:[NSString stringWithUTF8String:archive_path_]];
        }
    }
    NSError* error = nil;
    archive_ = [device newBinaryArchiveWithDescriptor:descriptor error:&error];
    if (archive_ == nil && descriptor.url != nil)
    {
        // Metal itself refused the bytes (another driver compiler): start empty.
        RhiLog(LogVerbosity::Warn, "Metal: the pipeline cache %s could not be opened (%s); discarded, starting empty",
               path_, error != nil ? error.localizedDescription.UTF8String : "no reason given");
        stats_.discarded = true;
        archive_ = [device newBinaryArchiveWithDescriptor:[[MTLBinaryArchiveDescriptor alloc] init] error:&error];
    }
    else if (descriptor.url != nil)
    {
        stats_.loaded = true;
    }
    if (archive_ == nil)
    {
        RhiLog(LogVerbosity::Warn, "Metal: no pipeline cache (an empty binary archive could not be made)");
        stats_.enabled = false;
    }
}

id<MTLRenderPipelineState> MetalPipelineCache::NewRender(MTLRenderPipelineDescriptor* descriptor, bool* hit,
                                                         NSError** error)
{
    *hit = false;
    LockGuard<Mutex> lock(mutex_);
    if (archive_ == nil)
    {
        return [device_ newRenderPipelineStateWithDescriptor:descriptor error:error];
    }
    descriptor.binaryArchives = @[ archive_ ];
    id<MTLRenderPipelineState> state = [device_
        newRenderPipelineStateWithDescriptor:descriptor
                                     options:MTLPipelineOptionFailOnBinaryArchiveMiss
                                  reflection:nil
                                       error:nil];
    if (state != nil)
    {
        *hit = true;
        return state;
    }
    // A miss: compiled once, into the archive, then created from it.
    NSError* add_error = nil;
    if ([archive_ addRenderPipelineFunctionsWithDescriptor:descriptor error:&add_error])
    {
        dirty_ = true;
        ++stats_.added;
        state = [device_ newRenderPipelineStateWithDescriptor:descriptor
                                                      options:MTLPipelineOptionFailOnBinaryArchiveMiss
                                                   reflection:nil
                                                        error:nil];
    }
    else
    {
        RhiLog(LogVerbosity::Warn, "Metal: the pipeline '%s' was not added to the pipeline cache (%s)",
               descriptor.label != nil ? descriptor.label.UTF8String : "(unnamed)",
               add_error != nil ? add_error.localizedDescription.UTF8String : "no reason given");
    }
    return state != nil ? state : [device_ newRenderPipelineStateWithDescriptor:descriptor error:error];
}

id<MTLComputePipelineState> MetalPipelineCache::NewCompute(MTLComputePipelineDescriptor* descriptor, bool* hit,
                                                           NSError** error)
{
    *hit = false;
    LockGuard<Mutex> lock(mutex_);
    if (archive_ == nil)
    {
        return [device_ newComputePipelineStateWithDescriptor:descriptor
                                                      options:MTLPipelineOptionNone
                                                   reflection:nil
                                                        error:error];
    }
    descriptor.binaryArchives = @[ archive_ ];
    id<MTLComputePipelineState> state = [device_
        newComputePipelineStateWithDescriptor:descriptor
                                      options:MTLPipelineOptionFailOnBinaryArchiveMiss
                                   reflection:nil
                                        error:nil];
    if (state != nil)
    {
        *hit = true;
        return state;
    }
    NSError* add_error = nil;
    if ([archive_ addComputePipelineFunctionsWithDescriptor:descriptor error:&add_error])
    {
        dirty_ = true;
        ++stats_.added;
        state = [device_ newComputePipelineStateWithDescriptor:descriptor
                                                       options:MTLPipelineOptionFailOnBinaryArchiveMiss
                                                    reflection:nil
                                                         error:nil];
    }
    else
    {
        RhiLog(LogVerbosity::Warn, "Metal: the compute pipeline '%s' was not added to the pipeline cache (%s)",
               descriptor.label != nil ? descriptor.label.UTF8String : "(unnamed)",
               add_error != nil ? add_error.localizedDescription.UTF8String : "no reason given");
    }
    return state != nil ? state
                        : [device_ newComputePipelineStateWithDescriptor:descriptor
                                                                 options:MTLPipelineOptionNone
                                                              reflection:nil
                                                                   error:error];
}

bool MetalPipelineCache::Save()
{
    LockGuard<Mutex> lock(mutex_);
    if (archive_ == nil || !dirty_)
    {
        return true;
    }
    const char* temp = temp_path_;
    bool ok = false;
    @autoreleasepool
    {
        NSError* error = nil;
        if ([archive_ serializeToURL:[NSURL fileURLWithPath:[NSString stringWithUTF8String:temp]] error:&error])
        {
            Vector<u8> bytes;
            if (io::ReadFile(PathOf(temp), &bytes).Ok() && !bytes.Empty())
            {
                Vector<u8> file;
                file.Resize(kMetalArchiveHeaderBytes + bytes.Size());
                WriteMetalArchiveHeader(file.Data(), identity_, bytes.Data(), bytes.Size());
                MemCopy(file.Data() + kMetalArchiveHeaderBytes, bytes.Data(), bytes.Size());
                ok = WritePipelineCacheFile(path_, file.Data(), file.Size());
                if (ok)
                {
                    stats_.saved_bytes = file.Size();
                    dirty_ = false;
                }
            }
        }
        else
        {
            RhiLog(LogVerbosity::Warn, "Metal: the pipeline cache could not be serialized (%s)",
                   error != nil ? error.localizedDescription.UTF8String : "no reason given");
        }
    }
    io::Remove(PathOf(temp));
    if (!ok)
    {
        RhiLog(LogVerbosity::Warn, "Metal: the pipeline cache %s was not written", path_);
    }
    return ok;
}

MetalPipelineCache::Stats MetalPipelineCache::GetStats() const
{
    LockGuard<Mutex> lock(mutex_);
    return stats_;
}

}  // namespace engine_placeholder::rhi::metal
