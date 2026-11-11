// Where a backend keeps its pipeline cache, and how the file is read and written (Phase 13 Day 167, moved out of the
// Vulkan backend).

#include "rhi/pipeline_cache_file.h"

#include "os/console.h"
#include "cstr/cstr.h"

#include "io/file.h"
#include "io/file_system.h"
#include "io/path.h"
#include "rhi/rhi.h"
#include "string_view.h"
#include "cstr/mem_ops.h"

namespace engine_placeholder::rhi
{

namespace
{

bool Append(char* out, usize capacity, usize* pos, const char* text)
{
    const usize n = CStrLength(text);
    if (*pos + n + 1 > capacity)
    {
        return false;
    }
    MemCopy(out + *pos, text, n);
    *pos += n;
    return true;
}

constexpr u64 kMaxCacheFileBytes = u64{256} << 20;

}  // namespace

PipelineCacheDirChoice ChoosePipelineCacheDir(const char* explicit_dir, const char* env_cache_dir, char* out,
                                              usize capacity)
{
    if (capacity != 0)
    {
        out[0] = '\0';
    }
    auto set = [](const char* s) { return s != nullptr && s[0] != '\0'; };
    const char* source = set(explicit_dir) ? explicit_dir : (set(env_cache_dir) ? env_cache_dir : nullptr);
    if (source == nullptr)
    {
        return PipelineCacheDirChoice::NotConfigured;
    }
    usize pos = 0;
    if (capacity == 0 || !Append(out, capacity, &pos, source))
    {
        if (capacity != 0)
        {
            out[0] = '\0';
        }
        return PipelineCacheDirChoice::TooLong;
    }
    out[pos] = '\0';
    return PipelineCacheDirChoice::Chosen;
}

bool SanitizePipelineCacheAppName(const char* app, char* out, usize capacity)
{
    if (capacity == 0)
    {
        return false;
    }
    out[0] = '\0';
    const char* name = (app != nullptr && app[0] != '\0') ? app : "engine";
    usize pos = 0;
    bool ok = true;
    for (; ok && *name != '\0'; ++name)
    {
        const char one[2] = {*name == '/' ? '_' : *name, '\0'};
        ok = Append(out, capacity, &pos, one);
    }
    out[ok ? pos : 0] = '\0';
    return ok;
}

bool ResolvePipelineCacheDir(const DeviceCreateInfo& info, char* out, usize capacity)
{
    const PipelineCacheDirChoice choice = ChoosePipelineCacheDir(
        info.pipeline_cache_dir, os::GetEnvironment("ENGINE_RHI_CACHE_DIR"), out, capacity);
    if (choice != PipelineCacheDirChoice::NotConfigured)
    {
        return choice == PipelineCacheDirChoice::Chosen;
    }
    char app[200];
    io::Path user_cache;
    const io::Status s = SanitizePipelineCacheAppName(info.application_name, app, sizeof(app))
                             ? io::UserCacheDirectory(StringView(app), &user_cache)
                             : io::Status::Of(io::Error::Invalid);
    if (s.Ok() && user_cache.ToNative(out, capacity))
    {
        return true;
    }
    if (capacity != 0)
    {
        out[0] = '\0';
    }
    return false;
}

bool ReadPipelineCacheFile(const char* path, Vector<byte>* out)
{
    // Empty on failure, whatever it held and whatever a failed read left in it: callers test the size.
    out->Reset();
    io::Path file_path;
    io::FileInfo info;
    if (!io::Path::Parse(StringView(path), io::NativePathStyle(), &file_path) || io::Stat(file_path, &info).Failed() ||
        info.type != io::FileType::Regular || info.size == 0 || info.size > kMaxCacheFileBytes)
    {
        return false;
    }
    if (io::ReadFile(file_path, out, kMaxCacheFileBytes).Failed())
    {
        out->Reset();
        return false;
    }
    return true;
}

bool WritePipelineCacheFile(const char* path, const void* data, usize size)
{
    io::Path file_path;
    if (!io::Path::Parse(StringView(path), io::NativePathStyle(), &file_path))
    {
        return false;
    }
    const io::Path dir = file_path.Parent();
    // Readable by others, as a cache shared through ENGINE_RHI_CACHE_DIR was before.
    return (dir.IsEmpty() || io::CreateDirectories(dir).Ok()) && io::WriteFileAtomic(file_path, data, size, 0644).Ok();
}

}  // namespace engine_placeholder::rhi
