#pragma once

// The Metal pipeline cache (Phase 26 Day 319): an MTLBinaryArchive the device's pipelines are added to, kept in a file
// named by the adapter (pipeline_cache_file.h) behind a header that records the adapter and macOS build it is for and
// a hash of the archive. A stale or damaged file is discarded with a log line. Objective-C++.

#include "sync/mutex.h"

#include "containers/vector.h"
#include "rhi/rhi.h"

#import <Metal/Metal.h>

namespace engine_placeholder::rhi::metal
{

// The header's size in front of the archive's bytes: magic `ENGMTLA1`, a format version, a reserved word, the identity,
// and the archive's FNV-1a hash seeded with its size.
inline constexpr usize kMetalArchiveHeaderBytes = 32;

// FNV-1a over bytes, continuing from `seed`. Pure.
u64 MetalArchiveFnv(const void* data, usize size, u64 seed = 0xcbf29ce484222325ull);

// The identity a cache file is for: the adapter's id and its driver (the macOS build). Pure.
u64 MetalArchiveIdentity(const char* adapter_id, const char* driver);

// Writes the header for `archive` of `size` bytes into `out` (kMetalArchiveHeaderBytes). Pure.
void WriteMetalArchiveHeader(u8* out, u64 identity, const void* archive, u64 size);

// Checks a cache file's bytes: the magic, the version, the identity, the size and the hash. On success `*archive` and
// `*size` point at the archive inside `file`; else `why` names the problem. Pure.
bool CheckMetalArchiveFile(const u8* file, usize file_size, u64 identity, const u8** archive, usize* size,
                           const char** why);

// The archive and its file. Thread-safety: thread-safe (a mutex: pipelines are created from any thread).
class MetalPipelineCache
{
public:
    MetalPipelineCache() = default;
    ~MetalPipelineCache();
    MetalPipelineCache(const MetalPipelineCache&) = delete;
    MetalPipelineCache& operator=(const MetalPipelineCache&) = delete;

    // Opens the cache of `info` for `device` (none when info.pipeline_cache is false or no directory resolves): loads
    // the file if it is good, else starts empty. Never fails the device.
    // `serial` makes the device's own files unique (two devices on one adapter, in one process or two, never share
    // them).
    void Open(id<MTLDevice> device, const DeviceCreateInfo& info, const AdapterInfo& adapter, u32 serial);
    // Creates a pipeline state through the archive: found in it (`*hit` true), else added to it and created from it,
    // so a miss compiles once. Without a cache, a plain creation. Every use of the archive holds the cache's lock (the
    // archive is not documented as safe for a lookup beside an add), so creation is serialized while a cache is open.
    // Nil with `*error` set when Metal refuses the pipeline.
    id<MTLRenderPipelineState> NewRender(MTLRenderPipelineDescriptor* descriptor, bool* hit, NSError** error);
    id<MTLComputePipelineState> NewCompute(MTLComputePipelineDescriptor* descriptor, bool* hit, NSError** error);
    // Writes the file when pipelines were added since it was read or written. True when nothing was to do or it wrote.
    // Metal sometimes refuses to serialize an archive read from a file with pipelines added to it (macOS 15.8, "missing
    // 'vertex' stage in pipeline no. N", seen on Day 325): the file is then written from a fresh archive of the
    // pipelines this device made, which replaces the loaded one. If that fails too the device stops saving.
    bool Save();

    // What happened (tests and logs).
    struct Stats
    {
        bool enabled = false;
        bool loaded = false;     // a good file was read
        bool discarded = false;  // a file was there and was refused (stale or damaged)
        u32 added = 0;           // pipelines added since Open
        u64 saved_bytes = 0;     // the last file written
        u32 recorded = 0;        // distinct pipeline descriptions kept for a rebuild
        u32 rebuilt = 0;         // saves that wrote a fresh archive of this run's pipelines (the loaded one would not)
    };
    Stats GetStats() const;
    // Test hook: the next save treats the archive as refused, as Metal does in the case above.
    void FailNextSerializeForTest()
    {
        LockGuard<Mutex> lock(mutex_);
        fail_next_serialize_ = true;
    }
    // The cache file's native path; empty when off.
    const char* Path() const { return path_; }

private:
    mutable Mutex mutex_;
    id<MTLDevice> device_ = nil;
    id<MTLBinaryArchive> archive_ = nil;
    char path_[512] = {};
    char archive_path_[560] = {};  // the archive's bytes for Metal to read, beside the cache file (this device's)
    char temp_path_[560] = {};     // where Metal serializes it (this device's)
    u64 identity_ = 0;
    bool dirty_ = false;
    bool given_up_ = false;
    bool fail_next_serialize_ = false;
    // A fresh archive with every pipeline in render_used_ and compute_used_; nil when Metal refuses one.
    id<MTLBinaryArchive> RebuildLocked();  // neither archive serialized: no more saves for this device (logged once)
    // Copies of the descriptors of every pipeline made through the archive since Open, hits and misses: a fresh
    // archive is built from them when the loaded one, with pipelines added, will not serialize (Day 325).
    // One entry per distinct description (equal ones are kept once), held for the device's life: a device reset ends
    // it.
    NSMutableOrderedSet<MTLRenderPipelineDescriptor*>* render_used_ = nil;
    NSMutableOrderedSet<MTLComputePipelineDescriptor*>* compute_used_ = nil;
    // Records a pipeline the archive holds (a hit, or a miss it took), for RebuildLocked.
    void RecordLocked(MTLRenderPipelineDescriptor* descriptor);
    void RecordLocked(MTLComputePipelineDescriptor* descriptor);
    Stats stats_;
};

}  // namespace engine_placeholder::rhi::metal
