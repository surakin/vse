// asset_demo: Phase 22 Day 339. The the host path in one program: a glTF file is imported from memory
// (`asset::ImportGltf`, or `ImportGltfAsync` with
// --async), the result is added to a host memory backend, mounted on a resource manager, and every mesh, skeleton and
// clip is loaded through the manager (by path and by id, then again through the AsyncLoader), the textures and
// materials are opened as blobs. The program prints what it did and fails when a load converted anything
// (`load_conversions` must be 0), when a blob does not verify, or when an id differs between loads.
//
//   asset_demo --import FILE.gltf|FILE.glb [--async] [--prefix /PATH] [--scale X]
//
// Headless: nothing is drawn.
#include "asset/async_loader.h"
#include "asset/cpu_loaders.h"
#include "asset/material.h"
#include "asset/memory_backend.h"
#include "asset/mesh.h"
#include "asset/resource_manager.h"
#include "asset/skeleton.h"
#include "asset/texture.h"
#include "asset_import/gltf_async.h"
#include "asset_import/gltf_import.h"
#include "asset_import/import_settings.h"
#include "containers/vector.h"
#include "cstr/cstr.h"
#include "cstr/mem_ops.h"
#include "cstr/number.h"
#include "io/file.h"
#include "io/path.h"
#include "jobs/scheduler.h"
#include "os/command_line.h"
#include "os/console.h"
#include "settings/registry.h"
#include "settings/settings.h"

using namespace engine_placeholder;

namespace
{

using Line = os::CommandLine<HeapAllocator>;

constexpr os::CommandLineOption kOptions[] = {{"import", true}, {"async", false}, {"prefix", true}, {"scale", true}};

bool ReadFile(const io::Path& path, u64 max_bytes, Vector<u8>* out)
{
    io::FilePtr in;
    if (io::File::Open(path, io::OpenMode::Read, io::kOpenNone, &in).Failed())
    {
        return false;
    }
    return io::ReadAll(*in, out, max_bytes).Ok();
}

// Reads the files a glTF names from its own folder.
class DirectorySource final : public asset::GltfSource
{
public:
    explicit DirectorySource(const io::Path& directory) : directory_(directory) {}
    asset::Status Read(StringView name, u64 max_bytes, Vector<byte>* out) override
    {
        io::Path relative;
        io::Path full;
        Vector<u8> bytes;
        if (!io::Path::Parse(name, io::NativePathStyle(), &relative) || !directory_.TryJoin(relative, &full) ||
            !ReadFile(full, max_bytes, &bytes))
        {
            return asset::Status::Make(asset::ErrorCode::NotFound, "the file cannot be read");
        }
        *out = static_cast<Vector<byte>&&>(bytes);
        return asset::Status::Success();
    }

private:
    io::Path directory_;
};

int Fail(const char* what)
{
    os::PrintError("asset_demo: %s\n", what);
    return 1;
}

int Usage()
{
    os::PrintError(
        "usage: asset_demo --import FILE.gltf|FILE.glb [--async] [--prefix /PATH] [--scale X] [--set KEY=VALUE | --assets-import-*]\n");
    return 64;
}

}  // namespace

