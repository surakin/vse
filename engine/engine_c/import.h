#pragma once
//! The glTF import of one engine (Phase 23 Day 351): `import_gltf` and the tickets of `import_gltf_async`. The importer
//! (`engine/asset_import`) is built into the library only when the build option `ENGINE_LIBRARY_WITH_IMPORTER` is on
//! (the default); without it every call fails with UNSUPPORTED and this header and the table are the same. Internal to
//! the module `engine_c`: only `import.cpp` includes the importer.
#include "asset/memory_backend.h"
#include "containers/unique_ptr.h"
#include "containers/vector.h"
#include "engine_c/engine_c.h"
#include "settings/registry.h"
#include "sync/atomic.h"
#include "sync/mutex.h"
#include "types.h"

namespace engine_placeholder::engine_c
{

class Runtime;
struct ImportObject;
struct ResourceConfig;

/// Whether this build of the library has the importer.
///
/// - Thread-safety: thread-safe.
/// - Fails: never.
bool ImporterBuilt();

/// Declares the `assets.import.*` settings when the importer is built in (nothing otherwise).
///
/// - Thread-safety: thread-compatible.
/// - Ownership: borrows `registry` for the call only.
void RegisterImporterSettings(settings::Registry* registry);

/// Reads the import limits of the registry into `config` when the importer is built in (nothing otherwise).
///
/// - Thread-safety: thread-compatible.
/// - Ownership: borrows its arguments for the call only.
void ReadImporterSettings(const settings::Registry& registry, ResourceConfig* config);

/// The imports of an engine: the synchronous call and the tickets of the asynchronous ones.
///
/// - Thread-safety: thread-safe; see the mutex.
/// - Ownership: owns the tickets; the backend it adds to is the resource host's, which outlives it.
class ImportHost
{
public:
    /// Makes the host.
    ///
    /// - Param runtime: the runtime whose job system runs asynchronous imports; it must outlive the host.
    /// - Param backend: where finished imports are added.
    /// - Param tables_dirty: set when content was added (the resource host reads its tables again).
    ImportHost(Runtime& runtime, asset::HostMemoryBackend& backend, Atomic<bool>* tables_dirty);
    /// Cancels the imports that are left and waits for their jobs.
    ~ImportHost();
    ImportHost(const ImportHost&) = delete;
    ImportHost& operator=(const ImportHost&) = delete;

    engine_status Import(const void* bytes, usize size, const engine_import_options* options,
                         engine_import_result* result);
    engine_status ImportAsync(const void* bytes, usize size, const engine_import_options* options,
                              engine_handle* ticket);
    engine_status Poll(engine_handle ticket, engine_import_info* out);
    engine_status Wait(engine_handle ticket, u64 timeout_ms);
    engine_status Finish(engine_handle ticket, engine_import_result* result);
    engine_status Cancel(engine_handle ticket);
    engine_status Destroy(engine_handle ticket);

private:
    struct Slot
    {
        UniquePtr<ImportObject> object;
        u32 generation = 0;
    };

#if ENGINE_C_WITH_IMPORTER
    Runtime* runtime_;
    asset::HostMemoryBackend* backend_;
    Atomic<bool>* tables_dirty_;
#endif
    Mutex mutex_;  // guards `slots_` and the objects' `waiters`
    Vector<Slot> slots_;
};

// The functions of the table that serve the imports (engine_c.cpp lists them in the table).
engine_status ImportGltfFn(engine_handle engine, const void* bytes, usize size, const engine_import_options* options,
                           engine_import_result* result);
engine_status ImportGltfAsyncFn(engine_handle engine, const void* bytes, usize size,
                                const engine_import_options* options, engine_handle* ticket);
engine_status ImportPollFn(engine_handle ticket, engine_import_info* out);
engine_status ImportWaitFn(engine_handle ticket, u64 timeout_ms);
engine_status ImportFinishFn(engine_handle ticket, engine_import_result* result);
engine_status ImportCancelFn(engine_handle ticket);
engine_status ImportDestroyFn(engine_handle ticket);

}  // namespace engine_placeholder::engine_c
