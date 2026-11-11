# Tutorial: Save a World Without Stalling the Frame

**Goal:** Describe where a world is saved with settings, save it to a SQLite file in the background, load it back into a fresh world, and let an autosaver keep the file current while the world runs.

**You need:** [Run Work in Parallel with Jobs](parallel-jobs.md). Background: [Storage](../guide/storage.md), [Settings](../guide/settings.md) and [Database](../guide/database.md).

## Steps

1. **Register the stored components and the settings.** `RegisterBuiltinComponents` adds the engine's own (transforms, the spatial components, the entity key); `RegisterStorageSettings` and
   `database::RegisterDatabaseSettings` declare `storage.*` and `database.*`. A program would then let the settings service fill them from a file, the environment and the command line; here
   the sample publishes `storage.path` itself.
2. **Open the storage from the settings.** `LocalStorage::OpenFromSettings` opens the database file, the SQL store (with or without column tables, by `storage.layout`) and the `WorldStorage` on top.
3. **Give every saved entity a key.** `EntityKeyComponent` is the durable identity: the file knows entities by key, never by entity id.
4. **Save in the background.** `WorldStorage::SaveAsync` encodes the world on the workers and returns; a writer job puts the records in the file. The world is free to change as soon as the call
   returns, because what is written is the world as the call found it. `Wait` (or `Finish`, once `IsDone`) reports the result.
5. **Load in the background.** `LoadAsync` reads the records on a job; `Wait` decodes them and fills the world on your thread, and the world is untouched if anything fails.
6. **Autosave.** `Autosaver::Update` once a frame saves only what changed, every few seconds, and never blocks on the disk.

## Complete code

```cpp compile run standalone
#include "database/database_settings.h"
#include "database/sqlite/sqlite_database.h"
#include "ecs/transform.h"
#include "io/file_system.h"
#include "io/path.h"
#include "jobs/scheduler.h"
#include "os/console.h"
#include "storage/async_storage.h"
#include "storage/builtin_components.h"
#include "storage/identity.h"
#include "storage/local_storage.h"

using namespace engine_placeholder;
using namespace engine_placeholder::storage;

int main() {
    io::Path tmp;
    io::Path scratch;
    io::TempDirectory(&tmp);
    io::CreateTempDirectory(tmp, "tutorial", &scratch);
    const io::Path file = scratch / "world.db";

    // Step 1: the components and the settings.
    ComponentRegistry components;
    if (RegisterBuiltinComponents(&components).Failed()) return 1;
    settings::Registry settings;
    RegisterStorageSettings(&settings);
    database::RegisterDatabaseSettings(&settings);
    settings.Publish(settings.Find("storage.path"), settings::Value::FromText(file.View()));

    // Step 2: the storage the settings describe.
    database::SqliteDatabase backend;
    UniquePtr<LocalStorage> storage;
    if (LocalStorage::OpenFromSettings(backend, settings, components, &storage).Failed()) return 1;

    jobs::Scheduler scheduler(jobs::Scheduler::Config{3, false});

    // Step 3: a world of keyed entities.
    ecs::World world;
    KeyGenerator keys = KeyGenerator::Seeded(1);
    for (u32 i = 0; i < 1000; ++i) {
        ecs::LocalTransform local;
        local.position = Vec3(static_cast<f32>(i), 0, 0);
        world.Create(EntityKeyComponent{keys.Next()}, local);
    }
    KeyIndex index;

    // Step 4: save in the background; the world may change while the writer works.
    UniquePtr<AsyncSave> save;
    if (storage->Worlds().SaveAsync(scheduler, world, index, SaveOptions(), &save).Failed()) return 1;
    Report saved;
    if (save->Wait(&saved).Failed()) return 1;
    os::Print("saved %zu entities\n", saved.created);

    // Step 5: load into a fresh world.
    ecs::World loaded;
    KeyIndex loaded_index;
    UniquePtr<AsyncLoad> load;
    if (storage->Worlds().LoadAsync(scheduler, LoadOptions(), &load).Failed()) return 1;
    Report read;
    if (load->Wait(loaded, loaded_index, &read).Failed()) return 1;
    os::Print("loaded %zu entities\n", read.created);

    // Step 6: autosave. Change a few entities, run some frames, and see that only they are written.
    Autosaver autosaver(storage->Worlds(), scheduler, world, index, 0.1);
    for (u32 frame = 0; frame < 30; ++frame) {
        if (frame == 5) {
            world.Query<ecs::Write<ecs::LocalTransform>>().ForEach([](ecs::Entity, ecs::LocalTransform& t) { t.position.y = 1.0f; });
        }
        if (autosaver.Update(1.0 / 60.0).Failed()) return 1;
        world.EndFrame();
    }
    if (autosaver.Flush().Failed()) return 1;
    os::Print("autosaves: %s\n", autosaver.Completed() >= 2 ? "at least two" : "fewer than two");

    io::RemoveAll(scratch);
    return 0;
}
```

## Expected output

```output
saved 1000 entities
loaded 1000 entities
autosaves: at least two
```

## Where next

- [Storage](../guide/storage.md): the stored format, incremental saves, the column layout and the settings.
- The `storage_demo` program (`game/storage_demo`) saves a city with the spatial sets, loads it into a fresh world and checks that both answer the same queries.