int main(int argc, char** argv)
{
    // The settings system takes its flags (--assets-import-*, --set, ...) and the environment first; what it leaves is
    // the program's own.
    settings::Registry registry;
    asset::RegisterImportSettings(&registry);
    settings::Settings loaded(registry);
    settings::LoadOptions load;
    load.app_name = "asset_demo";
    load.argc = argc;
    load.argv = argv;
    const settings::LoadReport report = loaded.Load(load);
    for (usize i = 0; i < report.problems.Size(); ++i)
    {
        os::PrintError("asset_demo: settings: %s: %s\n", report.problems[i].layer, report.problems[i].message);
    }
    const settings::CommandLineResult& cli = loaded.CommandLine();
    if (!cli.errors.Empty())
    {
        return Usage();
    }
    Vector<String<HeapAllocator>> rest_storage;
    Vector<char*> rest;
    const Line native(HeapAllocator::Default(), argc, argv);
    rest_storage.Push(String<HeapAllocator>(HeapAllocator::Default(), native.ProgramName()));
    for (usize i = 0; i < cli.rest.Size(); ++i)
    {
        rest_storage.Push(String<HeapAllocator>(HeapAllocator::Default(), cli.rest[i].View()));
    }
    for (usize i = 0; i < rest_storage.Size(); ++i)
    {
        rest.Push(const_cast<char*>(rest_storage[i].CStr()));
    }
    rest.Push(nullptr);
    const Line line(HeapAllocator::Default(), static_cast<int>(rest_storage.Size()), rest.Data(),
                    Span<const os::CommandLineOption>(kOptions, 4));
    StringView file_text;
    if (!line.Ok() || line.ArgCount() != 0 || !line.TryGetOption(StringView("import"), file_text))
    {
        return Usage();
    }
    io::Path file_path;
    if (!io::Path::Parse(file_text, io::NativePathStyle(), &file_path))
    {
        return Usage();
    }
    asset::ImportOptions options;
    options.limits = asset::ImportLimitsFromSettings(registry);
    StringView prefix;
    if (line.TryGetOption(StringView("prefix"), prefix))
    {
        options.path_prefix = prefix;
    }
    StringView scale_text;
    if (line.TryGetOption(StringView("scale"), scale_text))
    {
        const ParseDoubleResult parsed = ParseDoubleExact(scale_text.Data(), scale_text.Data() + scale_text.Length());
        if (!parsed.ok)
        {
            return Usage();
        }
        options.scale = static_cast<f32>(parsed.value);
    }
    Vector<u8> file;
    if (!ReadFile(file_path, options.limits.max_file_bytes, &file))
    {
        return Fail("the glTF file could not be read");
    }
    DirectorySource source(file_path.Parent());

    // 1. Import, in the calling thread or as a job.
    asset::ImportResult result;
    asset::GltfError error;
    asset::Status status;
    jobs::Scheduler::Config config;
    config.participants = 3;
    config.pin = false;
    jobs::Scheduler scheduler(config);
    if (line.HasFlag("async"))
    {
        Vector<byte> bytes;
        bytes.Resize(file.Size());
        MemCopy(bytes.Data(), file.Data(), file.Size());
        UniquePtr<asset::ImportTicket> ticket = asset::ImportGltfAsync(scheduler, static_cast<Vector<byte>&&>(bytes),
                                                                       source, options);
        status = ticket->Finish(&result, &error);
    }
    else
    {
        status = asset::ImportGltf(ByteSpan(file.Data(), file.Size()), source, options, &result, &error);
    }
    for (usize i = 0; i < result.report.notes.Size(); ++i)
    {
        os::PrintError("asset_demo: %s\n", result.report.notes[i].CStr());
    }
    if (status.Failed())
    {
        os::PrintError("asset_demo: the import was refused: %s\n", asset::GltfMessage(error, status));
        return 1;
    }

    // 2. The host's memory backend, mounted on a manager.
    asset::TypeRegistry types;
    asset::HostMemoryBackend backend(types, u64{1} << 30);
    if (asset::ImportIntoBackend(backend, result).Failed())
    {
        return Fail("the result could not be added to the memory backend");
    }
    asset::ManagerConfig manager_config;
    manager_config.buffer_pool_bytes = u64{256} << 20;
    asset::ResourceManager manager(types, manager_config);
    asset::RegisterCpuLoaders(&manager);
    if (manager.Mount(StringView("/"), &backend, 0).Failed() || manager.Mounts().RefreshTables().Failed())
    {
        return Fail("the backend could not be mounted");
    }

    // 3. Every resource: blobs verify, CPU resources load through the manager by path, by id and asynchronously.
    u32 meshes = 0;
    u32 skeletons = 0;
    u32 clips = 0;
    u32 textures = 0;
    u32 materials = 0;
    asset::AsyncLoader loader(manager, scheduler);
    for (usize i = 0; i < result.resources.Size(); ++i)
    {
        const asset::ImportedResource& r = result.resources[i];
        const ByteSpan blob(r.blob.Data(), r.blob.Size());
        if (r.type == asset::types::kMesh)
        {
            asset::Handle<asset::MeshView> by_path;
            asset::Handle<asset::MeshView> by_id;
            if (manager.LoadPath<asset::MeshView>(r.path.View(), &by_path).Failed() ||
                manager.Load<asset::MeshView>(r.id, &by_id).Failed() || by_path.Get() != by_id.Get() ||
                by_path.Get()->CheckIndices().Failed() || by_path.Get()->CheckSkin().Failed())
            {
                return Fail("a mesh did not load");
            }
            asset::LoadTicket ticket = loader.LoadAsync<asset::MeshView>(r.id);
            asset::Handle<asset::MeshView> async;
            if (loader.Wait(ticket).Failed() || ticket.Finish(&async).Failed() || async.Get() != by_id.Get())
            {
                return Fail("a mesh did not load asynchronously");
            }
            ++meshes;
        }
        else if (r.type == asset::types::kSkeleton)
        {
            asset::Handle<asset::SkeletonView> handle;
            if (manager.Load<asset::SkeletonView>(r.id, &handle).Failed())
            {
                return Fail("a skeleton did not load");
            }
            ++skeletons;
        }
        else if (r.type == asset::types::kAnimation)
        {
            asset::Handle<asset::ClipView> handle;
            if (manager.Load<asset::ClipView>(r.id, &handle).Failed())
            {
                return Fail("a clip did not load");
            }
            ++clips;
        }
        else if (r.type == asset::types::kTexture)
        {
            asset::TextureView view;
            if (view.Open(blob, types).Failed() || view.Id() != r.id)
            {
                return Fail("a texture blob does not verify");
            }
            ++textures;
        }
        else if (r.type == asset::types::kMaterial)
        {
            asset::MaterialAssetData data;
            asset::ResourceId id;
            if (asset::LoadMaterialBlob(blob, types, &data, &id).Failed() || id != r.id)
            {
                return Fail("a material blob does not verify");
            }
            ++materials;
        }
    }
    const asset::ResourceStats stats = manager.Stats();
    if (stats.load_conversions != 0)
    {
        return Fail("a load converted data (load_conversions must be 0)");
    }
    os::Print(
        "asset_demo: imported %u resources (%u meshes, %u skeletons, %u clips, %u textures, %u materials), %u warnings; "
        "loaded %u, load_conversions %u, %u bytes read\n",
        static_cast<unsigned>(result.resources.Size()), meshes, skeletons, clips, textures, materials,
        static_cast<unsigned>(result.report.warnings), static_cast<unsigned>(stats.loads),
        static_cast<unsigned>(stats.load_conversions), static_cast<unsigned>(stats.bytes_read));
    os::Print("asset_demo: ok\n");
    return 0;
}
